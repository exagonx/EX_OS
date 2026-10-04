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

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SCHED_H */
