/* =============================================================================
 * debug/grafica/anim_08_tendina.c  ->  lib/exwin/anim/anim_tendina.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * ANIMAZIONE 08 — TENDINA (un menu che si srotola)
 *
 * Il menu (gia' disegnato fuori schermo nella sua finestra EX_POPUP) si
 * rivela dall'alto verso il basso: l'altezza visibile segue la curva
 * d'uscita, e le ultime 10 righe rivelate sfumano invece di tagliare netto.
 * Il contenuto non si muove (e' la tenda che si alza, non il menu che
 * scende): le voci restano dove il mouse le trovera'.
 * Durata consigliata: 140 ms (un menu lento e' un menu odiato). Anche per
 * l'elenco dell'elenco a discesa (CL_COMBO) e per i menu contestuali.
 *
 * Anteprima: anim_08_tendina.png, anim_08_tendina_fotogrammi.png
 * ============================================================================= */
#include "extanim.h"

void exa_tendina(ExTela *d, int x, int y, const unsigned int *src,
                 int sw, int sh, int passo, int t)
{
    int vis = sh * exa_esce(t) / 1000, j, sfuma = 10;

    for (j = 0; j < vis; j++) {
        int a = 255;
        if (vis < sh && j > vis - sfuma) a = 255 * (vis - j) / sfuma;
        exa_posa(d, x, y + j, src + j * passo, sw, 1, passo, a);
    }
}
