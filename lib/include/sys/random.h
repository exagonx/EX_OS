/* =============================================================================
 * lib/include/sys/random.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/random.h> — where Linux declares getrandom() and getentropy() (APR
 * looks for it): a front on libc.h, where both are. EX-OS has no
 * /dev/urandom: this is the way to unpredictable bytes. The flags of Linux
 * are accepted and change nothing (the kernel answers EAGAIN until it has
 * gathered enough, see kernel/arch/x86/entropia.c).
 * ============================================================================= */
#ifndef EXOS_SYS_RANDOM_H
#define EXOS_SYS_RANDOM_H

#include "../libc.h"

#ifndef GRND_NONBLOCK
#define GRND_NONBLOCK   0x0001
#define GRND_RANDOM     0x0002
#define GRND_INSECURE   0x0004
#endif

#endif /* EXOS_SYS_RANDOM_H */
