/* =============================================================================
 * debug/grafica/anim_02_zoom.c  ->  lib/exwin/anim/anim_zoom.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * ANIMAZIONE 02 — ZOOM (la finestra si avvicina)
 *
 * In apertura la finestra parte all'86% della sua misura, centrata sul
 * posto finale, e cresce fino al 100% mentre si dissolve dentro; con la
 * curva "rimbalzo" supera di poco il 100% e torna, come un oggetto che si
 * posa. In chiusura il contrario, senza rimbalzo, verso l'80%.
 * Durata consigliata: 220 ms in apertura, 150 ms in chiusura.
 * Costo: bilineare su tutta la finestra (4 letture e 3 fusioni per pixel):
 * su macchine lente usare la 01.
 *
 * Anteprima: anim_02_zoom.png, anim_02_zoom_fotogrammi.png
 * ============================================================================= */
#include "extanim.h"

void exa_zoom(ExTela *d, int x, int y, const unsigned int *src,
              int sw, int sh, int passo, int t, int apre)
{
    int s, a, dw, dh;

    if (apre) {
        s = exa_lerp(860, 1000, exa_rimbalzo(t));     /* scala in millesimi */
        a = exa_esce(exa_stringi(t * 2, 0, 1000));      /* opaca a meta' tempo */
    } else {
        s = exa_lerp(1000, 800, exa_entra_esce(t));
        a = 1000 - exa_esce(t);
    }
    dw = sw * s / 1000;
    dh = sh * s / 1000;
    exa_posa_scalata(d, x + (sw - dw) / 2, y + (sh - dh) / 2, dw, dh,
                     src, sw, sh, passo, a * 255 / 1000);
}
