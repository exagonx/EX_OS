/* =============================================================================
 * tools/prove/reteprova.c - an ordinary user against the network card driver
 * (voce 111, step 4, kernel 0.356)
 *
 *     reteprova [uid]        as root: goes down to uid (1000) and asks rete0
 *
 * What it asks the driver of "rete0" as an ordinary user, and what Linux says
 * to the same user (no CAP_NET_RAW: no packet socket):
 *     INFO        the card's state          linux: si (/sys/class/net)
 *     CONTATORI   the counters              linux: si (/sys/.../statistics)
 *     INVIA       a raw frame on the wire   linux: no
 *     RICEVI      the next frame from wire  linux: no
 *     REFERTO     a file written by root    linux: no
 * and to ip.drv and pci.drv (kernel 0.356):
 *     ip    CONFIG    the address of the machine    linux: no (CAP_NET_ADMIN)
 *     ip    LINK      which card                    linux: no
 *     pci   ABILITA   a device on                   linux: no (setpci)
 * These go deliberately short or to a card that does not exist, so a service
 * that takes them answers -EINVAL or -ENOENT and changes nothing; -EPERM is
 * the refusal.
 * A "si" where Linux says no is a HOLE. The frame sent is harmless: to a
 * locally administered address, ethertype 0x88B5 (IEEE "local experimental").
 * RICEVI on a kernel before 0.356 takes the card from ip.drv until ip.drv asks
 * again (RIARMO_MS, a second): at most one frame of somebody else lost.
 *
 * Exit: the number of holes.
 * ============================================================================= */
#include "libc.h"

#define NET_MSG_INFO       1
#define NET_MSG_INVIA      2
#define NET_MSG_RICEVI     3
#define NET_MSG_CONTATORI  4
#define NET_MSG_REFERTO    6
#define NET_MSG_STATO    128
#define NET_MSG_FRAME    129
#define NET_MSG_ESITO    130
#define NET_MSG_CONTEGGI 131

static int           g_pid;
static int           g_salta_frame;     /* after RICEVI: a late frame is not an answer */
static unsigned char g_buf[1600];

/* The driver's answer within ms, or -1 if none came: tipo in *tipo, the first
 * int of the payload (NetEsito.codice) in *codice. */
static int risposta(unsigned int ms, unsigned int *tipo, int *codice)
{
    unsigned int inizio = uptime_ms();

    for (;;) {
        IpcMessage   m;
        unsigned int passati = uptime_ms() - inizio;

        if (passati >= ms) return -1;
        if (ipc_recv_timeout(&m, g_buf, sizeof(g_buf), ms - passati) < 0) return -1;
        if ((int)m.sender_pid != g_pid) continue;
        if (g_salta_frame && m.tipo == NET_MSG_FRAME) continue;
        *tipo = m.tipo;
        *codice = 0;
        if (m.len >= sizeof(int)) memcpy(codice, g_buf, sizeof(int));
        return 0;
    }
}

static int falle, giuste;

static void esito(const char *cosa, int riuscito, int linux_si, const char *come)
{
    printf("  [%s] %-34s linux: %s   %s%s\n", riuscito ? "SI" : "NO", cosa,
           linux_si ? "si" : "no", come,
           (riuscito && !linux_si) ? "   <-- FALLA" : "");
    if (riuscito && !linux_si) falle++;
    else giuste++;
}

int main(int argc, char **argv)
{
    unsigned int  uid = argc > 1 ? (unsigned int)atoi(argv[1]) : 1000u;
    unsigned int  tipo;
    int           codice;
    char          come[64];
    unsigned char f[60];

    g_pid = ipc_lookup("rete0");
    if (g_pid <= 0) { printf("reteprova: nessun rete0 (%d)\n", g_pid); return 100; }
    if (getuid() == 0) {
        setgid(uid);
        if (setuid(uid) != 0) { printf("reteprova: setuid(%u) non riesce\n", uid); return 100; }
    }
    printf("reteprova: rete0 = PID %d, io uid %u\n", g_pid, (unsigned)getuid());

    /* INFO */
    ipc_send((unsigned)g_pid, NET_MSG_INFO, NULL, 0);
    if (risposta(2000, &tipo, &codice) < 0) esito("INFO (lo stato)", 0, 1, "nessuna risposta");
    else esito("INFO (lo stato)", tipo == NET_MSG_STATO, 1, tipo == NET_MSG_STATO ? "STATO" : "altro");

    /* CONTATORI */
    ipc_send((unsigned)g_pid, NET_MSG_CONTATORI, NULL, 0);
    if (risposta(2000, &tipo, &codice) < 0) esito("CONTATORI", 0, 1, "nessuna risposta");
    else esito("CONTATORI", tipo == NET_MSG_CONTEGGI, 1, tipo == NET_MSG_CONTEGGI ? "CONTEGGI" : "altro");

    /* INVIA: a harmless frame */
    memset(f, 0, sizeof(f));
    f[0] = 0x02;                        /* locally administered, nobody's */
    f[6] = 0x02; f[11] = 0x01;
    f[12] = 0x88; f[13] = 0xB5;         /* local experimental */
    memcpy(f + 14, "EX-OS reteprova voce 111", 24);
    ipc_send((unsigned)g_pid, NET_MSG_INVIA, f, sizeof(f));
    if (risposta(2000, &tipo, &codice) < 0) esito("INVIA (un frame grezzo)", 0, 0, "nessuna risposta");
    else {
        snprintf(come, sizeof(come), "ESITO %d", codice);
        esito("INVIA (un frame grezzo)", tipo == NET_MSG_ESITO && codice >= 0, 0, come);
    }

    /* RICEVI: the next frame of the wire */
    ipc_send((unsigned)g_pid, NET_MSG_RICEVI, NULL, 0);
    if (risposta(3000, &tipo, &codice) < 0)
        esito("RICEVI (il prossimo frame)", 1, 0, "presa, nessun frame in 3 s");
    else if (tipo == NET_MSG_FRAME)
        esito("RICEVI (il prossimo frame)", 1, 0, "un frame di altri");
    else {
        snprintf(come, sizeof(come), "ESITO %d", codice);
        esito("RICEVI (il prossimo frame)", 0, 0, come);
    }

    /* REFERTO: root writes where we say */
    {
        static const char via[] = "/tmp/reteprova.ref";

        g_salta_frame = 1;
        unlink(via);
        ipc_send((unsigned)g_pid, NET_MSG_REFERTO, via, sizeof(via));
        if (risposta(3000, &tipo, &codice) < 0) {
            struct stat st;
            int c = stat(via, &st) == 0;

            esito("REFERTO (file scritto da root)", c, 0, c ? "il file c'e'" : "nessuna risposta");
        } else {
            snprintf(come, sizeof(come), "ESITO %d", codice);
            esito("REFERTO (file scritto da root)", tipo == NET_MSG_ESITO && codice == 0, 0, come);
        }
        unlink(via);
    }

    /* ip.drv and pci.drv: requests that change nothing even if taken */
    {
        static const struct { const char *serv, *cosa; unsigned int tipo, atteso; const char *dati; unsigned int n; } q[] = {
            { "ip",  "ip CONFIG (l'indirizzo)",  1u, 128u, "x", 1u },          /* too short: -EINVAL */
            { "ip",  "ip LINK (la scheda)",     16u, 128u, "nessuna", 8u },    /* no such card: -ENOENT */
            { "pci", "pci ABILITA (un dispositivo)", 4u, 131u, "x", 1u },     /* too short: -EINVAL */
        };
        int rete = g_pid;
        unsigned int k;

        g_salta_frame = 0;
        for (k = 0; k < sizeof(q) / sizeof(q[0]); k++) {
            g_pid = ipc_lookup(q[k].serv);
            if (g_pid <= 0) { esito(q[k].cosa, 0, 0, "servizio assente"); continue; }
            ipc_send((unsigned)g_pid, q[k].tipo, q[k].dati, q[k].n);
            if (risposta(3000, &tipo, &codice) < 0) { esito(q[k].cosa, 0, 0, "nessuna risposta"); continue; }
            snprintf(come, sizeof(come), "ESITO %d", codice);
            esito(q[k].cosa, tipo == q[k].atteso && codice != -EPERM, 0, come);
        }
        g_pid = rete;
    }

    printf("reteprova: %d falle, %d come Linux\n", falle, giuste);
    return falle;
}
