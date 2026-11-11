/* =============================================================================
 * drivers/nv40/nv40.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * /dev/nv40.drv - NVIDIA NV4x (Curie): GeForce 6/7 and the IGPs C51, C61,
 * C73 (11 November 2026, voce 99 a). Born on the .247: MCP73 with a C73
 * (10de:07e5, "GeForce 7100 / nForce 630i"), PMC_BOOT_0 0x063000a2.
 *
 * STEP 1 (this): -stato READS. Chip, memory, crystal, and the mode the BIOS
 * left on each head, decoded from the VGA CRTC plus NVIDIA's extended CRs
 * and the video PLL. On the .247 it must say 800x600, 32 bit, ~40 MHz: if it
 * doesn't, the registers aren't what we think, and nothing gets written.
 * -edid reads the monitor over DDC (bit-banged I2C on CR 0x3E/0x3F).
 *
 * Source: xf86-video-nv (nv_hw.c, nv_dac.c, nv_setup.c), written by NVIDIA
 * and released under the MIT licence. Read, not copied.
 *
 * ! nv50.drv IS NOT FOR THIS CARD. G80 has the EVO display at 0x610000; on a
 *   NV4x that whole range reads zero (seen on the .247 on 9/11). NV4x is the
 *   old CRTC display: PCRTC 0x600000, PCIO 0x601000 (VGA ports in MMIO),
 *   PRAMDAC 0x680000; head 1 is +0x2000 on each.
 * ! QEMU HAS NO NVIDIA: real hardware only. No card: says so and exits.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"
#include "accel_proto.h"

/* +0.001 at every change: `nv40.drv -version` prints it. */
EX_VERSIONE("nv40.drv", "0.005");

#define NV_VENDITORE   0x10DE

/* Only what step 1 needs from the 16 MB of BAR0. */
typedef struct {
    unsigned int   base, byte;
    volatile unsigned char *v;
} Finestra;

static Finestra g_fin[] = {
    { 0x000000, 0x2000 },   /* PMC: the chip, 0x1540 units, 0x1700 RAMIN window */
    { 0x002000, 0x2000 },   /* PFIFO */
    { 0x008000, 0x1000 },   /* PMC 0x87xx-0x89xx (xf86-video-nv sets them) */
    { 0x400000, 0x8000 },   /* PGRAPH */
    { 0x710000, 0x3000 },   /* PRAMIN: hash table, objects, RAMFC */
    { 0x800000, 0x1000 },   /* the user area of channel 0: PUT, GET */
    { 0x100000, 0x1000 },   /* PFB: memory */
    { 0x101000, 0x1000 },   /* PEXTDEV: the straps (crystal) */
    { 0x600000, 0x1000 },   /* PCRTC head 0 */
    { 0x601000, 0x1000 },   /* PCIO head 0: VGA 0x3C0-0x3DF */
    { 0x602000, 0x1000 },   /* PCRTC head 1 */
    { 0x603000, 0x1000 },   /* PCIO head 1 */
    { 0x680000, 0x1000 },   /* PRAMDAC head 0: PLLs, general, flat panel */
    { 0x682000, 0x1000 },   /* PRAMDAC head 1 */
    { 0x0C0000, 0x1000 },   /* PRMVIO: VGA misc (sync polarity) and sequencer */
};
#define N_FIN (sizeof(g_fin) / sizeof(g_fin[0]))

static unsigned int g_bar0, g_bar1;
#define PB_OFFSET_  0x00E00000u      /* the push buffer, in video memory */
static unsigned int g_rubata;   /* the stolen RAM the BAR1 shows, 0 = not found */
static unsigned int g_cpu_fb;   /* where the CPU writes video memory: g_rubata or g_bar1 */

static volatile unsigned char *byte_di(unsigned int reg)
{
    unsigned int i;

    for (i = 0; i < N_FIN; i++)
        if (g_fin[i].v && reg >= g_fin[i].base && reg < g_fin[i].base + g_fin[i].byte)
            return g_fin[i].v + (reg - g_fin[i].base);
    return 0;
}

static unsigned int rd(unsigned int reg)
{
    volatile unsigned char *p = byte_di(reg);

    return p ? *(volatile unsigned int *)p : 0xDEADBEEF;
}

static unsigned char rd8(unsigned int reg)
{
    volatile unsigned char *p = byte_di(reg);

    return p ? *p : 0xFF;
}

static void wr(unsigned int reg, unsigned int v)
{
    volatile unsigned char *p = byte_di(reg);

    if (p) *(volatile unsigned int *)p = v;
}

static void wr8(unsigned int reg, unsigned char v)
{
    volatile unsigned char *p = byte_di(reg);

    if (p) *p = v;
}

/* VGA CRTC register `i` of head `t`, through its own PCIO: no CR44 games. */
static unsigned char cr(int t, unsigned char i)
{
    unsigned int pcio = 0x601000 + (unsigned int)t * 0x2000;

    wr8(pcio + 0x3D4, i);
    return rd8(pcio + 0x3D5);
}

static void cr_scrivi(int t, unsigned char i, unsigned char v)
{
    unsigned int pcio = 0x601000 + (unsigned int)t * 0x2000;

    wr8(pcio + 0x3D4, i);
    wr8(pcio + 0x3D5, v);
}

/* --- the card, from the PCI service ------------------------------------------ */

static int chiedi_pci(int pid, unsigned int ordinale, PciDispositivo *out)
{
    PciRichiesta  r;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           t;

    r.ordinale    = ordinale;
    r.classe      = 0x03;
    r.sottoclasse = PCI_QUALUNQUE;
    r.venditore   = PCI_QUALUNQUE;
    r.dispositivo = PCI_QUALUNQUE;

    if (ipc_send((unsigned int)pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0) return -1;
    for (t = 0; t < 8; t++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
        if ((int)meta.sender_pid != pid) continue;
        if (meta.tipo == PCI_MSG_FINE) return 0;
        if (meta.tipo == PCI_MSG_DISPOSITIVO && meta.len >= sizeof(*out)) {
            memcpy(out, buf, sizeof(*out));
            return 1;
        }
        return -1;
    }
    return -1;
}

static int g_zitto;
#define TRACCIA(...)  do { if (!g_zitto) printf(__VA_ARGS__); } while (0)

static int trova_scheda(PciDispositivo *d)
{
    int          pid = ipc_attendi(PCI_SERVIZIO, 5000);
    unsigned int ord;
    int          n = 0;

    if (pid <= 0) {
        printf("nv40: il servizio PCI non c'e'. Avvialo:  /dev/pci.drv &\n");
        return -1;
    }
    for (ord = 0; ord < 8; ord++) {
        n = chiedi_pci(pid, ord, d);
        if (n != 1 || d->venditore == NV_VENDITORE) break;
    }
    if (n != 1) {
        TRACCIA("nv40: nessuna scheda video NVIDIA su questa macchina\n");
        return -1;
    }
    if (d->bar[0] == 0 || d->bar_io[0]) {
        printf("nv40: %04x:%04x senza BAR0 di memoria\n", d->venditore, d->dispositivo);
        return -1;
    }
    return 0;
}

static int mappa(void)
{
    unsigned int i;

    for (i = 0; i < N_FIN; i++) {
        MmioZona m;
        int      rc;

        m.fisico = g_bar0 + g_fin[i].base;
        m.byte   = g_fin[i].byte;
        rc = mmio_map(&m);
        if (rc != 0) {
            printf("nv40: mmio_map(0x%08x, 0x%x) rifiutata (%d)\n", m.fisico, m.byte, rc);
            return -1;
        }
        g_fin[i].v = (volatile unsigned char *)(unsigned long)m.virt;
    }
    return 0;
}

/* The architecture byte of PMC_BOOT_0. NV4x: 0x40-0x4F, and the late ones
 * (G70 family and the C73 IGP) 0x60-0x6F. G80 is 0x50 and later, 0x80+:
 * that's nv50.drv. */
static unsigned int architettura(void) { return (rd(0x000000) >> 20) & 0xFF; }

static int e_nv4x(unsigned int a)
{
    return (a >= 0x40 && a <= 0x4F) || (a >= 0x60 && a <= 0x6F);
}

/* The IGPs take their "video memory" from system RAM; the size the chipset
 * gave it is in PFB 0x20C (xf86-video-nv, NVCommonSetup). */
static int integrata(unsigned int a)
{
    return a == 0x4C || a == 0x4E || a == 0x63 || a == 0x67 || a == 0x68;
}

/* ! BITS 22 AND 6 TOGETHER ARE 25 MHz (nouveau, nvkm_device_ctor), and
 *   xf86-video-nv doesn't know it: it reads bit 22 alone and says 27. On
 *   the .247 that made the BIOS's 800x600 a 65 Hz mode; with 25 it's the
 *   VESA 60 Hz to the kHz. */
static unsigned int cristallo_khz(void)
{
    switch (rd(0x101000) & 0x00400040u) {
    case 0x00000000u: return 13500;
    case 0x00000040u: return 14318;
    case 0x00400000u: return 27000;
    default:          return 25000;
    }
}

/* --- the video BIOS: the PLL table ---------------------------------------------
 *
 * ! THE PLL REFERENCE IS NOT THE CRYSTAL on the IGPs. On the .247 the strap
 *   says 27 MHz, and with 27 the BIOS's own 800x600 came out at 65 Hz:
 *   bullshit, the VESA mode is 60. The video BIOS has a PLL table with the
 *   true reference (nouveau, nvbios_pll_parse): with 25 MHz head 0 gives
 *   40.18 MHz and head 1 the old 25.175 of VGA. An IGP has no ROM chip: its
 *   video BIOS exists only as the image at 0xC0000 (kernel 0.327 lets a
 *   driver map it). */
static volatile unsigned char *g_rom;
static unsigned int g_rom_byte;

static unsigned int r8(unsigned int o)  { return o < g_rom_byte ? g_rom[o] : 0; }
static unsigned int r16(unsigned int o) { return r8(o) | (r8(o + 1) << 8); }
static unsigned int r32(unsigned int o) { return r16(o) | (r16(o + 2) << 16); }

static int rom_apri(int verboso)
{
    MmioZona m;
    unsigned int pcir;

    if (g_rom) return 0;
    m.fisico = 0xC0000;
    m.byte   = 0x10000;
    if (mmio_map(&m) != 0) {
        if (verboso) printf("nv40: la ROM video a 0xC0000 non si mappa (serve il kernel 0.327)\n");
        return -1;
    }
    g_rom = (volatile unsigned char *)(unsigned long)m.virt;
    g_rom_byte = 0x10000;
    if (r8(0) != 0x55 || r8(1) != 0xAA) {
        if (verboso) printf("nv40: a 0xC0000 c'e' %02x %02x, non 55 aa\n", r8(0), r8(1));
        g_rom = 0;
        return -1;
    }
    if (r8(2) * 512u < g_rom_byte) g_rom_byte = r8(2) * 512u;
    pcir = r16(0x18);
    if (r32(pcir) != 0x52494350u || r16(pcir + 4) != NV_VENDITORE) {   /* "PCIR" */
        if (verboso) printf("nv40: la ROM a 0xC0000 (%u KB) e' di %04x:%04x, non di una NVIDIA\n",
                            r8(2) / 2, r16(pcir + 4), r16(pcir + 6));
        g_rom = 0;
        return -1;
    }
    return 0;
}

/* "\xff\xb8BIT": header, then entries {id, version, length(2), offset(2)}. */
static unsigned int bit_cerca(char id, unsigned int *ver, unsigned int *lun)
{
    unsigned int o, n, passo, e;

    for (o = 0; o + 12 < g_rom_byte; o++)
        if (r8(o) == 0xFF && r8(o + 1) == 0xB8 && r8(o + 2) == 'B' &&
            r8(o + 3) == 'I' && r8(o + 4) == 'T')
            break;
    if (o + 12 >= g_rom_byte) return 0;
    passo = r8(o + 9);
    n     = r8(o + 10);
    for (e = o + 12; n--; e += passo)
        if (r8(e) == (unsigned char)id) {
            *ver = r8(e + 1);
            *lun = r16(e + 2);
            return r16(e + 4);
        }
    return 0;
}

typedef struct {
    unsigned int rif_khz;              /* 0 = not found */
    unsigned int vco_min, vco_max, in_min, in_max;
    unsigned int m_min, m_max, n_min, n_max, p_max;
    unsigned int vco2_min, vco2_max, n2_min, n2_max, m2_min, m2_max;
    unsigned int versione;
} PllLimiti;

static int pll_limiti(unsigned int reg, PllLimiti *l)
{
    unsigned int bv, bl, c, tab, ver, hdr, lun, cnt, e, d;

    memset(l, 0, sizeof(*l));
    if (rom_apri(0) != 0) return -1;
    c = bit_cerca('C', &bv, &bl);
    if (c == 0) return -1;
    tab = (bv == 1 && bl >= 10) ? r16(c + 8) : (bv == 2 && bl >= 4) ? r32(c) : 0;
    if (tab == 0) return -1;
    ver = r8(tab); hdr = r8(tab + 1); lun = r8(tab + 2); cnt = r8(tab + 3);
    l->versione = ver;
    if (ver == 0x20 || ver == 0x21) {
        /* No type byte: the entry whose first dword is the register, or
         * the first one (pll_map_reg). The reference only if the entry is
         * long enough, else the crystal. */
        unsigned int prima = tab + hdr;

        for (e = prima; cnt--; e += lun)
            if (r32(e) == reg) break;
        if (r32(e) != reg) e = prima;
        l->vco_min = r16(e + 4) * 1000;  l->vco_max = r16(e + 6) * 1000;
        l->in_min  = r16(e + 12) * 1000; l->in_max  = r16(e + 16) * 1000;
        l->n_min = r8(e + 20); l->n_max = r8(e + 21);
        l->m_min = r8(e + 22); l->m_max = r8(e + 23);
        l->p_max = r8(e + 29);
        l->vco2_min = r16(e + 8) * 1000; l->vco2_max = r16(e + 10) * 1000;
        l->n2_min = r8(e + 24); l->n2_max = r8(e + 25);
        l->m2_min = r8(e + 26); l->m2_max = r8(e + 27);
        l->rif_khz = lun > 0x22 ? r32(e + 31) : 0;
        if (l->rif_khz == 0) l->rif_khz = cristallo_khz();
        return 0;
    }
    if (ver < 0x30) return -1;
    for (e = tab + hdr; cnt--; e += lun) {
        if (r32(e + 3) != reg) continue;
        if (ver == 0x40) {
            l->rif_khz = r16(e + 9) * 1000;
            d = r16(e + 1);
            l->vco_min = r16(d + 0) * 1000; l->vco_max = r16(d + 2) * 1000;
            l->in_min  = r16(d + 4) * 1000; l->in_max  = r16(d + 6) * 1000;
            l->m_min = r8(d + 8);  l->m_max = r8(d + 9);
            l->n_min = r8(d + 10); l->n_max = r8(d + 11);
            l->p_max = r8(d + 13);
        } else if (ver == 0x30) {
            d = r16(e + 1);
            l->vco_min = r16(d + 0) * 1000; l->vco_max = r16(d + 2) * 1000;
            l->in_min  = r16(d + 8) * 1000; l->in_max  = r16(d + 12) * 1000;
            l->n_min = r8(d + 16); l->n_max = r8(d + 17);
            l->m_min = r8(d + 18); l->m_max = r8(d + 19);
            l->p_max = r8(d + 25);
            l->rif_khz = r32(d + 28);
        }
        return l->rif_khz ? 0 : -1;
    }
    return -1;
}

typedef struct {
    int          tipo;              /* 0 or 4 */
    unsigned int scrivi, leggi;     /* CR indices, or the MMIO offset */
} Bus;


static int dcb_leggi(Bus *b, int *nb, int stampa);

static int bios(void)
{
    static const unsigned int regs[2] = { 0x680508, 0x680520 };
    PllLimiti l;
    int       i;

    if (rom_apri(1) != 0) return 1;
    printf("ROM video: %u KB a 0xC0000, NVIDIA %04x\n", g_rom_byte / 1024, r16(r16(0x18) + 6));
    for (i = 0; i < 2; i++) {
        if (pll_limiti(regs[i], &l) != 0) {
            printf("VPLL%d (0x%06x): non trovato nella tabella (versione 0x%02x)\n", i, regs[i], l.versione);
            continue;
        }
        printf("VPLL%d (0x%06x): riferimento in tabella %u kHz, VCO %u-%u kHz, ingresso %u-%u kHz,\n"
               "                 M %u-%u, N %u-%u, P fino a %u (tabella 0x%02x)\n"
               "                 seconda fase: VCO %u-%u kHz, M %u-%u, N %u-%u\n",
               i, regs[i], l.rif_khz, l.vco_min, l.vco_max, l.in_min, l.in_max,
               l.m_min, l.m_max, l.n_min, l.n_max, l.p_max, l.versione,
               l.vco2_min, l.vco2_max, l.m2_min, l.m2_max, l.n2_min, l.n2_max);
    }
    {
        Bus b[16];
        int nb;

        dcb_leggi(b, &nb, 1);
    }
    return 0;
}

/* The reference of the PLLs. ! NOT THE TABLE'S on the .247: its v0x21 entry
 * says 14318 kHz, and with that the BIOS's 800x600 would run at 23 MHz,
 * which no monitor takes. The crystal (25 MHz from the straps) gives the
 * VESA 40.18 MHz on head 0 and the VGA 25.175 on head 1: that's the truth.
 * The table gives the limits (VCO, M, N, P). */
static unsigned int riferimento_khz(unsigned int reg)
{
    (void)reg;
    return cristallo_khz();
}

/* --- one head, decoded --------------------------------------------------------- */

typedef struct {
    unsigned int htot, hdisp, hss, hse, vtot, vdisp, vss, vse;
    unsigned int bpp, passo, inizio, khz;
    unsigned int vpll, vpllb, general, owner, rif;
} Modo;

/* VCLK = crystal * N1/M1 (* N2/M2 if the second stage is on) >> P. */
static unsigned int pll_khz(unsigned int rif, unsigned int a, unsigned int b)
{
    unsigned int m1 = a & 0xFF, n1 = (a >> 8) & 0xFF, p = (a >> 16) & 7;
    unsigned int f = rif;                 /* 27000 * 255 * 255 fits in 32 bit */

    if (m1 == 0) return 0;
    f = f * n1 / m1;
    if (b & 0x80000000u) {
        unsigned int m2 = b & 0xFF, n2 = (b >> 8) & 0xFF;

        if (m2 == 0) return 0;
        f = f * n2 / m2;
    }
    return f >> p;
}

/* The inverse of NVDACInit: standard VGA fields + bit 8 in CR2D, bit 10 in
 * CR25, bit 11 in CR41. */
static void leggi_modo(int t, Modo *m)
{
    unsigned char c[0x19], x25, x2d, x41, x28, x19;
    unsigned int  i, ht, hd, hs, he, vt, vd, vs, ve;
    unsigned int  ramdac = 0x680000 + (unsigned int)t * 0x2000;

    for (i = 0; i < 0x19; i++) c[i] = cr(t, (unsigned char)i);
    x19 = cr(t, 0x19); x25 = cr(t, 0x25); x28 = cr(t, 0x28);
    x2d = cr(t, 0x2D); x41 = cr(t, 0x41);

    ht = c[0x0] | ((x2d & 0x01u) << 8);
    hd = c[0x1] | ((x2d & 0x02u) << 7);
    hs = c[0x4] | ((x2d & 0x08u) << 5);
    he = c[0x5] & 0x1F;
    vt = c[0x6] | ((c[0x7] & 0x01u) << 8) | ((c[0x7] & 0x20u) << 4) |
         ((x25 & 0x01u) << 10) | ((x41 & 0x01u) << 11);
    vd = c[0x12] | ((c[0x7] & 0x02u) << 7) | ((c[0x7] & 0x40u) << 3) |
         ((x25 & 0x02u) << 9) | ((x41 & 0x04u) << 9);
    vs = c[0x10] | ((c[0x7] & 0x04u) << 6) | ((c[0x7] & 0x80u) << 2) |
         ((x25 & 0x04u) << 8) | ((x41 & 0x10u) << 7);
    ve = c[0x11] & 0x0F;

    m->htot  = (ht + 5) * 8;
    m->hdisp = (hd + 1) * 8;
    m->hss   = (hs + 1) * 8;
    m->hse   = (((hs & ~0x1Fu) | he) + 1) * 8;
    if (m->hse < m->hss) m->hse += 32 * 8;
    m->vtot  = vt + 2;
    m->vdisp = vd + 1;
    m->vss   = vs + 1;
    m->vse   = ((vs & ~0x0Fu) | ve) + 1;
    if (m->vse < m->vss) m->vse += 16;

    switch (x28 & 3) {
    case 1:  m->bpp = 8;  break;
    case 2:  m->bpp = 16; break;
    case 3:  m->bpp = 32; break;
    default: m->bpp = 0;  break;     /* VGA text / planar */
    }
    /* CR13 + CR19 bits 7:5: the pitch in units of 8 bytes. */
    m->passo  = ((unsigned int)c[0x13] | (((unsigned int)x19 & 0xE0u) << 3)) * 8;
    m->inizio = rd(0x600800 + (unsigned int)t * 0x2000);
    m->vpll   = rd(0x680000 + (t ? 0x520u : 0x508u));
    m->vpllb  = rd(0x680000 + (t ? 0x57Cu : 0x578u));
    m->rif    = riferimento_khz(t ? 0x680520u : 0x680508u);
    m->khz    = pll_khz(m->rif, m->vpll, m->vpllb);
    m->general = rd(ramdac + 0x600);
    m->owner  = 0;
}

static void scrivi_modo(int t, const Modo *m)
{
    printf("Testa %d:\n", t);
    if (m->bpp == 0) {
        printf("  modo VGA (testo o planare), %ux%u caratteri/pixel\n", m->hdisp, m->vdisp);
    } else {
        unsigned int hz = (m->htot && m->vtot)
                          ? m->khz * 1000u / (m->htot * m->vtot)
                          : 0;

        printf("  %ux%u a %u bit, %u Hz\n", m->hdisp, m->vdisp, m->bpp, hz);
    }
    printf("  orizzontale  visibile %u, sync %u-%u, totale %u\n", m->hdisp, m->hss, m->hse, m->htot);
    printf("  verticale    visibile %u, sync %u-%u, totale %u\n", m->vdisp, m->vss, m->vse, m->vtot);
    printf("  orologio     %u kHz (VPLL 0x%08x, seconda fase 0x%08x, riferimento %u kHz)\n",
           m->khz, m->vpll, m->vpllb, m->rif);
    printf("  riga         %u byte, inizio nella memoria video 0x%08x\n", m->passo, m->inizio);
    printf("  RAMDAC       generale 0x%08x\n", m->general);
}

static int stato(void)
{
    unsigned int a = architettura(), mem_kb, lock;
    Modo         m;
    int          t;

    printf("Chip: PMC_BOOT_0 = 0x%08x (architettura 0x%02x), cristallo %u kHz (strap 0x%08x)\n",
           rd(0x000000), a, cristallo_khz(), rd(0x101000));
    if (!e_nv4x(a)) {
        printf("nv40: non e' una NV4x (0x%02x): %s\n", a,
               a >= 0x50 ? "per le G80 e dopo c'e' nv50.drv" : "troppo vecchia, non la conosco");
        return 1;
    }
    mem_kb = (rd(0x10020C) & 0xFFF00000u) >> 10;
    printf("Memoria video: %u MB %s (PFB 0x20C = 0x%08x, PFB 0x200 = 0x%08x)\n",
           mem_kb / 1024, integrata(a) ? "presi dalla RAM di sistema" : "sulla scheda",
           rd(0x10020C), rd(0x100200));
    printf("Finestra della memoria video (BAR1): 0x%08x\n", g_bar1);
    if (integrata(a))
        printf(g_rubata ? "La RAM della GPU: 0x%08x (trovata col segno: il processore la scrive li')\n"
                        : "La RAM della GPU: non trovata, il processore passa dalla BAR1%.0x\n", g_rubata);

    /* ! THE EXTENDED CRs ARE LOCKED until CR1F says 0x57. Read the lock,
     * open it, put it back as it was: the BIOS and VBE expect their state. */
    lock = cr(0, 0x1F);
    cr_scrivi(0, 0x1F, 0x57);
    printf("Proprietario della VGA (CR44): 0x%02x, lucchetto CR1F era 0x%02x\n", cr(0, 0x44), lock);
    for (t = 0; t < 2; t++) {
        if (t == 1) {
            lock = cr(1, 0x1F);
            cr_scrivi(1, 0x1F, 0x57);
        }
        leggi_modo(t, &m);
        scrivi_modo(t, &m);
        printf("  PCRTC        configurazione 0x%08x, testa 0x%08x\n",
               rd(0x600804 + (unsigned int)t * 0x2000), rd(0x600860 + (unsigned int)t * 0x2000));
        if (lock != 0x03 && lock != 0x57) cr_scrivi(t, 0x1F, 0x99);
    }
    printf("PRAMDAC 0x50C (scelta dei PLL): 0x%08x\n", rd(0x68050C));
    return 0;
}

/* --- -edid: the monitor over DDC --------------------------------------------
 *
 * Two kinds of bus on these chips (DCB i2c table, nouveau bios/i2c.c):
 *   type 0 (NV04): CR `scrivi` drives (SCL bit 5, SDA bit 4, bit 0 always
 *          set), CR `leggi` senses (SCL bit 2, SDA bit 3);
 *   type 4 (NV4E, the IGPs C51/C61/C73): ONE MMIO register at 0x600800 +
 *          drive: SCL with mask 0x2f (0x21/0x01), SDA with mask 0x1f
 *          (0x11/0x01), sensed on bits 18 and 19.
 * ! ON THE .247 THE TWO CR BUSES OF xf86-video-nv (0x3E, 0x36) ANSWERED
 *   NOTHING: an IGP may wire the monitor to a type 4 bus. The DCB says. */
static Bus g_bus;

/* ! usleep(10) IS A WHOLE TICK HERE, not 10 us: 128 bytes of EDID took
 * minutes, and a bus with SCL stuck low (CR 0x93 on the .247) hung the
 * stretching wait for 100 ticks a bit. A TSC wait, calibrated once. */
static unsigned int g_tsc_us;

static unsigned int tsc(void)
{
    unsigned int basso, alto;

    __asm__ volatile ("rdtsc" : "=a"(basso), "=d"(alto));
    (void)alto;
    return basso;
}

static void attendi_us(unsigned int us)
{
    unsigned int t0;

    if (g_tsc_us == 0) {
        t0 = tsc();
        usleep(50000);
        g_tsc_us = (tsc() - t0) / 50000;
        if (g_tsc_us == 0) g_tsc_us = 1;
    }
    t0 = tsc();
    while (tsc() - t0 < us * g_tsc_us) __asm__ volatile ("pause");
}

static void ddc_metti(int scl, int sda)
{
    if (g_bus.tipo == 4) {
        unsigned int r = 0x600800 + g_bus.scrivi, v = rd(r);

        v = (v & ~0x2Fu) | (scl ? 0x21u : 0x01u);
        v = (v & ~0x1Fu) | (sda ? 0x11u : 0x01u) | (v & 0x20u);
        wr(r, v);
    } else {
        unsigned char v = cr(0, (unsigned char)g_bus.scrivi) & 0xF0;

        if (scl) v |= 0x20; else v &= (unsigned char)~0x20;
        if (sda) v |= 0x10; else v &= (unsigned char)~0x10;
        cr_scrivi(0, (unsigned char)g_bus.scrivi, (unsigned char)(v | 1));
    }
    attendi_us(10);
}

static int ddc_sda(void)
{
    if (g_bus.tipo == 4) return (rd(0x600800 + g_bus.scrivi) & 0x00080000u) != 0;
    return (cr(0, (unsigned char)g_bus.leggi) & 0x08) != 0;
}

static int ddc_scl(void)
{
    if (g_bus.tipo == 4) return (rd(0x600800 + g_bus.scrivi) & 0x00040000u) != 0;
    return (cr(0, (unsigned char)g_bus.leggi) & 0x04) != 0;
}

static void i2c_start(void) { ddc_metti(1, 1); ddc_metti(1, 0); ddc_metti(0, 0); }
static void i2c_stop(void)  { ddc_metti(0, 0); ddc_metti(1, 0); ddc_metti(1, 1); }

static int i2c_manda(unsigned char b)
{
    int i, ack;

    for (i = 7; i >= 0; i--) {
        int bit = (b >> i) & 1;

        ddc_metti(0, bit);
        ddc_metti(1, bit);
        for (ack = 0; ack < 200 && !ddc_scl(); ack++) attendi_us(5);   /* stretching, 1 ms */
        ddc_metti(0, bit);
    }
    ddc_metti(0, 1);
    ddc_metti(1, 1);
    ack = !ddc_sda();
    ddc_metti(0, 1);
    return ack;
}

static unsigned char i2c_prendi(int ack)
{
    unsigned char b = 0;
    int           i;

    for (i = 0; i < 8; i++) {
        ddc_metti(0, 1);
        ddc_metti(1, 1);
        b = (unsigned char)((b << 1) | ddc_sda());
    }
    ddc_metti(0, ack ? 0 : 1);
    ddc_metti(1, ack ? 0 : 1);
    ddc_metti(0, 1);
    return b;
}

/* The DCB: 0 = found. Up to 16 buses into `b`, outputs printed if `stampa`. */
static int dcb_leggi(Bus *b, int *nb, int stampa)
{
    unsigned int dcb, ver, hdr, cnt, lun, i2c, i, e;

    *nb = 0;
    if (rom_apri(0) != 0) return -1;
    dcb = r16(0x36);
    if (dcb == 0) return -1;
    ver = r8(dcb);
    if (ver < 0x30 || ver >= 0x42 || r32(dcb + 6) != 0x4EDCBDCBu) {
        if (stampa) printf("DCB a 0x%04x: versione 0x%02x che non conosco\n", dcb, ver);
        return -1;
    }
    hdr = r8(dcb + 1); cnt = r8(dcb + 2); lun = r8(dcb + 3);
    if (stampa) {
        static const char *tipi[] = { "VGA analogica", "TV", "DVI/HDMI (TMDS)", "LVDS" };

        printf("DCB %u.%u a 0x%04x: %u uscite\n", ver >> 4, ver & 15, dcb, cnt);
        for (i = 0, e = dcb + hdr; i < cnt; i++, e += lun) {
            unsigned int c = r32(e), t = c & 0xF;

            if (t == 0xE || t == 0xF) break;           /* EOL / unused */
            printf("  uscita %u: %s, bus I2C %u, teste 0x%x, connettore %u, OR 0x%x\n",
                   i, t < 4 ? tipi[t] : "altro", (c >> 4) & 0xF, (c >> 8) & 0xF,
                   (c >> 12) & 0xF, (c >> 24) & 0xF);
        }
    }
    i2c = r16(dcb + 4);
    if (i2c == 0) return 0;
    ver = r8(i2c); hdr = r8(i2c + 1); cnt = r8(i2c + 2); lun = r8(i2c + 3);
    for (i = 0, e = i2c + hdr; i < cnt && *nb < 16; i++, e += lun) {
        unsigned int t = r8(e + 3);

        if (t == 0) { b[*nb].tipo = 0; b[*nb].scrivi = r8(e); b[*nb].leggi = r8(e + 1); }
        else if (t == 4) { b[*nb].tipo = 4; b[*nb].scrivi = r8(e + 1); b[*nb].leggi = 0; }
        else continue;
        if (stampa && t == 4)
            printf("  bus I2C %u: tipo NV4E, registro 0x600800 + 0x%02x\n", i, b[*nb].scrivi);
        else if (stampa)
            printf("  bus I2C %u: tipo NV04, CR 0x%02x (scrive) / CR 0x%02x (legge)\n",
                   i, b[*nb].scrivi, b[*nb].leggi);
        (*nb)++;
    }
    return 0;
}

static int edid_bus(const Bus *b, unsigned char *e)
{
    int i;

    g_bus = *b;
    i2c_stop();
    i2c_start();
    if (!i2c_manda(0xA0) || !i2c_manda(0x00)) { i2c_stop(); return -1; }
    i2c_start();
    if (!i2c_manda(0xA1)) { i2c_stop(); return -1; }
    for (i = 0; i < 128; i++) e[i] = i2c_prendi(i < 127);
    i2c_stop();
    return 0;
}

static void edid_scrivi(const unsigned char *e)
{
    unsigned int pclk = (unsigned int)(e[54] | (e[55] << 8)) * 10;
    unsigned int hw = e[56] | ((e[58] & 0xF0u) << 4);
    unsigned int vw = e[59] | ((e[61] & 0xF0u) << 4);
    int          i, somma;

    for (i = 0, somma = 0; i < 128; i++) somma += e[i];
    printf("  EDID %s, costruttore %c%c%c, prodotto 0x%04x, versione %u.%u, %s\n",
           (somma & 0xFF) == 0 && e[0] == 0 && e[1] == 0xFF ? "valido" : "con la somma sbagliata",
           '@' + ((e[8] >> 2) & 0x1F), '@' + (((e[8] & 3) << 3) | (e[9] >> 5)), '@' + (e[9] & 0x1F),
           (unsigned)(e[10] | (e[11] << 8)), e[18], e[19],
           (e[20] & 0x80) ? "digitale" : "analogico");
    printf("  modo preferito %ux%u a %u kHz\n", hw, vw, pclk);
    for (i = 72; i <= 108; i += 18)
        if (e[i] == 0 && e[i + 1] == 0 && e[i + 3] == 0xFC) {
            char n[14];
            int  k;

            for (k = 0; k < 13 && e[i + 5 + k] != 0x0A; k++) n[k] = (char)e[i + 5 + k];
            n[k] = '\0';
            printf("  nome '%s'\n", n);
        }
    printf("  ");
    for (i = 0; i < 128; i++) printf("%02x%s", e[i], (i & 31) == 31 ? "\n  " : "");
    printf("\n");
}

static int edid(void)
{
    Bus           b[18];
    unsigned char e[128], lock;
    int           nb = 0, i, trovati = 0;

    {
        VideoScheda vs;

        if (video_scheda(&vs) == 1 && vs.edid_ok) {
            printf("dal BIOS all'avvio (Stage 2, VBE/DDC): %s %s\n", vs.fornitore, vs.prodotto);
            edid_scrivi(vs.edid);
        } else {
            printf("dal BIOS all'avvio (Stage 2, VBE/DDC): nessun EDID\n");
        }
    }
    lock = cr(0, 0x1F);
    cr_scrivi(0, 0x1F, 0x57);
    if (dcb_leggi(b, &nb, 0) != 0 || nb == 0)
        printf("(niente DCB: provo i bus CR 0x3E e 0x36 di xf86-video-nv)\n");
    /* the two classic CR buses always, after the DCB's */
    b[nb].tipo = 0; b[nb].scrivi = 0x3F; b[nb].leggi = 0x3E; nb++;
    b[nb].tipo = 0; b[nb].scrivi = 0x37; b[nb].leggi = 0x36; nb++;
    for (i = 0; i < nb; i++) {
        if (b[i].tipo == 4) printf("bus %d (0x600800 + 0x%02x): ", i, b[i].scrivi);
        else                printf("bus %d (CR 0x%02x / 0x%02x): ", i, b[i].scrivi, b[i].leggi);
        if (edid_bus(&b[i], e) != 0) { printf("nessun monitor risponde\n"); continue; }
        /* ! A BUS WITH SDA STUCK LOW "ACKS" EVERYTHING and reads zeros
         * (CR 0x93 on the .247): without the header it's not a monitor. */
        if (e[0] != 0 || e[1] != 0xFF || e[2] != 0xFF || e[7] != 0) {
            printf("risponde, ma non e' un EDID (linea SDA bloccata?)\n");
            continue;
        }
        printf("un monitor\n");
        edid_scrivi(e);
        trovati++;
    }
    if (lock != 0x03 && lock != 0x57) cr_scrivi(0, 0x1F, 0x99);
    return trovati ? 0 : 1;
}

/* =============================================================================
 * STEP 2: -prova LxA[@Hz] and -modo LxA[@Hz] - a mode of our own on head 0
 *
 * ! ONLY WHAT A MODE NEEDS, the rest stays the BIOS's: timings (VGA CR +
 *   CR25/2D/41), pitch (CR13/19), the VPLL, sync polarity, the FIFO. No
 *   PFIFO, no PMC, no PGRAPH: they're working, and NVLoadStateExt resets them
 *   for its own reasons. Every register touched is saved first, and -prova
 *   puts them back after N seconds: a black screen fixes itself.
 * ! THE KERNEL FIRST, THEN THE CARD (as nv50.drv): video_cambia moves the
 *   console onto the new shape; if it says no (the desktop holds the
 *   graphics) the card is never touched.
 * ============================================================================= */
typedef struct {
    unsigned int  w, h, hz, khz;
    unsigned int  hss, hse, ht, vss, vse, vt;
    int           hneg, vneg;
} Tempi;

/* VESA DMT and CVT (reduced blanking where DMT has it) */
static const Tempi TEMPI[] = {
    {  640,  480, 60,  25175,  656,  752,  800,  490,  492,  525, 1, 1 },
    {  800,  600, 60,  40000,  840,  968, 1056,  601,  605,  628, 0, 0 },
    { 1024,  768, 60,  65000, 1048, 1184, 1344,  771,  777,  806, 1, 1 },
    { 1024,  768, 75,  78750, 1040, 1136, 1312,  769,  772,  800, 0, 0 },
    { 1152,  864, 75, 108000, 1216, 1344, 1600,  865,  868,  900, 0, 0 },
    { 1280,  720, 60,  74250, 1390, 1430, 1650,  725,  730,  750, 0, 0 },
    { 1280,  800, 60,  83500, 1352, 1480, 1680,  803,  809,  831, 1, 0 },
    { 1280,  960, 60, 108000, 1376, 1488, 1800,  961,  964, 1000, 0, 0 },
    { 1280, 1024, 60, 108000, 1328, 1440, 1688, 1025, 1028, 1066, 0, 0 },
    { 1280, 1024, 75, 135000, 1296, 1440, 1688, 1025, 1028, 1066, 0, 0 },
    { 1360,  768, 60,  85500, 1424, 1536, 1792,  771,  777,  795, 0, 0 },
    { 1440,  900, 60, 106500, 1520, 1672, 1904,  903,  909,  934, 1, 0 },
    { 1600, 1200, 60, 162000, 1664, 1856, 2160, 1201, 1204, 1250, 0, 0 },
    { 1680, 1050, 60, 146250, 1784, 1960, 2240, 1053, 1059, 1089, 1, 0 },
    { 1920, 1080, 60, 148500, 2008, 2052, 2200, 1084, 1089, 1125, 0, 0 },
};
#define N_TEMPI (sizeof(TEMPI) / sizeof(TEMPI[0]))

/* The two-stage VPLL nearest to `khz` inside the BIOS's limits.
 * f = rif * N1/M1 * N2/M2 >> P; VCO1 = rif*N1/M1, VCO2 = VCO1*N2/M2. */
static int pll_cerca(unsigned int khz, unsigned int *a, unsigned int *b, unsigned int *esce)
{
    PllLimiti    l;
    unsigned int rif = cristallo_khz(), m1, n1, m2, n2, p, migliore = 0xFFFFFFFFu;

    if (pll_limiti(0x680508, &l) != 0) {
        /* no table: the values the BIOS used for 800x600 are inside these */
        l.vco_min = 150000; l.vco_max = 300000; l.in_min = 2000; l.in_max = 27000;
        l.m_min = 2; l.m_max = 13; l.n_min = 30; l.n_max = 200; l.p_max = 6;
        l.vco2_min = 400000; l.vco2_max = 1000000;
        l.m2_min = 1; l.m2_max = 31; l.n2_min = 1; l.n2_max = 31;
    }
    if (l.vco2_max == 0) { l.vco2_min = 400000; l.vco2_max = 1000000; }
    if (l.m2_max == 0 || l.m2_max > 31) l.m2_max = 31;
    if (l.n2_max == 0 || l.n2_max > 31) l.n2_max = 31;
    if (l.m2_min == 0) l.m2_min = 1;
    if (l.n2_min == 0) l.n2_min = 1;
    for (p = 0; p <= l.p_max && p <= 6; p++)
        for (m1 = l.m_min ? l.m_min : 1; m1 <= l.m_max && m1 <= 255; m1++) {
            if (rif / m1 < l.in_min) break;
            if (rif / m1 > l.in_max) continue;
            for (n1 = l.n_min; n1 <= l.n_max && n1 <= 255; n1++) {
                unsigned int vco1 = rif * n1 / m1;

                if (vco1 < l.vco_min) continue;
                if (vco1 > l.vco_max) break;
                for (m2 = l.m2_min; m2 <= l.m2_max; m2++)
                    for (n2 = l.n2_min; n2 <= l.n2_max; n2++) {
                        unsigned int vco2 = vco1 * n2 / m2, f, d;

                        if (vco2 < l.vco2_min || vco2 > l.vco2_max) continue;
                        unsigned int marg1, marg2, punti;

                        f = vco2 >> p;
                        d = f > khz ? f - khz : khz - f;
                        if (d > khz / 200) continue;            /* 0.5%, as the BIOS */
                        /* ! THE MARGIN FIRST, NOT THE EXACT FREQUENCY. The
                         * first 1024x768 had VCO1 = 300 MHz, the very top of
                         * the ROM's range, and VCO2 = 520 at the bottom: the
                         * PLL unlocked now and then and the monitor lost the
                         * picture like a loose cable (the .247, 11/11). The
                         * BIOS keeps VCO1 at 250 and takes 0.4% of error.
                         * Points: the worst margin of the two VCOs, in
                         * thousandths of their range, minus the error in
                         * thousandths. */
                        marg1 = (vco1 - l.vco_min < l.vco_max - vco1 ? vco1 - l.vco_min : l.vco_max - vco1)
                                * 1000 / (l.vco_max - l.vco_min);
                        marg2 = (vco2 - l.vco2_min < l.vco2_max - vco2 ? vco2 - l.vco2_min : l.vco2_max - vco2)
                                * 1000 / (l.vco2_max - l.vco2_min);
                        punti = 1000 + (marg1 < marg2 ? marg1 : marg2) - d * 1000 / khz * 4;
                        if (migliore == 0xFFFFFFFFu || punti > migliore) {
                            migliore = punti;
                            *a = (p << 16) | (n1 << 8) | m1;
                            *b = 0x80000000u | (n2 << 8) | m2;
                            *esce = f;
                        }
                    }
            }
        }
    return migliore != 0xFFFFFFFFu ? 0 : -1;    /* something within 0.5% */
}

/* Everything step 2 writes, to put it back. */
static const unsigned char CR_SALVATI[] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09, 0x10, 0x12, 0x13,
    0x15, 0x16, 0x19, 0x1A, 0x1B, 0x1C, 0x20, 0x25, 0x2D, 0x41, 0x47, 0x11,
};
#define N_CR (sizeof(CR_SALVATI) / sizeof(CR_SALVATI[0]))

typedef struct {
    unsigned char cr[N_CR], misc;
    unsigned int  vpll, vpllb, inizio, r830, r834;
    VideoInfo     video;
} Salvati;

static void salva(Salvati *s)
{
    unsigned int i;

    for (i = 0; i < N_CR; i++) s->cr[i] = cr(0, CR_SALVATI[i]);
    s->misc   = rd8(0x0C03CC);
    s->vpll   = rd(0x680508);
    s->vpllb  = rd(0x680578);
    s->inizio = rd(0x600800);
    s->r830   = rd(0x600830);
    s->r834   = rd(0x600834);
    video_info(&s->video);
}

static void schermo(int acceso)         /* SR01 bit 5: the screen off while we change it */
{
    unsigned char v;

    wr8(0x0C03C4, 0x01);
    v = rd8(0x0C03C5);
    wr8(0x0C03C5, (unsigned char)(acceso ? v & ~0x20 : v | 0x20));
}

static void ripristina(const Salvati *s)
{
    unsigned int i;

    schermo(0);
    cr_scrivi(0, 0x11, (unsigned char)(cr(0, 0x11) & 0x7F));   /* CR0-7 writable */
    for (i = 0; i < N_CR; i++) cr_scrivi(0, CR_SALVATI[i], s->cr[i]);   /* CR11 last */
    wr8(0x0C03C2, s->misc);
    wr(0x680508, s->vpll);
    wr(0x680578, s->vpllb);
    wr(0x600800, s->inizio);
    wr(0x600830, s->r830);
    wr(0x600834, s->r834);
    schermo(1);
}

static void scrivi_tempi(const Tempi *t, unsigned int passo, unsigned int a, unsigned int b)
{
    /* the inverse of leggi_modo, as NVDACInit */
    unsigned int hd = t->w / 8 - 1, hs = t->hss / 8 - 1, he = t->hse / 8 - 1;
    unsigned int ht = t->ht / 8 - 5, hbs = hd, hbe = t->ht / 8 - 1;
    unsigned int vd = t->h - 1, vs = t->vss - 1, ve = t->vse - 1;
    unsigned int vt = t->vt - 2, vbs = vd, vbe = t->vt - 1;
    unsigned int pitch8 = passo / 8;

    cr_scrivi(0, 0x11, (unsigned char)(cr(0, 0x11) & 0x7F));
    cr_scrivi(0, 0x00, (unsigned char)ht);
    cr_scrivi(0, 0x01, (unsigned char)hd);
    cr_scrivi(0, 0x02, (unsigned char)hbs);
    cr_scrivi(0, 0x03, (unsigned char)((hbe & 0x1F) | 0x80));
    cr_scrivi(0, 0x04, (unsigned char)hs);
    cr_scrivi(0, 0x05, (unsigned char)(((hbe & 0x20) << 2) | (he & 0x1F)));
    cr_scrivi(0, 0x06, (unsigned char)vt);
    cr_scrivi(0, 0x07, (unsigned char)(((vt >> 8) & 1) | (((vd >> 8) & 1) << 1) |
                                       (((vs >> 8) & 1) << 2) | (((vbs >> 8) & 1) << 3) | 0x10 |
                                       (((vt >> 9) & 1) << 5) | (((vd >> 9) & 1) << 6) |
                                       (((vs >> 9) & 1) << 7)));
    cr_scrivi(0, 0x09, (unsigned char)((cr(0, 0x09) & ~0x20u & ~0x80u) | (((vbs >> 9) & 1) << 5) | 0x40));
    cr_scrivi(0, 0x10, (unsigned char)vs);
    cr_scrivi(0, 0x12, (unsigned char)vd);
    cr_scrivi(0, 0x13, (unsigned char)pitch8);
    cr_scrivi(0, 0x15, (unsigned char)vbs);
    cr_scrivi(0, 0x16, (unsigned char)vbe);
    cr_scrivi(0, 0x19, (unsigned char)((cr(0, 0x19) & 0x1F) | ((pitch8 & 0x700) >> 3)));
    cr_scrivi(0, 0x1A, (unsigned char)((cr(0, 0x1A) & ~0x04u) | (t->w < 1280 ? 0x04 : 0x00)));
    cr_scrivi(0, 0x25, (unsigned char)(((hbe >> 6) & 1) << 4 | ((vbs >> 10) & 1) << 3 |
                                       ((vs >> 10) & 1) << 2 | ((vd >> 10) & 1) << 1 | ((vt >> 10) & 1)));
    cr_scrivi(0, 0x2D, (unsigned char)(((ht >> 8) & 1) | ((hd >> 8) & 1) << 1 |
                                       ((hbs >> 8) & 1) << 2 | ((hs >> 8) & 1) << 3));
    cr_scrivi(0, 0x41, (unsigned char)(((vt >> 11) & 1) | ((vd >> 11) & 1) << 2 |
                                       ((vs >> 11) & 1) << 4 | ((vbs >> 11) & 1) << 6));
    /* ! THE FIFO OF THE IGPs: fixed, there's no memory clock to compute it
     * from: it's the RAM's. Low watermark 0x480 as xf86-video-nv (C51, C61,
     * MCP67); 0x580 and 0x780 changed nothing on the .247.
     * ! BUT THE BURST AS THE BIOS (0x85), NOT xf86-video-nv's 128: with
     * bursts of 0 the display starved whenever Exilla scrolled and the
     * monitor flickered like a loose cable. 5 = much better, 6 = the
     * screen in pieces (stripes, too much for this FIFO). Fucked range. */
    cr_scrivi(0, 0x1B, 0x85);
    cr_scrivi(0, 0x20, 0x80);
    cr_scrivi(0, 0x47, (unsigned char)((cr(0, 0x47) & ~0x07u) | 0x04));
    wr8(0x0C03C2, (unsigned char)((rd8(0x0C03CC) & 0x3F) | (t->hneg ? 0x40 : 0) | (t->vneg ? 0x80 : 0)));
    wr(0x680508, a);
    wr(0x680578, b);
    wr(0x600800, 0);
    wr(0x600830, t->h - 3);
    wr(0x600834, t->h - 1);
    cr_scrivi(0, 0x11, (unsigned char)((ve & 0x0F) | 0x20));    /* last: protection off */
}

static volatile unsigned char *g_fb;

/* =============================================================================
 * trova_rubata — the RAM behind the BAR1 (11 November 2026)
 *
 * ! ON THE IGPs THERE IS NO VIDEO MEMORY: it's system RAM the BIOS keeps
 * for the GPU (PFB 0x20C says how much), and through the BAR1 every pixel
 * goes CPU -> card -> RAM. nv50.drv learned it on the CQ60: same pixels at
 * the RAM address, many times faster. The G80 says the base in 0x100E10;
 * the NV4x IGPs don't say it anywhere we know of, so it's MEASURED: a mark
 * written through the BAR1, looked for at the 16 MB steps under the BAR1,
 * from the top down. The first refusal of mmio_map is real RAM: stop there.
 * Then a second mark, to be sure it isn't a leftover.
 * ============================================================================= */
static int trova_rubata(void)
{
    const unsigned int off = 0x00E08000u;    /* after the push buffer: nobody's */
    unsigned int dim = rd(0x10020C) & 0xFFF00000u, b, giro;
    MmioZona     m1, m2;
    volatile unsigned int *via_bar, *via_ram;

    g_cpu_fb = g_bar1;
    if (dim == 0 || g_bar1 < dim) return -1;
    m1.fisico = g_bar1 + off; m1.byte = 4096;
    if (mmio_map(&m1) != 0) return -1;
    via_bar = (volatile unsigned int *)(unsigned long)m1.virt;
    via_bar[0] = 0x4E563430u ^ (unsigned int)uptime_ms();
    via_bar[1] = ~via_bar[0];
    (void)via_bar[0];
    for (b = (g_bar1 - dim) & ~0xFFFFFFu; b >= 0x10000000u; b -= 0x1000000u) {
        m2.fisico = b + off; m2.byte = 4096;
        if (mmio_map(&m2) != 0) break;
        via_ram = (volatile unsigned int *)(unsigned long)m2.virt;
        if (via_ram[0] != via_bar[0] || via_ram[1] != via_bar[1]) continue;
        for (giro = 0; giro < 2; giro++) {
            via_bar[0] = 0x9E3779B9u * (giro + 2) ^ (unsigned int)uptime_ms();
            (void)via_bar[0];
            if (via_ram[0] != via_bar[0]) break;
        }
        if (giro == 2) { g_rubata = b; g_cpu_fb = b; return 0; }
    }
    return -1;
}

/* The VRAM offset of a physical address, through either door. */
static int vram_off(unsigned int fisico, unsigned int *off)
{
    if (fisico >= g_bar1 && fisico - g_bar1 < PB_OFFSET_) { *off = fisico - g_bar1; return 0; }
    if (g_rubata && fisico >= g_rubata && fisico - g_rubata < PB_OFFSET_) { *off = fisico - g_rubata; return 0; }
    return -1;
}

static int fb_mappa(unsigned int byte)
{
    MmioZona m;

    m.fisico = g_cpu_fb;
    m.byte   = byte;
    if (mmio_map(&m) != 0) { printf("nv40: mmio_map della memoria video rifiutata\n"); return -1; }
    g_fb = (volatile unsigned char *)(unsigned long)m.virt;
    return 0;
}

/* Bars, a white frame 1 pixel thick on the edges, and a diagonal: on the
 * monitor one sees at once if a line or a column is missing. */
static void motivo(unsigned int w, unsigned int h, unsigned int passo)
{
    static const unsigned int barre[8] = {
        0xFFFFFF, 0xFFFF00, 0x00FFFF, 0x00FF00, 0xFF00FF, 0xFF0000, 0x0000FF, 0x202020
    };
    unsigned int x, y;

    for (y = 0; y < h; y++) {
        volatile unsigned int *r = (volatile unsigned int *)(g_fb + y * passo);

        for (x = 0; x < w; x++) {
            unsigned int c = barre[x * 8 / w];

            if (y >= h * 3 / 4) c = (x * 255 / w) * 0x010101;     /* grey ramp */
            if (x == 0 || y == 0 || x == w - 1 || y == h - 1) c = 0xFFFFFF;
            if (x == y * w / h) c = 0xFF0000;
            r[x] = c;
        }
    }
}

static const Tempi *tempi_di(const char *a)
{
    unsigned int w = 0, h = 0, hz = 0, i;
    const char  *q = a;

    w = (unsigned int)atoi(q);
    while (*q && *q != 'x') q++;
    if (*q == 'x') h = (unsigned int)atoi(++q);
    while (*q && *q != '@') q++;
    if (*q == '@') hz = (unsigned int)atoi(++q);
    for (i = 0; i < N_TEMPI; i++)
        if (TEMPI[i].w == w && TEMPI[i].h == h && (hz == 0 || TEMPI[i].hz == hz)) return &TEMPI[i];
    return 0;
}

static void elenco_modi(void)
{
    unsigned int i;

    printf("I modi che nv40.drv conosce (VESA DMT/CVT):\n");
    for (i = 0; i < N_TEMPI; i++)
        printf("  %ux%u@%u   %u kHz\n", TEMPI[i].w, TEMPI[i].h, TEMPI[i].hz, TEMPI[i].khz);
}

static int cambia_modo(const char *quale, int secondi)
{
    const Tempi *t = tempi_di(quale);
    unsigned int a, b, f, passo, mem_kb, lock;
    Salvati      sv;
    VideoCambio  vc;

    if (!t) { printf("nv40: modo '%s' sconosciuto\n", quale); elenco_modi(); return 1; }
    if (pll_cerca(t->khz, &a, &b, &f) != 0) {
        printf("nv40: nessun PLL da %u kHz entro i limiti della ROM\n", t->khz);
        return 1;
    }
    passo  = (t->w * 4 + 255) & ~255u;      /* 256-byte aligned: the 2D engine wants it */
    mem_kb = (rd(0x10020C) & 0xFFF00000u) >> 10;
    if (passo * t->h > mem_kb * 1024 || passo * t->h > 16u * 1024 * 1024) {
        printf("nv40: %ux%u non ci sta nella memoria video\n", t->w, t->h);
        return 1;
    }
    TRACCIA("nv40: %ux%u@%u, orologio %u kHz (chiesto %u): VPLL 0x%08x 0x%08x, riga %u byte\n",
           t->w, t->h, t->hz, f, t->khz, a, b, passo);
    if (fb_mappa(passo * t->h) != 0) return 1;

    lock = cr(0, 0x1F);
    cr_scrivi(0, 0x1F, 0x57);
    salva(&sv);
    TRACCIA("nv40: il BIOS aveva FIFO CR1B 0x%02x, CR20 0x%02x, CR47 0x%02x, CR1C 0x%02x\n",
           cr(0, 0x1B), cr(0, 0x20), cr(0, 0x47), cr(0, 0x1C));

    vc.fisico = g_cpu_fb; vc.larghezza = t->w; vc.altezza = t->h; vc.passo = passo; vc.bit = 32;
    if (video_cambia(&vc) != 0) {
        printf("nv40: il kernel non passa al framebuffer nuovo (errno %d%s): non tocco la scheda.\n",
               errno, errno == 16 ? ", la scrivania tiene la grafica: chiudila" : "");
        if (lock != 0x03 && lock != 0x57) cr_scrivi(0, 0x1F, 0x99);
        return 1;
    }
    schermo(0);
    scrivi_tempi(t, passo, a, b);
    if (secondi > 0) motivo(t->w, t->h, passo);
    schermo(1);
    {
        Modo m;

        leggi_modo(0, &m);
        TRACCIA("nv40: riletto %ux%u a %u bit, totale %ux%u, %u kHz, riga %u byte\n",
               m.hdisp, m.vdisp, m.bpp, m.htot, m.vtot, m.khz, m.passo);
    }
    if (secondi > 0) {
        printf("nv40: il motivo di prova per %d secondi, poi torno a %ux%u\n",
               secondi, sv.video.larghezza, sv.video.altezza);
        sleep((unsigned int)secondi);
        ripristina(&sv);
        vc.fisico = sv.video.fisico; vc.larghezza = sv.video.larghezza;
        vc.altezza = sv.video.altezza; vc.passo = sv.video.passo; vc.bit = sv.video.bit;
        if (video_cambia(&vc) != 0) printf("nv40: ! il kernel non e' tornato al framebuffer di prima\n");
        printf("nv40: tornato al modo di prima\n");
    }
    if (lock != 0x03 && lock != 0x57) cr_scrivi(0, 0x1F, 0x99);
    return 0;
}

/* =============================================================================
 * STEP 3: THE 2D ENGINE (-2dprova, -2dmisura, -2dservizio)
 *
 * As xf86-video-nv does it on NV40 (NVLoadStateExt + NVResetGraphics): one
 * channel (0) in DMA mode, a 32 KB push buffer in video memory, the objects
 * in PRAMIN (hash table at PRAMIN+0x10000 = BAR0 0x710000, RAMFC at
 * +0x11000), one object per subchannel: 0 surface, 1 ROP, 2 pattern,
 * 3 clip, 4 line, 5 blit, 6 rectangle, 7 scaled image.
 * ! THE C73 IS NOT IN xf86-video-nv's LIST (it knows C51 0x240, C61 0x3D0,
 *   MCP67 0x530): it gets the IGP branch of the 0x530, its closest parent.
 * ! ONE CHANNEL, NEVER SWITCHED: no context program needed. That's how
 *   xf86-video-nv runs, and why it doesn't need nouveau's ctxprog.
 * ! THE PUSH BUFFER AT 14 MB, not at the end as xf86-video-nv: the BAR1
 *   size isn't known here, and 14 MB is past the biggest framebuffer
 *   (1920x1080x4 = 8.3 MB). Only the DMA object's base says where.
 * ============================================================================= */
#define PB_OFFSET   PB_OFFSET_
#define PB_BYTE     0x8000u
#define SKIPS       8
#define FB_LIMITE   0x0FFFFFFFu      /* 256 MB: the IGP's share of RAM */

static volatile unsigned int *g_pb;
static unsigned int g_put, g_cur, g_libere, g_max = PB_BYTE / 4 - 1;
static unsigned int g_w, g_h, g_passo, g_fb_off;

static void pfifo(unsigned int i, unsigned int v) { wr(0x002000 + i * 4, v); }
static void pgraph(unsigned int o, unsigned int v) { wr(0x400000 + o, v); }
static void pramin(unsigned int i, unsigned int v) { wr(0x710000 + i * 4, v); }

static void metti_put(unsigned int put)
{
    volatile unsigned char x;

    __sync_synchronize();
    x = g_fb ? g_fb[0] : 0;          /* a read of video memory: the writes land first */
    (void)x;
    wr(0x800040, put << 2);
}

static unsigned int leggi_get(void) { return rd(0x800044) >> 2; }

static void parti(void)
{
    if (g_cur != g_put) { g_put = g_cur; metti_put(g_put); }
}

static int dma_aspetta(unsigned int n)
{
    unsigned int giri = 0;

    n++;
    while (g_libere < n) {
        unsigned int get = leggi_get();

        if (++giri > 2000000) { printf("nv40: il push buffer non si svuota (GET %u, PUT %u)\n", get, g_put); return -1; }
        if (g_put >= get) {
            g_libere = g_max - g_cur;
            if (g_libere < n) {
                g_pb[g_cur++] = 0x20000000;          /* jump to 0 */
                if (get <= SKIPS) {
                    if (g_put <= SKIPS) metti_put(SKIPS + 1);
                    while (leggi_get() <= SKIPS) if (++giri > 2000000) return -1;
                }
                metti_put(SKIPS);
                g_cur = g_put = SKIPS;
                g_libere = get - (SKIPS + 1);
            }
        } else {
            g_libere = get - g_cur - 1;
        }
    }
    return 0;
}

/* One method of `n` data on a subchannel (the tag carries it in bits 13-15). */
static int M(unsigned int tag, unsigned int n, const unsigned int *d)
{
    unsigned int i;

    if (g_libere <= n && dma_aspetta(n) != 0) return -1;
    g_pb[g_cur++] = (n << 18) | tag;
    for (i = 0; i < n; i++) g_pb[g_cur++] = d[i];
    g_libere -= n + 1;
    return 0;
}

static int M1(unsigned int tag, unsigned int v) { return M(tag, 1, &v); }

/* Idle: everything consumed (GET == PUT) and PGRAPH not busy. */
static int aspetta_motore(void)
{
    unsigned int giri;

    parti();
    for (giri = 0; giri < 5000000; giri++)
        if (leggi_get() == g_put && rd(0x400700) == 0) return 0;
    printf("nv40: il motore non finisce (GET %u, PUT %u, PGRAPH 0x400700 = 0x%08x)\n",
           leggi_get(), g_put, rd(0x400700));
    return -1;
}

static int motore_accendi(int verboso)
{
    VideoInfo    v;
    MmioZona     m;
    unsigned int i, j;

    if (video_info(&v) != 0 || v.larghezza == 0 || v.bit != 32) {
        TRACCIA("nv40: il motore 2D vuole un modo grafico a 32 bit\n");
        return -1;
    }
    if (vram_off(v.fisico, &g_fb_off) != 0) {
        TRACCIA("nv40: il framebuffer (0x%08x) non e' nella memoria video della scheda\n", v.fisico);
        return -1;
    }
    g_w = v.larghezza; g_h = v.altezza; g_passo = v.passo;
    if (g_passo & 63) {
        printf("nv40: la riga di %u byte non e' multipla di 64: il motore non la vuole.\n"
               "      Rifai il modo con  nv40.drv -modo %ux%u\n", g_passo, g_w, g_h);
        return -1;
    }
    if (!g_fb && fb_mappa(g_fb_off + g_passo * g_h) != 0) return -1;
    m.fisico = g_bar1 + PB_OFFSET;
    m.byte   = PB_BYTE;
    if (mmio_map(&m) != 0) { printf("nv40: mmio_map del push buffer rifiutata\n"); return -1; }
    g_pb = (volatile unsigned int *)(unsigned long)m.virt;

    /* PMC: the engines off and on again (PFIFO bit 8, PGRAPH bit 12) */
    wr(0x000140, 0);
    wr(0x000200, 0xFFFF00FFu);
    wr(0x000200, 0xFFFFFFFFu);

    for (i = 0; i < 12; i++) {                        /* no tiling */
        wr(0x100600 + i * 0x10, 0);
        wr(0x100604 + i * 0x10, FB_LIMITE);
    }

    /* PRAMIN: the hash table (handle -> object) and the objects */
    for (i = 0; i < 8; i++) {
        pramin(i * 2,     0x80000010u + i);
        pramin(i * 2 + 1, 0x00101202u + i * 2);
    }
    pramin(0x0800, 0x00003000); pramin(0x0801, FB_LIMITE); pramin(0x0802, 0x00000002);
    {
        static const unsigned int classi[8] = {
            0x02080062, 0x02080043, 0x02080044, 0x02080019,
            0x020A005C, 0x0208009F, 0x0208004A, 0x02080077
        };
        for (i = 0; i < 8; i++) {
            unsigned int o = 0x0808 + i * 8;

            pramin(o, classi[i]);
            pramin(o + 1, (i == 2 || i == 6) ? 0x02000000u : 0);
            pramin(o + 2, (i == 0 || i == 5 || i == 7) ? 0x00001200u : 0);
            pramin(o + 3, (i == 0 || i == 5 || i == 7) ? 0x00001200u : 0);
            pramin(o + 4, 0);
            pramin(o + 5, 0);
        }
    }
    pramin(0x084C, 0x00003002); pramin(0x084D, PB_BYTE - 1); pramin(0x084E, PB_OFFSET | 2);

    /* PGRAPH, NV10 and later */
    pgraph(0x0080, 0xFFFFFFFFu);
    pgraph(0x0080, 0);
    pgraph(0x0140, 0);
    pgraph(0x0100, 0xFFFFFFFFu);
    pgraph(0x0144, 0x10010100);
    pgraph(0x0714, 0xFFFFFFFFu);
    pgraph(0x0720, 1);
    pgraph(0x0710, (rd(0x400710) & 0x0007FF00u) | 0x00020100u);
    /* NV40 */
    pgraph(0x0084, 0x401287C0);
    pgraph(0x008C, 0x60DE8051);
    pgraph(0x0090, 0x00008000);
    pgraph(0x0610, 0x00BE3C5F);
    pgraph(0x0BC4, rd(0x400BC4) | 0x00008000u);
    j = rd(0x001540) & 0xFF;
    if (j) { for (i = 0; !(j & 1); j >>= 1, i++); pgraph(0x5000, i); }
    pgraph(0x0820, 0x83280EFF);
    pgraph(0x0824, 0x000000A0);
    /* the IGP branch (0x0240, 0x03D0, 0x0530) */
    wr(0x001700, rd(0x10020C));
    wr(0x001704, 0);
    wr(0x001708, 0);
    wr(0x00170C, rd(0x10020C));
    pgraph(0x0860, 0);
    pgraph(0x0864, 0);
    wr(0x680608, rd(0x680608) | 0x00100000u);
    pgraph(0x0B38, 0x2FFFF800);
    pgraph(0x0B3C, 0x00006000);
    pgraph(0x032C, 0x01000000);
    pgraph(0x0220, 0x00001200);
    for (i = 0; i < 48; i++) pgraph(0x0900 + i * 4, rd(0x100600 + i * 4));
    pgraph(0x09F0, rd(0x100200)); pgraph(0x09F4, rd(0x100204));
    pgraph(0x69F0, rd(0x100200)); pgraph(0x69F4, rd(0x100204));
    pgraph(0x0840, 0); pgraph(0x0844, 0);
    pgraph(0x08A0, FB_LIMITE); pgraph(0x08A4, FB_LIMITE);
    pgraph(0x0B20, 0);
    pgraph(0x0B04, 0xFFFFFFFFu);
    pgraph(0x053C, 0); pgraph(0x0540, 0);
    pgraph(0x0544, 0x7FFF); pgraph(0x0548, 0x7FFF);

    /* PFIFO: channel 0 in DMA mode on the push buffer's object (0x1213) */
    pfifo(0x0140, 0); pfifo(0x0141, 1);
    pfifo(0x0480, 0); pfifo(0x0494, 0);
    pfifo(0x0481, 0x00010000);
    pfifo(0x0490, 0); pfifo(0x0491, 0);
    pfifo(0x048B, 0x00001213);
    pfifo(0x0400, 0); pfifo(0x0414, 0);
    pfifo(0x0084, 0x03000100); pfifo(0x0085, 0x00000110); pfifo(0x0086, 0x00000112);
    pfifo(0x0143, 0x0000FFFF); pfifo(0x0496, 0x0000FFFF);
    pfifo(0x0050, 0); pfifo(0x0040, 0xFFFFFFFFu);
    pfifo(0x0415, 1);
    pfifo(0x048C, 0); pfifo(0x04A0, 0);
    pfifo(0x0489, 0x000F0078);
    pfifo(0x0488, 1); pfifo(0x0480, 1); pfifo(0x0494, 1); pfifo(0x0495, 1);
    pfifo(0x0140, 1);

    /* the push buffer: SKIPS no-ops, then the 8 objects on the 8 subchannels */
    for (i = 0; i < SKIPS; i++) g_pb[i] = 0;
    for (i = 0; i < 8; i++) {
        g_pb[SKIPS + i * 2]     = 0x00040000u | (i << 13);
        g_pb[SKIPS + i * 2 + 1] = 0x80000010u + i;
    }
    g_put = 0;
    g_cur = 16 + SKIPS;
    g_libere = g_max - g_cur;

    {
        unsigned int d[4];

        d[0] = 6;                                   /* X8R8G8B8 */
        d[1] = g_passo | (g_passo << 16);
        d[2] = g_fb_off; d[3] = g_fb_off;
        M(0x0300, 4, d);
        M1(0x4300, 3);                              /* pattern format */
        M1(0xC300, 3);                              /* rectangle format */
        M1(0x8300, 3);                              /* line format */
        d[0] = d[1] = d[2] = d[3] = 0xFFFFFFFFu;
        M(0x4310, 4, d);                            /* pattern: all ones */
        M1(0x2300, 0xCC);                           /* ROP: copy */
        M1(0x6300, 0);                              /* clip: everything */
        M1(0x6304, (0x7FFFu << 16) | 0x7FFFu);
    }
    if (aspetta_motore() != 0) return -1;
    if (verboso)
        printf("nv40: motore 2D acceso: %ux%u, riga %u byte, framebuffer a +0x%x, push buffer a +0x%x\n",
               g_w, g_h, g_passo, g_fb_off, PB_OFFSET);
    return 0;
}

/* The service lives longer than a mode: `nv40.drv -modo` under it changes
 * the size and the line, and an engine left on the old pitch draws garbage.
 * One video_info per request costs microseconds, an IPC round trip costs
 * tens: cheap. 0 = the surface matches the screen (again). */
static int motore_geometria(void)
{
    VideoInfo    v;
    unsigned int d[4], off;

    if (video_info(&v) != 0 || v.bit != 32 || v.larghezza == 0 ||
        vram_off(v.fisico, &off) != 0 || (v.passo & 63))
        return -1;
    if (v.larghezza == g_w && v.altezza == g_h && v.passo == g_passo && off == g_fb_off)
        return 0;
    g_w = v.larghezza; g_h = v.altezza; g_passo = v.passo; g_fb_off = off;
    d[0] = 6;
    d[1] = g_passo | (g_passo << 16);
    d[2] = g_fb_off; d[3] = g_fb_off;
    if (M(0x0300, 4, d) != 0) return -1;
    return aspetta_motore();
}

static int motore_riempi(unsigned int x, unsigned int y, unsigned int w, unsigned int h,
                         unsigned int colore)
{
    unsigned int d[2];

    if (x >= g_w || y >= g_h || !w || !h) return 0;
    if (x + w > g_w) w = g_w - x;
    if (y + h > g_h) h = g_h - y;
    if (M1(0xC3FC, colore & 0xFFFFFFu) != 0) return -1;
    d[0] = (x << 16) | y;
    d[1] = (w << 16) | h;
    if (M(0xC400, 2, d) != 0) return -1;
    return aspetta_motore();
}

static int motore_copia(unsigned int sx, unsigned int sy, unsigned int dx, unsigned int dy,
                        unsigned int w, unsigned int h)
{
    unsigned int d[3];

    if (sx >= g_w || sy >= g_h || dx >= g_w || dy >= g_h || !w || !h) return 0;
    if (sx + w > g_w) w = g_w - sx;
    if (dx + w > g_w) w = g_w - dx;
    if (sy + h > g_h) h = g_h - sy;
    if (dy + h > g_h) h = g_h - dy;
    d[0] = (sy << 16) | sx;
    d[1] = (dy << 16) | dx;
    d[2] = (h << 16) | w;
    if (M(0xA300, 3, d) != 0) return -1;
    return aspetta_motore();
}

static volatile unsigned int *pixel(unsigned int x, unsigned int y)
{
    return (volatile unsigned int *)(g_fb + g_fb_off + y * g_passo) + x;
}

/* Every pixel of the rectangle must be `c`, and the ring around it untouched. */
static int controlla(const char *cosa, unsigned int x, unsigned int y, unsigned int w,
                     unsigned int h, unsigned int c, unsigned int fuori)
{
    unsigned int i, j, sbagliati = 0, fuori_sbagliati = 0;

    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if ((*pixel(i, j) & 0xFFFFFFu) != (c & 0xFFFFFFu)) sbagliati++;
    for (i = x; i < x + w; i++) {
        if (y > 0 && (*pixel(i, y - 1) & 0xFFFFFFu) != fuori) fuori_sbagliati++;
        if (y + h < g_h && (*pixel(i, y + h) & 0xFFFFFFu) != fuori) fuori_sbagliati++;
    }
    printf("  %-34s %s (%u pixel sbagliati, %u fuori dal bordo)\n", cosa,
           sbagliati || fuori_sbagliati ? "NO" : "ok", sbagliati, fuori_sbagliati);
    return sbagliati || fuori_sbagliati;
}

static int prova_2d(void)
{
    unsigned int x, y, no = 0;

    printf("nv40: disegno col motore e rileggo dalla memoria\n");
    if (motore_riempi(0, 0, g_w, g_h, 0x000000) != 0) return 1;
    no += controlla("tutto lo schermo nero", 0, 1, g_w, g_h - 2, 0x000000, 0);
    if (motore_riempi(100, 100, 200, 150, 0x00FF00) != 0) return 1;
    no += controlla("rettangolo verde 200x150", 100, 100, 200, 150, 0x00FF00, 0);
    if (motore_riempi(101, 300, 1, 1, 0xFF0000) != 0) return 1;
    no += controlla("un pixel rosso", 101, 300, 1, 1, 0xFF0000, 0);
    /* a pattern drawn by the CPU, copied by the engine */
    for (y = 0; y < 64; y++)
        for (x = 0; x < 64; x++) *pixel(400 + x, 100 + y) = (x * 4) << 16 | (y * 4) << 8 | 0x80;
    if (motore_copia(400, 100, 400, 300, 64, 64) != 0) return 1;
    {
        unsigned int sbagliati = 0;

        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++)
                if (*pixel(400 + x, 300 + y) != *pixel(400 + x, 100 + y)) sbagliati++;
        printf("  %-34s %s (%u pixel diversi)\n", "copia 64x64 di un motivo", sbagliati ? "NO" : "ok", sbagliati);
        no += sbagliati != 0;
    }
    /* overlapping copy, downwards: the classic one that breaks */
    if (motore_copia(400, 300, 400, 310, 64, 54) != 0) return 1;
    {
        unsigned int sbagliati = 0;

        for (y = 0; y < 54; y++)
            for (x = 0; x < 64; x++)
                if (*pixel(400 + x, 310 + y) != ((x * 4) << 16 | (y * 4) << 8 | 0x80)) sbagliati++;
        printf("  %-34s %s (%u pixel diversi)\n", "copia sovrapposta 10 righe giu'", sbagliati ? "NO" : "ok", sbagliati);
        no += sbagliati != 0;
    }
    printf("nv40: 2D %s\n", no ? "CON ERRORI" : "giusto");
    return no ? 1 : 0;
}

static unsigned int ms_ora(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned int)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static int misura_2d(void)
{
    unsigned int t0, t1, giri, x, y, byte = g_w * g_h * 4;

    printf("nv40: %ux%u, %u KB a schermata\n", g_w, g_h, byte / 1024);
    t0 = ms_ora();
    for (giri = 0; giri < 20; giri++)
        for (y = 0; y < g_h; y++) {
            volatile unsigned int *r = pixel(0, y);

            for (x = 0; x < g_w; x++) r[x] = giri * 0x0A0A0A;
        }
    t1 = ms_ora();
    printf("  processore, riempire:  %u ms per 20 schermate, %u MB/s\n", t1 - t0,
           t1 > t0 ? (byte / 1024) * 20 / (t1 - t0) * 1000 / 1024 : 0);
    {
        /* the door console and wserver use: fb_map, write-combining */
        volatile unsigned char *k = (volatile unsigned char *)fb_map();

        if (k) {
            t0 = ms_ora();
            for (giri = 0; giri < 20; giri++)
                for (y = 0; y < g_h; y++) {
                    volatile unsigned int *r = (volatile unsigned int *)(k + y * g_passo);

                    for (x = 0; x < g_w; x++) r[x] = giri * 0x0A0A0A;
                }
            t1 = ms_ora();
            printf("  fb_map,     riempire:  %u ms per 20 schermate, %u MB/s\n", t1 - t0,
                   t1 > t0 ? (byte / 1024) * 20 / (t1 - t0) * 1000 / 1024 : 0);
            t0 = ms_ora();
            for (giri = 0; giri < 5; giri++)
                for (y = 0; y + 1 < g_h; y++)
                    memmove((void *)(k + y * g_passo), (const void *)(k + (y + 1) * g_passo), g_w * 4);
            t1 = ms_ora();
            printf("  fb_map,     scorrere:  %u ms per 5 schermate, %u MB/s\n", t1 - t0,
                   t1 > t0 ? (byte / 1024) * 5 / (t1 - t0) * 1000 / 1024 : 0);
        }
    }
    t0 = ms_ora();
    for (giri = 0; giri < 200; giri++)
        if (motore_riempi(0, 0, g_w, g_h, giri * 0x050505) != 0) return 1;
    t1 = ms_ora();
    printf("  motore,     riempire:  %u ms per 200 schermate, %u MB/s\n", t1 - t0,
           t1 > t0 ? (byte / 1024) * 200 / (t1 - t0) * 1000 / 1024 : 0);
    t0 = ms_ora();
    for (giri = 0; giri < 5; giri++)
        for (y = 0; y + 1 < g_h; y++)
            memmove((void *)pixel(0, y), (const void *)pixel(0, y + 1), g_w * 4);
    t1 = ms_ora();
    printf("  processore, scorrere:  %u ms per 5 schermate, %u MB/s\n", t1 - t0,
           t1 > t0 ? (byte / 1024) * 5 / (t1 - t0) * 1000 / 1024 : 0);
    t0 = ms_ora();
    for (giri = 0; giri < 200; giri++)
        if (motore_copia(0, 1, 0, 0, g_w, g_h - 1) != 0) return 1;
    t1 = ms_ora();
    printf("  motore,     scorrere:  %u ms per 200 schermate, %u MB/s\n", t1 - t0,
           t1 > t0 ? (byte / 1024) * 200 / (t1 - t0) * 1000 / 1024 : 0);
    motore_riempi(0, 0, g_w, g_h, 0);
    return 0;
}

static int servizio_2d(void)
{
    IpcMessage    meta;
    unsigned char buf[64];
    unsigned int  servite = 0;

    if (ipc_lookup(ACC_SERVIZIO) > 0) {
        printf("nv40: c'e' gia' un acceleratore '%s' acceso\n", ACC_SERVIZIO);
        return 1;
    }
    /* ! SILENT WHERE IT CAN'T: text mode, another framebuffer. wserver
     * then doesn't find the service and draws by itself, the normal case. */
    if (motore_accendi(0) != 0) return g_zitto ? 0 : 1;
    if (ipc_register(ACC_SERVIZIO) < 0) {
        printf("nv40: il nome '%s' e' gia' preso\n", ACC_SERVIZIO);
        return 1;
    }
    printf("nv40: servizio '%s' attivo - %ux%u a 32 bit\n", ACC_SERVIZIO, g_w, g_h);
    for (;;) {
        int n = ipc_recv(&meta, buf, sizeof(buf));

        if (n < 0) continue;
        switch (meta.tipo) {
        case ACC_MSG_RIEMPI: {
            AccRiempi r;
            AccEsito  e;

            if (meta.len < sizeof(r)) break;
            memcpy(&r, buf, sizeof(r));
            e.esito = motore_geometria() || motore_riempi(r.x, r.y, r.w, r.h, r.colore) ? -EIO : 0;
            if (e.esito == 0) servite++;
            ipc_send(meta.sender_pid, ACC_MSG_ESITO, &e, sizeof(e));
            break;
        }
        case ACC_MSG_COPIA: {
            AccCopia c;
            AccEsito e;

            if (meta.len < sizeof(c)) break;
            memcpy(&c, buf, sizeof(c));
            e.esito = motore_geometria() || motore_copia(c.sx, c.sy, c.dx, c.dy, c.w, c.h) ? -EIO : 0;
            if (e.esito == 0) servite++;
            ipc_send(meta.sender_pid, ACC_MSG_ESITO, &e, sizeof(e));
            break;
        }
        case ACC_MSG_INFO: {
            AccInfo i;

            memset(&i, 0, sizeof(i));
            motore_geometria();
            i.larghezza = g_w; i.altezza = g_h; i.bit = 32; i.passo = g_passo;
            i.servite = servite;
            strcpy(i.nome, "nv40");
            ipc_send(meta.sender_pid, ACC_MSG_INFO_R, &i, sizeof(i));
            break;
        }
        default:
            break;
        }
    }
}

/* =============================================================================
 * -install [LxA[@Hz]] / -remove: the block in /boot/avvio.sh (avvio_blocco)
 *
 * The mode in the foreground, so the prompt and the desktop come after it,
 * already on the new framebuffer; the service with '&' (it never returns).
 * Without a mode only the service: the screen stays svga.drv's VESA one,
 * which on this card already lives in BAR1, where the engine draws.
 * ============================================================================= */
static int installa(int togli, const char *modo)
{
    char righe[640], riga_modo[64];
    int  rc = 0, pci;

    if (getuid() != 0) { printf("nv40: -install e -remove solo da root\n"); return 1; }
    if (!togli && modo && !tempi_di(modo)) {
        printf("nv40: modo '%s' sconosciuto\n", modo);
        elenco_modi();
        return 1;
    }
    if (avvio_blocco("nv40.drv", NULL) < 0) rc = -1;
    if (!togli && rc == 0) {
        pci = avvio_ha_riga("pci.drv") || avvio_ha_riga("/dev/pci.drv");
        riga_modo[0] = '\0';
        if (modo) snprintf(riga_modo, sizeof(riga_modo), "nv40.drv -avvio %s\n", modo);
        snprintf(righe, sizeof(righe),
                 "# La grafica NVIDIA NV4x (GeForce 6/7 e le integrate C51-C73): %s\n"
                 "# il motore 2D alla scrivania. Esce zitto su ogni altra scheda.\n"
                 "# `nv40.drv -remove` toglie questo blocco.\n"
                 "%s%s"
                 "nv40.drv -2dservizio &\n",
                 modo ? "il modo, poi" : "", pci ? "" : "pci.drv &\n", riga_modo);
        rc = avvio_blocco("nv40.drv", righe);
    }
    if (rc < 0) { printf("nv40: non riesco a scrivere /boot/avvio.sh (errno %d)\n", errno); return 1; }
    if (togli) printf("nv40: tolto da /boot/avvio.sh\n");
    else if (modo) printf("nv40: in /boot/avvio.sh: al prossimo avvio %s e il motore 2D\n", modo);
    else printf("nv40: in /boot/avvio.sh: al prossimo avvio il motore 2D\n");
    return 0;
}

/* -2dchiedi: the service tried FROM OUTSIDE, the way wserver uses it, and the
 * pixels read back. */
static int chiedi_mio(const IpcMessage *meta, void *dato)
{
    if ((meta->tipo == ACC_MSG_ESITO || meta->tipo == ACC_MSG_INFO_R) &&
        meta->sender_pid == *(unsigned int *)dato) return IPC_MIO;
    return IPC_ALTRUI;
}

static int chiedi_2d(void)
{
    AccInfo      info;
    AccRiempi    r;
    AccEsito     e;
    IpcMessage   meta;
    VideoInfo    v;
    unsigned int chi, giri, sbagliati = 0, t0, x, y;
    int          pid = ipc_lookup(ACC_SERVIZIO);

    if (pid <= 0) { printf("nv40: il servizio '%s' non c'e': nv40.drv -2dservizio &\n", ACC_SERVIZIO); return 1; }
    chi = (unsigned int)pid;
    if (ipc_send(chi, ACC_MSG_INFO, &chi, sizeof(chi)) < 0 ||
        ipc_scegli(chiedi_mio, &chi, &meta, &info, sizeof(info), 2000) < 0) {
        printf("nv40: il servizio (PID %d) non risponde all'INFO\n", pid);
        return 1;
    }
    printf("nv40: servizio '%s' PID %d: '%s', %ux%u a %u bit, riga %u, servite %u\n",
           ACC_SERVIZIO, pid, info.nome, info.larghezza, info.altezza, info.bit, info.passo, info.servite);
    if (video_info(&v) != 0 || vram_off(v.fisico, &g_fb_off) != 0 ||
        fb_mappa(g_fb_off + v.passo * v.altezza) != 0) return 1;
    g_passo = v.passo; g_w = v.larghezza; g_h = v.altezza;
    r.x = 0; r.y = 0; r.w = info.larghezza; r.h = info.altezza;
    t0 = ms_ora();
    for (giri = 0; giri < 100; giri++) {
        r.colore = giri & 1 ? 0x00204060u : 0x00406020u;
        if (ipc_send(chi, ACC_MSG_RIEMPI, &r, sizeof(r)) < 0 ||
            ipc_scegli(chiedi_mio, &chi, &meta, &e, sizeof(e), 2000) < 0 || e.esito != 0) {
            printf("nv40: il riempimento %u non e' riuscito (esito %d)\n", giri, e.esito);
            return 1;
        }
    }
    printf("nv40: 100 schermate intere chieste e fatte in %u ms, giro di IPC compreso\n", ms_ora() - t0);
    for (y = 0; y < g_h; y += 7)
        for (x = 0; x < g_w; x += 3)
            if ((*pixel(x, y) & 0xFFFFFFu) != 0x204060u) sbagliati++;
    printf("nv40: riletto: %u pixel sbagliati su %u controllati\n", sbagliati, (g_h / 7 + 1) * (g_w / 3 + 1));
    return sbagliati ? 1 : 0;
}

static void uso(void)
{
    printf("uso: nv40.drv -stato | -bios | -edid | -modi | -prova LxA[@Hz] [S] | -modo LxA[@Hz]\n"
           "              | -2dprova | -2dmisura | -2dservizio | -2dchiedi | -install [LxA[@Hz]] | -remove\n"
           "  NVIDIA NV4x (GeForce 6/7, C51, C61, C73)\n"
           "  -stato  il chip, la memoria video e il modo di ogni testa (legge)\n"
           "  -bios   la ROM video: riferimento e limiti dei PLL\n"
           "  -edid   il monitor, dalla DDC\n"
           "  -modi   i modi che conosce\n"
           "  -prova  il modo con un motivo di prova per S secondi (10), poi torna\n"
           "  -modo   il modo, e ci resta\n"
           "  -2dprova     il motore 2D disegna, e si rilegge se e' giusto\n"
           "  -2dmisura    processore contro motore, in MB/s\n"
           "  -2dservizio  il motore offerto alla scrivania (servizio accel2d)\n"
           "  -2dchiedi    prova il servizio da fuori, come lo usa wserver\n"
           "  -install     il servizio (e il modo, se dato) a ogni avvio, in /boot/avvio.sh\n");
}

int main(int argc, char **argv)
{
    PciDispositivo d;

    if (argc >= 2 && strcmp(argv[1], "-i") == 0) {
        g_zitto = 1;
        if (trova_scheda(&d) != 0) return 1;
        g_bar0 = d.bar[0];
        if (mappa() != 0) return 1;
        return e_nv4x(architettura()) ? 0 : 1;
    }
    if (argc >= 2 && strcmp(argv[1], "-modi") == 0) { elenco_modi(); return 0; }
    if (argc >= 2 && strcmp(argv[1], "-install") == 0) return installa(0, argc >= 3 ? argv[2] : NULL);
    if (argc >= 2 && strcmp(argv[1], "-remove") == 0) return installa(1, NULL);
    /* -avvio: -modo from avvio.sh, quiet, and nothing if the mode is already it
     * (`system -load` reruns avvio.sh). */
    if (argc >= 3 && strcmp(argv[1], "-avvio") == 0) { g_zitto = 1; argv[1] = "-modo"; }
    if (argc >= 2 && strcmp(argv[1], "-2dservizio") == 0) g_zitto = 1;
    if (argc < 2 || (strcmp(argv[1], "-stato") != 0 && strcmp(argv[1], "-edid") != 0 &&
                     strcmp(argv[1], "-bios") != 0 && strncmp(argv[1], "-2d", 3) != 0 &&
                     ((strcmp(argv[1], "-prova") != 0 && strcmp(argv[1], "-modo") != 0 &&
                       strcmp(argv[1], "-reg") != 0 && strcmp(argv[1], "-cr") != 0) || argc < 3))) {
        uso();
        return argc >= 2 && strcmp(argv[1], "-h") == 0 ? 0 : 1;
    }
    if (getuid() != 0) { printf("nv40: %s solo da root\n", argv[1]); return 1; }
    /* -bios only reads 0xC0000: it runs on any machine, and says whose ROM. */
    if (strcmp(argv[1], "-bios") == 0) return bios();
    if (trova_scheda(&d) != 0) return g_zitto ? 0 : 1;
    g_bar0 = d.bar[0];
    g_bar1 = d.bar[1];
    TRACCIA("Scheda: %04x:%04x in %02x:%02x.%x, registri a 0x%08x, memoria video a 0x%08x, IRQ %u\n",
           d.venditore, d.dispositivo, d.bus, d.slot, d.funzione, d.bar[0], d.bar[1], d.irq_linea);
    if (mappa() != 0) return 1;
    g_cpu_fb = g_bar1;
    {
        int i, bar1 = 0;

        for (i = 2; i < argc; i++) if (strcmp(argv[i], "-bar1") == 0) bar1 = 1;
        if (!bar1 && e_nv4x(architettura()) && integrata(architettura())) trova_rubata();
    }
    /* -reg OFF [VAL]: one register of BAR0, read (and written). For the
     * hunts with someone watching the screen. */
    /* -cr IDX [VAL]: one CRTC register of head 0, same purpose */
    if (strcmp(argv[1], "-cr") == 0) {
        unsigned int i = (unsigned int)strtoul(argv[2], NULL, 0), lock = cr(0, 0x1F);

        cr_scrivi(0, 0x1F, 0x57);
        printf("CR%02X = 0x%02x\n", i, cr(0, i));
        if (argc >= 4 && argv[3][0] != '-') {
            cr_scrivi(0, i, (unsigned char)strtoul(argv[3], NULL, 0));
            printf("CR%02X = 0x%02x (scritto)\n", i, cr(0, i));
        }
        if (lock != 0x03 && lock != 0x57) cr_scrivi(0, 0x1F, 0x99);
        return 0;
    }
    if (strcmp(argv[1], "-reg") == 0) {
        unsigned int o = (unsigned int)strtoul(argv[2], NULL, 0);

        printf("0x%06x = 0x%08x\n", o, rd(o));
        if (argc >= 4 && argv[3][0] != '-') {     /* "-bar1" is not a value, stupid */
            wr(o, (unsigned int)strtoul(argv[3], NULL, 0));
            printf("0x%06x = 0x%08x (scritto)\n", o, rd(o));
        }
        return 0;
    }
    if (strncmp(argv[1], "-2d", 3) == 0) {
        if (!e_nv4x(architettura())) { TRACCIA("nv40: non e' una NV4x\n"); return g_zitto ? 0 : 1; }
        if (strcmp(argv[1], "-2dservizio") == 0) return servizio_2d();
        if (strcmp(argv[1], "-2dchiedi") == 0) return chiedi_2d();
        if (strcmp(argv[1], "-2dprova") != 0 && strcmp(argv[1], "-2dmisura") != 0) { uso(); return 1; }
        if (ipc_lookup(ACC_SERVIZIO) > 0) {
            printf("nv40: il servizio '%s' e' acceso e usa il motore: non lo riaccendo sotto di lui\n",
                   ACC_SERVIZIO);
            return 1;
        }
        if (motore_accendi(1) != 0) return 1;
        return strcmp(argv[1], "-2dprova") == 0 ? prova_2d() : misura_2d();
    }
    if (strcmp(argv[1], "-edid") == 0 || strcmp(argv[1], "-prova") == 0 || strcmp(argv[1], "-modo") == 0) {
        if (!e_nv4x(architettura())) { TRACCIA("nv40: non e' una NV4x\n"); return g_zitto ? 0 : 1; }
        if (strcmp(argv[1], "-edid") == 0) return edid();
        if (g_zitto) {
            const Tempi *t = tempi_di(argv[2]);
            VideoInfo    v;

            if (t && video_info(&v) == 0 && v.fisico == g_cpu_fb && v.larghezza == t->w &&
                v.altezza == t->h && v.bit == 32)
                return 0;
        }
        return cambia_modo(argv[2], strcmp(argv[1], "-modo") == 0 ? 0 : (argc >= 4 ? atoi(argv[3]) : 10));
    }
    return stato();
}
