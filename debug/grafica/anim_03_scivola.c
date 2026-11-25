/* =============================================================================
 * debug/grafica/anim_03_scivola.c  ->  lib/exwin/anim/anim_scivola.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * ANIMAZIONE 03 — SCIVOLA (dal basso, con la molla)
 *
 * La finestra sale di 48 pixel verso il suo posto e si ferma con una piccola
 * oscillazione smorzata (exa_molla). Adatta ai dialoghi: si capisce da dove
 * arrivano senza coprire la finestra madre durante il moto. L'alfa sale nei
 * primi 40% del tempo.
 * Durata consigliata: 280 ms.
 * Costo: come la 01 (nessuna scala), piu' il ridisegno della striscia
 * scoperta sotto: il server la sporca con sporca() a ogni fotogramma.
 *
 * Anteprima: anim_03_scivola.png, anim_03_scivola_fotogrammi.png
 * ============================================================================= */
#include "extanim.h"

void exa_scivola(ExTela *d, int x, int y, const unsigned int *src,
                 int sw, int sh, int passo, int t)
{
    int dy = (1000 - exa_molla(t)) * 48 / 1000;
    int a = exa_esce(exa_stringi(t * 5 / 2, 0, 1000));

    exa_posa(d, x, y + dy, src, sw, sh, passo, a * 255 / 1000);
}
