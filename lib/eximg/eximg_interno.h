/* =============================================================================
 * lib/eximg/eximg_interno.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Quello che i decodificatori vedono, e che chi usa la libreria non vede.
 * ============================================================================= */
#ifndef EXIMG_INTERNO_H
#define EXIMG_INTERNO_H

#include "eximg.h"

/* ! IL LATO MASSIMO SI DICHIARA QUI, IN UN POSTO SOLO. Serve prima di
 * allocare: larghezza e altezza vengono dal file, e `larg * alt * 4` con due
 * numeri a 32 bit scelti da chi ha scritto il file trabocca senza fatica —
 * l'allocazione riuscirebbe piccola e la scrittura andrebbe oltre. A 8192 il
 * prodotto sta comodo in 32 bit, e un'immagine piu' grande di cosi' non entra
 * comunque in uno schermo di EX-OS. */
#define EXIMG_LATO_MAX      8192u

/* Memoria per la decodifica IN CORSO.
 *
 * ! NON E' UN malloc, ED E' LA DIFFERENZA CHE CONTA. Ogni blocco chiesto qui
 * viene annotato, e quando la decodifica finisce eximg_carica() li restituisce
 * TUTTI tranne quello dei pixel. Un decodificatore ha molte uscite — un pezzo
 * troncato, una profondita' che non si sa leggere, un albero di Huffman
 * incoerente — e chiedere a ognuna di ricordarsi cosa liberare vuol dire che
 * prima o poi una se ne dimentica. Qui non se ne puo' dimenticare: non e' lei
 * a liberare.
 *
 * Rende 0 se la memoria non c'e' o se i blocchi sono finiti. */
void *eximg_memoria(unsigned int byte);

/* Un decodificatore: rende 1 se ha riconosciuto il formato E l'ha letto, 0 se
 * non e' suo o se il file e' guasto.
 *
 * ! LE DUE RISPOSTE STANNO NELLO STESSO 0, ed e' voluto. Distinguere «non e'
 * mio» da «e' mio ma e' rotto» vorrebbe dire che il primo decodificatore che
 * si sbaglia sul riconoscimento impedisce agli altri di provare. */
typedef int (*EximgDecodificatore)(const unsigned char *d, unsigned int n,
                                   EximgBitmap *bm);

int eximg_png(const unsigned char *d, unsigned int n, EximgBitmap *bm);
int eximg_jpg(const unsigned char *d, unsigned int n, EximgBitmap *bm);
int eximg_ico(const unsigned char *d, unsigned int n, EximgBitmap *bm);

#endif /* EXIMG_INTERNO_H */
