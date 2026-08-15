/* =============================================================================
 * lib/include/memory.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <memory.h> — un nome vecchio di <string.h>.
 *
 * Prima che il C fosse standardizzato, memcpy e compagne stavano qui e le
 * funzioni di stringa in <string.h>. Lo standard le ha messe insieme in
 * <string.h> nel 1989, e da allora <memory.h> non serve piu' a niente —
 * ma il codice scritto prima lo include ancora, e ogni Unix se lo tiene
 * come alias per non rompere quel codice.
 *
 * Qui e' esattamente la stessa cosa: due righe perche' un sorgente di
 * trent'anni fa compili senza toccarlo.
 * ============================================================================= */

#ifndef EXOS_MEMORY_H
#define EXOS_MEMORY_H

#include "libc.h"

#endif /* EXOS_MEMORY_H */
