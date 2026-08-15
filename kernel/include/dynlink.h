/* =============================================================================
 * kernel/include/dynlink.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef DYNLINK_H
#define DYNLINK_H

#include "kernel.h"
#include "sched.h"

/* Indirizzo base default per le shared libraries nel processo utente */
#define DL_LIBS_BASE    0x10000000  /* 256MB — dopo l'eseguibile a 0x08000000 */

int      dynlink_init(Process *proc, uint32_t load_cursor);
int      dynlink_load_lib(const char *name, Process *proc, uint32_t base);
uint32_t dynlink_resolve(const char *name);
void     dynlink_reset(void);

#endif /* DYNLINK_H */
