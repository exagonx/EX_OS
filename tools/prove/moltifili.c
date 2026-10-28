/* =============================================================================
 * tools/prove/moltifili.c - many threads, no fixed ceiling (kernel 0.282)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     moltifili [N]
 *
 * Starts N threads (default 300) that all stay alive together: past the 127
 * of the old FILO_MAX, past the 128 descriptors of the libc's first block
 * and the 64 places of its first errno block. Every thread touches 64 KB of
 * its stack (the slots past the reserved band), sets errno to its own number
 * and checks it after the others did the same, and keeps a pthread key.
 * Then it starts threads until the system says no, which must be EAGAIN
 * and not a fault, and joins them all. Prints lines "MOLTI <name> 0 ..."
 * when a step is right.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>

static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  c = PTHREAD_COND_INITIALIZER;
static int             pronti, via;
static int             errori_errno, errori_chiave, errori_pila;
static pthread_key_t   chiave;

static void *filo(void *arg)
{
    long          n = (long)arg;
    volatile char pila[65536];
    int           i;

    for (i = 0; i < (int)sizeof(pila); i += 512) pila[i] = (char)n;
    errno = (int)(n % 1000) + 1;
    pthread_setspecific(chiave, arg);

    pthread_mutex_lock(&m);
    pronti++;
    pthread_cond_broadcast(&c);
    while (!via) pthread_cond_wait(&c, &m);
    pthread_mutex_unlock(&m);

    for (i = 0; i < (int)sizeof(pila); i += 512)
        if (pila[i] != (char)n) { pthread_mutex_lock(&m); errori_pila++; pthread_mutex_unlock(&m); break; }
    if (errno != (int)(n % 1000) + 1) { pthread_mutex_lock(&m); errori_errno++; pthread_mutex_unlock(&m); }
    if (pthread_getspecific(chiave) != arg) { pthread_mutex_lock(&m); errori_chiave++; pthread_mutex_unlock(&m); }
    return arg;
}

static pthread_t t[4096];

int main(int argc, char **argv)
{
    int  n = argc > 1 ? atoi(argv[1]) : 300;
    int  fatti = 0, r = 0, i, giusti = 0, oltre;

    if (n > 4096) n = 4096;
    setvbuf(stdout, NULL, _IONBF, 0);
    pthread_key_create(&chiave, NULL);

    for (i = 0; i < n; i++) {
        r = pthread_create(&t[i], NULL, filo, (void *)(long)(i + 1));
        if (r != 0) break;
        fatti++;
    }
    printf("MOLTI crea %d %d fili su %d (%s)\n", fatti == n ? 0 : 1, fatti, n,
           r ? strerror(r) : "tutti");

    pthread_mutex_lock(&m);
    while (pronti < fatti) pthread_cond_wait(&c, &m);
    pthread_mutex_unlock(&m);
    printf("MOLTI vivi 0 %d fili vivi insieme\n", pronti);

    /* now more until the system says no: must be EAGAIN, not a fault */
    oltre = fatti;
    if (fatti == n) {
        while (oltre < 4096) {
            r = pthread_create(&t[oltre], NULL, filo, (void *)(long)(oltre + 1));
            if (r != 0) break;
            oltre++;
        }
        printf("MOLTI tetto %d %d fili in tutto, poi %s\n",
               (r == EAGAIN || r == ENOMEM || oltre == 4096) ? 0 : 1, oltre,
               r ? strerror(r) : "nessun rifiuto");
        pthread_mutex_lock(&m);
        while (pronti < oltre) pthread_cond_wait(&c, &m);
        pthread_mutex_unlock(&m);
    }

    pthread_mutex_lock(&m);
    via = 1;
    pthread_cond_broadcast(&c);
    pthread_mutex_unlock(&m);
    for (i = 0; i < oltre; i++) {
        void *ris = NULL;
        if (pthread_join(t[i], &ris) == 0 && ris == (void *)(long)(i + 1)) giusti++;
    }
    printf("MOLTI join %d %d su %d\n", giusti == oltre ? 0 : 1, giusti, oltre);
    printf("MOLTI errno %d %d sbagliati\n", errori_errno ? 1 : 0, errori_errno);
    printf("MOLTI chiave %d %d sbagliate\n", errori_chiave ? 1 : 0, errori_chiave);
    printf("MOLTI pila %d %d rovinate\n", errori_pila ? 1 : 0, errori_pila);

    /* the slots are reused: one more round of 50 after the join */
    for (i = 0, fatti = 0; i < 50; i++) {
        pronti = 0; via = 1;
        if (pthread_create(&t[i], NULL, filo, (void *)(long)(i + 1)) == 0) fatti++;
    }
    for (i = 0, giusti = 0; i < fatti; i++)
        if (pthread_join(t[i], NULL) == 0) giusti++;
    printf("MOLTI ancora %d %d su 50\n", giusti == 50 ? 0 : 1, giusti);
    printf("MOLTI fine\n");
    return 0;
}
