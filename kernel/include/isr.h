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
int32_t irq_unbind_uno(uint8_t irq, uint32_t pid);

/* Il PID del primo proprietario di questa linea, 0 se non la vuole nessuno.
 * Serve a una diagnostica: leggere il buffer di un dispositivo mentre il suo
 * driver lo sta leggendo vuol dire rubarsi i byte a vicenda, cioe' misurare
 * male proprio la cosa da misurare. */
uint32_t irq_proprietario(uint8_t irq);

/* Il gestore kernel registrato su questa linea, NULL se non ce n'e'. Serve a
 * chi deve prendersi una linea per un momento e poi RIMETTERE quello che
 * c'era: registrare NULL alla fine non e' la stessa cosa — spegnerebbe il
 * gestore del TTY e la tastiera resterebbe morta dopo la prova. */
isr_handler_fn irq_handler_get(uint8_t irq);

/* Quante notifiche IPC sono state consegnate su questa linea da quando la
 * macchina e' accesa. Vedi il commento accanto a irq_notifiche in isr.c. */
uint32_t irq_notifiche_di(uint8_t irq);
void    irq_unbind_process(uint32_t pid);

#endif /* ISR_H */
