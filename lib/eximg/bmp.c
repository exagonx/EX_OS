/* =============================================================================
 * lib/eximg/bmp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The BMP reader of eximg (@PAINT, 28 September 2026)
 *
 * ! THE TOOLKIT ALREADY READS BMP, AND THIS IS NOT A COPY OF IT. ex_draw_image()
 * reads the BMP by itself and puts it on the screen: it never hands the pixels
 * to anyone. A paint program needs the pixels, and eximg_carica() is the one
 * door through which an application gets them — for PNG, JPG, GIF and ICO it
 * already was. Without BMP here, Pennello would open every format except the
 * one it saves in by default.
 *
 * What it reads: the Windows headers (40, 52, 56, 108 and 124 bytes) and the
 * OS/2 one (12 bytes); 1, 4 and 8 bits with a palette, 16, 24 and 32 bits;
 * no compression, BI_BITFIELDS, RLE8 and RLE4; rows bottom-up (the usual) or
 * top-down (negative height).
 *
 * ! EVERY NUMBER COMES FROM THE FILE, SO EVERY NUMBER IS CHECKED before it is
 * used as an offset: the offset of the pixels, the palette size, the row
 * length. A BMP is the easiest format to forge, precisely because it is the
 * simplest one.
 * ============================================================================= */

#include "eximg_interno.h"

static unsigned int le16(const unsigned char *p) { return (unsigned int)p[0] | ((unsigned int)p[1] << 8); }
static unsigned int le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* Where the lowest set bit of a mask is, and how many bits it has: a
 * BI_BITFIELDS channel is «these bits», and it is scaled up to 8. */
typedef struct { unsigned int maschera, spost, bit; } Canale;

static void canale_da(Canale *c, unsigned int m)
{
    c->maschera = m;
    c->spost = 0;
    c->bit = 0;
    if (m == 0) return;
    while (!(m & 1u)) { m >>= 1; c->spost++; }
    while (m & 1u)    { m >>= 1; c->bit++; }
}

static unsigned int canale_leggi(const Canale *c, unsigned int v)
{
    unsigned int x;

    if (c->bit == 0) return 0;
    x = (v & c->maschera) >> c->spost;
    if (c->bit >= 8) return x >> (c->bit - 8);
    /* Fewer than 8 bits: the bits are repeated downwards (5 bits abcde give
     * abcdeabc), so that the largest value is 255 and not 248 — a white that
     * is not white shows as soon as it is saved again. It is what every other
     * reader does, and a 16-bit BMP must give the same pixels everywhere. */
    {
        unsigned int r = x << (8 - c->bit), s;
        for (s = c->bit; s < 8; s += c->bit) r |= r >> s;
        return r & 0xFF;
    }
}

/* RLE8 and RLE4 write into an index buffer, one byte per pixel, rows in the
 * file's order. 1 if the stream stayed inside the image. */
static int rle_decodifica(const unsigned char *d, unsigned int n, unsigned int pos,
                          unsigned char *idx, unsigned int w, unsigned int h, int quattro)
{
    unsigned int x = 0, y = 0, i;

    while (pos + 1 < n) {
        unsigned int a = d[pos], b = d[pos + 1];
        pos += 2;
        if (a > 0) {                            /* a run: a pixels of b */
            for (i = 0; i < a; i++, x++) {
                if (x >= w || y >= h) break;    /* a run past the edge: cut */
                idx[y * w + x] = (unsigned char)(quattro ? ((i & 1) ? (b & 15) : (b >> 4)) : b);
            }
            continue;
        }
        if (b == 0) { x = 0; y++; continue; }   /* end of the line */
        if (b == 1) return 1;                   /* end of the bitmap */
        if (b == 2) {                           /* a jump */
            if (pos + 1 >= n) return 0;
            x += d[pos];
            y += d[pos + 1];
            pos += 2;
            continue;
        }
        /* b pixels written as they are, padded to a 16-bit boundary */
        {
            unsigned int byte = quattro ? (b + 1) / 2 : b;
            if (pos + byte > n) return 0;
            for (i = 0; i < b; i++, x++) {
                unsigned int v = quattro ? ((i & 1) ? (d[pos + i / 2] & 15) : (d[pos + i / 2] >> 4))
                                         : d[pos + i];
                if (x < w && y < h) idx[y * w + x] = (unsigned char)v;
            }
            pos += (byte + 1) & ~1u;
        }
    }
    return 1;                                   /* ended without the marker: keep what came */
}

int eximg_bmp(const unsigned char *d, unsigned int n, EximgBitmap *bm)
{
    unsigned int off, hsz, w, bpp, compr, ncol, righe, riga, x, y;
    int          h, dal_basso;
    unsigned int pal[256];
    Canale       cr, cg, cb, ca;
    unsigned int *px;
    unsigned char *idx = 0;
    int          os2;

    if (n < 26 || d[0] != 'B' || d[1] != 'M') return 0;
    off = le32(d + 10);
    hsz = le32(d + 14);
    os2 = (hsz == 12);
    if (!os2 && hsz < 40) return 0;
    if (14 + hsz > n) return 0;

    if (os2) {
        w     = le16(d + 18);
        h     = (int)le16(d + 20);
        bpp   = le16(d + 24);
        compr = 0;
        ncol  = 0;
    } else {
        w     = le32(d + 18);
        h     = (int)le32(d + 22);
        bpp   = le16(d + 28);
        compr = le32(d + 30);
        ncol  = le32(d + 46);
    }
    dal_basso = h > 0;
    righe = (unsigned int)(h < 0 ? -h : h);
    if (w == 0 || righe == 0 || w > EXIMG_LATO_MAX || righe > EXIMG_LATO_MAX) return 0;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return 0;
    if (compr != 0 && compr != 1 && compr != 2 && compr != 3 && compr != 6) return 0;
    if (compr == 1 && bpp != 8) return 0;
    if (compr == 2 && bpp != 4) return 0;
    if ((compr == 3 || compr == 6) && bpp != 16 && bpp != 32) return 0;

    /* The masks: the defaults, then the file's if it gives them. They follow
     * a 40-byte header (BI_BITFIELDS), or they are inside a larger one. */
    if (bpp == 16) { canale_da(&cr, 0x7C00); canale_da(&cg, 0x03E0); canale_da(&cb, 0x001F); canale_da(&ca, 0); }
    else           { canale_da(&cr, 0xFF0000); canale_da(&cg, 0xFF00); canale_da(&cb, 0xFF); canale_da(&ca, 0); }
    if (compr == 3 || compr == 6) {
        unsigned int dove = 14 + 40;
        if (dove + 12 > n) return 0;
        canale_da(&cr, le32(d + dove));
        canale_da(&cg, le32(d + dove + 4));
        canale_da(&cb, le32(d + dove + 8));
        if ((compr == 6 || hsz >= 56) && dove + 16 <= n) canale_da(&ca, le32(d + dove + 12));
    }

    /* The palette: it follows the header (and the masks, if they are outside it). */
    if (bpp <= 8) {
        unsigned int dove = 14 + hsz, passo = os2 ? 3 : 4, i, max = 1u << bpp;
        if (ncol == 0 || ncol > max) ncol = max;
        for (i = 0; i < 256; i++) pal[i] = 0xFF000000u;
        for (i = 0; i < ncol; i++) {
            if (dove + i * passo + 3 > n) break;
            pal[i] = 0xFF000000u | ((unsigned int)d[dove + i * passo + 2] << 16) |
                     ((unsigned int)d[dove + i * passo + 1] << 8) | d[dove + i * passo];
        }
    }

    if (off >= n) return 0;
    px = (unsigned int *)eximg_memoria(w * righe * 4);
    if (!px) return 0;

    if (compr == 1 || compr == 2) {
        idx = (unsigned char *)eximg_memoria(w * righe);
        if (!idx) return 0;
        for (x = 0; x < w * righe; x++) idx[x] = 0;
        if (!rle_decodifica(d, n, off, idx, w, righe, compr == 2)) return 0;
        for (y = 0; y < righe; y++) {
            unsigned int *dst = px + (dal_basso ? righe - 1 - y : y) * w;
            for (x = 0; x < w; x++) dst[x] = pal[idx[y * w + x]];
        }
    } else {
        riga = ((w * bpp + 31) / 32) * 4;
        /* ! THE WHOLE IMAGE MUST BE THERE. A truncated BMP is refused, not
         * read half: the rows are bottom-up, and «half» would be the bottom
         * half with the top one black, which looks like a drawing and is not. */
        if (riga > (n - off) / righe) return 0;
        for (y = 0; y < righe; y++) {
            const unsigned char *s = d + off + y * riga;
            unsigned int *dst = px + (dal_basso ? righe - 1 - y : y) * w;
            for (x = 0; x < w; x++) {
                unsigned int v;
                switch (bpp) {
                case 1:  dst[x] = pal[(s[x >> 3] >> (7 - (x & 7))) & 1]; break;
                case 4:  dst[x] = pal[(x & 1) ? (s[x >> 1] & 15) : (s[x >> 1] >> 4)]; break;
                case 8:  dst[x] = pal[s[x]]; break;
                case 24: dst[x] = 0xFF000000u | ((unsigned int)s[x * 3 + 2] << 16) |
                                  ((unsigned int)s[x * 3 + 1] << 8) | s[x * 3];
                         break;
                default:
                    v = bpp == 16 ? le16(s + x * 2) : le32(s + x * 4);
                    dst[x] = (canale_leggi(&cr, v) << 16) | (canale_leggi(&cg, v) << 8) |
                             canale_leggi(&cb, v) |
                             /* ! NO ALPHA MASK MEANS OPAQUE: a 32-bit BI_RGB
                              * file has zeros there, and reading them as
                              * alpha would make every pixel transparent. */
                             (ca.bit ? canale_leggi(&ca, v) << 24 : 0xFF000000u);
                    break;
                }
            }
        }
    }

    bm->larghezza = w;
    bm->altezza   = righe;
    bm->px        = px;
    return 1;
}
