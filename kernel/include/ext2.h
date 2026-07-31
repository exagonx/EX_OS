/* =============================================================================
 * kernel/include/ext2.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Driver ext2 in SOLA LETTURA, sopra il livello a blocchi.
 *
 * Sola lettura e' una scelta, non una fase incompiuta lasciata a meta'.
 * Un ext2 scrivibile richiede allocatore di blocchi e inode, aggiornamento
 * coerente di due bitmap e tre contatori per ogni operazione, e la
 * gestione di cosa succede se la corrente va via a meta'. Leggere richiede
 * di capire il formato; scrivere richiede di non romperlo mai. Sono due
 * lavori, e farli insieme significa scoprire gli errori del primo dentro i
 * danni del secondo.
 *
 * Cio' che serve OGGI e' leggere: /bin/mkfs sa creare un ext2, e senza un
 * lettore quel volume in EX-OS non si puo' nemmeno guardare.
 *
 * Le scelte e le trappole del formato stanno in kernel/fs/ext2.c.
 * ============================================================================= */

#ifndef EXT2_H
#define EXT2_H

#include "kernel.h"

#define EXT2_MAX_MOUNT      2
#define EXT2_NOME_MAX       60      /* ext2 arriva a 255; qui ci si ferma */
#define EXT2_PERCORSO_MAX   128

typedef struct {
    char     nome[EXT2_NOME_MAX];
    uint32_t dimensione;
    uint32_t inode;
    uint8_t  is_dir;
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

#endif /* EXT2_H */
