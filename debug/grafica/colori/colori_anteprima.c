/* =============================================================================
 * debug/grafica/colori/colori_anteprima.c  ->  exwin/bin/pm/colori_anteprima.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * L'ANTEPRIMA DEI COLORI nel pannello Impostazioni > Colori di pm
 *
 * Una scrivania in miniatura con una finestra inattiva dietro e una attiva
 * davanti (barra, titolo, chiusura, testo, casella, pulsante, lista con la
 * riga scelta, suggerimento), disegnata col tema ricavato dallo schema che
 * si sta modificando. Mentre disegna, registra le ZONE: dove sta ogni
 * elemento, perche' un clic sull'anteprima scelga quell'elemento
 * nell'elenco (clic sulla barra attiva = "Barra attiva", sul fondo =
 * "Scrivania" e cosi' via), come nel vecchio pannello "Aspetto".
 *
 * Solo extema: compila in pm e sull'host (anteprima_colori.c, che ne fa il
 * PNG del pannello).
 * ============================================================================= */
#include "excolori.h"
#include "colori_anteprima.h"

static void zona(ColZone *z, int x, int y, int w, int h, int el)
{
    if (!z || z->n >= COLZONE_MAX) return;
    z->z[z->n].x = x; z->z[z->n].y = y; z->z[z->n].w = w; z->z[z->n].h = h;
    z->z[z->n].el = el;
    z->n++;
}

int colori_zona_a(const ColZone *z, int x, int y)
{
    int i;
    /* dall'ultima registrata: quelle disegnate dopo stanno sopra */
    for (i = z->n - 1; i >= 0; i--)
        if (x >= z->z[i].x && y >= z->z[i].y &&
            x < z->z[i].x + z->z[i].w && y < z->z[i].y + z->z[i].h) return z->z[i].el;
    return -1;
}

void colori_anteprima(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                      int scelto, ColZone *z)
{
    static const char *const righe[] = { "Documenti", "Immagini", "Musica", "Progetti" };
    int cx, cy, cw, ch;
    int sp, bh = m->f.barra_h;
    int ax = x + 34, ay = y + 40, aw = w - 48, ah = h - 54;   /* la finestra attiva */
    ExTela v = *t;                                             /* il ritaglio di prima */

    if (z) z->n = 0;
    ext_ritaglio(t, x, y, w, h);
    if (v.cx0 > t->cx0) t->cx0 = v.cx0;
    if (v.cy0 > t->cy0) t->cy0 = v.cy0;
    if (v.cx1 < t->cx1) t->cx1 = v.cx1;
    if (v.cy1 < t->cy1) t->cy1 = v.cy1;

    /* la scrivania: sopra e sotto sono due elementi diversi */
    ext_sfuma_v(t, x, y, w, h, m->c.desktop_a, m->c.desktop_b);
    zona(z, x, y, w, h / 2, EXC_SCRIVANIA);
    zona(z, x, y + h / 2, w, h - h / 2, EXC_SCRIVANIA2);

    /* la finestra inattiva */
    ext_finestra(t, m, x + 8, y + 8, w * 6 / 10, 90, "Inattiva", 0);
    ext_finestra_client(m, x + 8, y + 8, w * 6 / 10, 90, &cx, &cy, &cw, &ch);
    sp = cx - (x + 8);
    zona(z, x + 8 + sp, y + 8 + sp, cw / 2, bh, EXC_BARRA_INA);
    zona(z, x + 8 + sp + cw / 2, y + 8 + sp, cw - cw / 2, bh, EXC_BARRA_INA2);
    zona(z, x + 8 + sp + 4, y + 8 + sp, ext_larg("Inattiva") + 4, bh, EXC_TITOLO_INA);

    /* la finestra attiva */
    ext_finestra(t, m, ax, ay, aw, ah, "Attiva", EXT_ATTIVA);
    ext_finestra_client(m, ax, ay, aw, ah, &cx, &cy, &cw, &ch);
    sp = cx - ax;
    zona(z, ax, ay, aw, ah, EXC_FINESTRA);
    zona(z, ax, ay, sp, ah, EXC_LUCE);                       /* il bordo sinistro: luce */
    zona(z, ax + aw - sp, ay, sp, ah, EXC_OMBRA);            /* il destro: ombra */
    zona(z, cx, ay + sp, cw / 2, bh, EXC_BARRA);
    zona(z, cx + cw / 2, ay + sp, cw - cw / 2, bh, EXC_BARRA2);
    zona(z, cx + 4, ay + sp, ext_larg("Attiva") + 4, bh, EXC_TITOLO);
    zona(z, cx + cw - 3 * (bh - 4), ay + sp, 3 * (bh - 4), bh, EXC_CHIUDI);

    ext_testo(t, cx + 8, cy + 8, "Testo della finestra", m->c.testo);
    zona(z, cx + 8, cy + 8, ext_larg("Testo della finestra"), 16, EXC_TESTO);

    ext_casella(t, m, cx + 8, cy + 30, cw / 2 - 12, 22, "Casella", -1, 0);
    zona(z, cx + 8, cy + 30, cw / 2 - 12, 22, EXC_CAMPO);
    zona(z, cx + 14, cy + 33, ext_larg("Casella"), 16, EXC_CAMPO_TESTO);

    ext_pulsante(t, m, cx + cw / 2 + 4, cy + 28, cw / 2 - 12, 26, "Pulsante", 0);
    zona(z, cx + cw / 2 + 4, cy + 28, 2, 26, EXC_LUCE);
    zona(z, cx + cw - 10, cy + 28, 2, 26, EXC_OMBRA);

    ext_lista(t, m, cx + 8, cy + 60, cw - 16, ch - 68, righe, 4, 1, -1, 0, 0);
    zona(z, cx + 8, cy + 60, cw - 16, ch - 68, EXC_CAMPO);
    zona(z, cx + 10, cy + 63 + EXT_RIGA_H, cw - 20, EXT_RIGA_H, EXC_SELEZIONE);
    zona(z, cx + 18, cy + 65 + EXT_RIGA_H, ext_larg("Immagini"), 16, EXC_SELEZIONE_TESTO);

    ext_suggerimento(t, m, cx + cw - 118, cy + ch - 30, "Suggerimento");
    zona(z, cx + cw - 118, cy + ch - 30, ext_larg("Suggerimento") + 14, 22, EXC_SUGGERIMENTO);

    /* l'elemento scelto si vede: un tratteggio attorno alla sua prima zona */
    if (z && scelto >= 0) {
        int i;
        for (i = 0; i < z->n; i++)
            if (z->z[i].el == scelto) {
                int k, zx = z->z[i].x - 2, zy = z->z[i].y - 2, zw = z->z[i].w + 4, zh = z->z[i].h + 4;
                for (k = 0; k < zw; k += 4) {
                    ext_riempi(t, zx + k, zy, 2, 1, 0x00FF00FFu);
                    ext_riempi(t, zx + k, zy + zh - 1, 2, 1, 0x00FF00FFu);
                }
                for (k = 0; k < zh; k += 4) {
                    ext_riempi(t, zx, zy + k, 1, 2, 0x00FF00FFu);
                    ext_riempi(t, zx + zw - 1, zy + k, 1, 2, 0x00FF00FFu);
                }
                break;
            }
    }
    t->cx0 = v.cx0; t->cy0 = v.cy0; t->cx1 = v.cx1; t->cy1 = v.cy1;
}

/* Le sedici tinte rapide sotto l'anteprima: un clic mette quella tinta
 * nell'elemento scelto. Le stesse della tavolozza VGA, piu' morbide. */
const unsigned int g_colori_rapidi[16] = {
    0x000000, 0x404040, 0x808080, 0xC0C0C0, 0xD4D0C8, 0xFFFFFF, 0x0A246A, 0x3A6EA5,
    0xA6CAF0, 0x0E7C86, 0x2E6B30, 0x7DB46C, 0xA0522D, 0xE3D5B8, 0x8C1C3C, 0xE07A5F,
};

void colori_rapidi(ExTela *t, int x, int y, ColZone *z)
{
    int i;

    for (i = 0; i < 16; i++) {
        int px = x + (i % 8) * 26, py = y + (i / 8) * 22;
        ext_riempi(t, px, py, 22, 18, g_colori_rapidi[i]);
        ext_rilievo(t, px - 1, py - 1, 24, 20, 0x00808080u, 0x00FFFFFFu);
        if (z && z->n < COLZONE_MAX) zona(z, px, py, 22, 18, 100 + i);    /* 100+i = tinta i */
    }
}
