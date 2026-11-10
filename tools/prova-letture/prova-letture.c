/* =============================================================================
 * tools/prova-letture/prova-letture.c - known files read back while other
 * threads write new ones (voce 5 of @CODA-20-10, @EXILLA-SU-NASI5DEB)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     prova-letture DIR [SECONDI] [LETTORI] [SCRITTORI] [PROCESSI]
 *
 * On 19 October 2026 Firefox once drew, instead of /disk/prova.html, the
 * contents of an SQLite database of its profile it had just written: a read()
 * of a known file gave blocks of a new one. The disk was clean afterwards.
 * The suspicion: a race between threads in the block cache. This program
 * tries to make it happen and to catch it:
 *
 *   - NOTI known files (from 4 KB to 1 MB, odd sizes too) are written first,
 *     every byte a function of (file, position), and synced;
 *   - LETTORI threads read them back for SECONDI seconds - read() from a
 *     random place, pread(), mmap(), whole files - and compare every byte;
 *   - SCRITTORI threads meanwhile create new files, append to them in small
 *     pieces (like SQLite's journal), overwrite pages in place, fsync, read
 *     back what they wrote, and delete them (one in two the round after);
 *   - with PROCESSI > 1 the whole thing runs in that many processes at once
 *     (fork): Firefox's content processes read the same disk.
 *
 * Every wrong byte is counted; the first few are printed with what was
 * expected and what came, and whether what came looks like a writer's
 * pattern (that would be the 19 October fault). Ends with
 * "prova-letture: 0 NO" when nothing was wrong.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>

#define NOTI 12
static const unsigned long g_misura[NOTI] = {
    4096, 4097, 12288, 40000, 65536, 100003, 131072, 262144, 300001,
    524288, 786433, 1048576
};

static const char *g_dir;
static volatile int g_fine;
static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static unsigned long g_sbagliati, g_letture, g_byte_letti, g_scritti, g_file_nuovi;
static unsigned long g_scr_sbagliati;
static int g_stampati;

/* the known files: byte i of file k */
static unsigned char noto(int k, unsigned long i)
{
    return (unsigned char)((i * 131u + (i >> 8) * 7u + (unsigned long)k * 29u + 1u) ^ 0x5A);
}

/* a writer's byte: a different family, recognisable (tag 0xC0 in bits) */
static unsigned char scritto(unsigned int seme, unsigned long i)
{
    return (unsigned char)(((i * 17u + seme * 101u) & 0x3Fu) | 0xC0u);
}

static long ms_ora(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (long)tv.tv_sec * 1000L + (long)(tv.tv_usec / 1000);
}

static unsigned int caso(unsigned int *s)
{
    *s = *s * 1103515245u + 12345u;
    return (*s >> 8) & 0xFFFFFFu;
}

static void nome_noto(char *b, size_t n, int k)
{
    snprintf(b, n, "%s/noto%02d", g_dir, k);
}

static void male(int k, unsigned long pos, unsigned char atteso, unsigned char venuto,
                 const char *come)
{
    pthread_mutex_lock(&g_mx);
    g_sbagliati++;
    if (g_stampati < 12) {
        g_stampati++;
        printf("prova-letture: SBAGLIATO noto%02d byte %lu (%s, blocco %lu): atteso %02x, venuto %02x%s\n",
               k, pos, come, pos / 4096ul, atteso, venuto,
               (venuto & 0xC0u) == 0xC0u ? " - sembra di uno scrittore" : "");
        fflush(stdout);
    }
    pthread_mutex_unlock(&g_mx);
}

static int controlla(int k, unsigned long da, const unsigned char *b, unsigned long n,
                     const char *come)
{
    unsigned long i;
    int brutti = 0;

    for (i = 0; i < n; i++)
        if (b[i] != noto(k, da + i)) {
            if (brutti < 2) male(k, da + i, noto(k, da + i), b[i], come);
            else { pthread_mutex_lock(&g_mx); g_sbagliati++; pthread_mutex_unlock(&g_mx); }
            brutti++;
        }
    return brutti;
}

static int crea_noti(void)
{
    static unsigned char b[65536];
    char nome[256];
    int k;

    for (k = 0; k < NOTI; k++) {
        unsigned long fatti = 0;
        int fd;

        nome_noto(nome, sizeof(nome), k);
        fd = open(nome, O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd < 0) { perror("prova-letture: crea"); return -1; }
        while (fatti < g_misura[k]) {
            unsigned long q = g_misura[k] - fatti, i;

            if (q > sizeof(b)) q = sizeof(b);
            for (i = 0; i < q; i++) b[i] = noto(k, fatti + i);
            if (write(fd, b, q) != (ssize_t)q) { perror("prova-letture: write"); return -1; }
            fatti += q;
        }
        fsync(fd);
        close(fd);
    }
    return 0;
}

static void *lettore(void *arg)
{
    unsigned int s = (unsigned int)(unsigned long)arg * 7919u + (unsigned int)getpid();
    unsigned char *b = malloc(65536);
    char nome[256];

    if (b == NULL) return NULL;
    while (!g_fine) {
        int k = (int)(caso(&s) % NOTI), modo = (int)(caso(&s) % 4), fd;
        unsigned long m = g_misura[k];

        nome_noto(nome, sizeof(nome), k);
        fd = open(nome, O_RDONLY);
        if (fd < 0) { perror("prova-letture: open noto"); g_sbagliati++; break; }
        if (modo == 0) {                    /* the whole file, read() */
            unsigned long fatti = 0;
            ssize_t r;

            while ((r = read(fd, b, (size_t)(caso(&s) % 65536u) + 1u)) > 0) {
                controlla(k, fatti, b, (unsigned long)r, "read intero");
                fatti += (unsigned long)r;
            }
            if (fatti != m) {
                pthread_mutex_lock(&g_mx);
                printf("prova-letture: SBAGLIATO noto%02d: letti %lu byte, sono %lu\n", k, fatti, m);
                g_sbagliati++;
                pthread_mutex_unlock(&g_mx);
            }
            g_byte_letti += fatti;
        } else if (modo == 1) {             /* lseek + read from anywhere */
            unsigned long da = caso(&s) % m, n = caso(&s) % 65536u + 1u;
            ssize_t r;

            if (da + n > m) n = m - da;
            lseek(fd, (off_t)da, SEEK_SET);
            r = read(fd, b, n);
            if (r > 0) controlla(k, da, b, (unsigned long)r, "read a caso");
            g_byte_letti += r > 0 ? (unsigned long)r : 0ul;
        } else if (modo == 2) {             /* pread */
            unsigned long da = caso(&s) % m, n = caso(&s) % 8192u + 1u;
            ssize_t r;

            if (da + n > m) n = m - da;
            r = pread(fd, b, n, (off_t)da);
            if (r > 0) controlla(k, da, b, (unsigned long)r, "pread");
            g_byte_letti += r > 0 ? (unsigned long)r : 0ul;
        } else {                            /* mmap of the whole file */
            void *p = mmap(NULL, m, PROT_READ, MAP_PRIVATE, fd, 0);

            if (p != MAP_FAILED) {
                controlla(k, 0, (const unsigned char *)p, m, "mmap");
                munmap(p, m);
                g_byte_letti += m;
            }
        }
        close(fd);
        g_letture++;
    }
    free(b);
    return NULL;
}

static void *scrittore(void *arg)
{
    unsigned int s = (unsigned int)(unsigned long)arg * 104729u + (unsigned int)getpid() * 3u;
    unsigned char *b = malloc(16384), *r = malloc(16384);
    char nome[256], prima[256] = "";
    unsigned int giro = 0;

    if (b == NULL || r == NULL) return NULL;

    while (!g_fine) {
        unsigned int seme = caso(&s);
        unsigned long misura = caso(&s) % 400000u + 1u, fatti = 0, i;
        int fd;

        snprintf(nome, sizeof(nome), "%s/nuovo-%d-%lu-%u", g_dir, (int)getpid(),
                 (unsigned long)arg, giro++);
        fd = open(nome, O_CREAT | O_TRUNC | O_RDWR, 0644);
        if (fd < 0) { perror("prova-letture: crea nuovo"); break; }
        /* small appends, like a journal */
        while (fatti < misura && !g_fine) {
            unsigned long q = caso(&s) % 3000u + 1u;

            if (q > misura - fatti) q = misura - fatti;
            for (i = 0; i < q; i++) b[i] = scritto(seme, fatti + i);
            if (write(fd, b, q) != (ssize_t)q) break;
            fatti += q;
            if (caso(&s) % 16u == 0) fsync(fd);
        }
        /* a page overwritten in place, with the same pattern */
        if (fatti > 8192) {
            unsigned long da = (caso(&s) % (fatti / 4096ul)) * 4096ul;
            unsigned long q = fatti - da < 4096ul ? fatti - da : 4096ul;

            for (i = 0; i < q; i++) b[i] = scritto(seme, da + i);
            pwrite(fd, b, q, (off_t)da);
        }
        fsync(fd);
        /* and read back */
        {
            unsigned long letti = 0;
            ssize_t n;

            lseek(fd, 0, SEEK_SET);
            while ((n = read(fd, r, 16384)) > 0) {
                for (i = 0; i < (unsigned long)n; i++)
                    if (r[i] != scritto(seme, letti + i)) {
                        pthread_mutex_lock(&g_mx);
                        if (g_scr_sbagliati++ < 4)
                            printf("prova-letture: SBAGLIATO %s byte %lu: atteso %02x, venuto %02x\n",
                                   nome, letti + i, scritto(seme, letti + i), r[i]);
                        pthread_mutex_unlock(&g_mx);
                        break;
                    }
                letti += (unsigned long)n;
            }
        }
        close(fd);
        g_scritti += fatti;
        g_file_nuovi++;
        /* the file of the round before stays until this one is done: the
         * disk has blocks freed and taken again all the time, at most two
         * files per writer */
        if (prima[0]) unlink(prima);
        if (caso(&s) % 2u) { snprintf(prima, sizeof(prima), "%s", nome); }
        else { unlink(nome); prima[0] = 0; }
    }
    if (prima[0]) unlink(prima);
    free(b);
    free(r);
    return NULL;
}

static int giro(int secondi, int lettori, int scrittori)
{
    pthread_t f[64];
    int n = 0, i;
    long t0 = ms_ora();

    for (i = 0; i < lettori && n < 64; i++)
        if (pthread_create(&f[n], NULL, lettore, (void *)(unsigned long)i) == 0) n++;
    for (i = 0; i < scrittori && n < 64; i++)
        if (pthread_create(&f[n], NULL, scrittore, (void *)(unsigned long)i) == 0) n++;
    while (ms_ora() - t0 < (long)secondi * 1000L) sleep(1);
    g_fine = 1;
    for (i = 0; i < n; i++) pthread_join(f[i], NULL);
    printf("prova-letture: pid %d, %d fili, %lu letture (%lu KB), %lu file nuovi (%lu KB), sbagliati %lu + %lu\n",
           (int)getpid(), n, g_letture, g_byte_letti / 1024ul, g_file_nuovi, g_scritti / 1024ul,
           g_sbagliati, g_scr_sbagliati);
    fflush(stdout);
    return (g_sbagliati + g_scr_sbagliati) ? 1 : 0;
}

int main(int argc, char **argv)
{
    int secondi, lettori, scrittori, processi, i, no = 0;

    if (argc < 2) {
        fprintf(stderr, "uso: prova-letture DIR [SECONDI] [LETTORI] [SCRITTORI] [PROCESSI]\n");
        return 2;
    }
    g_dir = argv[1];
    secondi   = argc > 2 ? atoi(argv[2]) : 60;
    lettori   = argc > 3 ? atoi(argv[3]) : 3;
    scrittori = argc > 4 ? atoi(argv[4]) : 2;
    processi  = argc > 5 ? atoi(argv[5]) : 1;
    mkdir(g_dir, 0755);
    if (crea_noti() < 0) return 1;
    printf("prova-letture: %d noti scritti, %d s, %d lettori e %d scrittori per processo, %d processi\n",
           NOTI, secondi, lettori, scrittori, processi);
    fflush(stdout);
    for (i = 1; i < processi; i++) {
        pid_t p = fork();

        if (p == 0) return giro(secondi, lettori, scrittori);
    }
    no = giro(secondi, lettori, scrittori);
    for (i = 1; i < processi; i++) {
        int st = 0;

        if (wait(&st) > 0 && (!WIFEXITED(st) || WEXITSTATUS(st) != 0)) no++;
    }
    printf("prova-letture: %d NO\n", no);
    return no ? 1 : 0;
}
