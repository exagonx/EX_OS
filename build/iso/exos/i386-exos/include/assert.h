/* =============================================================================
 * lib/include/assert.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under la GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <assert.h> — la condizione che non puo' essere falsa.
 *
 * assert() NON e' una facciata su libc.h come <string.h> e <stdio.h>,
 * perche' non e' una funzione: e' una macro, e deve espandersi qui per
 * poter catturare il file, la riga e il TESTO della condizione. Quel
 * testo e' l'unica cosa che rende utile un assert fallito: "assert
 * fallita" da solo non dice niente, "n > 0 fallita a tccpp.c:214" dice
 * tutto.
 *
 * NDEBUG e' la convenzione dello standard e va rispettata: definirla
 * prima dell'inclusione toglie di mezzo i controlli, e la macro deve
 * comunque espandersi a un'espressione valida — da qui `((void)0)`, che
 * si puo' scrivere dove il codice si aspetta un'istruzione.
 *
 * Il vantaggio di assert su un if scritto a mano e' che si toglie. Il
 * prezzo e' che si toglie: chi ci mette dentro un effetto collaterale
 * (assert(fai_qualcosa() == 0)) si ritrova un programma che con NDEBUG
 * non fa piu' quella cosa.
 * ============================================================================= */

#ifndef EXOS_ASSERT_H
#define EXOS_ASSERT_H

#include "libc.h"

/* Non c'e' guardia di inclusione singola sul CORPO, ed e' voluto: lo
 * standard vuole che includere di nuovo <assert.h> dopo aver cambiato
 * NDEBUG cambi il significato di assert(). Da qui l'#undef. */
#undef assert

#ifdef NDEBUG
# define assert(cond)   ((void)0)
#else
# define assert(cond) \
    ((cond) ? (void)0 : _assert_fallita(#cond, __FILE__, __LINE__))
#endif

/* Stampa su stderr e chiude il processo. Non ritorna: dichiararlo permette
 * al compilatore di sapere che dopo un assert fallita il codice non
 * prosegue, e di non lamentarsi di variabili "usate non inizializzate" nel
 * ramo che non esiste. */
void _assert_fallita(const char *cond, const char *file, int riga)
    __attribute__((noreturn));

#endif /* EXOS_ASSERT_H */
