/* =============================================================================
 * exwin/bin/immagini/immagini.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Immagini, the picture viewer of ExWin (@IMMAGINI, 28 September 2026)
 *
 *     /exwin/bin/immagini [file | directory]
 *
 * It shows one picture, and goes to the next or the previous one in the same
 * directory (Pag giu / Spazio, Pag su / Backspace, Home, Fine). The file
 * manager opens it on a double click on a picture, through
 * /exwin/lib/tipi.txt.
 *
 * THE THREE DECISIONS THE TASK ASKED FOR BEFORE WRITING:
 *   - the formats are eximg's: BMP, PNG, JPG, GIF, ICO, WebP. The directory list
 *     holds only those, so «next» never lands on a file it cannot show;
 *   - it zooms: «fit to the window» for a picture larger than the window (the
 *     default), 100%, and steps from 10% to 800%. Shrinking AVERAGES the
 *     pixels that collapse into one, enlarging repeats them;
 *   - the file manager reaches it through a rule «these extensions open with
 *     that program», which is a file of the system and not of this program.
 *
 * ! ONLY WHAT IS VISIBLE IS COMPUTED. The scaled picture is never built:
 * every screen pixel is worked out from the source when it is drawn. At 800%
 * a 2048-pixel picture would be 16384 wide, 1 GB in memory; the window needs
 * one screen of pixels whatever the zoom.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"
#include "exlib.h"
#include "eximg.h"
#include "kbd_proto.h"

/* +0.001 a ogni modifica: `immagini -version` la stampa. Vedi EX_VERSIONE. */
#define VERSIONE_APP "0.003"
EX_VERSIONE("immagini", VERSIONE_APP);

#define MENU_H      20
#define BARRA       16
#define STATO_H     20
#define FIN_W       720
#define FIN_H       540
#define SFONDO      0x00404040      /* around the picture */
#define NOMI_MAX    512             /* pictures in one directory */
#define PERC_MAX    256

enum {
    ID_APRI = 1, ID_PENNELLO, ID_ESCI,
    ID_ADATTA, ID_VERA, ID_PIU, ID_MENO,
    ID_SUCC, ID_PREC, ID_PRIMA, ID_ULTIMA,
    ID_INFO, ID_ISTR,
    ID_BARRA_V = 30, ID_BARRA_O
};

static ExFinestra g_f, g_menu, g_barra_v, g_barra_o, g_stato;
static int        g_w = FIN_W, g_h = FIN_H;

static EximgBitmap g_bm;                /* the picture shown, empty if none */
static char        g_file[PERC_MAX];
static char        g_errore[160];       /* shown in the canvas when a file does not open */

static int  g_adatta = 1;               /* fit to the window */
static int  g_perc = 100;               /* the zoom when not fitting */
static int  g_tw, g_th;                 /* the picture as shown */
static int  g_ox, g_oy;                 /* first shown pixel of the scaled picture */

static unsigned int *g_vista;
static unsigned int  g_vista_cap;

/* The pictures of the directory, sorted, and which one is shown. */
static char g_dir[PERC_MAX];
static char g_nomi[NOMI_MAX][DIRENT_NAME_MAX];
static int  g_n = 0, g_cur = -1;

/* For dragging the view with the mouse. */
static int  g_tira = 0, g_tx, g_ty, g_tox, g_toy;

static const int ZOOM[] = { 10, 25, 50, 75, 100, 150, 200, 300, 400, 800 };
#define N_ZOOM ((int)(sizeof(ZOOM) / sizeof(ZOOM[0])))

/* =============================================================================
 * GEOMETRY
 * ============================================================================= */
static int tela_x(void) { return 0; }
static int tela_y(void) { return MENU_H + 2; }
static int tela_w(void) { int v = g_w - BARRA; return v > 8 ? v : 8; }
static int tela_h(void) { int v = g_h - tela_y() - BARRA - STATO_H; return v > 8 ? v : 8; }

/* The size the picture is shown at, from the mode and the window. */
static void misura_calcola(void)
{
    int w = (int)g_bm.larghezza, h = (int)g_bm.altezza, cw = tela_w(), ch = tela_h();

    if (!g_bm.px) { g_tw = g_th = 0; return; }
    if (g_adatta) {
        g_tw = w; g_th = h;
        if (w > cw || h > ch) {
            /* ! THE COMPARISON IS CROSSED, NOT DIVIDED: w/cw and h/ch in
             * integers would both be 0 or 1 for most pictures.
             * ! AND IN 32 BITS UNSIGNED, ON PURPOSE: a 64-bit division needs
             * __divdi3 from libgcc, which programs here do not link. The
             * largest product anywhere in this file is 65536 * 8192 (800% of
             * the widest picture eximg reads), which fits. */
            if ((unsigned)w * (unsigned)ch > (unsigned)h * (unsigned)cw) { g_tw = cw; g_th = (int)((unsigned)h * (unsigned)cw / (unsigned)w); }
            else                                                  { g_th = ch; g_tw = (int)((unsigned)w * (unsigned)ch / (unsigned)h); }
        }
    } else {
        g_tw = (int)((unsigned)w * (unsigned)g_perc / 100u);
        g_th = (int)((unsigned)h * (unsigned)g_perc / 100u);
    }
    if (g_tw < 1) g_tw = 1;
    if (g_th < 1) g_th = 1;
}

/* The zoom in percent that is shown now (also when fitting). */
static int perc_attuale(void)
{
    if (!g_bm.px) return 100;
    return (int)((unsigned)g_tw * 100u / g_bm.larghezza);
}

static void vista_limita(void)
{
    int mx = g_tw - tela_w(), my = g_th - tela_h();
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (g_ox > mx) g_ox = mx;
    if (g_oy > my) g_oy = my;
    if (g_ox < 0) g_ox = 0;
    if (g_oy < 0) g_oy = 0;
}

static void barre_aggiorna(void)
{
    int mx = g_tw - tela_w(), my = g_th - tela_h();
    ex_scorri_limiti(g_barra_o, (unsigned int)(mx > 0 ? mx : 0), (unsigned int)tela_w());
    ex_scorri_vai(g_barra_o, (unsigned int)g_ox);
    ex_scorri_limiti(g_barra_v, (unsigned int)(my > 0 ? my : 0), (unsigned int)tela_h());
    ex_scorri_vai(g_barra_v, (unsigned int)g_oy);
}

/* =============================================================================
 * DRAWING
 * ============================================================================= */

/* ! AT MOST FOUR SAMPLES A SIDE WHEN SHRINKING. A 4096-pixel photo fitted in
 * 600 pixels collapses about 7x7 pixels into one: averaging them all is 49
 * reads per screen pixel, and on this machine a redraw would take seconds.
 * Four evenly spaced samples a side already remove the stairs and the
 * sparkle that one sample (nearest pixel) leaves. */
#define CAMPIONI 4

static void tela_disegna(void)
{
    static int sx0[4096], sx1[4096];
    int cw = tela_w(), ch = tela_h(), X = tela_x(), Y = tela_y();
    int w = (int)g_bm.larghezza, h = (int)g_bm.altezza;
    int vw, vh, px, py, c, r;

    if (!g_bm.px) {
        ex_riempi(g_f, X, Y, cw, ch, SFONDO);
        ex_scrivi(g_f, X + 16, Y + 16,
                  g_errore[0] ? g_errore : "Nessuna immagine. Ctrl+O per aprirne una.", EX_BIANCO);
        return;
    }
    vw = g_tw - g_ox; if (vw > cw) vw = cw;
    vh = g_th - g_oy; if (vh > ch) vh = ch;
    px = g_tw < cw ? (cw - g_tw) / 2 : 0;
    py = g_th < ch ? (ch - g_th) / 2 : 0;
    if (vw > 4096) vw = 4096;
    if ((unsigned int)(vw * vh) > g_vista_cap) return;

    /* The grey around: four bands, not the whole canvas under the picture,
     * or every redraw would flash. */
    if (py > 0)            ex_riempi(g_f, X, Y, cw, py, SFONDO);
    if (py + vh < ch)      ex_riempi(g_f, X, Y + py + vh, cw, ch - py - vh, SFONDO);
    if (px > 0)            ex_riempi(g_f, X, Y + py, px, vh, SFONDO);
    if (px + vw < cw)      ex_riempi(g_f, X + px + vw, Y + py, cw - px - vw, vh, SFONDO);

    for (c = 0; c < vw; c++) {
        unsigned int a = (unsigned)(g_ox + c) * (unsigned)w / (unsigned)g_tw, b = (unsigned)(g_ox + c + 1) * (unsigned)w / (unsigned)g_tw;
        if (b <= a) b = a + 1;
        if (b > (unsigned)w) b = (unsigned)w;
        sx0[c] = (int)a; sx1[c] = (int)b;
    }
    for (r = 0; r < vh; r++) {
        int y0 = (int)((unsigned)(g_oy + r) * (unsigned)h / (unsigned)g_th), y1 = (int)((unsigned)(g_oy + r + 1) * (unsigned)h / (unsigned)g_th);
        int sy, py_ = 1;
        unsigned int *d = g_vista + (unsigned int)r * (unsigned int)vw;

        if (y1 <= y0) y1 = y0 + 1;
        if (y1 > h) y1 = h;
        if (y1 - y0 > CAMPIONI) py_ = (y1 - y0) / CAMPIONI;
        for (c = 0; c < vw; c++) {
            int a = sx0[c], b = sx1[c], px_ = 1, sx;
            unsigned int sr = 0, sg = 0, sb = 0, n = 0;

            if (b - a == 1 && y1 - y0 == 1) { d[c] = g_bm.px[(unsigned int)y0 * (unsigned int)w + (unsigned int)a]; continue; }
            if (b - a > CAMPIONI) px_ = (b - a) / CAMPIONI;
            for (sy = y0; sy < y1; sy += py_)
                for (sx = a; sx < b; sx += px_) {
                    unsigned int v = g_bm.px[(unsigned int)sy * (unsigned int)w + (unsigned int)sx];
                    sr += (v >> 16) & 0xFF; sg += (v >> 8) & 0xFF; sb += v & 0xFF; n++;
                }
            d[c] = ((sr / n) << 16) | ((sg / n) << 8) | (sb / n);
        }
    }
    ex_pixmap(g_f, X + px, Y + py, vw, vh, g_vista, (unsigned int)vw);
}

static void stato_aggiorna(void)
{
    char t[300];
    const char *n = strrchr(g_file, '/');

    n = n ? n + 1 : g_file;
    if (!g_bm.px) snprintf(t, sizeof(t), "%s", g_file[0] ? n : "");
    else if (g_cur >= 0)
        snprintf(t, sizeof(t), "%s   %u x %u   %d%%%s   %d di %d", n, g_bm.larghezza, g_bm.altezza,
                 perc_attuale(), g_adatta ? " (adattata)" : "", g_cur + 1, g_n);
    else
        snprintf(t, sizeof(t), "%s   %u x %u   %d%%%s", n, g_bm.larghezza, g_bm.altezza,
                 perc_attuale(), g_adatta ? " (adattata)" : "");
    ex_testo_metti(g_stato, t);
}

static void titolo_aggiorna(void)
{
    char t[300];
    const char *n = strrchr(g_file, '/');
    n = n ? n + 1 : g_file;
    snprintf(t, sizeof(t), g_file[0] ? "Immagini - %s" : "Immagini", n);
    ex_titolo(g_f, t);
}

static void tutto(void)
{
    misura_calcola();
    vista_limita();
    barre_aggiorna();
    stato_aggiorna();
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    tela_disegna();
    ex_aggiorna(g_f);
}

/* =============================================================================
 * LOADING
 * ============================================================================= */
static int (*g_carica)(const unsigned char *, unsigned int, EximgBitmap *);
static void (*g_libera)(EximgBitmap *);
/* Le GIF animate (@NAV-GIF): facoltative, come nel navigatore. */
static int  (*g_an_apri)(const unsigned char *, unsigned int, EximgAnim **);
static int  (*g_an_passo)(EximgAnim *, EximgBitmap *, unsigned int *);
static void (*g_an_chiudi)(EximgAnim *);
static EximgAnim   *g_an;
static unsigned int g_an_prossimo;

static int eximg_pronta(void)
{
    static const char *const dove[] = { "/exwin/lib/eximg.so", "/cdrom/exwin/lib/eximg.so" };
    static int provato = 0;

    if (!provato) {
        const ExLibTesta *t = exlib_apri_fra(dove, (int)(sizeof dove / sizeof dove[0]));
        provato = 1;
        if (t) {
            g_carica = (int (*)(const unsigned char *, unsigned int, EximgBitmap *))exlib_simbolo(t, "eximg_carica");
            g_libera = (void (*)(EximgBitmap *))exlib_simbolo(t, "eximg_libera");
        }
        if (!g_carica || !g_libera) g_carica = 0;
        if (t) {
            g_an_apri   = (int (*)(const unsigned char *, unsigned int, EximgAnim **))exlib_simbolo(t, "eximg_anima_apri");
            g_an_passo  = (int (*)(EximgAnim *, EximgBitmap *, unsigned int *))exlib_simbolo(t, "eximg_anima_passo");
            g_an_chiudi = (void (*)(EximgAnim *))exlib_simbolo(t, "eximg_anima_chiudi");
            if (!g_an_apri || !g_an_passo || !g_an_chiudi) g_an_apri = 0;
        }
    }
    return g_carica != 0;
}

/* The frame of the animation into the picture shown, the alpha blended on
 * the grey of the canvas like a still picture's. 0 if the sizes differ
 * (then the first frame simply stays). */
static int anima_copia(const EximgBitmap *v)
{
    unsigned int i;

    if (!g_bm.px || v->larghezza != g_bm.larghezza || v->altezza != g_bm.altezza) return 0;
    for (i = 0; i < v->larghezza * v->altezza; i++) {
        unsigned int s = v->px[i], a = s >> 24, k, o = 0;
        for (k = 0; k < 24; k += 8)
            o |= ((((s >> k) & 0xFF) * a + ((SFONDO >> k) & 0xFF) * (255 - a)) / 255) << k;
        g_bm.px[i] = o;
    }
    return 1;
}

static void anima_ferma(void)
{
    if (g_an) g_an_chiudi(g_an);
    g_an = 0;
    ex_sveglia(g_f, 0);
}

/* Loads `p`. On failure the canvas says why (g_errore) and 0 comes back. */
static int carica(const char *p)
{
    int fd, ok;
    long n;
    unsigned char *d;
    unsigned int i, opaca = 1;

    if (g_bm.px) g_libera(&g_bm);
    g_bm.px = 0;
    g_errore[0] = 0;
    anima_ferma();
    snprintf(g_file, sizeof(g_file), "%s", p);

    fd = open(p, O_RDONLY, 0);
    if (fd < 0) { snprintf(g_errore, sizeof(g_errore), "Il file non si apre."); return 0; }
    n = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (n <= 0 || n > 64L * 1024 * 1024) { close(fd); snprintf(g_errore, sizeof(g_errore), "Il file e' vuoto o troppo grande."); return 0; }
    d = (unsigned char *)malloc((unsigned int)n);
    if (!d) { close(fd); snprintf(g_errore, sizeof(g_errore), "Non c'e' memoria per leggere il file."); return 0; }
    ok = read(fd, d, (unsigned int)n) == n;
    close(fd);
    if (!ok) { free(d); snprintf(g_errore, sizeof(g_errore), "Il file non si legge fino in fondo."); return 0; }
    if (!eximg_pronta()) { free(d); snprintf(g_errore, sizeof(g_errore), "Manca /exwin/lib/eximg.so."); return 0; }
    ok = g_carica(d, (unsigned int)n, &g_bm);
    /* ! L'ANIMAZIONE SI APRE PRIMA DI LIBERARE I DATI (la libreria se ne fa
     * una copia sua), e il primo passo si fa subito: la tela composta prende
     * il posto del primo fotogramma nudo. */
    if (ok && g_an_apri && g_an_apri(d, (unsigned int)n, &g_an)) {
        EximgBitmap v;
        unsigned int ms;
        if (g_an_passo(g_an, &v, &ms) && g_bm.larghezza == v.larghezza && g_bm.altezza == v.altezza) {
            g_an_prossimo = uptime_ms() + ms;
            ex_sveglia(g_f, 100);
        } else anima_ferma();
    }
    free(d);
    if (!ok) {
        g_bm.px = 0;
        /* ! eximg SAYS 0 FOR SEVERAL DIFFERENT THINGS: a format it does not
         * know, a broken file, a picture it has no memory to decode (a big
         * PNG wants the whole image three times over). The message names
         * them all instead of guessing one. The first run of the test said
         * «formato non riconosciuto» for a PNG that was fine — eximg read
         * only 8-bit PNGs then — and a message that is sure of itself sent
         * the search the wrong way. */
        snprintf(g_errore, sizeof(g_errore),
                 "Non si legge: non e' BMP, PNG, JPG, GIF, ICO o WebP, e' guasto, o manca la memoria.");
        return 0;
    }
    /* ! ALPHA ZERO EVERYWHERE MEANS «NO ALPHA» (the PNG and JPG readers of
     * eximg leave it so, see png.c). A real alpha — an icon — is blended on
     * the grey of the canvas, the colour it is seen against. */
    for (i = 0; i < g_bm.larghezza * g_bm.altezza; i++) if (g_bm.px[i] >> 24) { opaca = 0; break; }
    if (!opaca)
        for (i = 0; i < g_bm.larghezza * g_bm.altezza; i++) {
            unsigned int v = g_bm.px[i], a = v >> 24, k, o = 0;
            for (k = 0; k < 24; k += 8) {
                unsigned int s = (v >> k) & 0xFF, b = (SFONDO >> k) & 0xFF;
                o |= ((s * a + b * (255 - a)) / 255) << k;
            }
            g_bm.px[i] = o;
        }
    return 1;
}

/* =============================================================================
 * THE DIRECTORY
 * ============================================================================= */
static int immagine_per_nome(const char *nome)
{
    const char *p = strrchr(nome, '.');
    char e[8];
    unsigned int k = 0;

    if (!p || p == nome) return 0;
    for (p++; *p && k + 1 < sizeof(e); p++, k++) e[k] = (char)((*p >= 'A' && *p <= 'Z') ? *p + 32 : *p);
    e[k] = 0;
    return !strcmp(e, "bmp") || !strcmp(e, "png") || !strcmp(e, "jpg") ||
           !strcmp(e, "jpeg") || !strcmp(e, "gif") || !strcmp(e, "ico") ||
           !strcmp(e, "webp");
}

static int confronta(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = (*a >= 'A' && *a <= 'Z') ? *a + 32 : *a, y = (*b >= 'A' && *b <= 'Z') ? *b + 32 : *b;
        if (x != y || !x) return x - y;
    }
}

/* Reads the pictures of `dir`, sorted without regard to case. */
static void cartella_leggi(const char *dir)
{
    DirEntry v[LISTDIR_MAX_BATCH];
    int n, i, j, start = 0;
    char t[DIRENT_NAME_MAX];

    snprintf(g_dir, sizeof(g_dir), "%s", dir[0] ? dir : "/");
    g_n = 0;
    while ((n = listdir_from(g_dir, v, LISTDIR_MAX_BATCH, start)) > 0) {
        for (i = 0; i < n && g_n < NOMI_MAX; i++)
            if (!v[i].is_dir && immagine_per_nome(v[i].name)) {
                strncpy(g_nomi[g_n], v[i].name, DIRENT_NAME_MAX - 1);
                g_nomi[g_n][DIRENT_NAME_MAX - 1] = 0;
                g_n++;
            }
        start += n;
    }
    for (i = 1; i < g_n; i++) {                 /* insertion: a directory is small */
        memcpy(t, g_nomi[i], sizeof(t));
        for (j = i - 1; j >= 0 && confronta(g_nomi[j], t) > 0; j--) memcpy(g_nomi[j + 1], g_nomi[j], sizeof(t));
        memcpy(g_nomi[j + 1], t, sizeof(t));
    }
}

static void percorso_di(int i, char *out, unsigned int max)
{
    size_t l = strlen(g_dir);
    snprintf(out, max, "%s%s%s", g_dir, (l && g_dir[l - 1] == '/') ? "" : "/", g_nomi[i]);
}

/* Opens `p`, and reads its directory to go to the next ones. */
static void apri_percorso(const char *p)
{
    char d[PERC_MAX];
    const char *u = strrchr(p, '/');
    struct stat st;
    int i;

    if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {    /* a directory: its first picture */
        cartella_leggi(p);
        g_cur = -1;
        if (g_n) { char q[PERC_MAX]; g_cur = 0; percorso_di(0, q, sizeof(q)); carica(q); }
        else { if (g_bm.px) g_libera(&g_bm); g_bm.px = 0; g_file[0] = 0;
               snprintf(g_errore, sizeof(g_errore), "In questa cartella non ci sono immagini."); }
        return;
    }
    if (u) {
        unsigned int n = (unsigned int)(u - p);
        if (n == 0) n = 1;
        if (n >= sizeof(d)) n = sizeof(d) - 1;
        memcpy(d, p, n); d[n] = 0;
    } else snprintf(d, sizeof(d), "/");
    cartella_leggi(d);
    g_cur = -1;
    for (i = 0; i < g_n; i++) if (!strcmp(g_nomi[i], u ? u + 1 : p)) g_cur = i;
    carica(p);
}

static void vai(int i)
{
    char q[PERC_MAX];

    if (g_n == 0) return;
    if (i < 0) i = 0;
    if (i >= g_n) i = g_n - 1;
    if (i == g_cur && g_bm.px) return;
    g_cur = i;
    percorso_di(i, q, sizeof(q));
    carica(q);
    g_ox = g_oy = 0;
    g_adatta = 1;
    titolo_aggiorna();
    tutto();
}

/* =============================================================================
 * COMMANDS
 * ============================================================================= */
static void apri(void)
{
    char p[PERC_MAX];

    snprintf(p, sizeof(p), "%s/", g_dir[0] && strcmp(g_dir, "/") ? g_dir : "");
    if (!ex_dlg_apri(p, sizeof(p))) { tutto(); return; }
    apri_percorso(p);
    g_ox = g_oy = 0;
    g_adatta = 1;
    titolo_aggiorna();
    tutto();
}

static void con_pennello(void)
{
    static const char *const dove[] = { "/exwin/bin/pennello", "/cdrom/exwin/bin/pennello" };
    static char copia[PERC_MAX];
    char *argv[3];
    int i;

    if (!g_file[0]) return;
    snprintf(copia, sizeof(copia), "%s", g_file);
    argv[1] = copia;
    argv[2] = 0;
    for (i = 0; i < 2; i++) {
        argv[0] = (char *)dove[i];
        if (spawn_ex(dove[i], argv, 0, 0, 0) >= 0) return;
    }
    ex_dlg_avviso("Immagini", "Pennello non si trova: /exwin/bin/pennello");
    tutto();
}

/* The zoom steps: from the one shown now to the next one up or down. The
 * middle of the canvas stays where it was. */
static void zoom_passo(int su)
{
    int ora = perc_attuale(), i, nuovo = ora;
    int cx = g_ox + (g_tw < tela_w() ? g_tw : tela_w()) / 2;
    int cy = g_oy + (g_th < tela_h() ? g_th : tela_h()) / 2;

    if (!g_bm.px) return;
    if (su) { for (i = 0; i < N_ZOOM; i++) if (ZOOM[i] > ora) { nuovo = ZOOM[i]; break; } }
    else    { for (i = N_ZOOM - 1; i >= 0; i--) if (ZOOM[i] < ora) { nuovo = ZOOM[i]; break; } }
    g_adatta = 0;
    g_perc = nuovo;
    misura_calcola();
    g_ox = (int)((unsigned)cx * (unsigned)nuovo / (unsigned)ora) - tela_w() / 2;
    g_oy = (int)((unsigned)cy * (unsigned)nuovo / (unsigned)ora) - tela_h() / 2;
    tutto();
}

static void zoom_modo(int adatta)
{
    g_adatta = adatta;
    g_perc = 100;
    g_ox = g_oy = 0;
    tutto();
}

static void scorri(int dx, int dy)
{
    g_ox += dx;
    g_oy += dy;
    vista_limita();
    barre_aggiorna();
    tela_disegna();
    ex_aggiorna(g_f);
}

static void informazioni(void)
{
    char t[640];

    exinfo_testo(t, sizeof(t), "Immagini", VERSIONE_APP,
                 "Il visualizzatore di immagini di ExWin: BMP, PNG, JPG, GIF, ICO e WebP, "
                 "con lo zoom e il passaggio alle altre immagini della cartella.");
    ex_dlg_avviso("Informazioni su", t);
}

static void istruzioni(void)
{
    ex_dlg_testo("Istruzioni di Immagini",
        "SCORRERE LA CARTELLA\n"
        "  Pag giu' o Spazio: la successiva. Pag su' o Backspace: la precedente.\n"
        "  Home e Fine: la prima e l'ultima. Si vedono solo le immagini della\n"
        "  cartella (BMP, PNG, JPG, GIF, ICO, WebP), in ordine di nome.\n"
        "\n"
        "LO ZOOM\n"
        "  A: adatta alla finestra (un'immagine piu' grande si rimpicciolisce,\n"
        "  una piu' piccola resta com'e'). 1: grandezza vera, al 100%.\n"
        "  + e -: dal 10% all'800%. Rimpicciolendo si fa la media dei pixel.\n"
        "\n"
        "SPOSTARSI DENTRO L'IMMAGINE: le frecce, le barre, o si tira col mouse.\n"
        "\n"
        "Ctrl+O apre un'immagine, Ctrl+E la apre in Pennello per modificarla.\n"
        "Dal file manager ci si arriva col doppio clic su un'immagine: la regola\n"
        "sta in /exwin/lib/tipi.txt.\n");
}

/* =============================================================================
 * THE WINDOW
 * ============================================================================= */
static void disponi(void)
{
    ex_sposta(g_barra_v, tela_x() + tela_w(), tela_y());
    ex_misura(g_barra_v, BARRA, tela_h());
    ex_sposta(g_barra_o, tela_x(), tela_y() + tela_h());
    ex_misura(g_barra_o, tela_w(), BARRA);
    ex_sposta(g_stato, 6, g_h - STATO_H + 2);
    ex_misura(g_stato, g_w - 12, 16);
}

static int tasto(unsigned int wp)
{
    unsigned int c = wp & KBD_KEY_MASK;

    if (wp & KBD_MOD_CTRL) {
        switch (c | 32) {
        case 'o': apri(); return 1;
        case 'e': con_pennello(); return 1;
        case 'q': ex_esci(0); return 1;
        }
        return 0;
    }
    if (wp & KBD_MOD_ALT) return 0;
    switch (c) {
    case KBD_K_PGDN: case ' ':  vai(g_cur + 1); return 1;
    case KBD_K_PGUP: case '\b': vai(g_cur - 1); return 1;
    case KBD_K_HOME:  vai(0); return 1;
    case KBD_K_END:   vai(g_n - 1); return 1;
    case 'a': case 'A': zoom_modo(1); return 1;
    case '1':         zoom_modo(0); return 1;
    case '+': case '=': zoom_passo(1); return 1;
    case '-':         zoom_passo(0); return 1;
    case KBD_K_LEFT:  scorri(-tela_w() / 8, 0); return 1;
    case KBD_K_RIGHT: scorri(tela_w() / 8, 0); return 1;
    case KBD_K_UP:    scorri(0, -tela_h() / 8); return 1;
    case KBD_K_DOWN:  scorri(0, tela_h() / 8); return 1;
    }
    return 0;
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

    case EXM_DISEGNA:
        ex_procedura_base(f, msg, wp, lp);
        tela_disegna();
        ex_aggiorna(f);
        return 0;

    case EXM_MISURA:
        g_w = EX_X(lp);
        g_h = EX_Y(lp);
        ex_procedura_base(f, msg, wp, lp);
        disponi();
        tutto();
        return 0;

    /* The animation (@NAV-GIF): the frame whose time has come. The wake-up
     * is 200 ms at best (see EXM_TEMPO in exwin.h): a 100 ms GIF runs at half
     * speed, which is written, and better than a still frame. */
    case EXM_TEMPO:
        if (g_an && (int)(uptime_ms() - g_an_prossimo) >= 0) {
            EximgBitmap v;
            unsigned int ms = 100;
            if (!g_an_passo(g_an, &v, &ms) || !anima_copia(&v)) { anima_ferma(); return 0; }
            g_an_prossimo = uptime_ms() + ms;
            tela_disegna();
            ex_aggiorna(g_f);
        }
        return 0;

    case EXM_MOUSE_GIU:
        g_tira = 1;
        g_tx = EX_X(lp); g_ty = EX_Y(lp);
        g_tox = g_ox; g_toy = g_oy;
        return 0;

    case EXM_MOUSE_MOSSO:
        if (g_tira) {
            int x = EX_X(lp), y = EX_Y(lp);    /* negative off the window */
            g_ox = g_tox - (x - g_tx);
            g_oy = g_toy - (y - g_ty);
            scorri(0, 0);
        }
        return 0;

    case EXM_MOUSE_SU:
        g_tira = 0;
        return 0;

    case EXM_DOPPIOCLIC:
        /* A double click switches between «fit» and 100%, as in most viewers. */
        zoom_modo(!g_adatta);
        return 0;

    case EXM_COMANDO:
        switch (wp) {
        case ID_APRI:     apri(); break;
        case ID_PENNELLO: con_pennello(); break;
        case ID_ESCI:     ex_esci(0); break;
        case ID_ADATTA:   zoom_modo(1); break;
        case ID_VERA:     zoom_modo(0); break;
        case ID_PIU:      zoom_passo(1); break;
        case ID_MENO:     zoom_passo(0); break;
        case ID_SUCC:     vai(g_cur + 1); break;
        case ID_PREC:     vai(g_cur - 1); break;
        case ID_PRIMA:    vai(0); break;
        case ID_ULTIMA:   vai(g_n - 1); break;
        case ID_INFO:     informazioni(); tutto(); break;
        case ID_ISTR:     istruzioni(); tutto(); break;
        case ID_BARRA_O:  g_ox = (int)lp; scorri(0, 0); break;
        case ID_BARRA_V:  g_oy = (int)lp; scorri(0, 0); break;
        }
        ex_fuoco_via(g_f);
        return 0;

    case EXM_TASTO:
        if (tasto(wp)) return 0;
        break;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

int main(int argc, char **argv)
{
    ExMsg m;
    unsigned int sw = 0, sh = 0;

    ex_schermo(&sw, &sh);
    if (sw && (int)sw < g_w + 40) g_w = (int)sw - 40;
    if (sh && (int)sh < g_h + 60) g_h = (int)sh - 60;

    g_f = ex_crea("finestra", "Immagini", EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                  EX_AUTO, EX_AUTO, g_w, g_h, 0, 0, proc);
    if (!g_f) {
        printf("immagini: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }
    g_vista_cap = (sw && sh) ? sw * sh : 1024u * 768u;
    g_vista = (unsigned int *)malloc(g_vista_cap * 4);
    if (!g_vista) g_vista_cap = 0;

    g_menu = ex_menu(g_f);
    ex_menu_voce(g_menu, "File", "Apri...\tCtrl+O", ID_APRI);
    ex_menu_voce(g_menu, "File", "Modifica con Pennello\tCtrl+E", ID_PENNELLO);
    ex_menu_voce(g_menu, "File", "-", 0);
    ex_menu_voce(g_menu, "File", "Esci\tCtrl+Q", ID_ESCI);
    ex_menu_voce(g_menu, "Vista", "Adatta alla finestra\tA", ID_ADATTA);
    ex_menu_voce(g_menu, "Vista", "Grandezza vera\t1", ID_VERA);
    ex_menu_voce(g_menu, "Vista", "Ingrandisci\t+", ID_PIU);
    ex_menu_voce(g_menu, "Vista", "Rimpicciolisci\t-", ID_MENO);
    ex_menu_voce(g_menu, "Vai", "Successiva\tPag giu'", ID_SUCC);
    ex_menu_voce(g_menu, "Vai", "Precedente\tPag su'", ID_PREC);
    ex_menu_voce(g_menu, "Vai", "Prima\tHome", ID_PRIMA);
    ex_menu_voce(g_menu, "Vai", "Ultima\tFine", ID_ULTIMA);
    ex_menu_voce(g_menu, "Info", "Informazioni su", ID_INFO);
    ex_menu_voce(g_menu, "Info", "Istruzioni", ID_ISTR);

    g_barra_v = ex_crea("scorrimento", "", EX_FIGLIO, tela_w(), tela_y(), BARRA, tela_h(), g_f, ID_BARRA_V, 0);
    g_barra_o = ex_crea("scorrimento", "", EX_FIGLIO, 0, tela_y() + tela_h(), tela_w(), BARRA, g_f, ID_BARRA_O, 0);
    g_stato = ex_crea("etichetta", "", EX_FIGLIO, 6, g_h - STATO_H + 2, g_w - 12, 16, g_f, 0, 0);

    if (argc > 1 && argv[1][0] != '-') apri_percorso(argv[1]);
    else {
        const char *casa = getenv("HOME");
        snprintf(g_dir, sizeof(g_dir), "%s", casa && casa[0] ? casa : "/");
    }

    disponi();
    titolo_aggiorna();
    ex_fuoco_via(g_f);
    tutto();

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}
