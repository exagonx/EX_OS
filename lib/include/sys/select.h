/* =============================================================================
 * lib/include/sys/select.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/select.h> — facciata su libc.h, dove stanno select(), fd_set e le macro
 * FD_* (@EXILLA-NSPR, 30 settembre 2026). Dal 25 ottobre 2026 fd_set arriva a
 * FD_SETSIZE (2048): anche i socket, che hanno descrittori da 1024 a 2047.
 * ============================================================================= */
#ifndef EXOS_SYS_SELECT_H
#define EXOS_SYS_SELECT_H

#include "../libc.h"

#endif /* EXOS_SYS_SELECT_H */
