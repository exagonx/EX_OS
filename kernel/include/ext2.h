/* =============================================================================
 * kernel/include/ext2.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Driver ext2 in lettura e scrittura, sopra il livello a blocchi.
 *
 * La lettura e' stata scritta e provata per prima, da sola: leggere
 * richiede di capire il formato, scrivere richiede di non romperlo mai, e
 * farli insieme significa scoprire gli errori del primo dentro i danni del
 * secondo. Con il lettore gia' verificato contro volumi fatti da mke2fs,
 * ogni errore di scrittura si vede subito per quello che e'.
 *
 * ⚠️ NIENTE GIORNALE. ext2 non ne ha, e questo driver non ne inventa uno:
 * un'interruzione a meta' di un'operazione puo' lasciare i contatori dei
 * liberi indietro rispetto alle bitmap. E' la stessa finestra di ext2 su
 * Linux e la risposta e' la stessa, e2fsck. Cio' che NON puo' succedere e'
 * che un blocco risulti libero mentre e' gia' in uso: le bitmap si
 * scrivono sempre prima che il blocco venga consegnato.
 *
 * Le scelte e le trappole del formato stanno in kernel/fs/ext2.c.
 * ============================================================================= */

#ifndef EXT2_H
#define EXT2_H

#include "kernel.h"

#define EXT2_MAX_MOUNT      2
#define EXT2_NOME_MAX       256     /* 255 caratteri + NUL: il massimo di ext2 */
#define EXT2_PERCORSO_MAX   320

typedef struct {
    char     nome[EXT2_NOME_MAX];
    uint32_t dimensione;
    uint32_t inode;
    uint8_t  is_dir;
    /* Convertite dal i_mtime dell'inode, che e' un tempo Unix. Vedi
     * VfsStat in vfs.h: il formato FAT e' quello che attraversa la
     * syscall, quindi la conversione si fa una volta sola, qui. */
    uint16_t data;
    uint16_t ora;
} Ext2DirEntry;

/* Monta il volume sul dispositivo a blocchi `blkdev`.
 * Ritorna un handle >= 0, o <0 se non e' un ext2 leggibile. */
int  ext2_mount(int blkdev);
int  ext2_umount(int mnt);

/* Diagnostica del volume montato. */
uint32_t    ext2_blocchi(int mnt);
uint32_t    ext2_blocchi_liberi(int mnt);
uint32_t    ext2_dim_blocco(int mnt);
const char *ext2_etichetta(int mnt);

/* `percorso` e' interno al volume ("/" oppure "/bin/sh"). A differenza di
 * fat12.c i percorsi possono avere piu' livelli. */
int  ext2_readdir(int mnt, const char *percorso, Ext2DirEntry *out,
                  uint32_t max, uint32_t start);
int  ext2_stat(int mnt, const char *percorso, Ext2DirEntry *out);

/* Legge `size` byte dal file a partire da `offset`.
 * Ritorna i byte letti (0 = fine file), <0 su errore. */
int  ext2_read(int mnt, const char *percorso, void *buf,
               uint32_t size, uint32_t offset);

/* =============================================================================
 * Scrittura
 *
 * Stessa convenzione dei ritorni di kernel/include/fat.h, e non per
 * simmetria: il VFS li tratta con lo stesso codice, e due convenzioni
 * diverse costringerebbero a ricordarsi quale driver si sta chiamando.
 *
 *   0 (o i byte scritti)  riuscito
 *   -1                    errore generico
 *   -2                    il caso che il chiamante deve distinguere:
 *                         "esiste gia'" per create/mkdir, "non vuota" per
 *                         rmdir. Senza, EEXIST e ENOTEMPTY diventerebbero
 *                         un generico "non riuscito".
 *
 * A differenza di fat.c NON c'e' cache in scrittura: ogni operazione
 * arriva al disco prima di ritornare. ext2_sync() esiste per completare
 * l'interfaccia — un VFS che deve sapere quali filesystem hanno un sync e
 * quali no e' un VFS che tira a indovinare.
 *
 * ext2_truncate() accetta qualunque dimensione, in su e in giu'.
 * ALLUNGARE non alloca niente: lo spazio in mezzo diventa un buco, che in
 * ext2 e' legittimo e si legge come zeri. ACCORCIARE libera la coda della
 * catena di blocchi lasciando intatta la testa — indiretti parziali
 * compresi, che e' il caso in cui e' facile buttare via i puntatori ai
 * blocchi che restano. Vedi pota_indiretto() in kernel/fs/ext2.c.
 * ============================================================================= */
int  ext2_create  (int mnt, const char *percorso);
int  ext2_mkdir   (int mnt, const char *percorso);
int  ext2_rmdir   (int mnt, const char *percorso);
int  ext2_unlink  (int mnt, const char *percorso);
/* Cambia il NOME di una voce senza spostare i dati. ⚠️ SOLO nella stessa
 * directory: -3 se differiscono, -2 se la destinazione esiste gia'. Vedi
 * il commento esteso in kernel/fs/ext2.c. */
int  ext2_rename  (int mnt, const char *da, const char *a);
int  ext2_write   (int mnt, const char *percorso, const void *buf,
                   uint32_t size, uint32_t offset);
int  ext2_truncate(int mnt, const char *percorso, uint32_t nuova_dim);
int  ext2_sync    (int mnt);

/* =============================================================================
 * La mappa dei settori di un file, per chi non sa leggere un filesystem
 *
 * Serve al settore di avvio e a Stage 2: leggono SETTORI, non file, e
 * l'unica cosa che possono ricevere e' "da qui, per tanti".
 *
 * PERCHE' UNA LISTA E NON UN INTERVALLO UNICO, come su FAT. Un file ext2
 * non e' quasi mai contiguo, e non per frammentazione: il blocco di
 * PUNTATORI viene allocato in mezzo ai dati, perche' serve prima del
 * tredicesimo blocco. Un kernel da 147 KB appena copiato su un volume
 * vergine sta cosi':
 *
 *     (0-11):74-85, (IND):86, (12-144):87-219
 *
 * cioe' due tratti contigui separati dall'indiretto. Pretendere un
 * intervallo unico significherebbe rifiutare ogni file oltre i 12 blocchi,
 * cioe' ogni kernel.
 *
 * `lba` e `cnt` sono in SETTORI da 512 byte, relativi al volume. Il totale
 * e' troncato alla dimensione reale del file: leggere fino alla fine
 * dell'ultimo blocco tirerebbe in memoria byte di un altro file.
 *
 * Ritorna 0, -2 se servirebbero piu' di `max` intervalli, -1 su errore o
 * se il file e' SPARSO — un buco non e' caricabile da chi legge settori:
 * non sa che li' devono esserci zeri.
 * ============================================================================= */
int  ext2_estensioni(int mnt, const char *percorso, uint32_t *lba,
                     uint32_t *cnt, uint32_t max, uint32_t *n_out);

#endif /* EXT2_H */
