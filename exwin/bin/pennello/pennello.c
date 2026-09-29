/* =============================================================================
 * exwin/bin/pennello/pennello.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Pennello, the paint program of ExWin (@PAINT, asked on 22 September 2026)
 *
 *     /exwin/bin/pennello [file]
 *
 * Paint Brush of Windows 3.11 is the model: a column of tools on the left, the
 * palette at the bottom, the picture in the middle. The tools: pencil, brush,
 * eraser, spray, fill, colour picker, line, rectangle and ellipse (empty or
 * full). Four widths. Zoom 1x to 8x. Undo and redo.
 *
 * ! WHAT IT OPENS AND WHAT IT SAVES ARE TWO LISTS, and the program says so
 * before, not at the moment of saving (the first question in @PAINT):
 *   - it OPENS whatever eximg reads: BMP, PNG, JPG, GIF, ICO;
 *   - it SAVES BMP and PNG (lib/eximg/scrivi.c). A picture opened from a JPG
 *     is saved as PNG unless the name says .bmp: the dialog of «Salva» shows
 *     the name with the extension already changed.
 *
 * ! THE PICTURE IS ARGB AT 32 BITS IN MEMORY, whatever the file was (the
 * second question): eximg gives that, and the writers convert when saving.
 *
 * ! UNDO LIVES IN A FIXED POOL, AND IT IS THE THIRD QUESTION. EX-OS's free()
 * gives nothing back to the system (@DIF-GROSSI), so a snapshot of the whole
 * picture at every stroke would be memory lost at every stroke. Instead, one
 * pool is taken at start (sized on the free memory, see pool_prendi), and each operation leaves in it
 * only the RECTANGLE it touched, with the pixels that were there before. When
 * the pool is full the oldest operation goes. Undo SWAPS the rectangle with
 * the picture, and so the same record becomes the redo: no second pool.
 *
 * ! THE CHOICES ARE THE USER'S, SO THEY LIVE IN THE PROFILE:
 * $HOME/.exwin/config/pennello.cfg — tool, width, the two colours and the
 * last directory (the rule in SVILUPPO.md).
 *
 * ! THE MOUSE HAS ONE BUTTON HERE. ExWin does not tell which button was
 * pressed, so the second colour (the background, what the eraser leaves) is
 * not «the right button» as in Paint Brush: it is the small square under the
 * first one, and X (or the «Scambia» button) swaps them.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"
#include "exlib.h"
#include "eximg.h"
#include "scrivi.h"
#include "kbd_proto.h"

/* +0.001 a ogni modifica: `pennello -version` la stampa. Vedi EX_VERSIONE. */
#define VERSIONE_APP "0.005"
EX_VERSIONE("pennello", VERSIONE_APP);

/* --- Geometry -------------------------------------------------------------- */
#define MENU_H      20
#define MARGINE     4
#define ATTR_W      124         /* the column of the tools: "Ellisse piena" fits */
#define TELA_X      (MARGINE + ATTR_W + 6)
#define TELA_Y      (MENU_H + 4)
#define BARRA       16          /* the scroll bars */
#define PAL_H       40          /* the palette */
#define STATO_H     20          /* the status line */
#define CELLA       16          /* a colour of the palette */
#define FIN_W       800
#define FIN_H       560

/* --- Limits ---------------------------------------------------------------- */
/* ! 2048 x 2048 IS 16 MB A BUFFER, AND THERE ARE TWO (the picture and the copy
 * an operation starts from). Bigger pictures are refused when opened, with a
 * message, instead of failing half-way in malloc. */
#define PIXEL_MAX   (2048u * 2048u)
#define UNDO_POOL_MAX (4u * 1024u * 1024u) /* in pixels: 16 MB at most */
#define UNDO_POOL_MIN (64u * 1024u)        /* 256 KB: an undo of a few strokes */
#define UNDO_MAX    64                     /* operations remembered */
#define PILA_MAX    65536                  /* seeds of the fill */

/* --- Ids --------------------------------------------------------------------- */
enum {
    ID_NUOVO = 1, ID_APRI, ID_SALVA, ID_SALVA_COME, ID_ESCI,
    ID_ANNULLA, ID_RIPETI, ID_CANCELLA,
    ID_SPECCHIO_O, ID_SPECCHIO_V, ID_RUOTA, ID_INVERTI, ID_DIMENSIONI,
    ID_ZOOM1, ID_ZOOM2, ID_ZOOM4, ID_ZOOM8,
    ID_COLORE, ID_SCAMBIA_M,
    ID_INFO, ID_ISTR,
    ID_BARRA_V = 30, ID_BARRA_O, ID_SCAMBIA,
    ID_STRUMENTO = 40,          /* + tool */
    ID_SPESSORE = 60            /* + width index */
};

enum {
    S_MATITA, S_PENNELLO, S_GOMMA, S_SPRUZZO, S_RIEMPI, S_CONTAGOCCE,
    S_LINEA, S_RETT, S_RETT_PIENO, S_ELLISSE, S_ELLISSE_PIENA, S_TESTO, N_STRUMENTI
};

static const char *const STRUMENTO_NOME[N_STRUMENTI] = {
    "Matita", "Pennello", "Gomma", "Spruzzo", "Riempi", "Contagocce",
    "Linea", "Rettangolo", "Rett. pieno", "Ellisse", "Ellisse piena", "Testo"
};
/* The key that chooses each tool, and its name in the profile. */
static const char STRUMENTO_TASTO[N_STRUMENTI] = { 'p', 'b', 'e', 'a', 'f', 'i', 'l', 'r', 'R', 'o', 'O', 't' };
static const char *const STRUMENTO_CFG[N_STRUMENTI] = {
    "matita", "pennello", "gomma", "spruzzo", "riempi", "contagocce",
    "linea", "rett", "rettpieno", "ellisse", "ellissepiena", "testo"
};

#define N_SPESSORI 4
static const int SPESSORE[N_SPESSORI] = { 1, 3, 5, 9 };
static const char *const SPESSORE_NOME[N_SPESSORI] = { "1", "3", "5", "9" };

/* The 28 colours of Paint Brush, two rows. */
static const unsigned int TAVOLOZZA[28] = {
    0x000000, 0x808080, 0x800000, 0x808000, 0x008000, 0x008080, 0x000080,
    0x800080, 0x808040, 0x004040, 0x0080FF, 0x004080, 0x8000FF, 0x804000,
    0xFFFFFF, 0xC0C0C0, 0xFF0000, 0xFFFF00, 0x00FF00, 0x00FFFF, 0x0000FF,
    0xFF00FF, 0xFFFF80, 0x00FF80, 0x80FFFF, 0x8080FF, 0xFF0080, 0xFF8040
};

/* --- The state ----------------------------------------------------------- */
static ExFinestra g_f, g_menu, g_barra_v, g_barra_o, g_stato, g_scambia;
static ExFinestra g_r_str[N_STRUMENTI], g_r_sp[N_SPESSORI];
static int        g_w = FIN_W, g_h = FIN_H;        /* the client area */

static unsigned int *g_img;          /* the picture, g_iw x g_ih */
static unsigned int *g_prima;        /* the picture as it was when the operation began */
static unsigned int  g_cap;          /* pixels both buffers can hold */
static int           g_iw, g_ih;

static unsigned int *g_vista;        /* what the canvas shows, one screen at most */
static unsigned int  g_vista_cap;

static int  g_zoom = 1, g_ox = 0, g_oy = 0;        /* the view: zoom, first pixel shown */
static int  g_str = S_PENNELLO, g_sp = 1, g_str_prima = S_PENNELLO;
static unsigned int g_col = 0x000000, g_sfondo = 0xFFFFFF;

static char g_file[256];             /* "" for a picture never saved */
static char g_cartella[200];         /* where the dialogs start */
static int  g_mod = 0;

/* The operation in progress: the mouse went down in the canvas. */
static int  g_op = 0, g_x0, g_y0, g_xu, g_yu;
static int  g_sx0, g_sy0, g_sx1, g_sy1;            /* the rectangle touched (inclusive) */
static int  g_px0, g_py0, g_px1, g_py1;            /* the rubber band drawn last */

/* =============================================================================
 * THE CANVAS ON SCREEN
 * ============================================================================= */
static int tela_w(void) { int v = g_w - TELA_X - MARGINE - BARRA; return v > 8 ? v : 8; }
static int tela_h(void) { int v = g_h - TELA_Y - BARRA - PAL_H - STATO_H - 4; return v > 8 ? v : 8; }

/* How many picture pixels fit in the canvas, across and down. */
static int vis_w(void) { return tela_w() / g_zoom; }
static int vis_h(void) { return tela_h() / g_zoom; }

static void vista_limita(void)
{
    int mx = g_iw - vis_w(), my = g_ih - vis_h();
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (g_ox > mx) g_ox = mx;
    if (g_oy > my) g_oy = my;
    if (g_ox < 0) g_ox = 0;
    if (g_oy < 0) g_oy = 0;
}

static void barre_aggiorna(void)
{
    int mx = g_iw - vis_w(), my = g_ih - vis_h();
    ex_scorri_limiti(g_barra_o, (unsigned int)(mx > 0 ? mx : 0), (unsigned int)vis_w());
    ex_scorri_vai(g_barra_o, (unsigned int)g_ox);
    ex_scorri_limiti(g_barra_v, (unsigned int)(my > 0 ? my : 0), (unsigned int)vis_h());
    ex_scorri_vai(g_barra_v, (unsigned int)g_oy);
}

/* Draws the part of the canvas that shows picture pixels x0..x1, y0..y1
 * (inclusive, picture coordinates). With x0 > x1 it draws the whole canvas,
 * the grey around the picture included.
 *
 * ! AT ZOOM 1 THE PICTURE GOES OUT AS IT IS, with ex_pixmap's `passo`: no copy.
 * Only a zoom builds the enlarged pixels in g_vista first. */
static void tela_disegna(int x0, int y0, int x1, int y1)
{
    int tw = tela_w(), th = tela_h(), z = g_zoom;
    int vx0, vy0, vx1, vy1, x, y;

    if (!g_img) return;
    if (x0 > x1) {
        int pw = (g_iw - g_ox) * z, ph = (g_ih - g_oy) * z;
        if (pw < tw) ex_riempi(g_f, TELA_X + pw, TELA_Y, tw - pw, th, EX_GRIGIO_SC);
        if (ph < th) ex_riempi(g_f, TELA_X, TELA_Y + ph, pw < tw ? pw : tw, th - ph, EX_GRIGIO_SC);
        x0 = 0; y0 = 0; x1 = g_iw - 1; y1 = g_ih - 1;
    }
    /* Picture rectangle -> visible part -> canvas pixels. */
    if (x0 < g_ox) x0 = g_ox;
    if (y0 < g_oy) y0 = g_oy;
    if (x1 > g_ox + vis_w()) x1 = g_ox + vis_w();
    if (y1 > g_oy + vis_h()) y1 = g_oy + vis_h();
    if (x1 >= g_iw) x1 = g_iw - 1;
    if (y1 >= g_ih) y1 = g_ih - 1;
    if (x0 > x1 || y0 > y1) return;
    vx0 = (x0 - g_ox) * z; vy0 = (y0 - g_oy) * z;
    vx1 = (x1 - g_ox + 1) * z; vy1 = (y1 - g_oy + 1) * z;
    if (vx1 > tw) vx1 = tw;
    if (vy1 > th) vy1 = th;
    if (vx1 <= vx0 || vy1 <= vy0) return;

    if (z == 1) {
        ex_pixmap(g_f, TELA_X + vx0, TELA_Y + vy0, vx1 - vx0, vy1 - vy0,
                  g_img + (unsigned int)y0 * (unsigned int)g_iw + (unsigned int)x0, (unsigned int)g_iw);
        return;
    }
    if ((unsigned int)((vx1 - vx0) * (vy1 - vy0)) > g_vista_cap) return;
    for (y = vy0; y < vy1; y++) {
        const unsigned int *s = g_img + (unsigned int)(g_oy + y / z) * (unsigned int)g_iw + (unsigned int)g_ox;
        unsigned int *d = g_vista + (unsigned int)(y - vy0) * (unsigned int)(vx1 - vx0);
        for (x = vx0; x < vx1; x++) d[x - vx0] = s[x / z];
    }
    ex_pixmap(g_f, TELA_X + vx0, TELA_Y + vy0, vx1 - vx0, vy1 - vy0, g_vista, (unsigned int)(vx1 - vx0));
}

/* --- The palette and the two colours -------------------------------------- */
static int pal_y(void) { return g_h - STATO_H - PAL_H + 2; }
#define PAL_X   (TELA_X + 4)

static void colori_disegna(void)
{
    int y = pal_y(), i;

    /* The two colours: the first one on top, the background under it. */
    ex_incavo(g_f, MARGINE, y, 44, 36);
    ex_riempi(g_f, MARGINE + 16, y + 14, 24, 18, g_sfondo);
    ex_riquadro_disegna(g_f, MARGINE + 16, y + 14, 24, 18, EX_NERO);
    ex_riempi(g_f, MARGINE + 4, y + 4, 24, 18, g_col);
    ex_riquadro_disegna(g_f, MARGINE + 4, y + 4, 24, 18, EX_NERO);

    for (i = 0; i < 28; i++) {
        int cx = PAL_X + (i % 14) * (CELLA + 2), cy = y + (i / 14) * (CELLA + 2);
        ex_riempi(g_f, cx, cy, CELLA, CELLA, TAVOLOZZA[i]);
        ex_riquadro_disegna(g_f, cx, cy, CELLA, CELLA, EX_GRIGIO_SC);
    }
}

static void stato_aggiorna(void)
{
    char t[160];
    snprintf(t, sizeof(t), "%d x %d   %s %d px   zoom %dx   #%06X%s",
             g_iw, g_ih, STRUMENTO_NOME[g_str], SPESSORE[g_sp], g_zoom, g_col & 0xFFFFFF,
             g_mod ? "   (modificata)" : "");
    ex_testo_metti(g_stato, t);
}

static void titolo_aggiorna(void)
{
    char t[300];
    const char *n = strrchr(g_file, '/');
    n = n ? n + 1 : g_file;
    snprintf(t, sizeof(t), "Pennello - %s%s", g_file[0] ? n : "senza nome", g_mod ? " *" : "");
    ex_titolo(g_f, t);
}

static void ridisegna(void)
{
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    tela_disegna(1, 0, 0, 0);
    colori_disegna();
    ex_aggiorna(g_f);
}

/* =============================================================================
 * THE BUFFERS
 * ============================================================================= */

/* The picture becomes w x h. `copia`: keep what fits of the old one; the
 * rest is the background colour. 0 if there is no memory.
 *
 * ! THE BUFFERS GROW, THEY DO NOT SHRINK: a smaller picture reuses them. Only
 * a bigger one asks for new memory, and that is the only moment memory is
 * lost for good. */
static int dimensioni(int w, int h, int copia)
{
    unsigned int n = (unsigned int)w * (unsigned int)h, i;
    int x, y;

    if (w < 1 || h < 1 || n > PIXEL_MAX) return 0;
    if (n > g_cap) {
        unsigned int *a = (unsigned int *)malloc(n * 4), *b = (unsigned int *)malloc(n * 4);
        if (!a || !b) { if (a) free(a); if (b) free(b); return 0; }
        if (copia && g_img)
            for (y = 0; y < g_ih; y++)
                for (x = 0; x < g_iw; x++) b[y * g_iw + x] = g_img[y * g_iw + x];
        if (g_img) free(g_img);
        if (g_prima) free(g_prima);
        g_img = a;
        g_prima = b;
        g_cap = n;
    } else if (copia && g_img) {
        for (i = 0; i < (unsigned int)(g_iw * g_ih); i++) g_prima[i] = g_img[i];
    }
    /* g_prima now holds the old picture, g_iw x g_ih. */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            g_img[y * w + x] = (copia && x < g_iw && y < g_ih)
                               ? g_prima[y * g_iw + x] : (0xFF000000u | g_sfondo);
    g_iw = w;
    g_ih = h;
    return 1;
}

/* =============================================================================
 * UNDO AND REDO
 *
 * A record is {x, y, w, h} followed by w*h pixels, in g_pool. g_rec[] says
 * where each record starts; [0, g_fatti) can be undone, [g_fatti, g_nrec) can
 * be done again. A new operation throws the redoable ones away.
 * ============================================================================= */
static unsigned int *g_pool;
static unsigned int  g_pool_px = 0;     /* its size in pixels, decided at start */
static unsigned int  g_rec[UNDO_MAX], g_rec_n[UNDO_MAX];
static int           g_nrec = 0, g_fatti = 0;

static void storia_svuota(void) { g_nrec = g_fatti = 0; }

static unsigned int storia_fine(void)
{
    return g_nrec ? g_rec[g_nrec - 1] + g_rec_n[g_nrec - 1] : 0;
}

static void storia_via_la_piu_vecchia(void)
{
    unsigned int k = g_rec_n[0], i, fine = storia_fine();
    int j;

    for (i = 0; i + k < fine; i++) g_pool[i] = g_pool[i + k];
    for (j = 1; j < g_nrec; j++) { g_rec[j - 1] = g_rec[j] - k; g_rec_n[j - 1] = g_rec_n[j]; }
    g_nrec--;
    if (g_fatti) g_fatti--;
}

/* The operation just ended touched x0..x1, y0..y1 of g_prima's picture. */
static void storia_aggiungi(int x0, int y0, int x1, int y1)
{
    unsigned int w, h, n, *r;
    int x, y;

    if (!g_pool) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= g_iw) x1 = g_iw - 1;
    if (y1 >= g_ih) y1 = g_ih - 1;
    if (x0 > x1 || y0 > y1) return;
    w = (unsigned int)(x1 - x0 + 1);
    h = (unsigned int)(y1 - y0 + 1);
    n = 4 + w * h;
    g_nrec = g_fatti;                               /* no more redo */
    if (n > g_pool_px) { storia_svuota(); return; } /* too big to remember */
    while (g_nrec && (g_nrec >= UNDO_MAX || storia_fine() + n > g_pool_px))
        storia_via_la_piu_vecchia();
    g_rec[g_nrec] = storia_fine();
    g_rec_n[g_nrec] = n;
    r = g_pool + g_rec[g_nrec];
    r[0] = (unsigned int)x0; r[1] = (unsigned int)y0; r[2] = w; r[3] = h;
    for (y = 0; y < (int)h; y++)
        for (x = 0; x < (int)w; x++)
            r[4 + y * w + x] = g_prima[(y0 + y) * g_iw + x0 + x];
    g_nrec++;
    g_fatti = g_nrec;
}

/* Swaps record k with the picture: after an undo it holds what the redo puts back. */
static void storia_scambia(int k)
{
    unsigned int *r = g_pool + g_rec[k], x0 = r[0], y0 = r[1], w = r[2], h = r[3], x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned int *p = &g_img[(y0 + y) * (unsigned int)g_iw + x0 + x], t = *p;
            *p = r[4 + y * w + x];
            r[4 + y * w + x] = t;
        }
    tela_disegna((int)x0, (int)y0, (int)(x0 + w - 1), (int)(y0 + h - 1));
}

static void annulla(void)
{
    if (g_fatti == 0) { ex_testo_metti(g_stato, "Niente da annullare."); return; }
    storia_scambia(--g_fatti);
    g_mod = 1;
    ex_aggiorna(g_f);
    titolo_aggiorna();
}

static void ripeti(void)
{
    if (g_fatti >= g_nrec) { ex_testo_metti(g_stato, "Niente da ripetere."); return; }
    storia_scambia(g_fatti++);
    g_mod = 1;
    ex_aggiorna(g_f);
    titolo_aggiorna();
}

/* =============================================================================
 * DRAWING INTO THE PICTURE
 * ============================================================================= */
static void tocca(int x, int y)
{
    if (x < g_sx0) g_sx0 = x;
    if (y < g_sy0) g_sy0 = y;
    if (x > g_sx1) g_sx1 = x;
    if (y > g_sy1) g_sy1 = y;
}

static void metti(int x, int y, unsigned int c)
{
    if (x < 0 || y < 0 || x >= g_iw || y >= g_ih) return;
    g_img[y * g_iw + x] = 0xFF000000u | c;
    tocca(x, y);
}

/* A round point `s` pixels wide. */
static void punto(int x, int y, int s, unsigned int c)
{
    int r = s / 2, dx, dy;

    if (s <= 1) { metti(x, y, c); return; }
    for (dy = -r; dy <= r; dy++)
        for (dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r + r) metti(x + dx, y + dy, c);
}

static void linea(int x0, int y0, int x1, int y1, int s, unsigned int c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy, e2;

    /* ! IL DOPPIO DELL'ERRORE SI CALCOLA UNA VOLTA, PRIMA DEI DUE PASSI (28
     * settembre 2026). Qui c'era `2 * e` ricalcolato dopo aver gia' cambiato
     * `e` nel primo: su certi segmenti — da (166,154) a (164,155), per
     * dirne uno — la y scavalcava l'arrivo, il punto finale non si toccava
     * piu' e il ciclo non finiva: Pennello girava a vuoto per sempre. Col
     * mouse veloce i segmenti sono lunghi e storti, e ci si cadeva: era il
     * cuore del «blocca tutto». Provato sull'host su ogni segmento con gli
     * estremi fra -40 e 40 (tools/prove/lineaprova.c). */
    for (;;) {
        punto(x0, y0, s, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

static void ordina(int *a, int *b) { if (*a > *b) { int t = *a; *a = *b; *b = t; } }

static void rettangolo(int x0, int y0, int x1, int y1, int pieno, int s, unsigned int c)
{
    int x, y;

    ordina(&x0, &x1);
    ordina(&y0, &y1);
    if (pieno) {
        for (y = y0; y <= y1; y++)
            for (x = x0; x <= x1; x++) metti(x, y, c);
        return;
    }
    linea(x0, y0, x1, y0, s, c);
    linea(x1, y0, x1, y1, s, c);
    linea(x1, y1, x0, y1, s, c);
    linea(x0, y1, x0, y0, s, c);
}

/* The ellipse inside the box x0..x1, y0..y1.
 *
 * ! IN DOUBLED COORDINATES, SO THE CENTRE IS AN INTEGER: a box 10 pixels wide
 * has its centre between two pixels, and rounding it would make every
 * even-sized ellipse lopsided by one. A point (x, y) is inside when
 * (2x - cx)^2 * B^2 + (2y - cy)^2 * A^2 <= A^2 * B^2, with A and B the doubled
 * semi-axes — in 64 bits, because 8192 doubled and squared twice does not fit
 * in 32. */
static int ellisse_dentro(long long ddx, long long ddy, long long a2, long long b2)
{
    return ddx * ddx * b2 + ddy * ddy * a2 <= a2 * b2;
}

/* The half-width of row y, as the leftmost x inside (or x1 + 1 if none). */
static int ellisse_sinistra(int x0, int x1, int y, int cx2, int cy2, long long a2, long long b2)
{
    int lo = x0, hi = (cx2 >> 1) + 1;           /* the answer is in [lo, hi] */
    long long ddy = 2 * (long long)y - cy2;

    if (hi > x1 + 1) hi = x1 + 1;
    if (!ellisse_dentro(2 * (long long)((cx2) >> 1) - cx2, ddy, a2, b2) &&
        !ellisse_dentro(2 * (long long)((cx2 + 1) >> 1) - cx2, ddy, a2, b2)) return x1 + 1;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (ellisse_dentro(2 * (long long)m - cx2, ddy, a2, b2)) hi = m; else lo = m + 1;
    }
    return lo;
}

static void ellisse(int x0, int y0, int x1, int y1, int pieno, int s, unsigned int c)
{
    int cx2, cy2, y, x, sx, dx, lx = 0, rx = 0, ly = 0, prima = 1;
    long long a2, b2;

    ordina(&x0, &x1);
    ordina(&y0, &y1);
    if (x1 - x0 < 2 || y1 - y0 < 2) { rettangolo(x0, y0, x1, y1, 1, 1, c); return; }
    cx2 = x0 + x1;
    cy2 = y0 + y1;
    a2 = (long long)(x1 - x0) * (x1 - x0);
    b2 = (long long)(y1 - y0) * (y1 - y0);
    for (y = y0; y <= y1; y++) {
        sx = ellisse_sinistra(x0, x1, y, cx2, cy2, a2, b2);
        if (sx > x1) continue;
        dx = cx2 - sx;                          /* the mirror of sx */
        if (pieno) {
            for (x = sx; x <= dx; x++) metti(x, y, c);
            continue;
        }
        /* ! THE OUTLINE JOINS ROW TO ROW WITH LINES: near the top the ends of
         * two rows are far apart, and dots alone would leave holes. */
        if (prima) { linea(sx, y, dx, y, s, c); prima = 0; }
        else { linea(lx, y - 1, sx, y, s, c); linea(rx, y - 1, dx, y, s, c); }
        lx = sx; rx = dx; ly = y;
    }
    if (!prima) linea(lx, ly, rx, ly, s, c);     /* the flat bottom */
}

/* The scanline fill, 4-connected. 1 if it finished, 0 if the seeds ran out
 * (then what was reached is filled, and the status line says so). */
static int riempi(int x, int y, unsigned int c)
{
    static int px[PILA_MAX], py[PILA_MAX];
    int n = 0, completo = 1;
    unsigned int vecchio, nuovo = 0xFF000000u | c;

    if (x < 0 || y < 0 || x >= g_iw || y >= g_ih) return 1;
    vecchio = g_img[y * g_iw + x];
    if (vecchio == nuovo) return 1;
    px[n] = x; py[n] = y; n++;
    while (n) {
        int l, r, xx, su, giu;
        n--;
        x = px[n]; y = py[n];
        if (g_img[y * g_iw + x] != vecchio) continue;
        for (l = x; l > 0 && g_img[y * g_iw + l - 1] == vecchio; l--) ;
        for (r = x; r < g_iw - 1 && g_img[y * g_iw + r + 1] == vecchio; r++) ;
        su = giu = 0;
        for (xx = l; xx <= r; xx++) {
            g_img[y * g_iw + xx] = nuovo;
            /* One seed per run above and below, not one per pixel. */
            if (y > 0) {
                if (g_img[(y - 1) * g_iw + xx] == vecchio) {
                    if (!su) { if (n < PILA_MAX) { px[n] = xx; py[n] = y - 1; n++; } else completo = 0; su = 1; }
                } else su = 0;
            }
            if (y < g_ih - 1) {
                if (g_img[(y + 1) * g_iw + xx] == vecchio) {
                    if (!giu) { if (n < PILA_MAX) { px[n] = xx; py[n] = y + 1; n++; } else completo = 0; giu = 1; }
                } else giu = 0;
            }
        }
        tocca(l, y);
        tocca(r, y);
    }
    return completo;
}

static void spruzza(int x, int y, int s, unsigned int c)
{
    int r = s < 3 ? 6 : s * 2, i;

    for (i = 0; i < 12 + s * 2; i++) {
        int dx = rand() % (2 * r + 1) - r, dy = rand() % (2 * r + 1) - r;
        if (dx * dx + dy * dy <= r * r) metti(x + dx, y + dy, c);
    }
}

/* =============================================================================
 * THE OPERATIONS OF THE MOUSE
 * ============================================================================= */
static int nella_tela(int mx, int my)
{
    return mx >= TELA_X && my >= TELA_Y && mx < TELA_X + tela_w() && my < TELA_Y + tela_h();
}

/* Canvas pixel -> picture pixel. Outside the picture too: a line may end
 * off the edge, and what is off is simply not drawn. */
static void in_immagine(int mx, int my, int *x, int *y)
{
    /* The client coordinates come as 16 unsigned bits: a drag above or left of
     * the window arrives as a large number, and is made negative again. */
    if (mx > 32767) mx -= 65536;
    if (my > 32767) my -= 65536;
    *x = g_ox + (mx - TELA_X) / g_zoom - (mx < TELA_X ? 1 : 0);
    *y = g_oy + (my - TELA_Y) / g_zoom - (my < TELA_Y ? 1 : 0);
}

static int e_forma(int s) { return s == S_LINEA || s == S_RETT || s == S_RETT_PIENO || s == S_ELLISSE || s == S_ELLISSE_PIENA; }

static void forma(int x0, int y0, int x1, int y1)
{
    int s = SPESSORE[g_sp];
    switch (g_str) {
    case S_LINEA:         linea(x0, y0, x1, y1, s, g_col); break;
    case S_RETT:          rettangolo(x0, y0, x1, y1, 0, s, g_col); break;
    case S_RETT_PIENO:    rettangolo(x0, y0, x1, y1, 1, s, g_col); break;
    case S_ELLISSE:       ellisse(x0, y0, x1, y1, 0, s, g_col); break;
    case S_ELLISSE_PIENA: ellisse(x0, y0, x1, y1, 1, s, g_col); break;
    }
}

static void tratto(int x0, int y0, int x1, int y1)
{
    switch (g_str) {
    case S_MATITA:   linea(x0, y0, x1, y1, 1, g_col); break;
    case S_PENNELLO: linea(x0, y0, x1, y1, SPESSORE[g_sp], g_col); break;
    /* ! THE ERASER IS WIDER THAN THE BRUSH OF THE SAME WIDTH: erasing a
     * picture one pixel at a time is not a thing anybody wants. */
    case S_GOMMA:    linea(x0, y0, x1, y1, SPESSORE[g_sp] * 2 + 2, g_sfondo); break;
    case S_SPRUZZO:  spruzza(x1, y1, SPESSORE[g_sp], g_col); break;
    }
}

static void strumento_scegli(int s);

/* =============================================================================
 * IL TESTO (28 settembre 2026, @PAINT: «il testo dentro l'immagine»)
 *
 * Un clic dove comincia la scritta, una riga da scrivere, e il testo entra
 * nei PIXEL dell'immagine col colore scelto: ex_scrivi_in, nel toolkit, fa
 * con un bitmap quel che ex_scrivi_con fa con la finestra. La grandezza la
 * danno i quattro spessori (12, 16, 24, 36 pixel). E' un'operazione come le
 * altre: si annulla e si ripete col rettangolo che ha toccato.
 * ! Il testo scritto l'ultima volta si ripropone: chi mette la stessa
 * etichetta in tre punti la batte una volta.
 * ============================================================================= */
static const int CORPO_TESTO[N_SPESSORI] = { 12, 16, 24, 36 };
static char g_testo[160] = "";

static void ridisegna(void);

static void testo_metti(int x, int y)
{
    ExFont       f;
    int          w, h;
    unsigned int i;

    if (x < 0 || y < 0 || x >= g_iw || y >= g_ih) return;
    if (!ex_dlg_riga("Testo", "Il testo da scrivere nell'immagine:", g_testo,
                     sizeof(g_testo)) || !g_testo[0]) {
        ridisegna();
        return;
    }
    f = ex_font_trova(EX_FAM_SANS, CORPO_TESTO[g_sp], 0, 0);
    for (i = 0; i < (unsigned int)(g_iw * g_ih); i++) g_prima[i] = g_img[i];
    ex_scrivi_in(g_img, g_iw, g_ih, f, x, y, g_testo, g_col);

    /* il rettangolo toccato, con un margine: un glifo puo' sporgere di un
     * pixel dalla sua casella */
    w = ex_larghezza_testo(f, g_testo);
    h = ex_font_altezza(f);
    g_sx0 = x > 2 ? x - 2 : 0;
    g_sy0 = y > 2 ? y - 2 : 0;
    g_sx1 = x + w + 2 < g_iw ? x + w + 2 : g_iw - 1;
    g_sy1 = y + h + 2 < g_ih ? y + h + 2 : g_ih - 1;
    storia_aggiungi(g_sx0, g_sy0, g_sx1, g_sy1);
    g_mod = 1;
    ridisegna();
    stato_aggiorna();
    titolo_aggiorna();
}

static void op_inizia(int mx, int my)
{
    int x, y;
    unsigned int i;

    in_immagine(mx, my, &x, &y);
    if (g_str == S_CONTAGOCCE) {
        if (x >= 0 && y >= 0 && x < g_iw && y < g_ih) g_col = g_img[y * g_iw + x] & 0xFFFFFF;
        colori_disegna();
        /* Paint Brush goes back to the tool that was in use: the picker is
         * a glance, not a way of drawing. */
        strumento_scegli(g_str_prima);
        ex_aggiorna(g_f);
        return;
    }
    if (g_str == S_TESTO) { testo_metti(x, y); return; }

    for (i = 0; i < (unsigned int)(g_iw * g_ih); i++) g_prima[i] = g_img[i];
    g_sx0 = g_sy0 = 0x7FFFFFFF;
    g_sx1 = g_sy1 = -1;
    g_x0 = g_xu = x;
    g_y0 = g_yu = y;
    g_px0 = g_px1 = x; g_py0 = g_py1 = y;
    g_op = 1;

    if (g_str == S_RIEMPI) {
        int ok = riempi(x, y, g_col);
        g_op = 0;
        if (g_sx1 >= 0) {
            storia_aggiungi(g_sx0, g_sy0, g_sx1, g_sy1);
            g_mod = 1;
            tela_disegna(g_sx0, g_sy0, g_sx1, g_sy1);
        }
        stato_aggiorna();
        if (!ok) ex_testo_metti(g_stato, "Riempimento incompleto: l'area e' troppo frastagliata. Riprova dal punto rimasto.");
        titolo_aggiorna();
        ex_aggiorna(g_f);
        return;
    }
    if (!e_forma(g_str)) tratto(x, y, x, y);
    else forma(x, y, x, y);
    if (g_sx1 >= 0) tela_disegna(g_sx0, g_sy0, g_sx1, g_sy1);
    ex_aggiorna(g_f);
}

/* =============================================================================
 * LO SCHERMO SI AGGIORNA UNA VOLTA PER GRUPPO DI MOVIMENTI (28 settembre 2026)
 *
 * ! «CON IL MOUSE VELOCE SI BLOCCA TUTTO», detto da chi lo usa, e misurato da
 * tools/prova_pennello_veloce.sh: dopo trecento movimenti in tre secondi il
 * puntatore restava fermo per piu' di sei. Ogni movimento mandava al server
 * un rettangolo di pixel — con l'ellisse, l'intera anteprima: centinaia di KB —
 * piu' un ex_aggiorna. I movimenti arrivavano piu' in fretta di quanto quei
 * disegni si smaltissero, e il server, occupato a riceverli, non muoveva
 * nemmeno il puntatore.
 *
 * Adesso op_muovi tocca solo l'IMMAGINE e segna il rettangolo sporco; il
 * gestore del movimento prende anche quelli gia' in coda, e lo schermo si
 * aggiorna una volta sola alla fine (schermo_svuota). Il tratto a mano libera
 * passa per tutti i punti — la forma non perde niente —, la forma elastica
 * usa l'ultimo, che e' l'unico che si vede.
 * ============================================================================= */
static int g_dx0 = 0x7FFFFFFF, g_dy0 = 0x7FFFFFFF, g_dx1 = -1, g_dy1 = -1;

static void sporca(int x0, int y0, int x1, int y1)
{
    if (x0 < g_dx0) g_dx0 = x0;
    if (y0 < g_dy0) g_dy0 = y0;
    if (x1 > g_dx1) g_dx1 = x1;
    if (y1 > g_dy1) g_dy1 = y1;
}

/* ! L'ANNUNCIO AL SERVER SI DA' AL PIU' OGNI AGG_MS. Raccogliere i movimenti
 * in coda non bastava, ed e' la seconda lezione della stessa prova: Pennello
 * e' veloce, smaltisce ogni movimento prima che arrivi il prossimo, quindi la
 * coda e' quasi sempre vuota — e ogni ex_aggiorna costa al SERVER una
 * ricomposizione. Cento movimenti al secondo erano cento ricomposizioni, e il
 * server restava indietro di decine di secondi. I pixel vanno nella memoria
 * condivisa subito (ex_pixmap non manda messaggi); l'annuncio aspetta. Quello
 * rimasto indietro lo manda la sveglia, e il pulsante lasciato lo manda
 * sempre. */
#define AGG_MS 50u
static unsigned int g_agg_ultimo = 0;
static int          g_agg_attesa = 0;

static void annuncia(int forza)
{
    unsigned int ora = uptime_ms();

    if (!forza && ora - g_agg_ultimo < AGG_MS) {
        if (!g_agg_attesa) ex_sveglia(g_f, AGG_MS);
        g_agg_attesa = 1;
        return;
    }
    ex_aggiorna(g_f);
    g_agg_ultimo = ora;
    if (g_agg_attesa) ex_sveglia(g_f, 0);
    g_agg_attesa = 0;
}

static void schermo_svuota_come(int forza)
{
    if (g_dx1 >= 0) {
        tela_disegna(g_dx0 < 0 ? 0 : g_dx0, g_dy0 < 0 ? 0 : g_dy0, g_dx1, g_dy1);
        g_dx0 = g_dy0 = 0x7FFFFFFF;
        g_dx1 = g_dy1 = -1;
        annuncia(forza);
    } else if (forza && g_agg_attesa) {
        annuncia(1);
    }
}

static void schermo_svuota(void) { schermo_svuota_come(0); }

static void op_muovi(int mx, int my)
{
    int x, y, bx0, by0, bx1, by1, r;

    if (!g_op) return;
    in_immagine(mx, my, &x, &y);
    if (!e_forma(g_str)) {
        int a0 = g_sx0, a1 = g_sx1, b0 = g_sy0, b1 = g_sy1;
        /* Only the new piece is marked dirty, not all the stroke so far. */
        g_sx0 = g_sy0 = 0x7FFFFFFF; g_sx1 = g_sy1 = -1;
        tratto(g_xu, g_yu, x, y);
        if (g_sx1 >= 0) sporca(g_sx0, g_sy0, g_sx1, g_sy1);
        if (a1 >= 0) {
            if (a0 < g_sx0) g_sx0 = a0;
            if (b0 < g_sy0) g_sy0 = b0;
            if (a1 > g_sx1) g_sx1 = a1;
            if (b1 > g_sy1) g_sy1 = b1;
        }
        g_xu = x; g_yu = y;
        return;
    }
    /* ! THE RUBBER BAND: the picture under the old shape comes back from
     * g_prima, then the new shape is drawn — and both rectangles are dirty,
     * or the old shape would stay on screen as a ghost. */
    r = SPESSORE[g_sp] / 2 + 1;
    bx0 = g_px0 - r; by0 = g_py0 - r; bx1 = g_px1 + r; by1 = g_py1 + r;
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 >= g_iw) bx1 = g_iw - 1;
    if (by1 >= g_ih) by1 = g_ih - 1;
    {
        int yy, xx;
        for (yy = by0; yy <= by1; yy++)
            for (xx = bx0; xx <= bx1; xx++) g_img[yy * g_iw + xx] = g_prima[yy * g_iw + xx];
    }
    forma(g_x0, g_y0, x, y);
    g_xu = x; g_yu = y;
    g_px0 = g_x0 < x ? g_x0 : x; g_px1 = g_x0 < x ? x : g_x0;
    g_py0 = g_y0 < y ? g_y0 : y; g_py1 = g_y0 < y ? y : g_y0;
    sporca(bx0, by0, bx1, by1);
    sporca(g_px0 - r, g_py0 - r, g_px1 + r, g_py1 + r);
}

static void op_finisci(int mx, int my)
{
    if (!g_op) return;
    op_muovi(mx, my);
    schermo_svuota_come(1);     /* il pulsante lasciato: l'annuncio parte sempre */
    g_op = 0;
    if (g_sx1 >= 0) {
        storia_aggiungi(g_sx0, g_sy0, g_sx1, g_sy1);
        g_mod = 1;
    }
    stato_aggiorna();
    titolo_aggiorna();
}

/* =============================================================================
 * THE PICTURE AS A WHOLE
 * ============================================================================= */

/* Everything changes: the undo record is the whole picture. */
static void tutta_prima(void)
{
    unsigned int i;
    for (i = 0; i < (unsigned int)(g_iw * g_ih); i++) g_prima[i] = g_img[i];
}

static void tutta_dopo(void)
{
    storia_aggiungi(0, 0, g_iw - 1, g_ih - 1);
    g_mod = 1;
    tela_disegna(1, 0, 0, 0);
    ex_aggiorna(g_f);
    stato_aggiorna();
    titolo_aggiorna();
}

static void specchio(int verticale)
{
    int x, y;

    tutta_prima();
    for (y = 0; y < g_ih; y++)
        for (x = 0; x < g_iw; x++)
            g_img[y * g_iw + x] = verticale ? g_prima[(g_ih - 1 - y) * g_iw + x]
                                            : g_prima[y * g_iw + g_iw - 1 - x];
    tutta_dopo();
}

static void inverti(void)
{
    unsigned int i;

    tutta_prima();
    for (i = 0; i < (unsigned int)(g_iw * g_ih); i++) g_img[i] ^= 0x00FFFFFFu;
    tutta_dopo();
}

static void cancella_tutto(void)
{
    unsigned int i;

    tutta_prima();
    for (i = 0; i < (unsigned int)(g_iw * g_ih); i++) g_img[i] = 0xFF000000u | g_sfondo;
    tutta_dopo();
}

/* ! ROTATING CHANGES THE SIZE, AND AN UNDO RECORD IS A RECTANGLE OF THE SAME
 * PICTURE: it cannot hold «it was 480 wide». So a rotation empties the undo
 * history, and says so. Three more rotations give the picture back. */
static void ruota(void)
{
    int x, y, w = g_ih, h = g_iw;

    tutta_prima();
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            g_img[y * w + x] = g_prima[(g_ih - 1 - x) * g_iw + y];
    g_iw = w;
    g_ih = h;
    storia_svuota();
    g_mod = 1;
    vista_limita();
    barre_aggiorna();
    ridisegna();
    stato_aggiorna();
    titolo_aggiorna();
    ex_testo_metti(g_stato, "Ruotata di 90 gradi. La rotazione non si annulla: si ruota altre tre volte.");
}

/* "640x480", "640 480", "640,480" */
static int leggi_misura(const char *s, int *w, int *h)
{
    char *e;
    long a = strtol(s, &e, 10), b;
    while (*e == ' ' || *e == 'x' || *e == 'X' || *e == ',' || *e == '*') e++;
    b = strtol(e, 0, 10);
    if (a < 1 || b < 1 || a > 8192 || b > 8192) return 0;
    *w = (int)a;
    *h = (int)b;
    return 1;
}

static void chiedi_dimensioni(void)
{
    char v[40];
    int w, h;

    snprintf(v, sizeof(v), "%dx%d", g_iw, g_ih);
    if (!ex_dlg_chiedi("Dimensioni", "Larghezza x altezza, in pixel (il nuovo spazio prende il colore di sfondo):",
                       "Cambia", v, sizeof(v))) return;
    if (!leggi_misura(v, &w, &h) || (unsigned int)w * (unsigned int)h > PIXEL_MAX) {
        ex_dlg_avviso("Dimensioni", "Misura non valida: al massimo 2048 x 2048 pixel in tutto (4 milioni di pixel).");
        return;
    }
    if (!dimensioni(w, h, 1)) { ex_dlg_avviso("Dimensioni", "Non c'e' memoria per un'immagine cosi' grande."); return; }
    storia_svuota();
    g_mod = 1;
    vista_limita();
    barre_aggiorna();
    stato_aggiorna();
    titolo_aggiorna();
}

/* =============================================================================
 * THE PROFILE
 * ============================================================================= */
static const char *config_file(int crea)
{
    static char f[200];
    const char *casa = getenv("HOME");
    char        d[200];

    if (!casa || !casa[0] || strcmp(casa, "/") == 0)
        casa = (getuid() == 0) ? "/root" : "";
    if (crea) {
        if (casa[0]) mkdir(casa, 0700);
        snprintf(d, sizeof(d), "%s/.exwin", casa);        mkdir(d, 0755);
        snprintf(d, sizeof(d), "%s/.exwin/config", casa); mkdir(d, 0755);
    }
    snprintf(f, sizeof(f), "%s/.exwin/config/pennello.cfg", casa);
    return f;
}

static void opzioni_salva(void)
{
    char t[512];
    int  fd = open(config_file(1), O_WRONLY | O_CREAT | O_TRUNC, 0644), n;

    if (fd < 0) return;         /* from the CD there is no profile to write */
    n = snprintf(t, sizeof(t),
                 "# Pennello: le scelte. Le riscrive il programma.\n"
                 "strumento = %s\nspessore  = %d\ncolore    = %06X\nsfondo    = %06X\ncartella  = %s\n",
                 STRUMENTO_CFG[g_str], SPESSORE[g_sp], g_col & 0xFFFFFF, g_sfondo & 0xFFFFFF, g_cartella);
    write(fd, t, (unsigned int)n);
    close(fd);
}

/* The value after "chiave =", up to the end of the line, in out. */
static int valore(const char *buf, const char *chiave, char *out, unsigned int max)
{
    const char *p = buf;
    unsigned int l = (unsigned int)strlen(chiave), i = 0;

    while ((p = strstr(p, chiave)) != 0) {
        if ((p == buf || p[-1] == '\n') && (p[l] == ' ' || p[l] == '=')) {
            p = strchr(p, '=');
            if (!p) return 0;
            p++;
            while (*p == ' ' || *p == '\t') p++;
            while (*p && *p != '\n' && *p != '\r' && i + 1 < max) out[i++] = *p++;
            out[i] = 0;
            return 1;
        }
        p += l;
    }
    return 0;
}

static void opzioni_leggi(void)
{
    char buf[1024], v[200];
    int  fd = open(config_file(0), O_RDONLY, 0), n, i;

    if (fd < 0) return;
    n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    if (valore(buf, "strumento", v, sizeof(v)))
        for (i = 0; i < N_STRUMENTI; i++) if (!strcmp(v, STRUMENTO_CFG[i])) g_str = g_str_prima = i;
    if (g_str == S_CONTAGOCCE) g_str = g_str_prima = S_PENNELLO;
    if (valore(buf, "spessore", v, sizeof(v)))
        for (i = 0; i < N_SPESSORI; i++) if (atoi(v) == SPESSORE[i]) g_sp = i;
    if (valore(buf, "colore", v, sizeof(v))) g_col = (unsigned int)strtoul(v, 0, 16) & 0xFFFFFF;
    if (valore(buf, "sfondo", v, sizeof(v))) g_sfondo = (unsigned int)strtoul(v, 0, 16) & 0xFFFFFF;
    if (valore(buf, "cartella", v, sizeof(v))) snprintf(g_cartella, sizeof(g_cartella), "%s", v);
}

/* =============================================================================
 * OPENING AND SAVING
 * ============================================================================= */
static int (*g_carica)(const unsigned char *, unsigned int, EximgBitmap *);
static void (*g_libera)(EximgBitmap *);

/* eximg.so is opened the first time a picture is, as the toolkit does. */
static int eximg_pronta(void)
{
    static const char *const dove[] = {
        "/exwin/lib/eximg.so",
        "/cdrom/exwin/lib/eximg.so"
    };
    static int provato = 0;

    if (!provato) {
        const ExLibTesta *t = exlib_apri_fra(dove, (int)(sizeof dove / sizeof dove[0]));
        provato = 1;
        if (t) {
            g_carica = (int (*)(const unsigned char *, unsigned int, EximgBitmap *))exlib_simbolo(t, "eximg_carica");
            g_libera = (void (*)(EximgBitmap *))exlib_simbolo(t, "eximg_libera");
        }
        if (!g_carica || !g_libera) g_carica = 0;
    }
    return g_carica != 0;
}

static void cartella_da(const char *p)
{
    const char *u = strrchr(p, '/');
    unsigned int n;

    if (!u) return;
    n = (unsigned int)(u - p);
    if (n == 0) n = 1;
    if (n >= sizeof(g_cartella)) return;
    memcpy(g_cartella, p, n);
    g_cartella[n] = 0;
}

static int nuova(int w, int h)
{
    if (!dimensioni(w, h, 0)) return 0;
    storia_svuota();
    g_file[0] = 0;
    g_mod = 0;
    g_ox = g_oy = 0;
    return 1;
}

/* 1 if opened; on 0 it has already said why (with `muto` set, it only returns 0). */
static int apri_file(const char *p, int muto)
{
    int fd = open(p, O_RDONLY, 0), ok;
    long n;
    unsigned char *d;
    EximgBitmap bm;
    unsigned int i, opaca = 1;
    char t[320];

    if (fd < 0) { if (!muto) ex_dlg_avviso("Apri", "Il file non si apre."); return 0; }
    n = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (n <= 0 || n > 64L * 1024 * 1024) { close(fd); if (!muto) ex_dlg_avviso("Apri", "Il file e' vuoto o troppo grande."); return 0; }
    d = (unsigned char *)malloc((unsigned int)n);
    if (!d) { close(fd); if (!muto) ex_dlg_avviso("Apri", "Non c'e' memoria per leggere il file."); return 0; }
    ok = read(fd, d, (unsigned int)n) == n;
    close(fd);
    if (!ok) { free(d); if (!muto) ex_dlg_avviso("Apri", "Il file non si legge fino in fondo."); return 0; }
    if (!eximg_pronta()) {
        free(d);
        if (!muto) ex_dlg_avviso("Apri", "Manca /exwin/lib/eximg.so: senza, nessuna immagine si apre.");
        return 0;
    }
    ok = g_carica(d, (unsigned int)n, &bm);
    free(d);
    if (!ok) {
        if (!muto) ex_dlg_avviso("Apri", "Formato non riconosciuto. Pennello apre BMP, PNG, JPG, GIF e ICO.");
        return 0;
    }
    if (bm.larghezza * bm.altezza > PIXEL_MAX || !dimensioni((int)bm.larghezza, (int)bm.altezza, 0)) {
        snprintf(t, sizeof(t), "L'immagine e' %u x %u: troppo grande (al massimo 4 milioni di pixel, per esempio 2048 x 2048).",
                 bm.larghezza, bm.altezza);
        g_libera(&bm);
        if (!muto) ex_dlg_avviso("Apri", t);
        return 0;
    }
    /* ! ALPHA ZERO EVERYWHERE MEANS «NO ALPHA», NOT «ALL TRANSPARENT»: the PNG
     * and JPG readers of eximg leave it at zero on purpose ("alfa ignorata"
     * in png.c). The picture is opaque then, and is saved so. */
    for (i = 0; i < bm.larghezza * bm.altezza; i++) if (bm.px[i] >> 24) { opaca = 0; break; }
    for (i = 0; i < bm.larghezza * bm.altezza; i++) g_img[i] = opaca ? (bm.px[i] | 0xFF000000u) : bm.px[i];
    g_libera(&bm);
    storia_svuota();
    snprintf(g_file, sizeof(g_file), "%s", p);
    cartella_da(p);
    g_mod = 0;
    g_ox = g_oy = 0;
    return 1;
}

static int su_fd(void *chi, const unsigned char *p, unsigned int n)
{
    return write(*(int *)chi, p, n) == (long)n;
}

static int scrivi_in(const char *p, int png)
{
    EximgBitmap bm;
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644), ok;

    if (fd < 0) return 0;
    bm.larghezza = (unsigned int)g_iw;
    bm.altezza = (unsigned int)g_ih;
    bm.px = g_img;
    ok = png ? eximg_scrivi_png(&bm, su_fd, &fd) : eximg_scrivi_bmp(&bm, su_fd, &fd);
    if (close(fd) < 0) ok = 0;
    return ok;
}

/* ! FIRST A COPY NEXT TO IT, THEN THE RENAME. The VFS truncates a file when
 * it opens it for writing: a save that fails half-way — a full disk, a
 * removed stick — would leave neither the old picture nor the new one. With
 * the copy the old one stays until the new one is whole. On a FAT without
 * long names the copy cannot be created (the name has two dots), and then
 * the picture is written in place: the user has already been asked. */
static int salva_in(const char *p)
{
    char tmp[270];
    int  png = eximg_nome_png(p);
    struct stat st;

    snprintf(tmp, sizeof(tmp), "%s.tmp", p);
    if (scrivi_in(tmp, png)) {
        if (stat(p, &st) == 0) unlink(p);
        if (rename(tmp, p) == 0) return 1;
        ex_dlg_avviso("Salva", "Scritta, ma il nome non si cambia: l'immagine e' nel file con .tmp in fondo.");
        return 0;
    }
    unlink(tmp);
    return scrivi_in(p, png);
}

/* Changes the extension of `p` to .png if it is not .bmp or .png. */
static void estensione_giusta(char *p, unsigned int max)
{
    char *pt = strrchr(p, '.'), *sl = strrchr(p, '/');
    unsigned int l;

    if (pt && (!sl || pt > sl)) {
        if (eximg_nome_png(p)) return;
        if ((pt[1] | 32) == 'b' && (pt[2] | 32) == 'm' && (pt[3] | 32) == 'p' && pt[4] == 0) return;
        *pt = 0;
    }
    l = (unsigned int)strlen(p);
    if (l + 5 < max) strcat(p, ".png");
}

static int salva(int chiedi)
{
    char p[256];
    struct stat st;

    if (!chiedi && g_file[0]) {
        snprintf(p, sizeof(p), "%s", g_file);
    } else {
        if (g_file[0]) snprintf(p, sizeof(p), "%s", g_file);
        else snprintf(p, sizeof(p), "%s/disegno.png", g_cartella[0] ? g_cartella : "");
        estensione_giusta(p, sizeof(p));
        if (!ex_dlg_salva(p, sizeof(p))) return 0;
        estensione_giusta(p, sizeof(p));
        if (strcmp(p, g_file) != 0 && stat(p, &st) == 0 &&
            !ex_dlg_conferma("Salva", "Il file esiste gia'. Lo sostituisco?", "Sostituisci", "Annulla"))
            return 0;
    }
    /* A file opened as JPG or GIF and saved with «Salva» keeps its name only
     * if we can write that format; otherwise it is «Salva con nome». */
    {
        char q[256];
        snprintf(q, sizeof(q), "%s", p);
        estensione_giusta(q, sizeof(q));
        if (strcmp(q, p) != 0) {
            ex_dlg_avviso("Salva", "Questo formato si apre ma non si scrive: Pennello salva in PNG o BMP. Scegli il nome.");
            return salva(1);
        }
    }
    if (!salva_in(p)) { ex_dlg_avviso("Salva", "Il file non si scrive (disco pieno, o in sola lettura?)."); return 0; }
    snprintf(g_file, sizeof(g_file), "%s", p);
    cartella_da(p);
    g_mod = 0;
    opzioni_salva();
    titolo_aggiorna();
    stato_aggiorna();
    return 1;
}

/* 1 if the current picture may be dropped: saved, or the user said so. */
static int si_puo_lasciare(void)
{
    static const char *const voci[] = { "Salva", "Non salvare", "Annulla" };
    int r;

    if (!g_mod) return 1;
    r = ex_dlg_scegli("Pennello", "L'immagine e' cambiata. La salvo prima?", voci, 3);
    if (r == 0) return salva(0);
    return r == 1;
}

static void dopo_cambio(void)
{
    vista_limita();
    barre_aggiorna();
    stato_aggiorna();
    titolo_aggiorna();
    ridisegna();
}

static void apri(void)
{
    char p[256];

    if (!si_puo_lasciare()) return;
    snprintf(p, sizeof(p), "%s/", g_cartella[0] ? g_cartella : "");
    if (!ex_dlg_apri(p, sizeof(p))) return;
    if (apri_file(p, 0)) opzioni_salva();
    dopo_cambio();
}

static void nuovo(void)
{
    char v[40];
    int w, h;

    if (!si_puo_lasciare()) return;
    snprintf(v, sizeof(v), "%dx%d", g_iw, g_ih);
    if (!ex_dlg_chiedi("Nuova immagine", "Larghezza x altezza, in pixel:", "Crea", v, sizeof(v))) return;
    if (!leggi_misura(v, &w, &h) || (unsigned int)w * (unsigned int)h > PIXEL_MAX) {
        ex_dlg_avviso("Nuova immagine", "Misura non valida: al massimo 2048 x 2048 pixel in tutto (4 milioni di pixel).");
        return;
    }
    if (!nuova(w, h)) ex_dlg_avviso("Nuova immagine", "Non c'e' memoria per un'immagine cosi' grande.");
    dopo_cambio();
}

static void esci(void)
{
    if (!si_puo_lasciare()) return;
    opzioni_salva();
    ex_esci(0);
}

/* =============================================================================
 * TOOLS, COLOURS, ZOOM
 * ============================================================================= */
static void strumento_scegli(int s)
{
    if (s < 0 || s >= N_STRUMENTI) return;
    if (s != S_CONTAGOCCE) g_str_prima = s;
    g_str = s;
    ex_accendi(g_r_str[s], 1);
    stato_aggiorna();
}

static void spessore_scegli(int i)
{
    if (i < 0 || i >= N_SPESSORI) return;
    g_sp = i;
    ex_accendi(g_r_sp[i], 1);
    stato_aggiorna();
}

static void scambia_colori(void)
{
    unsigned int t = g_col;
    g_col = g_sfondo;
    g_sfondo = t;
    colori_disegna();
    ex_aggiorna(g_f);
    stato_aggiorna();
}

static void colore_chiedi(void)
{
    char v[16];
    char *e;
    unsigned long c;

    snprintf(v, sizeof(v), "%06X", g_col & 0xFFFFFF);
    if (!ex_dlg_chiedi("Colore", "Il colore in esadecimale, RRGGBB (per esempio FF8000 e' arancio):",
                       "Scegli", v, sizeof(v))) return;
    c = strtoul(v[0] == '#' ? v + 1 : v, &e, 16);
    if (e == v || c > 0xFFFFFF) { ex_dlg_avviso("Colore", "Non e' un colore: servono sei cifre esadecimali."); return; }
    g_col = (unsigned int)c;
    stato_aggiorna();
}

/* The zoom keeps the middle of the canvas where it was. */
static void zoom(int z)
{
    int cx = g_ox + vis_w() / 2, cy = g_oy + vis_h() / 2;

    if (z < 1) z = 1;
    if (z > 8) z = 8;
    g_zoom = z;
    g_ox = cx - vis_w() / 2;
    g_oy = cy - vis_h() / 2;
    vista_limita();
    barre_aggiorna();
    stato_aggiorna();
    ridisegna();
}

static void scorri(int dx, int dy)
{
    g_ox += dx;
    g_oy += dy;
    vista_limita();
    barre_aggiorna();
    tela_disegna(1, 0, 0, 0);
    ex_aggiorna(g_f);
}

static void informazioni(void)
{
    char t[640];

    exinfo_testo(t, sizeof(t), "Pennello", VERSIONE_APP,
                 "Il programma di disegno di ExWin: apre BMP, PNG, JPG, GIF e ICO, "
                 "salva in PNG e BMP.");
    ex_dlg_avviso("Informazioni su", t);
}

static void istruzioni(void)
{
    ex_dlg_testo("Istruzioni di Pennello",
        "GLI STRUMENTI (a sinistra, o con un tasto)\n"
        "  P matita, B pennello, E gomma, A spruzzo, F riempi, I contagocce,\n"
        "  L linea, R rettangolo, Shift+R rettangolo pieno, O ellisse,\n"
        "  Shift+O ellisse piena, T testo.\n"
        "  Le forme si tirano: si preme dove comincia e si lascia dove finisce.\n"
        "  Il contagocce prende il colore e torna allo strumento di prima.\n"
        "  Il testo: un clic dove comincia la scritta, poi la si batte. Lo\n"
        "  spessore ne sceglie la grandezza (12, 16, 24 o 36 pixel).\n"
        "\n"
        "LO SPESSORE: 1, 3, 5 o 9 pixel, oppure [ e ] per stringere e allargare.\n"
        "  La gomma e' piu' larga del pennello dello stesso spessore.\n"
        "\n"
        "I COLORI\n"
        "  Un clic sulla tavolozza sceglie il colore del disegno (il quadrato\n"
        "  in alto a sinistra). Quello sotto e' lo sfondo: e' cio' che lascia la\n"
        "  gomma e il colore dello spazio nuovo. X (o Scambia) li scambia:\n"
        "  per scegliere lo sfondo si sceglie il colore e si scambia.\n"
        "  Colori > Scegli... accetta un colore qualunque in esadecimale.\n"
        "\n"
        "ANNULLARE: Ctrl+Z annulla, Ctrl+Y ripete. Si ricordano le ultime 64\n"
        "  operazioni (meno, se sono grandi). Ruotare svuota la memoria.\n"
        "\n"
        "LA VISTA: + e - cambiano lo zoom (1x, 2x, 4x, 8x), le frecce scorrono.\n"
        "\n"
        "I FILE\n"
        "  Si aprono BMP, PNG, JPG, GIF e ICO. Si salva in PNG o in BMP, secondo\n"
        "  il nome: senza estensione, o con una che non si scrive, diventa .png.\n"
        "  Le immagini fino a 4 milioni di pixel (per esempio 2048 x 2048).\n"
        "  Le scelte (strumento, spessore, colori, cartella) si ricordano in\n"
        "  $HOME/.exwin/config/pennello.cfg.\n");
}

/* =============================================================================
 * THE WINDOW
 * ============================================================================= */
static void disponi(void)
{
    ex_sposta(g_barra_v, TELA_X + tela_w(), TELA_Y);
    ex_misura(g_barra_v, BARRA, tela_h());
    ex_sposta(g_barra_o, TELA_X, TELA_Y + tela_h());
    ex_misura(g_barra_o, tela_w(), BARRA);
    ex_sposta(g_scambia, MARGINE + 48, pal_y() + 8);
    ex_sposta(g_stato, MARGINE + 2, g_h - STATO_H + 2);
    ex_misura(g_stato, g_w - 2 * MARGINE - 4, 16);
    vista_limita();
    barre_aggiorna();
}

static void clic_colori(int mx, int my)
{
    int y = pal_y(), i;

    for (i = 0; i < 28; i++) {
        int cx = PAL_X + (i % 14) * (CELLA + 2), cy = y + (i / 14) * (CELLA + 2);
        if (mx >= cx && mx < cx + CELLA && my >= cy && my < cy + CELLA) {
            g_col = TAVOLOZZA[i];
            colori_disegna();
            ex_aggiorna(g_f);
            stato_aggiorna();
            return;
        }
    }
}

static int tasto(unsigned int wp)
{
    unsigned int c = wp & KBD_KEY_MASK;
    int i;

    if (wp & KBD_MOD_CTRL) {
        switch (c | 32) {
        case 'n': nuovo(); return 1;
        case 'o': apri(); return 1;
        case 's': salva(0); ridisegna(); return 1;
        case 'q': esci(); return 1;
        case 'z': annulla(); return 1;
        case 'y': ripeti(); return 1;
        }
        return 0;
    }
    if (wp & KBD_MOD_ALT) return 0;
    for (i = 0; i < N_STRUMENTI; i++)
        if ((char)c == STRUMENTO_TASTO[i]) { strumento_scegli(i); return 1; }
    switch (c) {
    case 'x': case 'X': scambia_colori(); return 1;
    case '[': spessore_scegli(g_sp - 1); return 1;
    case ']': spessore_scegli(g_sp + 1); return 1;
    case '+': case '=': zoom(g_zoom * 2); return 1;
    case '-': zoom(g_zoom / 2); return 1;
    case KBD_K_LEFT:  scorri(-vis_w() / 8 - 1, 0); return 1;
    case KBD_K_RIGHT: scorri(vis_w() / 8 + 1, 0); return 1;
    case KBD_K_UP:    scorri(0, -vis_h() / 8 - 1); return 1;
    case KBD_K_DOWN:  scorri(0, vis_h() / 8 + 1); return 1;
    case KBD_K_PGUP:  scorri(0, -vis_h()); return 1;
    case KBD_K_PGDN:  scorri(0, vis_h()); return 1;
    }
    return 0;
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        esci();
        return 0;

    case EXM_DISEGNA:
        ex_procedura_base(f, msg, wp, lp);
        tela_disegna(1, 0, 0, 0);
        colori_disegna();
        ex_aggiorna(f);
        return 0;

    case EXM_MISURA:
        g_w = EX_X(lp);
        g_h = EX_Y(lp);
        ex_procedura_base(f, msg, wp, lp);
        disponi();
        ridisegna();
        return 0;

    case EXM_TEMPO:
        /* L'annuncio rimasto indietro (vedi annuncia). */
        if (g_agg_attesa) annuncia(1);
        return 0;

    case EXM_MOUSE_GIU:
    case EXM_DOPPIOCLIC:
        if (nella_tela(EX_X(lp), EX_Y(lp))) op_inizia(EX_X(lp), EX_Y(lp));
        else if (EX_Y(lp) >= pal_y()) clic_colori(EX_X(lp), EX_Y(lp));
        return 0;

    case EXM_MOUSE_MOSSO: {
        /* I movimenti gia' in coda si prendono adesso (vedi schermo_svuota):
         * il primo messaggio che non e' un movimento di questa finestra si
         * smista dopo, quando lo schermo e' in pari. */
        ExMsg m2;
        int   x = EX_X(lp), y = EX_Y(lp), altro = 0;

        while (ex_msg_ora(&m2)) {
            if (m2.msg == EXM_MOUSE_MOSSO && m2.finestra == f) {
                if (!e_forma(g_str)) op_muovi(x, y);   /* il tratto passa per ogni punto */
                x = EX_X(m2.lp); y = EX_Y(m2.lp);
                continue;
            }
            altro = 1;
            break;
        }
        op_muovi(x, y);
        schermo_svuota();
        if (altro) ex_smista(&m2);
        return 0;
    }

    case EXM_MOUSE_SU:
        op_finisci(EX_X(lp), EX_Y(lp));
        return 0;

    case EXM_COMANDO:
        switch (wp) {
        case ID_NUOVO:      nuovo(); break;
        case ID_APRI:       apri(); break;
        case ID_SALVA:      salva(0); ridisegna(); break;
        case ID_SALVA_COME: salva(1); ridisegna(); break;
        case ID_ESCI:       esci(); break;
        case ID_ANNULLA:    annulla(); break;
        case ID_RIPETI:     ripeti(); break;
        case ID_CANCELLA:   cancella_tutto(); break;
        case ID_SPECCHIO_O: specchio(0); break;
        case ID_SPECCHIO_V: specchio(1); break;
        case ID_RUOTA:      ruota(); break;
        case ID_INVERTI:    inverti(); break;
        case ID_DIMENSIONI: chiedi_dimensioni(); ridisegna(); break;
        case ID_ZOOM1:      zoom(1); break;
        case ID_ZOOM2:      zoom(2); break;
        case ID_ZOOM4:      zoom(4); break;
        case ID_ZOOM8:      zoom(8); break;
        case ID_COLORE:     colore_chiedi(); ridisegna(); break;
        case ID_SCAMBIA_M:
        case ID_SCAMBIA:    scambia_colori(); break;
        case ID_INFO:       informazioni(); ridisegna(); break;
        case ID_ISTR:       istruzioni(); ridisegna(); break;
        case ID_BARRA_O:    g_ox = (int)lp; vista_limita(); tela_disegna(1, 0, 0, 0); ex_aggiorna(g_f); break;
        case ID_BARRA_V:    g_oy = (int)lp; vista_limita(); tela_disegna(1, 0, 0, 0); ex_aggiorna(g_f); break;
        default:
            if (wp >= ID_STRUMENTO && wp < ID_STRUMENTO + N_STRUMENTI)
                strumento_scegli((int)(wp - ID_STRUMENTO));
            else if (wp >= ID_SPESSORE && wp < ID_SPESSORE + N_SPESSORI)
                spessore_scegli((int)(wp - ID_SPESSORE));
            break;
        }
        /* ! THE FOCUS GOES BACK TO THE WINDOW after every control: a radio
         * keeping it would take the letters of the tools for itself. */
        ex_fuoco_via(g_f);
        return 0;

    case EXM_TASTO:
        if (tasto(wp)) return 0;
        break;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

/* ! IL DEPOSITO DELL'ANNULLA SI MISURA SULLA MEMORIA LIBERA (28 settembre
 * 2026). Era di 16 MB fissi, e su una macchina da 32 MB con la grafica accesa
 * Pennello appena aperto lasciava 204 KB liberi (misurato con `mem`: 23 MB
 * prima, 204 KB dopo). Al primo trascinamento il server o il kernel non
 * trovavano una pagina — «PMM: OUT OF MEMORY» sulla seriale — e si fermava
 * tutto: era il «blocca tutto col mouse veloce» segnalato. Adesso un quarto
 * del libero, fra 256 KB e 16 MB; se malloc dice di no, la meta'. */
static void pool_prendi(void)
{
    MemInfo      mi;
    unsigned int px = UNDO_POOL_MAX;

    if (meminfo(&mi) == 0 && mi.free_kb) {
        unsigned int quarto = (mi.free_kb / 4u) * 1024u / 4u;     /* in pixel */
        if (quarto < px) px = quarto;
    }
    if (px < UNDO_POOL_MIN) px = UNDO_POOL_MIN;
    while (px >= UNDO_POOL_MIN) {
        g_pool = (unsigned int *)malloc(px * 4u);
        if (g_pool) { g_pool_px = px; return; }
        px /= 2u;
    }
    g_pool_px = 0;              /* niente annulla: si disegna lo stesso */
}

int main(int argc, char **argv)
{
    ExMsg m;
    unsigned int sw = 0, sh = 0;
    int i, y;
    ExFinestra riq;

    opzioni_leggi();
    ex_schermo(&sw, &sh);
    if (sw && (int)sw < g_w + 40) g_w = (int)sw - 40;
    if (sh && (int)sh < g_h + 60) g_h = (int)sh - 60;

    g_f = ex_crea("finestra", "Pennello", EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                  EX_AUTO, EX_AUTO, g_w, g_h, 0, 0, proc);
    if (!g_f) {
        printf("pennello: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }

    /* The canvas at most as big as the screen: g_vista is taken once. */
    g_vista_cap = (sw && sh) ? sw * sh : 1024u * 768u;
    g_vista = (unsigned int *)malloc(g_vista_cap * 4);
    pool_prendi();
    if (!g_vista) g_vista_cap = 0;

    g_menu = ex_menu(g_f);
    ex_menu_voce(g_menu, "File", "Nuova...\tCtrl+N", ID_NUOVO);
    ex_menu_voce(g_menu, "File", "Apri...\tCtrl+O", ID_APRI);
    ex_menu_voce(g_menu, "File", "Salva\tCtrl+S", ID_SALVA);
    ex_menu_voce(g_menu, "File", "Salva con nome...", ID_SALVA_COME);
    ex_menu_voce(g_menu, "File", "-", 0);
    ex_menu_voce(g_menu, "File", "Esci\tCtrl+Q", ID_ESCI);
    ex_menu_voce(g_menu, "Modifica", "Annulla\tCtrl+Z", ID_ANNULLA);
    ex_menu_voce(g_menu, "Modifica", "Ripeti\tCtrl+Y", ID_RIPETI);
    ex_menu_voce(g_menu, "Modifica", "-", 0);
    ex_menu_voce(g_menu, "Modifica", "Cancella tutto", ID_CANCELLA);
    ex_menu_voce(g_menu, "Immagine", "Specchio orizzontale", ID_SPECCHIO_O);
    ex_menu_voce(g_menu, "Immagine", "Specchio verticale", ID_SPECCHIO_V);
    ex_menu_voce(g_menu, "Immagine", "Ruota di 90 gradi", ID_RUOTA);
    ex_menu_voce(g_menu, "Immagine", "Inverti i colori", ID_INVERTI);
    ex_menu_voce(g_menu, "Immagine", "-", 0);
    ex_menu_voce(g_menu, "Immagine", "Dimensioni...", ID_DIMENSIONI);
    ex_menu_voce(g_menu, "Vista", "Zoom 1x", ID_ZOOM1);
    ex_menu_voce(g_menu, "Vista", "Zoom 2x", ID_ZOOM2);
    ex_menu_voce(g_menu, "Vista", "Zoom 4x", ID_ZOOM4);
    ex_menu_voce(g_menu, "Vista", "Zoom 8x", ID_ZOOM8);
    ex_menu_voce(g_menu, "Colori", "Scegli...", ID_COLORE);
    ex_menu_voce(g_menu, "Colori", "Scambia con lo sfondo\tX", ID_SCAMBIA_M);
    ex_menu_voce(g_menu, "Info", "Informazioni su", ID_INFO);
    ex_menu_voce(g_menu, "Info", "Istruzioni", ID_ISTR);

    riq = ex_crea("riquadro", "Strumenti", EX_FIGLIO, MARGINE, TELA_Y,
                  ATTR_W, 18 + N_STRUMENTI * 20, g_f, 0, 0);
    for (i = 0; i < N_STRUMENTI; i++)
        g_r_str[i] = ex_crea("radio", STRUMENTO_NOME[i], EX_FIGLIO, 6, 16 + i * 20, ATTR_W - 12, 18,
                             riq, (unsigned int)(ID_STRUMENTO + i), 0);
    y = TELA_Y + 18 + N_STRUMENTI * 20 + 6;
    riq = ex_crea("riquadro", "Spessore", EX_FIGLIO, MARGINE, y, ATTR_W, 60, g_f, 0, 0);
    for (i = 0; i < N_SPESSORI; i++)
        g_r_sp[i] = ex_crea("radio", SPESSORE_NOME[i], EX_FIGLIO, 6 + (i % 2) * 48, 16 + (i / 2) * 20, 44, 18,
                            riq, (unsigned int)(ID_SPESSORE + i), 0);

    g_barra_v = ex_crea("scorrimento", "", EX_FIGLIO, TELA_X + tela_w(), TELA_Y, BARRA, tela_h(), g_f, ID_BARRA_V, 0);
    g_barra_o = ex_crea("scorrimento", "", EX_FIGLIO, TELA_X, TELA_Y + tela_h(), tela_w(), BARRA, g_f, ID_BARRA_O, 0);
    g_scambia = ex_crea("pulsante", "Scambia", EX_FIGLIO, MARGINE + 48, pal_y() + 8, 56, 22, g_f, ID_SCAMBIA, 0);
    g_stato = ex_crea("etichetta", "", EX_FIGLIO, MARGINE + 2, g_h - STATO_H + 2, g_w - 2 * MARGINE - 4, 16, g_f, 0, 0);

    /* The picture: the file on the command line, or a blank one as large as
     * the canvas. */
    if (!(argc > 1 && argv[1][0] != '-' && apri_file(argv[1], 1))) {
        int w = tela_w(), h = tela_h();
        if (!nuova(w, h)) {
            printf("pennello: non c'e' memoria nemmeno per un'immagine vuota.\n");
            return 1;
        }
        if (argc > 1 && argv[1][0] != '-')
            snprintf(g_file, sizeof(g_file), "%s", argv[1]);   /* a new file with that name */
    }

    strumento_scegli(g_str);
    spessore_scegli(g_sp);
    disponi();
    titolo_aggiorna();
    stato_aggiorna();
    ex_fuoco_via(g_f);
    ridisegna();

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}
