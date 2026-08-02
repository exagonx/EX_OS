/* =============================================================================
 * lib/include/time.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under la GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <time.h> — data e ora.
 *
 * Facciata su libc.h, dove stanno time_t, struct tm, time(), gmtime(),
 * localtime() e la spiegazione per esteso del punto che conta:
 *
 *   ⚠️ EX-OS NON HA FUSI ORARI. L'orologio CMOS e' ora locale e il sistema
 *   non sa dove si trova, quindi localtime() e gmtime() fanno la stessa
 *   identica cosa. I secondi di time() misurano bene gli INTERVALLI e
 *   datano bene i file; non sono un istante confrontabile con quello di
 *   un'altra macchina.
 *
 * Non ci sono strftime, mktime, ctime, asctime ne' clock(): come in
 * <math.h>, dichiarare cio' che non esiste sposterebbe l'errore dalla riga
 * che le usa al link, dove non spiega niente.
 * ============================================================================= */

#ifndef EXOS_TIME_H
#define EXOS_TIME_H

#include "libc.h"

/* CLOCKS_PER_SEC c'e' anche senza clock(): e' il tick del PIT, ed e' il
 * numero con cui si convertono i millisecondi di uptime_ms(). */
#define CLOCKS_PER_SEC  100

#endif /* EXOS_TIME_H */
