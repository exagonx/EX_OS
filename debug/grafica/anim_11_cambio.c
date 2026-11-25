/* =============================================================================
 * debug/grafica/anim_11_cambio.c  ->  lib/exwin/anim/anim_cambio.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * ANIMAZIONE 11 — CAMBIO DI PAGINA (linguette, pannelli, modali che si
 *                 avvicendano)
 *
 * Dissolvenza incrociata fra l'immagine VECCHIA e quella NUOVA dello stesso
 * rettangolo, con la nuova che entra spostata di 24 px dal lato di `verso`
 * (+1 da destra: linguetta successiva; -1 da sinistra: precedente; 0 senza
 * spostamento: modale che sostituisce modale).
 *
 * ! E' ANCHE LA CURA ESTETICA DEL "REFRESH SPORCO" FRA DUE MODALI di exide
 * (bug_EXIDE002): oggi chiudendo un modale e aprendone un altro si vede per
 * qualche fotogramma la finestra madre ridisegnata a meta'. Con questa, il
 * server tiene l'ultima immagine buona del rettangolo (vecchia) e la
 * dissolve nella nuova SOLO quando il client ha consegnato un fotogramma
 * completo (WIN_MSG_AGGIORNA): durante il ridisegno non si vede niente di
 * mezzo. La correzione vera (il ridisegno ordinato) sta nel bug; questa e'
 * la transizione che la rende morbida.
 * Durata consigliata: 180 ms.
 *
 * Anteprima: anim_11_cambio.png, anim_11_cambio_fotogrammi.png
 * ============================================================================= */
#include "extanim.h"

void exa_cambio(ExTela *d, int x, int y, const unsigned int *vecchia,
                const unsigned int *nuova, int sw, int sh, int passo,
                int verso, int t)
{
    int p = exa_esce(t);
    int dx = verso * (1000 - p) * 24 / 1000;
    int s0 = d->cx0, s1 = d->cx1;

    exa_posa(d, x, y, vecchia, sw, sh, passo, 255);
    /* la nuova resta dentro il rettangolo anche mentre scorre */
    if (d->cx0 < x) d->cx0 = x;
    if (d->cx1 > x + sw) d->cx1 = x + sw;
    exa_posa(d, x + dx, y, nuova, sw, sh, passo, p * 255 / 1000);
    d->cx0 = s0; d->cx1 = s1;
}
