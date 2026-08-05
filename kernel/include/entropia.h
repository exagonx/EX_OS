/* =============================================================================
 * kernel/include/entropia.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Raccolta di entropia. Il perche' — e soprattutto perche' questo NON e'
 * un generatore di numeri casuali — sta in kernel/arch/x86/entropia.c.
 * ============================================================================= */

#ifndef ENTROPIA_H
#define ENTROPIA_H

#include "kernel.h"

void entropia_init(void);

/* Da chiamare all'arrivo di un interrupt che valga come sorgente.
 * IRQ0 (il timer) viene ignorato dalla funzione stessa. */
void entropia_evento(uint8_t irq);

/* Scrive fino a `n` byte in `dst`. Ritorna quanti ne ha scritti, o
 * -11 (-EAGAIN) se non ne ha abbastanza: NON riempie con quello che ha. */
int  entropia_preleva(uint8_t *dst, uint32_t n);

void entropia_stato(uint32_t *bit, uint32_t *eventi, int *rdrand);

#endif /* ENTROPIA_H */
