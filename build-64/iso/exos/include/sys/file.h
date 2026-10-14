/* =============================================================================
 * lib/include/sys/file.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/file.h> — flock (@EXILLA-NSPR, 30 settembre 2026).
 *
 * ! EX-OS NON HA LUCCHETTI SUI FILE: flock risponde 0 e non esclude nessuno
 * (vedi lib/libc.c). Solo libc.a.
 * ============================================================================= */
#ifndef EXOS_SYS_FILE_H
#define EXOS_SYS_FILE_H

#include "../libc.h"

#define LOCK_SH  1
#define LOCK_EX  2
#define LOCK_NB  4
#define LOCK_UN  8

#ifdef __cplusplus
extern "C" {
#endif
int flock(int fd, int come);
#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_FILE_H */
