/* =============================================================================
 * kernel/include/bootinst.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Installazione dell'avvio su disco. Il perche' la logica stia nel kernel
 * e non in /bin/install e' spiegato in testa a kernel/boot/bootinst.c: e'
 * l'unico punto in cui il sistema scrive fuori da un filesystem, e le due
 * invarianti che protegge (tabella delle partizioni e BPB intatti) non
 * possono essere garantite da un programma utente.
 * ============================================================================= */

#ifndef BOOTINST_H
#define BOOTINST_H

#include "kernel.h"

/* Cio' che e' stato scritto, per poterlo mostrare a chi installa: un
 * "fatto" senza numeri non e' verificabile. */
typedef struct {
    uint32_t s2_lba, s2_cnt;    /* Stage 2: LBA assoluto e settori */
    uint32_t k_lba,  k_cnt;     /* kernel: PRIMO intervallo e settori TOTALI */
    uint32_t k_next;            /* in quanti intervalli e' spezzato */
    uint32_t disco;             /* indice ATA */
    uint32_t voce;              /* voce di partizione marcata attiva, 1-4 */
} BootInstEsito;

/* `punto` e' un punto di montaggio gia' attivo ("/disk"), non un
 * dispositivo: l'installazione ha bisogno di leggere i file, quindi il
 * volume dev'essere montato in scrittura comunque.
 *
 * Ritorna 0, o un errno negativo. In particolare -ESPIPE se uno dei due
 * file e' frammentato. */
int boot_installa(const char *punto, BootInstEsito *esito);

#endif /* BOOTINST_H */
