/* =============================================================================
 * lib/exrtf/vista.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The rich text view (@RTF stage 2). What it is and how a program uses it is
 * in exrtf_vista.h.
 *
 * ! ONE WALK OVER A LINE SERVES DRAWING, THE CARET AND THE MOUSE. A line is
 * cut into fragments (frammenti): one style, one side of the selection, and
 * in a justified line one word with its space. Drawing paints them, the
 * caret's x is measured inside one, and a click is found inside one. Three
 * walks written apart would disagree by a pixel somewhere - a caret drawn one
 * letter off from where typing goes.
 *
 * ! THE LAYOUT IS DONE AGAIN WHOLE after every change. It measures every word
 * of the document, which for the "base" mode's documents (letters, notes,
 * a few pages) is far below a keystroke's time. A long book would want only
 * the changed paragraph laid out again: the place to do it is
 * exrtf_vista_impagina, and nothing else would change.
 *
 * No libc: like rtf.c, this file can go into a library with no C runtime.
 * ============================================================================= */
#include "exrtf_vista.h"
#include "kbd_proto.h"

#define MARGINE     8       /* left and right of the text */
#define CIMA        6       /* above the first line */
#define BARRA      14       /* the vertical scroll bar */
#define FRAM_MAX  400       /* fragments in one line */
#define SEL_FONDO  EX_BLUE
#define BARRA_FONDO 0x00D8D8D8
#define BARRA_POLL  EX_DARK_GRAY

typedef struct {
    unsigned int      da, a;
    int               x, w, extra;  /* extra: the space a justified line adds */
    ExFont            f;
    const ExRtfStile *st;
} Fram;

static Fram g_fr[FRAM_MAX];

/* --------------------------------------------------------------------------
 * Small helpers
 * -------------------------------------------------------------------------- */
int exrtf_pixel(unsigned int punti) { return (int)((punti * 4 + 1) / 3); }

static ExFont font_di(const ExRtfStile *s)
{
    return ex_font_find(s->famiglia, exrtf_pixel(s->corpo), s->grassetto, s->corsivo);
}

static unsigned int minimo(unsigned int a, unsigned int b) { return a < b ? a : b; }
static unsigned int massimo(unsigned int a, unsigned int b) { return a > b ? a : b; }

static int utile(const ExRtfVista *v)           /* the width lines may take */
{
    int u = v->w - 2 * MARGINE - BARRA;
    return u < 16 ? 16 : u;
}

/* The next and previous character boundary (UTF-8). */
static unsigned int dopo(const ExRtfDoc *d, unsigned int p)
{
    if (p >= d->testo_n) return d->testo_n;
    p++;
    while (p < d->testo_n && ((unsigned char)d->testo[p] & 0xC0) == 0x80) p++;
    return p;
}

static unsigned int prima_di(const ExRtfDoc *d, unsigned int p)
{
    if (p == 0) return 0;
    p--;
    while (p > 0 && ((unsigned char)d->testo[p] & 0xC0) == 0x80) p--;
    return p;
}

/* The run holding byte pos (the last one for pos at the end). */
static unsigned int pezzo_di(const ExRtfDoc *d, unsigned int pos)
{
    unsigned int lo = 0, hi = d->pezzi_n;

    if (hi == 0) return 0;
    while (lo + 1 < hi) {
        unsigned int m = (lo + hi) / 2;
        if (d->pezzi[m].inizio <= pos) lo = m; else hi = m;
    }
    return lo;
}

/* The width of n bytes in one font. ex_text_width wants a string: the
 * bytes are copied in pieces, never cutting a UTF-8 character. */
static int misura_font(ExFont f, const char *t, unsigned int n)
{
    char b[256];
    int  w = 0;

    while (n) {
        unsigned int k = n < 255 ? n : 255, i;

        while (k < n && k > 1 && ((unsigned char)t[k] & 0xC0) == 0x80) k--;
        for (i = 0; i < k; i++) b[i] = t[i] == '\t' ? ' ' : t[i];     /* a tab shows as a space */
        b[k] = '\0';
        w += ex_text_width(f, b);
        t += k;
        n -= k;
    }
    return w;
}

/* The width of the bytes from da to a, each run in its own font. */
static int misura(const ExRtfVista *v, unsigned int da, unsigned int a)
{
    const ExRtfDoc *d = v->doc;
    unsigned int k = pezzo_di(d, da);
    int w = 0;

    while (da < a && k < d->pezzi_n) {
        const ExRtfPezzo *p = &d->pezzi[k];
        unsigned int e = minimo(a, p->inizio + p->lung);

        if (e > da) w += misura_font(font_di(&p->stile), d->testo + da, e - da);
        da = e > da ? e : da;
        k++;
    }
    return w;
}

/* --------------------------------------------------------------------------
 * The layout
 * -------------------------------------------------------------------------- */

/* The height of a line and its baseline: the tallest ascent over the tallest
 * descent, so that a big letter and a small one sit on the same base. An
 * empty line takes the style of the place it is in. */
static void metrica(const ExRtfVista *v, unsigned int da, unsigned int a, int *h, int *base)
{
    const ExRtfDoc *d = v->doc;
    int su = 0, giu = 0;

    if (da == a || d->pezzi_n == 0) {
        ExRtfStile s = v->stile;
        ExFont f;

        if (d->pezzi_n) exrtf_stile_a(d, da > 0 && da >= d->testo_n ? da - 1 : da, &s);
        f = font_di(&s);
        su = ex_font_baseline(f);
        giu = ex_font_height(f) - su;
    } else {
        unsigned int k = pezzo_di(d, da);

        while (k < d->pezzi_n && d->pezzi[k].inizio < a) {
            ExFont f = font_di(&d->pezzi[k].stile);
            int b = ex_font_baseline(f), hh = ex_font_height(f);

            if (b > su) su = b;
            if (hh - b > giu) giu = hh - b;
            k++;
        }
    }
    *h = su + giu;
    *base = su;
}

static void riga_aggiungi(ExRtfVista *v, unsigned int da, unsigned int a, int largo,
                          unsigned int spazi, unsigned int allinea, int *y)
{
    ExRtfRiga *r;

    if (v->righe_n >= v->righe_max) return;
    r = &v->righe[v->righe_n++];
    r->inizio = da;
    r->fine = a;
    r->largo = largo;
    r->spazi = spazi;
    r->allinea = (unsigned char)allinea;
    metrica(v, da, a, &r->h, &r->base);
    r->y = *y;
    *y += r->h;
}

void exrtf_vista_impagina(ExRtfVista *v)
{
    const ExRtfDoc *d = v->doc;
    unsigned int pos = 0, par = 0;
    int y = 0, largh = utile(v);

    v->righe_n = 0;
    if (v->w <= 0) return;          /* not placed yet: exrtf_vista_posto lays out */
    for (;;) {
        unsigned int fp = pos, s;
        unsigned int al;

        while (fp < d->testo_n && d->testo[fp] != '\n') fp++;
        al = par < d->par_n ? d->allinea[par] : EXRTF_SINISTRA;

        s = pos;
        do {
            unsigned int i = s, spazi = 0, spazi_vis = 0;
            int largo = 0, largo_vis = 0;

            /* Words, each with the spaces after it, while they fit. */
            while (i < fp) {
                unsigned int j = i, k;
                int ww;

                while (j < fp && d->testo[j] != ' ') j++;
                k = j;
                while (k < fp && d->testo[k] == ' ') k++;
                ww = misura(v, i, j);
                if (largo + ww > largh) {
                    /* ! A WORD LONGER THAN THE LINE IS CUT BY LETTERS, and
                     * only when it is the first of its line: otherwise it
                     * goes whole to the next one. At least one letter stays,
                     * or a very narrow view would never end a line. */
                    if (i == s) {
                        unsigned int c = i;
                        int cw = 0;

                        while (c < j) {
                            unsigned int c2 = dopo(d, c);
                            int w2 = misura(v, c, c2);

                            if (cw + w2 > largh && c > i) break;
                            cw += w2;
                            c = c2;
                        }
                        largo_vis = cw;
                        spazi_vis = 0;
                        i = c;
                    }
                    break;
                }
                largo_vis = largo + ww;
                spazi_vis = spazi;
                largo += ww + misura(v, j, k);
                spazi += k - j;
                i = k;
            }
            /* A paragraph's last line is never justified. */
            riga_aggiungi(v, s, i, largo_vis, spazi_vis,
                          (i >= fp && al == EXRTF_GIUSTO) ? EXRTF_SINISTRA : al, &y);
            s = i;
        } while (s < fp);

        if (fp >= d->testo_n) break;
        pos = fp + 1;
        par++;
    }
    if (v->prima >= v->righe_n) v->prima = v->righe_n ? v->righe_n - 1 : 0;
}

/* The line holding pos: the last one starting at or before it. The end of a
 * wrapped line is the start of the next one, and that is where it goes. */
static unsigned int riga_di(const ExRtfVista *v, unsigned int pos)
{
    unsigned int lo = 0, hi = v->righe_n;

    if (hi == 0) return 0;
    while (lo + 1 < hi) {
        unsigned int m = (lo + hi) / 2;
        if (v->righe[m].inizio <= pos) lo = m; else hi = m;
    }
    return lo;
}

/* How many lines fit from `da` on. */
static unsigned int visibili_da(const ExRtfVista *v, unsigned int da)
{
    unsigned int i;
    int alto = v->h - CIMA;

    for (i = da; i < v->righe_n; i++)
        if (v->righe[i].y + v->righe[i].h - v->righe[da].y > alto) break;
    return i > da ? i - da : 1;
}

static unsigned int prima_massima(const ExRtfVista *v)
{
    unsigned int k;

    if (v->righe_n == 0) return 0;
    /* the first line from which the rest all fit */
    for (k = v->righe_n - 1; k > 0; k--)
        if (v->righe[v->righe_n - 1].y + v->righe[v->righe_n - 1].h - v->righe[k - 1].y > v->h - CIMA)
            break;
    return k;
}

/* Scrolls so that the caret's line is in sight. */
static void segui(ExRtfVista *v)
{
    unsigned int r = riga_di(v, v->cur);

    if (r < v->prima) v->prima = r;
    while (v->prima < r && r >= v->prima + visibili_da(v, v->prima)) v->prima++;
}

static void scorri_righe(ExRtfVista *v, int n)
{
    int p = (int)v->prima + n, pm = (int)prima_massima(v);

    if (p > pm) p = pm;
    if (p < 0) p = 0;
    v->prima = (unsigned int)p;
}

/* The scroll bar's thumb: 0 when everything is in sight. */
static int pollice(const ExRtfVista *v, int *ty, int *th)
{
    unsigned int vis = visibili_da(v, v->prima), pm = prima_massima(v);

    if (v->righe_n <= vis || pm == 0) return 0;
    *th = (int)((unsigned int)v->h * vis / v->righe_n);
    if (*th < 16) *th = 16;
    *ty = v->y + (int)((unsigned int)(v->h - *th) * minimo(v->prima, pm) / pm);
    return 1;
}

/* --------------------------------------------------------------------------
 * The fragments of a line
 * -------------------------------------------------------------------------- */
static int x_riga(const ExRtfVista *v, const ExRtfRiga *r)
{
    int x = v->x + MARGINE, resto = utile(v) - r->largo;

    if (resto < 0) resto = 0;
    if (r->allinea == EXRTF_CENTRO) x += resto / 2;
    if (r->allinea == EXRTF_DESTRA) x += resto;
    return x;
}

static unsigned int frammenti(const ExRtfVista *v, const ExRtfRiga *r,
                              unsigned int sel_da, unsigned int sel_a)
{
    const ExRtfDoc *d = v->doc;
    unsigned int n = 0, pos = r->inizio, k, contati = 0;
    int x = x_riga(v, r);
    int giusto = (r->allinea == EXRTF_GIUSTO && r->spazi > 0);
    int per = 0, resto = 0;

    if (giusto) {
        int tot = utile(v) - r->largo;
        if (tot < 0) tot = 0;
        per = tot / (int)r->spazi;
        resto = tot % (int)r->spazi;
    }
    k = pezzo_di(d, pos);
    while (pos < r->fine && k < d->pezzi_n && n < FRAM_MAX) {
        const ExRtfPezzo *p = &d->pezzi[k];
        unsigned int e = minimo(r->fine, p->inizio + p->lung);
        Fram *fr;

        if (e <= pos) { k++; continue; }
        if (sel_da > pos && sel_da < e) e = sel_da;
        if (sel_a > pos && sel_a < e) e = sel_a;
        if (giusto) {
            unsigned int q = pos;
            while (q < e && d->testo[q] != ' ') q++;
            if (q < e) e = q + 1;
        }
        fr = &g_fr[n++];
        fr->da = pos;
        fr->a = e;
        fr->st = &p->stile;
        fr->f = font_di(fr->st);
        fr->x = x;
        fr->w = misura_font(fr->f, d->testo + pos, e - pos);
        fr->extra = 0;
        if (giusto && d->testo[e - 1] == ' ' && contati < r->spazi) {
            fr->extra = per + ((int)contati < resto ? 1 : 0);
            contati++;
        }
        x += fr->w + fr->extra;
        pos = e;
        if (e >= p->inizio + p->lung) k++;
    }
    return n;
}

/* The x of the caret at pos, in window coordinates. */
static int x_di(const ExRtfVista *v, unsigned int pos)
{
    const ExRtfRiga *r;
    unsigned int n, i;

    if (v->righe_n == 0) return v->x + MARGINE;
    r = &v->righe[riga_di(v, pos)];
    n = frammenti(v, r, 0, 0);
    for (i = 0; i < n; i++)
        if (pos >= g_fr[i].da && pos < g_fr[i].a)
            return g_fr[i].x + misura_font(g_fr[i].f, v->doc->testo + g_fr[i].da, pos - g_fr[i].da);
    return n ? g_fr[n - 1].x + g_fr[n - 1].w : x_riga(v, r);
}

/* The position nearest to x in line ri. */
static unsigned int pos_in_riga(const ExRtfVista *v, unsigned int ri, int x)
{
    const ExRtfDoc  *d = v->doc;
    const ExRtfRiga *r;
    unsigned int n, i;

    if (v->righe_n == 0) return 0;
    if (ri >= v->righe_n) ri = v->righe_n - 1;
    r = &v->righe[ri];
    n = frammenti(v, r, 0, 0);
    for (i = 0; i < n; i++) {
        const Fram *fr = &g_fr[i];
        unsigned int c = fr->da;
        int xc = fr->x;

        while (c < fr->a) {
            unsigned int c2 = dopo(d, c);
            int x2;

            if (c2 > fr->a) c2 = fr->a;
            x2 = fr->x + misura_font(fr->f, d->testo + fr->da, c2 - fr->da);
            if (c2 == fr->a) x2 += fr->extra;
            if (x < (xc + x2) / 2) return c;
            c = c2;
            xc = x2;
        }
    }
    /* Past the end. The end of a WRAPPED line is shown at the start of the
     * next one: the caret stays before the space that wrapped it. */
    if (r->fine > r->inizio && r->fine < d->testo_n && d->testo[r->fine] != '\n' &&
        d->testo[r->fine - 1] == ' ')
        return r->fine - 1;
    return r->fine;
}

/* The position under a window point. */
static unsigned int pos_a(const ExRtfVista *v, int x, int y)
{
    int yy;
    unsigned int i;

    if (v->righe_n == 0) return 0;
    yy = y - (v->y + CIMA) + v->righe[v->prima].y;
    for (i = 0; i + 1 < v->righe_n; i++)
        if (yy < v->righe[i].y + v->righe[i].h) break;
    return pos_in_riga(v, i, x);
}

/* --------------------------------------------------------------------------
 * Setting up
 * -------------------------------------------------------------------------- */
void exrtf_vista_prepara(ExRtfVista *v, ExRtfDoc *d, ExRtfRiga *righe, unsigned int righe_max)
{
    v->doc = d;
    v->righe = righe;
    v->righe_max = righe_max;
    v->righe_n = 0;
    v->x = v->y = 0;
    v->w = v->h = 0;
    v->fuoco = 1;
    v->trascina = 0;
    exrtf_stile_base(&v->stile);
    exrtf_vista_nuovo(v);
}

void exrtf_vista_nuovo(ExRtfVista *v)
{
    v->cur = v->anc = 0;
    v->prima = 0;
    v->x_voluta = -1;
    v->modificato = 0;
    if (v->doc->pezzi_n) exrtf_stile_a(v->doc, 0, &v->stile);
    exrtf_vista_impagina(v);
}

void exrtf_vista_posto(ExRtfVista *v, int x, int y, int w, int h)
{
    v->x = x; v->y = y; v->w = w; v->h = h;
    exrtf_vista_impagina(v);
    segui(v);
}

/* The typing style follows the caret: the letter before it, as every editor
 * does (typing after a bold word goes on in bold). */
static void stile_segui(ExRtfVista *v)
{
    unsigned int p = minimo(v->cur, v->anc);

    if (v->doc->testo_n == 0) return;
    exrtf_stile_a(v->doc, p > 0 ? p - 1 : 0, &v->stile);
}

/* --------------------------------------------------------------------------
 * Drawing
 * -------------------------------------------------------------------------- */
static void scrivi_pezzo(ExWindow f, ExFont fo, int x, int y, const char *t,
                         unsigned int n, unsigned int col)
{
    char b[256];

    while (n) {
        unsigned int k = n < 255 ? n : 255, i;

        while (k < n && k > 1 && ((unsigned char)t[k] & 0xC0) == 0x80) k--;
        for (i = 0; i < k; i++) b[i] = t[i] == '\t' ? ' ' : t[i];
        b[k] = '\0';
        ex_draw_text_font(f, fo, x, y, b, col);
        x += ex_text_width(fo, b);
        t += k;
        n -= k;
    }
}

void exrtf_vista_disegna(ExRtfVista *v, ExWindow f)
{
    const ExRtfDoc *d = v->doc;
    unsigned int sel_da = minimo(v->cur, v->anc), sel_a = massimo(v->cur, v->anc);
    unsigned int i, rc = riga_di(v, v->cur);
    int y0, fondo = v->y + v->h;

    ex_fill_rect(f, v->x, v->y, v->w - BARRA, v->h, EX_WHITE);
    if (v->righe_n == 0) return;
    y0 = v->y + CIMA - v->righe[v->prima].y;

    for (i = v->prima; i < v->righe_n; i++) {
        const ExRtfRiga *r = &v->righe[i];
        int ly = y0 + r->y;
        unsigned int n, j;

        if (ly + r->h > fondo) break;
        n = frammenti(v, r, sel_da, sel_a);
        for (j = 0; j < n; j++) {
            const Fram *fr = &g_fr[j];
            int scelto = sel_da < sel_a && fr->da >= sel_da && fr->a <= sel_a;
            unsigned int col = scelto ? EX_WHITE : fr->st->colore;

            if (scelto) ex_fill_rect(f, fr->x, ly, fr->w + fr->extra, r->h, SEL_FONDO);
            scrivi_pezzo(f, fr->f, fr->x, ly + r->base - ex_font_baseline(fr->f),
                         d->testo + fr->da, fr->a - fr->da, col);
            if (fr->st->sottolineato)
                ex_fill_rect(f, fr->x, ly + r->base + 1, fr->w + fr->extra, 1, col);
        }
        /* A chosen end of paragraph shows as a small block, or a selection
         * of empty lines would not show at all. */
        if (sel_da <= r->fine && r->fine < sel_a && r->fine < d->testo_n) {
            int ex = n ? g_fr[n - 1].x + g_fr[n - 1].w + g_fr[n - 1].extra : x_riga(v, r);
            ex_fill_rect(f, ex, ly, 6, r->h, SEL_FONDO);
        }
        if (v->fuoco && sel_da == sel_a && i == rc)
            ex_fill_rect(f, x_di(v, v->cur), ly, 2, r->h, EX_BLACK);
    }

    /* The scroll bar: the thumb says which part of the lines is in sight. */
    {
        int ty, th;

        ex_fill_rect(f, v->x + v->w - BARRA, v->y, BARRA, v->h, BARRA_FONDO);
        if (pollice(v, &ty, &th)) ex_fill_rect(f, v->x + v->w - BARRA + 2, ty, BARRA - 4, th, BARRA_POLL);
    }
}

/* --------------------------------------------------------------------------
 * Editing
 * -------------------------------------------------------------------------- */
static void dopo_modifica(ExRtfVista *v)
{
    v->modificato = 1;
    v->anc = v->cur;
    v->x_voluta = -1;
    exrtf_vista_impagina(v);
    segui(v);
}

static int via_scelta(ExRtfVista *v)
{
    unsigned int da = minimo(v->cur, v->anc), a = massimo(v->cur, v->anc);

    if (da == a) return 0;
    exrtf_cancella(v->doc, da, a);
    v->cur = v->anc = da;
    return 1;
}

static void inserisci(ExRtfVista *v, const char *t, unsigned int n)
{
    via_scelta(v);
    v->cur += exrtf_inserisci(v->doc, v->cur, t, n, &v->stile);
    dopo_modifica(v);
}

static void muovi(ExRtfVista *v, unsigned int dove, int estendi)
{
    v->cur = dove;
    if (!estendi) v->anc = dove;
    stile_segui(v);
    segui(v);
}

static int e_spazio(char c) { return c == ' ' || c == '\n' || c == '\t'; }

int exrtf_vista_tasto(ExRtfVista *v, unsigned int k)
{
    const ExRtfDoc *d = v->doc;
    unsigned int c = k & KBD_KEY_MASK;
    int ctrl = (k & KBD_MOD_CTRL) != 0, shift = (k & KBD_MOD_SHIFT) != 0;
    unsigned int ri = riga_di(v, v->cur), p;
    int su_giu = 0;

    if (ctrl && c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (ctrl) {
        switch (c) {
        case 'a': exrtf_vista_tutto(v); return 1;
        case 'c': exrtf_vista_copia(v); return 1;
        case 'x': exrtf_vista_taglia(v); return 1;
        case 'v': exrtf_vista_incolla(v); return 1;
        case 'b': exrtf_vista_cambia(v, EXRTF_C_GRASSETTO, EXRTF_INVERTI); return 1;
        case 'i': exrtf_vista_cambia(v, EXRTF_C_CORSIVO, EXRTF_INVERTI); return 1;
        case 'u': exrtf_vista_cambia(v, EXRTF_C_SOTTOLINEATO, EXRTF_INVERTI); return 1;
        case KBD_K_HOME: v->x_voluta = -1; muovi(v, 0, shift); return 1;
        case KBD_K_END:  v->x_voluta = -1; muovi(v, d->testo_n, shift); return 1;
        case KBD_K_LEFT:                    /* by words, as Ctrl+arrows everywhere */
            p = v->cur;
            while (p > 0 && e_spazio(d->testo[p - 1])) p--;
            while (p > 0 && !e_spazio(d->testo[p - 1])) p--;
            v->x_voluta = -1; muovi(v, p, shift); return 1;
        case KBD_K_RIGHT:
            p = v->cur;
            while (p < d->testo_n && !e_spazio(d->testo[p])) p++;
            while (p < d->testo_n && e_spazio(d->testo[p])) p++;
            v->x_voluta = -1; muovi(v, p, shift); return 1;
        default: return 0;
        }
    }

    switch (c) {
    case KBD_K_LEFT:
        /* with a selection and no Shift, Left goes to its start */
        if (!shift && v->cur != v->anc) p = minimo(v->cur, v->anc);
        else p = prima_di(d, v->cur);
        break;
    case KBD_K_RIGHT:
        if (!shift && v->cur != v->anc) p = massimo(v->cur, v->anc);
        else p = dopo(d, v->cur);
        break;
    case KBD_K_HOME:
        p = v->righe_n ? v->righe[ri].inizio : 0;
        break;
    case KBD_K_END:
        p = v->righe_n ? pos_in_riga(v, ri, 1 << 30) : 0;
        break;
    case KBD_K_UP: case KBD_K_DOWN: case KBD_K_PGUP: case KBD_K_PGDN: {
        unsigned int passo = (c == KBD_K_PGUP || c == KBD_K_PGDN)
                           ? visibili_da(v, v->prima) : 1;
        unsigned int nr;

        if (v->x_voluta < 0) v->x_voluta = x_di(v, v->cur);
        if (c == KBD_K_UP || c == KBD_K_PGUP) nr = ri > passo ? ri - passo : 0;
        else nr = ri + passo < v->righe_n ? ri + passo : (v->righe_n ? v->righe_n - 1 : 0);
        p = pos_in_riga(v, nr, v->x_voluta);
        if (c == KBD_K_PGUP || c == KBD_K_PGDN) {
            /* the page moves with the caret */
            if (c == KBD_K_PGUP) v->prima = v->prima > passo ? v->prima - passo : 0;
            else v->prima = minimo(v->prima + passo, prima_massima(v));
        }
        su_giu = 1;
        break;
    }
    case '\b':
        if (!via_scelta(v)) {
            if (v->cur == 0) return 1;
            p = prima_di(d, v->cur);
            exrtf_cancella(v->doc, p, v->cur);
            v->cur = p;
        }
        dopo_modifica(v);
        return 1;
    case KBD_K_DEL:
        if (!via_scelta(v)) {
            if (v->cur >= d->testo_n) return 1;
            exrtf_cancella(v->doc, v->cur, dopo(d, v->cur));
        }
        dopo_modifica(v);
        return 1;
    case '\n': case '\r':
        inserisci(v, "\n", 1);
        return 1;
    default:
        /* ! ONLY PRINTABLE ASCII AND TAB, as the toolkit's text area: the
         * keyboard service sends characters 1..127 and keys from 0x100 up,
         * and a key code put into the text would be a byte no reader knows. */
        if (c == '\t' || (c >= 0x20 && c < 0x7F)) {
            char ch = (char)c;
            inserisci(v, &ch, 1);
            return 1;
        }
        return 0;
    }

    if (!su_giu) v->x_voluta = -1;
    muovi(v, p, shift);
    return 1;
}

void exrtf_vista_cambia(ExRtfVista *v, int cosa, unsigned int valore)
{
    unsigned int da = minimo(v->cur, v->anc), a = massimo(v->cur, v->anc);

    if (valore == EXRTF_INVERTI) {
        ExRtfStile s = v->stile;

        if (da < a) exrtf_stile_a(v->doc, da, &s);
        valore = cosa == EXRTF_C_GRASSETTO ? !s.grassetto :
                 cosa == EXRTF_C_CORSIVO   ? !s.corsivo   : !s.sottolineato;
    }
    exrtf_cambia_stile(v->doc, &v->stile, cosa, valore);
    if (da < a) {
        exrtf_cambia(v->doc, da, a, cosa, valore);
        v->modificato = 1;
        exrtf_vista_impagina(v);
        segui(v);
    }
}

void exrtf_vista_allinea(ExRtfVista *v, unsigned int al)
{
    exrtf_allinea(v->doc, minimo(v->cur, v->anc), massimo(v->cur, v->anc), al);
    v->modificato = 1;
    exrtf_vista_impagina(v);
    segui(v);
}

const ExRtfStile *exrtf_vista_stile(const ExRtfVista *v) { return &v->stile; }

unsigned int exrtf_vista_allineamento(const ExRtfVista *v)
{
    unsigned int p = exrtf_paragrafo(v->doc, minimo(v->cur, v->anc));
    return p < v->doc->par_n ? v->doc->allinea[p] : EXRTF_SINISTRA;
}

/* --------------------------------------------------------------------------
 * The clipboard: plain text, the one every program reads
 * -------------------------------------------------------------------------- */
int exrtf_vista_copia(ExRtfVista *v)
{
    unsigned int da = minimo(v->cur, v->anc), a = massimo(v->cur, v->anc);

    if (da == a) return 0;
    ex_clipboard_set(v->doc->testo + da, a - da);
    return 1;
}

int exrtf_vista_taglia(ExRtfVista *v)
{
    if (!exrtf_vista_copia(v)) return 0;
    via_scelta(v);
    dopo_modifica(v);
    return 1;
}

int exrtf_vista_incolla(ExRtfVista *v)
{
    static char b[64 * 1024];
    unsigned int n = ex_clipboard_get(b, sizeof(b) - 1), i, k = 0;

    if (n == 0 || n >= sizeof(b)) return 0;
    /* \r\n from other systems becomes \n; other control bytes go */
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)b[i];
        if (c == '\r') continue;
        if (c < 0x20 && c != '\n' && c != '\t') continue;
        b[k++] = (char)c;
    }
    if (k == 0) return 0;
    inserisci(v, b, k);
    return 1;
}

void exrtf_vista_tutto(ExRtfVista *v)
{
    v->anc = 0;
    v->cur = v->doc->testo_n;
    stile_segui(v);
    segui(v);
}

/* --------------------------------------------------------------------------
 * The mouse
 * -------------------------------------------------------------------------- */
static int dentro(const ExRtfVista *v, int x, int y)
{
    return x >= v->x && x < v->x + v->w && y >= v->y && y < v->y + v->h;
}

void exrtf_vista_rotella(ExRtfVista *v, int scatti) { scorri_righe(v, scatti * 3); }

int exrtf_vista_clic(ExRtfVista *v, int x, int y, int shift)
{
    unsigned int p;

    if (!dentro(v, x, y)) return 0;
    if (x >= v->x + v->w - BARRA) {             /* the bar: a page at a time */
        int vis = (int)visibili_da(v, v->prima), ty, th;

        if (pollice(v, &ty, &th)) scorri_righe(v, y < ty ? -vis : vis);
        return 1;
    }
    p = pos_a(v, x, y);
    v->x_voluta = -1;
    v->trascina = 1;
    muovi(v, p, shift);
    return 1;
}

int exrtf_vista_trascina(ExRtfVista *v, int x, int y)
{
    if (!v->trascina) return 0;
    /* past the top or the bottom the text scrolls under the pointer */
    if (y < v->y + CIMA && v->prima > 0) v->prima--;
    if (y >= v->y + v->h && v->prima < prima_massima(v)) v->prima++;
    if (y < v->y) y = v->y;
    if (y >= v->y + v->h) y = v->y + v->h - 1;
    v->cur = pos_a(v, x, y);
    stile_segui(v);
    return 1;
}

void exrtf_vista_su(ExRtfVista *v) { v->trascina = 0; }

int exrtf_vista_doppio(ExRtfVista *v, int x, int y)
{
    const ExRtfDoc *d = v->doc;
    unsigned int p, da, a;

    if (!dentro(v, x, y) || x >= v->x + v->w - BARRA) return 0;
    p = pos_a(v, x, y);
    da = a = p;
    while (da > 0 && !e_spazio(d->testo[da - 1])) da--;
    while (a < d->testo_n && !e_spazio(d->testo[a])) a++;
    v->anc = da;
    v->cur = a;
    v->trascina = 0;
    stile_segui(v);
    return 1;
}
