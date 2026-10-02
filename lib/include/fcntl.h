/* =============================================================================
 * lib/include/fcntl.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under la GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <fcntl.h> — aprire un file.
 *
 * Facciata su libc.h, dove stanno open() e le costanti O_*.
 *
 * O_BINARY e' definito a zero: non esiste su POSIX perche' non serve — su
 * Unix e su EX-OS un file e' una sequenza di byte e basta — ma il codice
 * che nasce anche per Windows lo scrive lo stesso, e li' distingue fra
 * leggere i byte come sono e farsi tradurre i fine riga. Definirlo a zero
 * fa compilare quel codice senza cambiargli il comportamento; ometterlo
 * costringerebbe a toccare ogni sorgente che si vuole portare.
 * ============================================================================= */

#ifndef EXOS_FCNTL_H
#define EXOS_FCNTL_H

#include "libc.h"

#define O_BINARY    0
#define O_TEXT      0

/* Costanti che POSIX mette qui e che EX-OS non ha: O_EXCL e O_SYNC.
 * Valgono zero perche' un programma che le chiede non deve fallire a
 * compilare, ma ! NON FANNO NIENTE — in particolare O_EXCL non protegge
 * da una creazione concorrente.
 *
 * O_NONBLOCK sta in libc.h insieme agli altri flag di open() e vale 0x800,
 * il bit vero: da quando c'e' fcntl(F_GETFL/F_SETFL) il valore torna
 * indietro a chi lo ha messo, e uno zero avrebbe fatto sparire il flag
 * fra l'andata e il ritorno. ! Neanche lui fa qualcosa: nessuna
 * operazione di EX-OS ritorna EAGAIN, il flag si ricorda e basta. */
#define O_EXCL      0
#define O_SYNC      0

/* ! I LUCCHETTI DI fcntl (F_GETLK, F_SETLK, F_SETLKW) ESISTONO E NON ESCLUDONO
 * NESSUNO (@EXILLA-NSS, 30 settembre 2026): EX-OS non ha lucchetti sui file.
 * F_GETLK risponde sempre «libero», F_SETLK sempre «preso». SQLite (la base
 * dei certificati di NSS) li chiede; due processi sullo stesso database non
 * sono protetti l'uno dall'altro — lo stesso limite di flock in <sys/file.h>.
 * I numeri sono quelli di Linux. */
#define F_GETLK     5
#define F_SETLK     6
#define F_SETLKW    7
#define F_RDLCK     0
#define F_WRLCK     1
#define F_UNLCK     2

struct flock {
    short l_type;
    short l_whence;
    off_t l_start;
    off_t l_len;
    pid_t l_pid;
};

#endif /* EXOS_FCNTL_H */
