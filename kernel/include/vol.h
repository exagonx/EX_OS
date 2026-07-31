/* =============================================================================
 * kernel/include/vol.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Riconoscimento del filesystem dentro una partizione. Vedi
 * kernel/block/vol.c per la regola di determinazione del tipo di FAT, che
 * e' il punto in cui e' piu' facile — e piu' costoso — sbagliare.
 * ============================================================================= */

#ifndef VOL_H
#define VOL_H

#include "kernel.h"

#define VOL_FS_SCONOSCIUTO  0
#define VOL_FS_EXT2         2
#define VOL_FS_FAT12        12
#define VOL_FS_FAT16        16
#define VOL_FS_FAT32        32
#define VOL_FS_ILLEGGIBILE  255   /* errore di lettura, non "nessun fs" */

typedef struct {
    uint32_t tipo;          /* VOL_FS_* */
    uint32_t incoerente;    /* 1 = i campi contraddicono il tipo dedotto */

    uint32_t byts_per_sec;
    uint32_t sett_per_clu;
    uint32_t n_fat;
    uint32_t riservati;
    uint32_t sett_per_fat;
    uint32_t root_entries;  /* 0 su FAT32 */
    uint32_t tot_settori;
    uint32_t n_cluster;     /* cluster dell'area dati: DECIDE il tipo */

    uint32_t root_cluster;  /* solo FAT32 */
    uint32_t fsinfo;        /* solo FAT32 */

    char     etichetta[12];
} VolumeInfo;

/* Legge il settore di avvio della partizione e ne deduce il filesystem.
 * Ritorna 0 se ha potuto guardare (anche se il risultato e'
 * "sconosciuto"), <0 se non e' riuscito nemmeno a leggere. */
/* Identifica il filesystem sul dispositivo a blocchi `blkdev`, leggendone
 * il settore 0 RELATIVO. La lunghezza viene dal dispositivo stesso: non
 * la si passa piu' da fuori, cosi' non puo' divergere dalla finestra
 * vera. */
int vol_identifica(int blkdev, VolumeInfo *out);

#endif /* VOL_H */
