/* =============================================================================
 * kernel/include/rtc.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Orologio in tempo reale (MC146818 / CMOS).
 *
 * Fino ad agosto 2026 EX-OS non aveva NESSUNA nozione di data e ora:
 * l'unica misura del tempo era uptime_ms(), i millisecondi dall'avvio.
 * Basta per misurare durate e scadenze, e non basta per niente che
 * debba dire CHE ORA È — la barra di stato di un editor, e un giorno la
 * data di modifica di un file su FAT.
 *
 * L'orologio è una periferica separata dalla CPU, alimentata a batteria,
 * che continua a contare a macchina spenta. Si legge attraverso due
 * porte: 0x70 seleziona il registro, 0x71 ne dà il contenuto.
 * ============================================================================= */

#ifndef RTC_H
#define RTC_H

#include "kernel.h"

/* Data e ora. DUPLICATA A MANO in lib/include/libc.h e lib/libc.c
 * (stessa convenzione di DirEntry e MemInfo): attraversa l'ABI della
 * syscall, e le tre copie devono restare identiche.
 *
 * I campi sono già in binario e in formato 24 ore: la conversione da
 * BCD e da 12 ore, se l'orologio è configurato così, la fa rtc_read. */
typedef struct {
    uint32_t anno;      /* 4 cifre, es. 2026 */
    uint32_t mese;      /* 1-12 */
    uint32_t giorno;    /* 1-31 */
    uint32_t ora;       /* 0-23 */
    uint32_t minuto;    /* 0-59 */
    uint32_t secondo;   /* 0-59 */
} RtcTime;

/* Riempie *t. Ritorna 0, o -1 se l'orologio non risponde o consegna una
 * data impossibile (CMOS scarico su hardware vecchio). */
int rtc_read(RtcTime *t);

#endif /* RTC_H */
