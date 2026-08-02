/* =============================================================================
 * kernel/arch/x86/fpu.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Coprocessore matematico x87: rilevamento, azzeramento e salvataggio
 * dello stato. Il perche' di tutto questo sta in kernel/include/fpu.h.
 * ============================================================================= */

#include "fpu.h"
#include "kernel.h"

/* Bit di CR0 che riguardano il coprocessore. Gli altri (PG, PE...) li
 * tocca chi di dovere: qui si legge, si cambiano SOLO questi e si
 * riscrive, perche' CR0 e' un registro condiviso e sovrascriverlo per
 * intero spegnerebbe la paginazione. */
#define CR0_MP  (1u << 1)   /* Monitor coProcessor: WAIT rispetta TS */
#define CR0_EM  (1u << 2)   /* EMulation: le istruzioni x87 danno #NM */
#define CR0_TS  (1u << 3)   /* Task Switched: usato dallo switch pigro */
#define CR0_NE  (1u << 5)   /* Numeric Error: eccezioni via #MF, non IRQ13 */

static int      g_fpu_present = 0;
static uint8_t  g_stato_pulito[FPU_STATE_SIZE] ALIGNED(16);

/* =============================================================================
 * fpu_init — rileva la FPU, la azzera, prepara il modello di stato pulito
 *
 * IL RILEVAMENTO. Si azzera EM (altrimenti ogni istruzione x87 diventa
 * un'eccezione #NM: e' il modo di dire "coprocessore emulato via
 * software", che qui non abbiamo) e si esegue FNINIT seguita da FNSTSW su
 * una variabile precaricata con un valore riconoscibile. Se un
 * coprocessore c'e', FNSTSW ci scrive la parola di stato, che dopo FNINIT
 * vale zero; se non c'e', nessuno scrive e il valore riconoscibile resta.
 *
 * E' la prova classica, quella di Linux delle origini, e funziona perche'
 * FNINIT/FNSTSW sono le uniche due istruzioni x87 che NON aspettano il
 * coprocessore (la N sta per "no wait"): su una macchina senza 387 non
 * restano appese.
 *
 * NE=1 dice alla CPU di segnalare gli errori di virgola mobile come
 * eccezione #MF invece che con l'IRQ13 del PC originale. #MF ha gia' un
 * gate nell'IDT (vedi idt.c), l'IRQ13 arriverebbe a un handler che non
 * c'e'.
 * ============================================================================= */
int fpu_init(void)
{
    uint32_t cr0 = read_cr0();

    cr0 &= ~(CR0_EM | CR0_TS);
    cr0 |=  (CR0_MP | CR0_NE);
    write_cr0(cr0);

    uint16_t stato = 0x55AA;    /* valore riconoscibile: nessuna FPU lo scrive */
    __asm__ __volatile__("fninit; fnstsw %0" : "=m"(stato));

    if (stato != 0) {
        g_fpu_present = 0;
        klog(LOG_WARN, "FPU: nessun coprocessore (fnstsw=0x%04x)", stato);
        /* Senza coprocessore le istruzioni x87 vanno fatte fallire in
         * modo riconoscibile invece di produrre numeri inventati: EM=1
         * le trasforma in #NM. */
        write_cr0(read_cr0() | CR0_EM);
        return 0;
    }

    g_fpu_present = 1;

    /* Il modello: la FPU e' appena stata azzerata da FNINIT, quindi lo
     * stato che salviamo ORA e' per definizione quello pulito. */
    __asm__ __volatile__("fnsave %0" : "=m"(g_stato_pulito));
    /* FNSAVE azzera la FPU dopo aver salvato: rimettiamola come l'abbiamo
     * trovata, cosi' il kernel non lascia il coprocessore in uno stato
     * diverso da quello che il modello descrive. */
    __asm__ __volatile__("frstor %0" : : "m"(g_stato_pulito));

    klog(LOG_INFO, "FPU: x87 presente e inizializzata (stato %u byte per processo)",
         (unsigned)FPU_STATE_SIZE);
    return 1;
}

int fpu_present(void)
{
    return g_fpu_present;
}

/* Copia a byte come nel resto del kernel, che una memcpy non ce l'ha:
 * la libc sta in ring3, e questi 108 byte non giustificano una libreria. */
void fpu_init_state(void *dst)
{
    if (!dst) return;

    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = g_stato_pulito;

    for (uint32_t i = 0; i < FPU_STATE_SIZE; i++) {
        /* Senza coprocessore lo stato non verra' mai ne' salvato ne'
         * ripristinato, ma azzerarlo lo stesso evita che un dump del PCB
         * mostri byte casuali. */
        d[i] = g_fpu_present ? s[i] : 0;
    }
}

void fpu_save(void *dst)
{
    if (!g_fpu_present || !dst) return;
    __asm__ __volatile__("fnsave (%0)" : : "r"(dst) : "memory");
}

void fpu_restore(const void *src)
{
    if (!g_fpu_present || !src) return;
    __asm__ __volatile__("frstor (%0)" : : "r"(src) : "memory");
}
