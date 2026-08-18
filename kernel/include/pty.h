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

#define PTY_MAX         4       /* quante coppie insieme */
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

/* Crea una coppia. Rende 0 e riempie i due handle, o un errno negativo. */
int32_t pty_apri(int *master, int *slave);

/* Le quattro strade. `h` e' l'handle reso da pty_apri. */
int32_t pty_leggi_master(int h, void *buf, uint32_t n);
int32_t pty_scrivi_master(int h, const void *buf, uint32_t n);
int32_t pty_leggi_slave(int h, void *buf, uint32_t n);
int32_t pty_scrivi_slave(int h, const void *buf, uint32_t n);

/* Chiusura di un'estremita'. `master` dice quale. */
void    pty_chiudi(int h, int master);
void    pty_apri_riferimento(int h, int master);

/* Controllo: primo piano, modo, misura. Rende 0 o il valore chiesto. */
int32_t pty_ctl(int h, uint32_t cmd, uint32_t arg);

/* Quanto c'e' da leggere da una parte e dall'altra: serve a poll(). */
int     pty_attesa_registra_locked(int h, int master, uint32_t pid);
void    pty_attesa_togli_locked(int h, int master, uint32_t pid);

int     pty_pronto_master(int h);
int     pty_pronto_slave(int h);

#endif /* PTY_H */
