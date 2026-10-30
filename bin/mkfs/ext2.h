/* =============================================================================
 * bin/mkfs/ext2.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Creazione di un filesystem ext2, ext3 o ext4. Le scelte e il perche' stanno in
 * bin/mkfs/ext2.c.
 *
 * L'interfaccia e' divisa in tre perche' lo e' anche il comando: prima si
 * calcola cosa si farebbe, poi lo si mostra, poi — solo se l'utente
 * conferma — lo si scrive. Un formattatore che calcola e scrive nella
 * stessa chiamata non puo' far vedere niente prima di aver gia' fatto.
 * ============================================================================= */

#ifndef MKFS_EXT2_H
#define MKFS_EXT2_H

typedef struct {
    unsigned int gen;               /* 2, 3 o 4: quale ext */
    unsigned int bs;                /* byte per blocco */
    unsigned int dim_inode;         /* byte per inode */
    unsigned int blocchi;           /* blocchi totali */
    unsigned int gruppi;
    unsigned int blocchi_per_gruppo;
    unsigned int inode_per_gruppo;
    unsigned int inode_totali;
    unsigned int gdt_blocchi;       /* blocchi della tabella dei descrittori */
    unsigned int itab_blocchi;      /* blocchi della tabella inode, per gruppo */
    unsigned int riservati;         /* blocchi riservati a root (5%) */
    unsigned int liberi;            /* blocchi liberi a formattazione finita */
    unsigned int giornale;          /* blocchi del giornale, 0 su ext2 */
    unsigned int giornale_ind;      /* blocchi di puntatori, solo ext3 */
    unsigned int giornale_gruppo;   /* il gruppo che lo contiene */
    unsigned int giornale_primo;    /* il primo blocco, puntatori compresi */
} Ext2Geo;

/* Calcola la geometria. Ritorna 0, o <0 dopo aver stampato il motivo. */
int  ext2_piano(Ext2Geo *g, unsigned int settori, unsigned int gen);

/* Mostra cosa verra' scritto. */
void ext2_mostra(const Ext2Geo *g);

/* Scrive. `toccato` viene messo a 1 appena una scrittura va a segno, cosi'
 * chi chiama puo' distinguere "non e' successo niente" da "il volume e' a
 * meta'". Ritorna 0, o l'errno negativo della scrittura fallita. */
int  ext2_scrivi(const char *dev, const Ext2Geo *g, const char *etichetta,
                 unsigned int uuid_seme, int *toccato);

#endif /* MKFS_EXT2_H */
