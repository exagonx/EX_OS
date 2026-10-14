/* =============================================================================
 * kernel/arch/x86/acpi.c — powering off and rebooting a real PC (ACPI)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Until 14 October 2026 `shutdown` turned off emulators only (fixed ports of
 * QEMU, Bochs, VirtualBox: see power.c) and a real PC stopped on "E' ora
 * sicuro spegnere il computer" - the user's MCP77 laptop "just hangs". And
 * `reboot` knew the keyboard controller and a triple fault, which some
 * laptops ignore. The real ways are ACPI's:
 *
 *   - RSDP (in the BIOS area) -> RSDT or XSDT -> FADT ("FACP");
 *   - the FADT gives the PM1a/PM1b control ports, SMI_CMD and ACPI_ENABLE
 *     (to switch the chipset to ACPI mode if the firmware left it in legacy
 *     mode), the DSDT, and - from revision 2 - the RESET register;
 *   - the DSDT is AML bytecode; S5's sleep types are the two first values of
 *     the package named _S5_. They are found by the usual pattern, NameOp
 *     "_S5_" PackageOp, not by interpreting AML: no AML interpreter here.
 *   - power off = write SLP_TYP | SLP_EN to PM1a (and PM1b) control.
 *
 * acpi_init() reads all this at boot and LOGS it, so `dmesg` on a machine
 * says what shutdown will do there, before anyone tries it.
 *
 * The physical memory of the tables is read like smp.c reads the MADT: below
 * 1 MB directly, above through the paging window, a page at a time.
 * ============================================================================= */
#include "kernel.h"
#include "paging.h"
#include "pmm.h"
#include "acpi.h"

static struct {
    int      trovata;            /* a FADT was found                         */
    uint32_t pm1a_cnt, pm1b_cnt; /* control ports (I/O space)                */
    uint32_t smi_cmd;
    uint8_t  acpi_enable;
    uint16_t slp_a, slp_b;       /* SLP_TYP for S5, already shifted (<< 10)  */
    int      s5;                 /* the _S5_ package was found               */
    int      reset;              /* the reset register can be used           */
    uint8_t  reset_spazio;       /* 1 = I/O, 2 = PCI configuration           */
    uint64_t reset_ind;
    uint8_t  reset_val;
} g_acpi;

/* --- physical memory ------------------------------------------------------------ */
static void fis_copia(paddr_t fis, void *dove, uint32_t n)
{
    uint8_t *d = (uint8_t *)dove;

    while (n > 0) {
        uint32_t in_pagina = PAGE_SIZE - (uint32_t)(fis & (PAGE_SIZE - 1));
        uint32_t quanti    = n < in_pagina ? n : in_pagina, i;

        if (fis + quanti <= 0x100000u) {
            volatile const uint8_t *s = (volatile const uint8_t *)(uintptr_t)fis;
            for (i = 0; i < quanti; i++) d[i] = s[i];
        } else {
            const uint8_t *s = (const uint8_t *)paging_finestra_apri(fis);
            for (i = 0; i < quanti; i++) d[i] = s[i];
            paging_finestra_chiudi();
        }
        d += quanti; fis += quanti; n -= quanti;
    }
}

static uint8_t fis_somma(paddr_t fis, uint32_t n)
{
    uint8_t buf[64], somma = 0;

    while (n > 0) {
        uint32_t quanti = n < sizeof(buf) ? n : (uint32_t)sizeof(buf), i;

        fis_copia(fis, buf, quanti);
        for (i = 0; i < quanti; i++) somma = (uint8_t)(somma + buf[i]);
        fis += quanti; n -= quanti;
    }
    return somma;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p) | ((uint64_t)le32(p + 4) << 32); }

static int uguali(const uint8_t *a, const char *b, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) if (a[i] != (uint8_t)b[i]) return 0;
    return 1;
}

/* the RSDP: 16-byte aligned, in the first KB of the EBDA or in 0xE0000-0xFFFFF */
static paddr_t cerca_rsdp(void)
{
    uint8_t  b[20];
    uint32_t ebda, zone[2][2], z, p;

    fis_copia(0x40E, b, 2);
    ebda = (uint32_t)(b[0] | (b[1] << 8)) << 4;
    zone[0][0] = (ebda >= 0x80000u && ebda < 0xA0000u) ? ebda : 0;
    zone[0][1] = zone[0][0] + 1024;
    zone[1][0] = 0xE0000u;
    zone[1][1] = 0x100000u;
    for (z = 0; z < 2; z++) {
        if (zone[z][0] == 0) continue;
        for (p = zone[z][0]; p + 20 <= zone[z][1]; p += 16) {
            fis_copia(p, b, 8);
            if (uguali(b, "RSD PTR ", 8) && fis_somma(p, 20) == 0) return p;
        }
    }
    return 0;
}

/* a table by signature, from the RSDT (4-byte entries) or the XSDT (8-byte) */
static paddr_t cerca_tabella(paddr_t sdt, int larga, const char *firma)
{
    uint8_t  t[36], v[8];
    uint32_t lung, n, i, passo = larga ? 8u : 4u;

    fis_copia(sdt, t, 36);
    lung = le32(t + 4);
    if (lung < 36 || lung > 0x10000u || fis_somma(sdt, lung) != 0) return 0;
    n = (lung - 36) / passo;
    for (i = 0; i < n; i++) {
        paddr_t tab;

        fis_copia(sdt + 36 + i * passo, v, passo);
        tab = larga ? (paddr_t)le64(v) : (paddr_t)le32(v);
        if (tab == 0) continue;
        fis_copia(tab, t, 4);
        if (uguali(t, firma, 4)) return tab;
    }
    return 0;
}

/* _S5_ in the DSDT: NameOp (0x08, maybe after a '\') "_S5_" PackageOp (0x12)
 * PkgLength NumElements, then SLP_TYPa and SLP_TYPb as BytePrefix (0x0A) +
 * byte, or ZeroOp / OneOp. Read a page at a time with a 16-byte overlap so
 * a pattern across two pages is not missed. */
static void cerca_s5(paddr_t dsdt, uint32_t lung)
{
    uint8_t  buf[4096 + 32];
    uint32_t pos = 36;

    while (pos + 8 < lung) {
        uint32_t quanti = lung - pos < 4096u + 32u ? lung - pos : 4096u + 32u, i;

        fis_copia(dsdt + pos, buf, quanti);
        for (i = 2; i + 12 < quanti; i++) {
            uint8_t *s = buf + i;
            uint32_t k;

            if (!uguali(s, "_S5_", 4) || s[4] != 0x12) continue;
            if (!(s[-1] == 0x08 || (s[-2] == 0x08 && s[-1] == '\\'))) continue;
            k = 5 + ((s[5] & 0xC0u) >> 6) + 2;          /* PkgLength + NumElements */
            if (s[k] == 0x0A) k++;
            g_acpi.slp_a = (uint16_t)((s[k] & 7u) << 10);
            k++;
            if (s[k] == 0x0A) k++;
            g_acpi.slp_b = (uint16_t)((s[k] & 7u) << 10);
            g_acpi.s5 = 1;
            return;
        }
        if (quanti < 4096u + 32u) break;
        pos += 4096;
    }
}

void acpi_init(void)
{
    uint8_t  r[36], f[244];
    paddr_t  rsdp, sdt, fadt, dsdt;
    uint32_t flen;
    int      larga = 0;

    g_acpi.trovata = 0;
    rsdp = cerca_rsdp();
    if (rsdp == 0) { klog(LOG_INFO, "ACPI: nessuna tabella RSDP: spegnimento e riavvio senza ACPI"); return; }
    fis_copia(rsdp, r, 36);
    sdt = (paddr_t)le32(r + 16);
    if (r[15] >= 2 && fis_somma(rsdp, 36) == 0 && le64(r + 24) != 0) {
        sdt = (paddr_t)le64(r + 24);                     /* the XSDT, ACPI 2.0+ */
        larga = 1;
    }
    fadt = sdt ? cerca_tabella(sdt, larga, "FACP") : 0;
    if (fadt == 0 && larga) fadt = cerca_tabella((paddr_t)le32(r + 16), 0, "FACP");
    if (fadt == 0) { klog(LOG_WARN, "ACPI: RSDP a 0x%x ma nessuna FADT", (uint32_t)rsdp); return; }

    fis_copia(fadt, f, 36);
    flen = le32(f + 4);
    if (flen > sizeof(f)) flen = sizeof(f);
    fis_copia(fadt, f, flen);

    g_acpi.smi_cmd     = flen > 52 ? le32(f + 48) : 0;
    g_acpi.acpi_enable = flen > 52 ? f[52] : 0;
    g_acpi.pm1a_cnt    = flen > 68 ? le32(f + 64) : 0;
    g_acpi.pm1b_cnt    = flen > 72 ? le32(f + 68) : 0;
    g_acpi.trovata     = g_acpi.pm1a_cnt != 0;

    /* the reset register: FADT revision 2+, flag RESET_REG_SUP (bit 10) */
    if (flen >= 129 && (le32(f + 112) & (1u << 10))) {
        g_acpi.reset_spazio = f[116];
        g_acpi.reset_ind    = le64(f + 120);
        g_acpi.reset_val    = f[128];
        g_acpi.reset = (g_acpi.reset_spazio == 1 || g_acpi.reset_spazio == 2) && g_acpi.reset_ind != 0;
    }

    dsdt = (paddr_t)le32(f + 40);
    if (flen >= 148 && le64(f + 140) != 0) dsdt = (paddr_t)le64(f + 140);
    if (dsdt) {
        uint8_t h[8];

        fis_copia(dsdt, h, 8);
        if (uguali(h, "DSDT", 4) && le32(h + 4) > 36 && le32(h + 4) < 0x100000u)
            cerca_s5(dsdt, le32(h + 4));
    }

    klog(LOG_INFO, "ACPI: FADT a 0x%x, PM1a_CNT 0x%x, PM1b_CNT 0x%x, SMI_CMD 0x%x/%02x",
         (uint32_t)fadt, g_acpi.pm1a_cnt, g_acpi.pm1b_cnt, g_acpi.smi_cmd, g_acpi.acpi_enable);
    if (g_acpi.s5)
        klog(LOG_INFO, "ACPI: S5 (spegnimento) SLP_TYP a=%u b=%u", g_acpi.slp_a >> 10, g_acpi.slp_b >> 10);
    else
        klog(LOG_WARN, "ACPI: nella DSDT non trovo _S5_: lo spegnimento non si puo' fare");
    if (g_acpi.reset)
        klog(LOG_INFO, "ACPI: registro di reset nello spazio %u, 0x%x, valore 0x%02x",
             g_acpi.reset_spazio, (uint32_t)g_acpi.reset_ind, g_acpi.reset_val);
}

void acpi_spegni(void)
{
    uint32_t i;

    if (!g_acpi.trovata || !g_acpi.s5) return;

    /* SCI_EN (bit 0 of PM1 control) off = the chipset is still in legacy
     * mode: ask the firmware to switch, through SMI_CMD, and wait a bounded
     * while (counted reads: interrupts may be off here). */
    if (!(port_inw((uint16_t)g_acpi.pm1a_cnt) & 1u) && g_acpi.smi_cmd && g_acpi.acpi_enable) {
        port_outb((uint16_t)g_acpi.smi_cmd, g_acpi.acpi_enable);
        for (i = 0; i < 3000000u && !(port_inw((uint16_t)g_acpi.pm1a_cnt) & 1u); i++) { }
    }

    port_outw((uint16_t)g_acpi.pm1a_cnt, (uint16_t)(g_acpi.slp_a | (1u << 13)));
    if (g_acpi.pm1b_cnt)
        port_outw((uint16_t)g_acpi.pm1b_cnt, (uint16_t)(g_acpi.slp_b | (1u << 13)));
    for (i = 0; i < 3000000u; i++) __asm__ volatile ("pause");
}

void acpi_riavvia(void)
{
    uint32_t i;

    if (!g_acpi.reset) return;
    if (g_acpi.reset_spazio == 1) {
        port_outb((uint16_t)g_acpi.reset_ind, g_acpi.reset_val);
    } else {
        /* PCI configuration space of bus 0: device in bits 32-47, function
         * in 16-31, register in 0-15 (ACPI's GAS encoding) */
        uint32_t dev = (uint32_t)(g_acpi.reset_ind >> 32) & 0x1Fu;
        uint32_t fn  = (uint32_t)(g_acpi.reset_ind >> 16) & 0x7u;
        uint32_t reg = (uint32_t)g_acpi.reset_ind & 0xFFu;

        port_outl(0xCF8, 0x80000000u | (dev << 11) | (fn << 8) | (reg & 0xFCu));
        port_outb((uint16_t)(0xCFC + (reg & 3u)), g_acpi.reset_val);
    }
    for (i = 0; i < 3000000u; i++) __asm__ volatile ("pause");
}
