/* =============================================================================
 * kernel/include/fpu.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Coprocessore matematico x87.
 *
 * PERCHE' ADESSO. Fino ad agosto 2026 nessun programma di EX-OS usava la
 * virgola mobile, quindi la FPU non veniva nemmeno inizializzata (lo
 * dichiarava version.h). Un compilatore la usa: TCC tiene le costanti
 * numeriche in `long double`, cioe' nei registri a 80 bit dell'x87, e
 * anche solo per compilare `float x = 1.5;` ci passa attraverso.
 *
 * IL GUASTO CHE QUESTO FILE EVITA. Senza salvataggio dello stato, due
 * processi che usano la FPU se la calpestano a vicenda: lo scheduler
 * preempita a meta' di un calcolo, l'altro processo carica i propri
 * valori nei registri x87, e al ritorno il primo trova numeri di
 * qualcun altro. Non e' un crash — e' un risultato SBAGLIATO in
 * silenzio, cioe' il tipo di guasto che in un compilatore si manifesta
 * come un programma compilato male.
 * ============================================================================= */

#ifndef FPU_H
#define FPU_H

#include "kernel.h"

/* =============================================================================
 * ! 512 BYTE E NON 108, da agosto 2026: SERVE FXSAVE, NON FNSAVE
 *
 * 108 e' la dimensione di FNSAVE/FRSTOR, che salva i soli registri x87.
 * Bastava finche' i programmi usavano solo la virgola mobile classica.
 *
 * Non basta piu' da quando gira codice compilato da GCC per un bersaglio
 * moderno: libcpp usa `movdqa` e gli altri SIMD in search_line_sse2(),
 * cioe' i registri XMM. Salvare solo l'x87 fra un processo e l'altro
 * lascerebbe gli XMM condivisi — e due processi che li usano si
 * calpesterebbero i dati senza che nessuno se ne accorga, che e'
 * esattamente il guasto silenzioso che questo file esiste per evitare.
 *
 * FXSAVE salva x87 E XMM insieme in un'area di 512 byte allineata a 16.
 * L'allineamento non e' consigliato: e' obbligatorio, un FXSAVE su un
 * indirizzo disallineato e' una #GP.
 * ============================================================================= */
#define FPU_STATE_SIZE  512

/* Rileva il coprocessore, lo azzera e prepara il modello di stato
 * "pulito" che ogni nuovo processo eredita. Va chiamata una volta sola,
 * dopo l'IDT (l'eccezione #MF ha bisogno di un gate installato).
 *
 * Ritorna 1 se una FPU c'e', 0 se non c'e' (un 386 senza 387). */
int  fpu_init(void);

/* 1 se la FPU esiste: chi salva e ripristina lo controlla, perche' su una
 * macchina senza coprocessore FNSAVE non e' lento, e' un'eccezione. */
int  fpu_present(void);

/* 1 se SSE e' stato acceso davvero (CPU capace E CR4 impostato). Serve a
 * chi vuole sapere se l'hardware sotto puo' eseguire codice SIMD: senza,
 * quelle istruzioni sono #UD e il processo muore. */
int  sse_attivo(void);

/* =============================================================================
 * Cosa sa fare la CPU su cui stiamo girando
 *
 * ! SERVE PERCHE' EX-OS DEVE GIRARE ANCHE SU UN 486. Non si puo'
 * decidere a tempo di compilazione: lo stesso kernel.bin deve partire su
 * una macchina senza CPUID e su una con SSE3. Ogni bit qui e' una
 * domanda a cui si risponde UNA VOLTA all'avvio, non a ogni uso.
 *
 * ! MMX NON HA BISOGNO DI NIENTE DAL KERNEL, e questo e' il punto meno
 * ovvio di tutto il file. I registri MM0-MM7 sono ALIAS dei registri x87
 * ST0-ST7 (usano le mantisse a 64 bit): FNSAVE e FXSAVE li salvano gia'
 * entrambi, perche' e' la stessa memoria fisica. Non c'e' nessun bit di
 * CR4 da alzare, nessuna area in piu' da riservare. Se la CPU ha MMX, i
 * programmi possono usarlo — la sola cosa che serviva era una FPU
 * inizializzata, che c'e' dal kernel 0.144.
 * ============================================================================= */
typedef struct {
    uint8_t cpuid;      /* la CPU ha l'istruzione CPUID */
    uint8_t fxsr;       /* FXSAVE/FXRSTOR (e quindi lo stato da 512 byte) */
    uint8_t mmx;
    uint8_t sse;
    uint8_t sse2;
    uint8_t sse3;
    uint8_t cmov;       /* utile al compilatore, non al kernel */
} CpuCapacita;

/* Sempre valida dopo fpu_init(). Prima, tutti zeri. */
const CpuCapacita *cpu_capacita(void);

/* Stato iniziale da mettere nel PCB di un processo appena creato.
 *
 * Non e' un dettaglio di comodo: FRSTOR carica quello che trova, e su un
 * buffer mai scritto caricherebbe spazzatura come parola di controllo e
 * come tag dei registri — con eccezioni di virgola mobile al primo uso,
 * dentro un processo che non ha ancora eseguito una sola istruzione. */
void fpu_init_state(void *dst);

/* Salva lo stato corrente e lo ripristina. Il salvataggio azzera la FPU
 * (FNSAVE lo fa per definizione), quindi va sempre seguito da un
 * ripristino prima che qualcuno la usi di nuovo. */
void fpu_save(void *dst);
void fpu_restore(const void *src);

#endif /* FPU_H */
