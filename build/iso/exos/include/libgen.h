/* =============================================================================
 * lib/include/libgen.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <libgen.h> — basename e dirname nella forma di POSIX (@EXILLA-JS).
 *
 * ! POSSONO SCRIVERE NELLA STRINGA che ricevono (toglie le «/» in fondo), come
 * permette POSIX; con NULL o "" rendono ".". Il risultato puo' essere un
 * buffer statico: non rientranti, come ovunque.
 * ============================================================================= */
#ifndef EXOS_LIBGEN_H
#define EXOS_LIBGEN_H

#include "libc.h"


#ifdef __cplusplus
extern "C" {
#endif
char *basename(char *percorso);
char *dirname(char *percorso);
#ifdef __cplusplus
}
#endif

#endif /* EXOS_LIBGEN_H */
