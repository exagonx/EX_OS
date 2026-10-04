/* =============================================================================
 * lib/include/sys/resource.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/resource.h> — i limiti di un processo (@EXILLA-JS, 30 settembre 2026).
 *
 * ! EX-OS NON HA LIMITI DA IMPOSTARE: getrlimit dice quelli che ci sono per
 * costruzione (la pila principale e' la riserva di 256 KB, i descrittori sono
 * 32, lo spazio di indirizzamento non ha un tetto da chiedere), setrlimit
 * accetta solo di lasciarli come sono. getrusage sta gia' in libc.h.
 * ============================================================================= */
#ifndef EXOS_SYS_RESOURCE_H
#define EXOS_SYS_RESOURCE_H

#include "../libc.h"

typedef unsigned long rlim_t;
#define RLIM_INFINITY   (~0UL)
#define RLIM_SAVED_MAX  RLIM_INFINITY
#define RLIM_SAVED_CUR  RLIM_INFINITY

#define RLIMIT_CPU      0
#define RLIMIT_FSIZE    1
#define RLIMIT_DATA     2
#define RLIMIT_STACK    3
#define RLIMIT_CORE     4
#define RLIMIT_RSS      5
#define RLIMIT_NPROC    6
#define RLIMIT_NOFILE   7
#define RLIMIT_MEMLOCK  8
#define RLIMIT_AS       9

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};


#ifdef __cplusplus
extern "C" {
#endif
int getrlimit(int risorsa, struct rlimit *r);
int setrlimit(int risorsa, const struct rlimit *r);
#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_RESOURCE_H */
