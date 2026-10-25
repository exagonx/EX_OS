/* =============================================================================
 * lib/include/sys/ioctl.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/ioctl.h> — facciata su libc.h, dove stanno ioctl() e, fra i socket,
 * FIONBIO e FIONREAD (@EXILLA-NSPR, 30 settembre 2026).
 * ============================================================================= */
#ifndef EXOS_SYS_IOCTL_H
#define EXOS_SYS_IOCTL_H

#include "../libc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The terminal's requests, Linux's numbers (kernel 0.279): the kernel
 * answers them on the console and on a pty (kernel/syscall/syscall_impl.c),
 * ENOTTY elsewhere. TCGETS/TCSETS* take a struct termios (<termios.h>),
 * TIOCSWINSZ is a no-op on the console. */
#ifndef TCGETS
#define TCGETS     0x5401
#define TCSETS     0x5402
#define TCSETSW    0x5403
#define TCSETSF    0x5404
#define TCFLSH     0x540B
#endif
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_IOCTL_H */
