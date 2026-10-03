/* =============================================================================
 * lib/exgioco/tela.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The picture the games draw into, and the little they need around it. See
 * exgioco.h.
 *
 * ! THE EDGES ARE SAMPLED ONLY WHERE THEY ARE: a pixel well inside a disc or
 * well outside is decided with one comparison, and only the ring of pixels
 * on the border gets its 16 samples. A 19x19 Go board is 361 stones, and on a
 * Pentium MMX sixteen samples for every pixel of every stone would be felt.
 * ============================================================================= */

#include "libc.h"
#include "exgioco.h"

#define OPACO 0xFF000000u

int tela_crea(Tela *t, int w, int h)
{
    t->px = (unsigned int *)malloc((unsigned int)w * (unsigned int)h * 4u);
    if (!t->px) { t->w = t->h = 0; return 0; }
    t->w = w;
    t->h = h;
    return 1;
}

void tela_libera(Tela *t)
{
    free(t->px);
    t->px = 0;
    t->w = t->h = 0;
}

unsigned int colore_mescola(unsigned int a, unsigned int b, int alfa)
{
    unsigned int r, g, bl;

    if (alfa <= 0) return b | OPACO;
    if (alfa >= 255) return a | OPACO;
    r  = (((a >> 16) & 255) * (unsigned int)alfa + ((b >> 16) & 255) * (unsigned int)(255 - alfa)) / 255;
    g  = (((a >> 8) & 255) * (unsigned int)alfa + ((b >> 8) & 255) * (unsigned int)(255 - alfa)) / 255;
    bl = ((a & 255) * (unsigned int)alfa + (b & 255) * (unsigned int)(255 - alfa)) / 255;
    return OPACO | (r << 16) | (g << 8) | bl;
}

unsigned int colore_scuro(unsigned int c, int quanto)
{
    return colore_mescola(0x000000, c, quanto);
}

static void punto(Tela *t, int x, int y, unsigned int c, int alfa)
{
    unsigned int *p;

    if (x < 0 || y < 0 || x >= t->w || y >= t->h || alfa <= 0) return;
    p = &t->px[y * t->w + x];
    *p = alfa >= 255 ? (c | OPACO) : colore_mescola(c, *p, alfa);
}

void tela_rett(Tela *t, int x, int y, int w, int h, unsigned int c)
{
    int i, j;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > t->w) w = t->w - x;
    if (y + h > t->h) h = t->h - y;
    c |= OPACO;
    for (j = 0; j < h; j++) {
        unsigned int *p = &t->px[(y + j) * t->w + x];
        for (i = 0; i < w; i++) p[i] = c;
    }
}

void tela_bordo(Tela *t, int x, int y, int w, int h, unsigned int c)
{
    tela_rett(t, x, y, w, 1, c);
    tela_rett(t, x, y + h - 1, w, 1, c);
    tela_rett(t, x, y, 1, h, c);
    tela_rett(t, x + w - 1, y, 1, h, c);
}

void tela_sfuma(Tela *t, int x, int y, int w, int h, unsigned int c1, unsigned int c2)
{
    int j;

    for (j = 0; j < h; j++)
        tela_rett(t, x, y + j, w, 1, colore_mescola(c2, c1, h > 1 ? j * 255 / (h - 1) : 0));
}

void tela_velo(Tela *t, int x, int y, int w, int h, unsigned int c, int alfa)
{
    int i, j;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > t->w) w = t->w - x;
    if (y + h > t->h) h = t->h - y;
    for (j = 0; j < h; j++) {
        unsigned int *p = &t->px[(y + j) * t->w + x];
        for (i = 0; i < w; i++) p[i] = colore_mescola(c, p[i], alfa);
    }
}

/* How much of the pixel (px, py) is inside the circle of centre (cx, cy) and
 * radius r, in 0..255. Coordinates in pixels, the centre may be fractional. */
static int copertura_disco(float cx, float cy, float r, int px, int py)
{
    float dx = (float)px + 0.5f - cx, dy = (float)py + 0.5f - cy;
    float d2 = dx * dx + dy * dy;
    int   i, j, n = 0;

    if (d2 <= (r - 0.75f) * (r - 0.75f) && r > 0.75f) return 255;
    if (d2 >= (r + 0.75f) * (r + 0.75f)) return 0;
    for (j = 0; j < 4; j++)
        for (i = 0; i < 4; i++) {
            float sx = (float)px + (i + 0.5f) / 4.0f - cx;
            float sy = (float)py + (j + 0.5f) / 4.0f - cy;
            if (sx * sx + sy * sy <= r * r) n++;
        }
    return n * 255 / 16;
}

void tela_tondo(Tela *t, int x, int y, int w, int h, int r, unsigned int c)
{
    int i, j;

    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    tela_rett(t, x + r, y, w - 2 * r, h, c);
    tela_rett(t, x, y + r, r, h - 2 * r, c);
    tela_rett(t, x + w - r, y + r, r, h - 2 * r, c);
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            int a = copertura_disco((float)(x + r), (float)(y + r), (float)r, x + i, y + j);
            punto(t, x + i, y + j, c, a);
            a = copertura_disco((float)(x + w - r), (float)(y + r), (float)r, x + w - r + i, y + j);
            punto(t, x + w - r + i, y + j, c, a);
            a = copertura_disco((float)(x + r), (float)(y + h - r), (float)r, x + i, y + h - r + j);
            punto(t, x + i, y + h - r + j, c, a);
            a = copertura_disco((float)(x + w - r), (float)(y + h - r), (float)r,
                                x + w - r + i, y + h - r + j);
            punto(t, x + w - r + i, y + h - r + j, c, a);
        }
}

void tela_disco(Tela *t, int cx, int cy, int r, unsigned int c, int luce)
{
    float fx = (float)cx, fy = (float)cy, fr = (float)r;
    float lx = fx - fr * 0.35f, ly = fy - fr * 0.4f;
    unsigned int chiaro = colore_mescola(0xFFFFFF, c, 60), scuro = colore_scuro(c, 90);
    int x, y;

    for (y = cy - r - 1; y <= cy + r + 1; y++)
        for (x = cx - r - 1; x <= cx + r + 1; x++) {
            int a = copertura_disco(fx, fy, fr, x, y);
            unsigned int col = c;

            if (!a) continue;
            if (luce) {
                float dx = (float)x + 0.5f - lx, dy = (float)y + 0.5f - ly;
                float d = (dx * dx + dy * dy) / (fr * fr * 2.2f);
                int   k = (int)(d * 255.0f);
                if (k > 255) k = 255;
                col = colore_mescola(scuro, chiaro, k);
            }
            punto(t, x, y, col, a);
        }
}

void tela_linea(Tela *t, int x0, int y0, int x1, int y1, unsigned int c)
{
    int dx, dy, sx, sy, e;

    if (x0 == x1) { tela_rett(t, x0, y0 < y1 ? y0 : y1, 1, (y1 > y0 ? y1 - y0 : y0 - y1) + 1, c); return; }
    if (y0 == y1) { tela_rett(t, x0 < x1 ? x0 : x1, y0, (x1 > x0 ? x1 - x0 : x0 - x1) + 1, 1, c); return; }
    dx = x1 > x0 ? x1 - x0 : x0 - x1;
    dy = y1 > y0 ? y0 - y1 : y1 - y0;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    e = dx + dy;
    for (;;) {
        punto(t, x0, y0, c, 255);
        if (x0 == x1 && y0 == y1) break;
        if (2 * e >= dy) { e += dy; x0 += sx; }
        if (2 * e <= dx) { e += dx; y0 += sy; }
    }
}

void tela_forma(Tela *t, int x, int y, int w, int h, Forma fn, unsigned int c)
{
    int px, py, i, j;

    if (w <= 0 || h <= 0) return;
    for (py = 0; py < h; py++)
        for (px = 0; px < w; px++) {
            int n = 0;
            for (j = 0; j < 4; j++)
                for (i = 0; i < 4; i++)
                    if (fn(((float)px + (i + 0.5f) / 4.0f) / (float)w,
                           ((float)py + (j + 0.5f) / 4.0f) / (float)h)) n++;
            if (n) punto(t, x + px, y + py, c, n * 255 / 16);
        }
}

void tela_copia(Tela *t, int dx, int dy, const Tela *s, int sx, int sy,
                int w, int h, int alfa)
{
    int i, j;

    if (dx < 0) { sx -= dx; w += dx; dx = 0; }
    if (dy < 0) { sy -= dy; h += dy; dy = 0; }
    if (dx + w > t->w) w = t->w - dx;
    if (dy + h > t->h) h = t->h - dy;
    if (sx + w > s->w) w = s->w - sx;
    if (sy + h > s->h) h = s->h - sy;
    for (j = 0; j < h; j++) {
        const unsigned int *a = &s->px[(sy + j) * s->w + sx];
        unsigned int       *b = &t->px[(dy + j) * t->w + dx];

        if (!alfa) { memcpy(b, a, (unsigned int)w * 4u); continue; }
        for (i = 0; i < w; i++) {
            unsigned int al = a[i] >> 24;
            if (al == 255) b[i] = a[i];
            else if (al) b[i] = colore_mescola(a[i], b[i], (int)al);
        }
    }
}

void tela_testo(Tela *t, ExFont f, int x, int y, const char *s, unsigned int c)
{
    ex_draw_text_buffer(t->px, t->w, t->h, f, x, y, s, c);
}

void tela_testo_c(Tela *t, ExFont f, int cx, int y, const char *s, unsigned int c)
{
    tela_testo(t, f, cx - ex_text_width(f, s) / 2, y, s, c);
}

void tela_mostra(ExWindow f, const Tela *t, int x, int y, int w, int h)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > t->w) w = t->w - x;
    if (y + h > t->h) h = t->h - y;
    if (w <= 0 || h <= 0) return;
    ex_pixmap(f, x, GIOCO_MENU_H + y, w, h, t->px + y * t->w + x, (unsigned int)t->w);
}

#define ANNUNCIO_MS 50u
static unsigned int g_annunciato = 0;
static int          g_in_attesa  = 0;

void gioco_annuncia(ExWindow f, int forza)
{
    unsigned int ora = uptime_ms();

    if (!forza && ora - g_annunciato < ANNUNCIO_MS) { g_in_attesa = 1; return; }
    ex_update(f);
    g_annunciato = ora;
    g_in_attesa = 0;
}

void gioco_tempo(ExWindow f)
{
    if (g_in_attesa) gioco_annuncia(f, 1);
}

/* --- Random numbers: xorshift, seeded with the clock --------------------- */
static unsigned int g_caso = 2463534242u;

void caso_semina(void)
{
    struct timespec ts;

    g_caso ^= uptime_ms() * 2654435761u;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        g_caso ^= (unsigned int)ts.tv_nsec ^ ((unsigned int)ts.tv_sec << 7);
    if (!g_caso) g_caso = 1;
}

unsigned int caso(void)
{
    g_caso ^= g_caso << 13;
    g_caso ^= g_caso >> 17;
    g_caso ^= g_caso << 5;
    return g_caso;
}

unsigned int caso_fino(unsigned int n)
{
    return n ? caso() % n : 0;
}

/* --- The profile --------------------------------------------------------- */
static const char *cfg_file(const char *nome, int crea)
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
    snprintf(f, sizeof(f), "%s/.exwin/config/%s.cfg", casa, nome);
    return f;
}

int gioco_cfg_leggi(const char *nome, const char *chiave, char *val, unsigned int max)
{
    char buf[1024], *p;
    int  fd = open(cfg_file(nome, 0), O_RDONLY, 0), n;
    unsigned int lc = (unsigned int)strlen(chiave);

    if (fd < 0) return 0;
    n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';
    for (p = buf; *p; ) {
        char *fine = strchr(p, '\n');

        if (fine) *fine = '\0';
        while (*p == ' ') p++;
        if (strncmp(p, chiave, lc) == 0 && (p[lc] == ' ' || p[lc] == '=')) {
            unsigned int k = 0;

            p += lc;
            while (*p == ' ' || *p == '=') p++;
            while (p[k] && p[k] != '\r' && k + 1 < max) { val[k] = p[k]; k++; }
            while (k > 0 && val[k - 1] == ' ') k--;
            val[k] = '\0';
            return 1;
        }
        if (!fine) break;
        p = fine + 1;
    }
    return 0;
}

int gioco_cfg_intero(const char *nome, const char *chiave, int predefinito)
{
    char v[32];

    if (!gioco_cfg_leggi(nome, chiave, v, sizeof(v)) || !v[0]) return predefinito;
    return atoi(v);
}

void gioco_cfg_scrivi(const char *nome, const char *testo)
{
    int fd = open(cfg_file(nome, 1), O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) return;         /* from the CD there is no profile to write */
    write(fd, testo, (unsigned int)strlen(testo));
    close(fd);
}

void gioco_tempo_testo(char *out, unsigned int s)
{
    if (s >= 3600) sprintf(out, "%u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
    else           sprintf(out, "%u:%02u", s / 60, s % 60);
}
