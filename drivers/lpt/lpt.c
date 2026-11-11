/* =============================================================================
 * drivers/lpt/lpt.c - /dev/lpt.drv, the parallel port (voce 98)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     lpt.drv                    the ports, what they can do, who holds them
 *     lpt.drv -avvia             finds the ports and starts a service for each
 *                                (lpt0, lpt1...); -porta:0x378 only that one,
 *                                -irq:N the line of its nACK (0 = none)  (root)
 *     lpt.drv -ferma             the services end                         (root)
 *     lpt.drv -stampa FILE       FILE to the printer (compatibility mode)
 *     lpt.drv -id                the IEEE 1284 Device ID of what is attached
 *     lpt.drv -pin               the three registers, pin by pin
 *     lpt.drv -dati:V            the data lines to V (0x.. or decimal)
 *     lpt.drv -controllo:V       the control register to V
 *     lpt.drv -fronti:S          for S seconds the edges on nACK (pin 10):
 *                                how many, and the time between them
 *     lpt.drv -prova-fronti:N    N interrupts made by the port itself, as
 *                                QEMU's makes them (tools/prova_lpt.sh)
 *     lpt.drv -prova-onda:N:US   N half-waves of US microseconds on D1, and
 *                                how late the latest one came out
 *     lpt.drv -install           -avvia at every boot, /boot/avvio.sh
 *     lpt.drv -remove            takes that away
 *     lpt.drv -i                 0 if this machine has a parallel port
 *     -n:N                       which port the commands talk to (0)
 *
 * ONE DRIVER FOR EVERYTHING ON THE PORT. Printers, the Iomega ZIP drive
 * (ppa/imm), parallel scanners, a Commodore datasette through a C64S-style
 * adapter, prototype boards: all of them are the same 17 wires used in a
 * different way. This service owns the wires and gives them to ONE client at
 * a time (LPT_MSG_PRENDI), with the primitives they need:
 *   - compatibility-mode printing with the Busy/nStrobe handshake;
 *   - the raw registers, for boards and for protocols written in a client;
 *   - the IEEE 1284 Device ID (nibble mode), to know what is attached;
 *   - the edges of nACK WITH THEIR INSTANT, from the kernel (0.323,
 *     SYS_IRQ_FRONTI): the datasette's READ line is wired to nACK, and the
 *     data is the time between its edges;
 *   - a wave on data lines with given half-wave times, from a processor
 *     reserved to this service when there are two or more: the datasette's
 *     WRITE line (D1 on the C64S adapter).
 * The devices' own protocols (the TAP format, the ZIP's SCSI over nibbles)
 * live in their programs, which use these. See lpt_proto.h for the messages.
 *
 * THE DATASETTE'S WIRES, as documented by the mtap and ptap programs (only as
 * documentation, nothing of them is copied here): READ on nACK (status bit 6,
 * pin 10), SENSE on Paper End (status bit 5, pin 12: 0 while PLAY is down),
 * WRITE on D1 (data bit 1, pin 3). A Commodore 64 PAL counts tape time at
 * 123156 Hz (NTSC 127841, VIC-20 PAL 138551, C16 PAL 110840): those numbers
 * belong to the TAP programs, here every time is in the clock's ticks
 * (LptFronti.hz) or in nanoseconds.
 *
 * WHAT THE PORT IS, by probing (sonda): data lines that read back what was
 * written (SPP); with the direction bit set they stop doing it (PS/2,
 * bidirectional); an ECR at base+0x402 that answers like one (ECP); an EPP
 * address read that ends in the timeout bit, as an EPP port without a device
 * does (EPP). ! THE EPP ANSWER IS A GUESS in one direction: an EPP port with
 * an EPP device that answers gives no timeout and is reported as not EPP.
 * ============================================================================= */
#include "libc.h"
#include "lpt_proto.h"

EX_VERSIONE("lpt.drv", "0.002");

#define PORTE_N 3
static const unsigned int g_candidate[PORTE_N] = { 0x378, 0x278, 0x3BC };

/* --- the port ------------------------------------------------------------------ */

static unsigned int g_base, g_irq, g_modi;
static int          g_ecp;

/* ! IN AND OUT STRAIGHT FROM HERE when the kernel opens the port's windows in
 * the TSS's I/O bitmap (0.324, ioport_diretto): a system call per byte took
 * the kernel's lock, and with two processors a wave came out up to 1.3 ms
 * late against half-waves of 180 us. With an older kernel, the calls. */
static int          g_diretto;

static unsigned int in8(unsigned int off)
{
    int v;

    if (g_diretto) {
        unsigned char b;
        __asm__ volatile ("inb %w1, %0" : "=a"(b) : "Nd"((unsigned short)(g_base + off)));
        return b;
    }
    v = ioport_in(g_base + off);
    return v < 0 ? 0xFFu : (unsigned int)v;
}
static void out8(unsigned int off, unsigned int v)
{
    if (g_diretto) {
        __asm__ volatile ("outb %b0, %w1" : : "a"((unsigned char)v), "Nd"((unsigned short)(g_base + off)));
        return;
    }
    ioport_out(g_base + off, v & 0xFFu);
}

/* 64 bits divided by 32 without libgcc's __udivdi3 (the drivers link only the
 * libc). The second division cannot overflow: the remainder is < d. */
static unsigned long long div64(unsigned long long n, unsigned int d)
{
#if defined(__x86_64__)
    return n / d;
#else
    unsigned int hi = (unsigned int)(n >> 32), lo = (unsigned int)n;
    unsigned int qh = hi / d, r = hi % d, ql;

    __asm__ ("divl %4" : "=a"(ql), "=d"(r) : "a"(lo), "d"(r), "rm"(d));
    return ((unsigned long long)qh << 32) | ql;
#endif
}

/* The fine clock: RDTSC where the kernel's clock is the TSC (ring 3 may read
 * it), the kernel otherwise (the PIT, a 486). */
static unsigned int g_hz, g_tsc;

static void orologio_apri(void)
{
    IrqFrontiStato s;

    memset(&s, 0, sizeof(s));
    if (irq_fronti_stato(0, &s) == 0) { g_hz = s.hz; g_tsc = s.tsc; }
    if (g_hz == 0) g_hz = 1000;     /* a kernel before 0.323: ms, no waves */
}

static unsigned long long orologio(void)
{
    IrqFrontiStato s;

    if (g_tsc) {
        unsigned int basso, alto;
        __asm__ volatile ("rdtsc" : "=a"(basso), "=d"(alto));
        return ((unsigned long long)alto << 32) | basso;
    }
    if (irq_fronti_stato(0, &s) == 0) return s.adesso;
    return (unsigned long long)uptime_ms();
}

static unsigned long long ns_a_tick(unsigned int ns)
{
    return div64((unsigned long long)ns * g_hz, 1000000000u);
}
static unsigned int tick_a_ns(unsigned long long t)
{
    unsigned long long ns;

    /* through kHz: t * 10^6 stays in 64 bits up to hours of a 4 GHz TSC */
    if (t > 0xFFFFFFFFFFFull) return 0xFFFFFFFFu;
    ns = div64(t * 1000000u, g_hz / 1000u ? g_hz / 1000u : 1u);
    return ns > 0xFFFFFFFFull ? 0xFFFFFFFFu : (unsigned int)ns;
}

/* Is there a port at `base`, and what can it do? 0 = none. The data and
 * control registers are put back as they were. */
static unsigned int sonda(unsigned int base, int *ecp)
{
    unsigned int dati, ctrl, a, b, modi = 0, ecr = 0;
    unsigned int vecchia = g_base;

    *ecp = 0;
    if (ioport_bind(base, base == 0x3BC ? 3 : 8) < 0) return 0;
    g_base = base;
    dati = in8(LPT_DATI); ctrl = in8(LPT_CONTROLLO);
    out8(LPT_CONTROLLO, LPT_CT_RIPOSO);         /* outputs, no interrupt */
    out8(LPT_DATI, 0xAA); a = in8(LPT_DATI);
    out8(LPT_DATI, 0x55); b = in8(LPT_DATI);
    if (a != 0xAA || b != 0x55 || (in8(LPT_CONTROLLO) & 0x1F) != LPT_CT_RIPOSO) {
        out8(LPT_DATI, dati); out8(LPT_CONTROLLO, ctrl);
        g_base = vecchia;
        return 0;
    }
    modi = LPT_MODO_SPP;

    /* ECP: the test of the IEEE 1284 extended control register. Some SPP
     * ports repeat the control register at +0x402: a bit changed there must
     * not change here. */
    if (base != 0x3BC && ioport_bind(base + 0x400, 3) == 0) {
        unsigned int c = in8(LPT_CONTROLLO), e = in8(LPT_ECR), si = 1;

        if ((e & 3) == (c & 3)) {
            out8(LPT_CONTROLLO, c ^ 2);
            c = in8(LPT_CONTROLLO);
            if ((in8(LPT_ECR) & 2) == (c & 2)) si = 0;
            out8(LPT_CONTROLLO, LPT_CT_RIPOSO);
        }
        if (si && (in8(LPT_ECR) & 3) != 1) si = 0;     /* FIFO empty, not full */
        if (si) {
            ecr = in8(LPT_ECR);
            out8(LPT_ECR, 0x34);                        /* mode 001, PS/2 */
            if (in8(LPT_ECR) != 0x35) { out8(LPT_ECR, ecr); si = 0; }
        }
        if (si) { modi |= LPT_MODO_ECP; *ecp = 1; }
    }

    /* PS/2: with the data lines as inputs they no longer read back what is
     * written to them. (On an ECP chip mode 001 is set above.) */
    out8(LPT_CONTROLLO, LPT_CT_RIPOSO | LPT_CT_INGRESSO);
    out8(LPT_DATI, 0xAA); a = in8(LPT_DATI);
    out8(LPT_DATI, 0x55); b = in8(LPT_DATI);
    out8(LPT_CONTROLLO, LPT_CT_RIPOSO);
    if (a != 0xAA || b != 0x55) modi |= LPT_MODO_PS2;

    /* EPP: an address cycle with nobody to answer ends in the timeout bit
     * (see the head). The bit is cleared writing 1 on some chips, 0 on
     * others: both. */
    if (base != 0x3BC) {
        if (*ecp) out8(LPT_ECR, 0x94);                  /* mode 100, EPP */
        out8(LPT_STATO, 0x01); out8(LPT_STATO, 0x00);
        if (!(in8(LPT_STATO) & LPT_ST_TIMEOUT)) {
            out8(LPT_CONTROLLO, LPT_CT_NINIT);          /* EPP strobes idle */
            (void)in8(3);                               /* EPP address read */
            if (in8(LPT_STATO) & LPT_ST_TIMEOUT) modi |= LPT_MODO_EPP;
            out8(LPT_STATO, 0x01); out8(LPT_STATO, 0x00);
        }
        out8(LPT_CONTROLLO, LPT_CT_RIPOSO);
        if (*ecp) out8(LPT_ECR, 0x34);
    }
    out8(LPT_DATI, dati);
    out8(LPT_CONTROLLO, (ctrl & ~(LPT_CT_IRQ | LPT_CT_INGRESSO)) & 0x0F);
    g_base = vecchia;
    return modi;
}

static unsigned int irq_di(unsigned int base) { return base == 0x278 ? 5 : 7; }

/* --- the service ---------------------------------------------------------------- */

static unsigned char g_buf[IPC_MSG_MAX_DATA];
static int           g_chi;             /* holder, 0 = free */
static unsigned int  g_uso, g_scritti;

/* The edges, as the kernel gives them, until a client reads them. Grows as
 * needed: a datasette sends some 3000 a second, and the client may be slow. */
static unsigned long long *g_ed;
static unsigned int g_ed_cap, g_ed_inizio, g_ed_fine, g_ed_persi;
static int          g_fronti, g_irq_preso;

/* The wave to play (LPT_MSG_ONDA_*). */
static unsigned int *g_onda;
static unsigned int  g_onda_n, g_onda_cap;

static void rispondi(int pid, unsigned int tipo, const void *d, unsigned int len)
{
    ipc_send((unsigned int)pid, LPT_RISPOSTA(tipo), d, len);
}
static void esito(int pid, unsigned int tipo, int rc)
{
    LptEsito e;

    e.rc = rc;
    rispondi(pid, tipo, &e, sizeof(e));
}

static void fronti_spegni(void)
{
    if (!g_fronti) return;
    out8(LPT_CONTROLLO, in8(LPT_CONTROLLO) & ~LPT_CT_IRQ & 0x3Fu);
    irq_fronti_spegni(g_irq);
    /* ! irq_unbind, NOT irq_done: with the edges off, a line kept would come
     * back masked after its first interrupt, and the next client asking for
     * edges would find it so. */
    irq_unbind(g_irq);
    g_irq_preso = 0;
    g_fronti = 0;
    g_ed_inizio = g_ed_fine = 0;
}

static void rilascia(void)
{
    fronti_spegni();
    g_onda_n = 0;
    g_chi = 0; g_uso = 0;
    /* the pins stay as the client left them: -dati and -controllo are
     * commands that end, and what they set must stay */
}

/* The port for `pid`: free, or already its own. A holder that is gone frees
 * it. */
static int puo(int pid)
{
    if (g_chi && g_chi != pid && kill(g_chi, 0) < 0) rilascia();
    return g_chi == 0 || g_chi == pid;
}

static int fronti_metti(unsigned long long t)
{
    if (g_ed_fine == g_ed_cap) {
        if (g_ed_inizio > 0) {
            memmove(g_ed, g_ed + g_ed_inizio, (g_ed_fine - g_ed_inizio) * sizeof(*g_ed));
            g_ed_fine -= g_ed_inizio; g_ed_inizio = 0;
        } else {
            unsigned int nc = g_ed_cap ? g_ed_cap * 2 : 16384;
            unsigned long long *n = realloc(g_ed, nc * sizeof(*g_ed));

            if (n == NULL) return -1;
            g_ed = n; g_ed_cap = nc;
        }
    }
    g_ed[g_ed_fine++] = t;
    return 0;
}

static void fronti_drena(void)
{
    unsigned long long t[256];
    int n, i;

    if (!g_fronti) return;
    do {
        n = irq_fronti_leggi(g_irq, t, 256);
        for (i = 0; i < n; i++) if (fronti_metti(t[i]) != 0) g_ed_persi++;
    } while (n == 256);
}

static int fronti_accendi(unsigned int quanti)
{
    int rc;

    if (g_irq == 0) return -ENODEV;
    if (g_fronti) fronti_spegni();
    if (!g_irq_preso) {
        rc = irq_bind(g_irq);
        if (rc < 0) return rc;
        g_irq_preso = 1;
    }
    rc = irq_fronti_accendi(g_irq, quanti ? quanti : 16384u);
    if (rc < 0) { irq_unbind(g_irq); g_irq_preso = 0; return rc; }
    g_fronti = 1; g_ed_persi = 0;
    g_ed_inizio = g_ed_fine = 0;
    out8(LPT_CONTROLLO, (in8(LPT_CONTROLLO) | LPT_CT_IRQ) & 0x3Fu);
    return 0;
}

static void fronti_leggi(int pid, unsigned int max)
{
    static LptFronti f;
    IrqFrontiStato s;
    unsigned int n;

    memset(&f, 0, LPT_FRONTI_TESTA);
    if (!g_fronti) { f.rc = -EINVAL; rispondi(pid, LPT_MSG_FRONTI_LEGGI, &f, LPT_FRONTI_TESTA); return; }
    fronti_drena();
    n = g_ed_fine - g_ed_inizio;
    if (max == 0 || max > LPT_FRONTI_PEZZO) max = LPT_FRONTI_PEZZO;
    if (n > max) n = max;
    memcpy(f.t, g_ed + g_ed_inizio, n * sizeof(f.t[0]));
    g_ed_inizio += n;
    if (g_ed_inizio == g_ed_fine) g_ed_inizio = g_ed_fine = 0;
    memset(&s, 0, sizeof(s));
    irq_fronti_stato(g_irq, &s);
    f.hz        = g_hz;
    f.n         = n;
    f.persi     = s.persi + g_ed_persi;
    f.in_attesa = g_ed_fine - g_ed_inizio + s.in_coda;
    f.spuri     = s.spuri;
    f.stato     = (unsigned char)in8(LPT_STATO);
    rispondi(pid, LPT_MSG_FRONTI_LEGGI, &f, LPT_FRONTI_TESTA + n * sizeof(f.t[0]));
}

/* Waits until (status & m) == v, up to ms (the clock is 10 ms). */
static int aspetta_stato(unsigned int m, unsigned int v, unsigned int ms, unsigned int *st)
{
    unsigned int fine = uptime_ms() + ms + 10, giri = 0, s;

    for (;;) {
        s = in8(LPT_STATO);
        if ((s & m) == v) { if (st) *st = s; return 0; }
        if ((int)(uptime_ms() - fine) >= 0) { if (st) *st = s; return -ETIMEDOUT; }
        if (++giri > 2000) usleep(1000);
    }
}

/* Compatibility mode: Busy low, data, a pulse on nStrobe. */
static void scrivi(int pid, const unsigned char *d, unsigned int n)
{
    LptScritto r;
    unsigned int i, st = 0, ctrl;

    memset(&r, 0, sizeof(r));
    ctrl = (in8(LPT_CONTROLLO) & ~(LPT_CT_STROBE | LPT_CT_IRQ | LPT_CT_INGRESSO | LPT_CT_AUTOFD)) & 0x0F;
    ctrl |= LPT_CT_NINIT | LPT_CT_SELECTIN;
    out8(LPT_CONTROLLO, ctrl | (g_fronti ? LPT_CT_IRQ : 0));
    for (i = 0; i < n; i++) {
        /* a printer feeding paper says Busy for seconds; 30 s is a jam */
        r.rc = aspetta_stato(LPT_ST_NBUSY, LPT_ST_NBUSY, 30000, &st);
        if (r.rc < 0) break;
        out8(LPT_DATI, d[i]);
        out8(LPT_CONTROLLO, ctrl | LPT_CT_STROBE);  /* >= 0.5 us: a syscall is more */
        out8(LPT_CONTROLLO, ctrl);
        r.scritti++;
    }
    g_scritti += r.scritti;
    r.stato = (unsigned char)in8(LPT_STATO);
    rispondi(pid, LPT_MSG_SCRIVI, &r, sizeof(r));
}

/* The IEEE 1284 Device ID in nibble mode: negotiation with the extensibility
 * byte 0x04, the reverse transfer four bits at a time on nFault, Select,
 * PError and Busy, the termination. Control values as pins (see
 * lpt_proto.h for the inverted bits):
 *   0x0C compatibility idle      nSelectIn low, nAutoFd high
 *   0x06 HostBusy low            nSelectIn high (1284 active), nAutoFd low
 *   0x04 HostBusy high           nSelectIn high, nAutoFd high
 *   0x07 nStrobe low             the peripheral latches the extensibility */
static int nibble(unsigned int *v)
{
    unsigned int st;

    out8(LPT_CONTROLLO, 0x06);
    if (aspetta_stato(LPT_ST_NACK, 0, 50, &st) < 0) return -ETIMEDOUT;
    *v = ((st >> 3) & 7u) | ((st & LPT_ST_NBUSY) ? 0u : 8u);
    out8(LPT_CONTROLLO, 0x04);
    if (aspetta_stato(LPT_ST_NACK, LPT_ST_NACK, 50, &st) < 0) return -ETIMEDOUT;
    return 0;
}

static void termina_1284(void)
{
    out8(LPT_CONTROLLO, 0x0C);
    if (aspetta_stato(LPT_ST_NACK, 0, 50, NULL) == 0) {
        out8(LPT_CONTROLLO, 0x0E);
        aspetta_stato(LPT_ST_NACK, LPT_ST_NACK, 50, NULL);
    }
    out8(LPT_CONTROLLO, 0x0C);
}

static void id_leggi(int pid)
{
    static LptId r;
    unsigned int st, lo, hi, quanti = 0xFFFFu, n = 0;
    unsigned char due[2];

    memset(&r, 0, sizeof(r));
    out8(LPT_CONTROLLO, 0x0C);
    out8(LPT_DATI, 0x04);
    out8(LPT_CONTROLLO, 0x06);
    if (aspetta_stato(LPT_ST_NACK | LPT_ST_PE | LPT_ST_SELECT | LPT_ST_NERROR,
                      LPT_ST_PE | LPT_ST_SELECT | LPT_ST_NERROR, 50, &st) < 0) {
        out8(LPT_CONTROLLO, 0x0C);
        r.rc = -ETIMEDOUT;
        rispondi(pid, LPT_MSG_ID, &r, 8);
        return;
    }
    out8(LPT_CONTROLLO, 0x07);
    out8(LPT_CONTROLLO, 0x04);
    if (aspetta_stato(LPT_ST_NACK, LPT_ST_NACK, 50, &st) < 0 || !(st & LPT_ST_SELECT)) {
        termina_1284();
        r.rc = (st & LPT_ST_SELECT) ? -ETIMEDOUT : -EOPNOTSUPP;
        rispondi(pid, LPT_MSG_ID, &r, 8);
        return;
    }
    /* nFault low = a byte is waiting (nDataAvail) */
    while (n < quanti && !(in8(LPT_STATO) & LPT_ST_NERROR)) {
        if (nibble(&lo) < 0 || nibble(&hi) < 0) { r.rc = -ETIMEDOUT; break; }
        if (n < 2) {
            due[n] = (unsigned char)(lo | (hi << 4));
            if (n == 1) quanti = ((unsigned int)due[0] << 8) | due[1];
        } else if (n - 2 < LPT_ID_MAX - 1) {
            r.id[n - 2] = (char)(lo | (hi << 4));
        }
        n++;
    }
    termina_1284();
    r.len = n > 2 ? n - 2 : 0;
    if (r.len > LPT_ID_MAX - 1) r.len = LPT_ID_MAX - 1;
    r.id[r.len] = 0;
    if (r.rc == 0 && n < 2) r.rc = -ENODATA;
    rispondi(pid, LPT_MSG_ID, &r, 8 + r.len + 1);
}

static void pin(int pid, unsigned int tipo, int rc)
{
    LptPin p;

    p.dati = (unsigned char)in8(LPT_DATI);
    p.stato = (unsigned char)in8(LPT_STATO);
    p.controllo = (unsigned char)in8(LPT_CONTROLLO);
    p.ecr = g_ecp ? (unsigned char)in8(LPT_ECR) : 0;
    p.rc = rc;
    rispondi(pid, tipo, &p, sizeof(p));
}

static void info(int pid)
{
    LptInfo i;

    memset(&i, 0, sizeof(i));
    puo(-1);                    /* a holder that is gone is not shown */
    i.base = g_base; i.irq = g_irq; i.modi = g_modi;
    i.chi = g_chi; i.uso = g_uso; i.fronti_attivi = (unsigned int)g_fronti;
    i.hz = g_hz;
    i.dati = (unsigned char)in8(LPT_DATI);
    i.stato = (unsigned char)in8(LPT_STATO);
    i.controllo = (unsigned char)in8(LPT_CONTROLLO);
    i.ecr = g_ecp ? (unsigned char)in8(LPT_ECR) : 0;
    i.scritti = g_scritti;
    rispondi(pid, LPT_MSG_INFO, &i, sizeof(i));
}

static int onda_aggiungi(const LptOndaPezzo *p, unsigned int len)
{
    unsigned int n = p->n;

    if (len < 4 || n > LPT_ONDA_PEZZO || len < 4 + n * 4u) return -EINVAL;
    if (g_onda_n + n > g_onda_cap) {
        unsigned int nc = g_onda_cap ? g_onda_cap : 4096;
        unsigned int *q;

        while (nc < g_onda_n + n) nc *= 2;
        q = realloc(g_onda, nc * sizeof(*q));
        if (q == NULL) return -ENOMEM;
        g_onda = q; g_onda_cap = nc;
    }
    memcpy(g_onda + g_onda_n, p->ns, n * 4u);
    g_onda_n += n;
    return 0;
}

/* A message during the wave: FERMA from the holder stops it, the others are
 * told the port is busy. 1 = stop. */
static int onda_posta(void)
{
    IpcMessage m;

    while (ipc_recv_timeout(&m, g_buf, sizeof(g_buf), IPC_SUBITO) >= 0) {
        if (m.sender_pid == IPC_SENDER_KERNEL) continue;
        if (m.tipo == LPT_MSG_ONDA_FERMA && (int)m.sender_pid == g_chi) return 1;
        if (m.tipo == LPT_MSG_INFO) { info((int)m.sender_pid); continue; }
        esito((int)m.sender_pid, m.tipo, -EBUSY);
    }
    return 0;
}

/* ! THE WAVE IS MADE HERE, NOT BY THE CLIENT: an IPC message per half-wave
 * would cost more than a half-wave lasts. Busy waiting on the fine clock,
 * on a processor reserved to this process when there are two or more (the
 * other processes go to the others). Each edge is late by the cost of one
 * `out` (0.324, ioport_diretto), of one ioport_out with an older kernel -
 * more if the other processor holds the kernel's lock - and of whatever
 * interrupt this processor takes meanwhile: that is measured and given back
 * (ritardo_max_ns, ritardi_10us). */
static void onda_suona(int pid, const LptOndaSuona *o)
{
    LptOndaFatta r;
    unsigned long long t, ora, due_ms;
    unsigned int i, livello, ritardo, cpu = 0;
    long ncpu = sysconf(_SC_NPROCESSORS_ONLN);

    memset(&r, 0, sizeof(r));
    if (g_onda_n == 0) { r.rc = -ENODATA; rispondi(pid, LPT_MSG_ONDA_SUONA, &r, sizeof(r)); return; }
    if (g_hz < 1000000u) { r.rc = -EOPNOTSUPP; rispondi(pid, LPT_MSG_ONDA_SUONA, &r, sizeof(r)); return; }
    if (ncpu > 1 && cpu_riserva(getpid(), (unsigned int)(ncpu - 1)) == 0) {
        cpu = (unsigned int)(ncpu - 1);
        usleep(20000);          /* the next scheduling takes it there */
    }
    due_ms = ns_a_tick(2000000u);
    livello = o->inizio;
    out8(LPT_DATI, livello);
    t = orologio();
    for (i = 0; i < g_onda_n; i++) {
        unsigned long long d = ns_a_tick(g_onda[i]);

        t += d;
        /* the mail only in a long pause, where looking costs nothing */
        if (d > due_ms && onda_posta()) { r.rc = -EINTR; break; }
        while ((ora = orologio()) < t) ;
        livello ^= o->maschera;
        out8(LPT_DATI, livello);
        ritardo = tick_a_ns(orologio() - t);
        if (ritardo > r.ritardo_max_ns) r.ritardo_max_ns = ritardo;
        if (ritardo > 10000u) r.ritardi_10us++;
        r.suonate++;
    }
    (void)ora;
    if (cpu) cpu_libera(cpu);
    r.processore = cpu;
    r.diretto = (unsigned int)g_diretto;
    g_onda_n = 0;
    rispondi(pid, LPT_MSG_ONDA_SUONA, &r, sizeof(r));
}

static int servizio(unsigned int base, unsigned int irq, unsigned int n)
{
    char       nome[IPC_NAME_LEN];
    IpcMessage m;
    int        pid, fine = 0;

    g_base = base;
    g_modi = sonda(base, &g_ecp);
    if (g_modi == 0) { printf("lpt: nessuna porta a 0x%x\n", base); return 1; }
    g_base = base;
    ioport_bind(base, base == 0x3BC ? 3 : 8);
    if (g_ecp) ioport_bind(base + 0x400, 3);
    g_diretto = (ioport_diretto(1) == 0);
    g_irq = irq;
    orologio_apri();
    snprintf(nome, sizeof(nome), "%s%u", LPT_SERVIZIO, n);
    if (ipc_register(nome) < 0) { printf("lpt: il nome '%s' e' gia' preso\n", nome); return 1; }

    while (!fine) {
        int len = ipc_recv(&m, g_buf, sizeof(g_buf));

        if (len < 0) continue;
        len = (int)m.len;
        pid = (int)m.sender_pid;
        if (m.sender_pid == IPC_SENDER_KERNEL) {
            if (m.tipo == IPC_TYPE_IRQ_NOTIFY) {
                if (g_fronti) fronti_drena();
                else if (g_irq_preso) irq_done(g_irq);
            }
            continue;
        }
        switch (m.tipo) {
        case LPT_MSG_INFO:
            info(pid);
            break;
        case LPT_MSG_PRENDI:
            if (!puo(pid)) { esito(pid, m.tipo, -EBUSY); break; }
            g_chi = pid;
            g_uso = len >= 4 ? ((LptPrendi *)g_buf)->uso : 0;
            esito(pid, m.tipo, 0);
            break;
        case LPT_MSG_LASCIA:
            if (g_chi == pid) rilascia();
            esito(pid, m.tipo, 0);
            break;
        case LPT_MSG_SCRIVI:
            if (!puo(pid)) { LptScritto r; memset(&r, 0, sizeof(r)); r.rc = -EBUSY;
                             rispondi(pid, m.tipo, &r, sizeof(r)); break; }
            scrivi(pid, g_buf, (unsigned int)len);
            break;
        case LPT_MSG_PIN_LEGGI:
            pin(pid, m.tipo, 0);
            break;
        case LPT_MSG_PIN_SCRIVI: {
            const LptPinScrivi *p = (const LptPinScrivi *)g_buf;
            unsigned int r;

            if (g_chi != pid) { pin(pid, m.tipo, -ENOLCK); break; }
            if (len < 3 || (p->registro != LPT_DATI && p->registro != LPT_CONTROLLO)) {
                pin(pid, m.tipo, -EINVAL); break;
            }
            r = in8(p->registro);
            if (p->registro == LPT_CONTROLLO) r &= 0x3Fu;
            out8(p->registro, (r & ~(unsigned int)p->maschera) | (p->valore & p->maschera));
            pin(pid, m.tipo, 0);
            break;
        }
        case LPT_MSG_ID:
            if (!puo(pid)) { LptId r; r.rc = -EBUSY; r.len = 0; rispondi(pid, m.tipo, &r, 8); break; }
            id_leggi(pid);
            break;
        case LPT_MSG_FRONTI_ACCENDI:
            if (g_chi != pid) { esito(pid, m.tipo, -ENOLCK); break; }
            esito(pid, m.tipo, fronti_accendi(len >= 4 ? ((LptFrontiAccendi *)g_buf)->quanti : 0));
            break;
        case LPT_MSG_FRONTI_SPEGNI:
            if (g_chi == pid) fronti_spegni();
            esito(pid, m.tipo, g_chi == pid ? 0 : -ENOLCK);
            break;
        case LPT_MSG_FRONTI_LEGGI:
            if (g_chi != pid) { LptFronti r; memset(&r, 0, LPT_FRONTI_TESTA); r.rc = -ENOLCK;
                                rispondi(pid, m.tipo, &r, LPT_FRONTI_TESTA); break; }
            fronti_leggi(pid, len >= 4 ? ((LptFrontiChiedi *)g_buf)->max : 0);
            break;
        case LPT_MSG_ONDA_AGGIUNGI:
            if (g_chi != pid) { esito(pid, m.tipo, -ENOLCK); break; }
            esito(pid, m.tipo, onda_aggiungi((const LptOndaPezzo *)g_buf, (unsigned int)len));
            break;
        case LPT_MSG_ONDA_SUONA: {
            LptOndaSuona o;

            if (g_chi != pid) { esito(pid, m.tipo, -ENOLCK); break; }
            memset(&o, 0, sizeof(o));
            if (len >= 2) memcpy(&o, g_buf, 2);
            onda_suona(pid, &o);
            break;
        }
        case LPT_MSG_FINE:
            if (!puo(pid)) { esito(pid, m.tipo, -EBUSY); break; }
            rilascia();
            esito(pid, m.tipo, 0);
            fine = 1;
            break;
        default:
            esito(pid, m.tipo, -EINVAL);
            break;
        }
    }
    return 0;
}

/* --- the command line ------------------------------------------------------------ */

static unsigned char g_risposta[IPC_MSG_MAX_DATA];

/* A request to lptN and its answer in g_risposta; its length, or -1. */
static int chiedi(int pid, unsigned int tipo, const void *d, unsigned int len)
{
    IpcMessage m;
    int t;

    if (ipc_send((unsigned int)pid, tipo, d, len) < 0) return -1;
    for (t = 0; t < 16; t++) {
        /* a wave or a job may take long: the answer comes at its end */
        if (ipc_recv_timeout(&m, g_risposta, sizeof(g_risposta), tipo == LPT_MSG_ONDA_SUONA ? 0 : 60000) < 0)
            return -1;
        if ((int)m.sender_pid != pid || m.tipo != LPT_RISPOSTA(tipo)) continue;
        return (int)m.len;
    }
    return -1;
}
static int chiedi_esito(int pid, unsigned int tipo, const void *d, unsigned int len)
{
    if (chiedi(pid, tipo, d, len) < 4) return -EIO;
    return ((LptEsito *)g_risposta)->rc;
}

static int servizio_di(unsigned int n)
{
    char nome[IPC_NAME_LEN];

    snprintf(nome, sizeof(nome), "%s%u", LPT_SERVIZIO, n);
    return ipc_lookup(nome);
}

static int apri(unsigned int n)
{
    int pid = servizio_di(n);

    if (pid <= 0) printf("lpt: il servizio lpt%u non c'e' (lpt.drv -avvia)\n", n);
    return pid;
}

static const char *uso_nome(unsigned int u)
{
    switch (u) {
    case LPT_USO_PIN:     return "piedini";
    case LPT_USO_STAMPA:  return "stampa";
    case LPT_USO_NASTRO:  return "datasette";
    case LPT_USO_ZIP:     return "ZIP";
    case LPT_USO_SCANNER: return "scanner";
    }
    return "?";
}

static void modi_scrivi(unsigned int m)
{
    printf("%s%s%s%s", (m & LPT_MODO_SPP) ? "SPP" : "", (m & LPT_MODO_PS2) ? " PS/2" : "",
           (m & LPT_MODO_EPP) ? " EPP" : "", (m & LPT_MODO_ECP) ? " ECP" : "");
}

static void stato_scrivi(unsigned int s)
{
    printf("  stato     0x%02x  busy=%u ack=%u pe=%u select=%u errore=%u\n", s,
           (s & LPT_ST_NBUSY) ? 0u : 1u, (s & LPT_ST_NACK) ? 1u : 0u, (s & LPT_ST_PE) ? 1u : 0u,
           (s & LPT_ST_SELECT) ? 1u : 0u, (s & LPT_ST_NERROR) ? 0u : 1u);
}

static int elenco(void)
{
    unsigned int n, trovati = 0;

    for (n = 0; n < PORTE_N; n++) {
        int pid = servizio_di(n);
        LptInfo *i = (LptInfo *)g_risposta;

        if (pid <= 0) continue;
        if (chiedi(pid, LPT_MSG_INFO, 0, 0) < (int)sizeof(LptInfo)) {
            printf("lpt%u: non risponde\n", n);
            continue;
        }
        trovati++;
        printf("lpt%u: 0x%x", n, i->base);
        if (i->irq) printf(" irq %u", i->irq);
        printf(", ");
        modi_scrivi(i->modi);
        printf("\n");
        if (i->chi) printf("  la tiene il processo %d (%s)%s\n", i->chi, uso_nome(i->uso),
                           i->fronti_attivi ? ", fronti accesi" : "");
        else        printf("  libera\n");
        printf("  dati      0x%02x\n", i->dati);
        stato_scrivi(i->stato);
        printf("  controllo 0x%02x\n", i->controllo);
        if (i->modi & LPT_MODO_ECP) printf("  ecr       0x%02x\n", i->ecr);
        printf("  byte stampati: %u\n", i->scritti);
    }
    if (trovati == 0) {
        printf("lpt: nessun servizio acceso. Si accende con  lpt.drv -avvia\n");
        return 1;
    }
    return 0;
}

static int numero(const char *p, unsigned int *out)
{
    char *fine;
    unsigned long v;

    if (*p == 0) return -1;
    v = strtoul(p, &fine, 0);
    if (*fine) return -1;
    *out = (unsigned int)v;
    return 0;
}

static int avvia(const char *argv0, unsigned int solo, int irq_dato)
{
    unsigned int i, n = 0, avviati = 0;
    const char *via = strchr(argv0, '/') ? argv0 : "/dev/lpt.drv";

    if (getuid() != 0) { printf("lpt: -avvia solo da root\n"); return 1; }
    for (i = 0; i < PORTE_N; i++) {
        unsigned int base = g_candidate[i], modi, irq;
        int ecp, pid;
        char a1[48], *av[3];

        if (solo && base != solo) continue;
        pid = servizio_di(n);
        if (pid > 0 && chiedi(pid, LPT_MSG_INFO, 0, 0) >= (int)sizeof(LptInfo) &&
            ((LptInfo *)g_risposta)->base == base) {
            printf("lpt%u: 0x%x c'e' gia'\n", n, base);
            n++;
            continue;
        }
        modi = sonda(base, &ecp);
        if (modi == 0) continue;
        irq = irq_dato >= 0 ? (unsigned int)irq_dato : irq_di(base);
        snprintf(a1, sizeof(a1), "-servizio:0x%x:%u:%u", base, irq, n);
        av[0] = (char *)via; av[1] = a1; av[2] = 0;
        if (spawn(via, av) < 0) { printf("lpt: %s non si avvia\n", via); return 1; }
        snprintf(a1, sizeof(a1), "%s%u", LPT_SERVIZIO, n);
        if (ipc_attendi(a1, 5000) <= 0) { printf("lpt: %s non si e' registrato\n", a1); return 1; }
        printf("lpt%u: 0x%x", n, base);
        if (irq) printf(" irq %u", irq);
        printf(", ");
        modi_scrivi(modi);
        printf("\n");
        n++; avviati++;
    }
    if (n == 0) { printf("lpt: nessuna porta parallela\n"); return 1; }
    (void)avviati;
    return 0;
}

static int ferma(void)
{
    unsigned int n;
    int rc = 0;

    if (getuid() != 0) { printf("lpt: -ferma solo da root\n"); return 1; }
    for (n = 0; n < PORTE_N; n++) {
        int pid = servizio_di(n), r;

        if (pid <= 0) continue;
        r = chiedi_esito(pid, LPT_MSG_FINE, 0, 0);
        if (r == 0) printf("lpt%u: fermato\n", n);
        else { printf("lpt%u: non si ferma (%d: e' in uso)\n", n, r); rc = 1; }
    }
    return rc;
}

static int prendi(int pid, unsigned int uso)
{
    LptPrendi p;
    int rc;

    p.uso = uso;
    rc = chiedi_esito(pid, LPT_MSG_PRENDI, &p, sizeof(p));
    if (rc == -EBUSY) printf("lpt: la porta e' in uso da un altro programma\n");
    else if (rc < 0)  printf("lpt: la porta non si prende (%d)\n", rc);
    return rc;
}

static int stampa(int pid, const char *file)
{
    unsigned char b[1400];
    int fd, n, rc = 0;
    unsigned int tot = 0;

    fd = open(file, O_RDONLY);
    if (fd < 0) { printf("lpt: non riesco ad aprire %s\n", file); return 1; }
    if (prendi(pid, LPT_USO_STAMPA) < 0) { close(fd); return 1; }
    while ((n = (int)read(fd, b, sizeof(b))) > 0) {
        LptScritto *r = (LptScritto *)g_risposta;

        if (chiedi(pid, LPT_MSG_SCRIVI, b, (unsigned int)n) < (int)sizeof(LptScritto)) {
            printf("lpt: il servizio non risponde\n"); rc = 1; break;
        }
        tot += r->scritti;
        if (r->rc < 0) {
            printf("lpt: la stampante e' occupata da 30 s dopo %u byte\n", tot);
            stato_scrivi(r->stato);
            if (r->stato & LPT_ST_PE) printf("  (manca la carta)\n");
            if (!(r->stato & LPT_ST_SELECT)) printf("  (non e' in linea o non e' accesa)\n");
            rc = 1; break;
        }
    }
    close(fd);
    chiedi_esito(pid, LPT_MSG_LASCIA, 0, 0);
    if (rc == 0) printf("lpt: %u byte alla stampante\n", tot);
    return rc;
}

static int id(int pid)
{
    LptId *r = (LptId *)g_risposta;

    if (chiedi(pid, LPT_MSG_ID, 0, 0) < 8) { printf("lpt: il servizio non risponde\n"); return 1; }
    if (r->rc == -ETIMEDOUT) { printf("lpt: nessuna risposta IEEE 1284 (niente collegato, o non la sa)\n"); return 1; }
    if (r->rc == -EOPNOTSUPP) { printf("lpt: la periferica non da' il Device ID\n"); return 1; }
    if (r->rc < 0) { printf("lpt: Device ID non letto (%d)\n", r->rc); return 1; }
    printf("%s\n", r->id);
    return 0;
}

static int pin_scrivi(int pid, unsigned int reg, unsigned int v)
{
    LptPinScrivi p;
    LptPin *r = (LptPin *)g_risposta;
    int rc;

    if (prendi(pid, LPT_USO_PIN) < 0) return 1;
    p.registro = (unsigned char)reg; p.valore = (unsigned char)v; p.maschera = 0xFF;
    rc = chiedi(pid, LPT_MSG_PIN_SCRIVI, &p, sizeof(p)) >= (int)sizeof(LptPin) ? r->rc : -EIO;
    chiedi_esito(pid, LPT_MSG_LASCIA, 0, 0);
    if (rc < 0) { printf("lpt: non scritto (%d)\n", rc); return 1; }
    printf("dati 0x%02x, controllo 0x%02x\n", r->dati, r->controllo);
    return 0;
}

static int pin_mostra(int pid)
{
    LptPin *r = (LptPin *)g_risposta;

    if (chiedi(pid, LPT_MSG_PIN_LEGGI, 0, 0) < (int)sizeof(LptPin)) { printf("lpt: il servizio non risponde\n"); return 1; }
    printf("  dati      0x%02x  D7..D0 = piedini 9..2\n", r->dati);
    stato_scrivi(r->stato);
    printf("  controllo 0x%02x  strobe=%u autofd=%u init=%u selectin=%u irq=%u ingresso=%u\n",
           r->controllo, (r->controllo & 1) ? 1u : 0u, (r->controllo & 2) ? 1u : 0u,
           (r->controllo & 4) ? 0u : 1u, (r->controllo & 8) ? 1u : 0u,
           (r->controllo & 0x10) ? 1u : 0u, (r->controllo & 0x20) ? 1u : 0u);
    printf("  (busy, strobe, autofd, init, selectin: 1 = attivo, con la loro logica negata)\n");
    return 0;
}

/* The edges, how many and the time between them. */
typedef struct {
    unsigned long long prima, tot_tick;
    unsigned int n, intervalli, min_ns, max_ns, persi, spuri;
} Conti;

static int fronti_prendi_tutti(int pid, Conti *c)
{
    LptFronti *f = (LptFronti *)g_risposta;
    LptFrontiChiedi q;
    unsigned int i;
    int len;

    q.max = LPT_FRONTI_PEZZO;
    do {
        len = chiedi(pid, LPT_MSG_FRONTI_LEGGI, &q, sizeof(q));
        if (len < (int)LPT_FRONTI_TESTA || f->rc < 0) return len < (int)LPT_FRONTI_TESTA ? -EIO : f->rc;
        for (i = 0; i < f->n; i++) {
            if (c->n > 0) {
                unsigned long long d = f->t[i] - c->prima;
                unsigned int ns = tick_a_ns(d);

                c->tot_tick += d;
                if (c->intervalli == 0 || ns < c->min_ns) c->min_ns = ns;
                if (ns > c->max_ns) c->max_ns = ns;
                c->intervalli++;
            }
            c->prima = f->t[i];
            c->n++;
        }
        c->persi = f->persi;
        c->spuri = f->spuri;
    } while (f->n == LPT_FRONTI_PEZZO);
    return 0;
}

static void conti_scrivi(const Conti *c)
{
    printf("fronti: %u", c->n);
    if (c->intervalli) {
        unsigned int media = tick_a_ns(div64(c->tot_tick, c->intervalli));

        printf(", fra uno e l'altro min %u.%03u us, media %u.%03u us, max %u.%03u us",
               c->min_ns / 1000, c->min_ns % 1000, media / 1000, media % 1000,
               c->max_ns / 1000, c->max_ns % 1000);
    }
    printf(", persi %u", c->persi);
    if (c->spuri) printf(" (%u arrivati come IRQ spuri)", c->spuri);
    printf("\n");
}

static int fronti(int pid, unsigned int secondi, unsigned int prova)
{
    LptFrontiAccendi a;
    Conti c;
    int rc;

    memset(&c, 0, sizeof(c));
    if (prendi(pid, LPT_USO_PIN) < 0) return 1;
    a.quanti = 0;
    rc = chiedi_esito(pid, LPT_MSG_FRONTI_ACCENDI, &a, sizeof(a));
    if (rc < 0) {
        if (rc == -ENODEV) printf("lpt: questa porta non ha una linea di interrupt (-irq:N)\n");
        else if (rc == -EBUSY) printf("lpt: la linea dell'interrupt e' condivisa con un altro driver\n");
        else printf("lpt: i fronti non si accendono (%d; serve il kernel 0.323)\n", rc);
        chiedi_esito(pid, LPT_MSG_LASCIA, 0, 0);
        return 1;
    }
    if (prova) {
        /* ! THE PORT MAKES ITS OWN INTERRUPTS, as QEMU's does: a write of the
         * control register with nSelectIn and nInit, the interrupt enabled
         * and nStrobe idle raises the line; reading the status lowers it.
         * On a real port nACK is an input and this makes nothing. */
        LptPinScrivi p;
        unsigned int i;

        p.registro = LPT_CONTROLLO; p.valore = LPT_CT_RIPOSO | LPT_CT_IRQ; p.maschera = 0x3F;
        for (i = 0; i < prova; i++) {
            chiedi(pid, LPT_MSG_PIN_SCRIVI, &p, sizeof(p));
            if (i % 32 == 31) fronti_prendi_tutti(pid, &c);
        }
        usleep(50000);
    } else {
        unsigned int fine = uptime_ms() + secondi * 1000u;

        while ((int)(uptime_ms() - fine) < 0) {
            usleep(100000);
            if (fronti_prendi_tutti(pid, &c) < 0) break;
        }
    }
    rc = fronti_prendi_tutti(pid, &c);
    chiedi_esito(pid, LPT_MSG_FRONTI_SPEGNI, 0, 0);
    chiedi_esito(pid, LPT_MSG_LASCIA, 0, 0);
    if (rc < 0) { printf("lpt: fronti non letti (%d)\n", rc); return 1; }
    conti_scrivi(&c);
    if (prova) printf("fronti attesi: %u\n", prova);
    return 0;
}

static int onda_prova(int pid, unsigned int n, unsigned int us)
{
    static LptOndaPezzo p;
    LptOndaSuona s;
    LptOndaFatta *r = (LptOndaFatta *)g_risposta;
    unsigned int fatti = 0, i, rc;

    if (prendi(pid, LPT_USO_PIN) < 0) return 1;
    while (fatti < n) {
        p.n = n - fatti > LPT_ONDA_PEZZO ? LPT_ONDA_PEZZO : n - fatti;
        for (i = 0; i < p.n; i++) p.ns[i] = us * 1000u;
        rc = (unsigned int)chiedi_esito(pid, LPT_MSG_ONDA_AGGIUNGI, &p, 4 + p.n * 4u);
        if ((int)rc < 0) { printf("lpt: onda non accettata (%d)\n", (int)rc); chiedi_esito(pid, LPT_MSG_LASCIA, 0, 0); return 1; }
        fatti += p.n;
    }
    memset(&s, 0, sizeof(s));
    s.maschera = 0x02; s.inizio = 0;
    if (chiedi(pid, LPT_MSG_ONDA_SUONA, &s, sizeof(s)) < (int)sizeof(LptOndaFatta)) {
        printf("lpt: il servizio non risponde\n"); chiedi_esito(pid, LPT_MSG_LASCIA, 0, 0); return 1;
    }
    chiedi_esito(pid, LPT_MSG_LASCIA, 0, 0);
    if (r->rc == -EOPNOTSUPP) { printf("lpt: per l'onda serve un orologio fine (TSC)\n"); return 1; }
    if (r->rc < 0) { printf("lpt: onda interrotta (%d) dopo %u\n", r->rc, r->suonate); return 1; }
    printf("onda: %u mezze onde di %u us su D1, la piu' in ritardo di %u.%03u us, %u oltre 10 us",
           r->suonate, us, r->ritardo_max_ns / 1000, r->ritardo_max_ns % 1000, r->ritardi_10us);
    if (r->processore) printf(", processore %u riservato", r->processore);
    printf(r->diretto ? ", in/out diretti" : ", in/out con le chiamate di sistema");
    printf("\n");
    return 0;
}

static int installa(int togli)
{
    int rc;

    if (getuid() != 0) { printf("lpt: -install e -remove solo da root\n"); return 1; }
    rc = avvio_blocco("lpt.drv", NULL);
    if (rc >= 0 && !togli)
        rc = avvio_blocco("lpt.drv",
                          "# La porta parallela: stampanti, ZIP, scanner, datasette,\n"
                          "# schede di prova. `lpt.drv -remove` toglie questo blocco.\n"
                          "lpt.drv -avvia\n");
    if (rc < 0) { printf("lpt: non riesco a scrivere /boot/avvio.sh (errno %d)\n", errno); return 1; }
    if (togli) printf("lpt: tolto da /boot/avvio.sh; adesso: `lpt.drv -ferma`\n");
    else       printf("lpt: in /boot/avvio.sh, dal prossimo avvio\n");
    return 0;
}

static void aiuto(void)
{
    printf("lpt.drv - la porta parallela (stampanti, ZIP, scanner, datasette, schede)\n\n");
    printf("  lpt.drv                  le porte, cosa sanno fare, chi le usa\n");
    printf("  lpt.drv -avvia           cerca le porte e accende un servizio per ognuna\n");
    printf("                           (-porta:0x378 solo quella, -irq:N la linea)\n");
    printf("  lpt.drv -ferma           spegne i servizi\n");
    printf("  lpt.drv -stampa FILE     FILE alla stampante\n");
    printf("  lpt.drv -id              il Device ID IEEE 1284 di cio' che e' collegato\n");
    printf("  lpt.drv -pin             i registri piedino per piedino\n");
    printf("  lpt.drv -dati:V          le linee dei dati a V\n");
    printf("  lpt.drv -controllo:V     il registro di controllo a V\n");
    printf("  lpt.drv -fronti:S        per S secondi i fronti su nACK (piedino 10)\n");
    printf("  lpt.drv -prova-fronti:N  N interrupt fatti dalla porta (QEMU)\n");
    printf("  lpt.drv -prova-onda:N:US N mezze onde di US microsecondi su D1\n");
    printf("  lpt.drv -install         -avvia a ogni accensione, /boot/avvio.sh\n");
    printf("  lpt.drv -remove          lo toglie\n");
    printf("  -n:N                     la porta dei comandi (0 = lpt0)\n");
}

int main(int argc, char **argv)
{
    const char  *cmd = NULL, *file = NULL;
    unsigned int n = 0, solo = 0, v, a, b;
    int          irq = -1, i, pid;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-n:", 3) == 0) {
            if (numero(argv[i] + 3, &n) != 0 || n >= PORTE_N) { printf("lpt: -n:N vuole 0, 1 o 2\n"); return 1; }
        } else if (strncmp(argv[i], "-porta:", 7) == 0) {
            if (numero(argv[i] + 7, &solo) != 0) { printf("lpt: -porta: vuole un indirizzo\n"); return 1; }
        } else if (strncmp(argv[i], "-irq:", 5) == 0) {
            if (numero(argv[i] + 5, &v) != 0 || v > 15) { printf("lpt: -irq: vuole 0..15\n"); return 1; }
            irq = (int)v;
        } else if (cmd == NULL) {
            cmd = argv[i];
        } else if (file == NULL) {
            file = argv[i];
        }
    }
    if (cmd == NULL || strcmp(cmd, "-stato") == 0) return elenco();
    if (strncmp(cmd, "-servizio:", 10) == 0) {
        unsigned int base, linea, num;
        char *p = (char *)cmd + 10;

        base = (unsigned int)strtoul(p, &p, 0);
        if (*p++ != ':') return 1;
        linea = (unsigned int)strtoul(p, &p, 0);
        if (*p++ != ':') return 1;
        num = (unsigned int)strtoul(p, &p, 0);
        return servizio(base, linea, num);
    }
    if (strcmp(cmd, "-i") == 0) {
        int ecp;

        if (servizio_di(0) > 0) return 0;
        for (v = 0; v < PORTE_N; v++) if (sonda(g_candidate[v], &ecp)) return 0;
        return 1;
    }
    if (strcmp(cmd, "-avvia") == 0) return avvia(argv[0], solo, irq);
    if (strcmp(cmd, "-ferma") == 0) return ferma();
    if (strcmp(cmd, "-install") == 0) return installa(0);
    if (strcmp(cmd, "-remove") == 0) return installa(1);
    if (strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) { aiuto(); return 0; }

    orologio_apri();
    if (strcmp(cmd, "-stampa") == 0) {
        if (file == NULL) { printf("lpt: -stampa FILE\n"); return 1; }
        if ((pid = apri(n)) <= 0) return 1;
        return stampa(pid, file);
    }
    if (strcmp(cmd, "-id") == 0)  { if ((pid = apri(n)) <= 0) return 1; return id(pid); }
    if (strcmp(cmd, "-pin") == 0) { if ((pid = apri(n)) <= 0) return 1; return pin_mostra(pid); }
    if (strncmp(cmd, "-dati:", 6) == 0 || strncmp(cmd, "-controllo:", 11) == 0) {
        int dati = cmd[1] == 'd';

        if (numero(cmd + (dati ? 6 : 11), &v) != 0 || v > 0xFF) { printf("lpt: il valore va da 0 a 255\n"); return 1; }
        if ((pid = apri(n)) <= 0) return 1;
        return pin_scrivi(pid, dati ? LPT_DATI : LPT_CONTROLLO, v);
    }
    if (strncmp(cmd, "-fronti:", 8) == 0) {
        if (numero(cmd + 8, &v) != 0 || v == 0) { printf("lpt: -fronti:S vuole i secondi\n"); return 1; }
        if ((pid = apri(n)) <= 0) return 1;
        return fronti(pid, v, 0);
    }
    if (strncmp(cmd, "-prova-fronti:", 14) == 0) {
        if (numero(cmd + 14, &v) != 0 || v == 0) { printf("lpt: -prova-fronti:N vuole quanti\n"); return 1; }
        if ((pid = apri(n)) <= 0) return 1;
        return fronti(pid, 0, v);
    }
    if (strncmp(cmd, "-prova-onda:", 12) == 0) {
        char *p = (char *)cmd + 12;

        a = (unsigned int)strtoul(p, &p, 0);
        if (*p++ != ':' || a == 0) { printf("lpt: -prova-onda:N:US\n"); return 1; }
        b = (unsigned int)strtoul(p, &p, 0);
        if (*p || b == 0 || b > 4000000u) { printf("lpt: -prova-onda:N:US, US da 1 a 4000000\n"); return 1; }
        if ((pid = apri(n)) <= 0) return 1;
        return onda_prova(pid, a, b);
    }
    printf("lpt: opzione sconosciuta '%s'\n\n", cmd);
    aiuto();
    return 1;
}
