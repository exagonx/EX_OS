/* =============================================================================
 * debug/grafica/anim_05_cristallo.c  ->  lib/exwin/anim/anim_cristallo.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * ANIMAZIONE 05 — CRISTALLO (dal logo: la finestra si apre come una gemma)
 *
 * La finestra si rivela dentro un esagono che cresce dal centro (la forma
 * della piattaforma del logo); il bordo dell'esagono e' una linea ciano col
 * suo alone, e una scintilla bianca corre lungo il bordo. Quando l'esagono
 * ha coperto tutta la finestra la linea si dissolve e resta la finestra.
 * Pensata per il tema Cristallo; con gli altri temi usa il loro colore
 * `fuoco`.
 * Durata consigliata: 420 ms (e' la piu' lunga: per l'avvio della sessione e
 * per le finestre principali, non per i dialoghi).
 *
 * Il test "dentro l'esagono" e' intero: esagono a punte laterali di raggio
 * R, centrato: |dy| <= R*0.866 e |dx|*0.866 + |dy|*0.5 <= R*0.866 (in
 * millesimi: 866 e 500).
 *
 * Anteprima: anim_05_cristallo.png, anim_05_cristallo_fotogrammi.png
 * ============================================================================= */
#include "extanim.h"

void exa_cristallo(ExTela *d, const ExTema *m, int x, int y,
                   const unsigned int *src, int sw, int sh, int passo, int t)
{
    int cx = x + sw / 2, cy = y + sh / 2;
    /* il raggio che copre l'angolo piu' lontano, con margine: R*0.866 >= meta' diagonale */
    int rmax = (sw / 2 + sh * 577 / 1000) + 8;
    int p = exa_esce(exa_stringi(t * 1000 / 820, 0, 1000));   /* finisce all'82% */
    int R = rmax * p / 1000;
    int i, j;
    unsigned int luce = m->c.fuoco;

    /* la finestra, dentro l'esagono */
    for (j = 0; j < sh; j++) {
        int dy = y + j - cy, ady = dy < 0 ? -dy : dy;

        if (ady * 1000 > R * 866) continue;
        for (i = 0; i < sw; i++) {
            int dx = x + i - cx, adx = dx < 0 ? -dx : dx;
            if (adx * 866 + ady * 500 <= R * 866)
                ext_punto(d, x + i, y + j, src[j * passo + i], 255);
        }
    }

    /* il bordo luminoso, che sparisce nell'ultimo 25% */
    {
        int a = 255 - exa_stringi((t - 750) * 255 / 250, 0, 255);

        if (a > 0 && R > 4) {
            /* la scintilla: gira di 300 gradi lungo il bordo durante l'apertura */
            int g = t * 300 / 1000;
            int sx = cx + R * ext_coseno(g) / 1000;
            int sy = cy + R * ext_seno(g) / 1000;

            ext_esagono(d, cx, cy, R, R, 2, luce, a);
            ext_esagono(d, cx, cy, R + 3, R + 3, 1, luce, a / 3);
            if (a > 60) ext_scintilla(d, sx, sy, 8, 0x00FFFFFFu);
        }
    }
}
