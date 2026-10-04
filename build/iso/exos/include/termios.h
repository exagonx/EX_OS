/* =============================================================================
 * lib/include/termios.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <termios.h> — il terminale (@EXILLA-NSS, 30 settembre 2026).
 *
 * ! LA CONSOLE DI EX-OS NON E' UN TERMINALE POSIX: tcgetattr e tcsetattr
 * rendono -1 con ENOTTY, e chi le usa (la richiesta di password di NSS) tratta
 * il caso come «non e' un terminale». Le strutture ci sono perche' il codice
 * le nomina. Solo libc.a.
 * ============================================================================= */
#ifndef EXOS_TERMIOS_H
#define EXOS_TERMIOS_H

#include "libc.h"

typedef unsigned int  tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int  speed_t;
#define NCCS 20

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[NCCS];
};

#define ECHO     0x0008
#define ECHONL   0x0040
#define ICANON   0x0002
#define ISIG     0x0001
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define VMIN      6
#define VTIME     5

#ifdef __cplusplus
extern "C" {
#endif
int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int come, const struct termios *t);
#ifdef __cplusplus
}
#endif

#endif /* EXOS_TERMIOS_H */
