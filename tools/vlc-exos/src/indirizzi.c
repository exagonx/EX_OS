/*****************************************************************************
 * indirizzi.c: vlc_addr_wait and friends for EX-OS (tools/vlc-exos)
 *****************************************************************************
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * On Linux these are the futex. EX-OS has none: one mutex and a condition
 * per bucket of addresses. The waiter looks at the value with the mutex held
 * and whoever changes it wakes under the same mutex, so no wake is lost;
 * waking a whole bucket is allowed, the callers loop on their value.
 * Appended to src/posix/thread.c (Android's) by costruisci.sh.
 *****************************************************************************/
#define EXOS_SECCHI 64

static pthread_mutex_t exos_lucchetto = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t exos_secchi[EXOS_SECCHI];
static pthread_once_t exos_una = PTHREAD_ONCE_INIT;

static void exos_prepara(void)
{
    for (int i = 0; i < EXOS_SECCHI; i++)
        pthread_cond_init(&exos_secchi[i], NULL);
}

static pthread_cond_t *exos_secchio(void *addr)
{
    pthread_once(&exos_una, exos_prepara);
    return &exos_secchi[((uintptr_t)addr >> 2) % EXOS_SECCHI];
}

void vlc_addr_signal(void *addr)
{
    vlc_addr_broadcast(addr);
}

void vlc_addr_broadcast(void *addr)
{
    pthread_cond_t *c = exos_secchio(addr);

    pthread_mutex_lock(&exos_lucchetto);
    pthread_cond_broadcast(c);
    pthread_mutex_unlock(&exos_lucchetto);
}

void vlc_addr_wait(void *addr, unsigned value)
{
    pthread_cond_t *c = exos_secchio(addr);

    pthread_mutex_lock(&exos_lucchetto);
    if (atomic_load_explicit((atomic_uint *)addr, memory_order_relaxed) == value)
        pthread_cond_wait(c, &exos_lucchetto);
    pthread_mutex_unlock(&exos_lucchetto);
}

bool vlc_addr_timedwait(void *addr, unsigned value, mtime_t delay)
{
    pthread_cond_t *c = exos_secchio(addr);
    struct timespec ts;
    int r = 0;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += delay / CLOCK_FREQ;
    ts.tv_nsec += (delay % CLOCK_FREQ) * (1000000000 / CLOCK_FREQ);
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    pthread_mutex_lock(&exos_lucchetto);
    if (atomic_load_explicit((atomic_uint *)addr, memory_order_relaxed) == value)
        r = pthread_cond_timedwait(c, &exos_lucchetto, &ts);
    pthread_mutex_unlock(&exos_lucchetto);
    return r == 0;
}

/* linux/thread.c has this one with gettid: the thread's own number here. */
unsigned long vlc_thread_id(void)
{
    return (unsigned long)pthread_self();
}
