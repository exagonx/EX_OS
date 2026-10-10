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
/* =============================================================================
 * LO STATO SALVATO DI CHI E' STATO INTERROTTO, E COME LO SI LEGGE (@EXOS-64)
 *
 * La struttura e' del processore: a 32 bit ha eax, ebx...; a 64 ha rax, rbx,
 * e otto registri in piu'. Ma il codice COMUNE - le chiamate di sistema, i
 * segnali, la creazione dei processi - non vuole sapere come si chiamano i
 * registri: vuole «il primo argomento», «dove riprende», «la sua pila».
 *
 * ! QUINDI IL CODICE COMUNE NON SCRIVE frame->ebx: SCRIVE FR_A1(frame). Le
 * macro qui sotto dicono, per ogni macchina, in quale registro sta che cosa.
 * La convenzione delle chiamate di sistema e' la stessa nelle due: il numero
 * in A (eax/rax), gli argomenti in B, C, D, SI, DI, il risultato in A.
 *
 * A 32 bit le macro sono i campi di prima, lettera per lettera: il kernel
 * compilato e' lo stesso (verificato confrontando le istruzioni di ogni
 * oggetto prima e dopo, 10 ottobre 2026).
 * ============================================================================= */
#if defined(__x86_64__)

typedef struct PACKED {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t int_no;
    uint64_t err_code;
    /* Messi dal processore: a 64 bit SEMPRE tutti e cinque, anche quando
     * l'interrupt arriva in ring 0. */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t user_rsp;
    uint64_t user_ss;
} InterruptFrame;

#define FR_NUM(f)    ((f)->rax)
#define FR_RET(f)    ((f)->rax)
#define FR_A1(f)     ((f)->rbx)
#define FR_A2(f)     ((f)->rcx)
#define FR_A3(f)     ((f)->rdx)
#define FR_A4(f)     ((f)->rsi)
#define FR_A5(f)     ((f)->rdi)
#define FR_BP(f)     ((f)->rbp)
#define FR_IP(f)     ((f)->rip)
#define FR_SP(f)     ((f)->user_rsp)
#define FR_FLAGS(f)  ((f)->rflags)

#else

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

#define FR_NUM(f)    ((f)->eax)
#define FR_RET(f)    ((f)->eax)
#define FR_A1(f)     ((f)->ebx)
#define FR_A2(f)     ((f)->ecx)
#define FR_A3(f)     ((f)->edx)
#define FR_A4(f)     ((f)->esi)
#define FR_A5(f)     ((f)->edi)
#define FR_BP(f)     ((f)->ebp)
#define FR_IP(f)     ((f)->eip)
#define FR_SP(f)     ((f)->user_esp)
#define FR_FLAGS(f)  ((f)->eflags)

#endif

#endif /* IDT_H */
