/* =============================================================================
 * kernel/include/tsc.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il contatore di cicli — misurare sotto i 10 millisecondi
 *
 * Fino a qui l'unico orologio del kernel era g_ticks, il PIT a 100 Hz: la sua
 * unita' e' 10 ms. Va benissimo per aspettare che un floppy risponda, e non
 * serve a niente per sapere quanto costa una chiamata di sistema o una
 * composizione di schermo — cose che stanno sotto il MILLESIMO di quella
 * unita'. Con g_ticks, misurare MMX nel compositore era impossibile: la
 * risposta sarebbe stata «0 tick» in entrambi i casi.
 *
 * RDTSC conta i CICLI di clock dall'accensione, in un registro a 64 bit. Su un
 * Pentium 133 un ciclo e' 7,5 nanosecondi.
 * ============================================================================= */
#ifndef TSC_H
#define TSC_H

#include "kernel.h"

/* Legge il contatore grezzo. **Non chiamarla senza aver verificato tsc_c_e()**:
 * su un 486 RDTSC e' un'istruzione inesistente e alza #UD. */
uint64_t tsc_leggi(void);

/* 1 se la CPU ha RDTSC ed e' stato calibrato con successo. */
int      tsc_c_e(void);

/* Cicli al secondo, come misurati all'avvio. 0 se non disponibile. */
uint32_t tsc_hz(void);

/* Calibrazione. Va chiamata DOPO fpu_init() (che e' chi interroga CPUID) e
 * puo' essere chiamata prima che gli interrupt siano attivi: non usa g_ticks,
 * usa il canale 2 del PIT letto a polling. */
void     tsc_init(void);

/* Converte una differenza di cicli in microsecondi. Prende la differenza gia'
 * fatta, non i due estremi: cosi' il caso normale — un intervallo breve — non
 * paga mai una divisione a 64 bit.
 *
 * ! RITORNA 0 SE IL TSC NON C'E', e chi misura deve saperlo distinguere da un
 * intervallo davvero nullo. */
uint32_t tsc_us(uint64_t cicli);

#endif /* TSC_H */
