/* =============================================================================
 * exwin/bin/exbrowser/browser_disegno.c — the drawing of a box (P10)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Rounded corners (border-radius), the shadow (box-shadow), linear and radial
 * gradients, and colours that are not opaque (rgba, hsla, #rrggbbaa), drawn
 * with plutovg (tools/plutovg-exos, libplutovg.so.1), opened with dlopen the
 * first time a page needs it: debug/implementazione_BROWSER002.txt, P10.
 *
 * ! ONLY WHAT IS ON THE SCREEN IS RENDERED: a surface as big as the visible
 * part of the box (with its shadow), translated, then blended onto the window
 * with ex_pixmap_blend. A box as tall as the page costs what the screen costs.
 *
 * ! WITHOUT plutovg a box is what it was before P10: a rectangle of its
 * colour (the first stop of a gradient), and a colour that is not opaque is
 * blended here by hand - dis_riempi does not need the library.
 *
 * plutovg's types are not included: its header is in the tools' tree, not
 * in the system's. The few structs used (colour, stop) are four and five
 * floats, as plutovg.h 1.3.3 declares them.
 * ========================================================================== */
#include "browser_priv.h"

typedef struct { float r, g, b, a; } PvColore;
typedef struct { float pos; PvColore c; } PvFermata;

#define PV_OP_SRC_OVER 3
#define PV_OP_DST_OUT  8
#define PV_PARI_DISPARI 1

static void *(*pv_sup_crea)(int, int);
static void  (*pv_sup_via)(void *);
static unsigned char *(*pv_sup_dati)(const void *);
static void *(*pv_tela)(void *);
static void  (*pv_tela_via)(void *);
static void  (*pv_rgba)(void *, float, float, float, float);
static void  (*pv_lineare)(void *, float, float, float, float, int, const PvFermata *, int, const void *);
static void  (*pv_radiale)(void *, float, float, float, float, float, float, int, const PvFermata *, int, const void *);
static void  (*pv_tondo)(void *, float, float, float, float, float, float);
static void  (*pv_riempi)(void *);
static void  (*pv_regola)(void *, int);
static void  (*pv_op)(void *, int);
static void  (*pv_trasla)(void *, float, float);
static void  (*pv_muovi)(void *, float, float);
static void  (*pv_linea)(void *, float, float);
static void  (*pv_cubica)(void *, float, float, float, float, float, float);
static void  (*pv_chiudi)(void *);
/* (SVG) */
static void *(*pv_path)(void);
static void  (*pv_path_via)(void *);
static int   (*pv_path_leggi)(void *, const char *, int);
static void  (*pv_add_path)(void *, const void *);
static void  (*pv_salva)(void *);
static void  (*pv_ripristina)(void *);
static void  (*pv_trasforma)(void *, const float *);
static void  (*pv_spessore)(void *, float);
static void  (*pv_capo)(void *, int);
static void  (*pv_giunto)(void *, int);
static void  (*pv_traccia)(void *);
static void  (*pv_riempi_tieni)(void *);
static void  (*pv_nuovo)(void *);
static void  (*pv_ellisse)(void *, float, float, float, float);
static void  (*pv_rett)(void *, float, float, float, float);
static void  (*pv_opac)(void *, float);
static void  (*pv_tratteggio)(void *, float, const float *, int);
/* (canvas) */
static void  (*pv_scala)(void *, float, float);
static void  (*pv_ruota)(void *, float);
static void  (*pv_matrice_metti)(void *, const float *);
static void  (*pv_ritaglia)(void *);
static void  (*pv_arco)(void *, float, float, float, float, float, int);
static void  (*pv_quad)(void *, float, float, float, float);
static void  (*pv_tessuto)(void *, void *, int, float, const float *);
static void *(*pv_faccia_file)(const char *, int);
static void  (*pv_font)(void *, void *, float);
static float (*pv_testo)(void *, const void *, int, int, float, float);
static float (*pv_testo_misura)(void *, const void *, int, int, float *);
static void  (*pv_metriche)(const void *, float, float *, float *, float *, float *);
static void  (*pv_giunto_lim)(void *, float);
static void *(*pv_sup_dati_crea)(unsigned char *, int, int, int);
static void  (*pv_sup_pulisci)(void *, const PvColore *);
static void  (*pv_matrice_leggi)(const void *, float *);
static void  (*pv_matrice_zero)(void *);
static void  (*pv_traccia_tieni)(void *);
static void  (*pp_muovi)(void *, float, float);
static void  (*pp_linea)(void *, float, float);
static void  (*pp_cubica)(void *, float, float, float, float, float, float);
static void  (*pp_quad)(void *, float, float, float, float);
static void  (*pp_chiudi)(void *);
static void  (*pp_azzera)(void *);
static void  (*pp_arco)(void *, float, float, float, float, float, int);
static void  (*pp_rett)(void *, float, float, float, float);
static void  (*pp_trasforma)(void *, const float *);
static int   (*pp_elementi)(const void *, const int **);
static void *(*pv_percorso)(const void *);
static float (*pp_estensione)(const void *, float *, int);

static int g_cercata, g_pronta;

static void *sim(void *l, const char *n, int *ok)
{
    void *p = dlsym(l, n);

    if (!p) *ok = 0;
    return p;
}

int dis_disponibile(void)
{
    void *l;
    int   ok = 1;

    if (g_cercata) return g_pronta;
    g_cercata = 1;
    l = dlopen("libplutovg.so.1", RTLD_NOW);
    if (!l) return 0;
    *(void **)&pv_sup_crea = sim(l, "plutovg_surface_create", &ok);
    *(void **)&pv_sup_via  = sim(l, "plutovg_surface_destroy", &ok);
    *(void **)&pv_sup_dati = sim(l, "plutovg_surface_get_data", &ok);
    *(void **)&pv_tela     = sim(l, "plutovg_canvas_create", &ok);
    *(void **)&pv_tela_via = sim(l, "plutovg_canvas_destroy", &ok);
    *(void **)&pv_rgba     = sim(l, "plutovg_canvas_set_rgba", &ok);
    *(void **)&pv_lineare  = sim(l, "plutovg_canvas_set_linear_gradient", &ok);
    *(void **)&pv_radiale  = sim(l, "plutovg_canvas_set_radial_gradient", &ok);
    *(void **)&pv_regola   = sim(l, "plutovg_canvas_set_fill_rule", &ok);
    *(void **)&pv_op       = sim(l, "plutovg_canvas_set_operator", &ok);
    *(void **)&pv_trasla   = sim(l, "plutovg_canvas_translate", &ok);
    *(void **)&pv_tondo    = sim(l, "plutovg_canvas_round_rect", &ok);
    *(void **)&pv_riempi   = sim(l, "plutovg_canvas_fill", &ok);
    *(void **)&pv_muovi    = sim(l, "plutovg_canvas_move_to", &ok);
    *(void **)&pv_linea    = sim(l, "plutovg_canvas_line_to", &ok);
    *(void **)&pv_cubica   = sim(l, "plutovg_canvas_cubic_to", &ok);
    *(void **)&pv_chiudi   = sim(l, "plutovg_canvas_close_path", &ok);
    *(void **)&pv_path        = sim(l, "plutovg_path_create", &ok);
    *(void **)&pv_path_via    = sim(l, "plutovg_path_destroy", &ok);
    *(void **)&pv_path_leggi  = sim(l, "plutovg_path_parse", &ok);
    *(void **)&pv_add_path    = sim(l, "plutovg_canvas_add_path", &ok);
    *(void **)&pv_salva       = sim(l, "plutovg_canvas_save", &ok);
    *(void **)&pv_ripristina  = sim(l, "plutovg_canvas_restore", &ok);
    *(void **)&pv_trasforma   = sim(l, "plutovg_canvas_transform", &ok);
    *(void **)&pv_spessore    = sim(l, "plutovg_canvas_set_line_width", &ok);
    *(void **)&pv_capo        = sim(l, "plutovg_canvas_set_line_cap", &ok);
    *(void **)&pv_giunto      = sim(l, "plutovg_canvas_set_line_join", &ok);
    *(void **)&pv_traccia     = sim(l, "plutovg_canvas_stroke", &ok);
    *(void **)&pv_riempi_tieni = sim(l, "plutovg_canvas_fill_preserve", &ok);
    *(void **)&pv_nuovo       = sim(l, "plutovg_canvas_new_path", &ok);
    *(void **)&pv_ellisse     = sim(l, "plutovg_canvas_ellipse", &ok);
    *(void **)&pv_rett        = sim(l, "plutovg_canvas_rect", &ok);
    *(void **)&pv_opac        = sim(l, "plutovg_canvas_set_opacity", &ok);
    *(void **)&pv_tratteggio  = sim(l, "plutovg_canvas_set_dash", &ok);
    *(void **)&pv_scala       = sim(l, "plutovg_canvas_scale", &ok);
    *(void **)&pv_ruota       = sim(l, "plutovg_canvas_rotate", &ok);
    *(void **)&pv_matrice_metti = sim(l, "plutovg_canvas_set_matrix", &ok);
    *(void **)&pv_ritaglia    = sim(l, "plutovg_canvas_clip", &ok);
    *(void **)&pv_arco        = sim(l, "plutovg_canvas_arc", &ok);
    *(void **)&pv_quad        = sim(l, "plutovg_canvas_quad_to", &ok);
    *(void **)&pv_tessuto     = sim(l, "plutovg_canvas_set_texture", &ok);
    *(void **)&pv_faccia_file = sim(l, "plutovg_font_face_load_from_file", &ok);
    *(void **)&pv_font        = sim(l, "plutovg_canvas_set_font", &ok);
    *(void **)&pv_testo       = sim(l, "plutovg_canvas_fill_text", &ok);
    *(void **)&pv_testo_misura = sim(l, "plutovg_canvas_text_extents", &ok);
    *(void **)&pv_metriche    = sim(l, "plutovg_font_face_get_metrics", &ok);
    *(void **)&pv_giunto_lim  = sim(l, "plutovg_canvas_set_miter_limit", &ok);
    *(void **)&pv_sup_dati_crea = sim(l, "plutovg_surface_create_for_data", &ok);
    *(void **)&pv_sup_pulisci = sim(l, "plutovg_surface_clear", &ok);
    *(void **)&pv_matrice_leggi = sim(l, "plutovg_canvas_get_matrix", &ok);
    *(void **)&pv_matrice_zero = sim(l, "plutovg_canvas_reset_matrix", &ok);
    *(void **)&pv_traccia_tieni = sim(l, "plutovg_canvas_stroke_preserve", &ok);
    *(void **)&pp_muovi       = sim(l, "plutovg_path_move_to", &ok);
    *(void **)&pp_linea       = sim(l, "plutovg_path_line_to", &ok);
    *(void **)&pp_cubica      = sim(l, "plutovg_path_cubic_to", &ok);
    *(void **)&pp_quad        = sim(l, "plutovg_path_quad_to", &ok);
    *(void **)&pp_chiudi      = sim(l, "plutovg_path_close", &ok);
    *(void **)&pp_azzera      = sim(l, "plutovg_path_reset", &ok);
    *(void **)&pp_arco        = sim(l, "plutovg_path_add_arc", &ok);
    *(void **)&pp_rett        = sim(l, "plutovg_path_add_rect", &ok);
    *(void **)&pp_trasforma   = sim(l, "plutovg_path_transform", &ok);
    *(void **)&pp_elementi    = sim(l, "plutovg_path_get_elements", &ok);
    *(void **)&pv_percorso    = sim(l, "plutovg_canvas_get_path", &ok);
    *(void **)&pp_estensione  = sim(l, "plutovg_path_extents", &ok);
    g_pronta = ok;
    return ok;
}

/* ------------------------------------------------- colours and the window */
/* ! THE ALPHA OF A CSS COLOUR is its top byte (excss, since P10); ExWin's own
 * colours have 0 there and are opaque. A box colour of 0 or 255 is opaque,
 * 1 is invisible (the browser's "transparent", alfa_sistema), between is
 * blended. */
static unsigned int alfa_di(unsigned int c)
{
    unsigned int a = c >> 24;

    return (a == 0) ? 255 : a;
}

static void colore_pv(void *t, unsigned int c)
{
    pv_rgba(t, ((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f,
            alfa_di(c) <= 1 ? 0.0f : alfa_di(c) / 255.0f);
}

void dis_riempi(int x, int y, int w, int h, unsigned int c)
{
    unsigned int a = alfa_di(c), riga[256];
    int          i, j, k;

    if (w <= 0 || h <= 0 || a <= 1) return;
    if (a == 255) { ex_fill_rect(g_f, x, y, w, h, c & 0xFFFFFFu); return; }
    for (i = 0; i < 256; i++) riga[i] = c;
    /* a row of 256 at a time, the same row for every line: passo 0 = w */
    for (k = 0; k < w; k += 256) {
        int ww = (w - k > 256) ? 256 : w - k;

        for (j = 0; j < h; j++) ex_pixmap_blend(g_f, x + k, y + j, ww, 1, riga, 256);
    }
}

/* the surface (premultiplied) onto the window (straight alpha) */
static void posa(void *sup, int ox, int oy, int w, int h)
{
    unsigned int *p = (unsigned int *)pv_sup_dati(sup);
    int           i;

    if (!p) return;
    for (i = 0; i < w * h; i++) {
        unsigned int v = p[i], a = v >> 24;

        if (a == 0 || a == 255) continue;
        p[i] = (a << 24) | (((v >> 16 & 0xFF) * 255 / a) << 16) |
               (((v >> 8 & 0xFF) * 255 / a) << 8) | ((v & 0xFF) * 255 / a);
    }
    ex_pixmap_blend(g_f, ox, oy, w, h, p, (unsigned int)w);
}

/* ------------------------------------------------------------ the shapes */
/* A rounded rectangle with four radii (plutovg's round_rect has one): the
 * radii are scaled down together when they do not fit, as CSS says. */
static void tondo4(void *t, float x, float y, float w, float h, const float *r0)
{
    float r[4], f = 1.0f;
    const float k = 0.5522847f;
    int   i;

    for (i = 0; i < 4; i++) r[i] = r0[i] < 0 ? 0 : r0[i];
    if (r[0] + r[1] > w && r[0] + r[1] > 0) f = w / (r[0] + r[1]);
    if (r[3] + r[2] > w && (r[3] + r[2]) * f > w) f = w / (r[3] + r[2]);
    if (r[0] + r[3] > h && (r[0] + r[3]) * f > h) f = h / (r[0] + r[3]);
    if (r[1] + r[2] > h && (r[1] + r[2]) * f > h) f = h / (r[1] + r[2]);
    for (i = 0; i < 4; i++) r[i] *= f;
    pv_muovi(t, x + r[0], y);
    pv_linea(t, x + w - r[1], y);
    if (r[1] > 0) pv_cubica(t, x + w - r[1] + r[1] * k, y, x + w, y + r[1] - r[1] * k, x + w, y + r[1]);
    pv_linea(t, x + w, y + h - r[2]);
    if (r[2] > 0) pv_cubica(t, x + w, y + h - r[2] + r[2] * k, x + w - r[2] + r[2] * k, y + h, x + w - r[2], y + h);
    pv_linea(t, x + r[3], y + h);
    if (r[3] > 0) pv_cubica(t, x + r[3] - r[3] * k, y + h, x, y + h - r[3] + r[3] * k, x, y + h - r[3]);
    pv_linea(t, x, y + r[0]);
    if (r[0] > 0) pv_cubica(t, x, y + r[0] - r[0] * k, x + r[0] - r[0] * k, y, x + r[0], y);
    pv_chiudi(t);
}

static void raggi(const Decoro *d, int bw, int bh, float *r)
{
    int i, m = bw < bh ? bw : bh;

    for (i = 0; i < 4; i++) {
        int v = d->d.raggio[i];

        /* a percentage of the box: -(hundredths); of the smaller side, which
         * is what 50% on a square (a circle) needs */
        r[i] = v < 0 ? (float)m * (float)(-v) / 10000.0f : (float)v;
    }
}

/* ------------------------------------------------------------ the shadow */
/* ! A BLUR IS THREE BOX BLURS, horizontally and vertically, of the
 * premultiplied pixels: close enough to the Gaussian CSS asks for (its
 * standard deviation is half the blur radius). */
static void sfoca(unsigned int *p, int w, int h, int r)
{
    unsigned int *t;
    int           pass, x, y, c;

    if (r < 1) return;
    t = (unsigned int *)malloc((unsigned int)(w > h ? w : h) * sizeof(unsigned int));
    if (!t) return;
    for (pass = 0; pass < 3; pass++) {
        for (y = 0; y < h; y++) {
            unsigned int *riga = p + y * w;

            for (c = 0; c < 32; c += 8) {
                int somma = 0, n = 2 * r + 1;

                for (x = -r; x <= r; x++) somma += x >= 0 && x < w ? (int)(riga[x] >> c & 0xFF) : 0;
                for (x = 0; x < w; x++) {
                    int dentro = x + r + 1, fuori = x - r;

                    t[x] = (t[x] & ~(0xFFu << c)) | ((unsigned int)(somma / n) << c);
                    if (dentro < w) somma += (int)(riga[dentro] >> c & 0xFF);
                    if (fuori >= 0) somma -= (int)(riga[fuori] >> c & 0xFF);
                }
            }
            for (x = 0; x < w; x++) riga[x] = t[x];
        }
        for (x = 0; x < w; x++) {
            for (c = 0; c < 32; c += 8) {
                int somma = 0, n = 2 * r + 1;

                for (y = -r; y <= r; y++) somma += y >= 0 && y < h ? (int)(p[y * w + x] >> c & 0xFF) : 0;
                for (y = 0; y < h; y++) {
                    int dentro = y + r + 1, fuori = y - r;

                    t[y] = (t[y] & ~(0xFFu << c)) | ((unsigned int)(somma / n) << c);
                    if (dentro < h) somma += (int)(p[dentro * w + x] >> c & 0xFF);
                    if (fuori >= 0) somma -= (int)(p[fuori * w + x] >> c & 0xFF);
                }
            }
            for (y = 0; y < h; y++) p[y * w + x] = t[y];
        }
    }
    free(t);
}

/* ---------------------------------------------------------- the gradient */
/* sin and cos of an angle in degrees, and a square root: EX-OS's libc has
 * sqrt only, and a gradient's line needs three numbers */
static float seno_g(int g)
{
    float x, x2;
    int   segno = 1;

    g = ((g % 360) + 360) % 360;
    if (g > 180) { g -= 180; segno = -1; }
    if (g > 90) g = 180 - g;
    x = g * 3.14159265f / 180.0f;
    x2 = x * x;
    return segno * x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}

static float radice_q(float v)
{
    float r = v > 1 ? v / 2 : 1;
    int   i;

    if (v <= 0) return 0;
    for (i = 0; i < 20; i++) r = (r + v / r) / 2;
    return r;
}

static void sfumatura(void *t, const CssDecoro *d, float x, float y, float w, float h)
{
    PvFermata f[CSS_FERMATE_MAX];
    int       n = d->n_fermate, i, j;

    for (i = 0; i < n; i++) {
        unsigned int c = d->fermata_colore[i];

        f[i].c.r = ((c >> 16) & 0xFF) / 255.0f;
        f[i].c.g = ((c >> 8) & 0xFF) / 255.0f;
        f[i].c.b = (c & 0xFF) / 255.0f;
        f[i].c.a = ((c >> 24) & 0xFF) / 255.0f;
        f[i].pos = d->fermata_pos[i] < 0 ? -1.0f : d->fermata_pos[i] / 10000.0f;
    }
    /* the positions not said: the first 0, the last 1, the others evenly
     * between the ones around them; and never going back */
    if (f[0].pos < 0) f[0].pos = 0;
    if (f[n - 1].pos < 0) f[n - 1].pos = 1;
    for (i = 1; i < n; i++) {
        if (f[i].pos >= 0) { if (f[i].pos < f[i - 1].pos) f[i].pos = f[i - 1].pos; continue; }
        for (j = i + 1; j < n && f[j].pos < 0; j++) {}
        {
            float a = f[i - 1].pos, b = f[j].pos;
            int   k;

            for (k = i; k < j; k++) f[k].pos = a + (b - a) * (float)(k - i + 1) / (float)(j - i + 1);
        }
    }
    if (d->sfumatura == 1) {
        /* ! THE GRADIENT LINE of an angle goes through the centre and is as
         * long as the corners need: |w sin a| + |h cos a| */
        float s, c, l, cx = x + w / 2, cy = y + h / 2;

        s = seno_g(d->angolo);
        c = seno_g(d->angolo + 90);
        l = (s < 0 ? -s : s) * w + (c < 0 ? -c : c) * h;
        pv_lineare(t, cx - s * l / 2, cy + c * l / 2, cx + s * l / 2, cy - c * l / 2, 0, f, n, 0);
    } else {
        /* a circle to the farthest corner (the CSS default is an ellipse) */
        float r = radice_q(w * w + h * h) / 2;

        pv_radiale(t, x + w / 2, y + h / 2, r, x + w / 2, y + h / 2, 0, 0, f, n, 0);
    }
}

/* ------------------------------------------------------------- the box */
void dis_scatola(const Decoro *d, unsigned int sfondo, int bx, int by, int bw, int bh,
                 int cx, int cy, int cw, int ch)
{
    float r[4], ri[4];
    int   ox, oy, ow, oh, est = 0, i;
    void *sup, *t;
    const CssDecoro *k = &d->d;

    if (bw <= 0 || bh <= 0) return;
    if (!dis_disponibile()) {
        /* as before P10: the rectangle of its colour */
        unsigned int c = k->sfumatura ? k->fermata_colore[0] : sfondo;

        if (c != CSS_NIENTE) {
            int x = bx, y = by, w = bw, h = bh;

            if (x < cx) { w -= cx - x; x = cx; }
            if (y < cy) { h -= cy - y; y = cy; }
            if (x + w > cx + cw) w = cx + cw - x;
            if (y + h > cy + ch) h = cy + ch - y;
            dis_riempi(x, y, w, h, c);
        }
        return;
    }
    if (k->ombra && !k->ombra_dentro) {
        int ax = k->ombra_x < 0 ? -k->ombra_x : k->ombra_x, ay = k->ombra_y < 0 ? -k->ombra_y : k->ombra_y;

        est = (ax > ay ? ax : ay) + k->ombra_sfoca + (k->ombra_allarga > 0 ? k->ombra_allarga : 0) + 1;
    }
    /* the part on the screen of the box and its shadow */
    ox = bx - est; oy = by - est; ow = bw + 2 * est; oh = bh + 2 * est;
    if (ox < cx) { ow -= cx - ox; ox = cx; }
    if (oy < cy) { oh -= cy - oy; oy = cy; }
    if (ox + ow > cx + cw) ow = cx + cw - ox;
    if (oy + oh > cy + ch) oh = cy + ch - oy;
    if (ow <= 0 || oh <= 0) return;
    raggi(d, bw, bh, r);

    sup = pv_sup_crea(ow, oh);
    if (!sup) return;
    t = pv_tela(sup);
    if (!t) { pv_sup_via(sup); return; }
    pv_trasla(t, (float)-ox, (float)-oy);

    /* 1. the shadow outside the box: its own surface, blurred, with the box
     * cut out of it (CSS draws an outer shadow only outside the border box) */
    if (est) {
        int   m = k->ombra_sfoca, sx = ox - m, sy = oy - m, sw = ow + 2 * m, sh = oh + 2 * m;
        void *s2 = pv_sup_crea(sw, sh), *t2 = s2 ? pv_tela(s2) : 0;

        if (t2) {
            float a = (float)k->ombra_allarga, rr[4];
            unsigned int *p2, *p1;
            int   y;

            for (i = 0; i < 4; i++) rr[i] = r[i] > 0 ? r[i] + a : 0;
            pv_trasla(t2, (float)-sx, (float)-sy);
            colore_pv(t2, k->ombra_colore);
            tondo4(t2, bx + k->ombra_x - a, by + k->ombra_y - a, bw + 2 * a, bh + 2 * a, rr);
            pv_riempi(t2);
            p2 = (unsigned int *)pv_sup_dati(s2);
            sfoca(p2, sw, sh, k->ombra_sfoca / 2);
            pv_op(t2, PV_OP_DST_OUT);
            pv_rgba(t2, 0, 0, 0, 1);
            tondo4(t2, bx, by, bw, bh, r);
            pv_riempi(t2);
            p1 = (unsigned int *)pv_sup_dati(sup);
            for (y = 0; y < oh; y++) {
                unsigned int *src = p2 + (y + m) * sw + m;

                for (i = 0; i < ow; i++) p1[y * ow + i] = src[i];
            }
            pv_tela_via(t2);
        }
        if (s2) pv_sup_via(s2);
    }

    /* 2. the background: a gradient, or the colour */
    if (k->sfumatura) {
        sfumatura(t, k, (float)bx, (float)by, (float)bw, (float)bh);
        tondo4(t, bx, by, bw, bh, r);
        pv_riempi(t);
    } else if (sfondo != CSS_NIENTE && alfa_di(sfondo) > 1) {
        colore_pv(t, sfondo);
        tondo4(t, bx, by, bw, bh, r);
        pv_riempi(t);
    }

    /* 3. the borders: the outer shape less the inner one (even-odd), in the
     * colour of the top one (four colours with round corners: one) */
    if (d->bordo[0] || d->bordo[1] || d->bordo[2] || d->bordo[3]) {
        int q = d->bordo[0] ? 0 : d->bordo[1] ? 1 : d->bordo[2] ? 2 : 3;

        ri[0] = r[0] - (d->bordo[0] > d->bordo[3] ? d->bordo[0] : d->bordo[3]);
        ri[1] = r[1] - (d->bordo[0] > d->bordo[1] ? d->bordo[0] : d->bordo[1]);
        ri[2] = r[2] - (d->bordo[2] > d->bordo[1] ? d->bordo[2] : d->bordo[1]);
        ri[3] = r[3] - (d->bordo[2] > d->bordo[3] ? d->bordo[2] : d->bordo[3]);
        colore_pv(t, d->bordo_col[q]);
        pv_regola(t, PV_PARI_DISPARI);
        tondo4(t, bx, by, bw, bh, r);
        tondo4(t, bx + d->bordo[3], by + d->bordo[0], bw - d->bordo[3] - d->bordo[1],
               bh - d->bordo[0] - d->bordo[2], ri);
        pv_riempi(t);
    }
    pv_tela_via(t);
    posa(sup, ox, oy, ow, oh);
    pv_sup_via(sup);
}

/* =============================================================================
 * INLINE SVG (P10, 26 November 2026)
 *
 * An <svg> in the page is a piece (browser_estranei.c, EST_SVG) drawn from
 * its nodes: svg (viewBox, preserveAspectRatio as xMidYMid meet), g, path,
 * rect, circle, ellipse, line, polyline, polygon, use (#id in the page, with
 * <symbol>), with fill, stroke, their opacities, stroke-width, linecap,
 * linejoin, fill-rule, opacity, stroke-dasharray, transform, from the
 * attributes or the style attribute. currentColor is the CSS colour of the
 * <svg>. Not here: CSS rules that target SVG elements, <text>, gradients and
 * patterns (a gradient's first stop is used), filters, masks, clipPath.
 * ========================================================================== */
/* ! THE DOCUMENT THE SVG IS IN: the page's for an inline <svg>, its own for
 * an SVG image (dis_svg_immagine) */
static HtmlDoc *g_sd;

typedef struct {
    unsigned int fill, stroke, corrente;
    float        fill_op, stroke_op, sw, opac;
    unsigned char fill_no, stroke_no, capo, giunto, regola;
    int          fill_grad, stroke_grad;    /* a gradient's node + 1, or 0 */
    float        tratti[8];
    int          n_tratti;
} SvgPittura;

/* a property, from style="" first, then the attribute: into buf */
static int svg_prop(int n, const char *nome, char *buf, int max)
{
    const char *st = html_attr(g_sd, n, "style"), *a;
    int         ln = (int)strlen(nome), k;

    if (st) {
        const char *p = st;

        while (*p) {
            while (*p == ' ' || *p == ';' || *p == '\t' || *p == '\n') p++;
            if (!strncmp(p, nome, (size_t)ln)) {
                const char *q = p + ln;

                while (*q == ' ') q++;
                if (*q == ':') {
                    q++;
                    while (*q == ' ') q++;
                    for (k = 0; q[k] && q[k] != ';' && k < max - 1; k++) buf[k] = q[k];
                    while (k > 0 && buf[k - 1] == ' ') k--;
                    buf[k] = 0;
                    return k > 0;
                }
            }
            while (*p && *p != ';') p++;
        }
    }
    a = html_attr(g_sd, n, nome);
    if (!a) return 0;
    for (k = 0; a[k] && k < max - 1; k++) buf[k] = a[k];
    buf[k] = 0;
    return k > 0;
}

/* a number, and where it ended: "12", "-3.5e2", ".5", "10px", "50%" */
static float svg_num(const char *s, const char **fine)
{
    float v = 0, sc = 1, segno = 1;
    int   e = 0, es = 1, cifre = 0;

    while (*s == ' ' || *s == ',' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '-') { segno = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (float)(*s++ - '0'); cifre = 1; }
    if (*s == '.') { s++; while (*s >= '0' && *s <= '9') { sc /= 10; v += (float)(*s++ - '0') * sc; cifre = 1; } }
    if (cifre && (*s == 'e' || *s == 'E') && (s[1] == '-' || s[1] == '+' || (s[1] >= '0' && s[1] <= '9'))) {
        s++;
        if (*s == '-') { es = -1; s++; } else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') e = e * 10 + (*s++ - '0');
        while (e-- > 0) v = es > 0 ? v * 10 : v / 10;
    }
    if (fine) *fine = cifre ? s : 0;
    return segno * v;
}

/* a length attribute; a percentage of `rif` */
static float svg_lung(int n, const char *nome, float rif, float predef)
{
    char        b[64];
    const char *f;
    float       v;

    if (!svg_prop(n, nome, b, sizeof(b))) return predef;
    v = svg_num(b, &f);
    if (!f) return predef;
    if (*f == '%') v = v * rif / 100;
    else if (f[0] == 'e' && f[1] == 'm') v *= 16;
    return v;
}

static int g_svg_grad;      /* set by svg_colore: the gradient it found, + 1 */
static int svg_per_id(const char *id);
static int svg_prop(int n, const char *nome, char *buf, int max);

/* the first stop's colour of gradient m */
static int svg_colore_stop(int m, unsigned int *c)
{
    int s;

    for (s = g_sd->nodi[m].primo_figlio; s >= 0; s = g_sd->nodi[s].prossimo) {
        char bc[64];

        if (g_sd->nodi[s].tipo != HTML_ELEMENTO || strcmp(html_nome(g_sd, s), "stop")) continue;
        if (svg_prop(s, "stop-color", bc, sizeof(bc)) && css_colore(bc, (unsigned int)strlen(bc), c)) return 1;
    }
    return 0;
}

static int svg_colore(const char *b, unsigned int corrente, unsigned int *out, unsigned char *no)
{
    unsigned int c;

    g_svg_grad = 0;
    if (!strcmp(b, "none") || !strcmp(b, "transparent")) { *no = 1; return 1; }
    if (!strcasecmp(b, "currentColor")) { *out = corrente; *no = 0; return 1; }
    if (!strncmp(b, "url(", 4)) {
        /* a gradient or a pattern: the colour of the first stop, if we
         * find it, else what follows the url() (its fallback) */
        const char *h = strchr(b, '#');
        int         k, m, s;
        char        id[64];

        if (h) {
            for (k = 0, h++; h[k] && h[k] != ')' && h[k] != '\'' && h[k] != '"' && k < 63; k++) id[k] = h[k];
            id[k] = 0;
            for (m = 0; m < (int)g_sd->nodi_n; m++) {
                const char *i2 = g_sd->nodi[m].tipo == HTML_ELEMENTO ? html_attr(g_sd, m, "id") : 0;

                if (!i2 || strcmp(i2, id)) continue;
                if (strstr(html_nome(g_sd, m), "radient")) g_svg_grad = m + 1;
                {
                    /* the stops may be another gradient's (href) */
                    const char *hr = html_attr(g_sd, m, "href");
                    int         m2;

                    if (!hr) hr = html_attr(g_sd, m, "xlink:href");
                    if (hr && hr[0] == '#' && g_sd->nodi[m].primo_figlio < 0 && (m2 = svg_per_id(hr + 1)) >= 0) {
                        int gr = g_svg_grad;

                        if (svg_colore_stop(m2, &c)) { *out = c; *no = 0; g_svg_grad = gr; return 1; }
                    }
                }
                for (s = g_sd->nodi[m].primo_figlio; s >= 0; s = g_sd->nodi[s].prossimo) {
                    char bc[64];

                    if (g_sd->nodi[s].tipo != HTML_ELEMENTO || strcmp(html_nome(g_sd, s), "stop")) continue;
                    if (svg_prop(s, "stop-color", bc, sizeof(bc)) && css_colore(bc, (unsigned int)strlen(bc), &c)) {
                        *out = c; *no = 0; return 1;
                    }
                }
                break;
            }
        }
        h = strchr(b, ')');
        if (h && h[1]) { while (*++h == ' ') {} return svg_colore(h, corrente, out, no); }
        return 0;
    }
    if (!css_colore(b, (unsigned int)strlen(b), &c)) return 0;
    *out = c; *no = 0;
    return 1;
}

static void svg_pittura(int n, SvgPittura *p)
{
    char b[128];

    if (svg_prop(n, "color", b, sizeof(b))) { unsigned char z; svg_colore(b, p->corrente, &p->corrente, &z); }
    if (svg_prop(n, "fill", b, sizeof(b)) && svg_colore(b, p->corrente, &p->fill, &p->fill_no)) p->fill_grad = g_svg_grad;
    if (svg_prop(n, "stroke", b, sizeof(b)) && svg_colore(b, p->corrente, &p->stroke, &p->stroke_no)) p->stroke_grad = g_svg_grad;
    if (svg_prop(n, "fill-opacity", b, sizeof(b))) p->fill_op = svg_num(b, 0);
    if (svg_prop(n, "stroke-opacity", b, sizeof(b))) p->stroke_op = svg_num(b, 0);
    if (svg_prop(n, "stroke-width", b, sizeof(b))) p->sw = svg_num(b, 0);
    if (svg_prop(n, "stroke-linecap", b, sizeof(b))) p->capo = !strcmp(b, "round") ? 1 : !strcmp(b, "square") ? 2 : 0;
    if (svg_prop(n, "stroke-linejoin", b, sizeof(b))) p->giunto = !strcmp(b, "round") ? 1 : !strcmp(b, "bevel") ? 2 : 0;
    if (svg_prop(n, "fill-rule", b, sizeof(b))) p->regola = !strcmp(b, "evenodd") ? 1 : 0;
    if (svg_prop(n, "stroke-dasharray", b, sizeof(b))) {
        const char *q = b, *f;

        p->n_tratti = 0;
        if (strcmp(b, "none"))
            while (p->n_tratti < 8) {
                float v = svg_num(q, &f);

                if (!f) break;
                p->tratti[p->n_tratti++] = v;
                q = f;
            }
    }
}

/* transform="": matrix, translate, scale, rotate, skewX, skewY */
static void svg_matrice_per(float *m, const float *b)
{
    float r[6];

    r[0] = m[0] * b[0] + m[2] * b[1];
    r[1] = m[1] * b[0] + m[3] * b[1];
    r[2] = m[0] * b[2] + m[2] * b[3];
    r[3] = m[1] * b[2] + m[3] * b[3];
    r[4] = m[0] * b[4] + m[2] * b[5] + m[4];
    r[5] = m[1] * b[4] + m[3] * b[5] + m[5];
    memcpy(m, r, sizeof(r));
}

static void svg_trasforma(void *t, const char *s)
{
    float m[6] = { 1, 0, 0, 1, 0, 0 };

    while (s && *s) {
        float a[6], b[6] = { 1, 0, 0, 1, 0, 0 };
        int   n = 0, k;
        const char *f, *nome;

        while (*s == ' ' || *s == ',') s++;
        nome = s;
        while (*s && *s != '(') s++;
        if (!*s) break;
        s++;
        while (n < 6) { float v = svg_num(s, &f); if (!f) break; a[n++] = v; s = f; }
        while (*s && *s != ')') s++;
        if (*s) s++;
        if (!strncmp(nome, "matrix", 6) && n == 6) for (k = 0; k < 6; k++) b[k] = a[k];
        else if (!strncmp(nome, "translate", 9) && n >= 1) { b[4] = a[0]; b[5] = n > 1 ? a[1] : 0; }
        else if (!strncmp(nome, "scale", 5) && n >= 1) { b[0] = a[0]; b[3] = n > 1 ? a[1] : a[0]; }
        else if (!strncmp(nome, "rotate", 6) && n >= 1) {
            int   g = (int)(a[0] >= 0 ? a[0] + 0.5f : a[0] - 0.5f);
            float c = seno_g(g + 90), sn = seno_g(g);

            if (n == 3) {
                float t1[6] = { 1, 0, 0, 1, a[1], a[2] }, t2[6] = { 1, 0, 0, 1, -a[1], -a[2] }, rr[6] = { c, sn, -sn, c, 0, 0 };

                svg_matrice_per(m, t1); svg_matrice_per(m, rr); svg_matrice_per(m, t2);
                continue;
            }
            b[0] = c; b[1] = sn; b[2] = -sn; b[3] = c;
        } else if (!strncmp(nome, "skewX", 5) && n >= 1) b[2] = seno_g((int)a[0]) / seno_g((int)a[0] + 90);
        else if (!strncmp(nome, "skewY", 5) && n >= 1) b[1] = seno_g((int)a[0]) / seno_g((int)a[0] + 90);
        else continue;
        svg_matrice_per(m, b);
    }
    pv_trasforma(t, m);
}

/* ! A GRADIENT OF THE SVG (linearGradient, radialGradient): its stops (or
 * those of the one its href names), its units (the shape's bounding box by
 * default), gradientTransform and spreadMethod */
static float svg_frazione(int n, const char *nome, float predef)
{
    char        b[64];
    const char *f;
    float       v;

    if (!svg_prop(n, nome, b, sizeof(b))) return predef;
    v = svg_num(b, &f);
    if (!f) return predef;
    return *f == '%' ? v / 100 : v;
}

static int svg_sfumatura(void *t, int g, float alfa)
{
    PvFermata   fe[16];
    int         n = 0, s, sorg = g, giri = 0, spread = 0, utente;
    float       bb[4], m[6] = { 1, 0, 0, 1, 0, 0 };
    char        b[256];
    const char *hr;

    /* the stops: here, or along href */
    while (g_sd->nodi[sorg].primo_figlio < 0 && giri++ < 4) {
        hr = html_attr(g_sd, sorg, "href");
        if (!hr) hr = html_attr(g_sd, sorg, "xlink:href");
        if (!hr || hr[0] != '#' || (s = svg_per_id(hr + 1)) < 0) break;
        sorg = s;
    }
    for (s = g_sd->nodi[sorg].primo_figlio; s >= 0 && n < 16; s = g_sd->nodi[s].prossimo) {
        unsigned int c = 0xFF000000u;
        float        o;

        if (g_sd->nodi[s].tipo != HTML_ELEMENTO || strcmp(html_nome(g_sd, s), "stop")) continue;
        if (svg_prop(s, "stop-color", b, sizeof(b))) css_colore(b, (unsigned int)strlen(b), &c);
        o = svg_frazione(s, "stop-opacity", 1);
        fe[n].pos = svg_frazione(s, "offset", 0);
        if (n > 0 && fe[n].pos < fe[n - 1].pos) fe[n].pos = fe[n - 1].pos;
        fe[n].c.r = ((c >> 16) & 0xFF) / 255.0f;
        fe[n].c.g = ((c >> 8) & 0xFF) / 255.0f;
        fe[n].c.b = (c & 0xFF) / 255.0f;
        fe[n].c.a = alfa_di(c) / 255.0f * o * alfa;
        n++;
    }
    if (n == 0) return 0;
    if (svg_prop(g, "spreadMethod", b, sizeof(b))) spread = !strcmp(b, "reflect") ? 1 : !strcmp(b, "repeat") ? 2 : 0;
    utente = svg_prop(g, "gradientUnits", b, sizeof(b)) && !strcmp(b, "userSpaceOnUse");
    if (!utente) {
        pp_estensione(pv_percorso(t), bb, 0);
        if (bb[2] <= 0 || bb[3] <= 0) return 0;
        m[0] = bb[2]; m[3] = bb[3]; m[4] = bb[0]; m[5] = bb[1];
    }
    hr = html_attr(g_sd, g, "gradientTransform");
    if (hr) {
        /* through a canvas we do not draw on: the matrix only */
        float id[6] = { 1, 0, 0, 1, 0, 0 }, gt[6];
        void *s2 = pv_sup_crea(1, 1), *t2 = s2 ? pv_tela(s2) : 0;

        if (t2) {
            pv_matrice_metti(t2, id);
            svg_trasforma(t2, hr);
            pv_matrice_leggi(t2, gt);
            svg_matrice_per(m, gt);
            pv_tela_via(t2);
        }
        if (s2) pv_sup_via(s2);
    }
    if (strstr(html_nome(g_sd, g), "inear")) {
        float x1 = svg_frazione(g, "x1", 0), y1 = svg_frazione(g, "y1", 0);
        float x2 = svg_frazione(g, "x2", utente ? 0 : 1), y2 = svg_frazione(g, "y2", 0);

        if (utente) {
            x1 = svg_lung(g, "x1", 1, 0); y1 = svg_lung(g, "y1", 1, 0);
            x2 = svg_lung(g, "x2", 1, 0); y2 = svg_lung(g, "y2", 1, 0);
        }
        pv_lineare(t, x1, y1, x2, y2, spread, fe, n, m);
    } else {
        float cx = svg_frazione(g, "cx", 0.5f), cy = svg_frazione(g, "cy", 0.5f), rr = svg_frazione(g, "r", 0.5f);
        float fx = svg_frazione(g, "fx", cx), fy = svg_frazione(g, "fy", cy);

        if (utente) {
            cx = svg_lung(g, "cx", 1, 0); cy = svg_lung(g, "cy", 1, 0); rr = svg_lung(g, "r", 1, 0);
            fx = svg_lung(g, "fx", 1, cx); fy = svg_lung(g, "fy", 1, cy);
        }
        pv_radiale(t, cx, cy, rr, fx, fy, 0, spread, fe, n, m);
    }
    return 1;
}

static void svg_disegna_forma(void *t, const SvgPittura *p)
{
    float op = p->opac;

    if (!p->fill_no) {
        unsigned int c = p->fill;

        if (!p->fill_grad || !svg_sfumatura(t, p->fill_grad - 1, p->fill_op * op))
        pv_rgba(t, ((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f,
                alfa_di(c) / 255.0f * p->fill_op * op);
        pv_regola(t, p->regola);
        pv_riempi_tieni(t);
    }
    if (!p->stroke_no && p->sw > 0) {
        unsigned int c = p->stroke;

        if (!p->stroke_grad || !svg_sfumatura(t, p->stroke_grad - 1, p->stroke_op * op))
        pv_rgba(t, ((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f,
                alfa_di(c) / 255.0f * p->stroke_op * op);
        pv_spessore(t, p->sw);
        pv_capo(t, p->capo);
        pv_giunto(t, p->giunto);
        pv_tratteggio(t, 0, p->n_tratti ? p->tratti : 0, p->n_tratti);
        pv_traccia(t);
    }
    pv_nuovo(t);
}

static void svg_punti(void *t, const char *s, int chiudi)
{
    const char *f;
    int         primo = 1;

    for (;;) {
        float x = svg_num(s, &f), y;

        if (!f) break;
        y = svg_num(f, &f);
        if (!f) break;
        if (primo) pv_muovi(t, x, y); else pv_linea(t, x, y);
        primo = 0;
        s = f;
    }
    if (chiudi && !primo) pv_chiudi(t);
}

static int svg_per_id(const char *id)
{
    int m;

    for (m = 0; m < (int)g_sd->nodi_n; m++) {
        const char *i2;

        if (g_sd->nodi[m].tipo != HTML_ELEMENTO) continue;
        i2 = html_attr(g_sd, m, "id");
        if (i2 && !strcmp(i2, id)) return m;
    }
    return -1;
}

static void svg_nodo(void *t, int n, SvgPittura ered, float vw, float vh, int prof);

/* the viewBox of an <svg> or <symbol> into (0,0,w,h) */
static void svg_vista(void *t, int n, float w, float h, float *vw, float *vh)
{
    char        b[128];
    const char *f;
    float       v[4];
    int         k;

    *vw = w; *vh = h;
    if (!svg_prop(n, "viewBox", b, sizeof(b))) return;
    for (k = 0, f = b; k < 4; k++) { v[k] = svg_num(f, &f); if (!f) return; }
    if (v[2] <= 0 || v[3] <= 0) return;
    {
        float sx = w / v[2], sy = h / v[3], sc;
        char  a[64];

        if (svg_prop(n, "preserveAspectRatio", a, sizeof(a)) && !strncmp(a, "none", 4)) {
            float m[6] = { sx, 0, 0, sy, -v[0] * sx, -v[1] * sy };

            pv_trasforma(t, m);
        } else {
            float m[6];

            sc = sx < sy ? sx : sy;
            m[0] = sc; m[1] = 0; m[2] = 0; m[3] = sc;
            m[4] = (w - v[2] * sc) / 2 - v[0] * sc;
            m[5] = (h - v[3] * sc) / 2 - v[1] * sc;
            pv_trasforma(t, m);
        }
        *vw = v[2]; *vh = v[3];
    }
}

static void svg_figli(void *t, int n, SvgPittura p, float vw, float vh, int prof)
{
    int f;

    for (f = g_sd->nodi[n].primo_figlio; f >= 0; f = g_sd->nodi[f].prossimo)
        if (g_sd->nodi[f].tipo == HTML_ELEMENTO) svg_nodo(t, f, p, vw, vh, prof + 1);
}

static void svg_nodo(void *t, int n, SvgPittura p, float vw, float vh, int prof)
{
    const char *nome = html_nome(g_sd, n);
    char        b[64];
    const char *tr;

    if (prof > 40) return;
    if (!strcmp(nome, "defs") || !strcmp(nome, "symbol") || !strcmp(nome, "title") || !strcmp(nome, "desc") ||
        !strcmp(nome, "clippath") || !strcmp(nome, "clipPath") || !strcmp(nome, "mask") || !strcmp(nome, "style") ||
        !strcmp(nome, "lineargradient") || !strcmp(nome, "linearGradient") || !strcmp(nome, "radialgradient") ||
        !strcmp(nome, "radialGradient") || !strcmp(nome, "pattern") || !strcmp(nome, "filter") ||
        !strcmp(nome, "script") || !strcmp(nome, "metadata") || !strcmp(nome, "text"))
        return;
    if (svg_prop(n, "display", b, sizeof(b)) && !strcmp(b, "none")) return;
    if (svg_prop(n, "visibility", b, sizeof(b)) && !strcmp(b, "hidden")) return;
    svg_pittura(n, &p);
    if (svg_prop(n, "opacity", b, sizeof(b))) p.opac *= svg_num(b, 0);
    pv_salva(t);
    tr = html_attr(g_sd, n, "transform");
    if (tr) svg_trasforma(t, tr);

    if (!strcmp(nome, "g") || !strcmp(nome, "a") || !strcmp(nome, "switch")) {
        svg_figli(t, n, p, vw, vh, prof);
    } else if (!strcmp(nome, "svg")) {
        /* a nested <svg>: its own viewport */
        float x = svg_lung(n, "x", vw, 0), y = svg_lung(n, "y", vh, 0);
        float w = svg_lung(n, "width", vw, vw), h = svg_lung(n, "height", vh, vh), nw, nh;

        pv_trasla(t, x, y);
        svg_vista(t, n, w, h, &nw, &nh);
        svg_figli(t, n, p, nw, nh, prof);
    } else if (!strcmp(nome, "use")) {
        const char *h = html_attr(g_sd, n, "href");
        int         m;

        if (!h) h = html_attr(g_sd, n, "xlink:href");
        if (h && h[0] == '#' && (m = svg_per_id(h + 1)) >= 0 && m != n) {
            float x = svg_lung(n, "x", vw, 0), y = svg_lung(n, "y", vh, 0);

            pv_trasla(t, x, y);
            if (!strcmp(html_nome(g_sd, m), "symbol") || !strcmp(html_nome(g_sd, m), "svg")) {
                float w = svg_lung(n, "width", vw, vw), hh = svg_lung(n, "height", vh, vh), nw, nh;

                svg_pittura(m, &p);
                svg_vista(t, m, w, hh, &nw, &nh);
                svg_figli(t, m, p, nw, nh, prof + 1);
            } else svg_nodo(t, m, p, vw, vh, prof + 1);
        }
    } else if (!strcmp(nome, "path")) {
        const char *d = html_attr(g_sd, n, "d");

        if (d && d[0]) {
            void *pa = pv_path();

            if (pa) {
                if (pv_path_leggi(pa, d, (int)strlen(d))) { pv_add_path(t, pa); svg_disegna_forma(t, &p); }
                pv_path_via(pa);
            }
        }
    } else if (!strcmp(nome, "rect")) {
        float x = svg_lung(n, "x", vw, 0), y = svg_lung(n, "y", vh, 0);
        float w = svg_lung(n, "width", vw, 0), h = svg_lung(n, "height", vh, 0);
        float rx = svg_lung(n, "rx", vw, -1), ry = svg_lung(n, "ry", vh, -1);

        if (w > 0 && h > 0) {
            if (rx < 0) rx = ry;
            if (ry < 0) ry = rx;
            if (rx > 0) pv_tondo(t, x, y, w, h, rx > w / 2 ? w / 2 : rx, ry > h / 2 ? h / 2 : ry);
            else pv_rett(t, x, y, w, h);
            svg_disegna_forma(t, &p);
        }
    } else if (!strcmp(nome, "circle")) {
        float r = svg_lung(n, "r", vw, 0);

        if (r > 0) { pv_ellisse(t, svg_lung(n, "cx", vw, 0), svg_lung(n, "cy", vh, 0), r, r); svg_disegna_forma(t, &p); }
    } else if (!strcmp(nome, "ellipse")) {
        float rx = svg_lung(n, "rx", vw, 0), ry = svg_lung(n, "ry", vh, 0);

        if (rx > 0 && ry > 0) { pv_ellisse(t, svg_lung(n, "cx", vw, 0), svg_lung(n, "cy", vh, 0), rx, ry); svg_disegna_forma(t, &p); }
    } else if (!strcmp(nome, "line")) {
        pv_muovi(t, svg_lung(n, "x1", vw, 0), svg_lung(n, "y1", vh, 0));
        pv_linea(t, svg_lung(n, "x2", vw, 0), svg_lung(n, "y2", vh, 0));
        p.fill_no = 1;
        svg_disegna_forma(t, &p);
    } else if (!strcmp(nome, "polyline") || !strcmp(nome, "polygon")) {
        const char *pt = html_attr(g_sd, n, "points");

        if (pt) { svg_punti(t, pt, nome[4] == 'g'); svg_disegna_forma(t, &p); }
    }
    pv_ripristina(t);
}

/* the <svg> node n in the rectangle (x, y, w, h) of the screen, cut to the
 * clip; `colore` is its CSS colour (currentColor) */
void dis_svg(int n, unsigned int colore, int x, int y, int w, int h, int cx, int cy, int cw, int ch)
{
    int         ox = x, oy = y, ow = w, oh = h;
    void       *sup, *t;
    SvgPittura  p;
    float       vw, vh;

    if (w <= 0 || h <= 0 || !dis_disponibile()) return;
    g_sd = &g_doc;
    if (ox < cx) { ow -= cx - ox; ox = cx; }
    if (oy < cy) { oh -= cy - oy; oy = cy; }
    if (ox + ow > cx + cw) ow = cx + cw - ox;
    if (oy + oh > cy + ch) oh = cy + ch - oy;
    if (ow <= 0 || oh <= 0) return;
    sup = pv_sup_crea(ow, oh);
    if (!sup) return;
    t = pv_tela(sup);
    if (!t) { pv_sup_via(sup); return; }
    pv_trasla(t, (float)(x - ox), (float)(y - oy));
    memset(&p, 0, sizeof(p));
    p.corrente = colore;
    p.fill = 0xFF000000u;           /* SVG's default fill: black */
    p.stroke_no = 1;
    p.fill_op = p.stroke_op = p.opac = 1;
    p.sw = 1;
    svg_vista(t, n, (float)w, (float)h, &vw, &vh);
    svg_pittura(n, &p);
    svg_figli(t, n, p, vw, vh, 0);
    pv_tela_via(t);
    posa(sup, ox, oy, ow, oh);
    pv_sup_via(sup);
}

/* the size an <svg> asks for: width/height, or the viewBox's ratio */
static void dis_svg_misura_tutta(int n, int larg_css, int alt_css, int *w, int *h);

void dis_svg_misura(int n, int larg_css, int alt_css, int *w, int *h)
{
    g_sd = &g_doc;
    dis_svg_misura_tutta(n, larg_css, alt_css, w, h);
}

static void dis_svg_misura_in(int n, int *w, int *h)
{
    dis_svg_misura_tutta(n, 0, 0, w, h);
}

static void dis_svg_misura_tutta(int n, int larg_css, int alt_css, int *w, int *h)
{
    char        b[128];
    const char *f;
    float       v[4], aw = -1, ah = -1, rapp = 0;
    int         k;

    if (svg_prop(n, "viewBox", b, sizeof(b))) {
        for (k = 0, f = b; k < 4 && f; k++) v[k] = svg_num(f, &f);
        if (f && v[2] > 0 && v[3] > 0) rapp = v[3] / v[2];
    }
    if (html_attr(g_sd, n, "width")) { aw = svg_num(html_attr(g_sd, n, "width"), &f); if (!f || *f == '%') aw = -1; }
    if (html_attr(g_sd, n, "height")) { ah = svg_num(html_attr(g_sd, n, "height"), &f); if (!f || *f == '%') ah = -1; }
    if (larg_css > 0) aw = (float)larg_css;
    if (alt_css > 0) ah = (float)alt_css;
    if (aw < 0 && ah >= 0 && rapp > 0) aw = ah / rapp;
    if (ah < 0 && aw >= 0 && rapp > 0) ah = aw * rapp;
    if (aw < 0) aw = 300;
    if (ah < 0) ah = rapp > 0 ? aw * rapp : 150;
    *w = (int)(aw + 0.5f);
    *h = (int)(ah + 0.5f);
}

/* =============================================================================
 * AN SVG IMAGE (P10): <img src="logo.svg">, a background. Its own document,
 * parsed with exhtml into buffers of its own, drawn at the size the page
 * gives it into straight ARGB pixels (what eximg gives the browser).
 * ========================================================================== */
#define SVGI_NODI  8192
#define SVGI_ATTR  16384
#define SVGI_ARENA (512 * 1024)
static HtmlDoc   g_svgi;
static HtmlNodo *g_svgi_nodi;
static HtmlAttr *g_svgi_attr;
static char     *g_svgi_arena;
static int       g_svgi_radice = -1;

int dis_svg_immagine_apri(const unsigned char *b, unsigned int n, int *iw, int *ih)
{
    unsigned int i;
    int          k;

    g_svgi_radice = -1;
    if (!dis_disponibile() || n < 5) return 0;
    for (i = 0; i + 4 < n && i < 4096; i++) if (!memcmp(b + i, "<svg", 4)) break;
    if (i + 4 >= n || i >= 4096) return 0;
    if (!g_svgi_nodi) {
        g_svgi_nodi = (HtmlNodo *)malloc(SVGI_NODI * sizeof(HtmlNodo));
        g_svgi_attr = (HtmlAttr *)malloc(SVGI_ATTR * sizeof(HtmlAttr));
        g_svgi_arena = (char *)malloc(SVGI_ARENA);
        if (!g_svgi_nodi || !g_svgi_attr || !g_svgi_arena) return 0;
    }
    html_prepara(&g_svgi, g_svgi_nodi, SVGI_NODI, g_svgi_attr, SVGI_ATTR, g_svgi_arena, SVGI_ARENA);
    if (!html_analizza(&g_svgi, (const char *)b, n)) return 0;
    for (k = 0; k < (int)g_svgi.nodi_n; k++)
        if (g_svgi.nodi[k].tipo == HTML_ELEMENTO && !strcmp(html_nome(&g_svgi, k), "svg")) break;
    if (k >= (int)g_svgi.nodi_n) return 0;
    g_svgi_radice = k;
    g_sd = &g_svgi;
    dis_svg_misura_in(k, iw, ih);
    return 1;
}

unsigned int *dis_svg_immagine(int w, int h)
{
    void         *sup, *t;
    unsigned int *px, *d;
    SvgPittura    p;
    float         vw, vh;
    int           i;

    if (g_svgi_radice < 0 || w <= 0 || h <= 0 || (long)w * h > 4L * 1024 * 1024) return 0;
    sup = pv_sup_crea(w, h);
    if (!sup) return 0;
    t = pv_tela(sup);
    if (!t) { pv_sup_via(sup); return 0; }
    g_sd = &g_svgi;
    memset(&p, 0, sizeof(p));
    p.corrente = 0xFF000000u;
    p.fill = 0xFF000000u;
    p.stroke_no = 1;
    p.fill_op = p.stroke_op = p.opac = 1;
    p.sw = 1;
    svg_vista(t, g_svgi_radice, (float)w, (float)h, &vw, &vh);
    svg_pittura(g_svgi_radice, &p);
    svg_figli(t, g_svgi_radice, p, vw, vh, 0);
    pv_tela_via(t);
    px = (unsigned int *)malloc((unsigned int)(w * h) * sizeof(unsigned int));
    d = (unsigned int *)pv_sup_dati(sup);
    if (px && d)
        for (i = 0; i < w * h; i++) {
            unsigned int v = d[i], a = v >> 24;

            px[i] = (a == 0 || a == 255) ? v
                  : (a << 24) | (((v >> 16 & 0xFF) * 255 / a) << 16) | (((v >> 8 & 0xFF) * 255 / a) << 8) | ((v & 0xFF) * 255 / a);
        }
    pv_sup_via(sup);
    g_sd = &g_doc;
    return px;
}

/* =============================================================================
 * <canvas> 2D (P10, 26 November 2026)
 *
 * Each canvas of a view is a plutovg surface as big as its width and height
 * attributes; the prelude's CanvasRenderingContext2D sends its operations
 * here one by one (__exos_tela in exbrowser.c, TELA_* below), and the layout
 * draws the surface as a piece (browser_estranei.c), scaled to the CSS size.
 *
 * ! THE PATH, THE MATRIX, THE CLIP AND save/restore ARE plutovg's: the
 * canvas API was modelled on the same PostScript model, and plutovg keeps
 * them per canvas as the 2D context does. The styles (fillStyle and the
 * others) stay in the JavaScript object and come with each fill or stroke.
 * ========================================================================== */
#define TELE_MAX 32
typedef struct {
    const void *chiave;         /* the view (g_vi) */
    int         nodo, w, h;
    void       *sup, *t;
    /* ! THE CONTEXT'S PATH IS ITS OWN, in device coordinates: the canvas
     * standard transforms each point when it is added, and fillRect,
     * clearRect and fillText leave the path alone - plutovg's canvas path
     * is used by those, and rebuilt from this one to fill or stroke */
    void       *up, *tmp;
} Tela;

static Tela  g_tele[TELE_MAX];
static int   g_tela_sporca;
static void *g_facce[12];       /* Liberation: sans serif mono x regular bold italic bolditalic */
static int   g_facce_cercate[12];

int dis_tela_sporca(void)
{
    int s = g_tela_sporca;

    g_tela_sporca = 0;
    return s;
}

static Tela *tela_di(int nodo, int w, int h, int crea)
{
    int   i, libera = -1;
    Tela *t;

    for (i = 0; i < TELE_MAX; i++) {
        if (g_tele[i].sup && g_tele[i].chiave == g_vi && g_tele[i].nodo == nodo) break;
        if (!g_tele[i].sup && libera < 0) libera = i;
    }
    if (i < TELE_MAX) {
        t = &g_tele[i];
        /* ! SETTING width OR height CLEARS THE CANVAS, as the standard says:
         * here, a new surface of the new size */
        if (w > 0 && h > 0 && (t->w != w || t->h != h) && crea) {
            void *s2 = pv_sup_crea(w, h), *t2 = s2 ? pv_tela(s2) : 0;

            if (!t2) { if (s2) pv_sup_via(s2); return t; }
            pv_tela_via(t->t); pv_sup_via(t->sup);
            t->sup = s2; t->t = t2; t->w = w; t->h = h;
            pp_azzera(t->up);
        }
        return t;
    }
    if (!crea || libera < 0 || w <= 0 || h <= 0 || w > 4096 || h > 4096 || (long)w * h > 4L * 1024 * 1024) return 0;
    t = &g_tele[libera];
    t->sup = pv_sup_crea(w, h);
    if (!t->sup) return 0;
    t->t = pv_tela(t->sup);
    if (!t->t) { pv_sup_via(t->sup); t->sup = 0; return 0; }
    if (!t->up) t->up = pv_path();
    if (!t->tmp) t->tmp = pv_path();
    if (!t->up || !t->tmp) { pv_tela_via(t->t); pv_sup_via(t->sup); t->sup = 0; return 0; }
    pp_azzera(t->up);
    t->chiave = g_vi; t->nodo = nodo; t->w = w; t->h = h;
    return t;
}

/* the page is going: its canvases with it */
void dis_tele_via(void)
{
    int i;

    for (i = 0; i < TELE_MAX; i++)
        if (g_tele[i].sup && g_tele[i].chiave == g_vi) {
            pv_tela_via(g_tele[i].t);
            pv_sup_via(g_tele[i].sup);
            g_tele[i].sup = g_tele[i].t = 0;
        }
}

/* a style: "c|colour", "l|x0|y0|x1|y1|pos|colour|...", "r|x0|y0|r0|x1|y1|r1|pos|colour|..." */
static void tela_stile(void *t, const char *s, float alfa, const float *mat)
{
    if (!s) s = "c|#000";
    if (s[0] == 'l' || s[0] == 'r') {
        PvFermata   f[16];
        float       v[6];
        int         nv = s[0] == 'l' ? 4 : 6, k, n = 0;
        const char *q = s + 1, *fi;

        for (k = 0; k < nv; k++) { if (*q != '|') return; v[k] = svg_num(q + 1, &fi); if (!fi) return; q = fi; }
        while (*q == '|' && n < 16) {
            char         col[64];
            int          m = 0;
            unsigned int c;

            f[n].pos = svg_num(q + 1, &fi);
            if (!fi || *fi != '|') break;
            q = fi + 1;
            while (*q && *q != '|' && m < 63) col[m++] = *q++;
            col[m] = 0;
            if (!css_colore(col, (unsigned int)m, &c)) continue;
            f[n].c.r = ((c >> 16) & 0xFF) / 255.0f;
            f[n].c.g = ((c >> 8) & 0xFF) / 255.0f;
            f[n].c.b = (c & 0xFF) / 255.0f;
            f[n].c.a = ((c >> 24) & 0xFF) / 255.0f * alfa;
            n++;
        }
        if (n == 0) { pv_rgba(t, 0, 0, 0, 0); return; }
        if (s[0] == 'l') pv_lineare(t, v[0], v[1], v[2], v[3], 0, f, n, mat);
        else pv_radiale(t, v[3], v[4], v[5], v[0], v[1], v[2], 0, f, n, mat);
        return;
    }
    {
        unsigned int c;
        const char  *cs = s[0] == 'c' && s[1] == '|' ? s + 2 : s;

        if (!css_colore(cs, (unsigned int)strlen(cs), &c)) c = 0xFF000000u;
        pv_rgba(t, ((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f,
                ((c >> 24) & 0xFF) / 255.0f * alfa);
    }
}

static void *tela_faccia(int fam, int stile)
{
    static const char *FAM[3] = { "Sans", "Serif", "Mono" };
    static const char *ST[4] = { "Regular", "Bold", "Italic", "BoldItalic" };
    int k = fam * 4 + stile;
    char p[96];

    if (fam < 0 || fam > 2 || stile < 0 || stile > 3) return 0;
    if (!g_facce_cercate[k]) {
        g_facce_cercate[k] = 1;
        snprintf(p, sizeof(p), "/exwin/font/Liberation%s-%s.ttf", FAM[fam], ST[stile]);
        g_facce[k] = pv_faccia_file(p, 0);
    }
    return g_facce[k];
}

/* an <img> of this view, or another canvas, as a surface to draw from; the
 * caller destroys it if *propria */
static void *tela_sorgente(int nodo, int *w, int *h, int *propria)
{
    const unsigned int *px;
    int                 i;

    *propria = 0;
    for (i = 0; i < TELE_MAX; i++)
        if (g_tele[i].sup && g_tele[i].chiave == g_vi && g_tele[i].nodo == nodo) {
            *w = g_tele[i].w; *h = g_tele[i].h;
            return g_tele[i].sup;
        }
    px = est_imm_px(nodo, w, h);
    if (px && *w > 0 && *h > 0) {
        void         *s = pv_sup_crea(*w, *h);
        unsigned int *d = s ? (unsigned int *)pv_sup_dati(s) : 0;
        int           n = *w * *h;

        if (!d) { if (s) pv_sup_via(s); return 0; }
        for (i = 0; i < n; i++) {
            unsigned int v = px[i], a = v >> 24;

            /* ! eximg's pixels: alpha 0 there means opaque when the image
             * has no alpha at all - as ExWin's colours */
            if (a == 255 || a == 0) d[i] = v | 0xFF000000u;
            else d[i] = (a << 24) | (((v >> 16 & 0xFF) * a / 255) << 16) |
                        (((v >> 8 & 0xFF) * a / 255) << 8) | ((v & 0xFF) * a / 255);
        }
        *propria = 1;
        return s;
    }
    return 0;
}

/* a point through the current matrix */
static void mappa(const float *m, float x, float y, float *xx, float *yy)
{
    *xx = m[0] * x + m[2] * y + m[4];
    *yy = m[1] * x + m[3] * y + m[5];
}

/* tmp, built in user coordinates, appended to the path through the matrix;
 * with `collega` its first point is joined to the current one by a line,
 * as arc() does */
static void unisci(Tela *tt, const float *m, int collega)
{
    const int *el;
    int        n = pp_elementi(tt->tmp, &el), i = 0, prima = 1;
    const int *u;
    int        nu = pp_elementi(tt->up, &u);

    while (i < n) {
        int          cmd = el[i * 2], len = el[i * 2 + 1];
        const float *pt = (const float *)(el + (i + 1) * 2);
        float        x[3], y[3];
        int          k;

        if (len < 1) break;
        for (k = 0; k < 3 && k < len - 1; k++) mappa(m, pt[k * 2], pt[k * 2 + 1], &x[k], &y[k]);
        switch (cmd) {
        case 0: if (prima && collega && nu > 0) pp_linea(tt->up, x[0], y[0]); else pp_muovi(tt->up, x[0], y[0]); break;
        case 1: pp_linea(tt->up, x[0], y[0]); break;
        case 2: pp_cubica(tt->up, x[0], y[0], x[1], y[1], x[2], y[2]); break;
        case 3: pp_chiudi(tt->up); break;
        default: break;
        }
        prima = 0;
        i += len;
    }
    pp_azzera(tt->tmp);
}

/* fill, stroke or clip the context's path: with the identity matrix, since
 * its points are already in the device's; the paint keeps the user's */
static void col_percorso(Tela *tt, float *m)
{
    pv_matrice_leggi(tt->t, m);
    pv_matrice_zero(tt->t);
    pv_nuovo(tt->t);
    pv_add_path(tt->t, tt->up);
}

double dis_tela_op(int nodo, int w, int h, int op, const double *a, int na, const char *s1, const char *s2)
{
    Tela  *tt;
    void  *t;
    float  f[8];
    int    i;

    if (!dis_disponibile() || nodo < 0) return 0;
    tt = tela_di(nodo, w, h, 1);
    if (!tt) return 0;
    t = tt->t;
    for (i = 0; i < 8; i++) f[i] = i < na ? (float)a[i] : 0;
    switch (op) {
    case TELA_MUOVI: case TELA_LINEA: case TELA_CUBICA: case TELA_QUAD: {
        float m[6], x[3], y[3];
        const int *u;
        int k;

        pv_matrice_leggi(t, m);
        for (k = 0; k < 3; k++) mappa(m, f[k * 2], f[k * 2 + 1], &x[k], &y[k]);
        /* lineTo on an empty path is a moveTo, as the standard says; a
         * curve starts from its first control point */
        if (op == TELA_MUOVI || pp_elementi(tt->up, &u) == 0) {
            pp_muovi(tt->up, x[0], y[0]);
            if (op != TELA_CUBICA && op != TELA_QUAD) return 0;
        }
        if (op == TELA_LINEA) pp_linea(tt->up, x[0], y[0]);
        else if (op == TELA_CUBICA) pp_cubica(tt->up, x[0], y[0], x[1], y[1], x[2], y[2]);
        else if (op == TELA_QUAD) pp_quad(tt->up, x[0], y[0], x[1], y[1]);
        return 0;
    }
    case TELA_ARCO: case TELA_RETT: case TELA_ELLISSE: {
        float m[6];

        pv_matrice_leggi(t, m);
        pp_azzera(tt->tmp);
        if (op == TELA_RETT) pp_rett(tt->tmp, f[0], f[1], f[2], f[3]);
        else if (op == TELA_ARCO) pp_arco(tt->tmp, f[0], f[1], f[2], f[3], f[4], f[5] != 0);
        else {
            /* ellipse(x, y, rx, ry, rotation, a0, a1, ccw): the unit
             * circle's arc, scaled, turned and moved */
            float c = 1, sn = 0, e[6];

            pp_arco(tt->tmp, 0, 0, 1, f[5], f[6], f[7] != 0);
            if (f[4] != 0) {
                int g = (int)(f[4] * 57.29578f);

                c = seno_g(g + 90); sn = seno_g(g);
            }
            e[0] = c * f[2]; e[1] = sn * f[2]; e[2] = -sn * f[3]; e[3] = c * f[3]; e[4] = f[0]; e[5] = f[1];
            pp_trasforma(tt->tmp, e);
        }
        unisci(tt, m, op != TELA_RETT);
        return 0;
    }
    case TELA_CHIUDI:   pp_chiudi(tt->up); return 0;
    case TELA_NUOVO:    pp_azzera(tt->up); return 0;
    case TELA_RIEMPI: {
        /* fill(rule): style s1, globalAlpha f[1] */
        float m[6];

        col_percorso(tt, m);
        tela_stile(t, s1, na > 1 ? f[1] : 1, m);
        pv_regola(t, f[0] != 0);
        pv_riempi(t);
        pv_matrice_metti(t, m);
        g_tela_sporca = 1;
        return 0;
    }
    case TELA_RETT_PIENO: case TELA_RETT_TRACCIA:
        /* fillRect, strokeRect: plutovg's own path, the path stays; style
         * s1, then as fill and stroke from f[4] */
        pv_nuovo(t);
        pv_rett(t, f[0], f[1], f[2], f[3]);
        if (op == TELA_RETT_PIENO) {
            tela_stile(t, s1, f[4], 0);
            pv_regola(t, 0);
            pv_riempi(t);
        } else {
            tela_stile(t, s1, f[4], 0);
            pv_spessore(t, f[5] > 0 ? f[5] : 1);
            pv_giunto(t, (int)f[6]);
            pv_tratteggio(t, 0, 0, 0);
            pv_traccia(t);
        }
        g_tela_sporca = 1;
        return 0;
    case TELA_TRACCIA: {
        /* stroke(): style s1, alpha, width, cap, join, miter */
        float m[6], sc;

        col_percorso(tt, m);
        tela_stile(t, s1, f[0], m);
        /* the width is the user's: through the matrix's scale */
        sc = radice_q(m[0] * m[3] - m[1] * m[2] < 0 ? m[1] * m[2] - m[0] * m[3] : m[0] * m[3] - m[1] * m[2]);
        if (sc <= 0) sc = 1;
        pv_spessore(t, (f[1] > 0 ? f[1] : 1) * sc);
        pv_capo(t, (int)f[2]);
        pv_giunto(t, (int)f[3]);
        pv_giunto_lim(t, f[4] > 0 ? f[4] : 10);
        {
            /* the dashes: s2, "a b c" */
            float d[16];
            int   n = 0;
            const char *q = s2, *fi;

            while (q && n < 16) { float v = svg_num(q, &fi); if (!fi) break; d[n++] = v * sc; q = fi; }
            pv_tratteggio(t, f[5] * sc, n ? d : 0, n);
        }
        pv_traccia(t);
        pv_matrice_metti(t, m);
        g_tela_sporca = 1;
        return 0;
    }
    case TELA_RITAGLIA: {
        float m[6];

        col_percorso(tt, m);
        pv_regola(t, f[0] != 0);
        pv_ritaglia(t);
        pv_matrice_metti(t, m);
        return 0;
    }
    case TELA_PULISCI: {
        /* clearRect: DST_OUT with an opaque rectangle, outside the path */
        pv_salva(t);
        pv_op(t, PV_OP_DST_OUT);
        pv_rgba(t, 0, 0, 0, 1);
        pv_nuovo(t);
        pv_rett(t, f[0], f[1], f[2], f[3]);
        pv_riempi(t);
        pv_ripristina(t);
        g_tela_sporca = 1;
        return 0;
    }
    case TELA_SALVA:     pv_salva(t); return 0;
    case TELA_RIPRISTINA: pv_ripristina(t); return 0;
    case TELA_TRASLA:    pv_trasla(t, f[0], f[1]); return 0;
    case TELA_SCALA:     pv_scala(t, f[0], f[1]); return 0;
    case TELA_RUOTA:     pv_ruota(t, f[0]); return 0;
    case TELA_MATRICE:   pv_trasforma(t, f); return 0;
    case TELA_MATRICE_METTI: pv_matrice_metti(t, f); return 0;
    case TELA_TESTO:
    case TELA_TESTO_MISURA: {
        /* f: size, family (0 sans 1 serif 2 mono), style (0..3), x, y,
         * align (0 start/left 1 center 2 right/end), baseline (0 alphabetic
         * 1 top 2 middle 3 bottom 4 hanging), alpha; s1 the text, s2 style */
        void *fc = tela_faccia((int)f[1], (int)f[2]);
        float la = 0, x = f[3], y = f[4], su = 0, giu = 0, gap = 0, ext[4];

        if (!fc || !s1) return op == TELA_TESTO_MISURA ? (double)strlen(s1 ? s1 : "") * f[0] * 0.5 : 0;
        pv_font(t, fc, f[0] > 0 ? f[0] : 10);
        la = pv_testo_misura(t, s1, (int)strlen(s1), 1, 0);
        if (op == TELA_TESTO_MISURA) return la;
        pv_metriche(fc, f[0] > 0 ? f[0] : 10, &su, &giu, &gap, ext);
        if ((int)f[5] == 1) x -= la / 2;
        else if ((int)f[5] == 2) x -= la;
        switch ((int)f[6]) {
        case 1: case 4: y += su; break;
        case 2: y += (su + giu) / 2; break;
        case 3: y += giu; break;
        default: break;
        }
        tela_stile(t, s2, f[7], 0);
        pv_testo(t, s1, (int)strlen(s1), 1, x, y);
        g_tela_sporca = 1;
        return la;
    }
    case TELA_IMMAGINE: {
        /* drawImage(node f[0], sx sy sw sh, dx dy dw dh): f[1..8] are read
         * from a: 9 numbers */
        int    sw2, sh2, propria, src = (int)a[0];
        void  *s;
        double sx = na > 1 ? a[1] : 0, sy = na > 2 ? a[2] : 0, sw = na > 3 ? a[3] : -1, sh = na > 4 ? a[4] : -1;
        double dx = na > 5 ? a[5] : 0, dy = na > 6 ? a[6] : 0, dw = na > 7 ? a[7] : -1, dh = na > 8 ? a[8] : -1;
        float  m[6];

        s = tela_sorgente(src, &sw2, &sh2, &propria);
        if (!s) return 0;
        if (sw < 0) sw = sw2;
        if (sh < 0) sh = sh2;
        if (dw < 0) dw = sw;
        if (dh < 0) dh = sh;
        if (sw > 0 && sh > 0 && dw != 0 && dh != 0) {
            m[0] = (float)(dw / sw); m[1] = 0; m[2] = 0; m[3] = (float)(dh / sh);
            m[4] = (float)(dx - sx * dw / sw); m[5] = (float)(dy - sy * dh / sh);
            pv_nuovo(t);
            pv_tessuto(t, s, 0, na > 9 ? (float)a[9] : 1, m);
            pv_rett(t, (float)dx, (float)dy, (float)dw, (float)dh);
            pv_riempi(t);
            g_tela_sporca = 1;
        }
        if (propria) pv_sup_via(s);
        return 0;
    }
    case TELA_PIXEL: {
        /* getImageData(x, y): one pixel as 0xAARRGGBB straight (a test
         * helper and enough for the sites that look at one pixel) */
        int x = (int)f[0], y = (int)f[1];
        unsigned int v, al;

        if (x < 0 || y < 0 || x >= tt->w || y >= tt->h) return 0;
        v = ((unsigned int *)pv_sup_dati(tt->sup))[y * tt->w + x];
        al = v >> 24;
        if (al && al != 255)
            v = (al << 24) | (((v >> 16 & 0xFF) * 255 / al) << 16) | (((v >> 8 & 0xFF) * 255 / al) << 8) | ((v & 0xFF) * 255 / al);
        return (double)v;
    }
    default: return 0;
    }
}

/* the canvas of node n in its rectangle on the screen, scaled if the CSS
 * size is not the canvas's own */
void dis_tela_disegna(int nodo, int x, int y, int w, int h, int cx, int cy, int cw, int ch)
{
    Tela         *tt;
    unsigned int *sp, *riga;
    int           ox = x, oy = y, ow = w, oh = h, i, j;

    if (!dis_disponibile()) return;
    tt = tela_di(nodo, 0, 0, 0);
    if (!tt || w <= 0 || h <= 0) return;
    if (ox < cx) { ow -= cx - ox; ox = cx; }
    if (oy < cy) { oh -= cy - oy; oy = cy; }
    if (ox + ow > cx + cw) ow = cx + cw - ox;
    if (oy + oh > cy + ch) oh = cy + ch - oy;
    if (ow <= 0 || oh <= 0) return;
    sp = (unsigned int *)pv_sup_dati(tt->sup);
    riga = (unsigned int *)malloc((unsigned int)ow * sizeof(unsigned int));
    if (!sp || !riga) { free(riga); return; }
    for (j = 0; j < oh; j++) {
        int sy = (oy + j - y) * tt->h / h;

        for (i = 0; i < ow; i++) {
            int          sx = (ox + i - x) * tt->w / w;
            unsigned int v = sp[sy * tt->w + sx], a = v >> 24;

            if (a && a != 255)
                v = (a << 24) | (((v >> 16 & 0xFF) * 255 / a) << 16) | (((v >> 8 & 0xFF) * 255 / a) << 8) | ((v & 0xFF) * 255 / a);
            riga[i] = v;
        }
        ex_pixmap_blend(g_f, ox, oy + j, ow, 1, riga, (unsigned int)ow);
    }
    free(riga);
}

