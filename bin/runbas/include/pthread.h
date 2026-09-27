/* =============================================================================
 * bin/runbas/include/pthread.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The four mutex calls of gfbasic/gf_basic.c, for runbas.
 *
 * ! THEY DO NOTHING, AND IT IS CORRECT HERE: the interpreter takes its mutex
 * so that one GF_BASIC object can be shared between threads. runbas has one
 * thread and one object. A program that embeds the interpreter in more than
 * one thread must link the real ones (lib/libc has threads since 4 September
 * 2026, see @FILI) and not this file.
 * ============================================================================= */
#ifndef RUNBAS_PTHREAD_H
#define RUNBAS_PTHREAD_H

typedef int pthread_mutex_t;

#define pthread_mutex_init(m, a)   ((void)(m), (void)(a))
#define pthread_mutex_destroy(m)   ((void)(m))
#define pthread_mutex_lock(m)      ((void)(m))
#define pthread_mutex_unlock(m)    ((void)(m))

#endif
