/* =============================================================================
 * tools/prove/condiviprova.c — many threads on one file, and on one FILE
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     condiviprova DIR         works in DIR (a writable volume)
 *
 * Kernel 0.277 (pread/pwrite) and the stdio locks, each on its own line:
 *   FILI pread <bad>          8 threads, random pread on one shared fd: every
 *                             4-byte word must hold its own offset (on the
 *                             CQ60 the fonts Firefox mapped came back wrong);
 *   FILI posizione <bad> ...  4 threads reading the same fd with read(): the
 *                             pieces must add up to the file, none twice;
 *   FILI pwrite <bad>         4 threads pwrite disjoint words, then a check;
 *   FILI stdio <bad> ...      4 threads fprintf on one FILE: every line whole;
 *   FILI mutex <bad>          8 threads increment under pthread_mutex.
 * <bad> = 0 is good. Last line: "FILI fine".
 * ============================================================================= */
#include <libc.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

#define PAROLE     (256 * 1024)            /* 1 MB of 4-byte words */
#define PEZZO      512
#define SECONDI    3                       /* how long the pread threads run */
#define N_PREAD    8
#define N_LETTORI  4
#define N_RIGHE    3000

static int  g_fd;
static volatile int g_male;
static volatile int g_giri;
static volatile int g_letti[N_LETTORI];
static unsigned char g_visto[PAROLE * 4 / 100 + 1];   /* pieces seen */
static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;
static long g_conta;
static FILE *g_f;

static unsigned int caso(unsigned int *s)
{
    *s = *s * 1103515245u + 12345u;
    return *s >> 8;
}

static void *filo_pread(void *a)
{
    unsigned int s = (unsigned int)(uintptr_t)a * 7919u + 1;
    static __thread unsigned int buf[PEZZO / 4];
    int g, i;

    /* by time, not by count: the threads must cross many scheduler ticks,
     * or each one ends inside its own quantum and nothing ever races */
    time_t fine = time(0) + SECONDI;

    for (g = 0; time(0) < fine; g++) {
        unsigned int w = caso(&s) % (PAROLE - PEZZO / 4);
        if (pread(g_fd, buf, PEZZO, (off_t)w * 4) != PEZZO) { g_male++; continue; }
        for (i = 0; i < PEZZO / 4; i++)
            if (buf[i] != w + i) { g_male++; break; }
    }
    __atomic_add_fetch(&g_giri, g, __ATOMIC_RELAXED);
    return 0;
}

/* read() on the shared offset in 100-byte pieces. With the offset held
 * while it moves, every piece starts at a multiple of 100 that no other
 * piece got, and holds the words of that place. */
static void *filo_read(void *a)
{
    int k = (int)(uintptr_t)a, i;
    unsigned int buf[25];
    long n;

    for (;;) {
        n = read(g_fd, buf, sizeof(buf));
        if (n <= 0) break;
        g_letti[k] += (int)n;
        if (buf[0] % 25 != 0) { g_male++; continue; }
        for (i = 1; i < n / 4; i++)
            if (buf[i] != buf[0] + (unsigned int)i) { g_male++; break; }
        if (g_visto[buf[0] / 25]++) g_male++;
    }
    return 0;
}

static void *filo_pwrite(void *a)
{
    int k = (int)(uintptr_t)a, i;

    for (i = k; i < 4096; i += 4) {
        unsigned int v = 0xA0000000u | (unsigned int)i;
        if (pwrite(g_fd, &v, 4, (off_t)i * 4) != 4) g_male++;
    }
    return 0;
}

static void *filo_stdio(void *a)
{
    int k = (int)(uintptr_t)a, i;

    for (i = 0; i < N_RIGHE; i++)
        fprintf(g_f, "riga %d %04d ...................................................."
                     "......................................................... %d\n", k, i, k);
    return 0;
}

static void *filo_mutex(void *a)
{
    int i;

    (void)a;
    for (i = 0; i < 20000; i++) {
        pthread_mutex_lock(&g_m);
        g_conta++;
        pthread_mutex_unlock(&g_m);
    }
    return 0;
}

static void tutti(int n, void *(*fn)(void *))
{
    pthread_t t[8];
    int i;

    for (i = 0; i < n; i++) pthread_create(&t[i], 0, fn, (void *)(uintptr_t)i);
    for (i = 0; i < n; i++) pthread_join(t[i], 0);
}

int main(int argc, char **argv)
{
    char nome[128], riga[256];
    unsigned int *dati;
    int i, tot, righe, rotte;
    size_t lung;

    if (argc < 2) { printf("uso: condiviprova DIR\n"); return 2; }
    snprintf(nome, sizeof(nome), "%s/fili.dat", argv[1]);
    dati = malloc(PAROLE * 4);
    for (i = 0; i < PAROLE; i++) dati[i] = (unsigned int)i;
    g_fd = open(nome, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (g_fd < 0 || write(g_fd, dati, PAROLE * 4) != PAROLE * 4) {
        printf("FILI non scrivo %s\n", nome); return 1;
    }

    g_male = 0;
    tutti(N_PREAD, filo_pread);
    printf("FILI pread %d giri %d\n", g_male, g_giri);

    g_male = 0;
    lseek(g_fd, 0, SEEK_SET);
    tutti(N_LETTORI, filo_read);
    for (tot = 0, i = 0; i < N_LETTORI; i++) tot += g_letti[i];
    printf("FILI posizione %d letti %d su %d, pezzi storti %d\n",
           tot != PAROLE * 4 || g_male, tot, PAROLE * 4, g_male);

    g_male = 0;
    tutti(4, filo_pwrite);
    for (i = 0; i < 4096; i++) {
        unsigned int v;
        if (pread(g_fd, &v, 4, (off_t)i * 4) != 4 || v != (0xA0000000u | (unsigned int)i)) g_male++;
    }
    printf("FILI pwrite %d\n", g_male);
    close(g_fd);

    snprintf(nome, sizeof(nome), "%s/fili.txt", argv[1]);
    g_f = fopen(nome, "w");
    tutti(4, filo_stdio);
    fclose(g_f);
    g_f = fopen(nome, "r");
    righe = rotte = 0;
    lung = 0;
    while (g_f && fgets(riga, sizeof(riga), g_f)) {
        int k, n;
        size_t l = strlen(riga);
        righe++;
        if (!lung) lung = l;
        /* "riga K NNNN ....... K\n": same length, same K at both ends */
        if (sscanf(riga, "riga %d %d", &k, &n) != 2 || l != lung || l < 3 ||
            riga[l - 1] != '\n' || riga[l - 2] != (char)('0' + k)) rotte++;
    }
    if (g_f) fclose(g_f);
    printf("FILI stdio %d righe %d rotte %d\n", righe != 4 * N_RIGHE || rotte, righe, rotte);

    tutti(8, filo_mutex);
    printf("FILI mutex %d\n", g_conta != 8 * 20000);
    printf("FILI fine\n");
    return 0;
}
