/* =============================================================================
 * debug/grafica/anim_04_genio.c  ->  lib/exwin/anim/anim_genio.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * ANIMAZIONE 04 — IMBUTO (riduci a icona verso la barra)
 *
 * La finestra viene risucchiata verso il punto (ix, iy) — l'icona della
 * barra delle applicazioni o il posto dell'icona sulla scrivania (@FIN-ICONA:
 * oggi la riduzione e' istantanea). Ogni riga della finestra parte con un
 * ritardo che dipende da quanto e' lontana dal bersaglio: le righe vicine
 * partono subito e si stringono, quelle lontane seguono, e la sagoma diventa
 * un imbuto. Ogni riga si posa stretta con exa_posa_riga (bilineare in
 * orizzontale); le righe che finiscono una sull'altra si coprono, l'ultima
 * vince. Alla fine l'alfa cala.
 * Per il ripristino (dall'icona alla finestra) si chiama con t = 1000 - t.
 * Durata consigliata: 380 ms.
 * Costo: ~ una riga bilineare per riga di sorgente; nessuna memoria in piu'.
 *
 * Anteprima: anim_04_genio.png, anim_04_genio_fotogrammi.png
 * ============================================================================= */
#include "extanim.h"

void exa_genio(ExTela *d, int x, int y, const unsigned int *src,
               int sw, int sh, int passo, int ix, int iy, int t)
{
    int r;
    int sotto = iy > y + sh / 2;        /* il bersaglio e' sotto: le righe basse partono prima */

    for (r = 0; r < sh; r++) {
        /* ritardo: 0 per la riga piu' vicina al bersaglio, 550 per la piu' lontana */
        int lontana = sotto ? (sh - 1 - r) : r;
        int rit = lontana * 550 / (sh > 1 ? sh - 1 : 1);
        int p = exa_entra_esce(exa_stringi((t * 1550 / 1000) - rit, 0, 1000));
        int yy = exa_lerp(y + r, iy, p);
        int ww = exa_lerp(sw, 6, p);
        int cx = exa_lerp(x + sw / 2, ix, p * p / 1000);   /* il centro scivola dopo */
        int a = 255 - exa_stringi((t - 800) * 255 / 200, 0, 255);

        exa_posa_riga(d, cx - ww / 2, yy, ww, src + r * passo, sw, a);
    }
}
