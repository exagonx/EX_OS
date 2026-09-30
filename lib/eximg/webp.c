/* =============================================================================
 * lib/eximg/webp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * WebP (@IMG-FORMATI, 30 September 2026): the RIFF container, the lossless
 * format (VP8L), the alpha plane (ALPH) and the first frame of an animation.
 * The lossy format (VP8) is in vp8.c.
 *
 * ! IT IS THE WEB'S FORMAT NOW: CDNs send WebP to whoever does not refuse it,
 * and Wikipedia's thumbnails, news sites and shops serve .webp by name. Until
 * today EXBrowser did not even download them.
 *
 * The specification is RFC 9649. What it calls:
 *   - a PREFIX CODE is a canonical Huffman code, bits read from the least
 *     significant end, as in DEFLATE;
 *   - TRANSFORMS are undone in the reverse order they were read: predictor
 *     (14 ways of guessing a pixel from its neighbours), colour (red and blue
 *     from green), subtract-green, colour indexing (a palette, several pixels
 *     packed in one);
 *   - the COLOUR CACHE is a small hash of recent colours, one more way of
 *     saying a pixel.
 *
 * ! THE MEMORY IS AN ARENA. A lossless image can carry hundreds of groups of
 * five prefix codes each, and eximg_memoria keeps a register of every block
 * to give them all back at the end: the arena asks it for big pieces and cuts
 * them itself, and the register stays short.
 * ============================================================================= */
#include "eximg_interno.h"
#include "webp_interno.h"

/* --------------------------------------------------------------------------
 * The arena
 * -------------------------------------------------------------------------- */
#define ARENA_PEZZO  (1024u * 1024u)

static unsigned char *g_ar;
static unsigned int   g_ar_libero;

void webp_arena_azzera(void) { g_ar = 0; g_ar_libero = 0; }

void *webp_prendi(unsigned int n)
{
    void *p;

    n = (n + 7u) & ~7u;
    if (n == 0) n = 8;
    if (n > ARENA_PEZZO / 4) {              /* big: a block of its own */
        p = eximg_memoria(n);
        if (p) { unsigned char *c = (unsigned char *)p; unsigned int i; for (i = 0; i < n; i++) c[i] = 0; }
        return p;
    }
    if (!g_ar || g_ar_libero < n) {
        g_ar = (unsigned char *)eximg_memoria(ARENA_PEZZO);
        if (!g_ar) return 0;
        g_ar_libero = ARENA_PEZZO;
        { unsigned int i; for (i = 0; i < ARENA_PEZZO; i++) g_ar[i] = 0; }
    }
    p = g_ar;
    g_ar += n;
    g_ar_libero -= n;
    return p;
}

/* --------------------------------------------------------------------------
 * The bit reader: least significant bit first
 * -------------------------------------------------------------------------- */
typedef struct {
    const unsigned char *d;
    unsigned int         n, pos;
    unsigned long long   v;
    int                  nb;
    int                  err;
} Bit;

static void bit_inizia(Bit *b, const unsigned char *d, unsigned int n)
{
    b->d = d; b->n = n; b->pos = 0; b->v = 0; b->nb = 0; b->err = 0;
}

static void riempi(Bit *b)
{
    while (b->nb <= 56) {
        unsigned long long c = 0;

        if (b->pos < b->n) c = b->d[b->pos];
        else if (b->pos > b->n + 8) b->err = 1;     /* read far past the end */
        b->pos++;
        b->v |= c << b->nb;
        b->nb += 8;
    }
}

static unsigned int leggi(Bit *b, int k)
{
    unsigned int r;

    if (k == 0) return 0;
    if (b->nb < k) riempi(b);
    r = (unsigned int)(b->v & ((1ull << k) - 1));
    b->v >>= k;
    b->nb -= k;
    return r;
}

/* --------------------------------------------------------------------------
 * Prefix codes
 *
 * ! A TABLE FOR THE SHORT CODES, A WALK FOR THE LONG ONES. Codes up to 10
 * bits (almost all the symbols read) take one lookup; a longer one is read
 * bit by bit as in DEFLATE's canonical decoding. A table for all 15 bits
 * would be 64 KB per code, and a picture has five codes per group.
 * -------------------------------------------------------------------------- */
#define HF 10

typedef struct {
    unsigned short  fast[1 << HF];      /* symbol << 4 | length; 0 = longer */
    unsigned short  cnt[16];
    unsigned short *sym;
    int             unico;              /* the only symbol, read with no bits */
} Huff;

static int huff_crea(Huff *h, const unsigned char *len, int n)
{
    unsigned short offs[16];
    int i, l, nz = 0, ultimo = 0, resto = 1;
    unsigned int code, k;

    for (i = 0; i < 16; i++) h->cnt[i] = 0;
    for (i = 0; i < (1 << HF); i++) h->fast[i] = 0;
    h->unico = -1;
    for (i = 0; i < n; i++)
        if (len[i]) { h->cnt[len[i]]++; nz++; ultimo = i; }
    if (nz == 0) return 0;
    if (nz == 1) { h->unico = ultimo; return 1; }
    for (l = 1; l < 16; l++) {
        resto = (resto << 1) - h->cnt[l];
        if (resto < 0) return 0;                    /* over-subscribed */
    }
    h->sym = (unsigned short *)webp_prendi((unsigned int)nz * 2u);
    if (!h->sym) return 0;
    offs[1] = 0;
    for (l = 1; l < 15; l++) offs[l + 1] = (unsigned short)(offs[l] + h->cnt[l]);
    for (i = 0; i < n; i++) if (len[i]) h->sym[offs[len[i]]++] = (unsigned short)i;

    code = 0; k = 0;
    for (l = 1; l < 16; l++) {
        unsigned int j;
        for (j = 0; j < h->cnt[l]; j++, code++) {
            unsigned int s = h->sym[k++];
            if (l <= HF) {
                unsigned int rev = 0, q, r;
                for (q = 0; q < (unsigned int)l; q++) rev |= ((code >> q) & 1u) << (l - 1 - q);
                for (r = rev; r < (1u << HF); r += 1u << l)
                    h->fast[r] = (unsigned short)((s << 4) | (unsigned int)l);
            }
        }
        code <<= 1;
    }
    return 1;
}

static int huff_leggi(Bit *b, const Huff *h)
{
    unsigned int e;
    int code = 0, first = 0, index = 0, l;

    if (h->unico >= 0) return h->unico;
    if (b->nb < 16) riempi(b);
    e = h->fast[b->v & ((1u << HF) - 1)];
    if (e) {
        b->v >>= (e & 15);
        b->nb -= (int)(e & 15);
        return (int)(e >> 4);
    }
    for (l = 1; l < 16; l++) {
        int count = h->cnt[l];
        code |= (int)leggi(b, 1);
        if (code < first + count) return h->sym[index + code - first];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    b->err = 1;
    return 0;
}

#define ALFABETO_MAX (256 + 24 + 2048)

static int leggi_codice(Bit *b, Huff *h, int n)
{
    static unsigned char len[ALFABETO_MAX];
    int i;

    for (i = 0; i < n; i++) len[i] = 0;
    if (leggi(b, 1)) {                              /* simple: one or two symbols */
        int ns = (int)leggi(b, 1) + 1, otto = (int)leggi(b, 1);
        int s0 = (int)leggi(b, otto ? 8 : 1);

        if (s0 >= n) return 0;
        len[s0] = 1;
        if (ns == 2) {
            int s1 = (int)leggi(b, 8);
            if (s1 >= n) return 0;
            len[s1] = 1;
        }
    } else {
        static const int ORD[19] = { 17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
        unsigned char cl[19];
        Huff hc;
        int nc = 4 + (int)leggi(b, 4), max_sym, s = 0, prima = 8;

        for (i = 0; i < 19; i++) cl[i] = 0;
        if (nc > 19) return 0;
        for (i = 0; i < nc; i++) cl[ORD[i]] = (unsigned char)leggi(b, 3);
        if (!huff_crea(&hc, cl, 19)) return 0;
        if (leggi(b, 1)) {
            int lb = 2 + 2 * (int)leggi(b, 3);
            max_sym = 2 + (int)leggi(b, lb);
            if (max_sym > n) return 0;
        } else max_sym = n;
        while (s < n) {
            int c;
            if (max_sym-- == 0) break;
            c = huff_leggi(b, &hc);
            if (b->err) return 0;
            if (c < 16) {
                len[s++] = (unsigned char)c;
                if (c) prima = c;
            } else {
                int rip, val;
                if (c == 16)      { rip = 3 + (int)leggi(b, 2); val = prima; }
                else if (c == 17) { rip = 3 + (int)leggi(b, 3); val = 0; }
                else              { rip = 11 + (int)leggi(b, 7); val = 0; }
                if (s + rip > n) return 0;
                while (rip--) len[s++] = (unsigned char)val;
            }
        }
    }
    return huff_crea(h, len, n);
}

/* --------------------------------------------------------------------------
 * The entropy-coded image
 * -------------------------------------------------------------------------- */
typedef struct { Huff h[5]; } Gruppo;

/* Where a distance code points: the 120 nearest neighbours, as (dx, dy). */
static const signed char DIST[120][2] = {
    {0,1},{1,0},{1,1},{-1,1},{0,2},{2,0},{1,2},{-1,2},{2,1},{-2,1},{2,2},{-2,2},{0,3},{3,0},{1,3},{-1,3},
    {3,1},{-3,1},{2,3},{-2,3},{3,2},{-3,2},{0,4},{4,0},{1,4},{-1,4},{4,1},{-4,1},{3,3},{-3,3},{2,4},{-2,4},
    {4,2},{-4,2},{0,5},{3,4},{-3,4},{4,3},{-4,3},{5,0},{1,5},{-1,5},{5,1},{-5,1},{2,5},{-2,5},{5,2},{-5,2},
    {4,4},{-4,4},{3,5},{-3,5},{5,3},{-5,3},{0,6},{6,0},{1,6},{-1,6},{6,1},{-6,1},{2,6},{-2,6},{6,2},{-6,2},
    {4,5},{-4,5},{5,4},{-5,4},{3,6},{-3,6},{6,3},{-6,3},{0,7},{7,0},{1,7},{-1,7},{5,5},{-5,5},{7,1},{-7,1},
    {4,6},{-4,6},{6,4},{-6,4},{2,7},{-2,7},{7,2},{-7,2},{3,7},{-3,7},{7,3},{-7,3},{5,6},{-5,6},{6,5},{-6,5},
    {8,0},{4,7},{-4,7},{7,4},{-7,4},{8,1},{8,2},{6,6},{-6,6},{8,3},{5,7},{-5,7},{7,5},{-7,5},{8,4},{6,7},
    {-6,7},{7,6},{-7,6},{8,5},{7,7},{-7,7},{8,6},{8,7}
};

static unsigned int prefisso(Bit *b, int c)
{
    int eb;
    unsigned int off;

    if (c < 4) return (unsigned int)c + 1;
    eb = (c - 2) >> 1;
    off = (2u + ((unsigned int)c & 1u)) << eb;
    return off + leggi(b, eb) + 1;
}

#define DIV_SU(a, bits) (((a) + (1u << (bits)) - 1u) >> (bits))

static unsigned int *immagine(Bit *b, unsigned int xs, unsigned int ys, int principale)
{
    unsigned int  cache_bits = 0, *cache = 0, meta_bits = 0, *meta = 0, meta_w = 0;
    unsigned int  ngruppi = 1, i, pos = 0, x = 0, y = 0, tot;
    unsigned int *px;
    Gruppo       *gr, *G;

    if (xs == 0 || ys == 0 || xs > EXIMG_LATO_MAX || ys > EXIMG_LATO_MAX) return 0;
    tot = xs * ys;
    if (leggi(b, 1)) {
        cache_bits = leggi(b, 4);
        if (cache_bits < 1 || cache_bits > 11) return 0;
        cache = (unsigned int *)webp_prendi(4u << cache_bits);
        if (!cache) return 0;
    }
    if (principale && leggi(b, 1)) {
        meta_bits = leggi(b, 3) + 2;
        meta_w = DIV_SU(xs, meta_bits);
        meta = immagine(b, meta_w, DIV_SU(ys, meta_bits), 0);
        if (!meta) return 0;
        for (i = 0; i < meta_w * DIV_SU(ys, meta_bits); i++) {
            meta[i] = (meta[i] >> 8) & 0xFFFFu;
            if (meta[i] + 1 > ngruppi) ngruppi = meta[i] + 1;
        }
    }
    gr = (Gruppo *)webp_prendi(ngruppi * (unsigned int)sizeof(Gruppo));
    if (!gr) return 0;
    for (i = 0; i < ngruppi; i++) {
        int k;
        for (k = 0; k < 5; k++) {
            int alfabeto = k == 0 ? 256 + 24 + (cache_bits ? (1 << cache_bits) : 0) :
                           k == 4 ? 40 : 256;
            if (!leggi_codice(b, &gr[i].h[k], alfabeto) || b->err) return 0;
        }
    }
    px = (unsigned int *)webp_prendi(tot * 4u);
    if (!px) return 0;

    G = gr;
    while (pos < tot) {
        int s;

        if (meta && (x & ((1u << meta_bits) - 1)) == 0)
            G = &gr[meta[(y >> meta_bits) * meta_w + (x >> meta_bits)]];
        s = huff_leggi(b, &G->h[0]);
        if (s < 256) {
            unsigned int r = (unsigned int)huff_leggi(b, &G->h[1]);
            unsigned int bl = (unsigned int)huff_leggi(b, &G->h[2]);
            unsigned int a = (unsigned int)huff_leggi(b, &G->h[3]);
            px[pos] = (a << 24) | (r << 16) | ((unsigned int)s << 8) | bl;
            if (cache) cache[(0x1e35a7bdu * px[pos]) >> (32 - cache_bits)] = px[pos];
            pos++;
            if (++x == xs) { x = 0; y++; }
        } else if (s < 280) {
            unsigned int lung = prefisso(b, s - 256), dist, k;
            int dc = huff_leggi(b, &G->h[4]);

            dist = prefisso(b, dc);
            if (dist > 120) dist -= 120;
            else {
                int d = DIST[dist - 1][0] + DIST[dist - 1][1] * (int)xs;
                dist = d < 1 ? 1u : (unsigned int)d;
            }
            if (dist > pos || lung > tot - pos) return 0;
            for (k = 0; k < lung; k++) {
                px[pos] = px[pos - dist];
                if (cache) cache[(0x1e35a7bdu * px[pos]) >> (32 - cache_bits)] = px[pos];
                pos++;
                if (++x == xs) { x = 0; y++; }
            }
            /* a copy can end in the middle of another group's block */
            if (meta && pos < tot) G = &gr[meta[(y >> meta_bits) * meta_w + (x >> meta_bits)]];
        } else {
            unsigned int k = (unsigned int)s - 280;
            if (!cache || k >= (1u << cache_bits)) return 0;
            px[pos] = cache[k];
            pos++;
            if (++x == xs) { x = 0; y++; }
        }
        if (b->err) return 0;
    }
    return px;
}

/* --------------------------------------------------------------------------
 * The transforms, undone
 * -------------------------------------------------------------------------- */
static unsigned int somma(unsigned int a, unsigned int b)
{
    return (((a & 0xFF00FF00u) + (b & 0xFF00FF00u)) & 0xFF00FF00u) |
           (((a & 0x00FF00FFu) + (b & 0x00FF00FFu)) & 0x00FF00FFu);
}

static unsigned int media2(unsigned int a, unsigned int b)
{
    return (((a ^ b) & 0xFEFEFEFEu) >> 1) + (a & b);
}

static int canale(unsigned int p, int k) { return (int)((p >> k) & 0xFF); }

static unsigned int scegli(unsigned int L, unsigned int T, unsigned int TL)
{
    int k, pl = 0, pt = 0;

    for (k = 0; k < 32; k += 8) {
        int p = canale(L, k) + canale(T, k) - canale(TL, k);
        pl += p > canale(L, k) ? p - canale(L, k) : canale(L, k) - p;
        pt += p > canale(T, k) ? p - canale(T, k) : canale(T, k) - p;
    }
    return pl < pt ? L : T;
}

static int limita(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static unsigned int piena(unsigned int a, unsigned int b, unsigned int c)
{
    unsigned int r = 0;
    int k;
    for (k = 0; k < 32; k += 8)
        r |= (unsigned int)limita(canale(a, k) + canale(b, k) - canale(c, k)) << k;
    return r;
}

static unsigned int mezza(unsigned int a, unsigned int b)
{
    unsigned int r = 0;
    int k;
    for (k = 0; k < 32; k += 8)
        r |= (unsigned int)limita(canale(a, k) + (canale(a, k) - canale(b, k)) / 2) << k;
    return r;
}

static void predittore(unsigned int *p, unsigned int w, unsigned int h,
                       const unsigned int *dat, unsigned int bits)
{
    unsigned int x, y, bw = DIV_SU(w, bits);

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned int i = y * w + x, pr;

            if (y == 0) pr = x == 0 ? 0xFF000000u : p[i - 1];
            else if (x == 0) pr = p[i - w];
            else {
                unsigned int L = p[i - 1], T = p[i - w], TL = p[i - w - 1], TR = p[i - w + 1];
                switch ((dat[(y >> bits) * bw + (x >> bits)] >> 8) & 15) {
                case 1:  pr = L; break;
                case 2:  pr = T; break;
                case 3:  pr = TR; break;
                case 4:  pr = TL; break;
                case 5:  pr = media2(media2(L, TR), T); break;
                case 6:  pr = media2(L, TL); break;
                case 7:  pr = media2(L, T); break;
                case 8:  pr = media2(TL, T); break;
                case 9:  pr = media2(T, TR); break;
                case 10: pr = media2(media2(L, TL), media2(T, TR)); break;
                case 11: pr = scegli(L, T, TL); break;
                case 12: pr = piena(L, T, TL); break;
                case 13: pr = mezza(media2(L, T), TL); break;
                default: pr = 0xFF000000u; break;
                }
            }
            p[i] = somma(p[i], pr);
        }
}

static void colore(unsigned int *p, unsigned int w, unsigned int h,
                   const unsigned int *dat, unsigned int bits)
{
    unsigned int x, y, bw = DIV_SU(w, bits);

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned int e = dat[(y >> bits) * bw + (x >> bits)], v = p[y * w + x];
            int g2r = (signed char)(e & 0xFF), g2b = (signed char)((e >> 8) & 0xFF);
            int r2b = (signed char)((e >> 16) & 0xFF);
            int g = (signed char)((v >> 8) & 0xFF);
            int r = (int)((v >> 16) & 0xFF), bl = (int)(v & 0xFF);

            r += (g2r * g) >> 5;
            bl += (g2b * g) >> 5;
            bl += (r2b * (signed char)(r & 0xFF)) >> 5;
            p[y * w + x] = (v & 0xFF00FF00u) | ((unsigned int)(r & 0xFF) << 16) | (unsigned int)(bl & 0xFF);
        }
}

static void verde(unsigned int *p, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) {
        unsigned int g = (p[i] >> 8) & 0xFF;
        p[i] = (p[i] & 0xFF00FF00u) | ((((p[i] >> 16) + g) & 0xFF) << 16) | ((p[i] + g) & 0xFF);
    }
}

typedef struct {
    int           tipo;
    unsigned int  bits, w, *dat, nc;
} Trasf;

/* A lossless image stream: transforms, then the image; `px` gets w*h ARGB
 * pixels. With `testa` the 5-byte header is there (a VP8L chunk); without it
 * (the alpha plane) the size is given. */
int webp_vp8l(const unsigned char *d, unsigned int n, int testa,
              unsigned int *w, unsigned int *h, unsigned int **out, int *alfa)
{
    Bit          b;
    Trasf        t[4];
    int          nt = 0, visti = 0, k;
    unsigned int xs, *px;

    bit_inizia(&b, d, n);
    if (testa) {
        if (n < 5 || d[0] != 0x2F) return 0;
        leggi(&b, 8);
        *w = leggi(&b, 14) + 1;
        *h = leggi(&b, 14) + 1;
        *alfa = (int)leggi(&b, 1);
        if (leggi(&b, 3) != 0) return 0;            /* version */
    }
    if (*w > EXIMG_LATO_MAX || *h > EXIMG_LATO_MAX) return 0;
    xs = *w;

    while (leggi(&b, 1)) {
        Trasf *T = &t[nt];
        T->tipo = (int)leggi(&b, 2);
        if (visti & (1 << T->tipo)) return 0;
        visti |= 1 << T->tipo;
        T->w = xs;
        if (T->tipo == 0 || T->tipo == 1) {
            T->bits = leggi(&b, 3) + 2;
            T->dat = immagine(&b, DIV_SU(xs, T->bits), DIV_SU(*h, T->bits), 0);
            if (!T->dat) return 0;
        } else if (T->tipo == 3) {
            unsigned int i;
            T->nc = leggi(&b, 8) + 1;
            T->dat = immagine(&b, T->nc, 1, 0);
            if (!T->dat) return 0;
            for (i = 1; i < T->nc; i++) T->dat[i] = somma(T->dat[i], T->dat[i - 1]);
            T->bits = T->nc <= 2 ? 3 : T->nc <= 4 ? 2 : T->nc <= 16 ? 1 : 0;
            xs = DIV_SU(xs, T->bits);
        }
        nt++;
        if (b.err) return 0;
    }
    px = immagine(&b, xs, *h, 1);
    if (!px) return 0;

    for (k = nt - 1; k >= 0; k--) {
        Trasf *T = &t[k];
        if (T->tipo == 0) predittore(px, T->w, *h, T->dat, T->bits);
        else if (T->tipo == 1) colore(px, T->w, *h, T->dat, T->bits);
        else if (T->tipo == 2) verde(px, T->w * *h);
        else {
            /* the palette: from the packed width back to the real one */
            unsigned int pw = DIV_SU(T->w, T->bits), bpp = 8u >> T->bits, x, y;
            unsigned int mask = (1u << T->bits) - 1u, im = (1u << bpp) - 1u;
            unsigned int *np = (unsigned int *)webp_prendi(T->w * *h * 4u);
            if (!np) return 0;
            for (y = 0; y < *h; y++)
                for (x = 0; x < T->w; x++) {
                    unsigned int g = (px[y * pw + (x >> T->bits)] >> 8) & 0xFF;
                    unsigned int idx = (g >> ((x & mask) * bpp)) & im;
                    np[y * T->w + x] = idx < T->nc ? T->dat[idx] : 0;
                }
            px = np;
        }
    }
    *out = px;
    return 1;
}

/* --------------------------------------------------------------------------
 * The alpha plane of a lossy image (ALPH)
 * -------------------------------------------------------------------------- */
static int alfa_leggi(const unsigned char *d, unsigned int n, unsigned int w, unsigned int h,
                      unsigned char *a)
{
    unsigned int metodo, filtro, x, y;

    if (n < 1) return 0;
    metodo = d[0] & 3;
    filtro = (d[0] >> 2) & 3;
    if (metodo == 0) {
        if (n - 1 < w * h) return 0;
        for (x = 0; x < w * h; x++) a[x] = d[1 + x];
    } else if (metodo == 1) {
        unsigned int *px, ww = w, hh = h;
        int al = 0;
        if (!webp_vp8l(d + 1, n - 1, 0, &ww, &hh, &px, &al)) return 0;
        for (x = 0; x < w * h; x++) a[x] = (unsigned char)(px[x] >> 8);
    } else return 0;

    /* The filters: each value is a difference from a guess. */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned char *p = &a[y * w + x];
            int pr;
            if (filtro == 0 || (x == 0 && y == 0)) continue;
            if (y == 0) pr = p[-1];
            else if (x == 0) pr = p[-(int)w];
            else if (filtro == 1) pr = p[-1];
            else if (filtro == 2) pr = p[-(int)w];
            else pr = limita(p[-1] + p[-(int)w] - p[-(int)w - 1]);
            *p = (unsigned char)(*p + pr);
        }
    return 1;
}

/* --------------------------------------------------------------------------
 * The container
 * -------------------------------------------------------------------------- */
static unsigned int le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16) |
           ((unsigned int)p[3] << 24);
}

static unsigned int le24(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) | ((unsigned int)p[2] << 16);
}

static int nome(const unsigned char *p, const char *s)
{
    return p[0] == s[0] && p[1] == s[1] && p[2] == s[2] && p[3] == s[3];
}

/* One frame's image from its chunks (ALPH? then VP8, or VP8L): pixels ARGB. */
static int fotogramma(const unsigned char *d, unsigned int n,
                      unsigned int *w, unsigned int *h, unsigned int **px, int *ha_alfa)
{
    const unsigned char *alph = 0;
    unsigned int alph_n = 0, pos = 0;

    while (pos + 8 <= n) {
        unsigned int l = le32(d + pos + 4);
        const unsigned char *c = d + pos + 8;

        if (l > n - pos - 8) l = n - pos - 8;
        if (nome(d + pos, "ALPH")) { alph = c; alph_n = l; }
        else if (nome(d + pos, "VP8L")) {
            return webp_vp8l(c, l, 1, w, h, px, ha_alfa);
        } else if (nome(d + pos, "VP8 ")) {
            if (!webp_vp8(c, l, w, h, px)) return 0;
            *ha_alfa = 0;
            if (alph) {
                unsigned char *a = (unsigned char *)webp_prendi(*w * *h);
                unsigned int i;
                if (a && alfa_leggi(alph, alph_n, *w, *h, a)) {
                    for (i = 0; i < *w * *h; i++)
                        (*px)[i] = ((*px)[i] & 0x00FFFFFFu) | ((unsigned int)a[i] << 24);
                    *ha_alfa = 1;
                }
            }
            return 1;
        }
        pos += 8 + l + (l & 1);
    }
    return 0;
}

int eximg_webp(const unsigned char *d, unsigned int n, EximgBitmap *bm)
{
    unsigned int  w = 0, h = 0, *px = 0, i, pos;
    int           ha_alfa = 0;

    if (n < 20 || !nome(d, "RIFF") || !nome(d + 8, "WEBP")) return 0;
    if (le32(d + 4) + 8 < n) n = le32(d + 4) + 8;
    webp_arena_azzera();

    pos = 12;
    if (nome(d + pos, "VP8X")) {
        unsigned int cw = le24(d + pos + 12) + 1, ch = le24(d + pos + 15) + 1;
        unsigned int l = le32(d + pos + 4);

        pos += 8 + l + (l & 1);
        /* ! AN ANIMATION SHOWS ITS FIRST FRAME, placed on its canvas. Moving
         * WebP is rarer than moving GIF, and a still frame says what it is. */
        while (pos + 8 <= n) {
            unsigned int cl = le32(d + pos + 4);
            if (cl > n - pos - 8) cl = n - pos - 8;
            if (nome(d + pos, "ANMF") && cl >= 16) {
                const unsigned char *f = d + pos + 8;
                unsigned int fx = le24(f) * 2, fy = le24(f + 3) * 2, x, y;
                unsigned int *tela;

                if (!fotogramma(f + 16, cl - 16, &w, &h, &px, &ha_alfa)) return 0;
                if (cw > EXIMG_LATO_MAX || ch > EXIMG_LATO_MAX) return 0;
                tela = (unsigned int *)eximg_memoria(cw * ch * 4u);
                if (!tela) return 0;
                for (i = 0; i < cw * ch; i++) tela[i] = 0;
                for (y = 0; y < h && fy + y < ch; y++)
                    for (x = 0; x < w && fx + x < cw; x++) tela[(fy + y) * cw + fx + x] = px[y * w + x];
                bm->larghezza = cw;
                bm->altezza = ch;
                bm->px = tela;
                return 1;
            }
            if (nome(d + pos, "ALPH") || nome(d + pos, "VP8 ") || nome(d + pos, "VP8L")) break;
            pos += 8 + cl + (cl & 1);
        }
    }
    if (!fotogramma(d + pos, n - pos, &w, &h, &px, &ha_alfa)) return 0;

    /* The pixels the caller keeps: a block of their own (the arena goes). */
    bm->px = (unsigned int *)eximg_memoria(w * h * 4u);
    if (!bm->px) return 0;
    /* ! OPAQUE MEANS ALPHA 255 here, and a picture with no alpha says so on
     * every pixel: "alpha 0 everywhere" is eximg's old way of saying "no
     * alpha" (png.c), and a lossless WebP without alpha carries 255s. */
    for (i = 0; i < w * h; i++) bm->px[i] = ha_alfa ? px[i] : (px[i] | 0xFF000000u);
    bm->larghezza = w;
    bm->altezza = h;
    return 1;
}
