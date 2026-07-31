/* =============================================================================
 * kernel/include/fat.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Driver FAT12/16/32 guidato dal BPB, a piu' istanze, sopra il livello a
 * blocchi. Nasce ACCANTO a kernel/fs/fat12.c e non al suo posto: il floppy
 * di avvio resta sulla strada collaudata finche' questa non e' provata su
 * entrambi.
 *
 * Le scelte di progetto e il perche' stanno in kernel/fs/fat.c.
 * ============================================================================= */

#ifndef FAT_H
#define FAT_H

#include "kernel.h"

#define FAT_MAX_MOUNT       4
#define FAT_MAX_OPEN        16
#define FAT_NOME_MAX        13      /* "NOME8.EXT" + NUL */
#define FAT_PERCORSO_MAX    128

/* Attributi (identici a FAT12 classico) */
#define FAT_ATTR_RDONLY     0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLID      0x08
#define FAT_ATTR_DIR        0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F    /* voce di nome lungo: da saltare */

typedef struct {
    char     nome[FAT_NOME_MAX];
    uint32_t dimensione;
    uint32_t primo_cluster;
    uint8_t  attributi;
    uint8_t  is_dir;
} FatDirEntry;

/* Monta il filesystem sul dispositivo a blocchi `blkdev`.
 * Ritorna un handle >= 0, o <0 se il volume non e' un FAT valido. */
int  fat_mount(int blkdev);
int  fat_umount(int mnt);

/* Tipo (12/16/32) e numero di cluster del volume montato, per diagnostica. */
int  fat_tipo(int mnt);
uint32_t fat_n_cluster(int mnt);
const char *fat_etichetta(int mnt);

/* Elenca una directory. `percorso` e' interno al volume ("/" oppure
 * "/BIN"). Ritorna quante voci ha scritto, <0 su errore. */
int  fat_readdir(int mnt, const char *percorso, FatDirEntry *out,
                 uint32_t max, uint32_t start);

/* Cerca un file/directory. Ritorna 0 e riempie `out`, <0 se assente. */
int  fat_stat(int mnt, const char *percorso, FatDirEntry *out);

/* Legge `size` byte dal file a partire da `offset`.
 * Ritorna i byte letti (0 = fine file), <0 su errore. */
int  fat_read(int mnt, const char *percorso, void *buf,
              uint32_t size, uint32_t offset);

/* =============================================================================
 * Scrittura
 *
 * Convenzione dei ritorni: 0 (o i byte scritti) se riuscito, -1 su errore
 * generico, -2 per il caso specifico che il chiamante deve poter
 * distinguere — "esiste gia'" per fat_create/fat_mkdir, "non vuota" per
 * fat_rmdir. Senza quella distinzione il VFS non potrebbe restituire
 * EEXIST e ENOTEMPTY, e ogni errore diverrebbe un generico "non riuscito".
 *
 * NIENTE VA SU DISCO FINCHE' NON SI CHIAMA fat_sync(): la cache e'
 * write-back. E' la stessa scelta di fat12.c, e per la stessa ragione —
 * riversare a ogni operazione costa decine di settori per file e rende
 * inutilizzabile qualunque comando che ne tocchi molti.
 * ============================================================================= */
int  fat_create  (int mnt, const char *percorso);
int  fat_mkdir   (int mnt, const char *percorso);
int  fat_rmdir   (int mnt, const char *percorso);
int  fat_unlink  (int mnt, const char *percorso);
int  fat_write   (int mnt, const char *percorso, const void *buf,
                  uint32_t size, uint32_t offset);
int  fat_truncate(int mnt, const char *percorso, uint32_t nuova_dim);
int  fat_sync    (int mnt);

/* Primo settore (RELATIVO al volume) e lunghezza in settori di un file,
 * per chi deve leggerlo senza saper leggere la FAT — cioe' il settore di
 * avvio. Ritorna -2 se il file e' FRAMMENTATO: un intervallo unico
 * comprenderebbe cluster di altri file. */
int  fat_estensione(int mnt, const char *percorso, uint32_t *lba_rel,
                    uint32_t *n_sett);

#endif /* FAT_H */
