/* =============================================================================
 * lib/include/sched.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <sched.h> — cedere il processore, e le politiche dello scheduler.
 *
 * EX-OS ha uno scheduler solo, a priorita' fisse fra i processi: le
 * politiche POSIX ci sono come nomi (il codice di terzi le scrive), e le
 * priorita' vanno da 0 a 0. sched_yield() e' vera: passa la mano.
 * ============================================================================= */

#ifndef EXOS_SCHED_H
#define EXOS_SCHED_H

#include "libc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct sched_param { int sched_priority; };

#define SCHED_OTHER  0
#define SCHED_FIFO   1
#define SCHED_RR     2

/* sched_yield e' dichiarata in libc.h (la usano anche i programmi di EX-OS). */

/* Solo in libc.a. Rispondono 0: una priorita' sola. */
int sched_get_priority_min(int politica);
int sched_get_priority_max(int politica);

/* Processor sets (7 November 2026, Mesa), as glibc has them: 1024 bits in
 * cpu_set_t; the calls take any size (dim), so a bigger set made by hand
 * reaches every processor. Since kernel 0.319 they are real: where a thread
 * may run, set and read (pid 0 = the calling thread), and sched_getcpu. */
#define CPU_SETSIZE 1024
typedef struct { unsigned long __bit[CPU_SETSIZE / (8 * sizeof(unsigned long))]; } cpu_set_t;
#define __CPU_PAROLA(n)  ((n) / (8 * sizeof(unsigned long)))
#define __CPU_BIT(n)     (1UL << ((n) % (8 * sizeof(unsigned long))))
#define CPU_ZERO(s)      __builtin_memset((s), 0, sizeof(cpu_set_t))
#define CPU_SET(n, s)    ((void)((n) < CPU_SETSIZE ? ((s)->__bit[__CPU_PAROLA(n)] |= __CPU_BIT(n)) : 0))
#define CPU_CLR(n, s)    ((void)((n) < CPU_SETSIZE ? ((s)->__bit[__CPU_PAROLA(n)] &= ~__CPU_BIT(n)) : 0))
#define CPU_ISSET(n, s)  ((n) < CPU_SETSIZE ? (((s)->__bit[__CPU_PAROLA(n)] & __CPU_BIT(n)) != 0) : 0)
#define CPU_COUNT(s)     __sched_cpucount(sizeof(cpu_set_t), (s))
#define CPU_EQUAL(a, b)  (__builtin_memcmp((a), (b), sizeof(cpu_set_t)) == 0)
int __sched_cpucount(size_t dim, const cpu_set_t *s);
int sched_getaffinity(int pid, size_t dim, cpu_set_t *s);
int sched_setaffinity(int pid, size_t dim, const cpu_set_t *s);
int sched_getcpu(void);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SCHED_H */
