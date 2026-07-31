/* =============================================================================
 * kernel/include/idt.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef IDT_H
#define IDT_H

#include "kernel.h"

void idt_install(void);
void idt_set_gate(uint8_t num, uint32_t handler, uint16_t sel, uint8_t flags);
void pic_send_eoi(uint8_t irq);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);

/* Struttura frame passata agli ISR handler da isr_stubs.asm.
 *
 * L'ordine dei campi DEVE corrispondere esattamente all'ordine reale di
 * push in isr_common_stub (letto dal basso, cioe' dall'ultimo push al
 * primo, dato che la struct viene letta a partire da ESP dopo tutti i
 * push):
 *   1. (ultimo push, quindi primo campo)  DS originale (salvato in EAX,
 *      poi pushato, DOPO pushad — non e' il nono campo come si potrebbe
 *      pensare guardando l'ordine del codice asm, perche' viene pushato
 *      per ultimo)
 *   2-9. pushad, in ordine INVERSO rispetto a come la CPU li salva
 *      (pushad salva EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI in quest'ordine
 *      cronologico, quindi EDI e' l'ultimo salvato da pushad ed e' il
 *      campo immediatamente successivo a DS quando si legge dal basso)
 *   10. int_no (pushato dallo stub prima di pushad)
 *   11. err_code (pushato dallo stub, ancora prima di int_no)
 *   12-14. eip, cs, eflags (pushati automaticamente dalla CPU)
 *   15-16. user_esp, user_ss (solo se transizione da ring3) */
typedef struct PACKED {
    uint32_t ds;                                    /* salvato per ultimo */
    uint32_t edi, esi, ebp, esp_dummy;
    uint32_t ebx, edx, ecx, eax;
    uint32_t int_no;
    uint32_t err_code;
    /* Pushati automaticamente dalla CPU al momento dell'interrupt */
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
    /* Solo se transizione ring3→ring0 */
    uint32_t user_esp;
    uint32_t user_ss;
} InterruptFrame;

#endif /* IDT_H */
