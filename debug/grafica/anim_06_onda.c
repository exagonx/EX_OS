/* =============================================================================
 * debug/grafica/anim_06_onda.c  ->  lib/exwin/anim/anim_onda.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * ANIMAZIONE 06 — ONDA (il clic su un pulsante)
 *
 * Dal punto del clic (px, py) parte un cerchio che si allarga fino a coprire
 * il pulsante e intanto sbiadisce. Resta dentro la sagoma del pulsante: il
 * ritaglio e' il rettangolo, gli angoli arrotondati li rispetta
 * ext_tondo_alfa disegnando l'onda come intersezione (cerchio pieno
 * disegnato riga per riga dentro la forma).
 * Il colore e' l'accento chiaro del tema (o bianco sui pulsanti pieni).
 * Durata consigliata: 450 ms; il pulsante si ridisegna normale sotto a ogni
 * fotogramma (ext_pulsante), l'onda si disegna sopra.
 *
 * Dove: toolkit, CL_PULSANTE. Su EXM_MOUSE_DOWN si salvano (px, py) e
 * l'istante; disegna_oggetto_nudo, se l'onda e' viva, chiama questa dopo il
 * pulsante e chiede un altro fotogramma (vedi implementazione_ANIM001.txt,
 * "il timer dei fotogrammi nel toolkit").
 *
 * Anteprima: anim_06_onda.png, anim_06_onda_fotogrammi.png
 * ============================================================================= */
#include "extanim.h"

void exa_onda(ExTela *d, const ExTema *m, int x, int y, int w, int h,
              int px, int py, int t)
{
    int dxm = (px - x > x + w - px) ? px - x : x + w - px;
    int dym = (py - y > y + h - py) ? py - y : y + h - py;
    int rmax = 0, r, a, j;
    int sx0 = d->cx0, sy0 = d->cy0, sx1 = d->cx1, sy1 = d->cy1;
    unsigned int c = ext_mescola(m->c.accento_chiaro, 0x00FFFFFFu, 120);

    while (rmax * rmax < dxm * dxm + dym * dym) rmax += 4;
    r = rmax * exa_esce(t) / 1000;
    a = 150 * (1000 - exa_entra_esce(t)) / 1000;
    if (r <= 0 || a <= 0) return;

    /* dentro il pulsante, e dentro il ritaglio che c'era */
    if (d->cx0 < x) d->cx0 = x;
    if (d->cy0 < y) d->cy0 = y;
    if (d->cx1 > x + w) d->cx1 = x + w;
    if (d->cy1 > y + h) d->cy1 = y + h;

    for (j = py - r; j <= py + r; j++) {
        /* mezza corda del cerchio alla riga j */
        int dy = j - py, k = 0;
        while ((k + 1) * (k + 1) + dy * dy <= r * r) k++;
        if (j < d->cy0 || j >= d->cy1) continue;
        {
            int r0 = d->cy0, r1 = d->cy1;
            d->cy0 = j; d->cy1 = j + 1;
            /* la forma del pulsante (angoli) tagliata alla corda */
            {
                int c0 = d->cx0, c1 = d->cx1;
                if (d->cx0 < px - k) d->cx0 = px - k;
                if (d->cx1 > px + k + 1) d->cx1 = px + k + 1;
                ext_tondo_alfa(d, x, y, w, h, m->f.raggio, c, a);
                d->cx0 = c0; d->cx1 = c1;
            }
            d->cy0 = r0; d->cy1 = r1;
        }
    }
    d->cx0 = sx0; d->cy0 = sy0; d->cx1 = sx1; d->cy1 = sy1;
}
