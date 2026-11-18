/* =============================================================================
 * tools/prove/tettiprova.c - spawn and exec without fixed ceilings (voce 35,
 * kernel 0.347)
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Run by tools/prova_tetti.sh as /disk/tettiprova. Up to 0.346 a spawn took
 * at most 512 arguments and 32 KB of strings, and the environment variables
 * after the 32nd were dropped without a word. Here:
 *   a  spawn_ex of itself with 3000 arguments (~70 KB) and 200 variables:
 *      the child finds them all, in order, the last one intact
 *   b  the same through fork + execve
 *   c  a small spawn still works (argv only, no environment)
 *   d  1500 shm_open(SHM_ANON) zones open at once, each with its page
 *      (up to 0.346 the libc kept 1024 zones per process)
 *   e  dup() of a socket (it was EOPNOTSUPP): the copy writes after the
 *      first is closed, the other side sees the end only after the copy
 *      goes; through a fork, the child's close of both does not end it
 * Prints [OK]/[NO] lines and "tettiprova: N NO".
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <libc.h>

#define N_ARG 3000
#define N_ENV 200

static int g_no = 0;

static void esito(int ok, const char *t)
{
    printf("  [%s] %s\n", ok ? "OK" : "NO", t);
    if (!ok) g_no++;
}

/* the child: checks what it got, exit 0 if all is there */
static int figlio(int argc, char **argv, char **envp)
{
    char atteso[64];
    int  i, n = 0, ok = 1;

    for (i = 2; i < argc; i++) {
        snprintf(atteso, sizeof atteso, "argomento-%05d-abcdefghij", i - 2);
        if (strcmp(argv[i], atteso) != 0) { ok = 0; break; }
    }
    for (i = 0; envp && envp[i]; i++) {
        snprintf(atteso, sizeof atteso, "VAR%03d=valore-%03d", n, n);
        if (strncmp(envp[i], "VAR", 3) != 0) continue;
        if (strcmp(envp[i], atteso) != 0) { ok = 0; break; }
        n++;
    }
    printf("  figlio (%s): argc %d, variabili %d%s\n", argv[1], argc, n, ok ? "" : ", DIVERSI");
    fflush(stdout);
    return (ok && argc == N_ARG + 2 && n == N_ENV) ? 0 : 1;
}

static char **fai_argv(const char *chi)
{
    char **a = malloc((N_ARG + 3) * sizeof(char *));
    int    i;

    a[0] = "/disk/tettiprova";
    a[1] = (char *)chi;
    for (i = 0; i < N_ARG; i++) {
        a[i + 2] = malloc(32);
        snprintf(a[i + 2], 32, "argomento-%05d-abcdefghij", i);
    }
    a[N_ARG + 2] = NULL;
    return a;
}

static char **fai_env(void)
{
    char **e = malloc((N_ENV + 2) * sizeof(char *));
    int    i;

    e[0] = "PATH=/bin";
    for (i = 0; i < N_ENV; i++) {
        e[i + 1] = malloc(32);
        snprintf(e[i + 1], 32, "VAR%03d=valore-%03d", i, i);
    }
    e[N_ENV + 1] = NULL;
    return e;
}

int main(int argc, char **argv, char **envp)
{
    char   m[160];
    char **a, **e;
    int    pid, st = -1, r;
    char  *piccolo[] = { "/disk/tettiprova", "-piccolo", NULL };

    if (argc >= 2 && strcmp(argv[1], "-piccolo") == 0) {
        printf("  figlio (-piccolo): argc %d\n", argc);
        return argc == 2 ? 0 : 1;
    }
    if (argc >= 2 && argv[1][0] == '-') return figlio(argc, argv, envp);

    /* a */
    a = fai_argv("-spawn"); e = fai_env();
    pid = spawn_ex("/disk/tettiprova", a, e, NULL, 0);
    if (pid > 0) waitpid(pid, &st, 0);
    snprintf(m, sizeof m, "a spawn_ex con %d argomenti e %d variabili: pid %d, uscita %d",
             N_ARG, N_ENV, pid, pid > 0 && WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    esito(pid > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0, m);

    /* b */
    a[1] = "-exec"; st = -1;
    pid = fork();
    if (pid == 0) {
        execve("/disk/tettiprova", a, e);
        printf("  execve: errno %d\n", errno);
        _exit(9);
    }
    if (pid > 0) waitpid(pid, &st, 0);
    snprintf(m, sizeof m, "b fork + execve con gli stessi: pid %d, uscita %d",
             pid, pid > 0 && WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    esito(pid > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0, m);

    /* c */
    st = -1;
    pid = spawn_ex("/disk/tettiprova", piccolo, NULL, NULL, 0);
    r = pid > 0 ? waitpid(pid, &st, 0) : -1;
    snprintf(m, sizeof m, "c uno spawn piccolo senza ambiente: pid %d, uscita %d",
             pid, r > 0 && WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    esito(pid > 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0, m);

    /* d */
    {
        enum { N_SHM = 1500 };
        static int fd[N_SHM];
        int n = 0, giusti = 0, e = 0;

        for (n = 0; n < N_SHM; n++) {
            fd[n] = shm_open(SHM_ANON, O_RDWR, 0600);
            if (fd[n] < 0) { e = errno; break; }
            if (ftruncate(fd[n], 4096) != 0) { e = errno; n++; break; }
        }
        for (r = 0; r < n; r++) {
            unsigned char *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd[r], 0);

            if (p == MAP_FAILED) continue;
            p[0] = (unsigned char)r;
            if (p[0] == (unsigned char)r) giusti++;
            munmap(p, 4096);
        }
        for (r = 0; r < n; r++) close(fd[r]);
        snprintf(m, sizeof m, "d %d zone shm_open(SHM_ANON) aperte insieme, %d mappate e scritte (errno %d)",
                 n, giusti, e);
        esito(n == N_SHM && giusti == N_SHM, m);
    }

    /* e */
    {
        struct sockaddr_in ad;
        struct timeval     tv = { 3, 0 };
        int asc, cl, sv, d2, d3, uno = 1, figlio, n1, n2, n3;
        char c = 0;

        memset(&ad, 0, sizeof ad);
        ad.sin_family = AF_INET; ad.sin_port = htons(5139); ad.sin_addr.s_addr = htonl(0x7F000001);
        asc = socket(AF_INET, SOCK_STREAM, 0);
        setsockopt(asc, SOL_SOCKET, SO_REUSEADDR, &uno, sizeof uno);
        if (asc < 0 || bind(asc, (struct sockaddr *)&ad, sizeof ad) || listen(asc, 2)) {
            esito(0, "e socket/bind/listen");
        } else {
            cl = socket(AF_INET, SOCK_STREAM, 0);
            connect(cl, (struct sockaddr *)&ad, sizeof ad);
            sv = accept(asc, NULL, NULL);
            setsockopt(sv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
            d2 = dup(cl);
            close(cl);
            n1 = d2 >= 0 ? (int)write(d2, "D", 1) : -1;
            n2 = (int)recv(sv, &c, 1, 0);
            d3 = dup(d2);
            figlio = fork();
            if (figlio == 0) { close(d2); close(d3); _exit(0); }
            if (figlio > 0) waitpid(figlio, NULL, 0);
            write(d3, "E", 1);
            close(d2);
            n3 = (int)recv(sv, &c, 1, 0);          /* 'E': still open */
            close(d3);
            r  = (int)recv(sv, &c, 1, 0);          /* 0: the end, at last */
            snprintf(m, sizeof m, "e dup di un socket: dup %d, scrive %d, letto %d, "
                     "dopo fork e una chiusura %d, alla fine %d", d2, n1, n2, n3, r);
            esito(d2 >= 0 && n1 == 1 && n2 == 1 && n3 == 1 && r == 0, m);
            close(sv); close(asc);
        }
    }

    printf("tettiprova: %d NO\n", g_no);
    return g_no ? 1 : 0;
}
