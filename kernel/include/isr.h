/* =============================================================================
 * kernel/include/isr.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef ISR_H
#define ISR_H

#include "kernel.h"
#include "idt.h"

typedef void (*isr_handler_fn)(InterruptFrame *frame);

void isr_install(void);
void isr_register_handler(uint8_t num, isr_handler_fn handler);
void irq_register_handler(uint8_t irq, isr_handler_fn handler);

/* Handler C chiamati dagli stub ASM */
void isr_handler(InterruptFrame *frame);
void irq_handler(InterruptFrame *frame);

/* Binding IRQ hardware <-> driver ring3 (vedi ipc.h per la consegna) */
int32_t irq_bind_process(uint8_t irq, uint32_t pid);
/* Riapre la linea dopo che il driver ha servito l'interrupt. Obbligatoria:
 * il dispatcher maschera l'IRQ prima di notificare il driver ring3 (vedi
 * il commento in isr.c — senza, un IRQ a livello come quelli PCI blocca
 * la macchina). */
int32_t irq_done_process(uint8_t irq, uint32_t pid);
void    irq_unbind_process(uint32_t pid);

#endif /* ISR_H */
