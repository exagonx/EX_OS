/* =============================================================================
 * lib/include/math.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under la GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <math.h> — quel poco che c'e'.
 *
 * ⚠️ QUESTA NON E' UNA LIBM. Ci sono ldexp, frexp e le costanti; non ci sono
 * sqrt, sin, cos, log, exp, pow ne' il resto. La dichiarazione di
 * funzioni inesistenti sarebbe peggio dell'assenza: il programma
 * compilerebbe e fallirebbe al link, a valle di tutto il lavoro, con un
 * "undefined reference" che non spiega niente. Cosi' invece l'errore
 * arriva sulla riga che le usa.
 *
 * Il giorno che servira' una libm vera, il posto e' questo — e la parte
 * difficile non e' scrivere le formule, e' l'arrotondamento: una sqrt
 * "quasi giusta" e' peggio di nessuna sqrt, perche' sbaglia in silenzio.
 *
 * L'hardware invece c'e': dal 2026-08-02 il kernel inizializza la FPU e ne
 * salva lo stato a ogni cambio di contesto (kernel/include/fpu.h), quindi
 * l'aritmetica in virgola mobile nei programmi ring3 funziona — sono le
 * FUNZIONI a mancare, non i conti.
 * ============================================================================= */

#ifndef EXOS_MATH_H
#define EXOS_MATH_H

#include "libc.h"

/* Costanti dello standard, nella precisione dell'x87 (che internamente
 * lavora a 80 bit anche quando i valori arrivano e partono a 64). */
#define M_E         2.7182818284590452354
#define M_LOG2E     1.4426950408889634074
#define M_LOG10E    0.43429448190325182765
#define M_LN2       0.69314718055994530942
#define M_LN10      2.30258509299404568402
#define M_PI        3.14159265358979323846
#define M_PI_2      1.57079632679489661923
#define M_PI_4      0.78539816339744830962
#define M_1_PI      0.31830988618379067154
#define M_2_PI      0.63661977236758134308
#define M_SQRT2     1.41421356237309504880
#define M_SQRT1_2   0.70710678118654752440

/* Le due sole funzioni di math.h che EX-OS implementa, e non e' un caso
 * che siano una l'inversa dell'altra: nessuna delle due CALCOLA qualcosa —
 * scompongono e ricompongono un numero in virgola mobile, che e' cio' di
 * cui ha bisogno chi lo converte da un formato a un altro. ldexp la chiama
 * TCC per i letterali esadecimali del C99 (0x1.8p3); frexp la chiede
 * floatformat.c di libiberty, che converte fra i formati IEEE dei diversi
 * bersagli di binutils. */
double ldexp(double x, int e);
double frexp(double x, int *e);

#endif /* EXOS_MATH_H */
