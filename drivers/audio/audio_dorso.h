/* =============================================================================
 * drivers/audio/audio_dorso.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * IL DORSO — cio' che un driver audio deve saper fare, e nient'altro
 *
 * Il servizio "audio" e' fatto di due meta':
 *
 *   drivers/audio/audio_comune.c   l'anello, il protocollo IPC, le prove,
 *                                  il ciclo del server. UGUALE PER TUTTI.
 *   drivers/<scheda>/<scheda>.c    solo i registri di QUELLA scheda.
 *
 * La seconda meta' non contiene ne' IPC, ne' memoria condivisa, ne' il
 * concetto di client. Riceve un buffer, lo fa suonare, e dice quando la
 * scheda ne ha consumato meta'.
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' UN'INTERFACCIA E NON QUATTRO DRIVER INTERI
 *
 * Perche' le schede sono quattro famiglie e la logica e' una: un anello che
 * un client riempie, un buffer che una scheda svuota, e la copia dall'uno
 * all'altro a ogni interrupt. Scritta quattro volte, quella copia avrebbe
 * quattro difetti diversi, e i tre driver che si usano meno resterebbero
 * indietro sul primo che si corregge. E' successo con la tabella delle
 * schede di rete (vedi lib/include/rete.h): non si ripete.
 *
 * -----------------------------------------------------------------------------
 * ! IL DOPPIO BUFFER STA QUI, ED E' L'UNICO MODO CHE FUNZIONA DA RING 3
 *
 * La scheda legge la RAM da sola, a indirizzi fisici, senza fermarsi: un
 * driver in spazio utente non puo' promettere di essere schedulato in tempo
 * per riempire un buffer che sta finendo. Quindi il buffer si divide in due
 * meta': mentre la scheda suona la prima, il driver riempie la seconda, e
 * ha un'intera meta' di tempo — decine di millisecondi — per farlo. Se
 * arriva tardi, la scheda risuona cio' che c'era: si sente un ronzio, non
 * un silenzio, ed e' il sintomo da riconoscere.
 * ============================================================================= */

#ifndef AUDIO_DORSO_H
#define AUDIO_DORSO_H

#include "audio_proto.h"

typedef struct {
    /* Il nome del driver nei messaggi: «sb», «es1371», ... */
    const char *nome;

    /* -------------------------------------------------------------------------
     * Le opzioni della riga di comando che questa scheda capisce (-p, -q,
     * -d...). Chiamata prima di sonda(). Rende 0 se l'argomento e' suo, -1
     * se non lo riconosce — cosi' il comune puo' lamentarsene una volta
     * sola invece che in quattro punti.
     * --------------------------------------------------------------------- */
    int (*opzione)(const char *arg, const char *valore);

    /* -------------------------------------------------------------------------
     * Cerca la scheda e riempi `info`. Rende 0 se c'e', <0 se non c'e'.
     *
     * ! QUI DENTRO STA LA RILEVAZIONE DI IRQ E DMA, e non altrove: sono
     * numeri che si scoprono in modi diversi su schede diverse — su una SB16
     * si leggono da un registro del mixer, su una SB 2.0 si scoprono
     * provocando un interrupt e guardando quale linea si alza, su una scheda
     * PCI li dichiara la configurazione del bus. Chi chiama vuole solo i
     * numeri.
     * --------------------------------------------------------------------- */
    int (*sonda)(AudioInfo *info);

    /* -------------------------------------------------------------------------
     * Prepara la scheda per `f`, che si puo' CORREGGERE al formato piu'
     * vicino che sa suonare. Rende il buffer che la scheda leggera' — quello
     * vero, in memoria che il DMA raggiunge — e la sua misura, che dev'essere
     * PARI perche' si divide in due meta'.
     * --------------------------------------------------------------------- */
    int (*apri)(AudioFormato *f, unsigned char **buf, unsigned int *byte);

    void (*via)(void);      /* comincia il trasferimento continuo */
    void (*ferma)(void);    /* fermalo; il buffer resta valido */
    void (*chiudi)(void);   /* spegni la parte di riproduzione */

    /* -------------------------------------------------------------------------
     * E' arrivato un interrupt sulla linea della scheda. Azzera lo stato
     * dell'hardware — senza, la linea resta alta e irq_done() riaprirebbe
     * una tempesta — e rendi QUALE META' del buffer si e' appena liberata
     * (0 o 1), oppure -1 se l'interrupt non era suo.
     * --------------------------------------------------------------------- */
    int (*irq)(void);

    /* -------------------------------------------------------------------------
     * Quanti byte la SCHEDA dice di aver consumato, letti da un suo
     * registro.
     *
     * ! NON E' UN CONTO NOSTRO, ed e' tutto il punto: e' l'unica prova che
     * l'hardware si sia mosso davvero. Un driver puo' scrivere in tutti i
     * registri giusti, ricevere zero interrupt e credersi funzionante; un
     * contatore che avanza no. Lo usa la prova di collaudo.
     * --------------------------------------------------------------------- */
    unsigned int (*avanzamento)(void);

    void (*volume)(unsigned int percento);

    /* Byte MIDI grezzi verso il sintetizzatore. Rende quanti ne ha presi. */
    int (*midi)(const unsigned char *b, unsigned int n);

    /* -------------------------------------------------------------------------
     * La prova che il percorso MIDI e' vivo, senza suonare niente: la
     * risposta dell'hardware a una domanda a cui solo lui sa rispondere —
     * il timer dell'OPL, il bit di «pronto» della MPU-401. Rende 0 se ha
     * risposto, <0 se no.
     * --------------------------------------------------------------------- */
    int (*midi_vivo)(void);
} AudioDorso;

/* Ogni driver definisce QUESTA, e null'altro di visibile da fuori. */
const AudioDorso *audio_dorso_questo(void);

/* Il main sta in audio_comune.c: un driver non ne ha uno. */

#endif /* AUDIO_DORSO_H */
