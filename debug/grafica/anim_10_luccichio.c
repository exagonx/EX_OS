/* =============================================================================
 * debug/grafica/anim_10_luccichio.c  ->  lib/exwin/anim/anim_luccichio.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * ANIMAZIONE 10 — LUCCICHIO (attesa senza misura)
 *
 * Per le barre di progresso che non sanno quanto manca (download senza
 * Content-Length in EXBrowser, compilazione in exide, netupdate che scarica
 * l'elenco: @NETUPD-GIRA): una fascia di luce larga un terzo della barra la
 * attraversa da sinistra a destra, con i bordi sfumati, sopra il campo
 * pieno dell'accento a meta' intensita'. CICLICA, periodo 1200 ms.
 * Variante determinata: chiamarla dopo ext_progresso con lo stesso
 * rettangolo, ritagliata alla parte piena, da' il "riflesso che corre" sulle
 * barre normali.
 *
 * Anteprima: anim_10_luccichio.png, anim_10_luccichio_fotogrammi.png
 * ============================================================================= */
#include "extanim.h"

void exa_luccichio(ExTela *d, const ExTema *m, int x, int y, int w, int h, int t)
{
    int fascia = w / 3, i;
    int cx = exa_lerp(x - fascia, x + w + fascia, exa_entra_esce(t));
    int r = m->f.raggio > h / 2 ? h / 2 : m->f.raggio;
    int s0 = d->cx0, s1 = d->cx1;

    /* il binario e il fondo a meta' intensita' */
    ext_tondo_pieno(d, x, y, w, h, r, m->c.campo, m->c.campo);
    ext_tondo_bordo(d, x, y, w, h, r, 1, m->c.bordo);
    ext_tondo_alfa(d, x + 2, y + 2, w - 4, h - 4, r > 2 ? r - 2 : 0, m->c.progresso_a, 90);

    /* la fascia: colonne con alfa a campana (seno da 0 a 180 gradi) */
    for (i = -fascia; i <= fascia; i++) {
        int col = cx + i, a;
        if (col < x + 2 || col >= x + w - 2) continue;
        a = 230 * ext_seno((i + fascia) * 180 / (2 * fascia)) / 1000;
        if (d->cx0 < col) d->cx0 = col;
        if (d->cx1 > col + 1) d->cx1 = col + 1;
        ext_tondo_alfa(d, x + 2, y + 2, w - 4, h - 4, r > 2 ? r - 2 : 0, m->c.progresso_b, a);
        ext_tondo_alfa(d, x + 2, y + 2, w - 4, (h - 4) / 2, r > 2 ? r - 2 : 0, 0x00FFFFFFu, a / 3);
        d->cx0 = s0; d->cx1 = s1;
    }
}
