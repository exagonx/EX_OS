/* =============================================================================
 * drivers/ar5k/ar5k.c — the Atheros AR5xxx WiFi chips (AR5212 family) (@WIFI W7)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The register sequences and numbers come from Linux's ath5k driver
 * (drivers/net/wireless/ath/ath5k, v6.1, ISC licence: "Permission to use,
 * copy, modify, and distribute this software for any purpose with or without
 * fee is hereby granted..."), whose authors are Reyk Floeter, Nick
 * Kossifidis, Luis Rodriguez, Pavel Roskin and others. The reference copy is
 * in cross_build/terze-parti/ath5k-6.1.
 *
 * The first machine: the user's MCP77 laptop, an HP card 168c:001c
 * (subsystem 103c:137b), silicon revision 0xe2 = AR2425 "Swan", radio
 * RF2425, PCI Express. Seen with mappa.drv on 15 October 2026: the BIOS
 * leaves it in reset (RESET_CTL = 3).
 *
 * BY STAGES, each tried on the laptop before the next:
 *   1. -i   wake the chip, read the radio revision and the EEPROM (MAC,
 *           version, regulatory domain), and put it back as it was found.
 *   2.      radio and baseband init for 2.4 GHz, receive: scan for beacons
 *           (what `wifi -showlist` needs).
 *   3.      transmit, authenticate and associate with an open network.
 *   4.      WPA/WPA2 with lib/exwifi (software CCMP/TKIP).
 *   5.      the wifi0 service (drivers/net/wifi_proto.h + net_proto.h), so
 *           wifid, `wifi` and the desktop panel drive it.
 * ============================================================================= */
#include "libc.h"
#include "pci_proto.h"

EX_VERSIONE("ar5k.drv", "0.002");

#define AR5K_VENDITORE      0x168c
#define AR5K_MMIO_BYTE      0x10000

/* --- registers (ath5k reg.h) --------------------------------------------------- */
#define AR5K_RXDP           0x000c
#define AR5K_CFG            0x0014
#define AR5K_RESET_CTL      0x4000
#define   RESET_PCU         0x00000001
#define   RESET_BASEBAND    0x00000002
#define AR5K_SLEEP_CTL      0x4004
#define   SLE_MASK          0x00030000
#define   SLE_WAKE          0x00000000
#define   SLE_SLP           0x00010000
#define AR5K_PCICFG         0x4010
#define   PCICFG_SPWR_DN    0x00010000
#define AR5K_SREV           0x4020
#define AR5K_EEPROM_BASE    0x6000
#define AR5K_EEPROM_DATA    0x6004
#define AR5K_EEPROM_CMD     0x6008
#define   EEPROM_CMD_READ   0x00000001
#define AR5K_EEPROM_STAT    0x600c
#define   EEPROM_RDERR      0x00000001
#define   EEPROM_RDDONE     0x00000002
#define AR5K_STA_ID1        0x8004
#define   STA_ID1_PWR_SV    0x00040000
#define AR5K_PHY(n)         (0x9800 + ((n) << 2))
#define AR5K_PHY_CHIP_ID    0x9818
#define AR5K_PHY_SHIFT_2GHZ 0x00004007
#define AR5K_PHY_SHIFT_5GHZ 0x00000007

/* --- EEPROM words (ath5k eeprom.h) --------------------------------------------- */
#define EE_MAGIC            0x003d
#define EE_MAGIC_VALUE      0x5aa5
#define EE_REG_DOMAIN       0x00bf
#define EE_VERSION          0x00c1

static volatile unsigned int *g_reg;

static unsigned int rl(unsigned int off) { return g_reg[off / 4]; }
static void wl(unsigned int off, unsigned int v) { g_reg[off / 4] = v; }

/* the ath5k register wait: up to ~20 ms, by counted reads (no interrupt-based
 * timers needed) */
static int aspetta(unsigned int reg, unsigned int maschera, unsigned int valore)
{
    int i;

    for (i = 0; i < 1500; i++) {
        if ((rl(reg) & maschera) == valore) return 0;
        usleep(15);
    }
    return -1;
}

/* --- the PCI service ------------------------------------------------------------- */
static int chiedi_pci(int pid, unsigned int ordinale, PciDispositivo *out)
{
    PciRichiesta  r;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           t;

    r.ordinale    = ordinale;
    r.classe      = PCI_QUALUNQUE;
    r.sottoclasse = PCI_QUALUNQUE;
    r.venditore   = AR5K_VENDITORE;
    r.dispositivo = PCI_QUALUNQUE;
    if (ipc_send(pid, PCI_MSG_CERCA, &r, sizeof(r)) < 0) return -1;
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

static int g_pci_pid;

static int trova_scheda(PciDispositivo *d)
{
    int pid = ipc_attendi(PCI_SERVIZIO, 5000), n;

    g_pci_pid = pid;
    if (pid <= 0) { printf("ar5k: il servizio PCI non c'e'. Avvialo:  /dev/pci.drv &\n"); return -1; }
    n = chiedi_pci(pid, 0, d);
    if (n <= 0) { printf("ar5k: nessuna scheda Atheros (168c) sul bus PCI\n"); return -1; }
    if (d->bar[0] == 0 || d->bar_io[0]) { printf("ar5k: la scheda non ha i registri in BAR0\n"); return -1; }
    return 0;
}

/* PCI configuration: a 32-bit read, and the command register bits. */
static unsigned int pci_leggi(const PciDispositivo *d, unsigned int offset)
{
    PciAzione     a;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           t;

    memset(&a, 0, sizeof(a));
    a.bus = d->bus; a.slot = d->slot; a.funzione = d->funzione;
    a.offset = (unsigned short)offset;
    if (ipc_send(g_pci_pid, PCI_MSG_LEGGI, &a, sizeof(a)) < 0) return 0xffffffffu;
    for (t = 0; t < 16; t++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return 0xffffffffu;
        if ((int)meta.sender_pid != g_pci_pid) continue;
        if (meta.tipo != PCI_MSG_VALORE || meta.len < sizeof(PciValore)) return 0xffffffffu;
        return ((PciValore *)buf)->valore;
    }
    return 0xffffffffu;
}

static void pci_bit(const PciDispositivo *d, int tipo, unsigned int bit)
{
    PciAzione     a;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           t;

    if (bit == 0) return;
    memset(&a, 0, sizeof(a));
    a.bus = d->bus; a.slot = d->slot; a.funzione = d->funzione;
    a.bit = (unsigned short)bit;
    if (ipc_send(g_pci_pid, tipo, &a, sizeof(a)) < 0) return;
    for (t = 0; t < 16; t++) {                  /* the PCI_MSG_ESITO reply */
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return;
        if ((int)meta.sender_pid == g_pci_pid) return;
    }
}

/* --- the chip ---------------------------------------------------------------------- */

/* ath5k_hw_set_power_mode(AR5K_PM_AWAKE, true) */
static int sveglia(void)
{
    unsigned int dato = rl(AR5K_SLEEP_CTL);
    int          i;

    wl(AR5K_STA_ID1, rl(AR5K_STA_ID1) & ~STA_ID1_PWR_SV);
    dato = (dato & 0xffc00000u) ? 0 : (dato & ~SLE_MASK);
    wl(AR5K_SLEEP_CTL, dato | SLE_WAKE);
    usleep(20);
    for (i = 200; i > 0; i--) {
        if (!(rl(AR5K_PCICFG) & PCICFG_SPWR_DN)) return 0;
        usleep(75);
        wl(AR5K_SLEEP_CTL, dato | SLE_WAKE);
    }
    return -1;
}

/* ath5k_hw_nic_reset(): PCI Express cards never get the PCI reset bit */
static int azzera(unsigned int val)
{
    unsigned int maschera = (val ? val : ~0u) & (RESET_PCU | RESET_BASEBAND);
    int          r;

    (void)rl(AR5K_RXDP);
    wl(AR5K_RESET_CTL, val);
    usleep(20);
    r = aspetta(AR5K_RESET_CTL, maschera, val & (RESET_PCU | RESET_BASEBAND));
    if (!(val & RESET_PCU)) wl(AR5K_CFG, 0);     /* AR5K_INIT_CFG, little endian */
    return r;
}

/* ath5k_hw_nic_wakeup(ah, NULL): wake, warm reset, wake, out of reset */
static int avvia_chip(void)
{
    if (sveglia() != 0)                         { printf("ar5k: il chip non si sveglia\n"); return -1; }
    if (azzera(RESET_PCU | RESET_BASEBAND) != 0) { printf("ar5k: il reset non riesce\n"); return -1; }
    if (sveglia() != 0)                         { printf("ar5k: il chip non si risveglia dopo il reset\n"); return -1; }
    if (azzera(0) != 0)                         { printf("ar5k: il chip non esce dal reset\n"); return -1; }
    return 0;
}

static unsigned int bitswap(unsigned int v, int bit)
{
    unsigned int r = 0;
    int          i;

    for (i = 0; i < bit; i++) { r = (r << 1) | (v & 1); v >>= 1; }
    return r;
}

/* ath5k_hw_radio_revision() for the AR5212 family */
static unsigned int radio_revisione(int banda_2ghz)
{
    unsigned int s;
    int          i;

    wl(AR5K_PHY(0), banda_2ghz ? AR5K_PHY_SHIFT_2GHZ : AR5K_PHY_SHIFT_5GHZ);
    usleep(2500);
    wl(AR5K_PHY(0x34), 0x00001c16);
    for (i = 0; i < 8; i++) wl(AR5K_PHY(0x20), 0x00010000);
    s = (rl(AR5K_PHY(0x100)) >> 24) & 0xff;
    wl(AR5K_PHY(0), AR5K_PHY_SHIFT_5GHZ);
    return bitswap(((s & 0xf0) >> 4) | ((s & 0x0f) << 4), 8);
}

/* ath5k_pci_eeprom_read(): -1 if the word cannot be read */
static int eeprom(unsigned int off, unsigned int *dato)
{
    int i;

    wl(AR5K_EEPROM_BASE, off);
    wl(AR5K_EEPROM_CMD, rl(AR5K_EEPROM_CMD) | EEPROM_CMD_READ);
    for (i = 0; i < 1500; i++) {
        unsigned int st = rl(AR5K_EEPROM_STAT);

        if (st & EEPROM_RDDONE) {
            if (st & EEPROM_RDERR) return -1;
            *dato = rl(AR5K_EEPROM_DATA) & 0xffff;
            return 0;
        }
        usleep(15);
    }
    return -1;
}

static const char *nome_mac(unsigned int srev)
{
    switch (srev >> 4) {
    case 0x5: return "AR5212";
    case 0x7: return "AR5213";
    case 0x8: return "AR5213A";
    case 0x9: return "AR2413";
    case 0xa: return "AR5413/AR5414";
    case 0xc: return "AR5414";
    case 0xd: return "AR2415";
    case 0xe: return "AR2425 (Swan)";
    case 0xf: return "AR2417 (Nala)";
    }
    return "AR5xxx";
}

/* -i: look, then put back what was changed */
static int informa(const PciDispositivo *d)
{
    unsigned int reset0, sonno0, srev, phy, rad5, rad2, magia = 0, ver = 0, dom = 0, w;
    unsigned char mac[6];
    int           i, ok_mac = 1;

    reset0 = rl(AR5K_RESET_CTL);
    sonno0 = rl(AR5K_SLEEP_CTL);
    srev   = rl(AR5K_SREV);
    printf("ar5k: %02x:%02x.%x %04x:%04x, registri a 0x%08x\n", d->bus, d->slot, d->funzione,
           d->venditore, d->dispositivo, d->bar[0]);
    printf("  revisione del MAC  0x%02x  %s\n", srev & 0xff, nome_mac(srev & 0xff));
    printf("  trovato con        reset 0x%x, sonno 0x%08x\n", reset0, sonno0);
    if (srev == 0xffffffffu || (srev & 0xff) < 0x50) {
        printf("ar5k: non e' un chip della famiglia AR5212: mi fermo senza toccarlo\n");
        return 1;
    }

    if (avvia_chip() != 0) goto rimetti;
    phy  = rl(AR5K_PHY_CHIP_ID);
    rad5 = radio_revisione(0);
    rad2 = radio_revisione(1);
    printf("  sveglio e fuori dal reset: PHY 0x%08x, radio 5 GHz 0x%02x, 2,4 GHz 0x%02x\n",
           phy, rad5, rad2);

    if (eeprom(EE_MAGIC, &magia) == 0 && eeprom(EE_VERSION, &ver) == 0 &&
        eeprom(EE_REG_DOMAIN, &dom) == 0) {
        /* ath5k reads the word at 0x3d and never checks it: on the AR2425 of the
     * user's laptop it is 0, and the EEPROM is fine */
    printf("  EEPROM             parola 0x3d 0x%04x, versione %u.%u, dominio 0x%04x\n",
               magia, ver >> 12, ver & 0xfff, dom);
    } else {
        printf("  EEPROM             non si legge\n");
    }
    /* ath5k_pci_eeprom_read_mac(): words 0x1f, 0x1e, 0x1d, high byte first */
    for (i = 0; i < 3; i++) {
        if (eeprom(0x1f - (unsigned int)i, &w) != 0) { ok_mac = 0; break; }
        mac[i * 2]     = (unsigned char)(w >> 8);
        mac[i * 2 + 1] = (unsigned char)w;
    }
    if (ok_mac)
        printf("  indirizzo MAC      %02x:%02x:%02x:%02x:%02x:%02x\n",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    else
        printf("  indirizzo MAC      non si legge\n");

rimetti:
    /* ! PUT BACK WHAT WAS FOUND: this stage only looks. */
    wl(AR5K_RESET_CTL, reset0);
    usleep(20);
    wl(AR5K_SLEEP_CTL, sonno0);
    printf("  rimesso come l'ho trovato: reset 0x%x, sonno 0x%08x\n",
           rl(AR5K_RESET_CTL), rl(AR5K_SLEEP_CTL));
    return 0;
}

int ar5k_scansiona(volatile void *registri, unsigned int sotto_id);   /* ar5k_scan.c */
int ar5k_radio(int accendi, unsigned int sotto_id);                   /* ar5k_radio.c */
int ar5k_prova_led(volatile void *registri, unsigned int sotto_id);   /* ar5k_scan.c */
int ar5k_prova_led_ec(volatile void *registri, unsigned int sotto_id);
int ar5k_tasto(unsigned int secondi, unsigned int sotto_id);           /* ar5k_radio.c */
int ar5k_servi(volatile void *registri, unsigned int sotto_id, const unsigned char mac[6]);
int ar5k_leggi_mac(volatile void *registri, unsigned int sotto_id, unsigned char mac[6]);

static void uso(void)
{
    printf("uso: ar5k.drv        il servizio wifi0: wifid, `wifi -showlist` e il\n");
    printf("                     pannello vedono le reti vere (collegarsi: non ancora)\n");
    printf("     ar5k.drv -i     sveglia il chip, legge radio ed EEPROM (MAC) e lo\n");
    printf("                     rimette come l'ha trovato (tappa 1)\n");
    printf("     ar5k.drv -s     accende radio e banda base a 2,4 GHz e ascolta i\n");
    printf("                     canali 1-13: le reti che si sentono. Non trasmette\n");
    printf("                     niente (tappa 2). Il servizio wifi0 non c'e' ancora.\n");
    printf("     ar5k.drv -radio     solo sul portatile HP (scheda 103c:137b): legge\n");
    printf("                         dall'EC e dalla CMOS se la radio e' accesa\n");
    printf("     ar5k.drv -radio:on  la accende come farebbe il tasto WiFi\n");
    printf("     ar5k.drv -led       prova, un passo alla volta e chiedendo, cosa\n");
    printf("                         comanda la spia del WiFi\n");
    printf("     ar5k.drv -led:ec    il colore della spia per ogni combinazione di\n");
    printf("                         GPIO 3 e LED dell'EC (chiede che cosa vedi)\n");
    printf("     ar5k.drv -tasto     per 60 secondi fa funzionare il tasto WiFi\n");
}

int main(int argc, char **argv)
{
    PciDispositivo d;
    MmioZona       m;

    if (argc > 2 || (argc == 2 && strcmp(argv[1], "-i") != 0 && strcmp(argv[1], "-s") != 0 &&
                      strcmp(argv[1], "-radio") != 0 && strcmp(argv[1], "-radio:on") != 0 &&
                      strcmp(argv[1], "-led") != 0 && strcmp(argv[1], "-led:ec") != 0 &&
                      strcmp(argv[1], "-tasto") != 0)) {
        uso();
        return argc == 2 && strcmp(argv[1], "-h") == 0 ? 0 : 2;
    }
    if (argc == 1) {
        /* the wifi0 service, until stopped (ar5k_servizio.c) */
        unsigned char mac[6];
        unsigned int  sotto;

        if (trova_scheda(&d) != 0) return 1;
        m.fisico = d.bar[0];
        m.byte   = AR5K_MMIO_BYTE;
        m.virt   = 0;
        if (mmio_map(&m) != 0) { printf("ar5k: mmio_map(0x%08x) rifiutata: serve root\n", d.bar[0]); return 1; }
        g_reg = (volatile unsigned int *)(uintptr_t)m.virt;
        sotto = pci_leggi(&d, 0x2c);
        if (ar5k_leggi_mac(g_reg, sotto, mac) != 0) { printf("ar5k: il chip non si inizializza\n"); return 1; }
        return ar5k_servi(g_reg, sotto, mac);
    }
    if (trova_scheda(&d) != 0) return 1;
    if (strncmp(argv[1], "-radio", 6) == 0)
        return ar5k_radio(argv[1][6] == ':', pci_leggi(&d, 0x2c));
    m.fisico = d.bar[0];
    m.byte   = AR5K_MMIO_BYTE;
    m.virt   = 0;
    if (mmio_map(&m) != 0) { printf("ar5k: mmio_map(0x%08x) rifiutata: serve root\n", d.bar[0]); return 1; }
    g_reg = (volatile unsigned int *)(uintptr_t)m.virt;
    if (strcmp(argv[1], "-led") == 0) return ar5k_prova_led(g_reg, pci_leggi(&d, 0x2c));
    if (strcmp(argv[1], "-led:ec") == 0) return ar5k_prova_led_ec(g_reg, pci_leggi(&d, 0x2c));
    if (strcmp(argv[1], "-tasto") == 0) return ar5k_tasto(60, pci_leggi(&d, 0x2c));
    if (strcmp(argv[1], "-s") == 0) {
        /* ! BUS MASTER. Without it the chip cannot write a received frame
         * into RAM: the first scan on the laptop (15 October 2026) reset
         * all 13 channels fine and completed no descriptor at all. The
         * bits that were off are turned off again at the end: a card left
         * bus master would go on writing into pages already given back. */
        unsigned int cmd0 = pci_leggi(&d, 0x04) & 0xffff, accesi, r;

        printf("ar5k: registro comando PCI 0x%04x (memoria %s, bus master %s)\n", cmd0,
               cmd0 & PCI_ABIL_MEMORIA ? "si'" : "no", cmd0 & PCI_ABIL_BUSMASTER ? "si'" : "no");
        accesi = (PCI_ABIL_MEMORIA | PCI_ABIL_BUSMASTER) & ~cmd0;
        if (cmd0 == 0xffff) accesi = 0;
        pci_bit(&d, PCI_MSG_ABILITA, accesi);
        printf("ar5k: ora 0x%04x\n", pci_leggi(&d, 0x04) & 0xffff);
        r = (unsigned int)ar5k_scansiona(g_reg, pci_leggi(&d, 0x2c));   /* subsystem */
        pci_bit(&d, PCI_MSG_DISABILITA, accesi & PCI_ABIL_BUSMASTER);
        printf("ar5k: registro comando PCI rimesso a 0x%04x\n", pci_leggi(&d, 0x04) & 0xffff);
        return (int)r;
    }
    return informa(&d);
}
