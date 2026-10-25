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
 * ! FROM KERNEL 0.279 (25 October 2026) THE MODES ARE REAL. A pseudo-terminal
 * (telnet, sshd, the ExWin terminal) and the text console keep a termios each:
 * tcgetattr/tcsetattr are ioctl(TCGETS/TCSETS) on the descriptor. Honoured:
 * ICRNL INLCR IGNCR ISTRIP, OPOST ONLCR, ICANON ECHO ECHOE ECHOK ISIG NOFLSH,
 * VINTR VEOF VERASE VKILL VMIN VTIME. The other flags are stored and given
 * back as they were set. Values and layout are Linux's (the kernel's copy:
 * PtyTermios in kernel/include/pty.h). There are no signals for Ctrl+\ and
 * Ctrl+Z yet; Ctrl+C with ISIG interrupts the foreground program.
 *
 * Before 0.279 tcgetattr and tcsetattr said ENOTTY, and the NSS password
 * prompt treated that as «not a terminal». Solo libc.a.
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

/* c_cc */
#define VINTR     0
#define VQUIT     1
#define VERASE    2
#define VKILL     3
#define VEOF      4
#define VTIME     5
#define VMIN      6
#define VSWTC     7
#define VSTART    8
#define VSTOP     9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

/* c_iflag */
#define IGNBRK   0x0001
#define BRKINT   0x0002
#define IGNPAR   0x0004
#define PARMRK   0x0008
#define INPCK    0x0010
#define ISTRIP   0x0020
#define INLCR    0x0040
#define IGNCR    0x0080
#define ICRNL    0x0100
#define IUCLC    0x0200
#define IXON     0x0400
#define IXANY    0x0800
#define IXOFF    0x1000
#define IMAXBEL  0x2000
#define IUTF8    0x4000

/* c_oflag */
#define OPOST    0x0001
#define OLCUC    0x0002
#define ONLCR    0x0004
#define OCRNL    0x0008
#define ONOCR    0x0010
#define ONLRET   0x0020
#define OFILL    0x0040
#define OFDEL    0x0080
#define TABDLY   0x1800
#define TAB0     0x0000
#define TAB3     0x1800
#define XTABS    TAB3

/* c_cflag */
#define CBAUD    0x100F
#define B0       0x0000
#define B50      0x0001
#define B75      0x0002
#define B110     0x0003
#define B134     0x0004
#define B150     0x0005
#define B200     0x0006
#define B300     0x0007
#define B600     0x0008
#define B1200    0x0009
#define B1800    0x000A
#define B2400    0x000B
#define B4800    0x000C
#define B9600    0x000D
#define B19200   0x000E
#define B38400   0x000F
#define B57600   0x1001
#define B115200  0x1002
#define CSIZE    0x0030
#define CS5      0x0000
#define CS6      0x0010
#define CS7      0x0020
#define CS8      0x0030
#define CSTOPB   0x0040
#define CREAD    0x0080
#define PARENB   0x0100
#define PARODD   0x0200
#define HUPCL    0x0400
#define CLOCAL   0x0800

/* c_lflag */
#define ISIG     0x0001
#define ICANON   0x0002
#define XCASE    0x0004
#define ECHO     0x0008
#define ECHOE    0x0010
#define ECHOK    0x0020
#define ECHONL   0x0040
#define NOFLSH   0x0080
#define TOSTOP   0x0100
#define ECHOCTL  0x0200
#define ECHOPRT  0x0400
#define ECHOKE   0x0800
#define FLUSHO   0x1000
#define PENDIN   0x4000
#define IEXTEN   0x8000

/* tcsetattr */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* tcflush */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

/* tcflow */
#define TCOOFF    0
#define TCOON     1
#define TCIOFF    2
#define TCION     3

#ifdef __cplusplus
extern "C" {
#endif
int     tcgetattr(int fd, struct termios *t);
int     tcsetattr(int fd, int come, const struct termios *t);
int     tcflush(int fd, int coda);
int     tcdrain(int fd);
int     tcflow(int fd, int azione);
int     tcsendbreak(int fd, int durata);
void    cfmakeraw(struct termios *t);
speed_t cfgetispeed(const struct termios *t);
speed_t cfgetospeed(const struct termios *t);
int     cfsetispeed(struct termios *t, speed_t v);
int     cfsetospeed(struct termios *t, speed_t v);
int     cfsetspeed(struct termios *t, speed_t v);
int     tcgetpgrp(int fd);
int     tcsetpgrp(int fd, int pgrp);
int     tcgetsid(int fd);
#ifdef __cplusplus
}
#endif

#endif /* EXOS_TERMIOS_H */
