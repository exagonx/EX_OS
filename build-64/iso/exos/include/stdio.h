/* =============================================================================
 * lib/include/stdio.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <stdio.h> — flussi bufferizzati, printf e famiglia.
 *
 * PERCHE' QUESTO FILE E' UNA FACCIATA. Tutte le dichiarazioni della libc
 * di EX-OS stanno in un header solo, libc.h, e i programmi di /bin lo
 * includono cosi' com'e'. Questi header con i nomi standard esistono
 * perche' il codice scritto per un sistema POSIX — a cominciare da un
 * compilatore da portare — scrive #include <stdio.h> e si aspetta che
 * funzioni.
 *
 * Sono volutamente sottili: rimandano a libc.h invece di dichiarare un
 * sottoinsieme proprio. Due elenchi della stessa funzione sono due elenchi
 * che prima o poi divergono, e la divergenza si manifesta come un
 * prototipo sbagliato — cioe' argomenti passati storti, non un errore di
 * compilazione.
 *
 * Quando la libc verra' spezzata in un archivio vero (libc.a, un file per
 * area), questi header prenderanno il proprio contenuto e libc.h restera'
 * per compatibilita'. Fino ad allora, la fonte unica e' una sola.
 * ============================================================================= */

#ifndef EXOS_STDIO_H
#define EXOS_STDIO_H

#include "libc.h"

/* =============================================================================
 * ! _STDIO_H NON E' LA NOSTRA GUARDIA: E' UNA BANDIERA PER GLI ALTRI
 *
 * La guardia di questo file e' EXOS_STDIO_H, qui sopra. `_STDIO_H` e' il
 * nome che usa la glibc, e c'e' perche' del codice di terzi lo ANNUSA per
 * sapere se <stdio.h> e' gia' stato incluso — non potendo dipendere da un
 * nome nostro che non conosce.
 *
 * Il caso reale: gmp.h dichiara le funzioni che prendono un `FILE *`
 * (mpz_inp_str, mpz_out_str…) solo se trova una di quindici macro note,
 * una per libc storica — _STDIO_H per glibc, __DEFINED_FILE per musl,
 * _FILE_DEFINED per Microsoft… Senza, le omette in silenzio, e i suoi
 * stessi sorgenti non compilano:
 *
 *     gmp.h:884: error: implicit declaration of function '__gmpz_inp_str'
 *
 * cioe' una libreria che non compila se stessa perche' non riconosce la
 * libc sotto. Dichiararsi con il nome della glibc e' la cosa onesta: la
 * nostra <stdio.h> quel contratto lo rispetta davvero — FILE, fopen,
 * fprintf e il resto ci sono.
 * ============================================================================= */
#ifndef _STDIO_H
#define _STDIO_H 1
#endif

#endif /* EXOS_STDIO_H */
