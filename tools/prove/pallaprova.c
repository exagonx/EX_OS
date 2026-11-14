/* tools/prove/pallaprova.c [secondi]: how fast an idle processor takes a
 * thread that was woken (analisi_kernel voce 24, fuori_uso/voce24-ipi.patch)
 *
 * The main thread never sleeps: it wakes the other one through a condition
 * variable, then spins until it answers. The woken thread can only run on
 * the other processor, which sits in hlt. Up to 0.334 that processor looked
 * at the queue again only at its next tick (10 ms): about 100 rounds a
 * second. With the message to the idle processor (the patch), thousands.
 * Last line: "PALLA giri N in M ms, G al secondo". */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_c = PTHREAD_COND_INITIALIZER;
static int             g_turno, g_fine;
static volatile int    g_risposta;

static long ora_ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long)t.tv_sec * 1000L + t.tv_nsec / 1000000L;
}

static void *ribatte(void *x)
{
    (void)x;
    for (;;) {
        pthread_mutex_lock(&g_m);
        while (!g_turno && !g_fine) pthread_cond_wait(&g_c, &g_m);
        if (g_fine) { pthread_mutex_unlock(&g_m); break; }
        g_turno = 0;
        pthread_mutex_unlock(&g_m);
        __atomic_store_n(&g_risposta, 1, __ATOMIC_SEQ_CST);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    pthread_t f;
    long      secondi = argc > 1 ? atol(argv[1]) : 3, inizio, fine, giri = 0;
    unsigned  k;

    if (pthread_create(&f, NULL, ribatte, NULL) != 0) { puts("PALLA pthread_create fallita"); return 1; }
    inizio = ora_ms();
    fine   = inizio + secondi * 1000L;
    while (ora_ms() < fine) {
        pthread_mutex_lock(&g_m);
        g_turno = 1;
        pthread_cond_signal(&g_c);
        pthread_mutex_unlock(&g_m);
        /* spin, never sleep: this processor stays busy */
        for (k = 0; !__atomic_load_n(&g_risposta, __ATOMIC_SEQ_CST); k++)
            if ((k & 0xFFFFu) == 0 && ora_ms() >= fine + 2000L) break;
        if (!g_risposta) { puts("PALLA nessuna risposta in 2 s"); break; }
        g_risposta = 0;
        giri++;
    }
    fine = ora_ms() - inizio;
    pthread_mutex_lock(&g_m);
    g_fine = 1;
    pthread_cond_signal(&g_c);
    pthread_mutex_unlock(&g_m);
    pthread_join(f, NULL);
    printf("PALLA giri %ld in %ld ms, %ld al secondo\n", giri, fine,
           fine > 0 ? giri * 1000L / fine : 0L);
    return 0;
}
