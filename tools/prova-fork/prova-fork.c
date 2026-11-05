/* =============================================================================
 * tools/prova-fork/prova-fork.c - fork() on EX-OS (kernel 0.313, voce 33)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Run by tools/prova-fork/prova-fork.sh in QEMU, 32 and 64 bit. Each check
 * prints "[OK] ..." or "[NO] ..."; at the end "prova-fork: <n> NO". The
 * checks are the promises of fork, one by one:
 *
 *   1  0 in the child, its pid in the parent; getpid/getppid agree
 *   2  copy-on-write: the child's writes do not reach the parent (a global,
 *      the heap, the stack) and the parent's later writes do not reach the
 *      child
 *   3  the KERNEL writing a copy-on-write page (read() into a buffer of the
 *      child): the parent's copy stays as it was
 *   4  a pipe made before fork: the parent writes, the child reads; the
 *      exit code comes back through waitpid
 *   5  8 MB of heap, half rewritten by the child: both sides intact
 *   6  mmap(MAP_SHARED | MAP_ANONYMOUS) stays shared: the child's writes
 *      are seen by the parent
 *   7  mprotect in the child: read-only, then writable again (copy-on-write
 *      through mprotect), the parent's page unchanged
 *   8  300 children alive at the same time (more than 256 owners of the
 *      same pages), all collected with their codes
 *   9  waitpid(WNOHANG) is 0 while the child lives, waitpid(-1) collects
 *  10  SIGCHLD reaches a handler
 *  11  fork with a second thread running: the child has one thread,
 *      malloc and pthread_self work, the parent's thread goes on
 *  12  fork + execv: the child becomes another program (this one, with an
 *      argument), its exit code comes back
 *  13  pthread_atfork: prepare, parent and child handlers, in order
 *  14  prefork (ip.drv 0.021): the parent listens on 127.0.0.1, four children
 *      wait in accept() on its listener at the same time and serve eight
 *      connections of the parent between them
 *  15  the children closed their copy of the listener and exited: it is
 *      still the parent's, which accepts on it
 *  16  a connection the parent accepted, inherited by a child: the parent
 *      closes its copy, the child still writes on it; the peer sees the end
 *      only when the child closes too
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int g_no = 0;
static void esito(int ok, const char *cosa)
{
    printf("[%s] %s\n", ok ? "OK" : "NO", cosa);
    if (!ok) g_no++;
}

static volatile int g_globale = 1;
static char         g_buf[8192];

static int aspetta(int pid)
{
    int st = -1;

    if (waitpid(pid, &st, 0) != pid) return -1;
    return WEXITSTATUS(st);
}

/* 1 */
static void prova_base(void)
{
    int padre = getpid();
    int p[2];
    int pid;

    if (pipe(p) != 0) { esito(0, "1 pipe"); return; }
    pid = fork();
    if (pid < 0) { esito(0, "1 fork"); return; }
    if (pid == 0) {
        int dati[2];

        dati[0] = getpid();
        dati[1] = getppid();
        write(p[1], dati, sizeof(dati));
        _exit(dati[1] == padre ? 7 : 8);
    }
    {
        int dati[2] = { 0, 0 };
        int r = (int)read(p[0], dati, sizeof(dati));
        int c = aspetta(pid);

        close(p[0]); close(p[1]);
        esito(r == (int)sizeof(dati) && dati[0] == pid && dati[1] == padre && c == 7,
              "1 fork: 0 nel figlio, il suo pid nel padre, getppid giusto");
    }
}

/* 2 */
static void prova_cow(void)
{
    int    sullo_stack = 11;
    char  *heap = malloc(4096);
    int    p[2], q[2], pid;
    char   c;

    memset(heap, 'a', 4096);
    g_globale = 1;
    if (pipe(p) != 0 || pipe(q) != 0 || !heap) { esito(0, "2 preparazione"); return; }
    pid = fork();
    if (pid == 0) {
        int bene = (g_globale == 1 && sullo_stack == 11 && heap[100] == 'a');

        g_globale   = 2;
        sullo_stack = 22;
        memset(heap, 'b', 4096);
        write(p[1], "x", 1);            /* scritto: il padre controlli */
        read(q[0], &c, 1);              /* il padre ha scritto i suoi */
        bene = bene && g_globale == 2 && sullo_stack == 22 && heap[4095] == 'b';
        _exit(bene ? 0 : 1);
    }
    read(p[0], &c, 1);
    {
        int bene = (g_globale == 1 && sullo_stack == 11 && heap[0] == 'a' && heap[4095] == 'a');

        g_globale = 3;
        memset(heap, 'c', 4096);
        write(q[1], "y", 1);
        esito(bene && aspetta(pid) == 0 && g_globale == 3,
              "2 copia su scrittura: globale, heap e stack separati nei due versi");
    }
    close(p[0]); close(p[1]); close(q[0]); close(q[1]);
    free(heap);
}

/* 3 */
static void prova_scrive_il_kernel(void)
{
    int p[2], pid;

    memset(g_buf, 'P', sizeof(g_buf));
    if (pipe(p) != 0) { esito(0, "3 pipe"); return; }
    pid = fork();
    if (pid == 0) {
        /* read() writes g_buf from ring 0: a copy-on-write page */
        int n = (int)read(p[0], g_buf + 4000, 200);

        _exit(n == 200 && g_buf[4000] == 'K' && g_buf[4199] == 'K' && g_buf[0] == 'P' ? 0 : 1);
    }
    {
        char k[200];

        memset(k, 'K', sizeof(k));
        write(p[1], k, sizeof(k));
    }
    esito(aspetta(pid) == 0 && g_buf[4000] == 'P' && g_buf[4199] == 'P',
          "3 il kernel scrive una pagina in copia (read nel figlio): il padre intatto");
    close(p[0]); close(p[1]);
}

/* 4 */
static void prova_pipe(void)
{
    int p[2], pid;

    if (pipe(p) != 0) { esito(0, "4 pipe"); return; }
    pid = fork();
    if (pid == 0) {
        char b[16] = { 0 };

        close(p[1]);
        read(p[0], b, 5);
        _exit(strcmp(b, "ciao!") == 0 ? 42 : 1);
    }
    close(p[0]);
    write(p[1], "ciao!", 5);
    close(p[1]);
    esito(aspetta(pid) == 42, "4 una pipe attraverso fork, il codice d'uscita torna");
}

/* 5 */
static void prova_grande(void)
{
    size_t n = 8u << 20, i;
    unsigned char *m = malloc(n);
    int pid;

    if (!m) { esito(0, "5 malloc 8 MB"); return; }
    for (i = 0; i < n; i++) m[i] = (unsigned char)(i * 7);
    pid = fork();
    if (pid == 0) {
        int bene = 1;

        for (i = 0; i < n; i += 4096) if (m[i] != (unsigned char)(i * 7)) bene = 0;
        for (i = 0; i < n / 2; i++) m[i] = (unsigned char)~(i * 7);
        for (i = 0; i < n; i += 997)
            if (m[i] != (unsigned char)(i < n / 2 ? ~(i * 7) : (i * 7))) bene = 0;
        _exit(bene ? 0 : 1);
    }
    {
        int c = aspetta(pid), bene = 1;

        for (i = 0; i < n; i += 13) if (m[i] != (unsigned char)(i * 7)) { bene = 0; break; }
        esito(c == 0 && bene, "5 8 MB di heap, meta' riscritta dal figlio: i due lati intatti");
    }
    free(m);
}

/* 6 */
static void prova_condivisa(void)
{
    volatile int *c = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    int pid;

    if (c == MAP_FAILED) { esito(0, "6 mmap MAP_SHARED"); return; }
    c[0] = 5;
    pid = fork();
    if (pid == 0) {
        c[0] = c[0] + 100;
        c[1] = 77;
        _exit(0);
    }
    aspetta(pid);
    esito(c[0] == 105 && c[1] == 77, "6 mmap(MAP_SHARED|MAP_ANONYMOUS) resta condivisa");
    munmap((void *)c, 4096);
}

/* 7 */
static void prova_mprotect(void)
{
    char *m = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int   pid;

    if (m == MAP_FAILED) { esito(0, "7 mmap"); return; }
    memset(m, 'm', 8192);
    pid = fork();
    if (pid == 0) {
        if (mprotect(m, 8192, PROT_READ) != 0) _exit(1);
        if (mprotect(m, 8192, PROT_READ | PROT_WRITE) != 0) _exit(2);
        m[10] = 'z';
        m[5000] = 'z';
        _exit(m[10] == 'z' ? 0 : 3);
    }
    {
        int c = aspetta(pid);
        char r[64];

        snprintf(r, sizeof r, "7 mprotect nel figlio (codice %d), la pagina del padre intatta", c);
        esito(c == 0 && m[10] == 'm' && m[5000] == 'm', r);
    }
    munmap(m, 8192);
}

/* 8 */
#define TANTI 300
static void prova_tanti(void)
{
    static int pid[TANTI];
    int p[2], i, nati = 0, bene = 1;
    char c;

    if (pipe(p) != 0) { esito(0, "8 pipe"); return; }
    for (i = 0; i < TANTI; i++) {
        pid[i] = fork();
        if (pid[i] == 0) {
            close(p[1]);
            read(p[0], &c, 1);          /* fino a che il padre chiude: tutti vivi insieme */
            _exit(i & 0x7F);
        }
        if (pid[i] < 0) break;
        nati++;
    }
    close(p[0]);
    close(p[1]);                        /* tutti leggono la fine */
    for (i = 0; i < nati; i++)
        if (aspetta(pid[i]) != (i & 0x7F)) bene = 0;
    {
        char r[96];

        snprintf(r, sizeof r, "8 %d figli vivi insieme (oltre 256 proprietari), tutti raccolti", nati);
        esito(bene && nati == TANTI, r);
    }
}

/* 9 */
static void prova_wnohang(void)
{
    int p[2], pid, st = 0, r0, r1;
    char c = 0;

    if (pipe(p) != 0) { esito(0, "9 pipe"); return; }
    pid = fork();
    if (pid == 0) { read(p[0], &c, 1); _exit(9); }
    r0 = waitpid(pid, &st, WNOHANG);
    write(p[1], "v", 1);
    r1 = waitpid(-1, &st, 0);
    esito(r0 == 0 && r1 == pid && WEXITSTATUS(st) == 9, "9 waitpid WNOHANG 0 col figlio vivo, -1 lo raccoglie");
    close(p[0]); close(p[1]);
}

/* 10 */
static volatile int g_sigchld = 0;
static void su_sigchld(int s) { (void)s; g_sigchld++; }
static void prova_sigchld(void)
{
    int pid, i;

    signal(SIGCHLD, su_sigchld);
    pid = fork();
    if (pid == 0) _exit(0);
    for (i = 0; i < 200 && g_sigchld == 0; i++) usleep(10000);
    aspetta(pid);
    esito(g_sigchld >= 1, "10 SIGCHLD arriva al gestore");
    signal(SIGCHLD, SIG_DFL);
}

/* 11 */
static volatile int g_gira = 1, g_giri = 0;
static void *filo(void *a) { (void)a; while (g_gira) { g_giri++; usleep(1000); } return NULL; }
static void prova_fili(void)
{
    pthread_t t;
    int pid, prima;

    if (pthread_create(&t, NULL, filo, NULL) != 0) { esito(0, "11 pthread_create"); return; }
    usleep(20000);
    pid = fork();
    if (pid == 0) {
        void *x = malloc(1000);
        int   bene = x != NULL && pthread_self() != 0;

        prima = g_giri;
        usleep(30000);                  /* nel figlio il filo non c'e': g_giri fermo */
        bene = bene && g_giri == prima;
        free(x);
        _exit(bene ? 0 : 1);
    }
    prima = g_giri;
    {
        int c = aspetta(pid);

        usleep(20000);
        g_gira = 0;
        pthread_join(t, NULL);
        esito(c == 0 && g_giri > prima, "11 fork con un filo che gira: il figlio ne ha uno, il padre va avanti");
    }
}

/* 12 */
static void prova_exec(const char *io)
{
    int pid = fork();

    if (pid == 0) {
        char *av[3];

        av[0] = (char *)io;
        av[1] = (char *)"--figlio-exec";
        av[2] = NULL;
        execv(io, av);
        _exit(99);
    }
    esito(aspetta(pid) == 33, "12 fork + execv: il figlio diventa un altro programma");
}

/* 13 */
static char g_ordine[16];
static int  g_ord_n = 0;
static void af_p1(void) { g_ordine[g_ord_n++] = 'P'; }
static void af_p2(void) { g_ordine[g_ord_n++] = 'p'; }
static void af_g1(void) { g_ordine[g_ord_n++] = 'G'; }
static void af_g2(void) { g_ordine[g_ord_n++] = 'g'; }
static void af_f1(void) { g_ordine[g_ord_n++] = 'F'; }
static void af_f2(void) { g_ordine[g_ord_n++] = 'f'; }
static void prova_atfork(void)
{
    int p[2], pid;

    if (pipe(p) != 0) { esito(0, "13 pipe"); return; }
    pthread_atfork(af_p1, af_g1, af_f1);
    pthread_atfork(af_p2, af_g2, af_f2);
    g_ord_n = 0;
    pid = fork();
    if (pid == 0) {
        g_ordine[g_ord_n] = 0;
        write(p[1], g_ordine, 5);
        _exit(0);
    }
    {
        char f[8] = { 0 };

        read(p[0], f, 5);
        aspetta(pid);
        g_ordine[g_ord_n] = 0;
        /* prepare newest first (p P), then oldest first (G g / F f) */
        esito(strcmp(g_ordine, "pPGg") == 0 && strcmp(f, "pPFf") == 0,
              "13 pthread_atfork: prima, genitore e figlio nell'ordine di POSIX");
    }
    close(p[0]); close(p[1]);
}

/* 14, 15, 16 */
#define PORTA   5050
#define SERVI   4
#define A_TESTA 2

static int collega(void)
{
    struct sockaddr_in a;
    int s = socket(AF_INET, SOCK_STREAM, 0);

    if (s < 0) return -1;
    memset(&a, 0, sizeof a);
    a.sin_family      = AF_INET;
    a.sin_port        = htons(PORTA);
    a.sin_addr.s_addr = htonl(0x7F000001);
    if (connect(s, (struct sockaddr *)&a, sizeof a) != 0) { close(s); return -1; }
    return s;
}

static int leggi_tutto(int s, char *b, int max)
{
    int n = 0, r;

    while (n < max && (r = (int)read(s, b + n, (size_t)(max - n))) > 0) n += r;
    return n;
}

static void prova_prese(void)
{
    struct sockaddr_in a;
    int  asc, i, pid[SERVI], serviti = 0, chi[SERVI] = { 0 }, diversi = 0, uscite = 1;
    char r[96];

    asc = socket(AF_INET, SOCK_STREAM, 0);
    if (asc < 0) { esito(0, "14 socket (ip.drv non c'e'?)"); return; }
    memset(&a, 0, sizeof a);
    a.sin_family      = AF_INET;
    a.sin_port        = htons(PORTA);
    a.sin_addr.s_addr = htonl(0x7F000001);
    if (bind(asc, (struct sockaddr *)&a, sizeof a) != 0 || listen(asc, 16) != 0) {
        esito(0, "14 bind/listen");
        close(asc);
        return;
    }
    for (i = 0; i < SERVI; i++) {
        pid[i] = fork();
        if (pid[i] == 0) {
            int k;

            for (k = 0; k < A_TESTA; k++) {
                char b[4];
                int  c = accept(asc, NULL, NULL);

                if (c < 0) _exit(10 + k);
                if (leggi_tutto(c, b, 4) != 4 || memcmp(b, "ciao", 4) != 0) _exit(20);
                b[0] = (char)('0' + i);
                write(c, b, 1);
                close(c);
            }
            close(asc);
            _exit(0);
        }
    }
    usleep(100000);                     /* all four in accept() */
    for (i = 0; i < SERVI * A_TESTA; i++) {
        int  c = collega();
        char b = 0;

        if (c < 0) continue;
        write(c, "ciao", 4);
        if (read(c, &b, 1) == 1 && b >= '0' && b < '0' + SERVI) { serviti++; chi[b - '0']++; }
        close(c);
    }
    for (i = 0; i < SERVI; i++) {
        int c = aspetta(pid[i]);

        if (c != 0) uscite = 0;
        if (chi[i]) diversi++;
    }
    snprintf(r, sizeof r, "14 prefork: %d connessioni servite da %d figli in accept insieme",
             serviti, diversi);
    esito(serviti == SERVI * A_TESTA && diversi == SERVI && uscite, r);

    /* 15 */
    {
        int  c = collega(), d = -1;
        char b = 0;

        if (c >= 0) d = accept(asc, NULL, NULL);
        if (d >= 0) { write(c, "x", 1); read(d, &b, 1); }
        esito(b == 'x', "15 i figli hanno chiuso l'ascoltatore: resta del padre, che accetta");

        /* 16 */
        if (d >= 0) {
            int  p = fork();
            char t[16] = { 0 };
            int  n;

            if (p == 0) {
                usleep(50000);          /* the parent has closed its copy by now */
                write(d, "figlio", 6);
                close(d);
                _exit(0);
            }
            close(d);
            n = leggi_tutto(c, t, (int)sizeof(t) - 1);
            aspetta(p);
            esito(n == 6 && memcmp(t, "figlio", 6) == 0,
                  "16 il padre chiude la sua copia, il figlio scrive ancora; poi la fine");
        } else {
            esito(0, "16 nessuna connessione accettata");
        }
        if (c >= 0) close(c);
    }
    close(asc);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--figlio-exec") == 0) return 33;

    setvbuf(stdout, NULL, _IONBF, 0);
    prova_base();
    prova_cow();
    prova_scrive_il_kernel();
    prova_pipe();
    prova_grande();
    prova_condivisa();
    prova_mprotect();
    prova_tanti();
    prova_wnohang();
    prova_sigchld();
    prova_fili();
    prova_exec(argv[0]);
    prova_atfork();
    prova_prese();
    printf("prova-fork: %d NO\n", g_no);
    return g_no ? 1 : 0;
}
