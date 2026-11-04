/* =============================================================================
 * drivers/dpms/dpms.c - /dev/dpms.drv, the screen switches off when nobody uses it
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     dpms.drv                 on or off, after how long, the last key
 *     dpms.drv -dopo:M         off after M minutes without keys or mouse
 *                              (starts the service if it is not there;
 *                              -dopo:0 = never)                      (root)
 *     dpms.drv -spegni         off now (one second later, so the key that
 *                              ran the command does not wake it)     (root)
 *     dpms.drv -accendi        on now                                (root)
 *     dpms.drv -ferma          the service ends, the screen stays on (root)
 *     dpms.drv -install [M]    -dopo:M at every boot (10), /boot/avvio.sh
 *     dpms.drv -remove         takes that away
 *     dpms.drv -i              0 if it can work here (hwconfig)
 *
 * (voce 78 b, 4 November 2026: "prima la frequenza della CPU, poi lo schermo".)
 *
 * WHO KNOWS THAT SOMEBODY IS THERE: the kernel (0.312, SYS_ATTIVITA). kbd.drv
 * at every key and PS/2 mouse packet, the USB mice (usb_comune.c) and the
 * serial mouse call attivita_tocca(); here attivita_da() says how many ms ago.
 * No message per event: an IPC mailbox is four messages deep, and a mouse
 * sends a hundred packets a second.
 *
 * HOW THE SCREEN GOES OFF, by card:
 *   nv50  NVIDIA G80 and later (the laptop's GeForce 8200M): the SOR that
 *         drives the panel is powered down, as xf86-video-nv does in
 *         G80SorDPMSSet (0x61C004 + 0x800*n: bit 0 on, bit 31 "do it";
 *         0x61C030 bit 28 = still busy). The panel's own sequencer takes the
 *         backlight down with it. The legacy VGA registers would do nothing
 *         there: the panel is fed by the display engine, not by the VGA.
 *   vga   every other card, and text mode: sequencer register SR01 bit 5,
 *         "screen off", and the attribute controller's PAS bit (see ar_pas).
 *         The picture goes black; on a CRT the signal stays,
 *         so it saves the electron beam, not the monitor's power supply.
 *         ! NOT CRTC 17 (the syncs): the kernel writes the CRTC for the text
 *         cursor, and two writers of the index port 0x3D4 overwrite each
 *         other's index (seen in the sonda reports, CR24).
 * The method is chosen once, at start; the card does not change under us.
 *
 * ! THE SERVICE KEEPS THE STATE, NOT A FILE: which SORs were on is known only
 * to the process that switched them off. If it dies with the screen off, the
 * screen stays off until `dpms.drv -accendi` (it switches on everything that
 * the method knows), from telnet or typed blind.
 * ============================================================================= */

#include "libc.h"
#include "pci_proto.h"

EX_VERSIONE("dpms.drv", "0.001");

#define SERVIZIO        "dpms"
#define MSG_STATO       0xD901u     /* -> DpmsStato */
#define MSG_DOPO        0xD902u     /* unsigned minutes -> DpmsStato */
#define MSG_SPEGNI      0xD903u
#define MSG_ACCENDI     0xD904u
#define MSG_FERMA       0xD905u
#define MSG_RISPOSTA    0xD980u

#define MINUTI_INIZIO   10u

typedef struct {
    unsigned int spento;        /* 1 = the screen is off now */
    unsigned int minuti;        /* 0 = never */
    unsigned int da_ms;         /* since the last key or movement */
    unsigned int spegnimenti;   /* how many times since the service started */
    char         metodo[40];
} DpmsStato;

/* --- the methods ------------------------------------------------------------- */

static char g_metodo[40] = "nessuno";
static int  g_nv;                       /* 1 = nv50, 0 = vga */
static volatile unsigned int *g_sor;    /* BAR0 + 0x61C000, 0x2000 bytes */
static unsigned int g_sor_spenti;       /* bit n = SOR n switched off by us */
static int  g_sr01 = -1;                /* SR01 as found, -1 = not off */

static int chiedi_pci(int pid, unsigned int ordinale, PciDispositivo *out)
{
    PciRichiesta  r;
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           t;

    r.ordinale    = ordinale;
    r.classe      = 0x03;           /* display */
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

/* An NVIDIA G80 or later, with its SOR registers mapped. 0 = not here. */
static int nv_apri(void)
{
    int            pid = ipc_attendi(PCI_SERVIZIO, 3000);
    unsigned int   ord, arch;
    PciDispositivo d;
    MmioZona       m;

    if (pid <= 0) return 0;
    for (ord = 0; ord < 8; ord++) {
        if (chiedi_pci(pid, ord, &d) != 1) return 0;
        if (d.venditore == 0x10DE && d.bar[0] && !d.bar_io[0]) break;
    }
    if (ord == 8) return 0;
    m.fisico = d.bar[0];
    m.byte   = 0x1000;
    if (mmio_map(&m) != 0) return 0;
    arch = (((volatile unsigned int *)(unsigned long)m.virt)[0] >> 20) & 0x1ff;
    if (arch < 0x50) return 0;          /* before G80: another display engine */
    m.fisico = d.bar[0] + 0x61C000;
    m.byte   = 0x2000;
    if (mmio_map(&m) != 0) return 0;
    g_sor = (volatile unsigned int *)(unsigned long)m.virt;
    snprintf(g_metodo, sizeof(g_metodo), "nv50 (%04x:%04x, NV%02X)",
             d.venditore, d.dispositivo, arch);
    return 1;
}

static int nv_aspetta(unsigned int off, unsigned int bit)
{
    unsigned int t0 = uptime_ms();

    while (g_sor[off / 4] & bit)
        if (uptime_ms() - t0 > 1000) return -1;
    return 0;
}

/* G80SorDPMSSet, for SOR n. */
static void nv_sor(unsigned int n, int acceso)
{
    unsigned int c = 0x004 + n * 0x800, s = 0x030 + n * 0x800, v;

    nv_aspetta(c, 0x80000000u);
    v = g_sor[c / 4];
    v = acceso ? (v | 1u) : (v & ~1u);
    g_sor[c / 4] = v | 0x80000000u;
    nv_aspetta(s, 0x10000000u);
}

static void nv_spegni(void)
{
    unsigned int n;

    for (n = 0; n < 2; n++)
        if (g_sor[(0x004 + n * 0x800) / 4] & 1u) {
            nv_sor(n, 0);
            g_sor_spenti |= 1u << n;
        }
}

/* ! WITH NOTHING REMEMBERED, SOR0: -accendi from a new process, after the
 * service died with the screen off, knows nothing; SOR0 is the panel on the
 * laptop and on xf86-video-nv's LVDS path. */
static void nv_accendi(void)
{
    unsigned int n, m = g_sor_spenti ? g_sor_spenti : 1u;

    for (n = 0; n < 2; n++)
        if (m & (1u << n)) nv_sor(n, 1);
    g_sor_spenti = 0;
}

static int vga_apri(void)
{
    if (ioport_bind(0x3C0, 6) != 0) return 0;      /* AR 3C0, SEQ 3C4-3C5 */
    if (ioport_bind(0x3DA, 1) != 0) return 0;      /* resets the AR flip-flop */
    snprintf(g_metodo, sizeof(g_metodo), "vga (SR01 e AR)");
    return 1;
}

/* The attribute controller's index register, bit 5 PAS: 0 = the palette is
 * being written, so the screen shows the border colour only. Reading 0x3DA
 * first puts the 0x3C0 flip-flop on "index". ! QEMU IN A VBE MODE IGNORES
 * SR01 and honours this; most real cards in a linear mode do the opposite,
 * hence both. (0x3DA is the colour address; the kernel only drives colour.) */
static void ar_pas(int acceso)
{
    (void)ioport_in(0x3DA);
    ioport_out(0x3C0, acceso ? 0x20u : 0x00u);
}

static int sr(unsigned int i) { ioport_out(0x3C4, i); return ioport_in(0x3C5) & 0xFF; }
static void sw(unsigned int i, unsigned int v) { ioport_out(0x3C4, i); ioport_out(0x3C5, v); }

static void vga_spegni(void)
{
    int v = sr(0x01);

    if (g_sr01 < 0) g_sr01 = v & ~0x20;
    sw(0x00, 0x01);                     /* synchronous reset, as vgacon does */
    sw(0x01, (unsigned int)(v | 0x20));
    sw(0x00, 0x03);
    ar_pas(0);
}

static void vga_accendi(void)
{
    int v = g_sr01 >= 0 ? g_sr01 : (sr(0x01) & ~0x20);

    sw(0x00, 0x01);
    sw(0x01, (unsigned int)v);
    sw(0x00, 0x03);
    ar_pas(1);
    g_sr01 = -1;
}

static int apri(void)
{
    if (nv_apri()) { g_nv = 1; return 1; }
    return vga_apri();
}

static void spegni(void)  { if (g_nv) nv_spegni();  else vga_spegni(); }
static void accendi(void) { if (g_nv) nv_accendi(); else vga_accendi(); }

/* --- the service ------------------------------------------------------------- */

static int servizio(unsigned int minuti)
{
    IpcMessage    meta;
    unsigned char buf[64];
    unsigned int  spento = 0, spegnimenti = 0, prima = 0, tra = 0;
    int           fine = 0;

    if (ipc_register(SERVIZIO) < 0) {
        printf("dpms: il nome '%s' e' gia' preso\n", SERVIZIO);
        return 1;
    }
    while (!fine) {
        int d, n = ipc_recv_timeout(&meta, buf, sizeof(buf), spento ? 100 : (tra ? 100 : 1000));

        if (n >= 0) {
            DpmsStato s;

            switch (meta.tipo) {
            case MSG_DOPO:
                if (meta.len >= sizeof(unsigned int)) memcpy(&minuti, buf, sizeof(minuti));
                break;
            case MSG_SPEGNI:
                if (!spento) tra = uptime_ms() + 1000u;   /* see the head */
                break;
            case MSG_ACCENDI:
                tra = 0;
                if (spento) { accendi(); spento = 0; }
                break;
            case MSG_FERMA:
                if (spento) { accendi(); spento = 0; }
                fine = 1;
                break;
            case MSG_STATO:
                break;
            default:
                continue;
            }
            memset(&s, 0, sizeof(s));
            s.spento      = spento;
            s.minuti      = minuti;
            d             = attivita_da();
            s.da_ms       = d < 0 ? 0 : (unsigned int)d;
            s.spegnimenti = spegnimenti;
            strncpy(s.metodo, g_metodo, sizeof(s.metodo) - 1);
            ipc_send(meta.sender_pid, MSG_RISPOSTA, &s, sizeof(s));
        }

        d = attivita_da();
        if (d < 0) continue;
        if (spento) {
            /* The time since the last touch only grows while nobody is there:
             * smaller than a moment ago = a key, a click, a movement. */
            if ((unsigned int)d < prima) { accendi(); spento = 0; }
        } else if (tra) {
            if ((int)(uptime_ms() - tra) >= 0) {
                tra = 0;
                spegni(); spento = 1; spegnimenti++;
            }
        } else if (minuti && (unsigned int)d >= minuti * 60000u) {
            spegni(); spento = 1; spegnimenti++;
        }
        prima = (unsigned int)d;
    }
    return 0;
}

/* --- the client -------------------------------------------------------------- */

static int chiedi(unsigned int tipo, const void *dato, unsigned int len, DpmsStato *s)
{
    int        pid = ipc_lookup(SERVIZIO), t;
    IpcMessage meta;
    unsigned char buf[sizeof(DpmsStato) + 16];

    if (pid <= 0) return -1;
    if (ipc_send((unsigned int)pid, tipo, dato, len) < 0) return -1;
    for (t = 0; t < 8; t++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
        if ((int)meta.sender_pid != pid || meta.tipo != MSG_RISPOSTA) continue;
        if (s && meta.len >= sizeof(*s)) memcpy(s, buf, sizeof(*s));
        return 0;
    }
    return -1;
}

static void durata(unsigned int ms, char *b, int n)
{
    unsigned int s = ms / 1000u;

    if (s < 60)        snprintf(b, (size_t)n, "%u s", s);
    else if (s < 3600) snprintf(b, (size_t)n, "%u min %u s", s / 60, s % 60);
    else               snprintf(b, (size_t)n, "%u h %u min", s / 3600, (s / 60) % 60);
}

static int stato(void)
{
    DpmsStato s;
    char      b[32];
    int       d = attivita_da();

    if (d < 0) {
        printf("dpms: il kernel non dice quando si e' toccata la tastiera\n");
        printf("      (serve il kernel 0.312 o successivo)\n");
        return 1;
    }
    durata((unsigned int)d, b, sizeof(b));
    if (chiedi(MSG_STATO, 0, 0, &s) != 0) {
        printf("servizio:   spento - lo schermo resta sempre acceso\n");
        printf("            si accende con  dpms.drv -dopo:%u\n", MINUTI_INIZIO);
        printf("ultimo tasto o movimento: %s fa\n", b);
        return 0;
    }
    printf("schermo:    %s\n", s.spento ? "spento" : "acceso");
    printf("metodo:     %s\n", s.metodo);
    if (s.minuti) printf("si spegne:  dopo %u minuti senza tasti ne' mouse\n", s.minuti);
    else          printf("si spegne:  mai (solo con  dpms.drv -spegni)\n");
    durata(s.da_ms, b, sizeof(b));
    printf("ultimo tasto o movimento: %s fa\n", b);
    printf("spegnimenti dall'avvio del servizio: %u\n", s.spegnimenti);
    return 0;
}

static int numero(const char *p, unsigned int *out)
{
    unsigned int n = 0;

    if (*p < '0' || *p > '9') return -1;
    while (*p >= '0' && *p <= '9') n = n * 10u + (unsigned int)(*p++ - '0');
    if (*p) return -1;
    *out = n;
    return 0;
}

static int dopo(const char *argv0, unsigned int minuti)
{
    DpmsStato s;

    if (chiedi(MSG_DOPO, &minuti, sizeof(minuti), &s) == 0) {
        if (minuti) printf("dpms: lo schermo si spegne dopo %u minuti\n", minuti);
        else        printf("dpms: lo schermo non si spegne piu' da solo\n");
        return 0;
    }
    if (attivita_da() < 0) {
        printf("dpms: serve il kernel 0.312 o successivo (SYS_ATTIVITA)\n");
        return 1;
    }
    {
        /* ! THE SERVICE STARTS ITSELF, so that avvio.sh and the user write the
         * same line with no `&`, and a second -dopo only changes the time. */
        char  n[16], *av[3];
        const char *via = strchr(argv0, '/') ? argv0 : "/dev/dpms.drv";

        snprintf(n, sizeof(n), "-servizio:%u", minuti);
        av[0] = (char *)via; av[1] = n; av[2] = 0;
        if (spawn(via, av) < 0) { printf("dpms: %s non si avvia\n", via); return 1; }
        if (ipc_attendi(SERVIZIO, 5000) <= 0) {
            printf("dpms: il servizio non si e' registrato\n");
            return 1;
        }
    }
    if (chiedi(MSG_STATO, 0, 0, &s) != 0) { printf("dpms: il servizio non risponde\n"); return 1; }
    printf("dpms: servizio acceso, metodo %s\n", s.metodo);
    if (minuti) printf("      lo schermo si spegne dopo %u minuti senza tasti ne' mouse\n", minuti);
    else        printf("      lo schermo si spegne solo con  dpms.drv -spegni\n");
    return 0;
}

static int installa(int togli, unsigned int minuti)
{
    char righe[320];
    int  rc;

    if (getuid() != 0) { printf("dpms: -install e -remove solo da root\n"); return 1; }
    rc = avvio_blocco("dpms.drv", NULL);
    if (rc >= 0 && !togli) {
        snprintf(righe, sizeof(righe),
                 "# Lo schermo si spegne dopo %u minuti senza tasti ne' mouse,\n"
                 "# e si riaccende al primo tasto o movimento.\n"
                 "# `dpms.drv -remove` toglie questo blocco.\n"
                 "dpms.drv -dopo:%u\n", minuti, minuti);
        rc = avvio_blocco("dpms.drv", righe);
    }
    if (rc < 0) { printf("dpms: non riesco a scrivere /boot/avvio.sh (errno %d)\n", errno); return 1; }
    if (togli) printf("dpms: tolto da /boot/avvio.sh; adesso: `dpms.drv -ferma`\n");
    else       printf("dpms: in /boot/avvio.sh, dal prossimo avvio lo schermo si spegne dopo %u minuti\n", minuti);
    return 0;
}

static void aiuto(void)
{
    printf("dpms.drv - lo schermo si spegne quando nessuno lo usa (kernel 0.312)\n\n");
    printf("  dpms.drv               acceso o spento, dopo quanto, l'ultimo tasto\n");
    printf("  dpms.drv -dopo:M       si spegne dopo M minuti senza tasti ne' mouse\n");
    printf("                         (accende il servizio; -dopo:0 = mai)\n");
    printf("  dpms.drv -spegni       spento adesso (fra un secondo)\n");
    printf("  dpms.drv -accendi      acceso adesso\n");
    printf("  dpms.drv -ferma        il servizio finisce, lo schermo resta acceso\n");
    printf("  dpms.drv -install [M]  -dopo:M a ogni accensione (%u), /boot/avvio.sh\n", MINUTI_INIZIO);
    printf("  dpms.drv -remove       lo toglie\n");
    printf("Si riaccende al primo tasto, clic o movimento del mouse.\n");
}

static int comando(unsigned int tipo, const char *cosa)
{
    if (getuid() != 0) { printf("dpms: %s solo da root\n", cosa); return 1; }
    if (chiedi(tipo, 0, 0, 0) == 0) return 0;
    if (tipo == MSG_ACCENDI && apri()) {
        /* No service: switch on whatever the method knows (see the head). */
        accendi();
        printf("dpms: acceso con %s (il servizio non c'era)\n", g_metodo);
        return 0;
    }
    printf("dpms: il servizio non c'e'. Si accende con  dpms.drv -dopo:%u\n", MINUTI_INIZIO);
    return 1;
}

int main(int argc, char **argv)
{
    const char  *a = argc >= 2 ? argv[1] : "";
    unsigned int m;

    if (argc < 2 || strcmp(a, "-stato") == 0) return stato();
    if (strcmp(a, "-i") == 0) return attivita_da() >= 0 ? 0 : 1;
    if (strncmp(a, "-servizio:", 10) == 0) {
        if (numero(a + 10, &m) != 0) return 1;
        if (!apri()) { printf("dpms: nessun modo di spegnere questo schermo\n"); return 1; }
        return servizio(m);
    }
    if (strncmp(a, "-dopo:", 6) == 0) {
        if (getuid() != 0) { printf("dpms: -dopo solo da root\n"); return 1; }
        if (numero(a + 6, &m) != 0) { printf("dpms: -dopo:M vuole i minuti, un numero\n"); return 1; }
        return dopo(argv[0], m);
    }
    if (strcmp(a, "-spegni") == 0)  return comando(MSG_SPEGNI, a);
    if (strcmp(a, "-accendi") == 0) return comando(MSG_ACCENDI, a);
    if (strcmp(a, "-ferma") == 0)   return comando(MSG_FERMA, a);
    if (strcmp(a, "-install") == 0) {
        m = MINUTI_INIZIO;
        if (argc >= 3 && numero(argv[2], &m) != 0) { printf("dpms: -install M vuole i minuti\n"); return 1; }
        return installa(0, m);
    }
    if (strcmp(a, "-remove") == 0) return installa(1, 0);
    if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { aiuto(); return 0; }
    printf("dpms: opzione sconosciuta '%s'\n\n", a);
    aiuto();
    return 1;
}
