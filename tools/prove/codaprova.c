/* =============================================================================
 * tools/prove/codaprova.c - the IPC of kernel 0.356 (voce 111, step 4)
 *
 *     codaprova          as root
 *
 *   1  the stamp: a child at uid 1000 writes to us, SYS_IPC_RECV_CHI says
 *      uid 1000 gid 1000 (and a message of our own, uid 0);
 *   2  the mailbox grows: 60 children send while we sleep 2.5 s; with four
 *      slots in the PCB (up to 0.355) 56 of them waited for our reading, now
 *      every send returns at once (the slowest under a second);
 *   3  the names table grows: a user registers 200 "1000:" names, past the
 *      128 of the old table.
 *
 * The syscall is called directly: the libc of the toolchain may be older
 * than ipc_recv_chi. Exit: how many of the three failed.
 * ============================================================================= */
#include "libc.h"
#include <unistd.h>
#include <sys/wait.h>

#define SYS_IPC_RECV_CHI 275

typedef struct { unsigned int pid, tipo, len, uid, gid; } Chi;

static int ricevi_chi(Chi *c, void *buf, unsigned int len, unsigned int ms)
{
    int r;
    __asm__ volatile("int $0x80"
        : "=a"(r)
        : "a"(SYS_IPC_RECV_CHI), "b"((unsigned long)c), "c"((unsigned long)buf),
          "d"((unsigned long)len), "S"((unsigned long)ms)
        : "memory");
    return r;
}

static int no;

static void esito(int ok, const char *cosa)
{
    printf("  [%s] %s\n", ok ? "OK" : "NO", cosa);
    if (!ok) no++;
}

int main(void)
{
    int          io = getpid(), i, figli, st;
    unsigned int buf[8], lento = 0, arrivati = 0;
    Chi          c;
    char         riga[128];

    if (getuid() != 0) { printf("codaprova: da root\n"); return 100; }

    /* --- 1. the stamp ----------------------------------------------------- */
    if (fork() == 0) {
        setgid(1000); setuid(1000);
        buf[0] = 111;
        ipc_send((unsigned)io, 7001, buf, sizeof(buf[0]));
        _exit(0);
    }
    if (ricevi_chi(&c, buf, sizeof(buf), 5000) < 0) esito(0, "timbro: nessun messaggio dal figlio");
    else {
        snprintf(riga, sizeof(riga), "timbro: dal figlio a uid 1000 arriva uid %u gid %u (tipo %u)",
                 c.uid, c.gid, c.tipo);
        esito(c.tipo == 7001 && c.uid == 1000 && c.gid == 1000, riga);
    }
    waitpid(-1, &st, 0);
    ipc_send((unsigned)io, 7002, NULL, 0);
    if (ricevi_chi(&c, buf, sizeof(buf), 2000) < 0) esito(0, "timbro: il mio messaggio non torna");
    else {
        snprintf(riga, sizeof(riga), "timbro: il mio (root) arriva uid %u gid %u", c.uid, c.gid);
        esito(c.tipo == 7002 && c.uid == 0 && c.gid == 0, riga);
    }

    /* --- 2. the mailbox grows -------------------------------------------- */
    for (figli = 0; figli < 60; figli++) {
        int p = fork();

        if (p < 0) break;
        if (p == 0) {
            unsigned int t0 = uptime_ms(), dt;

            ipc_send((unsigned)io, 7010, &t0, sizeof(t0));
            dt = uptime_ms() - t0;
            ipc_send((unsigned)io, 7011, &dt, sizeof(dt));
            _exit(0);
        }
    }
    usleep(2500000);
    while (arrivati < (unsigned)figli * 2u && ricevi_chi(&c, buf, sizeof(buf), 5000) >= 0) {
        if (c.tipo == 7011 && buf[0] > lento) lento = buf[0];
        if (c.tipo == 7010 || c.tipo == 7011) arrivati++;
    }
    for (i = 0; i < figli; i++) waitpid(-1, &st, 0);
    snprintf(riga, sizeof(riga), "casella: %d figli, %u messaggi, il piu' lento ha aspettato %u ms",
             figli, arrivati, lento);
    esito(figli == 60 && arrivati == 120u && lento < 1000u, riga);

    /* --- 3. the names table grows ---------------------------------------- */
    if (fork() == 0) {
        int presi = 0;

        setgid(1000); setuid(1000);
        for (i = 0; i < 200; i++) {
            char n[16];

            snprintf(n, sizeof(n), "1000:c%d", i);
            if (ipc_register(n) == 0) presi++;
        }
        _exit(presi == 200 ? 0 : 1 + presi % 250);
    }
    waitpid(-1, &st, 0);
    snprintf(riga, sizeof(riga), "nomi: un utente ne registra 200 (uscita del figlio %d)",
             WEXITSTATUS(st));
    esito((WEXITSTATUS(st)) == 0, riga);

    printf("codaprova: %d no\n", no);
    return no;
}
