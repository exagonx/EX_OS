/* =============================================================================
 * exwin/bin/salvaschermo/salvaschermo.c - the screen savers of ExWin
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 *     salvaschermo scritta "TESTO"   the text crosses the screen, every pass
 *                                    with its own side, height, size, colour
 *     salvaschermo cerchio           a circle bounces on the edges, from 200
 *                                    px across to a dot and back, leaving its
 *                                    trail until the screen is covered, then
 *                                    again
 *
 * (voce 116, the user, 23 November 2026.) pm starts it when nobody has used
 * keyboard or mouse for the minutes chosen in Impostazioni > Scrivania, and
 * the Prova button there starts it at once.
 *
 * ! IT ENDS AT THE FIRST KEY OR MOVE, AND THE KERNEL SAYS WHEN. attivita_da()
 * (SYS_ATTIVITA, kernel 0.312) is the time since the last key, click or
 * mouse packet of the whole machine - the same clock dpms.drv uses. Here it
 * must keep growing with the time spent here; the moment it is smaller,
 * somebody touched something. A window event would not do: a mouse moved
 * over the bar, or a key that the server keeps for itself (Ctrl+Alt+Canc),
 * never reaches this window.
 *
 * ! A WINDOW «SOPRA» BORN LAST IS ABOVE THE TASKBAR TOO (wserver, in_cima:
 * among the windows «sopra» the order is the order of birth).
 *
 * ! THE FRAMES ARE COUNTED HERE, NOT BY ex_set_timer: its resolution is 200
 * ms (exwin.h). ex_peek_message and usleep, as the games do.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"

EX_VERSIONE("salvaschermo", "0.001");

#define FOTOGRAMMA_US  33000u       /* about 30 a second */
#define CELLA          8            /* the circle's coverage grid, in px */

static ExWindow     g_f;
static int          g_w, g_h;
static int          g_fine;

/* integer square root, by halving: no float needed for a circle */
static unsigned int isqrt(unsigned int v)
{
    unsigned int r = 0, b = 1u << 30;

    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

static unsigned int a_caso(unsigned int n)
{
    return n ? (unsigned int)rand() % n : 0;
}

/* A bright colour: one channel full, one at random, one low. */
static unsigned int colore_vivo(void)
{
    unsigned int c[3], k = a_caso(3), j = (k + 1 + a_caso(2)) % 3;

    c[0] = c[1] = c[2] = 40u + a_caso(60);
    c[k] = 255u;
    c[j] = 80u + a_caso(176);
    return (c[0] << 16) | (c[1] << 8) | c[2];
}

static long proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    (void)wp; (void)lp;
    switch (msg) {
    case EXM_PAINT:
        ex_fill_rect(f, 0, 0, g_w, g_h, EX_BLACK);
        return 0;
    case EXM_KEY: case EXM_KEY_DOWN: case EXM_MOUSE_DOWN: case EXM_CLOSE:
        g_fine = 1;
        return EX_NO_REDRAW;
    }
    return ex_default_proc(f, msg, wp, lp);
}

/* --- the text ------------------------------------------------------------ */

typedef struct {
    const char *testo;
    ExFont      f;
    int         x, y, tw, th, dir, passo;
    unsigned int colore;
} Scritta;

static void scritta_nuova(Scritta *s)
{
    /* a handful of sizes, not any: the fonts opened stay in ExWin's reserve */
    int corpi[8], n = 0, k;

    corpi[n++] = 24; corpi[n++] = 32; corpi[n++] = 40; corpi[n++] = 56;
    corpi[n++] = 72; corpi[n++] = 96; corpi[n++] = 128; corpi[n++] = g_h / 5;
    k = (int)a_caso((unsigned int)n);
    s->f      = ex_font_find(EX_FAMILY_SANS, corpi[k], a_caso(2), 0);
    s->tw     = ex_text_width(s->f, s->testo);
    s->th     = ex_font_height(s->f);
    if (s->tw <= 0) s->tw = 8 * (int)strlen(s->testo);
    if (s->th <= 0) s->th = 16;
    s->y      = g_h > s->th ? (int)a_caso((unsigned int)(g_h - s->th)) : 0;
    s->dir    = a_caso(2) ? 1 : -1;
    s->passo  = 3 + (int)a_caso(6);
    s->x      = s->dir > 0 ? -s->tw : g_w;
    s->colore = colore_vivo();
}

static void scritta_passo(Scritta *s)
{
    int vecchio = s->x, x0, x1;

    s->x += s->dir * s->passo;
    ex_fill_rect(g_f, vecchio, s->y, s->tw, s->th, EX_BLACK);
    ex_draw_text_font(g_f, s->f, s->x, s->y, s->testo, s->colore);
    x0 = vecchio < s->x ? vecchio : s->x;
    x1 = (vecchio > s->x ? vecchio : s->x) + s->tw;
    if (x0 < 0) x0 = 0;
    if (x1 > g_w) x1 = g_w;
    if (x1 > x0) ex_update_rect(g_f, x0, s->y, x1 - x0, s->th);
    if ((s->dir > 0 && s->x >= g_w) || (s->dir < 0 && s->x + s->tw <= 0))
        scritta_nuova(s);
}

/* --- the circle ------------------------------------------------------------ */

typedef struct {
    int           cx, cy;           /* centre, in 1/16 px */
    int           vx, vy;           /* speed, in 1/16 px per frame */
    int           r, dr;            /* radius 1..100, and where it goes */
    unsigned int  colore;
    unsigned char *coperta;         /* one byte per CELLA x CELLA cell */
    int           col, righe, fatte;
    unsigned int  fermo;            /* frames since the last new cell */
} Cerchio;

static void cerchio_punta(Cerchio *c)
{
    /* towards a cell still uncovered, at the same speed: the bounce alone
     * may never reach the last corners, and the screen must be covered */
    int n = c->col * c->righe, k = (int)a_caso((unsigned int)n), i;
    int dx, dy, d, v = 96;

    for (i = 0; i < n; i++) {
        int q = (k + i) % n;

        if (c->coperta[q]) continue;
        dx = ((q % c->col) * CELLA + CELLA / 2) * 16 - c->cx;
        dy = ((q / c->col) * CELLA + CELLA / 2) * 16 - c->cy;
        d  = (int)isqrt((unsigned int)(dx / 16 * (dx / 16) + dy / 16 * (dy / 16)));
        if (d < 1) d = 1;
        c->vx = dx / 16 * v / d;
        c->vy = dy / 16 * v / d;
        if (c->vx == 0 && c->vy == 0) c->vx = v;
        return;
    }
}

static void cerchio_da_capo(Cerchio *c)
{
    memset(c->coperta, 0, (size_t)(c->col * c->righe));
    c->fatte = 0;
    c->fermo = 0;
    ex_fill_rect(g_f, 0, 0, g_w, g_h, EX_BLACK);
    ex_update(g_f);
    c->colore = colore_vivo();
}

static int cerchio_inizia(Cerchio *c)
{
    c->col   = (g_w + CELLA - 1) / CELLA;
    c->righe = (g_h + CELLA - 1) / CELLA;
    c->coperta = (unsigned char *)malloc((size_t)(c->col * c->righe));
    if (!c->coperta) return -1;
    c->r  = 100;
    c->dr = -1;
    c->cx = (g_w / 2) * 16;
    c->cy = (g_h / 2) * 16;
    c->vx = 80 + (int)a_caso(40);
    c->vy = 60 + (int)a_caso(40);
    cerchio_da_capo(c);
    return 0;
}

static void cerchio_passo(Cerchio *c)
{
    int x = c->cx / 16, y = c->cy / 16, r = c->r, dy, rimbalzo = 0;
    int a, b, i, j;

    for (dy = -r; dy <= r; dy++) {
        int dx = (int)isqrt((unsigned int)(r * r - dy * dy));
        ex_fill_rect(g_f, x - dx, y + dy, 2 * dx + 1, 1, c->colore);
    }
    ex_update_rect(g_f, x - r, y - r, 2 * r + 1, 2 * r + 1);

    /* the cells whose centre is inside */
    a = (x - r) / CELLA; b = (x + r) / CELLA;
    for (j = (y - r) / CELLA; j <= (y + r) / CELLA; j++) {
        if (j < 0 || j >= c->righe) continue;
        for (i = a; i <= b; i++) {
            int ex = i * CELLA + CELLA / 2 - x, ey = j * CELLA + CELLA / 2 - y;
            unsigned char *q;

            if (i < 0 || i >= c->col || ex * ex + ey * ey > r * r) continue;
            q = &c->coperta[j * c->col + i];
            if (!*q) { *q = 1; c->fatte++; c->fermo = 0; }
        }
    }
    c->fermo++;

    /* the size: 100 -> 1 -> 100 (200 px across to a dot) */
    c->r += c->dr;
    if (c->r <= 1)   { c->r = 1;   c->dr = 1; }
    if (c->r >= 100) { c->r = 100; c->dr = -1; }

    /* the move and the bounces */
    c->cx += c->vx; c->cy += c->vy;
    if (c->cx < c->r * 16)         { c->cx = c->r * 16;         c->vx = -c->vx; rimbalzo = 1; }
    if (c->cx > (g_w - c->r) * 16) { c->cx = (g_w - c->r) * 16; c->vx = -c->vx; rimbalzo = 1; }
    if (c->cy < c->r * 16)         { c->cy = c->r * 16;         c->vy = -c->vy; rimbalzo = 1; }
    if (c->cy > (g_h - c->r) * 16) { c->cy = (g_h - c->r) * 16; c->vy = -c->vy; rimbalzo = 1; }
    if (rimbalzo || c->fermo > 90) { cerchio_punta(c); c->fermo = 0; }

    if (c->fatte >= c->col * c->righe) {
        usleep(1000000);                /* a second to see it full */
        cerchio_da_capo(c);
    }
}

int main(int argc, char **argv)
{
    unsigned int sw = 0, sh = 0, t0, a0;
    const char  *tipo = argc >= 2 ? argv[1] : "cerchio";
    Scritta      s;
    Cerchio      c;
    ExMsg        m;
    int          cerchio = strcmp(tipo, "scritta") != 0;

    ex_screen_size(&sw, &sh);
    if (sw == 0) {
        printf("salvaschermo: il server a finestre non risponde (exwin).\n");
        return 1;
    }
    g_w = (int)sw; g_h = (int)sh;
    srand(uptime_ms());

    g_f = ex_create("window", "", EX_TOPMOST, 0, 0, g_w, g_h, 0, 0, proc);
    if (!g_f) { printf("salvaschermo: non riesco a creare la finestra\n"); return 1; }
    proc(g_f, EXM_PAINT, 0, 0);
    ex_update(g_f);

    memset(&s, 0, sizeof(s));
    memset(&c, 0, sizeof(c));
    if (cerchio) {
        if (cerchio_inizia(&c) != 0) return 1;
    } else {
        s.testo = (argc >= 3 && argv[2][0]) ? argv[2] : "EX-OS";
        scritta_nuova(&s);
    }

    t0 = uptime_ms();
    a0 = (unsigned int)attivita_da();
    while (!g_fine) {
        unsigned int dentro = uptime_ms() - t0;

        while (ex_peek_message(&m)) ex_dispatch(&m);
        /* the half second after the start does not count: the click on
         * Prova, the mouse settling */
        if (dentro > 500 && (unsigned int)attivita_da() + 250 < a0 + dentro) break;
        if (cerchio) cerchio_passo(&c); else scritta_passo(&s);
        usleep(FOTOGRAMMA_US);
    }
    ex_destroy(g_f);
    return 0;
}
