/* =============================================================================
 * debug/grafica/extema.c  ->  destinazione: lib/exwin/extema.c
 * EX-OS — Extensible Operating System
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il motore dei temi: primitive con antialias in aritmetica intera e il
 * disegno di ogni controllo guidato da ExTema. Vedi extema.h.
 *
 * Dipende da una cosa sola fuori di se': il font di sistema 8x16 (CP437),
 * `font8x16`, che exwin.c e wserver.c hanno gia' (kernel/arch/x86/font8x16.c
 * sull'host). Niente libc: solo le due funzioni di stringa scritte qui.
 * ============================================================================= */
#include "extema.h"

extern const unsigned char font8x16[256 * 16];

/* -----------------------------------------------------------------------------
 * Aritmetica
 * --------------------------------------------------------------------------- */
static int minimo(int a, int b) { return a < b ? a : b; }
static int massimo(int a, int b) { return a > b ? a : b; }

static unsigned int radq(unsigned int v)          /* radice quadrata intera */
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

static int lung(const char *s) { int n = 0; while (s && s[n]) n++; return n; }

/* seno da 0 a 90 gradi, in millesimi */
static const short g_seno90[91] = {
       0,  17,  35,  52,  70,  87, 105, 122, 139, 156, 174, 191, 208, 225, 242,
     259, 276, 292, 309, 326, 342, 358, 375, 391, 407, 423, 438, 454, 469, 485,
     500, 515, 530, 545, 559, 574, 588, 602, 616, 629, 643, 656, 669, 682, 695,
     707, 719, 731, 743, 755, 766, 777, 788, 799, 809, 819, 829, 839, 848, 857,
     866, 875, 883, 891, 899, 906, 914, 921, 927, 934, 940, 946, 951, 956, 961,
     966, 970, 974, 978, 982, 985, 988, 990, 993, 995, 996, 998, 999, 999,1000,
    1000 };

int ext_seno(int g)
{
    g %= 360; if (g < 0) g += 360;
    if (g <= 90)  return g_seno90[g];
    if (g <= 180) return g_seno90[180 - g];
    if (g <= 270) return -g_seno90[g - 180];
    return -g_seno90[360 - g];
}
int ext_coseno(int g) { return ext_seno(g + 90); }

/* -----------------------------------------------------------------------------
 * La tela
 * --------------------------------------------------------------------------- */
void ext_tela(ExTela *t, unsigned int *pix, int w, int h, int passo)
{
    t->pix = pix; t->w = w; t->h = h; t->passo = passo;
    ext_ritaglio_tutto(t);
}

void ext_ritaglio_tutto(ExTela *t) { t->cx0 = 0; t->cy0 = 0; t->cx1 = t->w; t->cy1 = t->h; }

void ext_ritaglio(ExTela *t, int x, int y, int w, int h)
{
    t->cx0 = massimo(x, 0);        t->cy0 = massimo(y, 0);
    t->cx1 = minimo(x + w, t->w);  t->cy1 = minimo(y + h, t->h);
    if (t->cx1 < t->cx0) t->cx1 = t->cx0;
    if (t->cy1 < t->cy0) t->cy1 = t->cy0;
}

/* un ritaglio dentro l'altro, da rimettere poi com'era */
typedef struct { int a, b, c, d; } Rit;
static Rit rit_stringi(ExTela *t, int x, int y, int w, int h)
{
    Rit v = { t->cx0, t->cy0, t->cx1, t->cy1 };

    t->cx0 = massimo(t->cx0, x);      t->cy0 = massimo(t->cy0, y);
    t->cx1 = minimo(t->cx1, x + w);   t->cy1 = minimo(t->cy1, y + h);
    if (t->cx1 < t->cx0) t->cx1 = t->cx0;
    if (t->cy1 < t->cy0) t->cy1 = t->cy0;
    return v;
}
static void rit_rimetti(ExTela *t, Rit v) { t->cx0 = v.a; t->cy0 = v.b; t->cx1 = v.c; t->cy1 = v.d; }

/* -----------------------------------------------------------------------------
 * Colori
 * --------------------------------------------------------------------------- */
unsigned int ext_mescola(unsigned int a, unsigned int b, int alfa)
{
    int ra = (a >> 16) & 255, ga = (a >> 8) & 255, ba = a & 255;
    int rb = (b >> 16) & 255, gb = (b >> 8) & 255, bb = b & 255;

    if (alfa <= 0) return a & 0xFFFFFFu;
    if (alfa >= 255) return b & 0xFFFFFFu;
    ra += (rb - ra) * alfa / 255;
    ga += (gb - ga) * alfa / 255;
    ba += (bb - ba) * alfa / 255;
    return ((unsigned int)ra << 16) | ((unsigned int)ga << 8) | (unsigned int)ba;
}

unsigned int ext_schiarisci(unsigned int c, int q)
{
    return q >= 0 ? ext_mescola(c, 0xFFFFFFu, q) : ext_mescola(c, 0x000000u, -q);
}

void ext_punto(ExTela *t, int x, int y, unsigned int c, int alfa)
{
    unsigned int *p;

    if (alfa <= 0) return;
    if (x < t->cx0 || x >= t->cx1 || y < t->cy0 || y >= t->cy1) return;
    p = t->pix + y * t->passo + x;
    *p = alfa >= 255 ? (c & 0xFFFFFFu) : ext_mescola(*p, c, alfa);
}

void ext_riempi(ExTela *t, int x, int y, int w, int h, unsigned int c)
{
    int x0 = massimo(x, t->cx0), y0 = massimo(y, t->cy0);
    int x1 = minimo(x + w, t->cx1), y1 = minimo(y + h, t->cy1), i, j;

    for (j = y0; j < y1; j++) {
        unsigned int *r = t->pix + j * t->passo;
        for (i = x0; i < x1; i++) r[i] = c;
    }
}

void ext_riempi_alfa(ExTela *t, int x, int y, int w, int h, unsigned int c, int alfa)
{
    int i, j;

    if (alfa >= 255) { ext_riempi(t, x, y, w, h, c); return; }
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) ext_punto(t, i, j, c, alfa);
}

void ext_contorno(ExTela *t, int x, int y, int w, int h, unsigned int c)
{
    ext_riempi(t, x, y, w, 1, c);
    ext_riempi(t, x, y + h - 1, w, 1, c);
    ext_riempi(t, x, y, 1, h, c);
    ext_riempi(t, x + w - 1, y, 1, h, c);
}

void ext_sfuma_v(ExTela *t, int x, int y, int w, int h, unsigned int a, unsigned int b)
{
    int j;

    for (j = 0; j < h; j++)
        ext_riempi(t, x, y + j, w, 1, ext_mescola(a, b, h > 1 ? j * 255 / (h - 1) : 0));
}

void ext_sfuma_o(ExTela *t, int x, int y, int w, int h, unsigned int a, unsigned int b)
{
    int i;

    for (i = 0; i < w; i++)
        ext_riempi(t, x + i, y, 1, h, ext_mescola(a, b, w > 1 ? i * 255 / (w - 1) : 0));
}

/* la luce vince negli angoli: prima l'ombra, poi la luce (come bordo3d) */
void ext_rilievo(ExTela *t, int x, int y, int w, int h, unsigned int luce, unsigned int ombra)
{
    if (w <= 0 || h <= 0) return;
    ext_riempi(t, x, y + h - 1, w, 1, ombra);
    ext_riempi(t, x + w - 1, y, 1, h, ombra);
    ext_riempi(t, x, y, w, 1, luce);
    ext_riempi(t, x, y, 1, h, luce);
}

/* -----------------------------------------------------------------------------
 * Rettangoli arrotondati: quanta parte del pixel (px, py) sta dentro il
 * rettangolo w x h con angoli di raggio r, in sedicesimi (4x4 campioni).
 * Coordinate in ottavi di pixel: il campione k sta a 2k+1 ottavi.
 * --------------------------------------------------------------------------- */
static int copertura(int w, int h, int r, int px, int py)
{
    int sx, sy, n = 0, r8, cx, cy;

    if (px < 0 || py < 0 || px >= w || py >= h) return 0;
    if (r <= 0) return 16;
    if ((px >= r && px < w - r) || (py >= r && py < h - r)) return 16;
    r8 = r * 8;
    cx = (px < r) ? r8 : (w - r) * 8;
    cy = (py < r) ? r8 : (h - r) * 8;
    for (sy = 0; sy < 4; sy++)
        for (sx = 0; sx < 4; sx++) {
            int X = px * 8 + sx * 2 + 1, Y = py * 8 + sy * 2 + 1;
            int dx = X - cx, dy = Y - cy;

            if ((px < r && X > cx) || (px >= w - r && X < cx)) dx = 0;
            if ((py < r && Y > cy) || (py >= h - r && Y < cy)) dy = 0;
            if (dx * dx + dy * dy <= r8 * r8) n++;
        }
    return n;
}

static int r_valido(int w, int h, int r)
{
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    return r;
}

void ext_tondo_pieno(ExTela *t, int x, int y, int w, int h, int r,
                     unsigned int a, unsigned int b)
{
    int i, j;

    r = r_valido(w, h, r);
    for (j = 0; j < h; j++) {
        unsigned int c = ext_mescola(a, b, h > 1 ? j * 255 / (h - 1) : 0);

        if (r == 0 || (j >= r && j < h - r)) {
            ext_riempi(t, x, y + j, w, 1, c);
            continue;
        }
        for (i = 0; i < w; i++) {
            int k = copertura(w, h, r, i, j);
            if (k) ext_punto(t, x + i, y + j, c, k == 16 ? 255 : k * 255 / 16);
        }
    }
}

void ext_tondo_alfa(ExTela *t, int x, int y, int w, int h, int r, unsigned int c, int alfa)
{
    int i, j;

    r = r_valido(w, h, r);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            int k = copertura(w, h, r, i, j);
            if (k) ext_punto(t, x + i, y + j, c, alfa * k / 16);
        }
}

void ext_tondo_bordo(ExTela *t, int x, int y, int w, int h, int r, int sp, unsigned int c)
{
    int i, j, ri;

    if (sp <= 0 || w <= 0 || h <= 0) return;
    r = r_valido(w, h, r);
    ri = r - sp; if (ri < 0) ri = 0;
    for (j = 0; j < h; j++) {
        int dentro_y = (j >= sp && j < h - sp);

        for (i = 0; i < w; i++) {
            int k, ki = 0;

            /* il centro pieno di righe lontane dagli angoli: si salta */
            if (dentro_y && i >= sp && i < w - sp &&
                ((j >= r && j < h - r) || (i >= r && i < w - r))) {
                i = w - sp - 1;
                continue;
            }
            k = copertura(w, h, r, i, j);
            if (dentro_y && i >= sp && i < w - sp)
                ki = copertura(w - 2 * sp, h - 2 * sp, ri, i - sp, j - sp);
            k -= ki;
            if (k > 0) ext_punto(t, x + i, y + j, c, k == 16 ? 255 : k * 255 / 16);
        }
    }
}

/* Ombra morbida: alfa che cala col quadrato della distanza dal rettangolo
 * arrotondato (spostato di dy in basso), fino a s pixel. L'interno non si
 * tocca: lo coprira' cio' che fa ombra. */
void ext_ombra(ExTela *t, int x, int y, int w, int h, int r, int s,
               int alfa, unsigned int c, int dy)
{
    int i, j;

    if (s <= 0 || alfa <= 0) return;
    r = r_valido(w, h, r);
    y += dy;
    for (j = y - s; j < y + h + s; j++)
        for (i = x - s; i < x + w + s; i++) {
            int qx = 0, qy = 0, d;

            if (i < x + r) qx = x + r - i; else if (i > x + w - 1 - r) qx = i - (x + w - 1 - r);
            if (j < y + r) qy = y + r - j; else if (j > y + h - 1 - r) qy = j - (y + h - 1 - r);
            d = (int)radq((unsigned int)(qx * qx + qy * qy)) - r;
            if (d <= 0) {
                /* dentro: si disegna solo la parte scoperta dallo spostamento */
                if (j < y - dy + h && j >= y - dy && i >= x && i < x + w) continue;
                d = 0;
            }
            if (d >= s) continue;
            ext_punto(t, i, j, c, alfa * (s - d) * (s - d) / (s * s));
        }
}

/* cerchio pieno con centro nel MEZZO del pixel (cx, cy) */
static int cop_cerchio(int px, int py, int cx, int cy, int r)
{
    int sx, sy, n = 0, r8 = r * 8;

    for (sy = 0; sy < 4; sy++)
        for (sx = 0; sx < 4; sx++) {
            int dx = px * 8 + sx * 2 + 1 - (cx * 8 + 4);
            int dy = py * 8 + sy * 2 + 1 - (cy * 8 + 4);
            if (dx * dx + dy * dy <= r8 * r8) n++;
        }
    return n;
}

void ext_cerchio(ExTela *t, int cx, int cy, int r, unsigned int c)
{
    int i, j;

    for (j = cy - r - 1; j <= cy + r + 1; j++)
        for (i = cx - r - 1; i <= cx + r + 1; i++) {
            int k = cop_cerchio(i, j, cx, cy, r);
            if (k) ext_punto(t, i, j, c, k == 16 ? 255 : k * 255 / 16);
        }
}

void ext_anello(ExTela *t, int cx, int cy, int r, int sp, unsigned int c)
{
    int i, j;

    for (j = cy - r - 1; j <= cy + r + 1; j++)
        for (i = cx - r - 1; i <= cx + r + 1; i++) {
            int k = cop_cerchio(i, j, cx, cy, r) - (r > sp ? cop_cerchio(i, j, cx, cy, r - sp) : 0);
            if (k > 0) ext_punto(t, i, j, c, k == 16 ? 255 : k * 255 / 16);
        }
}

/* Segmento spesso sp con estremi tondi, antialias 4x4. Il prodotto
 * vettoriale al quadrato esce dai 32 bit su segmenti lunghi: long long, che
 * a 32 bit e' solo moltiplicazione e confronto (niente __divdi3). */
void ext_linea_alfa(ExTela *t, int x0, int y0, int x1, int y1, int sp, unsigned int c, int alfa)
{
    int bx0 = minimo(x0, x1) - sp, bx1 = massimo(x0, x1) + sp;
    int by0 = minimo(y0, y1) - sp, by1 = massimo(y0, y1) + sp;
    int ax = x0 * 8 + 4, ay = y0 * 8 + 4, vx = (x1 - x0) * 8, vy = (y1 - y0) * 8;
    long long L = (long long)vx * vx + (long long)vy * vy;
    long long hw = (long long)sp * 4;           /* mezza larghezza in ottavi */
    int i, j, sx, sy;

    for (j = by0; j <= by1; j++)
        for (i = bx0; i <= bx1; i++) {
            int n = 0;

            for (sy = 0; sy < 4; sy++)
                for (sx = 0; sx < 4; sx++) {
                    long long wx = i * 8 + sx * 2 + 1 - ax, wy = j * 8 + sy * 2 + 1 - ay;
                    long long dot = wx * vx + wy * vy, d2;

                    if (L == 0 || dot <= 0) d2 = (wx * wx + wy * wy) * (L ? L : 1);
                    else if (dot >= L) {
                        long long ex = wx - vx, ey = wy - vy;
                        d2 = (ex * ex + ey * ey) * L;
                    } else {
                        long long cr = wx * vy - wy * vx;
                        d2 = cr * cr;
                    }
                    if (d2 <= hw * hw * (L ? L : 1)) n++;
                }
            if (n) ext_punto(t, i, j, c, alfa * n / 16);
        }
}

void ext_linea(ExTela *t, int x0, int y0, int x1, int y1, int sp, unsigned int c)
{
    ext_linea_alfa(t, x0, y0, x1, y1, sp, c, 255);
}

/* esagono a punte laterali, schiacciato in prospettiva: ry/rx = quanto */
void ext_esagono(ExTela *t, int cx, int cy, int rx, int ry, int sp, unsigned int c, int alfa)
{
    int k;

    for (k = 0; k < 6; k++) {
        int a = k * 60, b = a + 60;
        ext_linea_alfa(t, cx + rx * ext_coseno(a) / 1000, cy + ry * ext_seno(a) / 1000,
                       cx + rx * ext_coseno(b) / 1000, cy + ry * ext_seno(b) / 1000, sp, c, alfa);
    }
}

void ext_esagono_pieno(ExTela *t, int cx, int cy, int rx, int ry, unsigned int c, int alfa)
{
    int j;

    /* righe orizzontali: la larghezza cala linearmente dalla meta' in su e in giu' */
    for (j = -ry * 866 / 1000; j <= ry * 866 / 1000; j++) {
        int aj = j < 0 ? -j : j, mezza = rx - (ry ? aj * rx / (2 * ry * 866 / 1000 + 1) : 0);
        ext_riempi_alfa(t, cx - mezza, cy + j, 2 * mezza, 1, c, alfa);
    }
}

/* ellisse a tratti di 4 gradi; da..a in gradi, per disegnarne un arco */
void ext_ellisse(ExTela *t, int cx, int cy, int rx, int ry, int da, int a, int sp,
                 unsigned int c, int alfa)
{
    int g;

    for (g = da; g < a; g += 4)
        ext_linea_alfa(t, cx + rx * ext_coseno(g) / 1000, cy + ry * ext_seno(g) / 1000,
                       cx + rx * ext_coseno(g + 4) / 1000, cy + ry * ext_seno(g + 4) / 1000,
                       sp, c, alfa);
}

/* la scintilla del logo: una croce che sfuma verso le punte e un punto pieno */
void ext_scintilla(ExTela *t, int cx, int cy, int r, unsigned int c)
{
    int k;

    for (k = 1; k <= r; k++) {
        int a = 255 * (r - k + 1) / (r + 1);
        ext_punto(t, cx + k, cy, c, a); ext_punto(t, cx - k, cy, c, a);
        ext_punto(t, cx, cy + k, c, a); ext_punto(t, cx, cy - k, c, a);
    }
    ext_ombra(t, cx, cy, 1, 1, 0, r > 2 ? r / 2 + 1 : 2, 120, c, 0);
    ext_cerchio(t, cx, cy, 1, c);
}

void ext_triangolo(ExTela *t, int cx, int cy, int lato, int verso, unsigned int c)
{
    int k, mezzo = lato / 2;

    for (k = 0; k <= mezzo; k++) {
        int l = lato - 2 * k;
        if (l <= 0) l = 1;
        switch (verso) {
        case 0: ext_riempi(t, cx - l / 2, cy - mezzo / 2 + k, l, 1, c); break;   /* giu' */
        case 1: ext_riempi(t, cx - l / 2, cy + mezzo / 2 - k, l, 1, c); break;   /* su   */
        case 2: ext_riempi(t, cx + mezzo / 2 - k, cy - l / 2, 1, l, c); break;   /* sx   */
        default: ext_riempi(t, cx - mezzo / 2 + k, cy - l / 2, 1, l, c); break;  /* dx   */
        }
    }
}

/* -----------------------------------------------------------------------------
 * Testo: il font di sistema 8x16, bit piu' alto = colonna di sinistra
 * --------------------------------------------------------------------------- */
int ext_larg(const char *s) { return lung(s) * 8; }

void ext_testo(ExTela *t, int x, int y, const char *s, unsigned int c)
{
    int k, r, b;

    for (k = 0; s && s[k]; k++, x += 8) {
        const unsigned char *g = font8x16 + (unsigned char)s[k] * 16;

        if (x >= t->cx1) break;
        if (x + 8 <= t->cx0) continue;
        for (r = 0; r < 16; r++)
            for (b = 0; b < 8; b++)
                if (g[r] & (0x80u >> b)) ext_punto(t, x + b, y + r, c, 255);
    }
}

void ext_testo_grassetto(ExTela *t, int x, int y, const char *s, unsigned int c)
{
    ext_testo(t, x, y, s, c);
    ext_testo(t, x + 1, y, s, c);
}

void ext_testo_centrato(ExTela *t, int x, int y, int w, int h, const char *s,
                        unsigned int c, int grassetto)
{
    int tx = x + (w - ext_larg(s) - (grassetto ? 1 : 0)) / 2, ty = y + (h - 16) / 2;
    Rit v = rit_stringi(t, x, y, w, h);

    if (grassetto) ext_testo_grassetto(t, tx, ty, s, c);
    else           ext_testo(t, tx, ty, s, c);
    rit_rimetti(t, v);
}

/* il testo dei pulsanti in maiuscolo, se il tema lo chiede (buffer piccolo:
 * un pulsante non porta frasi) */
static const char *forse_maiuscolo(const ExTema *m, const char *s, char *buf, int max)
{
    int i;

    if (!m->f.maiuscolo || !s) return s;
    for (i = 0; s[i] && i < max - 1; i++)
        buf[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
    buf[i] = 0;
    return buf;
}

/* Il riflesso del vetro: bianco che sfuma dall'alto fino a meta' altezza,
 * una riga per volta col ritaglio, cosi' gli angoli restano quelli del
 * rettangolo arrotondato intero. Una fascia di alfa costante si leggeva come
 * un pulsante di due colori, non come una superficie lucida. */
static void riflesso(ExTela *t, int x, int y, int w, int h, int r, int alfa)
{
    int j, meta = h / 2;

    for (j = 0; j < meta; j++) {
        Rit v = rit_stringi(t, x, y + j, w, 1);
        ext_tondo_alfa(t, x, y, w, h, r, 0xFFFFFFu, alfa * (meta - j) / meta);
        rit_rimetti(t, v);
    }
}

/* luminanza percepita 0..255 (pesi 77/150/29 su 256) */
static int luma(unsigned int c)
{
    return (int)((((c >> 16) & 255u) * 77u + ((c >> 8) & 255u) * 150u + (c & 255u) * 29u) >> 8);
}

/* il testo deve leggersi sul fondo: se i due sono troppo vicini, l'alternativa */
static unsigned int leggibile(unsigned int testo, unsigned int fondo, unsigned int alternativa)
{
    int d = luma(testo) - luma(fondo);
    return (d < 0 ? -d : d) < 90 ? alternativa : testo;
}

/* -----------------------------------------------------------------------------
 * Pezzi comuni ai controlli
 * --------------------------------------------------------------------------- */
static void bordo_3d(ExTela *t, const ExTema *m, int x, int y, int w, int h, int incavato)
{
    if (incavato) {
        ext_rilievo(t, x, y, w, h, m->c.ombra2, m->c.luce);
        ext_rilievo(t, x + 1, y + 1, w - 2, h - 2, m->c.ombra, m->c.luce2);
    } else {
        ext_rilievo(t, x, y, w, h, m->c.luce, m->c.ombra);
        ext_rilievo(t, x + 1, y + 1, w - 2, h - 2, m->c.luce2, m->c.ombra2);
    }
}

static void anello_fuoco(ExTela *t, const ExTema *m, int x, int y, int w, int h, int r)
{
    if (m->f.bagliore > 0)
        ext_ombra(t, x, y, w, h, r, m->f.bagliore, 110, m->c.fuoco, 0);
    switch (m->f.fuoco_stile) {
    case EXT_FUOCO_ANELLO:
        ext_tondo_bordo(t, x - 3, y - 3, w + 6, h + 6, r + 3, 2, m->c.fuoco);
        break;
    case EXT_FUOCO_PUNTINI: {
        int i;
        for (i = x + 3; i < x + w - 3; i += 2) {
            ext_punto(t, i, y + 3, m->c.fuoco, 255);
            ext_punto(t, i, y + h - 4, m->c.fuoco, 255);
        }
        for (i = y + 3; i < y + h - 3; i += 2) {
            ext_punto(t, x + 3, i, m->c.fuoco, 255);
            ext_punto(t, x + w - 4, i, m->c.fuoco, 255);
        }
        break;
    }
    default: break;         /* EXT_FUOCO_BORDO: lo fa chi disegna il bordo */
    }
}

/* lo spessore del contorno dei controlli: quello del tema, ma non oltre 2
 * (lo spessore del TELAIO puo' essere maggiore: vedi spessore_telaio) */
static int sp_ctrl(const ExTema *m)
{
    return m->f.spessore <= 0 ? 1 : m->f.spessore > 2 ? 2 : m->f.spessore;
}

/* il campo (casella, lista, combo): fondo e bordo, piatto o incavato */
static void campo(ExTela *t, const ExTema *m, int x, int y, int w, int h, unsigned int st)
{
    unsigned int fondo = (st & EXT_SPENTO) ? m->c.campo_spento : m->c.campo;
    int r = m->f.raggio;

    if (m->f.bordo3d) {
        ext_riempi(t, x, y, w, h, fondo);
        bordo_3d(t, m, x, y, w, h, 1);
        return;
    }
    ext_tondo_pieno(t, x, y, w, h, r, fondo, fondo);
    ext_tondo_bordo(t, x, y, w, h, r, sp_ctrl(m),
                    ((st & EXT_FUOCO) && m->f.fuoco_stile == EXT_FUOCO_BORDO) ? m->c.fuoco :
                    (st & EXT_SOPRA) ? m->c.bordo_sopra : m->c.bordo);
}

/* -----------------------------------------------------------------------------
 * La scrivania
 * --------------------------------------------------------------------------- */
static unsigned int rumore(unsigned int a)
{
    a ^= a >> 13; a *= 0x5bd1e995u; a ^= a >> 15;
    return a;
}

void ext_scrivania(ExTela *t, const ExTema *m, int x, int y, int w, int h)
{
    int i, j;

    if (m->sfondo) { m->sfondo(t, m, x, y, w, h); return; }
    ext_sfuma_v(t, x, y, w, h, m->c.desktop_a, m->c.desktop_b);
    switch (m->f.sfondo) {
    case EXT_SFONDO_RIGHE:
        for (j = 0; j < h; j += 4) ext_riempi_alfa(t, x, y + j, w, 1, 0x000000u, 18);
        break;
    case EXT_SFONDO_SCANSIONE:
        for (j = 0; j < h; j += 2) ext_riempi_alfa(t, x, y + j, w, 1, 0x000000u, 70);
        break;
    case EXT_SFONDO_SPAZZOLATO:
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++) {
                int n = (int)(rumore((unsigned int)j * 7919u + (unsigned int)(i >> 3) * 31u) & 31u) - 16;
                ext_punto(t, x + i, y + j, n > 0 ? 0xFFFFFFu : 0x000000u, n > 0 ? n : -n);
            }
        break;
    case EXT_SFONDO_PUNTI:
        for (j = 0; j < h; j += 12)
            for (i = 0; i < w; i += 12) ext_punto(t, x + i, y + j, m->c.desktop_b, 200);
        for (j = 0; j < h; j += 60) ext_riempi_alfa(t, x, y + j, w, 1, m->c.desktop_b, 60);
        for (i = 0; i < w; i += 60) ext_riempi_alfa(t, x + i, y, 1, h, m->c.desktop_b, 60);
        break;
    default: break;
    }
}

/* -----------------------------------------------------------------------------
 * La finestra: ombra, telaio, barra del titolo, pulsanti, client
 * --------------------------------------------------------------------------- */
static int spessore_telaio(const ExTema *m)
{
    return m->f.bordo3d ? (m->f.spessore > 2 ? m->f.spessore : 4)
                        : (sp_ctrl(m));
}

void ext_finestra_client(const ExTema *m, int x, int y, int w, int h,
                         int *cx, int *cy, int *cw, int *ch)
{
    int sp = spessore_telaio(m);

    *cx = x + sp;
    *cy = y + sp + m->f.barra_h;
    *cw = w - 2 * sp;
    *ch = h - 2 * sp - m->f.barra_h;
}

static void glifo_riduci(ExTela *t, int cx, int cy, int s, unsigned int c, int sp)
{ ext_linea(t, cx - s, cy + s / 2, cx + s, cy + s / 2, sp, c); }

static void glifo_ingrandisci(ExTela *t, int cx, int cy, int s, unsigned int c, int sp)
{
    ext_contorno(t, cx - s, cy - s, 2 * s + 1, 2 * s + 1, c);
    if (sp > 1) ext_riempi(t, cx - s, cy - s, 2 * s + 1, 2, c);
}

static void glifo_chiudi(ExTela *t, int cx, int cy, int s, unsigned int c, int sp)
{
    ext_linea(t, cx - s, cy - s, cx + s, cy + s, sp, c);
    ext_linea(t, cx + s, cy - s, cx - s, cy + s, sp, c);
}

void ext_finestra(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                  const char *titolo, unsigned int st)
{
    int sp = spessore_telaio(m), bh = m->f.barra_h, R = m->f.raggio_finestra;
    int attiva = (st & EXT_ATTIVA) != 0;
    int bx = x + sp, by = y + sp, bw = w - 2 * sp;
    int tx0, tx1;               /* lo spazio libero del titolo */
    Rit v;

    if (m->finestra) { m->finestra(t, m, x, y, w, h, titolo, st); return; }

    if (m->f.ombra_finestra > 0)
        ext_ombra(t, x, y, w, h, R, m->f.ombra_finestra,
                  attiva ? m->f.ombra_alfa : m->f.ombra_alfa / 2,
                  m->c.ombra_finestra, m->f.ombra_finestra / 3);

    /* il telaio */
    ext_tondo_pieno(t, x, y, w, h, R, m->c.telaio, m->c.telaio);
    if (m->f.bordo3d) bordo_3d(t, m, x, y, w, h, 0);
    else ext_tondo_bordo(t, x, y, w, h, R, sp,
                         attiva ? m->c.telaio : ext_mescola(m->c.telaio, m->c.fondo, 100));

    /* la barra: arrotondata sopra, dritta sotto */
    v = rit_stringi(t, bx, by, bw, bh);
    {
        unsigned int a = attiva ? m->c.barra_a : m->c.barra_ina_a;
        unsigned int b = attiva ? (m->f.sfumato ? m->c.barra_b : m->c.barra_a)
                                : (m->f.sfumato ? m->c.barra_ina_b : m->c.barra_ina_a);
        int ri = R > sp ? R - sp : 0;

        if (ri) ext_tondo_pieno(t, bx, by, bw, bh + ri * 2, ri, a, ext_mescola(a, b, 255));
        else    ext_sfuma_v(t, bx, by, bw, bh, a, b);
        if (ri) ext_sfuma_v(t, bx, by + ri, bw, bh - ri,
                            ext_mescola(a, b, ri * 255 / (bh > 1 ? bh - 1 : 1)), b);
        if (m->f.lucido) riflesso(t, bx, by, bw, bh, ri, 110);
    }
    rit_rimetti(t, v);

    /* i pulsanti */
    tx0 = bx + 6;
    tx1 = bx + bw - 6;
    {
        int lato = bh - 6, cy = by + bh / 2, i;
        unsigned int gl = attiva ? m->c.titolo : m->c.titolo_ina;

        switch (m->f.chiudi_stile) {
        case EXT_CHIUDI_TONDI: {
            unsigned int col[3] = { m->c.chiudi, m->c.riduci, m->c.ingrandisci };
            int r = (bh - 8) / 2 > 6 ? 6 : (bh - 8) / 2;

            for (i = 0; i < 3; i++) {
                int cx = bx + 10 + r + i * (2 * r + 8);
                unsigned int c = attiva ? col[i] : ext_mescola(m->c.barra_ina_a, 0x808080u, 90);

                ext_cerchio(t, cx, cy, r, c);
                ext_anello(t, cx, cy, r, 1, ext_schiarisci(c, -60));
                ext_cerchio(t, cx - r / 3, cy - r / 3, r / 3, ext_schiarisci(c, 110));
            }
            tx0 = bx + 10 + 3 * (2 * r + 8) + 4;
            break;
        }
        case EXT_CHIUDI_PIATTO: {
            int pw = bh * 3 / 2;

            for (i = 0; i < 3; i++) {
                int px = bx + bw - (3 - i) * pw, cx = px + pw / 2;
                if (i == 2 && (st & EXT_SOPRA)) {
                    ext_riempi(t, px, by, pw, bh, m->c.chiudi_sopra);
                    gl = 0xFFFFFFu;
                }
                if (i == 0) glifo_riduci(t, cx, cy - 2, 5, gl, 1);
                if (i == 1) glifo_ingrandisci(t, cx, cy, 4, gl, 1);
                if (i == 2) glifo_chiudi(t, cx, cy, 4, gl, 1);
            }
            tx1 = bx + bw - 3 * pw - 4;
            break;
        }
        case EXT_CHIUDI_SEGNI:
        case EXT_CHIUDI_QUADRO:
        default:
            for (i = 0; i < 3; i++) {
                int px = bx + bw - (3 - i) * (lato + 2) - 2, py = by + 3, cx = px + lato / 2;
                unsigned int f = (i == 2 && m->f.chiudi_stile == EXT_CHIUDI_SEGNI) ? m->c.chiudi : m->c.fondo;

                if (m->f.bordo3d) {
                    ext_riempi(t, px, py, lato, lato, f);
                    ext_rilievo(t, px, py, lato, lato, m->c.luce, m->c.ombra);
                    ext_rilievo(t, px + 1, py + 1, lato - 2, lato - 2, m->c.luce2, m->c.ombra2);
                } else {
                    ext_tondo_pieno(t, px, py, lato, lato, m->f.raggio / 2, f, ext_schiarisci(f, -20));
                    ext_tondo_bordo(t, px, py, lato, lato, m->f.raggio / 2, 1, m->c.bordo);
                }
                if (m->f.chiudi_stile == EXT_CHIUDI_SEGNI) {
                    unsigned int c = (i == 2) ? 0xFFFFFFu : m->c.testo;
                    if (i == 0) glifo_riduci(t, cx, py + lato / 2 - 2, lato / 4, c, 2);
                    if (i == 1) glifo_ingrandisci(t, cx, py + lato / 2, lato / 4, c, 2);
                    if (i == 2) glifo_chiudi(t, cx, py + lato / 2, lato / 4, c, 2);
                } else {
                    if (i == 0) ext_riempi(t, px + 4, py + lato - 6, lato - 8, 2, m->c.testo);
                    if (i == 1) {
                        ext_contorno(t, px + 4, py + 4, lato - 8, lato - 8, m->c.testo);
                        ext_riempi(t, px + 4, py + 4, lato - 8, 2, m->c.testo);
                    }
                    if (i == 2) {
                        ext_riempi(t, px + 4, py + 4, lato - 8, lato - 8, m->c.chiudi);
                        ext_rilievo(t, px + 4, py + 4, lato - 8, lato - 8, m->c.ombra, m->c.luce);
                    }
                }
            }
            tx1 = bx + bw - 3 * (lato + 2) - 6;
            break;
        }
    }

    /* il titolo, che non passa sopra ai pulsanti */
    if (titolo && tx1 > tx0) {
        unsigned int c = attiva ? m->c.titolo : m->c.titolo_ina;
        int ty = by + (bh - 16) / 2;

        v = rit_stringi(t, tx0, by, tx1 - tx0, bh);
        if (m->f.titolo_centrato) {
            /* al centro della finestra; se cosi' finisce sotto i pulsanti, il
             * piu' a destra possibile dentro lo spazio libero; mai prima di tx0 */
            int lx = bx + (bw - ext_larg(titolo)) / 2;
            if (lx + ext_larg(titolo) > tx1) lx = tx1 - ext_larg(titolo);
            if (lx < tx0) lx = tx0;
            if (m->f.titolo_grassetto) ext_testo_grassetto(t, lx, ty, titolo, c);
            else ext_testo(t, lx, ty, titolo, c);
        } else if (m->f.titolo_grassetto) ext_testo_grassetto(t, tx0, ty, titolo, c);
        else ext_testo(t, tx0, ty, titolo, c);
        rit_rimetti(t, v);
    }

    /* il client */
    {
        int cx, cy, cw, ch;

        ext_finestra_client(m, x, y, w, h, &cx, &cy, &cw, &ch);
        if (!m->f.bordo3d && R > sp) {
            int ri = R - sp;
            /* angoli bassi tondi come il telaio, alti dritti sotto la barra */
            v = rit_stringi(t, cx, cy, cw, ch);
            ext_tondo_pieno(t, cx, cy - ri, cw, ch + ri, ri, m->c.fondo, m->c.fondo);
            rit_rimetti(t, v);
            ext_riempi(t, cx, cy, cw, 1, ext_mescola(m->c.fondo, m->c.bordo, 120));
        } else {
            ext_riempi(t, cx, cy, cw, ch, m->c.fondo);
        }
    }
}

/* -----------------------------------------------------------------------------
 * Il pulsante
 * --------------------------------------------------------------------------- */
void ext_pulsante(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                  const char *testo, unsigned int st)
{
    unsigned int a, b, ct;
    int r = m->f.raggio, giu = (st & EXT_PREMUTO) != 0;
    int pieno = (st & EXT_PREDEFINITO) && !m->f.bordo3d;   /* il predefinito si colora */
    char buf[40];

    if (m->pulsante) { m->pulsante(t, m, x, y, w, h, testo, st); return; }

    if (st & EXT_SPENTO) {
        a = ext_mescola(m->c.puls_a, m->c.fondo, 140);
        b = ext_mescola(m->c.puls_b, m->c.fondo, 140);
        pieno = 0;
    } else if (giu)              { a = m->c.puls_giu_a;   b = m->c.puls_giu_b; }
    else if (st & EXT_SOPRA)     { a = m->c.puls_sopra_a; b = m->c.puls_sopra_b; }
    else                         { a = m->c.puls_a;       b = m->c.puls_b; }
    if (pieno) {
        unsigned int ac = m->c.accento;
        a = giu ? ext_schiarisci(ac, -40) : (st & EXT_SOPRA) ? ext_schiarisci(ac, 30) : ac;
        b = m->f.sfumato ? ext_schiarisci(a, -35) : a;
    }
    if (!m->f.sfumato && !pieno) b = a;

    if ((st & EXT_FUOCO) && !(st & EXT_SPENTO)) anello_fuoco(t, m, x, y, w, h, r);
    if (m->f.ombra_pulsante > 0 && !giu && !(st & EXT_SPENTO))
        ext_ombra(t, x, y, w, h, r, m->f.ombra_pulsante, 70, m->c.ombra_finestra, 1);

    if (m->f.bordo3d) {
        ext_sfuma_v(t, x, y, w, h, a, b);
        if (st & EXT_PREDEFINITO) {
            ext_contorno(t, x, y, w, h, m->c.ombra2 == m->c.ombra ? 0 : 0x000000u);
            bordo_3d(t, m, x + 1, y + 1, w - 2, h - 2, giu);
        } else {
            bordo_3d(t, m, x, y, w, h, giu);
        }
    } else {
        ext_tondo_pieno(t, x, y, w, h, r, a, b);
        if (m->f.lucido && !giu) riflesso(t, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, 150);
        ext_tondo_bordo(t, x, y, w, h, r, sp_ctrl(m),
                        pieno ? ext_schiarisci(m->c.accento, -50) :
                        ((st & EXT_FUOCO) && m->f.fuoco_stile == EXT_FUOCO_BORDO) ? m->c.fuoco :
                        (st & EXT_SOPRA) ? m->c.bordo_sopra : m->c.bordo);
    }

    ct = (st & EXT_SPENTO) ? m->c.testo_spento : pieno ? m->c.accento_testo : m->c.puls_testo;
    ct = leggibile(ct, ext_mescola(a, b, 128), m->c.accento_testo);
    testo = forse_maiuscolo(m, testo, buf, (int)sizeof(buf));
    {
        int d = (giu && m->f.bordo3d) ? 1 : 0;
        ext_testo_centrato(t, x + 2 + d, y + d, w - 4, h, testo, ct, (st & EXT_PREDEFINITO) != 0);
    }
}

/* -----------------------------------------------------------------------------
 * La casella di testo
 * --------------------------------------------------------------------------- */
void ext_casella(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                 const char *testo, int cursore, unsigned int st)
{
    Rit v;

    if ((st & EXT_FUOCO) && m->f.fuoco_stile == EXT_FUOCO_ANELLO)
        anello_fuoco(t, m, x, y, w, h, m->f.raggio);
    else if ((st & EXT_FUOCO) && m->f.bagliore)
        ext_ombra(t, x, y, w, h, m->f.raggio, m->f.bagliore, 110, m->c.fuoco, 0);
    campo(t, m, x, y, w, h, st);
    v = rit_stringi(t, x + 3, y + 2, w - 6, h - 4);
    ext_testo(t, x + 6, y + (h - 16) / 2, testo,
              (st & EXT_SPENTO) ? m->c.testo_spento : m->c.campo_testo);
    if (cursore >= 0 && (st & EXT_FUOCO))
        ext_riempi(t, x + 6 + cursore * 8, y + (h - 16) / 2, 1, 16, m->c.campo_testo);
    rit_rimetti(t, v);
    /* fuoco sottolineato per i temi piatti a bordo sottile */
    if ((st & EXT_FUOCO) && m->f.fuoco_stile == EXT_FUOCO_BORDO && !m->f.bordo3d)
        ext_riempi(t, x + m->f.raggio, y + h - 2, w - 2 * m->f.raggio, 2, m->c.fuoco);
}

/* -----------------------------------------------------------------------------
 * Spunta e radio
 * --------------------------------------------------------------------------- */
void ext_spunta(ExTela *t, const ExTema *m, int x, int y, const char *testo, unsigned int st)
{
    int l = 15, by = y;
    int sc = (st & EXT_SCELTO) != 0;
    unsigned int segno;

    if (m->f.bordo3d) {
        ext_riempi(t, x, by, l, l, (st & EXT_SPENTO) ? m->c.campo_spento : m->c.campo);
        bordo_3d(t, m, x, by, l, l, 1);
        segno = (st & EXT_SPENTO) ? m->c.testo_spento : m->c.campo_testo;
    } else {
        unsigned int f = sc ? m->c.accento : m->c.campo;
        if (st & EXT_SPENTO) f = sc ? ext_mescola(m->c.accento, m->c.fondo, 150) : m->c.campo_spento;
        ext_tondo_pieno(t, x, by, l, l, m->f.spunta_raggio, f, f);
        ext_tondo_bordo(t, x, by, l, l, m->f.spunta_raggio, 1,
                        sc ? ext_schiarisci(m->c.accento, -40) :
                        (st & EXT_SOPRA) ? m->c.bordo_sopra : m->c.bordo);
        segno = m->c.accento_testo;
    }
    if (sc) {
        ext_linea(t, x + 3, y + 7, x + 6, y + 10, 2, segno);
        ext_linea(t, x + 6, y + 10, x + 11, y + 4, 2, segno);
    }
    if (testo) ext_testo(t, x + l + 7, y + (l - 16) / 2,
                         testo, (st & EXT_SPENTO) ? m->c.testo_spento : m->c.testo);
}

void ext_radio(ExTela *t, const ExTema *m, int x, int y, const char *testo, unsigned int st)
{
    int r = 7, cx = x + r, cy = y + r;
    int sc = (st & EXT_SCELTO) != 0;

    if (m->f.bordo3d) {
        ext_cerchio(t, cx, cy, r, m->c.ombra2);
        ext_cerchio(t, cx, cy, r - 1, m->c.ombra);
        ext_cerchio(t, cx + 1, cy + 1, r - 2, m->c.luce);
        ext_cerchio(t, cx, cy, r - 2, (st & EXT_SPENTO) ? m->c.campo_spento : m->c.campo);
        if (sc) ext_cerchio(t, cx, cy, 2, m->c.campo_testo);
    } else {
        ext_cerchio(t, cx, cy, r, sc ? m->c.accento :
                    (st & EXT_SOPRA) ? m->c.bordo_sopra : m->c.bordo);
        ext_cerchio(t, cx, cy, r - 1, sc ? m->c.accento : m->c.campo);
        if (sc) ext_cerchio(t, cx, cy, 3, m->c.accento_testo);
    }
    if (testo) ext_testo(t, x + 2 * r + 8, y + r - 8,
                         testo, (st & EXT_SPENTO) ? m->c.testo_spento : m->c.testo);
}

/* -----------------------------------------------------------------------------
 * Elenco a discesa
 * --------------------------------------------------------------------------- */
void ext_combo(ExTela *t, const ExTema *m, int x, int y, int w, int h,
               const char *testo, unsigned int st)
{
    int bw = h;

    ext_casella(t, m, x, y, w, h, testo, -1, st & ~EXT_FUOCO);
    if (m->f.bordo3d) {
        int px = x + w - bw + 1, py = y + 2;
        ext_riempi(t, px, py, bw - 3, h - 4, m->c.puls_a);
        ext_rilievo(t, px, py, bw - 3, h - 4, m->c.luce, m->c.ombra);
        ext_triangolo(t, px + (bw - 3) / 2, y + h / 2, 8, 0, m->c.testo);
    } else {
        ext_riempi(t, x + w - bw, y + 4, 1, h - 8, m->c.bordo);
        ext_triangolo(t, x + w - bw / 2, y + h / 2, 8, (st & EXT_APERTO) ? 1 : 0,
                      (st & EXT_SOPRA) ? m->c.accento : m->c.testo);
    }
}

/* -----------------------------------------------------------------------------
 * Lista
 * --------------------------------------------------------------------------- */
void ext_lista(ExTela *t, const ExTema *m, int x, int y, int w, int h,
               const char *const *righe, int n, int scelta, int sopra,
               int prima, unsigned int st)
{
    int i, ry;
    Rit v;

    campo(t, m, x, y, w, h, st);
    v = rit_stringi(t, x + 2, y + 2, w - 4, h - 4);
    for (i = prima, ry = y + 3; i < n && ry < y + h - 2; i++, ry += EXT_RIGA_H) {
        unsigned int ct = m->c.campo_testo;
        int rr = m->f.raggio > 4 ? 4 : m->f.raggio;

        if (!m->f.bordo3d && (i & 1))
            ext_riempi(t, x + 2, ry, w - 4, EXT_RIGA_H, ext_mescola(m->c.campo, m->c.fondo_alt, 90));
        if (i == scelta) {
            if (m->f.bordo3d) ext_riempi(t, x + 2, ry, w - 4, EXT_RIGA_H, m->c.accento);
            else ext_tondo_pieno(t, x + 4, ry + 1, w - 8, EXT_RIGA_H - 2, rr,
                                 m->c.accento, m->f.sfumato ? ext_schiarisci(m->c.accento, -25) : m->c.accento);
            ct = m->c.accento_testo;
        } else if (i == sopra) {
            ext_tondo_pieno(t, x + 4, ry + 1, w - 8, EXT_RIGA_H - 2, rr,
                            m->c.accento_chiaro, m->c.accento_chiaro);
        }
        ext_testo(t, x + 10, ry + (EXT_RIGA_H - 16) / 2, righe[i], ct);
    }
    rit_rimetti(t, v);
}

/* -----------------------------------------------------------------------------
 * Barra di scorrimento
 * --------------------------------------------------------------------------- */
void ext_scorri(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                int verticale, int pos, int pagina, int totale, unsigned int st)
{
    int lato = verticale ? w : h, len = verticale ? h : w;
    int f = m->f.scorri_frecce ? lato : 0;
    int corsa = len - 2 * f, cl, cp;
    unsigned int cc = (st & EXT_SOPRA) ? m->c.scorri_cursore_sopra : m->c.scorri_cursore;

    if (totale <= 0) totale = 1;
    if (pagina > totale) pagina = totale;
    cl = corsa * pagina / totale;
    if (cl < 18) cl = 18;
    if (cl > corsa) cl = corsa;
    cp = (totale - pagina) > 0 ? (corsa - cl) * pos / (totale - pagina) : 0;

    if (m->f.scorri_tondo) ext_tondo_pieno(t, x, y, w, h, lato / 2, m->c.scorri_fondo, m->c.scorri_fondo);
    else ext_riempi(t, x, y, w, h, m->c.scorri_fondo);

    if (f) {
        int k;
        for (k = 0; k < 2; k++) {
            int px = verticale ? x : (k ? x + w - f : x);
            int py = verticale ? (k ? y + h - f : y) : y;

            if (m->f.bordo3d) {
                ext_riempi(t, px, py, f, f, m->c.puls_a);
                bordo_3d(t, m, px, py, f, f, 0);
            } else {
                ext_riempi(t, px, py, f, f, ext_mescola(m->c.scorri_fondo, m->c.puls_a, 128));
            }
            ext_triangolo(t, px + f / 2, py + f / 2, 7,
                          verticale ? (k ? 0 : 1) : (k ? 3 : 2), m->c.testo);
        }
    }

    {
        int px = verticale ? x : x + f + cp, py = verticale ? y + f + cp : y;
        int pw = verticale ? w : cl, ph = verticale ? cl : h;

        if (m->f.scorri_tondo) {
            int in = lato > 8 ? 3 : 1;
            ext_tondo_pieno(t, px + in, py + in, pw - 2 * in, ph - 2 * in, (lato - 2 * in) / 2, cc, cc);
        } else if (m->f.bordo3d) {
            ext_riempi(t, px, py, pw, ph, m->c.puls_a);
            bordo_3d(t, m, px, py, pw, ph, 0);
        } else {
            ext_tondo_pieno(t, px + 1, py + 1, pw - 2, ph - 2, m->f.raggio > 3 ? 3 : m->f.raggio, cc, cc);
        }
    }
}

/* -----------------------------------------------------------------------------
 * Linguette
 * --------------------------------------------------------------------------- */
void ext_linguette(ExTela *t, const ExTema *m, int x, int y, int w, int h,
                   const char *const *voci, int n, int attiva, int sopra)
{
    int i, lx = x + (m->f.linguette == EXT_LING_PILLOLA ? 4 : 2);

    if (m->f.linguette == EXT_LING_PILLOLA)
        ext_tondo_pieno(t, x, y, w, h, h / 2, m->c.fondo_alt, m->c.fondo_alt);

    for (i = 0; i < n; i++) {
        int lw = ext_larg(voci[i]) + 28;
        unsigned int ct = m->c.testo;

        switch (m->f.linguette) {
        case EXT_LING_SOTTOLINEA:
            if (i == attiva) {
                ct = m->c.accento;
                ext_riempi(t, lx + 4, y + h - 3, lw - 8, 3, m->c.accento);
            } else if (i == sopra) {
                ext_riempi(t, lx + 4, y + h - 2, lw - 8, 2, m->c.bordo_sopra);
            }
            ext_testo_centrato(t, lx, y, lw, h - 2, voci[i], ct, i == attiva);
            break;
        case EXT_LING_PILLOLA:
            if (i == attiva) {
                ext_tondo_pieno(t, lx, y + 3, lw, h - 6, (h - 6) / 2, m->c.accento,
                                m->f.sfumato ? ext_schiarisci(m->c.accento, -30) : m->c.accento);
                ct = m->c.accento_testo;
            } else if (i == sopra) {
                ext_tondo_pieno(t, lx, y + 3, lw, h - 6, (h - 6) / 2, m->c.accento_chiaro, m->c.accento_chiaro);
            }
            ext_testo_centrato(t, lx, y, lw, h, voci[i], ct, i == attiva);
            break;
        default: {                                  /* classica */
            int ly = (i == attiva) ? y : y + 3, lh = (i == attiva) ? h + 1 : h - 3;
            unsigned int f = (i == attiva) ? m->c.fondo :
                             (i == sopra) ? ext_schiarisci(m->c.fondo_alt, 20) : m->c.fondo_alt;
            int r = m->f.bordo3d ? 0 : m->f.raggio;

            {
                Rit v = rit_stringi(t, lx, ly, lw, lh);
                if (r) ext_tondo_pieno(t, lx, ly, lw, lh + r, r, f, f);
                else ext_riempi(t, lx, ly, lw, lh, f);
                if (m->f.bordo3d) {
                    ext_riempi(t, lx, ly, lw, 1, m->c.luce);
                    ext_riempi(t, lx, ly, 1, lh, m->c.luce);
                    ext_riempi(t, lx + lw - 1, ly, 1, lh, m->c.ombra);
                    ext_riempi(t, lx + lw - 2, ly + 1, 1, lh - 1, m->c.ombra2);
                } else {
                    ext_tondo_bordo(t, lx, ly, lw, lh + r + 2, r, 1, m->c.bordo);
                }
                rit_rimetti(t, v);
            }
            ext_testo_centrato(t, lx, ly, lw, lh, voci[i], ct, i == attiva);
            break;
        }
        }
        lx += lw + (m->f.linguette == EXT_LING_CLASSICA ? -1 : 2);
    }
    if (m->f.linguette == EXT_LING_CLASSICA) {
        /* la riga su cui poggiano, interrotta sotto quella attiva */
        int ax = x + 2, k;
        for (k = 0; k < attiva; k++) ax += ext_larg(voci[k]) + 28 - 1;
        ext_riempi(t, x, y + h, ax - x, 1, m->f.bordo3d ? m->c.luce : m->c.bordo);
        k = ext_larg(voci[attiva]) + 28;
        ext_riempi(t, ax + k, y + h, x + w - ax - k, 1, m->f.bordo3d ? m->c.luce : m->c.bordo);
    } else if (m->f.linguette == EXT_LING_SOTTOLINEA) {
        ext_riempi(t, x, y + h - 1, w, 1, m->c.bordo);
    }
}

/* -----------------------------------------------------------------------------
 * Riquadro, intestazione, separatore
 * --------------------------------------------------------------------------- */
void ext_riquadro(ExTela *t, const ExTema *m, int x, int y, int w, int h, const char *titolo)
{
    int ty = y + 8, tl = ext_larg(titolo);

    if (m->f.bordo3d) {
        ext_contorno(t, x, ty, w - 1, h - 9, m->c.ombra);
        ext_contorno(t, x + 1, ty + 1, w - 1, h - 9, m->c.luce);
    } else {
        ext_tondo_pieno(t, x, ty, w, h - 8, m->f.raggio, ext_mescola(m->c.fondo, m->c.fondo_alt, 120),
                        ext_mescola(m->c.fondo, m->c.fondo_alt, 120));
        ext_tondo_bordo(t, x, ty, w, h - 8, m->f.raggio, 1, m->c.bordo);
    }
    if (titolo && titolo[0]) {
        ext_riempi(t, x + 8, ty - 1, tl + 8, 3,
                   m->f.bordo3d ? m->c.fondo : m->c.fondo);
        ext_testo(t, x + 12, y, titolo, m->c.testo);
    }
}

void ext_intestazione(ExTela *t, const ExTema *m, int x, int y, int w, int h, const char *testo)
{
    ext_tondo_pieno(t, x, y, w, h, m->f.raggio, m->c.barra_a,
                    m->f.sfumato ? m->c.barra_b : m->c.barra_a);
    if (m->f.lucido) riflesso(t, x, y, w, h, m->f.raggio, 110);
    if (m->f.bordo3d) ext_rilievo(t, x, y, w, h, m->c.luce, m->c.ombra);
    ext_testo_grassetto(t, x + 8, y + (h - 16) / 2, testo, m->c.titolo);
}

void ext_separatore(ExTela *t, const ExTema *m, int x, int y, int w)
{
    if (m->f.bordo3d) {
        ext_riempi(t, x, y, w, 1, m->c.ombra);
        ext_riempi(t, x, y + 1, w, 1, m->c.luce);
    } else {
        ext_riempi(t, x, y, w, 1, m->c.bordo);
    }
}

/* -----------------------------------------------------------------------------
 * Menu
 * --------------------------------------------------------------------------- */
void ext_barra_menu(ExTela *t, const ExTema *m, int x, int y, int w,
                    const char *const *voci, int n, int aperta)
{
    int i, lx = x + 4;

    ext_riempi(t, x, y, w, EXT_MENU_H, m->c.fondo_alt);
    if (m->f.bordo3d) ext_riempi(t, x, y + EXT_MENU_H - 1, w, 1, m->c.ombra);
    else ext_riempi(t, x, y + EXT_MENU_H - 1, w, 1, m->c.bordo);
    for (i = 0; i < n; i++) {
        int lw = ext_larg(voci[i]) + 16;
        unsigned int ct = m->c.testo;

        if (i == aperta) {
            if (m->f.bordo3d) {
                ext_riempi(t, lx, y + 2, lw, EXT_MENU_H - 4, m->c.menu_sopra);
                ext_rilievo(t, lx, y + 2, lw, EXT_MENU_H - 4, m->c.ombra, m->c.luce);
            } else {
                ext_tondo_pieno(t, lx, y + 2, lw, EXT_MENU_H - 4, m->f.raggio > 4 ? 4 : m->f.raggio,
                                m->c.menu_sopra, m->c.menu_sopra);
            }
            ct = m->c.menu_sopra_testo;
        }
        ext_testo(t, lx + 8, y + (EXT_MENU_H - 16) / 2, voci[i], ct);
        lx += lw;
    }
}

void ext_tendina(ExTela *t, const ExTema *m, int x, int y, int w,
                 const char *const *voci, int n, int sopra)
{
    int i, h = 4, ry;
    int r = m->f.raggio > 6 ? 6 : m->f.raggio;

    for (i = 0; i < n; i++) h += (voci[i][0] == '-' && !voci[i][1]) ? 9 : EXT_MENU_H;
    h += 4;
    if (m->f.ombra_finestra) ext_ombra(t, x, y, w, h, r, m->f.ombra_finestra / 2 + 3,
                                       m->f.ombra_alfa, m->c.ombra_finestra, 3);
    if (m->f.bordo3d) {
        ext_riempi(t, x, y, w, h, m->c.menu_fondo);
        bordo_3d(t, m, x, y, w, h, 0);
    } else {
        ext_tondo_pieno(t, x, y, w, h, r, m->c.menu_fondo, m->c.menu_fondo);
        ext_tondo_bordo(t, x, y, w, h, r, 1, m->c.bordo);
    }
    for (i = 0, ry = y + 4; i < n; i++) {
        if (voci[i][0] == '-' && !voci[i][1]) {
            ext_separatore(t, m, x + 6, ry + 4, w - 12);
            ry += 9;
            continue;
        }
        if (i == sopra) {
            if (m->f.bordo3d) ext_riempi(t, x + 3, ry, w - 6, EXT_MENU_H, m->c.menu_sopra);
            else ext_tondo_pieno(t, x + 4, ry, w - 8, EXT_MENU_H, r > 4 ? 4 : r,
                                 m->c.menu_sopra, m->c.menu_sopra);
        }
        ext_testo(t, x + 14, ry + (EXT_MENU_H - 16) / 2, voci[i],
                  i == sopra ? m->c.menu_sopra_testo : m->c.menu_testo);
        ry += EXT_MENU_H;
    }
}

/* -----------------------------------------------------------------------------
 * Progresso e suggerimento
 * --------------------------------------------------------------------------- */
void ext_progresso(ExTela *t, const ExTema *m, int x, int y, int w, int h, int permille)
{
    int r = m->f.raggio > h / 2 ? h / 2 : m->f.raggio;
    int pw = (w - 4) * permille / 1000;
    char s[8];
    int pc = permille / 10, k = 0;

    campo(t, m, x, y, w, h, 0);
    if (pw > 0) {
        Rit v = rit_stringi(t, x + 2, y + 2, pw, h - 4);
        ext_tondo_pieno(t, x + 2, y + 2, w - 4, h - 4, r > 2 ? r - 2 : 0,
                        m->c.progresso_a, m->f.sfumato ? m->c.progresso_b : m->c.progresso_a);
        if (m->f.lucido) riflesso(t, x + 2, y + 2, w - 4, h - 4, r > 2 ? r - 2 : 0, 140);
        rit_rimetti(t, v);
    }
    if (pc >= 100) s[k++] = '1';
    if (pc >= 10) s[k++] = (char)('0' + (pc / 10) % 10);
    s[k++] = (char)('0' + pc % 10);
    s[k++] = '%'; s[k] = 0;
    /* la scritta cambia colore dove la barra piena le passa sotto: fuori il
     * testo del campo, dentro quello che si legge sul colore della barra */
    {
        Rit v = rit_stringi(t, x + 2 + pw, y, w, h);
        ext_testo_centrato(t, x, y, w, h, s, m->c.campo_testo, 1);
        rit_rimetti(t, v);
        v = rit_stringi(t, x, y, 2 + pw, h);
        ext_testo_centrato(t, x, y, w, h, s,
                           leggibile(m->c.accento_testo, m->c.progresso_a, m->c.campo_testo), 1);
        rit_rimetti(t, v);
    }
}

void ext_suggerimento(ExTela *t, const ExTema *m, int x, int y, const char *testo)
{
    int w = ext_larg(testo) + 14, h = 22, r = m->f.raggio > 5 ? 5 : m->f.raggio;

    if (m->f.ombra_finestra) ext_ombra(t, x, y, w, h, r, 6, m->f.ombra_alfa, m->c.ombra_finestra, 2);
    ext_tondo_pieno(t, x, y, w, h, r, m->c.suggerimento, m->c.suggerimento);
    ext_tondo_bordo(t, x, y, w, h, r, 1, ext_schiarisci(m->c.suggerimento, -90));
    ext_testo(t, x + 7, y + 3, testo, m->c.suggerimento_testo);
}

/* -----------------------------------------------------------------------------
 * L'elenco dei temi
 * --------------------------------------------------------------------------- */
static const ExTema *const g_temi[] = {
    &extema_classico, &extema_cristallo, &extema_ghiaccio, &extema_circuito,
    &extema_chiaro, &extema_scuro, &extema_vetro, &extema_pastello,
    &extema_ambra, &extema_metallo,
};

const ExTema *extema_elenco(int i)
{
    if (i < 0 || i >= (int)(sizeof(g_temi) / sizeof(g_temi[0]))) return 0;
    return g_temi[i];
}

const ExTema *extema_per_nome(const char *nome)
{
    int i, k;

    for (i = 0; extema_elenco(i); i++) {
        const char *a = extema_elenco(i)->nome;
        for (k = 0; a[k] && nome[k] && a[k] == nome[k]; k++) ;
        if (a[k] == 0 && nome[k] == 0) return extema_elenco(i);
    }
    return 0;
}
