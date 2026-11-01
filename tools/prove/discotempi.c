/* tools/prove/discotempi.c - what each disk operation costs, on real hardware.
 * Usage: discotempi <empty directory> [big file to read]
 * Prints milliseconds per operation, and MB/s for the sequential ones. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>

#define N 200
#define MB (1024L * 1024L)

static char g_b[1 << 20];
static const char *g_d;

static double ora(void)
{
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec * 1000.0 + t.tv_usec / 1000.0;
}

static void nome(char *o, size_t m, const char *pre, int i)
{
    snprintf(o, m, "%s/%s%d", g_d, pre, i);
}

static void riga(const char *c, double ms, int n)
{
    printf("%-34s %9.1f ms  %8.2f ms/op  (%d)\n", c, ms, ms / n, n);
    fflush(stdout);
}

static void flusso(const char *c, double ms, long byte)
{
    printf("%-34s %9.1f ms  %8.1f MB/s\n", c, ms, (byte / (double)MB) / (ms / 1000.0));
    fflush(stdout);
}

static void leggi(const char *c, const char *p, long tetto, int blocco)
{
    int fd = open(p, O_RDONLY);
    long tot = 0;
    double t = ora();

    if (fd < 0) { printf("%s: non si apre\n", c); return; }
    while (tot < tetto) {
        long r = read(fd, g_b, blocco);
        if (r <= 0) break;
        tot += r;
    }
    close(fd);
    flusso(c, ora() - t, tot);
}

int main(int argc, char **argv)
{
    char p[256], g[256];
    double t;
    int i, fd;
    long k;
    struct stat st;

    g_d = argc > 1 ? argv[1] : ".";
    setvbuf(stdout, NULL, _IONBF, 0);
    memset(g_b, 'x', sizeof g_b);

    t = ora();
    for (i = 0; i < N; i++) { nome(p, sizeof p, "f", i); fd = open(p, O_RDWR | O_CREAT, 0644); close(fd); }
    riga("crea e chiudi file vuoto", ora() - t, N);
    t = ora();
    for (i = 0; i < N; i++) { nome(p, sizeof p, "f", i); stat(p, &st); }
    riga("stat", ora() - t, N);
    t = ora();
    for (i = 0; i < N; i++) { nome(p, sizeof p, "f", i); stat(p, &st); }
    riga("stat, seconda volta", ora() - t, N);
    t = ora();
    for (i = 0; i < N; i++) { nome(p, sizeof p, "f", i); fd = open(p, O_RDONLY); close(fd); }
    riga("apri e chiudi, nessuna modifica", ora() - t, N);
    t = ora();
    for (i = 0; i < N; i++) { nome(p, sizeof p, "f", i); fd = open(p, O_RDWR); write(fd, g_b, 100); close(fd); }
    riga("apri, 100 byte, chiudi", ora() - t, N);
    t = ora();
    for (i = 0; i < N; i++) { nome(p, sizeof p, "f", i); unlink(p); }
    riga("cancella file", ora() - t, N);

    t = ora();
    for (i = 0; i < 50; i++) { nome(p, sizeof p, "d", i); mkdir(p, 0755); }
    riga("mkdir", ora() - t, 50);
    t = ora();
    for (i = 0; i < 50; i++) { nome(p, sizeof p, "d", i); rmdir(p); }
    riga("rmdir", ora() - t, 50);

    nome(g, sizeof g, "grande", 0);
    fd = open(g, O_RDWR | O_CREAT, 0644);
    t = ora();
    for (k = 0; k < 64; k++) write(fd, g_b, sizeof g_b);
    flusso("scrive 64 MB a blocchi da 1 MB", ora() - t, 64 * MB);
    t = ora();
    fsync(fd);
    riga("fsync dopo i 64 MB", ora() - t, 1);
    t = ora();
    for (i = 0; i < 50; i++) { pwrite(fd, g_b, 4096, (i * 7919L % 8000) * 4096); fsync(fd); }
    riga("pwrite 4 KB + fsync, come SQLite", ora() - t, 50);
    t = ora();
    for (i = 0; i < 50; i++) fsync(fd);
    riga("fsync senza scrivere niente", ora() - t, 50);
    t = ora();
    for (i = 0; i < 500; i++) pwrite(fd, g_b, 4096, (i * 7919L % 8000) * 4096);
    riga("pwrite 4 KB sparsi, senza fsync", ora() - t, 500);
    close(fd);

    leggi("legge 64 MB, blocchi da 1 MB", g, 64 * MB, 1 << 20);
    leggi("di nuovo (qui agisce la cache)", g, 64 * MB, 1 << 20);
    leggi("legge 64 MB, blocchi da 4 KB", g, 64 * MB, 4096);
    fd = open(g, O_RDONLY);
    t = ora();
    for (i = 0; i < 500; i++) pread(fd, g_b, 4096, (i * 7919L % 16000) * 4096);
    riga("pread 4 KB sparsi", ora() - t, 500);
    t = ora();
    for (i = 0; i < 500; i++) pread(fd, g_b, 4096, (i * 7919L % 16000) * 4096);
    riga("pread 4 KB sparsi, gli stessi", ora() - t, 500);
    close(fd);
    unlink(g);

    if (argc > 2) {
        leggi("Firefox, 256 MB, primo giro", argv[2], 256 * MB, 1 << 20);
        leggi("Firefox, 256 MB, secondo giro", argv[2], 256 * MB, 1 << 20);
    }
    printf("discotempi: fine\n");
    return 0;
}
