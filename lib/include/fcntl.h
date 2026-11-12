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

/* O_SYNC POSIX lo mette qui e EX-OS non ce l'ha: vale zero perche' un
 * programma che lo chiede non deve fallire a compilare, ma ! NON FA NIENTE.
 * O_EXCL e' vero dal kernel 0.330 (0x80, il valore di Linux): con O_CREAT,
 * un nome che c'e' gia' da' EEXIST, e controllo e creazione sono un passo
 * solo. Su un kernel piu' vecchio il bit e' ignorato (come prima lo zero).
 *
 * O_NONBLOCK sta in libc.h insieme agli altri flag di open() e vale 0x800,
 * il bit vero: da quando c'e' fcntl(F_GETFL/F_SETFL) il valore torna
 * indietro a chi lo ha messo, e uno zero avrebbe fatto sparire il flag
 * fra l'andata e il ritorno. ! Neanche lui fa qualcosa: nessuna
 * operazione di EX-OS ritorna EAGAIN, il flag si ricorda e basta. */
#define O_EXCL      0x80
#define O_SYNC      0

/* I lucchetti di fcntl (F_GETLK, F_SETLK, F_SETLKW) sono veri dal kernel 0.291
 * (voce 38; prima non escludevano nessuno): per processo e per intervallo di
 * byte, consultivi, e vanno via quando il processo chiude QUALSIASI descrittore
 * del file o esce. Nessun EDEADLK. I numeri sono quelli di Linux. */
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
