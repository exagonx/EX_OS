/* =============================================================================
 * tools/prove/fdprova.c - the descriptor table of kernel 0.281
 * EX-OS - Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     fdprova [connessioni]      one line per check, then "FD fine <errors>"
 *
 * Run by tools/prova_fd.sh (kernel 0.281, ip.drv 0.017, libc of 28 October
 * 2026), all on 127.0.0.1 with a client thread:
 *   - getrlimit(RLIMIT_NOFILE) and sysconf(_SC_OPEN_MAX) give the limit of
 *     the system (at least 1024), not a number written in the libc;
 *   - files and sockets share one table: a socket takes the lowest free
 *     number, also the one a file just left, and the other way round;
 *   - number 128 is never given (the old IPC descriptor);
 *   - dup2 to a number far beyond the table (3000) grows it; dup of a socket
 *     is EOPNOTSUPP (as before 0.281);
 *   - setrlimit lowers the limit: the next socket gets EMFILE, raising it
 *     back makes room again; raising beyond the limit of the system is
 *     refused (EPERM, also to root);
 *   - select() on a socket numbered beyond FD_SETSIZE (2148: the numbers
 *     below taken by copies of a file);
 *   - N connections at once (2000 by default: 4000 sockets in the program
 *     plus the listener, until 0.280 a program had 1024 and ip.drv 4096
 *     slots), served with epoll, each one echoing its own number.
 *
 *   FD ok <what>    /   FD NO <what>
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/resource.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORTA 41600

static int g_errori = 0;

static void verifica(int giusto, const char *cosa)
{
    printf("FD %s %s\n", giusto ? "ok" : "NO", cosa);
    fflush(stdout);
    if (!giusto) g_errori++;
}

static unsigned int ms_ora(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned int)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
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

/* --- many connections: each client sends its number and reads it back ------ */
static int g_n = 2000;
static volatile int g_eco_giusti = 0, g_eco_collegati = 0, g_fd_massimo = 0;

static void *molti_clienti(void *arg)
{
    int *fd = (int *)calloc((size_t)g_n, sizeof(int));
    int  i, giusti = 0;

    (void)arg;
    for (i = 0; i < g_n; i++) {
        fd[i] = collega();
        if (fd[i] >= 0) g_eco_collegati++;
        if (fd[i] > g_fd_massimo) g_fd_massimo = fd[i];
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
    struct rlimit       rl, rl0;
    struct epoll_event  ev, fuori[64];
    pthread_t           t;
    int                 asc, ep, n, uno = 1;
    char                b[64], riga[160];

    if (argc > 1) g_n = atoi(argv[1]);
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* the limit comes from the system */
    {
        long sc = sysconf(_SC_OPEN_MAX);

        n = getrlimit(RLIMIT_NOFILE, &rl0);
        snprintf(riga, sizeof(riga), "il limite: getrlimit %lu/%lu, sysconf %ld",
                 (unsigned long)rl0.rlim_cur, (unsigned long)rl0.rlim_max, sc);
        verifica(n == 0 && rl0.rlim_cur >= 1024 && rl0.rlim_max >= rl0.rlim_cur
                 && sc == (long)rl0.rlim_cur, riga);
    }

    /* files and sockets share the numbers */
    {
        int f1 = open("/a/fdprova", O_RDONLY);
        int s1 = socket(AF_INET, SOCK_STREAM, 0);
        int f2, s2;

        snprintf(riga, sizeof(riga), "un file e poi un socket: %d e %d, uno dopo l'altro", f1, s1);
        verifica(f1 >= 3 && s1 == f1 + 1, riga);
        close(f1);
        s2 = socket(AF_INET, SOCK_STREAM, 0);
        snprintf(riga, sizeof(riga), "il socket prende il numero lasciato dal file (%d)", s2);
        verifica(s2 == f1, riga);
        close(s1);
        f2 = open("/a/fdprova", O_RDONLY);
        snprintf(riga, sizeof(riga), "il file prende il numero lasciato dal socket (%d)", f2);
        verifica(f2 == s1, riga);
        close(s2);
        close(f2);
    }

    /* number 128 is skipped */
    {
        int *s = (int *)calloc(200, sizeof(int));
        int  i, visto128 = 0, buchi = 0, prima = -1;

        for (i = 0; i < 200; i++) {
            s[i] = socket(AF_INET, SOCK_STREAM, 0);
            if (s[i] == 128) visto128 = 1;
            if (prima >= 0 && s[i] != prima + 1 && !(prima == 127 && s[i] == 129)) buchi++;
            prima = s[i];
        }
        snprintf(riga, sizeof(riga), "200 socket di fila fino a %d, il 128 saltato", prima);
        verifica(!visto128 && buchi == 0 && prima > 128, riga);
        for (i = 0; i < 200; i++) if (s[i] >= 0) close(s[i]);
        free(s);
    }

    /* dup2 far away grows the table; a socket cannot be duplicated */
    {
        int f = open("/a/fdprova", O_RDONLY);
        int d = dup2(f, 3000);
        int g = fcntl(3000, F_GETFD);
        int s = socket(AF_INET, SOCK_STREAM, 0);

        snprintf(riga, sizeof(riga), "dup2 di un file sul 3000: %d (F_GETFD %d)", d, g);
        verifica(d == 3000 && g >= 0, riga);
        verifica(close(3000) == 0 && fcntl(3000, F_GETFD) < 0 && errno == EBADF,
                 "close del 3000, poi EBADF");
        verifica(dup(s) < 0 && errno == EOPNOTSUPP, "dup di un socket: EOPNOTSUPP");
        close(s);
        close(f);
    }

    /* setrlimit lowers and raises */
    {
        int  quanti = 0, err = 0, s[64], i, s2;

        rl = rl0;
        rl.rlim_cur = 40;
        verifica(setrlimit(RLIMIT_NOFILE, &rl) == 0, "setrlimit a 40");
        for (i = 0; i < 64; i++) {
            s[i] = socket(AF_INET, SOCK_STREAM, 0);
            if (s[i] >= 0) quanti++; else if (!err) err = errno;
        }
        snprintf(riga, sizeof(riga), "col limite a 40: %d socket, poi %s", quanti,
                 err == EMFILE ? "EMFILE" : strerror(err));
        verifica(err == EMFILE && quanti >= 30 && quanti <= 37, riga);
        verifica(setrlimit(RLIMIT_NOFILE, &rl0) == 0, "setrlimit di nuovo come prima");
        s2 = socket(AF_INET, SOCK_STREAM, 0);
        verifica(s2 >= 40, "di nuovo posto oltre il 40");
        if (s2 >= 0) close(s2);
        for (i = 0; i < 64; i++) if (s[i] >= 0) close(s[i]);
        rl = rl0;
        rl.rlim_max = rl0.rlim_max + 1000;
        rl.rlim_cur = rl.rlim_max;
        n = setrlimit(RLIMIT_NOFILE, &rl);
        verifica(n < 0 && errno == EPERM, "oltre il tetto del sistema: EPERM (anche a root)");
        setrlimit(RLIMIT_NOFILE, &rl0);
    }

    /* the listener */
    asc = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(asc, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof(uno));
    memset(&a, 0, sizeof(a));
    a.sin_family      = AF_INET;
    a.sin_port        = htons(PORTA);
    a.sin_addr.s_addr = htonl(0x7F000001);
    verifica(bind(asc, (struct sockaddr *)&a, sizeof(a)) == 0 && listen(asc, 128) == 0,
             "ascoltatore su 127.0.0.1");

    /* select on a socket numbered beyond FD_SETSIZE: the numbers below are
     * taken by copies of a file (the table of the kernel past 2048) */
    {
        int     c, s, alto = FD_SETSIZE + 100, k, copie = 0, f;
        size_t  parole = (size_t)(alto / 32 + 1);
        unsigned int *r = (unsigned int *)calloc(parole, 4);   /* a bigger fd_set */
        struct timeval tv;

        c = collega();
        f = open("/a/fdprova", O_RDONLY);
        while ((k = dup(f)) >= 0 && k < alto - 1) copie++;
        if (k >= 0) copie++;
        s = accept(asc, NULL, NULL);
        snprintf(riga, sizeof(riga), "%d copie di un file, poi il socket accettato e' il %d",
                 copie, s);
        verifica(s == alto, riga);
        send(c, "x", 1, 0);
        r[s / 32] |= 1u << (s % 32);
        tv.tv_sec = 5; tv.tv_usec = 0;
        n = select(s + 1, (fd_set *)r, NULL, NULL, &tv);
        snprintf(riga, sizeof(riga), "select sul socket %d (oltre FD_SETSIZE): %d", s, n);
        verifica(n == 1 && (r[s / 32] & (1u << (s % 32))), riga);
        verifica(recv(s, b, sizeof(b), 0) == 1, "recv dal socket alto");
        close(s);
        close(c);
        for (k = f + 1; k < alto; k++) if (k != c) close(k);
        close(f);
        free(r);
    }

    /* many at once, served with epoll from this thread */
    {
        int           accettate = 0, eco = 0, giri = 0, massimo = 0;
        unsigned int  inizio = ms_ora();

        ep = epoll_create1(0);
        ev.events = EPOLLIN;
        ev.data.fd = asc;
        epoll_ctl(ep, EPOLL_CTL_ADD, asc, &ev);
        pthread_create(&t, NULL, molti_clienti, NULL);
        while (eco < g_n && ms_ora() - inizio < 300000) {
            int i;

            n = epoll_wait(ep, fuori, 64, 1000);
            giri++;
            for (i = 0; i < n; i++) {
                int fd = fuori[i].data.fd;

                if (fd == asc) {
                    int nuovo = accept(asc, NULL, NULL);

                    if (nuovo < 0) continue;
                    accettate++;
                    if (nuovo > massimo) massimo = nuovo;
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
                 "%d echi giusti, descrittori fino a %d, %u ms, %d giri di epoll_wait",
                 g_n, g_eco_collegati, accettate, g_eco_giusti,
                 massimo > g_fd_massimo ? massimo : g_fd_massimo, ms_ora() - inizio, giri);
        verifica(g_eco_collegati == g_n && g_eco_giusti == g_n, riga);
        close(ep);
    }

    close(asc);
    printf("FD fine %d\n", g_errori);
    return g_errori ? 1 : 0;
}
