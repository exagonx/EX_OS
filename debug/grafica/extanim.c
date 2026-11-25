/* =============================================================================
 * debug/grafica/extanim.c  ->  destinazione: lib/exwin/extanim.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * Le curve e le copie comuni a tutti gli effetti. Gli effetti stanno in
 * anim_NN_*.c, uno per file, ognuno col suo PNG animato accanto.
 * ============================================================================= */
#include "extanim.h"

int exa_stringi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
int exa_lineare(int t) { return exa_stringi(t, 0, 1000); }
int exa_lerp(int a, int b, int t) { return a + (b - a) * t / 1000; }

/* 1 - (1 - t)^3 */
int exa_esce(int t)
{
    int u = 1000 - exa_stringi(t, 0, 1000);
    return 1000 - u * u / 1000 * u / 1000;
}

/* t < 1/2: 4 t^3;  altrimenti 1 - (2 - 2t)^3 / 2 */
int exa_entra_esce(int t)
{
    t = exa_stringi(t, 0, 1000);
    if (t < 500) return 4 * (t * t / 1000) * t / 1000;
    {
        int u = 2000 - 2 * t;                       /* 0..1000 */
        return 1000 - u * u / 1000 * u / 2000;
    }
}

/* "back" d'uscita: 1 + c3 (t-1)^3 + c1 (t-1)^2, c1 = 1.70158, c3 = c1 + 1 */
int exa_rimbalzo(int t)
{
    int u = exa_stringi(t, 0, 1000) - 1000;         /* -1000..0 */
    int u2 = u * u / 1000, u3 = u2 * u / 1000;
    return 1000 + 2702 * u3 / 1000 + 1702 * u2 / 1000;
}

/* molla: 1 - (1-t)^2 cos(540 gradi * t) — due mezze oscillazioni e si ferma */
int exa_molla(int t)
{
    int u = 1000 - exa_stringi(t, 0, 1000);
    return 1000 - (u * u / 1000) * ext_coseno(t * 540 / 1000) / 1000;
}

void exa_posa(ExTela *d, int x, int y, const unsigned int *src, int sw, int sh,
              int passo, int alfa)
{
    int i, j;

    if (alfa <= 0) return;
    for (j = 0; j < sh; j++)
        for (i = 0; i < sw; i++) ext_punto(d, x + i, y + j, src[j * passo + i], alfa);
}

/* Bilineare in virgola fissa 16.8: per ogni pixel di arrivo, la posizione
 * nella sorgente e i due pesi frazionari. */
static unsigned int campiona(const unsigned int *src, int sw, int sh, int passo, int fx, int fy)
{
    int x0 = fx >> 8, y0 = fy >> 8, ax = fx & 255, ay = fy & 255;
    int x1 = x0 + 1 < sw ? x0 + 1 : x0, y1 = y0 + 1 < sh ? y0 + 1 : y0;
    unsigned int a = src[y0 * passo + x0], b = src[y0 * passo + x1];
    unsigned int c = src[y1 * passo + x0], e = src[y1 * passo + x1];

    return ext_mescola(ext_mescola(a, b, ax), ext_mescola(c, e, ax), ay);
}

void exa_posa_scalata(ExTela *d, int x, int y, int dw, int dh,
                      const unsigned int *src, int sw, int sh, int passo, int alfa)
{
    int i, j;

    if (dw <= 0 || dh <= 0 || alfa <= 0) return;
    for (j = 0; j < dh; j++) {
        int fy = dh > 1 ? j * ((sh - 1) << 8) / (dh - 1) : 0;
        if (y + j < d->cy0 || y + j >= d->cy1) continue;
        for (i = 0; i < dw; i++) {
            int fx = dw > 1 ? i * ((sw - 1) << 8) / (dw - 1) : 0;
            ext_punto(d, x + i, y + j, campiona(src, sw, sh, passo, fx, fy), alfa);
        }
    }
}

void exa_posa_riga(ExTela *d, int x, int y, int dw, const unsigned int *riga, int sw, int alfa)
{
    int i;

    if (dw <= 0 || alfa <= 0 || y < d->cy0 || y >= d->cy1) return;
    for (i = 0; i < dw; i++) {
        int fx = dw > 1 ? i * ((sw - 1) << 8) / (dw - 1) : 0;
        int x0 = fx >> 8, x1 = x0 + 1 < sw ? x0 + 1 : x0;
        ext_punto(d, x + i, y, ext_mescola(riga[x0], riga[x1], fx & 255), alfa);
    }
}
