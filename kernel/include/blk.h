/* =============================================================================
 * kernel/include/blk.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Astrazione a blocchi: un'interfaccia unica per il floppy, i dischi ATA
 * interi e le singole partizioni.
 *
 * PERCHE' ESISTE. Oggi kernel/fs/fat12.c parla direttamente all'FDC, con
 * LBA a 16 bit e la geometria del floppy compilata dentro. Un driver FAT
 * che debba funzionare su una partizione da 1 GB non puo' nascere sopra
 * quel modello: gli serve qualcuno a cui chiedere "dammi il settore N di
 * QUESTO dispositivo", senza sapere se sotto c'e' un motore, un disco o
 * una fetta di disco.
 *
 * LA FINESTRA — la proprieta' di sicurezza piu' importante di questo file.
 *
 * Un dispositivo di tipo partizione e' una FINESTRA sul disco: ha un primo
 * settore e una lunghezza. Ogni lettura e scrittura viene tradotta
 * (lba -> primo + lba) e RIFIUTATA se esce dalla finestra.
 *
 * E' cio' che rende impossibile a un filesystem montato su hd0p1 di
 * toccare hd0p2, o il settore 0 con la tabella delle partizioni, anche
 * se ha un bug nei calcoli o legge metadati corrotti da un disco
 * malformato. Il controllo sta QUI, in un punto solo, dove passano
 * tutti: metterlo nei filesystem significherebbe riscriverlo per ognuno
 * e sbagliarlo prima o poi.
 * ============================================================================= */

#ifndef BLK_H
#define BLK_H

#include "kernel.h"
#include "mbr.h"

#define BLK_MAX_DEV     16
#define BLK_NOME_MAX    12

#define BLK_TIPO_NESSUNO    0
#define BLK_TIPO_FLOPPY     1
#define BLK_TIPO_DISCO      2   /* disco ATA intero */
#define BLK_TIPO_PART       3   /* partizione: finestra su un disco ATA */
#define BLK_TIPO_CDROM      4   /* lettore ottico ATAPI: vedi blk_supporto */

typedef struct {
    char     nome[BLK_NOME_MAX];  /* "fd0", "hd0", "hd0p1" */
    uint8_t  usato;
    uint8_t  tipo;                /* BLK_TIPO_* */
    uint8_t  disco;               /* indice ATA (per DISCO e PART) */
    uint8_t  sola_lettura;
    uint8_t  in_uso;              /* montaggi attivi: vedi blk_acquisisci */
    uint64_t primo;               /* LBA di partenza nel supporto sottostante */
    uint64_t settori;             /* lunghezza della finestra */
} BlkDev;

/* Registra i dispositivi trovati. Va chiamata DOPO ata_init(),
 * atapi_init() e fat12_init(), perche' li interroga tutti e tre. Ritorna
 * quanti ne ha registrati. */
int  blk_init(void);

/* =============================================================================
 * SUPPORTI RIMOVIBILI — la finestra di un CD la decide il disco inserito
 *
 * Per un disco rigido la lunghezza e' una proprieta' del DISPOSITIVO,
 * nota una volta per tutte al rilevamento. Per un lettore ottico no: e'
 * una proprieta' del SUPPORTO, cambia a ogni inserimento e non esiste
 * quando il vassoio e' vuoto.
 *
 * blk_supporto() e' il punto in cui quella differenza viene assorbita:
 * sonda il lettore, e se c'e' un disco aggiorna la finestra con la sua
 * capacita' vera. Un dispositivo NON rimovibile risponde sempre 1 senza
 * toccare niente, cosi' chi chiama non deve sapere che tipo ha in mano.
 *
 * Va chiamata prima di montare e prima di identificare un volume: senza,
 * un lettore con un disco appena inserito ha ancora una finestra di zero
 * settori, e ogni lettura verrebbe rifiutata come "fuori finestra".
 *
 * Ritorna 1 se c'e' un supporto leggibile, 0 se non c'e', <0 se il
 * dispositivo non risponde.
 * ============================================================================= */
int  blk_supporto(int i);

/* 1 se il dispositivo e' a supporto rimovibile (oggi: solo i CD/DVD). */
int  blk_rimovibile(int i);

int  blk_conta(void);
const BlkDev *blk_get(int i);

/* Cerca per nome ("hd0p1"). Ritorna l'indice, o -1. */
int  blk_trova(const char *nome);

/* lba e' RELATIVO al dispositivo. Ritorna 0, <0 su errore o se la
 * richiesta esce dalla finestra. */
int  blk_read (int i, uint64_t lba, uint32_t n, void *buf);
int  blk_write(int i, uint64_t lba, uint32_t n, const void *buf);
int  blk_flush(int i);

/* Indice del dispositivo che rappresenta la partizione `numero` del disco
 * ATA `disco`. Ritorna -1 se non registrata (per esempio perche' era
 * fuori dal disco, o perche' e' un contenitore esteso). */
int  blk_per_partizione(int disco, int numero);

/* =============================================================================
 * Conteggio degli usi — chi sta usando questo dispositivo
 *
 * Chi monta un dispositivo lo ACQUISISCE, chi smonta lo RILASCIA. Il
 * conteggio serve a una cosa sola, ma essenziale: blk_rescan() deve
 * poter dire di no.
 *
 * PERCHE' UN CONTATORE QUI E NON UNA DOMANDA AL VFS. La strada ovvia
 * sarebbe stata far chiedere a blk_rescan() "VFS, hai per caso montato
 * questo?". Ma cosi' il livello a blocchi dovrebbe conoscere il livello
 * dei filesystem — e conoscerli tutti, uno per uno, man mano che
 * arrivano: il giorno che esiste ext2 servirebbe una domanda in piu', e
 * il giorno che qualcuno la dimentica il rescan ripartiziona sotto un
 * volume montato senza accorgersene.
 *
 * Con il contatore la dipendenza va nel verso giusto — chi usa dichiara
 * di usare — e un nuovo filesystem non puo' sbagliare per omissione: se
 * non acquisisce, non ha nemmeno un dispositivo su cui lavorare.
 * ============================================================================= */
int  blk_acquisisci(int i);
int  blk_rilascia(int i);
int  blk_occupato(int i);

/* =============================================================================
 * Rilettura della tabella delle partizioni di UN disco.
 *
 * Da chiamare dopo mbr_scrivi(): finche' non si fa, i dispositivi hd<d>p*
 * descrivono finestre che sul disco non esistono piu'. Una scrittura
 * attraverso una di quelle finestre finirebbe in mezzo a un'altra
 * partizione — cioe' esattamente il danno che la finestra esiste per
 * impedire.
 *
 * Rifiuta con -EBUSY se una qualunque partizione di quel disco e' in uso.
 *
 * NON COMPATTA l'array, e non e' una svista: gli indici dei dispositivi
 * sono tenuti dai montaggi attivi. Se rimuovere hd0p1 facesse scalare di
 * uno tutti quelli dopo, un filesystem montato su hd1p1 si troverebbe a
 * leggere da un'altra partizione senza che nulla lo segnali. Gli slot
 * liberati restano vuoti e vengono riusati.
 *
 * Ritorna il numero di partizioni registrate per quel disco, o un errno
 * negativo.
 * ============================================================================= */
int  blk_rescan(int disco);

/* =============================================================================
 * Ripartizionamento: scrivi la tabella E rimetti in pari i dispositivi.
 *
 * E' l'UNICA porta d'ingresso al partizionamento, ed e' qui e non in
 * mbr.c per una ragione precisa: mbr_scrivi() sa scrivere una tabella ma
 * non sa niente dei dispositivi che quella tabella descrive. Chiamarla
 * da sola e poi ricordarsi di chiamare blk_rescan() e' un contratto che
 * funziona finche' qualcuno non se ne dimentica, e chi se ne dimentica
 * lascia in memoria delle finestre che sul disco non esistono piu' — cioe'
 * il livello a blocchi smette di essere una protezione e diventa il modo
 * per scrivere nel posto sbagliato.
 *
 * Nell'ordine: rifiuta se una qualunque partizione del disco e' in uso
 * (PRIMA di toccare il disco, non dopo), scrive, rilegge.
 *
 * Ritorna 0, o un errno negativo. `problemi` come in mbr_scrivi().
 * ============================================================================= */
int  blk_ripartiziona(int disco, const Partizione *voci, int n,
                      uint32_t *problemi);

#endif /* BLK_H */
