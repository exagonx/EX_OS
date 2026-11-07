/* =============================================================================
 * tools/prova-gnu/prova-gnu.c - what Mesa asks of the libc (voce 88)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Built with the cross compiler against the sysroot's libc.a and run in
 * EX-OS by prova-gnu.sh. Each check prints [OK] or [NO]; the last line is
 * "prova-gnu: N NO".
 *
 *   1  open_memstream: printf into it, fflush, the bytes and the size
 *   2  open_memstream: ftell, fseek back and overwrite, fseek past the end
 *   3  open_memstream: 100000 bytes, the buffer grows; fclose leaves it
 *   4  getline on a file with a 5000-byte line; getdelim with ','
 *   5  more than 16 streams open, not closed, written: exit flushes them all
 *      (the child writes, the parent reads)
 *   6  pthread_barrier: 4 threads, 50 rounds, one SERIAL per round
 *   7  newlocale "C" yes, "it_IT" ENOENT; uselocale; strtod_l "1.5"
 *   8  stpcpy, stpncpy
 *   9  qsort_r with a context
 *  10  reallocarray: overflow is ENOMEM
 *  11  secure_getenv
 *  12  sched_getaffinity = {0}, CPU_COUNT 1, setaffinity {0} yes {1} EINVAL,
 *      sched_getcpu 0, pthread_*affinity_np
 *  13  pthread_getcpuclockid of self, clock_gettime on it
 *  14  getpriority 0, setpriority 0 yes, 10 EACCES
 *  15  fegetexcept: none traps by default
 *  16  program_invocation_short_name
 *  17  st_rdev: /dev/null is S_IFCHR with st_rdev = st_ino, a file has 0
 *  18  the old symbol `stat` (code compiled before st_rdev) fills only up to
 *      st_ctime: the bytes after it stay as they were
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <locale.h>
#include <pthread.h>
#include <sched.h>
#include <fenv.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <stddef.h>

static int g_no = 0;
static char g_dir[128] = "/tmp";   /* TMPDIR: on the live CD /tmp is read-only */
static void esito(int n, int ok, const char *cosa)
{
    printf("  [%s] %d %s\n", ok ? "OK" : "NO", n, cosa);
    if (!ok) g_no++;
}

/* --- 6 --- */
static pthread_barrier_t g_b;
static volatile int g_seriali = 0, g_giri[4];
static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;
static void *filo(void *a)
{
    int i, io = (int)(long)a;
    for (i = 0; i < 50; i++) {
        g_giri[io] = i;
        if (pthread_barrier_wait(&g_b) == PTHREAD_BARRIER_SERIAL_THREAD) {
            pthread_mutex_lock(&g_m); g_seriali++; pthread_mutex_unlock(&g_m);
        }
        /* after the barrier everyone has reached round i */
        { int k; for (k = 0; k < 4; k++) if (g_giri[k] < i) return (void *)1; }
        pthread_barrier_wait(&g_b);
    }
    return 0;
}

static int per_contesto(const void *a, const void *b, void *ctx)
{
    int x = *(const int *)a, y = *(const int *)b;
    (*(int *)ctx)++;
    return x < y ? 1 : x > y ? -1 : 0;     /* descending */
}

int main(void)
{
    char  *m = NULL;
    size_t n = 0;
    FILE  *f;

    printf("prova-gnu: la libc che chiede Mesa\n");
    if (getenv("TMPDIR")) snprintf(g_dir, sizeof g_dir, "%s", getenv("TMPDIR"));

    /* 1 */
    f = open_memstream(&m, &n);
    if (f) {
        fprintf(f, "ciao %d", 42);
        fflush(f);
        esito(1, m && n == 7 && strcmp(m, "ciao 42") == 0, "open_memstream: printf e fflush");
    } else esito(1, 0, "open_memstream: NULL");

    /* 2 */
    if (f) {
        long p = ftell(f);
        int  ok = p == 7;
        fseek(f, 0, SEEK_SET);
        fputs("CIAO", f);
        fflush(f);
        ok = ok && strcmp(m, "CIAO 42") == 0 && n == 4;   /* size = the position */
        fseek(f, 0, SEEK_END);
        fflush(f);
        ok = ok && n == 7;
        fseek(f, 3, SEEK_END);
        fputc('!', f);
        fflush(f);
        ok = ok && n == 11 && memcmp(m, "CIAO 42\0\0\0!", 12) == 0;
        esito(2, ok, "open_memstream: ftell, fseek, il buco di zeri");
        fclose(f);
        free(m);
    } else esito(2, 0, "open_memstream: non provata");

    /* 3 */
    m = NULL; n = 0;
    f = open_memstream(&m, &n);
    if (f) {
        int i, ok = 1;
        for (i = 0; i < 100000; i++) fputc('a' + i % 26, f);
        fclose(f);
        ok = n == 100000 && m && m[100000] == '\0';
        for (i = 0; ok && i < 100000; i++) if (m[i] != 'a' + i % 26) ok = 0;
        esito(3, ok, "open_memstream: 100000 byte, il buffer resta dopo fclose");
        free(m);
    } else esito(3, 0, "open_memstream: NULL");

    /* 4 */
    {
        char   nf[160];
        FILE  *w;
        snprintf(nf, sizeof nf, "%s/prova-gnu.txt", g_dir);
        w = fopen(nf, "w");
        char  *r = NULL;
        size_t d = 0;
        int    i, ok = w != NULL;
        if (!w) printf("      fopen %s: %s\n", nf, strerror(errno));
        ssize_t l1 = 0, l2 = 0, l3 = 0, l4 = 0;
        if (w) {
            fputs("prima\n", w);
            for (i = 0; i < 5000; i++) fputc('x', w);
            fputs("\nuno,due", w);
            fclose(w);
            w = fopen(nf, "r");
        }
        if (w) {
            l1 = getline(&r, &d, w);
            ok = ok && l1 == 6 && strcmp(r, "prima\n") == 0;
            l2 = getline(&r, &d, w);
            ok = ok && l2 == 5001 && r[4999] == 'x' && r[5000] == '\n' && d > 5001;
            l3 = getdelim(&r, &d, ',', w);
            ok = ok && l3 == 4 && strcmp(r, "uno,") == 0;
            l4 = getdelim(&r, &d, ',', w);
            ok = ok && l4 == 3 && strcmp(r, "due") == 0;
            ok = ok && getline(&r, &d, w) == -1;
            fclose(w);
        } else ok = 0;
        if (!ok) printf("      %ld %ld %ld %ld\n", (long)l1, (long)l2, (long)l3, (long)l4);
        esito(4, ok, "getline e getdelim");
        free(r);
        unlink(nf);
    }

    /* 5 */
    {
        int pid = fork(), st = 0, ok = 1, i;
        if (pid == 0) {
            for (i = 0; i < 40; i++) {
                char nome[64];
                FILE *g;
                snprintf(nome, sizeof nome, "%s/pg-%d", g_dir, i);
                g = fopen(nome, "w");
                if (g) fprintf(g, "flusso %d", i);
            }
            exit(0);                /* no fclose: exit must flush all 40 */
        }
        waitpid(pid, &st, 0);
        for (i = 0; i < 40; i++) {
            char nome[64], atteso[32], letto[32] = "";
            FILE *g;
            snprintf(nome, sizeof nome, "%s/pg-%d", g_dir, i);
            snprintf(atteso, sizeof atteso, "flusso %d", i);
            g = fopen(nome, "r");
            if (g) { if (!fgets(letto, sizeof letto, g)) letto[0] = 0; fclose(g); }
            if (strcmp(letto, atteso) != 0) { if (ok) printf("      %s: '%s'\n", nome, letto); ok = 0; }
            unlink(nome);
        }
        esito(5, pid > 0 && ok, "40 flussi non chiusi, exit li svuota tutti");
    }

    /* 6 */
    {
        pthread_t t[4];
        int i, ok = pthread_barrier_init(&g_b, NULL, 4) == 0;
        void *r;
        for (i = 0; i < 4; i++) pthread_create(&t[i], NULL, filo, (void *)(long)i);
        for (i = 0; i < 4; i++) { pthread_join(t[i], &r); if (r) ok = 0; }
        ok = ok && g_seriali == 50 && pthread_barrier_destroy(&g_b) == 0;
        if (!ok) printf("      seriali %d\n", g_seriali);
        esito(6, ok, "pthread_barrier: 4 fili, 50 giri, un SERIAL per giro");
    }

    /* 7 */
    {
        locale_t c = newlocale(LC_ALL_MASK, "C", (locale_t)0), x, prima;
        char *fine;
        int ok = c != (locale_t)0;
        errno = 0;
        x = newlocale(LC_NUMERIC_MASK, "it_IT.UTF-8", (locale_t)0);
        ok = ok && x == (locale_t)0 && errno == ENOENT;
        prima = uselocale(c);
        ok = ok && prima == LC_GLOBAL_LOCALE && uselocale((locale_t)0) == c;
        ok = ok && strtod_l("1.5x", &fine, c) == 1.5 && *fine == 'x';
        ok = ok && strtof_l("-2.25", NULL, c) == -2.25f;
        uselocale(LC_GLOBAL_LOCALE);
        freelocale(c);
        esito(7, ok, "newlocale, uselocale, strtod_l");
    }

    /* 8 */
    {
        char b[16], *p;
        int ok;
        p = stpcpy(b, "abc");
        p = stpcpy(p, "def");
        ok = p == b + 6 && strcmp(b, "abcdef") == 0;
        memset(b, 'z', sizeof b);
        p = stpncpy(b, "ab", 5);
        ok = ok && p == b + 2 && b[2] == 0 && b[4] == 0 && b[5] == 'z';
        p = stpncpy(b, "abcdefgh", 3);
        ok = ok && p == b + 3 && b[2] == 'c';
        esito(8, ok, "stpcpy, stpncpy");
    }

    /* 9 */
    {
        int v[] = { 5, 1, 9, 3, 7, 2, 8 }, conta = 0, i, ok = 1;
        qsort_r(v, 7, sizeof v[0], per_contesto, &conta);
        for (i = 1; i < 7; i++) if (v[i - 1] < v[i]) ok = 0;
        esito(9, ok && conta > 0, "qsort_r col contesto");
    }

    /* 10 */
    {
        void *p = reallocarray(NULL, 10, 8), *q;
        int ok = p != NULL;
        errno = 0;
        q = reallocarray(p, (size_t)-1 / 4, 8);
        ok = ok && q == NULL && errno == ENOMEM;
        free(p);
        esito(10, ok, "reallocarray, e il traboccare");
    }

    /* 11 */
    setenv("PROVA_GNU", "si", 1);
    {
        char *s = secure_getenv("PROVA_GNU");
        esito(11, s && strcmp(s, "si") == 0 && secure_getenv("NON_CE_PROVA") == NULL, "secure_getenv");
    }

    /* 12 */
    {
        cpu_set_t s;
        int ok;
        CPU_ZERO(&s);
        ok = sched_getaffinity(0, sizeof s, &s) == 0 && CPU_ISSET(0, &s) && CPU_COUNT(&s) == 1 &&
             CPU_COUNT(&s) == (int)sysconf(_SC_NPROCESSORS_ONLN);
        ok = ok && sched_setaffinity(0, sizeof s, &s) == 0;
        CPU_ZERO(&s); CPU_SET(1, &s);
        errno = 0;
        ok = ok && sched_setaffinity(0, sizeof s, &s) == -1 && errno == EINVAL;
        ok = ok && sched_getcpu() == 0;
        ok = ok && pthread_setaffinity_np(pthread_self(), sizeof s, &s) == EINVAL;
        CPU_ZERO(&s);
        ok = ok && pthread_getaffinity_np(pthread_self(), sizeof s, &s) == 0 && CPU_ISSET(0, &s);
        esito(12, ok, "affinita': il processore 0, sched_getcpu");
    }

    /* 13 */
    {
        clockid_t c;
        struct timespec ts;
        int ok = pthread_getcpuclockid(pthread_self(), &c) == 0 && clock_gettime(c, &ts) == 0;
        esito(13, ok, "pthread_getcpuclockid e clock_gettime");
    }

    /* 14 */
    {
        int ok;
        errno = 0;
        ok = getpriority(PRIO_PROCESS, 0) == 0 && errno == 0;
        ok = ok && setpriority(PRIO_PROCESS, 0, 0) == 0;
        errno = 0;
        ok = ok && setpriority(PRIO_PROCESS, 0, 10) == -1 && errno == EACCES;
        esito(14, ok, "getpriority, setpriority");
    }

    /* 15 */
    esito(15, fegetexcept() == 0, "fegetexcept: nessuna eccezione abilitata");

    /* 16 */
    {
        extern char *program_invocation_short_name;
        esito(16, strcmp(program_invocation_short_name, "prova-gnu") == 0 &&
                  strchr(program_invocation_short_name, '/') == NULL, "program_invocation_short_name");
    }

    /* 17 */
    {
        struct stat d, f;
        int ok = stat("/dev/null", &d) == 0 && S_ISCHR(d.st_mode) && d.st_rdev == d.st_ino;
        ok = ok && stat(g_dir, &f) == 0 && f.st_rdev == 0;
        esito(17, ok, "st_rdev: /dev/null dispositivo, il resto 0");
    }

    /* 18 */
    {
        /* what an object compiled before 7/11/2026 calls */
        extern int stat_vecchia(const char *, struct stat *) __asm__("stat");
        unsigned char b[sizeof(struct stat) + 16];
        size_t vecchia = offsetof(struct stat, st_rdev), i;
        int ok;
        memset(b, 0xAA, sizeof b);
        ok = stat_vecchia("/dev/null", (struct stat *)b) == 0 &&
             S_ISCHR(((struct stat *)b)->st_mode);
        for (i = vecchia; i < sizeof b; i++) if (b[i] != 0xAA) ok = 0;
        esito(18, ok, "il vecchio simbolo stat non scrive oltre st_ctime");
    }

    printf("prova-gnu: %d NO\n", g_no);
    return g_no ? 1 : 0;
}
