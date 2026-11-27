/* =============================================================================
 * tools/prove/ppollprova.c - ppoll (kernel 0.373, SYS_PPOLL)
 *
 * Each line PPOLL <name> <result>; tools/prova_ppoll.sh checks them:
 *   pipe      a pipe with a byte: 1, POLLIN
 *   scade     nothing, 250 ms: 0 after 250-400 ms
 *   pendente  SIGUSR1 blocked and already pending, the mask lets it in:
 *             -1 EINTR at once, the handler ran, SIGUSR1 blocked again after
 *   figlio    a child sends SIGUSR1 300 ms into a 5 s ppoll: EINTR near 300
 *   tenuto    the mask keeps SIGUSR1 blocked: 0 at the timeout, still pending
 *   presa     the same as pendente with a UDP socket in the list
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>

static volatile int g_presi;
static void preso(int s) { (void)s; g_presi++; }

static long ms_ora(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static int bloccato(void)
{
    sigset_t m;

    sigprocmask(SIG_BLOCK, NULL, &m);
    return sigismember(&m, SIGUSR1);
}

int main(void)
{
    struct pollfd   p[2];
    struct timespec t;
    struct sigaction sa;
    sigset_t        blocca, lascia;
    int             fd[2], r, e, s;
    long            t0;
    pid_t           f;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = preso;
    sigaction(SIGUSR1, &sa, NULL);
    sigemptyset(&blocca); sigaddset(&blocca, SIGUSR1);
    sigprocmask(SIG_BLOCK, &blocca, NULL);
    sigemptyset(&lascia);

    pipe(fd);
    write(fd[1], "x", 1);
    p[0].fd = fd[0]; p[0].events = POLLIN; p[0].revents = 0;
    t.tv_sec = 1; t.tv_nsec = 0;
    r = ppoll(p, 1, &t, NULL);
    printf("PPOLL pipe r=%d in=%d\n", r, (p[0].revents & POLLIN) != 0);

    t.tv_sec = 0; t.tv_nsec = 250000000L;
    t0 = ms_ora();
    r = ppoll(NULL, 0, &t, NULL);
    printf("PPOLL scade r=%d ms=%ld\n", r, ms_ora() - t0);

    g_presi = 0;
    kill(getpid(), SIGUSR1);                /* blocked: stays pending */
    t.tv_sec = 2; t.tv_nsec = 0;
    t0 = ms_ora();
    r = ppoll(NULL, 0, &t, &lascia); e = errno;
    printf("PPOLL pendente r=%d eintr=%d ms=%ld presi=%d bloccato=%d\n", r, r < 0 && e == EINTR,
           ms_ora() - t0, g_presi, bloccato());

    g_presi = 0;
    f = fork();
    if (f == 0) { usleep(300000); kill(getppid(), SIGUSR1); _exit(0); }
    t.tv_sec = 5; t.tv_nsec = 0;
    t0 = ms_ora();
    r = ppoll(NULL, 0, &t, &lascia); e = errno;
    printf("PPOLL figlio r=%d eintr=%d ms=%ld presi=%d bloccato=%d\n", r, r < 0 && e == EINTR,
           ms_ora() - t0, g_presi, bloccato());
    waitpid(f, NULL, 0);

    g_presi = 0;
    kill(getpid(), SIGUSR1);
    t.tv_sec = 0; t.tv_nsec = 300000000L;
    t0 = ms_ora();
    r = ppoll(NULL, 0, &t, &blocca);
    {
        sigset_t pend;

        sigpending(&pend);
        printf("PPOLL tenuto r=%d ms=%ld presi=%d pendente=%d\n", r, ms_ora() - t0, g_presi,
               sigismember(&pend, SIGUSR1));
    }
    /* let the held one in now, so the next test starts clean */
    t.tv_sec = 0; t.tv_nsec = 0;
    ppoll(NULL, 0, &t, &lascia);

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { printf("PPOLL presa SALTA errno=%d\n", errno); return 0; }
    {
        struct sockaddr_in a;

        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET; a.sin_port = htons(40123); a.sin_addr.s_addr = htonl(0x7F000001);
        bind(s, (struct sockaddr *)&a, sizeof(a));
    }
    g_presi = 0;
    kill(getpid(), SIGUSR1);
    p[0].fd = s; p[0].events = POLLIN; p[0].revents = 0;
    p[1].fd = fd[0]; p[1].events = POLLOUT; p[1].revents = 0;   /* a read end: never writable */
    t.tv_sec = 3; t.tv_nsec = 0;
    t0 = ms_ora();
    r = ppoll(p, 1, &t, &lascia); e = errno;
    printf("PPOLL presa r=%d eintr=%d ms=%ld presi=%d bloccato=%d\n", r, r < 0 && e == EINTR,
           ms_ora() - t0, g_presi, bloccato());
    printf("PPOLL FINE\n");
    return 0;
}
