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
 * FD_* (@EXILLA-NSPR, 30 settembre 2026). ! fd_set ha 32 bit: i socket, che
 * hanno descrittori da 1024, con select non si guardano — si usa poll().
 * ============================================================================= */
#ifndef EXOS_SYS_SELECT_H
#define EXOS_SYS_SELECT_H

#include "../libc.h"

#endif /* EXOS_SYS_SELECT_H */
