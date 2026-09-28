/* =============================================================================
 * tools/exilla/prova-pthread.c — i thread POSIX dentro EX-OS (@PTHREAD)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Tappa 1 di Exilla (28 settembre 2026). Ogni controllo stampa [OK] o [NO];
 * l'ultima riga dice quanti NO. Si esegue con tools/exilla/prova-pthread.sh.
 *
 * Dal kernel 0.222 anche la parte del kernel: quaranta fili insieme (FILO_MAX
 * 64), fili staccati che il kernel raccoglie da solo, la pila vera da
 * pthread_getattr_np e un filo che ne usa un mega.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

static int g_no = 0;

static void ok(const char *cosa, int va)
{
    printf("  %s  %s\n", va ? "[OK]" : "[NO]", cosa);
    if (!va) g_no++;
}

/* 1. create/join col valore di ritorno */
static void *doppio(void *arg) { return (void *)((long)arg * 2); }

/* 2. un lucchetto conteso */
static pthread_mutex_t g_m = PTHREAD_MUTEX_INITIALIZER;
static long g_conto = 0;
static void *conta(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < 20000; i++) {
        pthread_mutex_lock(&g_m);
        g_conto++;
        pthread_mutex_unlock(&g_m);
    }
    return NULL;
}

/* 3. produttore e consumatore su una condizione */
static pthread_mutex_t g_cm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_piena = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  g_vuota = PTHREAD_COND_INITIALIZER;
static int g_casella = 0, g_ce = 0;
static void *produttore(void *arg)
{
    int i;
    (void)arg;
    for (i = 1; i <= 200; i++) {
        pthread_mutex_lock(&g_cm);
        while (g_ce) pthread_cond_wait(&g_vuota, &g_cm);
        g_casella = i; g_ce = 1;
        pthread_cond_signal(&g_piena);
        pthread_mutex_unlock(&g_cm);
    }
    return NULL;
}

/* 5. once */
static pthread_once_t g_una = PTHREAD_ONCE_INIT;
static int g_una_volte = 0;
static void inizia(void) { g_una_volte++; }
static void *chiama_una(void *arg) { (void)arg; pthread_once(&g_una, inizia); return NULL; }

/* 6. chiavi con distruttore */
static pthread_key_t g_chiave;
static int g_distrutti = 0;
static pthread_mutex_t g_dm = PTHREAD_MUTEX_INITIALIZER;
static void distruggi(void *v)
{
    pthread_mutex_lock(&g_dm);
    g_distrutti += (int)(long)v;
    pthread_mutex_unlock(&g_dm);
}
static void *usa_chiave(void *arg)
{
    pthread_setspecific(g_chiave, arg);
    return pthread_getspecific(g_chiave);
}

/* 9. semaforo */
static sem_t g_sem;
static void *posta(void *arg) { (void)arg; sem_post(&g_sem); return NULL; }

/* 10. TLS e errno per filo */
static __thread int t_mio = 7;
static void *tocca_tls(void *arg) { t_mio = (int)(long)arg; return (void *)(long)t_mio; }

/* 11. tanti fili insieme */
static volatile int g_vivi = 0, g_via = 0;
static pthread_mutex_t g_vm = PTHREAD_MUTEX_INITIALIZER;
static void *resta(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_vm); g_vivi++; pthread_mutex_unlock(&g_vm);
    while (!g_via) sched_yield();
    return NULL;
}

/* 12. fili staccati a raffica */
static volatile int g_staccati = 0;
static void *breve(void *arg)
{
    (void)arg;
    pthread_mutex_lock(&g_vm); g_staccati++; pthread_mutex_unlock(&g_vm);
    return NULL;
}

/* 13. la pila: dove sta, e un mega usato davvero */
static void *misura_pila(void *arg)
{
    pthread_attr_t a;
    void  *base = NULL;
    size_t misura = 0;
    volatile char grande[1024 * 1024];
    char  *qui = (char *)&a;
    int    i, ok_ = 0;

    (void)arg;
    if (pthread_getattr_np(pthread_self(), &a) == 0 &&
        pthread_attr_getstack(&a, &base, &misura) == 0 &&
        qui > (char *)base && qui < (char *)base + misura &&
        misura >= 2u * 1024u * 1024u - 65536u) ok_ = 1;
    for (i = 0; i < (int)sizeof(grande); i += 4096) grande[i] = (char)i;
    for (i = 0; i < (int)sizeof(grande); i += 4096) if (grande[i] != (char)i) ok_ = 0;
    return (void *)(long)ok_;
}

int main(void)
{
    pthread_t t[6];
    void     *r;
    int       i, somma = 0;

    printf("prova-pthread: i thread POSIX dentro EX-OS\n");

    for (i = 0; i < 6; i++) pthread_create(&t[i], NULL, doppio, (void *)(long)(i + 1));
    for (i = 0; i < 6; i++) { pthread_join(t[i], &r); somma += (int)(long)r; }
    ok("create e join, sei fili, valore di ritorno (somma 42)", somma == 42);

    for (i = 0; i < 6; i++) pthread_create(&t[i], NULL, conta, NULL);
    for (i = 0; i < 6; i++) pthread_join(t[i], NULL);
    ok("lucchetto conteso: 6 x 20000 incrementi = 120000", g_conto == 120000);

    {
        pthread_t p;
        int letti = 0, somma_c = 0;
        pthread_create(&p, NULL, produttore, NULL);
        while (letti < 200) {
            pthread_mutex_lock(&g_cm);
            while (!g_ce) pthread_cond_wait(&g_piena, &g_cm);
            somma_c += g_casella; g_ce = 0; letti++;
            pthread_cond_signal(&g_vuota);
            pthread_mutex_unlock(&g_cm);
        }
        pthread_join(p, NULL);
        ok("condizione: produttore e consumatore, 200 passaggi (somma 20100)", somma_c == 20100);
    }

    {
        pthread_rwlock_t l = PTHREAD_RWLOCK_INITIALIZER;
        int a = pthread_rwlock_rdlock(&l), b = pthread_rwlock_tryrdlock(&l);
        int c = pthread_rwlock_trywrlock(&l);          /* due lettori: EBUSY */
        pthread_rwlock_unlock(&l); pthread_rwlock_unlock(&l);
        int d = pthread_rwlock_trywrlock(&l);          /* libero: 0 */
        int e = pthread_rwlock_tryrdlock(&l);          /* uno scrive: EBUSY */
        pthread_rwlock_unlock(&l);
        ok("rwlock: due lettori insieme, lo scrittore aspetta", a == 0 && b == 0 && c == EBUSY && d == 0 && e == EBUSY);
    }

    for (i = 0; i < 4; i++) pthread_create(&t[i], NULL, chiama_una, NULL);
    for (i = 0; i < 4; i++) pthread_join(t[i], NULL);
    pthread_once(&g_una, inizia);
    ok("pthread_once: una volta sola fra cinque chiamanti", g_una_volte == 1);

    pthread_key_create(&g_chiave, distruggi);
    for (i = 0; i < 3; i++) pthread_create(&t[i], NULL, usa_chiave, (void *)(long)(i + 1));
    somma = 0;
    for (i = 0; i < 3; i++) { pthread_join(t[i], &r); somma += (int)(long)r; }
    ok("chiavi: ogni filo la sua, distruttori all'uscita (1+2+3)", somma == 6 && g_distrutti == 6);

    {
        pthread_mutex_t rm;
        pthread_mutexattr_t ma;
        pthread_mutexattr_init(&ma);
        pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&rm, &ma);
        int a = pthread_mutex_lock(&rm), b = pthread_mutex_lock(&rm);
        int c = pthread_mutex_unlock(&rm), d = pthread_mutex_unlock(&rm);
        int e = pthread_mutex_unlock(&rm);             /* non e' piu' mio */
        ok("lucchetto ricorsivo: due prese, due rilasci, il terzo rifiutato",
           a == 0 && b == 0 && c == 0 && d == 0 && e == EPERM);
    }

    {
        pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t  c;
        pthread_condattr_t ca;
        struct timespec a, b, fine;
        pthread_condattr_init(&ca);
        pthread_condattr_setclock(&ca, CLOCK_MONOTONIC);
        pthread_cond_init(&c, &ca);
        clock_gettime(CLOCK_MONOTONIC, &a);
        fine = a;
        fine.tv_nsec += 300000000;
        if (fine.tv_nsec >= 1000000000) { fine.tv_sec++; fine.tv_nsec -= 1000000000; }
        pthread_mutex_lock(&m);
        int e = pthread_cond_timedwait(&c, &m, &fine);
        pthread_mutex_unlock(&m);
        clock_gettime(CLOCK_MONOTONIC, &b);
        long ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
        printf("        (attesa scaduta dopo %ld ms)\n", ms);
        ok("timedwait sull'orologio monotono: ETIMEDOUT dopo ~300 ms",
           e == ETIMEDOUT && ms >= 280 && ms < 1000);
    }

    {
        pthread_t p;
        sem_init(&g_sem, 0, 0);
        int prima = sem_trywait(&g_sem);
        pthread_create(&p, NULL, posta, NULL);
        sem_wait(&g_sem);
        pthread_join(p, NULL);
        ok("semaforo: vuoto rifiuta, poi un altro filo lo riempie", prima == -1);
    }

    {
        pthread_t p;
        pthread_create(&p, NULL, tocca_tls, (void *)99L);
        pthread_join(p, &r);
        ok("__thread per filo: il figlio scrive 99, il principale ha ancora 7",
           (int)(long)r == 99 && t_mio == 7);
    }

    {
        pthread_attr_t at;
        pthread_t p;
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        int e = pthread_create(&p, &at, doppio, (void *)1L);
        struct timespec pausa = { 0, 100000000 };
        nanosleep(&pausa, NULL);
        ok("filo staccato: parte, e join lo rifiuta", e == 0 && pthread_join(p, NULL) != 0);
    }

    ok("pthread_self e pthread_equal", pthread_equal(pthread_self(), pthread_self()) != 0);

    {
        pthread_t q[40];
        int fatti = 0;
        for (i = 0; i < 40; i++) if (pthread_create(&q[i], NULL, resta, NULL) == 0) fatti++;
        while (g_vivi < fatti) sched_yield();
        g_via = 1;
        for (i = 0; i < fatti; i++) pthread_join(q[i], NULL);
        printf("        (%d fili vivi insieme)\n", fatti);
        ok("quaranta fili insieme, tutti aspettati", fatti == 40);
    }

    {
        pthread_attr_t at;
        pthread_t p;
        int partiti = 0, giro;
        struct timespec pausa = { 0, 20000000 };
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        for (giro = 0; giro < 150; giro++) {
            int tent;
            for (tent = 0; tent < 50; tent++) {
                if (pthread_create(&p, &at, breve, NULL) == 0) { partiti++; break; }
                nanosleep(&pausa, NULL);    /* il reaper passa ogni 100 ms */
            }
        }
        for (giro = 0; giro < 100 && g_staccati < partiti; giro++) nanosleep(&pausa, NULL);
        printf("        (%d staccati partiti, %d arrivati)\n", partiti, g_staccati);
        ok("150 fili staccati: il kernel li raccoglie da solo", partiti == 150 && g_staccati == 150);
    }

    {
        pthread_t p;
        pthread_attr_t a;
        void *base; size_t misura;
        int e = pthread_getattr_np(pthread_self(), &a);
        pthread_attr_getstack(&a, &base, &misura);
        printf("        (pila del principale: %p, %u KB)\n", base, (unsigned)(misura / 1024));
        pthread_create(&p, NULL, misura_pila, NULL);
        pthread_join(p, &r);
        ok("pila: getattr_np la trova, un filo ne usa un mega", e == 0 && (long)r == 1);
    }

    printf("prova-pthread: %d NO\n", g_no);
    return g_no;
}
