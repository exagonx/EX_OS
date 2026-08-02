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

/* Dimensione dell'area di stato di FNSAVE/FRSTOR: 108 byte in modo
 * protetto a 32 bit (7 parole di intestazione + 8 registri da 10 byte).
 * E' un formato fissato dall'architettura, non una scelta nostra. */
#define FPU_STATE_SIZE  108

/* Rileva il coprocessore, lo azzera e prepara il modello di stato
 * "pulito" che ogni nuovo processo eredita. Va chiamata una volta sola,
 * dopo l'IDT (l'eccezione #MF ha bisogno di un gate installato).
 *
 * Ritorna 1 se una FPU c'e', 0 se non c'e' (un 386 senza 387). */
int  fpu_init(void);

/* 1 se la FPU esiste: chi salva e ripristina lo controlla, perche' su una
 * macchina senza coprocessore FNSAVE non e' lento, e' un'eccezione. */
int  fpu_present(void);

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
