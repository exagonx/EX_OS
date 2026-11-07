/* =============================================================================
 * lib/include/pthread.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * I thread POSIX sopra i fili di EX-OS (@PTHREAD, 28 settembre 2026 — tappa 1
 * di Exilla).
 *
 * ! NON E' UN SECONDO SISTEMA DI FILI: e' un'interfaccia. Sotto ci sono
 * thread_crea/thread_attendi, il lucchetto che dorme (Mutex), le condizioni e
 * i semafori della libc (lib/include/libc.h, «I FILI»). Qui si da' loro la
 * forma che il codice di terzi si aspetta — NSPR, SpiderMonkey, mozglue, la
 * std di Rust la chiamano per nome — senza cambiare cosa fanno.
 *
 * ! pthread_t E' UN NUMERO, e dentro c'e' l'indirizzo del descrittore del
 * filo. Un intero perche' molto codice lo stampa, lo confronta con 0 o lo usa
 * come chiave; l'indirizzo perche' join, detach e le chiavi ci arrivano senza
 * cercare.
 *
 * ! LE COSE CHE QUI NON VALGONO, e sono dichiarate invece che scoperte:
 *   - la priorita' e la politica di scheduling (setschedparam & c.): EX-OS ha
 *     uno scheduler solo, e queste funzioni rispondono 0 senza fare niente;
 *   - (since kernel 0.227, in libc.a) pthread_sigmask and pthread_kill are
 *     real: the mask is per thread, the actions per process. In the libc.so
 *     of the floppy pthread_sigmask still answers 0 and does nothing;
 *   - la cancellazione asincrona non esiste: vedi thread_ferma in libc.h;
 *   - pthread_attr_setstacksize si ricorda la misura, ma la pila di un filo
 *     la sceglie il kernel (FILO_STACK_SIZE in kernel/include/sched.h: 2 MB
 *     di riserva); pthread_getattr_np dice quella vera.
 * ============================================================================= */
#ifndef EXOS_PTHREAD_H
#define EXOS_PTHREAD_H

#include "libc.h"
#include "sched.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long pthread_t;

/* --- attributi di un filo ------------------------------------------------- */
#define PTHREAD_CREATE_JOINABLE  0
#define PTHREAD_CREATE_DETACHED  1
#define PTHREAD_INHERIT_SCHED    0
#define PTHREAD_EXPLICIT_SCHED   1
#define PTHREAD_SCOPE_SYSTEM     0
#define PTHREAD_SCOPE_PROCESS    1
#define PTHREAD_STACK_MIN        16384

typedef struct {
    int     staccato;
    size_t  pila;           /* misura chiesta (vedi sopra: la sceglie il kernel) */
    void   *pila_base;      /* riempiti da pthread_getattr_np */
    size_t  pila_misura;
} pthread_attr_t;

/* struct sched_param e SCHED_*: stanno in <sched.h>, che POSIX vuole incluso
 * da <pthread.h>. */

/* --- lucchetti ---------------------------------------------------------------- */
#define PTHREAD_MUTEX_NORMAL      0
#define PTHREAD_MUTEX_RECURSIVE   1
#define PTHREAD_MUTEX_ERRORCHECK  2
#define PTHREAD_MUTEX_DEFAULT     PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_RECURSIVE_NP PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_ERRORCHECK_NP PTHREAD_MUTEX_ERRORCHECK

typedef struct {
    volatile int m;         /* un Mutex della libc (tipo vero: vedi EXOS_SOLO_POSIX) */
    int          tipo;
    volatile int padrone;   /* tid di chi lo tiene (ricorsivi e con controllo) */
    int          conta;     /* quante volte, per i ricorsivi */
} pthread_mutex_t;
typedef struct { int tipo; } pthread_mutexattr_t;

#define PTHREAD_MUTEX_INITIALIZER            { MUTEX_LIBERO, PTHREAD_MUTEX_NORMAL, 0, 0 }
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP { MUTEX_LIBERO, PTHREAD_MUTEX_RECURSIVE, 0, 0 }

/* --- condizioni ----------------------------------------------------------------- */
typedef struct {
    volatile int c;         /* una Condizione della libc */
    int        orologio;    /* CLOCK_REALTIME o CLOCK_MONOTONIC, per timedwait */
} pthread_cond_t;
typedef struct { int orologio; } pthread_condattr_t;
#define PTHREAD_COND_INITIALIZER  { CONDIZIONE_ZERO, 0 }

/* --- lettori e scrittori ------------------------------------------------------------ */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  c;
    int             lettori;          /* quanti leggono adesso */
    int             scrittore;        /* 1 se qualcuno scrive */
    int             scrittori_attesa; /* chi aspetta di scrivere passa davanti */
} pthread_rwlock_t;
typedef struct { int nulla; } pthread_rwlockattr_t;
#define PTHREAD_RWLOCK_INITIALIZER \
    { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0 }

/* --- una volta sola, e le chiavi -------------------------------------------------- */
typedef volatile int pthread_once_t;
#define PTHREAD_ONCE_INIT  0

typedef unsigned int pthread_key_t;
#define PTHREAD_KEYS_MAX               128
#define PTHREAD_DESTRUCTOR_ITERATIONS  4

/* --- i fili ---------------------------------------------------------------------------- */
int       pthread_create(pthread_t *t, const pthread_attr_t *a,
                         void *(*fn)(void *), void *arg);
int       pthread_join(pthread_t t, void **ris);
int       pthread_detach(pthread_t t);
/* The handlers of fork (kernel 0.313): `prima` before it, newest first;
 * `genitore` and `figlio` after it, in the parent and in the child, oldest
 * first. No limit on how many. */
int       pthread_atfork(void (*prima)(void), void (*genitore)(void), void (*figlio)(void));
void      pthread_exit(void *ris) __attribute__((noreturn));
/* Cancellation (5 November 2026): deferred, at pthread_testcancel,
 * pthread_cond_wait/timedwait and pthread_join; ASYNCHRONOUS is accepted and
 * acts as DEFERRED; a blocking read or poll is not a point. See lib/libc.c. */
#define PTHREAD_CANCELED            ((void *)-1)
#define PTHREAD_CANCEL_ENABLE       0
#define PTHREAD_CANCEL_DISABLE      1
#define PTHREAD_CANCEL_DEFERRED     0
#define PTHREAD_CANCEL_ASYNCHRONOUS 1
int       pthread_cancel(pthread_t t);
void      pthread_testcancel(void);
int       pthread_setcancelstate(int stato, int *prima);
int       pthread_setcanceltype(int tipo, int *prima);
pthread_t pthread_self(void);
int       pthread_equal(pthread_t a, pthread_t b);
int       pthread_yield(void);
int       pthread_kill(pthread_t t, int segnale);
int       pthread_sigmask(int come, const sigset_t *nuovo, sigset_t *vecchio);
int       pthread_setname_np(pthread_t t, const char *nome);
int       pthread_getname_np(pthread_t t, char *nome, size_t max);
int       pthread_getattr_np(pthread_t t, pthread_attr_t *a);
int       pthread_setschedparam(pthread_t t, int politica, const struct sched_param *p);
int       pthread_getschedparam(pthread_t t, int *politica, struct sched_param *p);

int pthread_attr_init(pthread_attr_t *a);
int pthread_attr_destroy(pthread_attr_t *a);
int pthread_attr_setdetachstate(pthread_attr_t *a, int stato);
int pthread_attr_getdetachstate(const pthread_attr_t *a, int *stato);
int pthread_attr_setstacksize(pthread_attr_t *a, size_t misura);
int pthread_attr_getstacksize(const pthread_attr_t *a, size_t *misura);
int pthread_attr_getstack(const pthread_attr_t *a, void **base, size_t *misura);
int pthread_attr_setguardsize(pthread_attr_t *a, size_t misura);
int pthread_attr_setscope(pthread_attr_t *a, int ambito);
int pthread_attr_setinheritsched(pthread_attr_t *a, int eredita);
int pthread_attr_setschedpolicy(pthread_attr_t *a, int politica);
int pthread_attr_getschedpolicy(const pthread_attr_t *a, int *politica);
int pthread_attr_setschedparam(pthread_attr_t *a, const struct sched_param *p);
int pthread_attr_getschedparam(const pthread_attr_t *a, struct sched_param *p);

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a);
int pthread_mutex_destroy(pthread_mutex_t *m);
int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_trylock(pthread_mutex_t *m);
int pthread_mutex_timedlock(pthread_mutex_t *m, const struct timespec *quando);
int pthread_mutex_unlock(pthread_mutex_t *m);
int pthread_mutexattr_init(pthread_mutexattr_t *a);
int pthread_mutexattr_destroy(pthread_mutexattr_t *a);
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int tipo);
/* Accettato e ignorato: vedi lib/libc.c. Solo in libc.a. */
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED  1
int pthread_mutexattr_setpshared(pthread_mutexattr_t *a, int p);
int pthread_mutexattr_getpshared(const pthread_mutexattr_t *a, int *p);
int pthread_condattr_setpshared(pthread_condattr_t *a, int p);
int pthread_condattr_getpshared(const pthread_condattr_t *a, int *p);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *a, int *tipo);

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a);
int pthread_cond_destroy(pthread_cond_t *c);
int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m);
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m,
                           const struct timespec *quando);
int pthread_cond_signal(pthread_cond_t *c);
int pthread_cond_broadcast(pthread_cond_t *c);
int pthread_condattr_init(pthread_condattr_t *a);
int pthread_condattr_destroy(pthread_condattr_t *a);
int pthread_condattr_setclock(pthread_condattr_t *a, int orologio);
int pthread_condattr_getclock(const pthread_condattr_t *a, int *orologio);

int pthread_rwlock_init(pthread_rwlock_t *l, const pthread_rwlockattr_t *a);
int pthread_rwlock_destroy(pthread_rwlock_t *l);
int pthread_rwlock_rdlock(pthread_rwlock_t *l);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *l);
int pthread_rwlock_wrlock(pthread_rwlock_t *l);
int pthread_rwlock_trywrlock(pthread_rwlock_t *l);
int pthread_rwlock_unlock(pthread_rwlock_t *l);

int   pthread_once(pthread_once_t *o, void (*fn)(void));
int   pthread_key_create(pthread_key_t *k, void (*distruttore)(void *));
int   pthread_key_delete(pthread_key_t k);
void *pthread_getspecific(pthread_key_t k);
int   pthread_setspecific(pthread_key_t k, const void *valore);

#ifdef __cplusplus
}
#endif

/* pthread_cleanup_push/pop: a stack of handlers per thread, kept by the libc,
 * that pthread_exit runs, the last pushed first (pthread_cancel too, since
 * 5 November 2026). Before that a library that cancels by itself did: VLC's Android threads end a
 * cancelled thread with pthread_exit, and the handler it pushed is the one
 * that unlocks its mutex. Run by pop(1) only, as before 3 November 2026, the
 * thread died holding the lock and VLC never quit at the end of a file. A
 * pair opens and closes one block, as POSIX allows the macros to do. */
/* --- barriers (7 November 2026, Mesa): see lib/libc_gnu.c ------------------------ */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  c;
    unsigned int    quanti, arrivati, giro;
} pthread_barrier_t;
typedef struct { int condiviso; } pthread_barrierattr_t;
#define PTHREAD_BARRIER_SERIAL_THREAD  (-1)
#ifndef PTHREAD_PROCESS_PRIVATE
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED  1
#endif
#ifdef __cplusplus
extern "C" {
#endif
int pthread_barrierattr_init(pthread_barrierattr_t *a);
int pthread_barrierattr_destroy(pthread_barrierattr_t *a);
int pthread_barrierattr_getpshared(const pthread_barrierattr_t *a, int *v);
int pthread_barrierattr_setpshared(pthread_barrierattr_t *a, int v);
int pthread_barrier_init(pthread_barrier_t *b, const pthread_barrierattr_t *a, unsigned int quanti);
int pthread_barrier_destroy(pthread_barrier_t *b);
int pthread_barrier_wait(pthread_barrier_t *b);
/* processors and the thread's CPU clock: processor 0 only, see <sched.h> */
#include "sched.h"
int pthread_getaffinity_np(pthread_t t, size_t dim, cpu_set_t *s);
int pthread_setaffinity_np(pthread_t t, size_t dim, const cpu_set_t *s);
int pthread_getcpuclockid(pthread_t t, int *orologio);
#ifdef __cplusplus
}
#endif

struct exos_pulizia {
    void (*fn)(void *);
    void *arg;
    struct exos_pulizia *prec;
};
#ifdef __cplusplus
extern "C" {
#endif
void exos_pulizia_metti(struct exos_pulizia *p);
void exos_pulizia_togli(struct exos_pulizia *p, int esegui);
#ifdef __cplusplus
}
#endif
#define pthread_cleanup_push(fn, arg) \
    { struct exos_pulizia exos_pulizia_v = { (fn), (arg), 0 }; \
      exos_pulizia_metti(&exos_pulizia_v);
#define pthread_cleanup_pop(esegui) \
      exos_pulizia_togli(&exos_pulizia_v, (esegui)); }

#endif /* EXOS_PTHREAD_H */
