/* =============================================================================
 * lib/include/sys/syscall.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/syscall.h> — la chiamata di sistema per numero, alla Linux (@EXILLA-JS).
 *
 * ! I NUMERI SONO QUELLI DI LINUX i386, e syscall() ne capisce pochissimi: il
 * codice di terzi la usa quasi solo per chiedere l'id del filo (SYS_gettid), e
 * su EX-OS quell'id e' il pid del filo. Tutto il resto rende -1 con ENOSYS —
 * i numeri delle chiamate di EX-OS sono altri (kernel/include/syscall.h).
 * ============================================================================= */
#ifndef EXOS_SYS_SYSCALL_H
#define EXOS_SYS_SYSCALL_H

#include "../libc.h"

#define SYS_gettid      224
#define __NR_gettid     SYS_gettid


#ifdef __cplusplus
extern "C" {
#endif
long syscall(long numero, ...);
#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_SYSCALL_H */
