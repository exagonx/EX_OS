/* =============================================================================
 * lib/include/exlib.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LIBRERIE CONDIVISE — il patto fra chi le scrive e chi le usa
 *
 * Una libreria di EX-OS e' un ELF normalissimo, collegato a un indirizzo
 * riservato nella fascia 0x04000000-0x08000000, il cui PUNTO D'INGRESSO non e'
 * codice: e' la struttura ExLibTesta qui sotto. Il kernel la mappa (una volta
 * per tutto il sistema, vedi kernel/loader/lib.c) e rende quell'indirizzo.
 *
 * ! SI RISOLVE PER NOME, NON PER POSIZIONE, ed e' tutto il punto. Con una
 * tabella posizionale — «la funzione 7 e' ex_scrivi» — riordinare le voci o
 * toglierne una in mezzo romperebbe ogni applicazione gia' compilata, e nessun
 * errore lo direbbe: si chiamerebbe semplicemente la funzione sbagliata. Coi
 * nomi si puo' aggiungere, riordinare e riscrivere il corpo di qualunque
 * funzione, e le applicazioni continuano a funzionare senza essere
 * ricompilate. Solo TOGLIERE un nome le rompe — ed e' esattamente il patto di
 * una DLL di Windows.
 *
 * ! IL COSTO E' UNA RICERCA ALL'AVVIO, e si paga una volta sola: l'applicazione
 * risolve i nomi che le servono la prima volta che li usa e poi chiama
 * attraverso un puntatore. Non c'e' niente di dinamico nel percorso caldo.
 *
 * Come si scrive una libreria:
 *
 *     #include "exlib.h"
 *
 *     static const char *const nomi[] = { "pippo", "pluto" };
 *     static void *const dove[]       = { (void *)pippo, (void *)pluto };
 *
 *     EXLIB_TESTA(mia_tabella, nomi, dove);
 *
 * e nel linker script:  ENTRY(mia_tabella)  con la base a 0x04000000.
 * ============================================================================= */

#ifndef EXLIB_H
#define EXLIB_H

#ifdef __cplusplus
extern "C" {
#endif

/* 'EXLB' in little-endian. Serve a distinguere una libreria vera da un
 * eseguibile qualunque che il kernel abbia mappato: senza, il primo campo
 * letto sarebbe codice interpretato come un numero di funzioni. */
#define EXLIB_MAGIA     0x424C5845u

/* La versione del PATTO, non della libreria. Cambia solo se cambia la forma di
 * questa struttura, e allora le applicazioni vecchie devono rifiutarsi invece
 * di leggere campi che stanno da un'altra parte. */
#define EXLIB_VERSIONE  1

typedef struct {
    unsigned int             magia;      /* EXLIB_MAGIA */
    unsigned int             versione;   /* EXLIB_VERSIONE */
    unsigned int             n;          /* quante funzioni sono esportate */
    const char *const       *nomi;       /* n stringhe */
    void *const             *indirizzi;  /* n puntatori, nello stesso ordine */
} ExLibTesta;

/* -----------------------------------------------------------------------------
 * ! LA TESTA VA IN UNA SEZIONE SUA, e il linker script la tiene con KEEP(). Con
 * --gc-sections il collegamento parte dal simbolo d'ingresso: se la tabella
 * finisse in .rodata insieme a tutto il resto, basterebbe un giro di
 * ottimizzazione perche' il collegamento la spostasse e l'indirizzo d'ingresso
 * non fosse piu' il suo. In una sezione dedicata sta dove e' stata messa.
 * --------------------------------------------------------------------------- */
#define EXLIB_TESTA(simbolo, vett_nomi, vett_indirizzi)                        \
    __attribute__((section(".exlib_testa"), used))                             \
    const ExLibTesta simbolo = {                                               \
        EXLIB_MAGIA, EXLIB_VERSIONE,                                           \
        (unsigned int)(sizeof(vett_nomi) / sizeof((vett_nomi)[0])),            \
        (vett_nomi), (vett_indirizzi)                                          \
    }

/* -----------------------------------------------------------------------------
 * Il lato di chi la usa
 *
 * exlib_apri()    chiede al kernel di mappare la libreria e rende la testa,
 *                 oppure 0. Chiamarla due volte con lo stesso percorso e'
 *                 innocuo ma inutile: e' gia' mappata.
 *
 * exlib_simbolo() cerca un nome. Rende 0 se non c'e' — e chi lo riceve DEVE
 *                 controllarlo: e' il modo in cui si accorge che la libreria
 *                 installata e' piu' vecchia del programma.
 * --------------------------------------------------------------------------- */
const ExLibTesta *exlib_apri(const char *percorso);
void             *exlib_simbolo(const ExLibTesta *t, const char *nome);

/* Comodita': prova i percorsi uno dopo l'altro e rende il primo che risponde.
 * Serve perche' su un sistema installato le librerie stanno in /exwin/lib e
 * avviando dal CD stanno sotto /cdrom — la stessa regola del program manager. */
const ExLibTesta *exlib_apri_fra(const char *const *percorsi, int quanti);

#ifdef __cplusplus
}
#endif

#endif /* EXLIB_H */
