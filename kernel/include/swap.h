/* =============================================================================
 * kernel/include/swap.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LA MEMORIA VIRTUALE — una pagina che se ne va sul disco e poi torna
 *
 * -----------------------------------------------------------------------------
 * ! SU UNA PARTIZIONE, NON DENTRO UN FILE, ed e' una decisione.
 *
 * Un file di scambio dentro un filesystem vorrebbe dire che per liberare
 * memoria bisogna passare dal filesystem — allocare blocchi, aggiornare inode,
 * magari LEGGERE metadati — e tutto questo mentre la memoria e' finita, cioe'
 * proprio quando ogni allocazione puo' fallire. E' una dipendenza circolare
 * scritta a chiare lettere: il codice che serve a trovare memoria ne chiede.
 *
 * Una partizione e' una finestra di settori con un primo e una lunghezza, e il
 * livello a blocchi la controlla gia' — vedi kernel/include/blk.h. Scriverci
 * una pagina e' «settore N, otto settori»: nessuna allocazione, nessun
 * metadato, nessun filesystem di mezzo. La stessa ragione per cui l'ATA sta
 * nel kernel e non in un server.
 *
 * -----------------------------------------------------------------------------
 * ! SI RICONOSCE DALLA FIRMA CHE C'E' DENTRO, NON DAL TIPO NELLA TABELLA.
 *
 * Il byte di tipo nella tabella delle partizioni lo scrive chiunque e non lo
 * controlla nessuno: fidarsene vorrebbe dire che un errore di fdisk basta a
 * far scrivere pagine di memoria sopra un filesystem. La firma sta nel primo
 * settore DELLA partizione, ce la mette `mkswap`, e senza quella il kernel non
 * tocca niente. Il tipo 0x82 nella tabella resta una cortesia per gli altri
 * sistemi, non una prova.
 *
 * -----------------------------------------------------------------------------
 * ! SI SFRATTANO SOLO PAGINE CON UN PROPRIETARIO SOLO, ed e' il limite piu'
 * importante di questa prima stesura.
 *
 * Mandare via una pagina vuol dire trovare TUTTE le tabelle che la mappano e
 * segnarle. EX-OS non ha una mappa all'indietro — da pagina fisica a chi la
 * usa — quindi le pagine condivise (il codice di una libreria, una zona di
 * memoria condivisa) non si toccano: il PMM sa quanti proprietari ha una
 * pagina, e si sfratta solo chi ne ha uno.
 *
 * Non e' una perdita: cio' che occupa RAM in un sistema sotto pressione sono
 * heap, stack e dati privati, e quelli hanno un proprietario solo. Il codice
 * condiviso e' anche quello che si rileggerebbe piu' facilmente dal file —
 * ed e' il prossimo gradino, non questo.
 * ============================================================================= */

#ifndef SWAP_H
#define SWAP_H

#include "kernel.h"

/* La firma nel primo settore della partizione. Otto byte esatti, senza zero
 * finale: si confronta con memcmp e non con le funzioni di stringa. */
#define SWAP_FIRMA      "EXOSSWAP"
#define SWAP_FIRMA_LEN  8
#define SWAP_VERSIONE   1

/* ! IL PRIMO SLOT NON E' IL SETTORE 0, e il salto non e' un byte di margine:
 * l'intestazione occupa il primo settore, e gli slot cominciano allineati a
 * una pagina perche' ogni slot e' esattamente una pagina — otto settori da
 * 512. Un allineamento diverso costringerebbe ogni lettura a due richieste. */
#define SWAP_SETTORI_TESTA  8       /* 4 KB: intestazione piu' margine */
#define SWAP_SETTORI_SLOT   (PAGE_SIZE / 512)

/* L'intestazione, come sta sul disco. La scrive /bin/mkswap.
 *
 * ! DUPLICATA A MANO in bin/mkswap/mkswap.c, come ogni struttura che passa fra
 * kernel e spazio utente in questo sistema. Devono restare identiche. */
typedef struct PACKED {
    char     firma[SWAP_FIRMA_LEN];  /* "EXOSSWAP" */
    uint32_t versione;               /* SWAP_VERSIONE */
    uint32_t pagina;                 /* byte per pagina: dev'essere PAGE_SIZE */
    uint32_t slot;                   /* quanti slot ci stanno */
    uint32_t primo;                  /* LBA del primo slot, dentro la partizione */
    char     etichetta[16];
} SwapTesta;

/* =============================================================================
 * LA PAGINA CHE NON C'E' PIU' STA SCRITTA NELLA SUA PTE
 *
 * Quando il bit Present e' zero la CPU non guarda piu' nessun altro bit: i
 * restanti trentuno sono del sistema operativo. Ci si mette il numero dello
 * slot e i permessi che la pagina aveva, cosi' al ritorno si rimette esatta
 * com'era senza doverli cercare altrove.
 *
 * ! IL MARCATORE SERVE, e non basta «non presente»: una PTE a zero e' una
 * pagina che non e' MAI esistita, e le due si trattano in modo opposto — la
 * prima si rilegge dal disco, la seconda e' un errore del programma. Senza un
 * bit che le distingua, ogni puntatore sbagliato diventerebbe una lettura di
 * swap a caso.
 *
 *     bit 0       0, sempre: la pagina non e' in RAM
 *     bit 1, 2    i permessi di prima (scrivibile, utente)
 *     bit 9       1: questa PTE parla di uno slot di swap
 *     bit 12..31  il numero dello slot (venti bit: un milione di slot, 4 GB)
 * ========================================================================== */
#define PG_SWAP         (1u << 9)
#define SWAP_SLOT_MAX   (1u << 20)

#define SWAP_PTE(slot, flags) \
    ((((uint32_t)(slot)) << 12) | PG_SWAP | ((flags) & (PG_WRITABLE | PG_USER)))
#define SWAP_PTE_E_SWAP(pte)  (((pte) & (PG_PRESENT | PG_SWAP)) == PG_SWAP)
#define SWAP_PTE_SLOT(pte)    ((pte) >> 12)
#define SWAP_PTE_FLAGS(pte)   (((pte) & (PG_WRITABLE | PG_USER)) | PG_PRESENT)

/* Accende l'area di scambio sul dispositivo a blocchi `nome` ("hd0p2").
 * Rende 0 se e' pronta, <0 altrimenti — e in ogni caso il sistema continua a
 * funzionare: senza swap si torna a com'era ieri. */
int  swap_init(const char *nome);

/* C'e' un'area accesa? */
int  swap_attivo(void);

/* Quanti slot in tutto e quanti occupati: per `mem` e per il log. */
void swap_conta(uint32_t *totali, uint32_t *usati);

/* Prende uno slot libero. Rende il numero, oppure -1 se sono finiti. */
int32_t swap_slot_prendi(void);

/* Rimette in circolo uno slot. Va chiamata da CHIUNQUE butti via una PTE che
 * parla di swap — compreso chi distrugge lo spazio di un processo morto: uno
 * slot dimenticato non si recupera fino al riavvio. */
void swap_slot_molla(uint32_t slot);

/* Scrive/legge una pagina intera. Rendono 0 o <0. */
int  swap_scrivi(uint32_t slot, uint32_t fisico);
int  swap_leggi(uint32_t slot, uint32_t fisico);

/* =============================================================================
 * swap_sfratta — manda via UNA pagina e rende 1 se c'e' riuscita
 *
 * E' la funzione che si chiama quando pmm_alloc_page() ha detto di no. Non
 * promette niente: se non trova un candidato rende 0, e chi la chiama fallisce
 * come faceva prima. Un sistema con lo swap pieno non deve comportarsi peggio
 * di uno senza swap.
 * ========================================================================== */
int  swap_sfratta(void);

#endif /* SWAP_H */
