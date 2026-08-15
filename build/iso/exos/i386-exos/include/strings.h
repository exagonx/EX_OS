/* =============================================================================
 * lib/include/strings.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <strings.h> — il confronto che ignora maiuscole e minuscole.
 *
 * ! NON E' <string.h> CON UNA S IN PIU', per quanto lo sembri. Sono due
 * header diversi di due standard diversi: <string.h> e' del C e ci stanno
 * strlen, strcpy, memcpy; <strings.h> e' di POSIX e ci stanno strcasecmp e
 * strncasecmp, che nel C standard non esistono. Chi include il primo
 * aspettandosi le seconde non le trova, ed e' il genere di sorpresa che
 * costa mezz'ora.
 *
 * Come gli altri header con i nomi standard, e' una facciata su libc.h:
 * le dichiarazioni stanno tutte li', in un posto solo.
 * ============================================================================= */

#ifndef EXOS_STRINGS_H
#define EXOS_STRINGS_H

#include "libc.h"

#endif /* EXOS_STRINGS_H */
