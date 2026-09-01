/* =============================================================================
 * lib/include/sys/time.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under la GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/time.h> — l'ora con i microsecondi.
 *
 * Facciata su libc.h, dove stanno struct timeval e gettimeofday().
 *
 * ! I MICROSECONDI SONO ARROTONDATI A 10 MILLISECONDI. Vengono dal
 * contatore dei tick del PIT, che batte a 100 Hz: le ultime quattro cifre
 * di tv_usec sono sempre zero. Chi misura un intervallo piu' corto di un
 * centesimo di secondo vede zero, e deve saperlo prima di scriverci sopra
 * una misura.
 * ============================================================================= */

#ifndef EXOS_SYS_TIME_H
#define EXOS_SYS_TIME_H

#include "../libc.h"

/* settimeofday() non c'e': l'orologio di EX-OS si legge e basta. */

#endif /* EXOS_SYS_TIME_H */
