/* =============================================================================
 * lib/exgioco/carte.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * French playing cards, drawn by program (@GIOCHI, 3 October 2026).
 *
 * ! NO PICTURES ON DISK: the four suits are shapes given as tests on the unit
 * square (forma_cuori and the others), sampled at the size the game asks.
 * Klondike wants 71 x 96 and Spider, with ten columns, a smaller card; one
 * set of pictures would be scaled, and a scaled heart of 12 pixels is a blot.
 *
 * Each face is drawn the first time it is shown and kept: 53 pictures of
 * 71 x 96 are 1.4 MB, drawn only for the cards that actually appear.
 * ============================================================================= */

#include "libc.h"
#include "exgioco.h"

#define N_FACCE 53                  /* 52 faces and the back, at index 52 */

static int   g_cw = 71, g_ch = 96;
static Tela  g_cache[N_FACCE];
static ExFont g_f_angolo, g_f_figura;

static const char *NOMI_VALORE[13] = {
    "A", "2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K"
};

#define ROSSO_CARTA  0xC81E1E
#define NERO_CARTA   0x141414

/* --- The suits, as shapes on the unit square ----------------------------- */
static int nel_cerchio(float u, float v, float cx, float cy, float r)
{
    return (u - cx) * (u - cx) + (v - cy) * (v - cy) <= r * r;
}

static float assoluto(float x) { return x < 0 ? -x : x; }

static int forma_quadri(float u, float v)
{
    return assoluto(u - 0.5f) / 0.42f + assoluto(v - 0.5f) / 0.5f <= 1.0f;
}

static int forma_cuori(float u, float v)
{
    if (nel_cerchio(u, v, 0.28f, 0.31f, 0.24f) || nel_cerchio(u, v, 0.72f, 0.31f, 0.24f))
        return 1;
    return v >= 0.33f && v <= 0.97f && assoluto(u - 0.5f) <= 0.49f * (0.97f - v) / 0.64f;
}

/* The stem of picche and fiori: a little foot that widens downwards. */
static int gambo(float u, float v)
{
    return v >= 0.55f && v <= 0.98f && assoluto(u - 0.5f) <= 0.05f + (v - 0.55f) * 0.42f;
}

static int forma_picche(float u, float v)
{
    if (nel_cerchio(u, v, 0.29f, 0.58f, 0.22f) || nel_cerchio(u, v, 0.71f, 0.58f, 0.22f))
        return 1;
    if (v >= 0.02f && v <= 0.6f && assoluto(u - 0.5f) <= 0.48f * (v - 0.02f) / 0.58f)
        return 1;
    return gambo(u, v);
}

static int forma_fiori(float u, float v)
{
    return nel_cerchio(u, v, 0.5f, 0.25f, 0.215f) ||
           nel_cerchio(u, v, 0.27f, 0.56f, 0.215f) ||
           nel_cerchio(u, v, 0.73f, 0.56f, 0.215f) ||
           (v > 0.3f && v < 0.62f && assoluto(u - 0.5f) < 0.1f) ||
           gambo(u, v);
}

static const Forma FORME[4] = { forma_picche, forma_cuori, forma_quadri, forma_fiori };

static void seme(Tela *t, int seme, int cx, int cy, int lato)
{
    tela_forma(t, cx - lato / 2, cy - lato / 2, lato, lato, FORME[seme],
               (seme == 1 || seme == 2) ? ROSSO_CARTA : NERO_CARTA);
}

/* --- Where the pips of 2..10 go: columns 0 1 2, rows in 1/8 of the field -- */
typedef struct { signed char col, riga; } Seme;
static const Seme PIP2[]  = { {1,0}, {1,8} };
static const Seme PIP3[]  = { {1,0}, {1,4}, {1,8} };
static const Seme PIP4[]  = { {0,0}, {2,0}, {0,8}, {2,8} };
static const Seme PIP5[]  = { {0,0}, {2,0}, {1,4}, {0,8}, {2,8} };
static const Seme PIP6[]  = { {0,0}, {2,0}, {0,4}, {2,4}, {0,8}, {2,8} };
static const Seme PIP7[]  = { {0,0}, {2,0}, {1,2}, {0,4}, {2,4}, {0,8}, {2,8} };
static const Seme PIP8[]  = { {0,0}, {2,0}, {1,2}, {0,4}, {2,4}, {1,6}, {0,8}, {2,8} };
static const Seme PIP9[]  = { {0,0}, {2,0}, {0,3}, {2,3}, {1,4}, {0,5}, {2,5}, {0,8}, {2,8} };
static const Seme PIP10[] = { {0,0}, {2,0}, {1,1}, {0,3}, {2,3}, {0,5}, {2,5}, {1,7}, {0,8}, {2,8} };
static const Seme *const PIP[11] = { 0, 0, PIP2, PIP3, PIP4, PIP5, PIP6, PIP7, PIP8, PIP9, PIP10 };

static void faccia(Tela *t, int carta)
{
    int s = SEME(carta), v = VALORE(carta), w = g_cw, h = g_ch;
    unsigned int col = ROSSA(carta) ? ROSSO_CARTA : NERO_CARTA;
    int angolo = w / 5, x0 = w * 35 / 100, x1 = w * 65 / 100, y0, y1;

    tela_tondo(t, 0, 0, w, h, w / 10, 0x7A7A7A);
    tela_tondo(t, 1, 1, w - 2, h - 2, w / 10 - 1, 0xFFFFFF);

    /* The two corners: value and a small suit, top left and bottom right. */
    tela_testo_c(t, g_f_angolo, 3 + angolo / 2, 2, NOMI_VALORE[v], col);
    seme(t, s, 3 + angolo / 2, ex_font_height(g_f_angolo) + 2 + angolo / 2, angolo - 2);
    tela_testo_c(t, g_f_angolo, w - 3 - angolo / 2, h - 3 - ex_font_height(g_f_angolo) - angolo,
                 NOMI_VALORE[v], col);
    seme(t, s, w - 3 - angolo / 2, h - 3 - angolo / 2, angolo - 2);

    y0 = h * 17 / 100;
    y1 = h * 83 / 100;
    if (v == 0) {
        seme(t, s, w / 2, h / 2, w * 46 / 100);
    } else if (v <= 9) {
        const Seme *p = PIP[v + 1];
        int k, lato = w * 20 / 100;

        for (k = 0; k <= v; k++) {
            int cx = p[k].col == 0 ? x0 : p[k].col == 1 ? w / 2 : x1;
            int cy = y0 + (y1 - y0) * p[k].riga / 8;
            seme(t, s, cx, cy, lato);
        }
    } else {
        /* Jack, queen, king: a framed field, the letter and the suit. */
        int fx = angolo + 4, fy = h * 13 / 100, fw = w - 2 * fx, fh = h - 2 * fy;

        tela_rett(t, fx, fy, fw, fh, ROSSA(carta) ? 0xFBE7C6 : 0xE3E9F5);
        tela_bordo(t, fx, fy, fw, fh, col);
        tela_testo_c(t, g_f_figura, w / 2, fy + fh / 2 - ex_font_height(g_f_figura) * 3 / 4,
                     NOMI_VALORE[v], col);
        seme(t, s, w / 2, fy + fh * 3 / 4, fw * 40 / 100);
    }
}

static void dorso(Tela *t)
{
    int w = g_cw, h = g_ch, x, y, m = w / 12;

    tela_tondo(t, 0, 0, w, h, w / 10, 0x7A7A7A);
    tela_tondo(t, 1, 1, w - 2, h - 2, w / 10 - 1, 0xFFFFFF);
    tela_sfuma(t, m, m, w - 2 * m, h - 2 * m, 0x2C5AA0, 0x173764);
    /* A lattice of small diamonds, lighter. */
    for (y = m + 4; y < h - m - 4; y += 8)
        for (x = m + 4 + ((y / 8) & 1) * 4; x < w - m - 4; x += 8) {
            int i, j;
            for (j = -2; j <= 2; j++)
                for (i = -2 + (j < 0 ? -j : j); i <= 2 - (j < 0 ? -j : j); i++)
                    t->px[(y + j) * t->w + x + i] = 0xFF4A7AC4;
        }
    tela_bordo(t, m, m, w - 2 * m, h - 2 * m, 0xD8E2F2);
}

int carte_misura(int w, int h)
{
    int k;

    for (k = 0; k < N_FACCE; k++) if (g_cache[k].px) tela_libera(&g_cache[k]);
    g_cw = w;
    g_ch = h;
    g_f_angolo = ex_font_find(EX_FAMILY_SANS, h >= 90 ? 15 : 13, 1, 0);
    g_f_figura = ex_font_find(EX_FAMILY_SERIF, h >= 90 ? 30 : 24, 1, 0);
    return 1;
}

int carte_w(void) { return g_cw; }
int carte_h(void) { return g_ch; }

void carta_disegna(Tela *t, int x, int y, int carta)
{
    int k = carta < 0 ? 52 : carta % 52;
    Tela *c = &g_cache[k];

    if (!c->px) {
        if (!tela_crea(c, g_cw, g_ch)) {
            tela_rett(t, x, y, g_cw, g_ch, 0xFFFFFF);       /* no memory: a plain card */
            return;
        }
        memset(c->px, 0, (unsigned int)(g_cw * g_ch) * 4u);  /* transparent corners */
        if (k == 52) dorso(c); else faccia(c, k);
    }
    tela_copia(t, x, y, c, 0, 0, g_cw, g_ch, 1);
}

void carta_posto(Tela *t, int x, int y, const char *segno)
{
    tela_tondo(t, x, y, g_cw, g_ch, g_cw / 10, 0x1F6B35);
    tela_tondo(t, x + 2, y + 2, g_cw - 4, g_ch - 4, g_cw / 10 - 2, 0x0E5426);
    if (segno && segno[0])
        tela_testo_c(t, g_f_figura, x + g_cw / 2, y + g_ch / 2 - ex_font_height(g_f_figura) / 2,
                     segno, 0x2E8A4E);
}

void carta_evidenzia(Tela *t, int x, int y, unsigned int c)
{
    tela_bordo(t, x - 1, y - 1, g_cw + 2, g_ch + 2, c);
    tela_bordo(t, x - 2, y - 2, g_cw + 4, g_ch + 4, c);
}
