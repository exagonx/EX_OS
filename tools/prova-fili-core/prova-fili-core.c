/* =============================================================================
 * tools/prova-fili-core/prova-fili-core.c - the threads of one program on
 * several processors at once (kernel 0.321, voce 92 b)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Built with the cross gcc (i386-exos / x86_64-exos, -lpthread) and run in
 * EX-OS by prova-fili-core.sh, with 2 or more processors. Up to kernel 0.320
 * two threads of one program never ran at the same time; from 0.321 they do,
 * and every change to the tables of the program must reach the TLB of the
 * other processors running it (TLB shootdown). The checks:
 *
 *   1. sysconf says at least 2 processors at work;
 *   2. N threads of this program (N = the processors) do about N times the
 *      work of one: at least 0.7 x N (printed, judged by the script);
 *   3. munmap while another thread, pinned on another processor, reads the
 *      page: from the moment munmap returns the other thread faults (its
 *      SIGSEGV handler ends it). If it goes on, its processor still has the
 *      old translation and reads a frame that is not the program's any more;
 *   4. the same with mprotect(PROT_READ) while the other thread writes;
 *   5. munmap of 2 MB (one TLB round for the whole range, kernel batch)
 *      while the other thread reads all of it;
 *   6. fork while another thread writes a page in a loop: the child reads
 *      the page twice, 200 ms apart, and must find the same value. If the
 *      writer's processor kept the writable translation, it writes on in
 *      the frame the child shares (copy-on-write not seen);
 *   7. 4 threads touch the same 1024 new pages together, 20 times: every
 *      page counted 4 times, no thread lost (a fault on a page that another
 *      thread brought in meanwhile goes back at once);
 *   8. 300 threads created and ended (their stacks freed) while one thread
 *      computes on another processor: all of them end with their code.
 *
 * ! THE LOOPS OF THE OTHER THREAD DO NOT CALL THE KERNEL. Nothing but the
 * shootdown can make its processor reload the tables: alone on its
 * processor, a program that computes keeps its TLB for ever.
 *
 * Prints "[OK] ..." / "[NO] ..." and at the end "prova-fili-core: N NO".
 * ============================================================================= */
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int g_no = 0;
static int g_cpu = 1;

static void esito(int bene, const char *fmt, const char *cosa)
{
    printf("%s %s %s\n", bene ? "[OK]" : "[NO]", fmt, cosa ? cosa : "");
    fflush(stdout);
    if (!bene) g_no++;
}

static unsigned int ora_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned int)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

static void dormi_ms(unsigned int ms) { usleep(ms * 1000u); }

/* this thread on processor c (if there are that many) */
static void fissa(int c)
{
    cpu_set_t s;

    if (g_cpu < 2) return;
    CPU_ZERO(&s);
    CPU_SET(c % g_cpu, &s);
    pthread_setaffinity_np(pthread_self(), sizeof(s), &s);
}

static void libera(void)
{
    cpu_set_t s;
    int c;

    CPU_ZERO(&s);
    for (c = 0; c < g_cpu; c++) CPU_SET(c, &s);
    pthread_setaffinity_np(pthread_self(), sizeof(s), &s);
}

/* -----------------------------------------------------------------------------
 * 2. the work of N threads
 * --------------------------------------------------------------------------- */
static volatile unsigned int g_scarico;
static unsigned int g_lavoro_ms;

static void *lavora(void *arg)
{
    unsigned int fine = ora_ms() + g_lavoro_ms, x = 12345u;
    unsigned long giri = 0;

    (void)arg;
    for (;;) {
        unsigned int k;

        for (k = 0; k < 200000u; k++) x = x * 1664525u + 1013904223u + (x >> 7);
        g_scarico = x;
        giri++;
        if ((int)(ora_ms() - fine) >= 0) break;
    }
    return (void *)giri;
}

static unsigned long giro_fili(int n, unsigned int ms)
{
    pthread_t t[64];
    unsigned long somma = 0;
    int k;

    g_lavoro_ms = ms;
    for (k = 0; k < n; k++) pthread_create(&t[k], NULL, lavora, NULL);
    for (k = 0; k < n; k++) {
        void *r = NULL;

        pthread_join(t[k], &r);
        somma += (unsigned long)r;
    }
    return somma;
}

/* -----------------------------------------------------------------------------
 * 3-5. the other thread must fault at once
 * --------------------------------------------------------------------------- */
static volatile unsigned int *g_pag;
static volatile size_t        g_pag_n;      /* pages it walks */
static volatile unsigned long g_giri;
static volatile int           g_pronto, g_colpito, g_basta, g_scrive;

static void segv(int sig)
{
    (void)sig;
    g_colpito = 1;
    pthread_exit(NULL);
}

static void *altro(void *arg)
{
    size_t i = 0;

    (void)arg;
    fissa(1);
    for (;;) {
        volatile unsigned int *p = g_pag + i * 1024u;

        if (g_scrive) p[1] = (unsigned int)g_giri;
        else          g_scarico = p[0];
        if (++i == g_pag_n) i = 0;
        g_giri++;
        g_pronto = 1;
        if (g_basta) return NULL;
    }
}

/* modo 0 munmap, 1 mprotect, with `pagine` pages */
static void deve_fermarsi(int modo, size_t pagine, const char *nome)
{
    size_t     byte = pagine * 4096u;
    void      *z = mmap(NULL, byte, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    pthread_t  t;
    unsigned long a, b;
    size_t     i;
    int        k;
    char       riga[160];

    if (z == MAP_FAILED) { esito(0, nome, "(mmap fallita)"); return; }
    for (i = 0; i < pagine; i++) ((volatile unsigned int *)z)[i * 1024u] = 1;
    g_pag = (volatile unsigned int *)z; g_pag_n = pagine;
    g_giri = 0; g_pronto = 0; g_colpito = 0; g_basta = 0; g_scrive = (modo == 1);

    fissa(0);
    pthread_create(&t, NULL, altro, NULL);
    for (k = 0; !g_pronto && k < 300; k++) dormi_ms(10);
    dormi_ms(100);

    /* running together with this one (awake, not asleep)? */
    a = g_giri;
    for (k = 0; k < 3000000; k++) g_scarico = (unsigned int)k;
    b = g_giri;
    if (b == a) {
        snprintf(riga, sizeof(riga), "(l'altro filo non gira insieme a questo: 0 giri)");
        esito(0, nome, riga);
        g_basta = 1; pthread_join(t, NULL); munmap(z, byte); libera();
        return;
    }

    if (modo == 0) munmap(z, byte);
    else           mprotect(z, byte, PROT_READ);
    dormi_ms(20);
    a = g_giri;
    dormi_ms(300);
    b = g_giri;
    if (!g_colpito) {
        g_basta = 1;
        if (modo == 1) mprotect(z, byte, PROT_READ | PROT_WRITE);
    }
    pthread_join(t, NULL);
    if (modo == 1) munmap(z, byte);
    libera();

    if (g_colpito && a == b) {
        esito(1, nome, "- l'altro filo si ferma sul fault");
    } else {
        snprintf(riga, sizeof(riga), "- l'altro filo ha fatto %lu giri in 300 ms dopo la chiamata%s",
                 b - a, g_colpito ? "" : ", nessun fault: TLB vecchio sull'altro processore");
        esito(0, nome, riga);
    }
}

/* -----------------------------------------------------------------------------
 * 6. fork while another thread writes
 * --------------------------------------------------------------------------- */
static void *scrittore(void *arg)
{
    (void)arg;
    fissa(1);
    for (;;) {
        g_pag[0]++;
        g_pronto = 1;
        if (g_basta) return NULL;
    }
}

static void fork_mentre_scrive(void)
{
    void     *z = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    pthread_t t;
    pid_t     f;
    int       k, stato = -1;
    char      riga[160];

    if (z == MAP_FAILED) { esito(0, "6. fork mentre un filo scrive", "(mmap fallita)"); return; }
    g_pag = (volatile unsigned int *)z; g_pag[0] = 0;
    g_pronto = 0; g_basta = 0;
    fissa(0);
    pthread_create(&t, NULL, scrittore, NULL);
    for (k = 0; !g_pronto && k < 300; k++) dormi_ms(10);
    dormi_ms(50);

    f = fork();
    if (f == 0) {
        unsigned int v1 = g_pag[0], v2;

        dormi_ms(200);
        v2 = g_pag[0];
        _exit(v1 == v2 ? 0 : 1);
    }
    if (f > 0) waitpid(f, &stato, 0);
    g_basta = 1;
    pthread_join(t, NULL);
    munmap(z, 4096);
    libera();
    if (f < 0) { esito(0, "6. fork mentre un filo scrive", "(fork fallita)"); return; }
    snprintf(riga, sizeof(riga), "- il figlio %s",
             (WIFEXITED(stato) && WEXITSTATUS(stato) == 0) ? "vede la pagina ferma com'era alla fork"
                                                          : "vede la pagina cambiare: copia su scrittura mancata");
    esito(WIFEXITED(stato) && WEXITSTATUS(stato) == 0, "6. fork mentre un filo scrive", riga);
}

/* -----------------------------------------------------------------------------
 * 7. the same new pages, touched together
 * --------------------------------------------------------------------------- */
#define TOCCA_PAGINE 1024
#define TOCCA_FILI   4
static volatile unsigned int *g_tocca;
static volatile int           g_via, g_pronti;

/* ! THEY START TOGETHER: created one after the other, the first would have
 * touched every page before the second is born, and no two would ever
 * fault on the same page (measured: the check passed without the kernel's
 * fix). Each waits for all, then all go; half of them walk backwards, so
 * they cross in the middle as well. */
static void *tocca(void *arg)
{
    unsigned int i, indietro = (unsigned int)(long)arg & 1u;

    __sync_fetch_and_add(&g_pronti, 1);
    while (!g_via) ;
    for (i = 0; i < TOCCA_PAGINE; i++) {
        unsigned int p = indietro ? TOCCA_PAGINE - 1u - i : i;

        __sync_fetch_and_add(&g_tocca[p * 1024u], 1u);
    }
    return (void *)1;
}

static void tocca_insieme(void)
{
    int giro, k, sbagli = 0, persi = 0;
    char riga[160];

    for (giro = 0; giro < 20; giro++) {
        pthread_t t[TOCCA_FILI];
        void *z = mmap(NULL, TOCCA_PAGINE * 4096u, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        unsigned int i;

        if (z == MAP_FAILED) { persi++; continue; }
        g_tocca = (volatile unsigned int *)z;
        g_via = 0; g_pronti = 0;
        for (k = 0; k < TOCCA_FILI; k++)
            if (pthread_create(&t[k], NULL, tocca, (void *)(long)k) != 0) { t[k] = 0; g_pronti++; }
        while (g_pronti < TOCCA_FILI) ;
        g_via = 1;
        for (k = 0; k < TOCCA_FILI; k++) {
            void *r = NULL;

            if (t[k] == 0 || pthread_join(t[k], &r) != 0 || r != (void *)1) persi++;
        }
        for (i = 0; i < TOCCA_PAGINE; i++) if (g_tocca[i * 1024u] != TOCCA_FILI) sbagli++;
        munmap(z, TOCCA_PAGINE * 4096u);
    }
    snprintf(riga, sizeof(riga), "- %d pagine col conto sbagliato, %d fili persi", sbagli, persi);
    esito(sbagli == 0 && persi == 0, "7. 4 fili insieme sulle stesse 1024 pagine nuove, 20 volte", riga);
}

/* -----------------------------------------------------------------------------
 * 8. threads born and gone while another computes
 * --------------------------------------------------------------------------- */
static void *breve(void *arg)
{
    volatile char buf[8192];

    buf[0] = 1; buf[8191] = 2;      /* two pages of its stack in use */
    return (void *)((long)arg + buf[0] + buf[8191] - 3);
}

static void *calcola(void *arg)
{
    (void)arg;
    fissa(1);
    while (!g_basta) g_scarico++;
    return NULL;
}

static void nascite(void)
{
    pthread_t c;
    int k, bene = 0;
    char riga[120];

    g_basta = 0;
    pthread_create(&c, NULL, calcola, NULL);
    fissa(0);
    for (k = 0; k < 300; k++) {
        pthread_t t;
        void *r = NULL;

        if (pthread_create(&t, NULL, breve, (void *)(long)k) != 0) continue;
        if (pthread_join(t, &r) == 0 && r == (void *)(long)k) bene++;
    }
    g_basta = 1;
    pthread_join(c, NULL);
    libera();
    snprintf(riga, sizeof(riga), "- %d su 300 finiti col loro codice", bene);
    esito(bene == 300, "8. 300 fili nati e finiti mentre un altro calcola", riga);
}

int main(int argc, char **argv)
{
    struct sigaction sa;
    unsigned long uno, tanti;
    unsigned int ms = 3000;
    char riga[160];
    int n;

    if (argc > 1) ms = (unsigned int)atoi(argv[1]) * 1000u;
    if (ms < 1000u) ms = 1000u;
    setvbuf(stdout, NULL, _IONBF, 0);

    g_cpu = (int)sysconf(_SC_NPROCESSORS_ONLN);
    snprintf(riga, sizeof(riga), "- sysconf dice %d", g_cpu);
    esito(g_cpu >= 2, "1. almeno due processori al lavoro", riga);
    if (g_cpu < 1) g_cpu = 1;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = segv;
    sigaction(SIGSEGV, &sa, NULL);

    n = g_cpu > 64 ? 64 : g_cpu;
    uno   = giro_fili(1, ms);
    tanti = giro_fili(n, ms);
    {
        unsigned long r = uno ? tanti * 100u / uno : 0;

        printf("prova-fili-core: %d fili fanno %lu.%02lu volte il lavoro di uno (%lu e %lu giri)\n",
               n, r / 100u, r % 100u, uno, tanti);
        snprintf(riga, sizeof(riga), "- %lu.%02lu volte uno (attese almeno %d.%02d)",
                 r / 100u, r % 100u, n * 70 / 100, n * 70 % 100);
        esito(r >= (unsigned long)n * 70u, "2. i fili di un programma su tutti i processori", riga);
    }

    deve_fermarsi(0, 1,   "3. munmap mentre l'altro filo legge");
    deve_fermarsi(1, 1,   "4. mprotect(PROT_READ) mentre l'altro filo scrive");
    deve_fermarsi(0, 512, "5. munmap di 2 MB mentre l'altro filo li legge");
    fork_mentre_scrive();
    tocca_insieme();
    nascite();

    printf("prova-fili-core: %d NO\n", g_no);
    return g_no != 0;
}
