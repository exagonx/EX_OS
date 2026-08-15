/* =============================================================================
 * kernel/include/atapi.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Lettori CD/DVD (ATAPI) in PIO, sopra lo stesso bus di kernel/block/ata.c.
 *
 * COS'E' DAVVERO UN DISPOSITIVO ATAPI. Sta sui canali IDE e ne usa i
 * registri, ma non prende comandi ATA: prende un PACCHETTO di 12 byte —
 * un comando SCSI — consegnato attraverso il registro dati. Il registro
 * che su un disco contiene l'LBA, qui contiene quanti byte il dispositivo
 * puo' consegnare per ogni DRQ. Cambiano i comandi, non il bus: per
 * questo gli helper di temporizzazione sono quelli di ata.h e non una
 * seconda copia (vedi il commento in fondo a quel file).
 *
 * LE TRE DIFFERENZE CHE CONTANO rispetto a un disco rigido:
 *
 *  1. IL BLOCCO E' DA 2048 BYTE, non da 512. Non e' un dettaglio da
 *     nascondere sotto una divisione: chiedere "il settore 3" a un lettore
 *     significa chiedere il blocco 0 e prenderne il quarto quarto. La
 *     traduzione sta in kernel/block/blk.c, in un punto solo.
 *
 *  2. IL SUPPORTO PUO' NON ESSERCI, e puo' cambiare mentre il sistema
 *     gira. La capacita' NON e' una proprieta' del dispositivo — come lo
 *     e' per un disco, che la dichiara con IDENTIFY una volta per tutte —
 *     ma del disco che c'e' dentro adesso. Va richiesta ogni volta che
 *     serve, e "vassoio vuoto" e' una risposta normale, non un guasto.
 *
 *  3. GLI ERRORI SONO A DUE LIVELLI. Il bit ERR nello stato dice solo
 *     "CHECK CONDITION": il motivo sta nei dati di SENSE, che vanno
 *     chiesti con un secondo comando. Senza leggerli, "non c'e' il disco",
 *     "il disco e' appena stato cambiato" e "il disco e' illeggibile"
 *     sono la stessa cosa — e le prime due non sono errori.
 *
 * SOLA LETTURA, E NON PER PIGRIZIA. Scrivere su un supporto ottico non e'
 * una write: e' una sessione di masterizzazione, con un modello di
 * comandi (TRACK RESERVE, CLOSE SESSION, e le loro varianti per ogni
 * famiglia di supporto) che non ha niente a che vedere con questo file.
 * Un dispositivo a blocchi che accettasse scritture e le perdesse sarebbe
 * peggio di uno che le rifiuta.
 * ============================================================================= */

#ifndef ATAPI_H
#define ATAPI_H

#include "kernel.h"

/* La dimensione del blocco logico di un CD-ROM dati (Mode 1) e di un DVD.
 * READ CAPACITY la dichiara comunque, e atapi_capacita() la riporta: un
 * supporto che dicesse altro va rifiutato, non assunto. */
#define ATAPI_DIM_BLOCCO    2048

/* Prepara i lettori gia' RICONOSCIUTI da ata_init() — quelli con
 * tipo == ATA_TYPE_ATAPI. Va chiamata dopo di lui e dopo l'abilitazione
 * degli interrupt (le attese usano il PIT). Ritorna quanti ne ha trovati.
 *
 * NON tocca il vassoio e non pretende che ci sia un disco: un lettore
 * vuoto e' un lettore funzionante. */
int atapi_init(void);

int atapi_conta(void);

/* 1 se `indice` (indice ATA, 0..3) e' un lettore ottico. */
int atapi_e_lettore(int indice);

/* C'e' un disco leggibile nel lettore?
 *   1  si'
 *   0  no: vassoio vuoto, o supporto non ancora pronto dopo l'attesa
 *  <0  il dispositivo non risponde
 *
 * Sonda davvero il lettore (TEST UNIT READY) e assorbe le due risposte
 * che non sono errori: l'UNIT ATTENTION del primo accesso dopo un cambio
 * di supporto, e il "sto diventando pronto" di un disco che sta ancora
 * prendendo giri. */
int atapi_supporto(int indice);

/* Capacita' del supporto ATTUALE, in blocchi da `dim_blocco` byte.
 * Ritorna 0 e scrive i valori, oppure <0 se non c'e' un disco leggibile.
 * `blocchi` o `dim_blocco` possono essere NULL. */
int atapi_capacita(int indice, uint32_t *blocchi, uint32_t *dim_blocco);

/* Legge `n` blocchi da 2048 byte a partire da `lba`. Ritorna 0, <0 su
 * errore. Il buffer deve essere di n * 2048 byte. */
int atapi_read(int indice, uint32_t lba, uint32_t n, void *buf);

/* Apre (`apri` = 1) o chiude (`apri` = 0) il vassoio. Ritorna 0, <0 su
 * errore. Chi chiama deve essersi gia' assicurato che il supporto non sia
 * montato: qui non si sa nulla dei montaggi. */
int atapi_vassoio(int indice, int apri);

#endif /* ATAPI_H */
