/* =============================================================================
 * lib/include/audio.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * «QUALE DRIVER VUOLE QUESTA SCHEDA AUDIO» — la tabella, in un posto solo
 *
 * Stessa forma e stessa ragione di lib/include/rete.h, e la ragione la
 * scrive per esteso quel file: la tabella dentro un programma diverge al
 * primo driver nuovo, perche' chi scrive un driver non ha motivo di aprire
 * il sorgente di un comando. Qui la leggono `audio`, che la stampa e la usa
 * per caricare, e `hwconfig`, che deve scrivere in kernel.cfg il driver
 * giusto per la scheda trovata.
 *
 * -----------------------------------------------------------------------------
 * ! «SCHEDA AUDIO REALTEK» NON E' UNA SCHEDA, ED E' MEGLIO SAPERLO SUBITO
 *
 * Realtek non ha mai fabbricato un controller audio PCI. Quello che sta su
 * quasi tutte le schede madri dal 2000 in poi — e che tutti chiamano «audio
 * Realtek» — e' un CODEC: il chip che converte i numeri in tensione, marcato
 * ALC seguito da tre cifre. Il controller a cui e' attaccato e' di qualcun
 * altro: Intel (ICH), AMD, nVidia, VIA.
 *
 * Quindi il driver di una «scheda Realtek» e' il driver del CONTROLLER —
 * AC'97 sulle macchine fino al 2004 circa, HD Audio dopo — e il codec si
 * riconosce dopo, chiedendo al controller chi ha attaccato. Il nome Realtek
 * compare nel modello che si stampa, non nel nome del driver, ed e' giusto
 * cosi': un driver che si chiamasse realtek.drv non saprebbe guidare la
 * stessa scheda madre con un codec Analog Devices sopra.
 *
 * -----------------------------------------------------------------------------
 * ! LE SCHEDE ISA NON SI ENUMERANO, e non e' un difetto di questo file
 *
 * Una Sound Blaster ISA non ha uno spazio di configurazione: non si puo'
 * chiedere al bus cosa c'e'. L'unico modo di trovarla e' scriverle addosso a
 * uno dei sette indirizzi che la Creative ha usato, e vedere se risponde. Per
 * questo la tabella qui sotto ha DUE elenchi: quello PCI, che si consulta, e
 * quello ISA, che si PROVA — in ordine, e sapendo che provare e' un atto e
 * non una domanda.
 * ============================================================================= */

#ifndef AUDIO_H
#define AUDIO_H

/* =============================================================================
 * Le schede PCI riconosciute
 *
 * ! `driver` NULL VUOL DIRE «MODELLO NOTO, DRIVER DA SCRIVERE», ed e' diverso
 * da scheda sconosciuta: dice che il numero e' stato riconosciuto e che manca
 * il codice, non che l'hardware sia un mistero.
 * ========================================================================== */
typedef struct {
    unsigned short venditore;
    unsigned short dispositivo;
    const char    *modello;
    const char    *driver;      /* NULL = modello noto, driver da scrivere */
} AudioScheda;

const AudioScheda *audio_riconosci(unsigned short venditore,
                                   unsigned short dispositivo);
const AudioScheda *audio_scheda(int i);
int                audio_schede_note(void);

/* =============================================================================
 * I driver da PROVARE quando il bus non dice niente
 *
 * Si provano in quest'ordine, e l'ordine e' una decisione: prima la Sound
 * Blaster, che e' l'unica famiglia ISA con una sonda sicura (il DSP risponde
 * 0xAA a una sequenza che nessun altro chip imita).
 * ========================================================================== */
typedef struct {
    const char *driver;         /* il percorso dell'eseguibile */
    const char *modello;        /* cosa si spera di trovare */
} AudioIsa;

const AudioIsa *audio_isa(int i);
int             audio_isa_quanti(void);

/* =============================================================================
 * Lo stato del servizio audio, per chi non puo' proseguire senza
 *
 * Rende il PID del servizio "audio", oppure un valore <= 0 dopo aver stampato
 * il motivo e cosa fare. ! STAMPA DA SE': chi la chiama non deve aggiungere
 * un proprio messaggio: sarebbero due spiegazioni della stessa cosa, e la
 * seconda quasi sempre meno precisa. Stessa scelta di rete_richiedi().
 * ========================================================================== */
int audio_richiedi(void);

#endif /* AUDIO_H */
