/* =============================================================================
 * lib/include/pty.h
 * EX-OS -- Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <pty.h> -- openpty, forkpty, login_tty (5 November 2026). glibc's name;
 * BSD's are <util.h> and <libutil.h>, which include this one. The code is in
 * lib/libc.c, on top of SYS_PTY_APRI (both ends at once, no /dev/ptmx).
 * `name` gets the slave's name, /dev/pts/N (kernel 0.318). posix_openpt,
 * grantpt, unlockpt and ptsname are in <stdlib.h>.
 * ============================================================================= */
#ifndef EXOS_PTY_H
#define EXOS_PTY_H

#include <termios.h>
#include <sys/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

int openpty(int *master, int *slave, char *name, const struct termios *t,
            const struct winsize *w);
int forkpty(int *master, char *name, const struct termios *t,
            const struct winsize *w);
int login_tty(int fd);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_PTY_H */
