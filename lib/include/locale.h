/* =============================================================================
 * lib/include/locale.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <locale.h> — setlocale: esiste solo la locale "C"
 *
 * Facciata sottile su libc.h, come gli altri header con i nomi standard:
 * due elenchi della stessa funzione divergono, e la divergenza si
 * manifesta come prototipo sbagliato invece che come errore di
 * compilazione. Vedi lib/include/errno.h per il ragionamento esteso.
 * ============================================================================= */

#ifndef EXOS_LOCALE_H
#define EXOS_LOCALE_H

#include "libc.h"

#endif /* EXOS_LOCALE_H */
