/* =============================================================================
 * lib/libc_gnu.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * What Mesa asks of the libc (voce 88, 7 November 2026)
 *
 * The list came from building Mesa 26.2.6 on the host and comparing what its
 * libraries leave undefined with what the EX-OS libraries define. Every name
 * here is POSIX 2008 or the GNU/BSD extension every Unix libc has; Mesa is the
 * first to ask, others (Qt, LLVM, Python) ask the same.
 *
 *     open_memstream, getline, getdelim         stdio
 *     pthread_barrier_*                          threads
 *     newlocale, duplocale, freelocale,
 *     uselocale, strtod_l, strtof_l, strtold_l   locale objects
 *     stpcpy, stpncpy, qsort_r, reallocarray,
 *     secure_getenv                              strings, memory, environment
 *     sched_getaffinity, sched_setaffinity,
 *     sched_getcpu, __sched_cpucount,
 *     pthread_getaffinity_np, pthread_setaffinity_np,
 *     pthread_getcpuclockid                      processors
 *     getpriority, setpriority                   priority
 *     program_invocation_short_name              (libc_avvio.c)
 *
 * ! THEY SAY WHAT THE SYSTEM IS, NOT WHAT A PROGRAM WOULD LIKE. EX-OS has one
 * locale ("C"), runs programs on one processor (sysconf says 1) and has one
 * priority (sched_get_priority_min/max say 0). So a locale object other than
 * "C" is refused with ENOENT, an affinity without processor 0 with EINVAL, a
 * priority other than 0 with EACCES. When the kernel learns more, these
 * learn it with it; a yes here would be a lie the program builds upon.
 *
 * Included by lib/libc.c inside its static-only part: ported software links
 * libc.a, and the floppy's libc.so has no room for what no EX-OS program
 * calls. It sees the libc's own statics (struct _FILE, scarica).
 * ============================================================================= */

/* ---------------------------------------------------------------------------
 * open_memstream: a stream that writes into a buffer from malloc
 *
 * The stream is an ordinary FILE without a descriptor (fd -1): scarica()
 * hands its buffer to memstream_scarica instead of SYS_WRITE. After every
 * fflush and at fclose *dove points to the bytes written, always followed by
 * a '\0', and *quanti says how many (the position, as POSIX wants). The
 * buffer belongs to the caller: fclose does not free it.
 * --------------------------------------------------------------------------- */
static int memstream_cresce(FILE *f, size_t serve)
{
    size_t cap = f->mem_cap ? f->mem_cap : 64;
    char  *n;

    while (cap < serve + 1) {
        if (cap > (size_t)-1 / 2) { errno = ENOMEM; return -1; }
        cap *= 2;
    }
    if (cap == f->mem_cap) return 0;
    n = (char *)realloc(f->mem, cap);
    if (n == NULL) { errno = ENOMEM; return -1; }
    f->mem = n;
    f->mem_cap = cap;
    return 0;
}

static void memstream_annuncia(FILE *f)
{
    *f->mem_dove   = f->mem;
    *f->mem_quanti = f->mem_pos < f->mem_fine ? f->mem_pos : f->mem_fine;
}

static int memstream_scarica(FILE *f)
{
    size_t n = f->pos;

    f->pos = 0;
    if (n == 0) { memstream_annuncia(f); return 0; }
    if (memstream_cresce(f, f->mem_pos + n) != 0) { f->flag |= _F_ERR; return -1; }
    /* a seek past the end leaves a hole: POSIX fills it with zeros */
    if (f->mem_pos > f->mem_fine) memset(f->mem + f->mem_fine, 0, f->mem_pos - f->mem_fine);
    memcpy(f->mem + f->mem_pos, f->buf, n);
    f->mem_pos += n;
    if (f->mem_pos > f->mem_fine) f->mem_fine = f->mem_pos;
    f->mem[f->mem_fine] = '\0';
    memstream_annuncia(f);
    return 0;
}

FILE *open_memstream(char **dove, size_t *quanti)
{
    FILE *f;

    if (dove == NULL || quanti == NULL) { errno = EINVAL; return NULL; }
    f = fdopen(0, "w");             /* the buffer and the lock, set as for a file */
    if (f == NULL) return NULL;
    f->fd = -1;                     /* no descriptor: fclose closes nothing */
    f->mem_dove = dove;
    f->mem_quanti = quanti;
    if (memstream_cresce(f, 0) != 0) { fclose(f); return NULL; }
    f->mem[0] = '\0';
    memstream_annuncia(f);
    return f;
}

static long memstream_dove(FILE *f)
{
    return (long)f->mem_pos + ((f->flag & _F_INSCRIT) ? (long)f->pos : 0);
}

static int memstream_vai(FILE *f, long off, int da)
{
    long base;

    if (memstream_scarica(f) != 0) return -1;
    f->flag &= ~(unsigned)_F_INSCRIT;
    base = da == 0 ? 0 : da == 1 ? (long)f->mem_pos : da == 2 ? (long)f->mem_fine : -1;
    if (base < 0 || (off < 0 && -off > base)) { errno = EINVAL; return -1; }
    f->mem_pos = (size_t)(base + off);
    f->flag &= ~(unsigned)_F_EOF;
    memstream_annuncia(f);
    return 0;
}

/* ---------------------------------------------------------------------------
 * getline, getdelim: a whole line, however long, in a buffer that grows
 * --------------------------------------------------------------------------- */
ssize_t getdelim(char **riga, size_t *dim, int delim, FILE *f)
{
    size_t n = 0;
    int    c;

    if (riga == NULL || dim == NULL || f == NULL) { errno = EINVAL; return -1; }
    if (*riga == NULL) *dim = 0;

    flockfile(f);
    for (;;) {
        c = fgetc_unlocked(f);
        if (c == EOF) break;
        if (n + 2 > *dim) {
            size_t nuova = *dim ? *dim * 2 : 128;
            char  *p;
            while (nuova < n + 2) nuova *= 2;
            p = (char *)realloc(*riga, nuova);
            if (p == NULL) { funlockfile(f); errno = ENOMEM; return -1; }
            *riga = p;
            *dim  = nuova;
        }
        (*riga)[n++] = (char)c;
        if (c == delim) break;
    }
    funlockfile(f);

    if (n == 0) return -1;          /* end of file (or an error) before any byte */
    (*riga)[n] = '\0';
    return (ssize_t)n;
}

ssize_t getline(char **riga, size_t *dim, FILE *f)
{
    return getdelim(riga, dim, '\n', f);
}

/* ---------------------------------------------------------------------------
 * pthread_barrier: N threads wait until the last one arrives
 *
 * A mutex, a condition and a generation counter. The generation is what
 * makes a barrier reusable: a thread released from round k that runs ahead
 * and waits again is in round k+1, and the threads of round k still waking
 * up compare against THEIR round, not against a count already reset.
 * --------------------------------------------------------------------------- */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t  c;
    unsigned int    quanti;     /* how many must arrive */
    unsigned int    arrivati;   /* how many are waiting in this round */
    unsigned int    giro;       /* the round */
} pthread_barrier_t;
typedef struct { int condiviso; } pthread_barrierattr_t;
#define PTHREAD_BARRIER_SERIAL_THREAD  (-1)

int pthread_barrierattr_init(pthread_barrierattr_t *a)    { if (!a) return EINVAL; a->condiviso = 0; return 0; }
int pthread_barrierattr_destroy(pthread_barrierattr_t *a) { return a ? 0 : EINVAL; }
int pthread_barrierattr_getpshared(const pthread_barrierattr_t *a, int *v)
{
    if (!a || !v) return EINVAL;
    *v = a->condiviso;
    return 0;
}
int pthread_barrierattr_setpshared(pthread_barrierattr_t *a, int v)
{
    /* PTHREAD_PROCESS_SHARED (1) would need the mutex in shared memory to
     * work across processes: EX-OS's threads are inside one process. */
    if (!a || (v != 0 && v != 1)) return EINVAL;
    if (v == 1) return 95;      /* ENOTSUP */
    a->condiviso = v;
    return 0;
}

int pthread_barrier_init(pthread_barrier_t *b, const pthread_barrierattr_t *a, unsigned int quanti)
{
    (void)a;
    if (b == NULL || quanti == 0) return EINVAL;
    pthread_mutex_init(&b->m, NULL);
    pthread_cond_init(&b->c, NULL);
    b->quanti = quanti;
    b->arrivati = 0;
    b->giro = 0;
    return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *b)
{
    if (b == NULL) return EINVAL;
    pthread_mutex_lock(&b->m);
    if (b->arrivati > 0) { pthread_mutex_unlock(&b->m); return EBUSY; }
    pthread_mutex_unlock(&b->m);
    pthread_cond_destroy(&b->c);
    pthread_mutex_destroy(&b->m);
    return 0;
}

int pthread_barrier_wait(pthread_barrier_t *b)
{
    unsigned int mio;

    if (b == NULL) return EINVAL;
    pthread_mutex_lock(&b->m);
    mio = b->giro;
    if (++b->arrivati == b->quanti) {
        b->arrivati = 0;
        b->giro++;
        pthread_cond_broadcast(&b->c);
        pthread_mutex_unlock(&b->m);
        return PTHREAD_BARRIER_SERIAL_THREAD;   /* exactly one thread per round */
    }
    while (mio == b->giro) pthread_cond_wait(&b->c, &b->m);
    pthread_mutex_unlock(&b->m);
    return 0;
}

/* ---------------------------------------------------------------------------
 * Locale objects. There is one locale, "C" (see setlocale), so there is one
 * object; "POSIX" and "" are its names too. The _l functions behave as
 * their plain versions because the plain versions ARE the "C" locale: the
 * decimal point of strtod is always '.', which is what Mesa wants it for
 * (a shader's 1.5 is 1.5 whatever language the user speaks).
 * --------------------------------------------------------------------------- */
struct __exos_locale { int nulla; };
typedef struct __exos_locale *locale_t;
static struct __exos_locale g_locale_c;
#define LC_GLOBAL_LOCALE_V ((locale_t)-1L)
static locale_t g_locale_in_uso = LC_GLOBAL_LOCALE_V;

static int nome_locale_c(const char *n)
{
    return n == NULL || n[0] == '\0' || strcmp(n, "C") == 0 || strcmp(n, "POSIX") == 0 ||
           strcmp(n, "C.UTF-8") == 0 || strcmp(n, "C.utf8") == 0;
}

locale_t newlocale(int maschera, const char *nome, locale_t base)
{
    (void)base;
    if (maschera & ~0x7F) { errno = EINVAL; return (locale_t)0; }
    if (!nome_locale_c(nome)) { errno = ENOENT; return (locale_t)0; }
    return &g_locale_c;
}

locale_t duplocale(locale_t l)
{
    if (l == (locale_t)0) { errno = EINVAL; return (locale_t)0; }
    return &g_locale_c;
}

void freelocale(locale_t l) { (void)l; }   /* the one object is static */

/* ! ONE SETTING FOR THE WHOLE PROCESS, not one per thread: with one locale
 * the answer a thread gets cannot differ, only which name it is told. */
locale_t uselocale(locale_t l)
{
    locale_t prima = g_locale_in_uso;
    if (l != (locale_t)0) g_locale_in_uso = l;
    return prima;
}

double      strtod_l(const char *s, char **fine, locale_t l)  { (void)l; return strtod(s, fine); }
float       strtof_l(const char *s, char **fine, locale_t l)  { (void)l; return strtof(s, fine); }
long double strtold_l(const char *s, char **fine, locale_t l) { (void)l; return strtold(s, fine); }

/* ---------------------------------------------------------------------------
 * Strings, memory, environment
 * --------------------------------------------------------------------------- */
char *stpcpy(char *d, const char *s)
{
    while ((*d = *s++) != '\0') d++;
    return d;                       /* the '\0' just written */
}

char *stpncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i]) { d[i] = s[i]; i++; }
    {
        char *fine = d + i;
        while (i < n) d[i++] = '\0';
        return fine;
    }
}

/* qsort_r, the GNU order (the context is the comparison's LAST argument, as
 * POSIX 2024 also chose). The same shell sort as qsort, and for its reasons. */
void qsort_r(void *base, size_t n, size_t dim,
             int (*cmp)(const void *, const void *, void *), void *ctx)
{
    char  *v = (char *)base;
    size_t salto;

    if (base == NULL || cmp == NULL || dim == 0) return;
    for (salto = n / 2; salto > 0; salto /= 2) {
        size_t i;
        for (i = salto; i < n; i++) {
            size_t j;
            for (j = i; j >= salto; j -= salto) {
                char *a = v + (j - salto) * dim;
                char *b = v + j * dim;
                size_t k;
                if (cmp(a, b, ctx) <= 0) break;
                for (k = 0; k < dim; k++) { char t = a[k]; a[k] = b[k]; b[k] = t; }
            }
        }
    }
}

void *reallocarray(void *p, size_t n, size_t dim)
{
    if (dim != 0 && n > (size_t)-1 / dim) { errno = ENOMEM; return NULL; }
    return realloc(p, n * dim);
}

/* secure_getenv: NULL in a program running with someone else's rights. */
char *secure_getenv(const char *nome)
{
    if (getuid() != geteuid()) return NULL;
    return getenv(nome);
}

/* ---------------------------------------------------------------------------
 * Processors. A set as glibc has it (1024 bits): the macros CPU_ZERO,
 * CPU_SET, CPU_ISSET, CPU_COUNT are in <sched.h>; CPU_COUNT calls
 * __sched_cpucount, the name glibc gives it, since programs built
 * elsewhere call that. EX-OS runs programs on processor 0 only.
 * --------------------------------------------------------------------------- */
typedef struct { unsigned long bit[1024 / (8 * sizeof(unsigned long))]; } cpu_set_t;

int __sched_cpucount(size_t dim, const cpu_set_t *s)
{
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;
    size_t i;
    /* by hand: __builtin_popcount becomes a libgcc call (__popcountdi2),
     * and the system's programs link no libgcc */
    for (i = 0; i < dim; i++) {
        unsigned int b = p[i];
        while (b) { n += (int)(b & 1u); b >>= 1; }
    }
    return n;
}

static int affinita_dai(size_t dim, cpu_set_t *s)
{
    if (s == NULL || dim == 0) { errno = EINVAL; return -1; }
    memset(s, 0, dim);
    ((unsigned char *)s)[0] = 1;    /* processor 0 */
    return 0;
}

/* a set is acceptable when it holds processor 0: there the program runs */
static int affinita_va(size_t dim, const cpu_set_t *s)
{
    if (s == NULL || dim == 0 || !(((const unsigned char *)s)[0] & 1)) { errno = EINVAL; return -1; }
    return 0;
}

int sched_getaffinity(int pid, size_t dim, cpu_set_t *s)       { (void)pid; return affinita_dai(dim, s); }
int sched_setaffinity(int pid, size_t dim, const cpu_set_t *s) { (void)pid; return affinita_va(dim, s); }
int sched_getcpu(void) { return 0; }

/* the _np versions return the error instead of setting errno */
int pthread_getaffinity_np(pthread_t t, size_t dim, cpu_set_t *s)
{
    (void)t;
    return affinita_dai(dim, s) ? EINVAL : 0;
}
int pthread_setaffinity_np(pthread_t t, size_t dim, const cpu_set_t *s)
{
    (void)t;
    return affinita_va(dim, s) ? EINVAL : 0;
}

/* A thread's CPU clock. clock_gettime answers CLOCK_THREAD_CPUTIME_ID with
 * the monotonic time (the kernel counts no CPU time per thread yet), and only
 * for the thread that asks: so only the calling thread has a clock. */
int pthread_getcpuclockid(pthread_t t, int *orologio)
{
    if (orologio == NULL) return EINVAL;
    if (t != pthread_self()) return ENOENT;     /* POSIX: not supported */
    *orologio = 3;                              /* CLOCK_THREAD_CPUTIME_ID */
    return 0;
}

/* ---------------------------------------------------------------------------
 * Priority: there is one, 0 (see sched_get_priority_min/max).
 * --------------------------------------------------------------------------- */
int getpriority(int quale, unsigned int chi)
{
    if (quale < 0 || quale > 2) { errno = EINVAL; return -1; }
    (void)chi;
    errno = 0;          /* -1 is a valid priority: callers look at errno */
    return 0;
}

int setpriority(int quale, unsigned int chi, int prio)
{
    if (quale < 0 || quale > 2) { errno = EINVAL; return -1; }
    (void)chi;
    if (prio != 0) { errno = EACCES; return -1; }
    return 0;
}
