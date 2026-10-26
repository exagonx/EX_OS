/* =============================================================================
 * tools/prove/epollprova.c - epoll, poll and socket waits done by the stack
 * EX-OS - Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     epollprova [connessioni]      one line per check, then "EPOLL fine <errors>"
 *
 * Run by tools/prova_epoll.sh (ip.drv 0.016, libc of 26 October 2026), all on
 * 127.0.0.1 with a client thread:
 *   - a listener in an epoll set reports the connection to accept;
 *   - EPOLLIN | EPOLLET | EPOLLONESHOT, as MariaDB's pool of threads uses it:
 *     one report, silence until EPOLL_CTL_MOD, and the MOD reports at once
 *     the bytes that came meanwhile;
 *   - bytes recv left in libc (asked 3 of 10): the MOD reports them;
 *   - the end of the other side: EPOLLIN | EPOLLRDHUP;
 *   - epoll_wait with a timeout and nothing: 0, after about that time;
 *   - poll() on a socket: the timeout, and data that comes after 100 ms
 *     heard at once (until 0.015 poll looked every 10 ms);
 *   - recv with SO_RCVTIMEO: EAGAIN after about the timeout;
 *   - a file in an epoll set: EPERM; an epoll fd closed: EBADF;
 *   - N connections at once (300 by default: more than the 64 sockets of a
 *     program and the 24 slots of the stack before), served with epoll, each
 *     one echoing its own number.
 *
 *   EPOLL ok <what>    /   EPOLL NO <what>
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <poll.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORTA 41500

static int g_errori = 0;

static void verifica(int giusto, const char *cosa)
{
    printf("EPOLL %s %s\n", giusto ? "ok" : "NO", cosa);
    fflush(stdout);
    if (!giusto) g_errori++;
}

static unsigned int ms_ora(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned int)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static void dormi(unsigned int ms)
{
    struct timespec t;

    t.tv_sec  = ms / 1000;
    t.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
}

static int collega(void)
{
    struct sockaddr_in a;
    int                fd = socket(AF_INET, SOCK_STREAM, 0);

    if (fd < 0) return -1;
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_port        = htons(PORTA);
    a.sin_addr.s_addr = htonl(0x7F000001);
    if (connect(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { close(fd); return -1; }
    return fd;
}

/* --- the client of the first checks: steps driven by g_passo --------------- */
static volatile int g_passo = 0, g_fatto = 0;

static void aspetta_passo(int n)
{
    while (g_passo < n) dormi(5);
}

static void *cliente(void *arg)
{
    int fd;

    (void)arg;
    aspetta_passo(1);
    fd = collega();
    g_fatto = 1;
    aspetta_passo(2);
    send(fd, "ciao", 4, 0);                         /* the first report */
    aspetta_passo(3);
    send(fd, "ancora", 6, 0);                       /* while disarmed */
    g_fatto = 3;
    aspetta_passo(4);
    send(fd, "0123456789", 10, 0);                  /* recv takes 3 */
    g_fatto = 4;
    aspetta_passo(5);
    dormi(100);
    send(fd, "x", 1, 0);                            /* poll hears it at once */
    aspetta_passo(6);
    close(fd);                                      /* EPOLLRDHUP */
    g_fatto = 6;
    return NULL;
}

/* --- many connections: each client sends its number and reads it back ------ */
static int g_n = 300;
static volatile int g_eco_giusti = 0, g_eco_collegati = 0;

static void *molti_clienti(void *arg)
{
    int *fd = (int *)calloc((size_t)g_n, sizeof(int));
    int  i, giusti = 0;

    (void)arg;
    for (i = 0; i < g_n; i++) {
        fd[i] = collega();
        if (fd[i] >= 0) g_eco_collegati++;
    }
    for (i = 0; i < g_n; i++) {
        char b[16];
        int  q;

        if (fd[i] < 0) continue;
        q = snprintf(b, sizeof(b), "%05d", i);
        send(fd[i], b, (size_t)q, 0);
    }
    for (i = 0; i < g_n; i++) {
        char b[16], atteso[16];
        int  q, presi = 0;

        if (fd[i] < 0) continue;
        snprintf(atteso, sizeof(atteso), "%05d", i);
        while (presi < 5 && (q = (int)recv(fd[i], b + presi, (size_t)(5 - presi), 0)) > 0)
            presi += q;
        if (presi == 5 && memcmp(b, atteso, 5) == 0) giusti++;
    }
    for (i = 0; i < g_n; i++) if (fd[i] >= 0) close(fd[i]);
    g_eco_giusti = giusti;
    free(fd);
    return NULL;
}

int main(int argc, char **argv)
{
    struct sockaddr_in  a;
    struct epoll_event  ev, fuori[16];
    pthread_t           t;
    int                 asc, ep, c, n, uno = 1;
    unsigned int        t0, dt;
    char                b[64], riga[128];

    if (argc > 1) g_n = atoi(argv[1]);
    setvbuf(stdout, NULL, _IOLBF, 0);

    asc = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(asc, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof(uno));
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_port        = htons(PORTA);
    a.sin_addr.s_addr = htonl(0x7F000001);
    verifica(bind(asc, (struct sockaddr *)&a, sizeof(a)) == 0 && listen(asc, 1024) == 0,
             "ascoltatore su 127.0.0.1");

    ep = epoll_create1(EPOLL_CLOEXEC);
    verifica(ep >= 0, "epoll_create1");
    ev.events = EPOLLIN;
    ev.data.u64 = 0x1122334455667788ULL;
    verifica(epoll_ctl(ep, EPOLL_CTL_ADD, asc, &ev) == 0, "l'ascoltatore nell'insieme");

    /* nothing yet: the timeout */
    t0 = ms_ora();
    n  = epoll_wait(ep, fuori, 16, 200);
    dt = ms_ora() - t0;
    snprintf(riga, sizeof(riga), "epoll_wait senza niente: %d dopo %u ms", n, dt);
    verifica(n == 0 && dt >= 150 && dt < 1000, riga);

    pthread_create(&t, NULL, cliente, NULL);
    g_passo = 1;
    n = epoll_wait(ep, fuori, 16, 5000);
    verifica(n == 1 && (fuori[0].events & EPOLLIN) &&
             fuori[0].data.u64 == 0x1122334455667788ULL,
             "una connessione da accettare, con i suoi dati (u64)");
    c = accept(asc, NULL, NULL);
    verifica(c >= 0, "accept");
    while (!g_fatto) dormi(5);

    /* ONESHOT | ET, as MariaDB's pool of threads */
    ev.events   = EPOLLIN | EPOLLET | EPOLLERR | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.ptr = &c;
    verifica(epoll_ctl(ep, EPOLL_CTL_ADD, c, &ev) == 0, "la connessione nell'insieme (ONESHOT|ET)");
    g_passo = 2;
    t0 = ms_ora();
    n  = epoll_wait(ep, fuori, 16, 5000);
    dt = ms_ora() - t0;
    snprintf(riga, sizeof(riga), "i dati arrivano: un evento in %u ms", dt);
    verifica(n == 1 && fuori[0].data.ptr == &c && (fuori[0].events & EPOLLIN), riga);
    n = (int)recv(c, b, sizeof(b), 0);
    verifica(n == 4 && memcmp(b, "ciao", 4) == 0, "recv: ciao");

    g_passo = 3;
    while (g_fatto < 3) dormi(5);
    dormi(50);
    n = epoll_wait(ep, fuori, 16, 300);
    verifica(n == 0, "ONESHOT: niente fino al riarmo");
    verifica(epoll_ctl(ep, EPOLL_CTL_MOD, c, &ev) == 0, "EPOLL_CTL_MOD");
    n = epoll_wait(ep, fuori, 16, 1000);
    verifica(n == 1 && fuori[0].data.ptr == &c, "il riarmo dice subito dei dati arrivati intanto");
    n = (int)recv(c, b, sizeof(b), 0);
    verifica(n == 6 && memcmp(b, "ancora", 6) == 0, "recv: ancora");

    /* bytes libc holds */
    epoll_ctl(ep, EPOLL_CTL_MOD, c, &ev);
    g_passo = 4;
    n = epoll_wait(ep, fuori, 16, 5000);
    verifica(n == 1, "dieci byte: un evento");
    n = (int)recv(c, b, 3, 0);
    verifica(n == 3 && memcmp(b, "012", 3) == 0, "recv di 3 su 10");
    epoll_ctl(ep, EPOLL_CTL_MOD, c, &ev);
    t0 = ms_ora();
    n  = epoll_wait(ep, fuori, 16, 1000);
    dt = ms_ora() - t0;
    snprintf(riga, sizeof(riga), "i 7 byte tenuti dalla libc: pronto in %u ms", dt);
    verifica(n == 1 && dt < 500, riga);
    n = (int)recv(c, b, sizeof(b), 0);
    verifica(n == 7 && memcmp(b, "3456789", 7) == 0, "recv: gli altri 7");

    /* poll: the timeout, then data after 100 ms heard at once */
    {
        struct pollfd p;

        p.fd = c; p.events = POLLIN; p.revents = 0;
        t0 = ms_ora();
        n  = poll(&p, 1, 250);
        dt = ms_ora() - t0;
        snprintf(riga, sizeof(riga), "poll senza dati: %d dopo %u ms", n, dt);
        verifica(n == 0 && dt >= 200 && dt < 1000, riga);
        g_passo = 5;
        t0 = ms_ora();
        n  = poll(&p, 1, 5000);
        dt = ms_ora() - t0;
        snprintf(riga, sizeof(riga), "poll: il byte mandato dopo 100 ms sentito dopo %u ms", dt);
        verifica(n == 1 && (p.revents & POLLIN) && dt >= 80 && dt < 160, riga);
        recv(c, b, sizeof(b), 0);
    }

    /* SO_RCVTIMEO */
    {
        struct timeval tv;

        tv.tv_sec = 0; tv.tv_usec = 200000;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        t0 = ms_ora();
        n  = (int)recv(c, b, sizeof(b), 0);
        dt = ms_ora() - t0;
        snprintf(riga, sizeof(riga), "recv con SO_RCVTIMEO 200 ms: EAGAIN dopo %u ms", dt);
        verifica(n < 0 && errno == EAGAIN && dt >= 150 && dt < 1000, riga);
        tv.tv_usec = 0;
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    /* the other side closes */
    epoll_ctl(ep, EPOLL_CTL_MOD, c, &ev);
    g_passo = 6;
    n = epoll_wait(ep, fuori, 16, 5000);
    verifica(n == 1 && (fuori[0].events & EPOLLIN) && (fuori[0].events & EPOLLRDHUP),
             "la fine dell'altra parte: EPOLLIN|EPOLLRDHUP");
    verifica(recv(c, b, sizeof(b), 0) == 0, "recv: 0 alla fine");
    pthread_join(t, NULL);
    close(c);
    verifica(epoll_ctl(ep, EPOLL_CTL_DEL, c, &ev) < 0 && errno == EBADF,
             "un socket chiuso non e' piu' nell'insieme (EBADF)");

    /* a file: EPERM */
    {
        int f = open("/a/epollprova", O_RDONLY);

        verifica(f >= 0 && epoll_ctl(ep, EPOLL_CTL_ADD, f, &ev) < 0 && errno == EPERM,
                 "un file nell'insieme: EPERM");
        if (f >= 0) close(f);
    }

    /* many at once, served with epoll from this thread */
    {
        int          *servite = (int *)calloc((size_t)g_n + 8, sizeof(int));
        int           accettate = 0, eco = 0, giri = 0;
        unsigned int  inizio = ms_ora();

        epoll_ctl(ep, EPOLL_CTL_DEL, asc, &ev);
        ev.events = EPOLLIN;
        ev.data.fd = asc;
        epoll_ctl(ep, EPOLL_CTL_ADD, asc, &ev);
        pthread_create(&t, NULL, molti_clienti, NULL);
        while (eco < g_n && ms_ora() - inizio < 120000) {
            int i;

            n = epoll_wait(ep, fuori, 16, 1000);
            giri++;
            for (i = 0; i < n; i++) {
                int fd = fuori[i].data.fd;

                if (fd == asc) {
                    int nuovo = accept(asc, NULL, NULL);

                    if (nuovo < 0) continue;
                    accettate++;
                    ev.events  = EPOLLIN | EPOLLRDHUP;
                    ev.data.fd = nuovo;
                    if (epoll_ctl(ep, EPOLL_CTL_ADD, nuovo, &ev) < 0) close(nuovo);
                } else {
                    int q = (int)recv(fd, b, sizeof(b), 0);

                    if (q > 0) {
                        send(fd, b, (size_t)q, 0);
                        eco++;
                    } else {
                        epoll_ctl(ep, EPOLL_CTL_DEL, fd, &ev);
                        close(fd);
                    }
                }
            }
        }
        pthread_join(t, NULL);
        snprintf(riga, sizeof(riga), "%d connessioni insieme: %d collegate, %d accettate, "
                 "%d echi giusti, %u ms, %d giri di epoll_wait",
                 g_n, g_eco_collegati, accettate, g_eco_giusti, ms_ora() - inizio, giri);
        verifica(g_eco_collegati == g_n && g_eco_giusti == g_n, riga);
        free(servite);
    }

    verifica(close(ep) == 0, "close dell'insieme");
    verifica(epoll_wait(ep, fuori, 16, 0) < 0 && errno == EBADF, "insieme chiuso: EBADF");
    close(asc);
    printf("EPOLL fine %d\n", g_errori);
    return g_errori ? 1 : 0;
}
