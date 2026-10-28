/* =============================================================================
 * kernel/include/pty.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Pseudo-terminali — una pipe CON una disciplina di linea attaccata
 *
 * ! E' IL PEZZO CHE SI SOTTOVALUTA SEMPRE, e la coda lo diceva da giorni. Una
 * shell su una pipe nuda e' esattamente la shell dentro una finestra prima di
 * oggi: niente eco, niente Backspace, niente Ctrl+C, niente misura dello
 * schermo. Quelle cose non le fa la shell e non le fa il terminale — le fa la
 * DISCIPLINA DI LINEA, e una pipe non ne ha. Un pty e' una pipe con quella
 * disciplina in mezzo, ed e' cio' che trasforma un tubo in una sessione.
 *
 * -----------------------------------------------------------------------------
 * LE DUE ESTREMITA' NON SONO SIMMETRICHE, e non e' un dettaglio
 *
 *     MASTER   lo tiene chi FA il terminale: il terminale in finestra oggi,
 *              un server telnet o ssh domani. Ci scrive i tasti battuti e ci
 *              legge cio' che il programma stampa.
 *
 *     SLAVE    lo eredita la shell come stdin/stdout/stderr, e per lei e' un
 *              terminale come un altro: non sa di essere dentro un pty.
 *
 * ! I TASTI NON ARRIVANO ALLO SLAVE COSI' COME SONO. Passano dalla disciplina,
 * che li accumula in una riga, li fa vedere (eco), lascia cancellare con
 * Backspace, e consegna solo quando si batte Invio. Senza quel passaggio, chi
 * legge riceve un carattere per volta e il Backspace e' un byte come un altro
 * — cioe' esattamente quello che si vedeva nel terminale in finestra.
 *
 * ! E L'ECO VA VERSO IL MASTER, NON VERSO LO SCHERMO. Il kernel non sa che
 * aspetto abbia il terminale: mette i caratteri nel tubo di ritorno, e chi lo
 * legge li disegna come vuole. E' la stessa ragione per cui il server a
 * finestre non disegna il contenuto delle finestre.
 * ============================================================================= */
#ifndef PTY_H
#define PTY_H

#include "kernel.h"

/* PTY_MAX (4 pairs) is gone since kernel 0.282: see pty.c */
#define PTY_DIM         1024    /* buffer, per direzione */
#define PTY_RIGA_MAX    256     /* la riga in costruzione */

/* I modi della disciplina. Il canonico e' quello di sempre: si accumula una
 * riga e si consegna all'Invio. In modo grezzo ogni byte passa subito — serve
 * a un editor a schermo pieno, che i tasti li vuole mentre si premono. */
#define PTY_CANONICO    0x0001
#define PTY_ECO         0x0002

/* I comandi di pty_ctl(). */
#define PTY_CTL_FG      1       /* arg = pid da interrompere con Ctrl+C */
#define PTY_CTL_MODO    2       /* arg = PTY_CANONICO | PTY_ECO             */
#define PTY_CTL_MISURA  3       /* arg = (righe << 16) | colonne            */
#define PTY_CTL_LEGGI_MISURA 4  /* rende (righe << 16) | colonne            */

/* -----------------------------------------------------------------------------
 * TERMIOS (25 October 2026, prerequisite of ncurses and of the line editors)
 *
 * ! THE LAYOUT IS THE ONE OF lib/include/termios.h, and the flag values are
 * Linux's: four 32-bit flag words and NCCS control characters. tcgetattr and
 * tcsetattr in the libc are ioctl(TCGETS/TCSETS) with this structure, copied
 * as it is. PTY_CTL_MODO stays, as the short way to say ICANON and ECHO.
 *
 * What the discipline honours: ICRNL INLCR IGNCR ISTRIP in input, OPOST+ONLCR
 * in output (and on the echo), ICANON ECHO ECHOE ECHOK ISIG NOFLSH, and the
 * characters VINTR VEOF VERASE VKILL VMIN VTIME. The rest is kept and given
 * back, so a program that saves and restores the modes gets what it set.
 * --------------------------------------------------------------------------- */
#define PTY_NCCS        20
typedef struct {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t  c_cc[PTY_NCCS];
} PtyTermios;

#define PTY_TCGETS      0x5401
#define PTY_TCSETS      0x5402
#define PTY_TCSETSW     0x5403
#define PTY_TCSETSF     0x5404
#define PTY_TCFLSH      0x540B
#define PTY_TIOCGWINSZ  0x5413
#define PTY_TIOCSWINSZ  0x5414

#define T_ISTRIP  0x0020
#define T_INLCR   0x0040
#define T_IGNCR   0x0080
#define T_ICRNL   0x0100
#define T_OPOST   0x0001
#define T_ONLCR   0x0004
#define T_ISIG    0x0001
#define T_ICANON  0x0002
#define T_ECHO    0x0008
#define T_ECHOE   0x0010
#define T_ECHOK   0x0020
#define T_NOFLSH  0x0080
#define T_VINTR   0
#define T_VERASE  2
#define T_VKILL   3
#define T_VEOF    4
#define T_VTIME   5
#define T_VMIN    6

/* ioctl on either end. `buf` is already checked by the caller. */
int32_t pty_ioctl(int h, uint32_t req, uintptr_t arg);

/* Crea una coppia. Rende 0 e riempie i due handle, o un errno negativo. */
int32_t pty_apri(int *master, int *slave);

/* Le quattro strade. `h` e' l'handle reso da pty_apri. */
int32_t pty_leggi_master(int h, void *buf, uint32_t n);
int32_t pty_scrivi_master(int h, const void *buf, uint32_t n);
int32_t pty_leggi_slave(int h, void *buf, uint32_t n);
int32_t pty_scrivi_slave(int h, const void *buf, uint32_t n);

/* Chiusura di un'estremita'. `master` dice quale. */
void    pty_chiudi(int h, int master);

/* Come pty_chiudi, ma il chiamante ha gia' `cli`: la usa proc_chiudi_fd()
 * quando un processo muore. Vedi il commento in kernel/ipc/pty.c. */
void    pty_chiudi_locked(int h, int master);
void    pty_apri_riferimento(int h, int master);

/* Controllo: primo piano, modo, misura. Rende 0 o il valore chiesto. */
int32_t pty_ctl(int h, uint32_t cmd, uint32_t arg);

/* Quanto c'e' da leggere da una parte e dall'altra: serve a poll(). */
int     pty_attesa_registra_locked(int h, int master, uint32_t pid);
void    pty_attesa_togli_locked(int h, int master, uint32_t pid);

int     pty_pronto_master(int h);
int     pty_pronto_slave(int h);

#endif /* PTY_H */
