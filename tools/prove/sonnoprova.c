/* tools/prove/sonnoprova.c: a sleep lasts at least the time asked
 * (analisi_kernel voce 27)
 *
 * The clock of the system moves in steps of 10 ms, so the time is measured
 * with rdtsc, calibrated on one second of uptime. Before each sleep the
 * program spins for a different part of a tick, so the sleep starts
 * anywhere between two beats: up to 0.335 a 10 ms sleep asked just before
 * the beat came back almost at once.
 * Lines: "SONNO <name> <0|1> ..." (0 = good), last "SONNO fine <errors>". */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

unsigned int uptime_ms(void);

static unsigned long long tsc(void)
{
    unsigned int lo, hi;

    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

static unsigned long long g_per_ms;     /* tsc ticks per millisecond */

static unsigned int us_da(unsigned long long t0)
{
    return (unsigned int)((tsc() - t0) * 1000ULL / g_per_ms);
}

static void gira_us(unsigned int us)
{
    unsigned long long t0 = tsc();

    while (us_da(t0) < us) { }
}

static int errori;

/* n sleeps of `ms` through `come` (0 nanosleep, 1 usleep): the shortest
 * must not be under ms, the longest not over ms + 25 */
static void prova(const char *nome, int come, unsigned int ms, int n)
{
    unsigned int minimo = ~0u, massimo = 0;
    int i;

    for (i = 0; i < n; i++) {
        unsigned long long t0;
        unsigned int fatto;

        gira_us((unsigned int)(i * 997u) % 10000u);   /* anywhere in a tick */
        t0 = tsc();
        if (come == 0) {
            struct timespec t = { (time_t)(ms / 1000u), (long)(ms % 1000u) * 1000000L };

            nanosleep(&t, NULL);
        } else {
            usleep(ms * 1000u);
        }
        fatto = us_da(t0);
        if (fatto < minimo)  minimo  = fatto;
        if (fatto > massimo) massimo = fatto;
    }
    {
        int male = minimo < ms * 1000u || massimo > (ms + 25u) * 1000u;

        printf("SONNO %s %d %d volte %u ms: da %u.%03u a %u.%03u ms\n", nome, male, n, ms,
               minimo / 1000u, minimo % 1000u, massimo / 1000u, massimo % 1000u);
        errori += male;
    }
}

int main(void)
{
    unsigned int u0, u1;
    unsigned long long t0;

    /* calibrate on one second, starting right on a beat */
    u0 = uptime_ms();
    while (uptime_ms() == u0) { }
    u0 = uptime_ms();
    t0 = tsc();
    while ((u1 = uptime_ms()) - u0 < 1000u) { }
    g_per_ms = (tsc() - t0) / (u1 - u0);
    printf("SONNO tsc %llu al ms\n", g_per_ms);
    if (g_per_ms == 0) { puts("SONNO fine 1"); return 1; }

    prova("nanosleep10", 0, 10, 60);
    prova("usleep10", 1, 10, 60);
    prova("nanosleep25", 0, 25, 30);
    prova("nanosleep1", 0, 1, 60);
    prova("sleep1000", 0, 1000, 3);   /* one deadline, not +10 ms a slice */
    printf("SONNO fine %d\n", errori);
    return errori != 0;
}
