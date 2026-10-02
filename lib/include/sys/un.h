/* =============================================================================
 * lib/include/sys/un.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <sys/un.h> — struct sockaddr_un, che sta in libc.h con gli altri socket.
 * ! La struttura c'e', i socket di dominio Unix no: socket(AF_UNIX, ...)
 * risponde EAFNOSUPPORT.
 * ============================================================================= */

#ifndef EXOS_SYS_UN_H
#define EXOS_SYS_UN_H

#include "../libc.h"

#endif /* EXOS_SYS_UN_H */
