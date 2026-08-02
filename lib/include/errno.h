/* =============================================================================
 * lib/include/errno.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <errno.h> — codice dell'ultimo errore.
 *
 * PERCHE' QUESTO FILE E' UNA FACCIATA. Tutte le dichiarazioni della libc
 * di EX-OS stanno in un header solo, libc.h, e i programmi di /bin lo
 * includono cosi' com'e'. Questi header con i nomi standard esistono
 * perche' il codice scritto per un sistema POSIX — a cominciare da un
 * compilatore da portare — scrive #include <errno.h> e si aspetta che
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

#ifndef EXOS_ERRNO_H
#define EXOS_ERRNO_H

#include "libc.h"

#endif /* EXOS_ERRNO_H */
