/* tools/prove/ritardo.c: how late the others get while one program reads a
 * lot (voce 117: the mouse stutters while Firefox loads)
 *
 *     ritardo SECONDS [FILE...]
 *
 * Three measures, every 5 ms for SECONDS seconds, each in its own thread so
 * that one blocked does not hide the others:
 *   sonno   a 5 ms sleep: how much longer than asked it lasted (scheduler)
 *   stat    stat("/") : how long one VFS operation waited (the VFS lock)
 *   tubo    a byte through a pipe to a child and back (IPC and wake-up)
 * With FILE arguments a child reads them all, 64 KB at a time, after one
 * second of quiet, so the first second is the baseline.
 * persi = how many ms the system clock (IRQ0) fell behind the TSC in a sleep:
 * more than zero means ticks lost, interrupts off somewhere.
 * Lines: "RIT <second> sonno <max ms> stat <max ms> tubo <max ms> persi <ms>",
 * last "RIT fine sonno <max> stat <max> tubo <max> oltre20 <count>". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

static unsigned long long tsc(void)
{
    unsigned int lo, hi;

    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

static unsigned long long g_per_us;
static volatile int g_fine;
static int g_secondi;
static unsigned long long g_t0;

#define SEC_MAX 600
static unsigned int g_max[3][SEC_MAX];     /* microseconds, per second */
static unsigned int g_persi[SEC_MAX];      /* ms the system clock fell behind the TSC */
unsigned int uptime_ms(void);
static unsigned int g_oltre20;

static unsigned int ora_us(void) { return (unsigned int)((tsc() - g_t0) / g_per_us); }

static void segna(int quale, unsigned int us)
{
    unsigned int s = ora_us() / 1000000u;

    if (s >= SEC_MAX) return;
    if (us > g_max[quale][s]) g_max[quale][s] = us;
    if (us > 20000u) g_oltre20++;
}

static void *filo_sonno(void *a)
{
    (void)a;
    while (!g_fine) {
        unsigned long long t = tsc();
        unsigned int us, u0 = uptime_ms(), du, s;

        usleep(5000);
        us = (unsigned int)((tsc() - t) / g_per_us);
        du = uptime_ms() - u0;
        segna(0, us > 5000u ? us - 5000u : 0u);
        /* ! TICKS LOST: the clock moves only when IRQ0 comes. If the
         * processor that takes it has interrupts off, the sleep ends late
         * AND the clock says less time went by than the TSC. */
        s = ora_us() / 1000000u;
        if (s < SEC_MAX && us / 1000u > du + 10u && us / 1000u - du > g_persi[s])
            g_persi[s] = us / 1000u - du;
    }
    return 0;
}

static void *filo_stat(void *a)
{
    struct stat st;

    (void)a;
    while (!g_fine) {
        unsigned long long t = tsc();

        stat("/", &st);
        segna(1, (unsigned int)((tsc() - t) / g_per_us));
        usleep(5000);
    }
    return 0;
}

static int g_andata[2], g_ritorno[2];

static void *filo_tubo(void *a)
{
    char c = 'x';

    (void)a;
    while (!g_fine) {
        unsigned long long t = tsc();

        if (write(g_andata[1], &c, 1) != 1 || read(g_ritorno[0], &c, 1) != 1) break;
        segna(2, (unsigned int)((tsc() - t) / g_per_us));
        usleep(5000);
    }
    return 0;
}

static void lettore(int n, char **file)
{
    static char buf[65536];
    unsigned long long tot = 0;
    int i;

    sleep(1);
    for (i = 0; i < n; i++) {
        int fd = open(file[i], O_RDONLY);
        long r;

        if (fd < 0) { printf("RIT non apro %s\n", file[i]); continue; }
        while ((r = read(fd, buf, sizeof(buf))) > 0) tot += (unsigned long long)r;
        close(fd);
    }
    printf("RIT letti %llu MB\n", tot >> 20);
    fflush(stdout);
    _exit(0);
}

int main(int argc, char **argv)
{
    pthread_t f[3];
    unsigned long long a;
    unsigned int s, m[3] = { 0, 0, 0 };
    int eco, lett = -1, k;

    if (argc < 2) { printf("uso: ritardo SECONDI [FILE...]\n"); return 2; }
    g_secondi = atoi(argv[1]);
    if (g_secondi < 1 || g_secondi > SEC_MAX) g_secondi = 10;

    a = tsc(); usleep(200000);
    g_per_us = (tsc() - a) / 200000ULL;
    if (!g_per_us) g_per_us = 1;
    g_t0 = tsc();

    if (pipe(g_andata) || pipe(g_ritorno)) { printf("RIT niente pipe\n"); return 1; }
    eco = fork();
    if (eco == 0) {
        char c;

        close(g_andata[1]); close(g_ritorno[0]);    /* or it never sees the end */
        while (read(g_andata[0], &c, 1) == 1) if (write(g_ritorno[1], &c, 1) != 1) break;
        _exit(0);
    }
    if (argc > 2) {
        lett = fork();
        if (lett == 0) {
            close(g_andata[0]); close(g_andata[1]); close(g_ritorno[0]); close(g_ritorno[1]);
            lettore(argc - 2, argv + 2);
        }
    }
    pthread_create(&f[0], 0, filo_sonno, 0);
    pthread_create(&f[1], 0, filo_stat, 0);
    pthread_create(&f[2], 0, filo_tubo, 0);
    sleep((unsigned int)g_secondi);
    g_fine = 1;
    for (k = 0; k < 3; k++) pthread_join(f[k], 0);
    if (lett > 0) { kill(lett, 9); waitpid(lett, 0, 0); }
    close(g_andata[1]);
    waitpid(eco, 0, 0);

    for (s = 0; s < (unsigned int)g_secondi; s++) {
        printf("RIT %u sonno %u stat %u tubo %u persi %u\n", s,
               g_max[0][s] / 1000u, g_max[1][s] / 1000u, g_max[2][s] / 1000u, g_persi[s]);
        for (k = 0; k < 3; k++) if (g_max[k][s] > m[k]) m[k] = g_max[k][s];
    }
    printf("RIT fine sonno %u stat %u tubo %u oltre20 %u\n",
           m[0] / 1000u, m[1] / 1000u, m[2] / 1000u, g_oltre20);
    return 0;
}
