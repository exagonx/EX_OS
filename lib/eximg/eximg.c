/* =============================================================================
 * lib/eximg/eximg.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La porta della libreria: la tabella dei decodificatori e la memoria.
 *
 * ! LA TABELLA E' LA STESSA IDEA DI QUELLA DI exwin.c, un piano piu' sotto.
 * Aggiungere JPG o ICO vuol dire scrivere un file e mettere una riga qui: non
 * si tocca eximg.h, non si tocca il toolkit, e chi disegna immagini non si
 * accorge di niente.
 * ============================================================================= */

#include <stdlib.h>
#include "eximg_interno.h"

/* -----------------------------------------------------------------------------
 * La memoria della decodifica in corso
 *
 * ! OTTO BLOCCHI BASTANO E IL LIMITE E' DICHIARATO. Il PNG ne chiede tre — gli
 * IDAT concatenati, le righe grezze, i pixel — e un decodificatore che ne
 * volesse piu' di otto sta facendo qualcosa che va guardato, non allargato.
 *
 * ! E NON E' RIENTRANTE, ED E' UNA SCELTA. Decodificare due immagini insieme
 * vorrebbe dire un contesto passato per parametro attraverso ogni funzione dei
 * decodificatori. Qui non c'e' nessuno che lo faccia: un'applicazione grafica
 * di EX-OS ha un filo solo, e le immagini le carica una per volta.
 * --------------------------------------------------------------------------- */
#define BLOCCHI_MAX     8

static void        *g_blocchi[BLOCCHI_MAX];
static unsigned int g_quanti = 0;

void *eximg_memoria(unsigned int byte)
{
    void *p;

    if (byte == 0 || g_quanti >= BLOCCHI_MAX) return 0;

    p = malloc(byte);
    if (!p) return 0;

    g_blocchi[g_quanti++] = p;
    return p;
}

/* Restituisce tutto quello che la decodifica ha chiesto, tranne `tieni`. */
static void restituisci(void *tieni)
{
    unsigned int i;

    for (i = 0; i < g_quanti; i++) {
        if (g_blocchi[i] && g_blocchi[i] != tieni) free(g_blocchi[i]);
        g_blocchi[i] = 0;
    }
    g_quanti = 0;
}

/* -----------------------------------------------------------------------------
 * I decodificatori, in tabella
 * --------------------------------------------------------------------------- */
static const EximgDecodificatore g_decodificatori[] = {
    eximg_png,
    eximg_jpg,
    eximg_gif,
    eximg_ico,
    0
};

int eximg_carica(const unsigned char *dati, unsigned int n, EximgBitmap *bm)
{
    int k;

    if (!bm) return 0;

    bm->larghezza = 0;
    bm->altezza   = 0;
    bm->px        = 0;

    if (!dati || n == 0) return 0;

    /* ! IL REGISTRO SI AZZERA QUI E NON ALLA FINE. Se una decodifica
     * precedente fosse finita per una strada che non passa di qui — non ne
     * esistono oggi, ma «oggi» scade — i suoi blocchi resterebbero annotati e
     * il primo decodificatore troverebbe il registro pieno. */
    g_quanti = 0;

    for (k = 0; g_decodificatori[k]; k++) {
        if (g_decodificatori[k](dati, n, bm) && bm->px) {
            restituisci(bm->px);        /* i temporanei se ne vanno */
            return 1;
        }

        /* Non era suo, o era suo ed e' guasto: in tutti e due i casi cio' che
         * ha chiesto va restituito prima che provi il prossimo. */
        restituisci(0);
        bm->larghezza = 0;
        bm->altezza   = 0;
        bm->px        = 0;
    }

    return 0;
}

void eximg_libera(EximgBitmap *bm)
{
    if (!bm || !bm->px) return;

    free(bm->px);
    bm->px        = 0;
    bm->larghezza = 0;
    bm->altezza   = 0;
}
