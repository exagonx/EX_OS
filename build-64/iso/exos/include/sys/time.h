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

#ifdef __cplusplus
extern "C" {
#endif
/* utimes (@EXILLA-NSS): i filesystem di EX-OS non si fanno cambiare le date da
 * un programma; rende 0 se il file c'e', senza toccarle. */
int utimes(const char *percorso, const struct timeval tempi[2]);

/* Il secondo argomento di gettimeofday, nella forma BSD: EX-OS lo ignora (il
 * fuso orario non sta nel kernel), ma il codice di terzi lo dichiara. */
struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

/* L'aritmetica delle timeval, con le macro di BSD e glibc (libevent). */
#define timerisset(t)   ((t)->tv_sec || (t)->tv_usec)
#define timerclear(t)   ((t)->tv_sec = (t)->tv_usec = 0)
#define timercmp(a, b, CMP) \
    (((a)->tv_sec == (b)->tv_sec) ? ((a)->tv_usec CMP (b)->tv_usec) \
                                  : ((a)->tv_sec CMP (b)->tv_sec))
#define timeradd(a, b, r) do {                              \
        (r)->tv_sec = (a)->tv_sec + (b)->tv_sec;            \
        (r)->tv_usec = (a)->tv_usec + (b)->tv_usec;         \
        if ((r)->tv_usec >= 1000000) {                      \
            (r)->tv_sec++; (r)->tv_usec -= 1000000;         \
        }                                                   \
    } while (0)
#define timersub(a, b, r) do {                              \
        (r)->tv_sec = (a)->tv_sec - (b)->tv_sec;            \
        (r)->tv_usec = (a)->tv_usec - (b)->tv_usec;         \
        if ((r)->tv_usec < 0) {                             \
            (r)->tv_sec--; (r)->tv_usec += 1000000;         \
        }                                                   \
    } while (0)
#ifdef __cplusplus
}
#endif

/* settimeofday() non c'e': l'orologio di EX-OS si legge e basta. */

#endif /* EXOS_SYS_TIME_H */
