/* =============================================================================
 * lib/eximg/scrivi.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The image writers: BMP and PNG. See scrivi.h for why they live here.
 *
 * ! NO malloc, AND IT IS THE LESSON OF @DIF-GROSSI: EX-OS's free() gives
 * nothing back, and a paint program saves many times in a session. The rows
 * the PNG filter works on are static, sized for the largest image eximg
 * accepts (8192 pixels): 160 KB of BSS paid once, instead of a new block lost
 * at every save.
 * ============================================================================= */

#include "scrivi.h"
#include "deflate.h"

#define LATO_MAX    8192u               /* = EXIMG_LATO_MAX */
#define RIGA_MAX    (LATO_MAX * 4u)
#define IDAT_MAX    32768u              /* one IDAT chunk */

/* --- Small pieces -------------------------------------------------------- */
static void be32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

static void le32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)v;         p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static int dimensioni_buone(const EximgBitmap *bm)
{
    return bm && bm->px && bm->larghezza && bm->altezza &&
           bm->larghezza <= LATO_MAX && bm->altezza <= LATO_MAX;
}

int eximg_nome_png(const char *nome)
{
    unsigned int l = 0;

    if (!nome) return 0;
    while (nome[l]) l++;
    return l >= 4 && nome[l - 4] == '.' &&
           (nome[l - 3] | 32) == 'p' && (nome[l - 2] | 32) == 'n' && (nome[l - 1] | 32) == 'g';
}

/* =============================================================================
 * BMP
 * ============================================================================= */
int eximg_scrivi_bmp(const EximgBitmap *bm, EximgScrivi scrivi, void *chi)
{
    static unsigned char riga[RIGA_MAX];
    unsigned char t[54];
    unsigned int  w, h, passo, dati, x, y, i;

    if (!dimensioni_buone(bm) || !scrivi) return 0;
    w = bm->larghezza;
    h = bm->altezza;
    passo = (w * 3 + 3) & ~3u;
    dati = passo * h;

    for (i = 0; i < sizeof(t); i++) t[i] = 0;
    t[0] = 'B'; t[1] = 'M';
    le32(t + 2, 54 + dati);
    le32(t + 10, 54);
    le32(t + 14, 40);
    le32(t + 18, w);
    le32(t + 22, h);                    /* positive: bottom-up, the usual way */
    t[26] = 1;                          /* planes */
    t[28] = 24;                         /* bits */
    le32(t + 34, dati);
    le32(t + 38, 2835);                 /* 72 dpi, as everybody writes */
    le32(t + 42, 2835);
    if (!scrivi(chi, t, sizeof(t))) return 0;

    for (y = h; y-- > 0; ) {
        const unsigned int *s = bm->px + y * w;
        for (x = 0; x < w; x++) {
            riga[x * 3]     = (unsigned char)s[x];
            riga[x * 3 + 1] = (unsigned char)(s[x] >> 8);
            riga[x * 3 + 2] = (unsigned char)(s[x] >> 16);
        }
        for (x = w * 3; x < passo; x++) riga[x] = 0;
        if (!scrivi(chi, riga, passo)) return 0;
    }
    return 1;
}

/* =============================================================================
 * PNG
 * ============================================================================= */
static unsigned int g_crc_tab[256];
static int          g_crc_pronta = 0;

static unsigned int crc_agg(unsigned int c, const unsigned char *p, unsigned int n)
{
    unsigned int i;

    if (!g_crc_pronta) {
        unsigned int k, v;
        for (k = 0; k < 256; k++) {
            v = k;
            for (i = 0; i < 8; i++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            g_crc_tab[k] = v;
        }
        g_crc_pronta = 1;
    }
    for (i = 0; i < n; i++) c = g_crc_tab[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c;
}

/* A whole chunk: length, type, data, CRC of type and data. */
static int pezzo(EximgScrivi scrivi, void *chi, const char *tipo,
                 const unsigned char *dati, unsigned int n)
{
    unsigned char t[8], c[4];
    unsigned int  crc;

    be32(t, n);
    t[4] = (unsigned char)tipo[0]; t[5] = (unsigned char)tipo[1];
    t[6] = (unsigned char)tipo[2]; t[7] = (unsigned char)tipo[3];
    crc = crc_agg(0xFFFFFFFFu, t + 4, 4);
    if (n) crc = crc_agg(crc, dati, n);
    be32(c, crc ^ 0xFFFFFFFFu);
    return scrivi(chi, t, 8) && (n == 0 || scrivi(chi, dati, n)) && scrivi(chi, c, 4);
}

/* What DEFLATE produces is gathered here and goes out as IDAT chunks.
 * ! THE ZLIB WRAPPER TOO: the two header bytes and the Adler-32 at the end
 * are part of the IDAT data, not chunks of their own. */
typedef struct {
    EximgScrivi   scrivi;
    void         *chi;
    unsigned char buf[IDAT_MAX];
    unsigned int  n;
    int           guasto;
} Idat;

static Idat g_idat;

static int idat_svuota(Idat *d)
{
    if (d->n == 0) return 1;
    if (!pezzo(d->scrivi, d->chi, "IDAT", d->buf, d->n)) { d->guasto = 1; return 0; }
    d->n = 0;
    return 1;
}

static int idat_metti(void *chi, const unsigned char *p, unsigned int n)
{
    Idat *d = (Idat *)chi;

    while (n) {
        unsigned int k = IDAT_MAX - d->n;
        if (k > n) k = n;
        {
            unsigned int i;
            for (i = 0; i < k; i++) d->buf[d->n + i] = p[i];
        }
        d->n += k; p += k; n -= k;
        if (d->n == IDAT_MAX && !idat_svuota(d)) return 0;
    }
    return 1;
}

static unsigned int paeth(int a, int b, int c)
{
    int p = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) return (unsigned int)a;
    if (pb <= pc) return (unsigned int)b;
    return (unsigned int)c;
}

/* Row `cur` filtered with `tipo` into `out` (n bytes, bpp bytes a pixel);
 * returns the sum of the absolute values, read as signed bytes. */
static unsigned int filtra(int tipo, const unsigned char *cur, const unsigned char *su,
                           unsigned char *out, unsigned int n, unsigned int bpp)
{
    unsigned int i, somma = 0;

    for (i = 0; i < n; i++) {
        int a = i >= bpp ? cur[i - bpp] : 0;
        int b = su[i];
        int c = i >= bpp ? su[i - bpp] : 0;
        unsigned int v;
        switch (tipo) {
        case 0:  v = cur[i]; break;
        case 1:  v = cur[i] - a; break;
        case 2:  v = cur[i] - b; break;
        case 3:  v = cur[i] - ((a + b) >> 1); break;
        default: v = cur[i] - paeth(a, b, c); break;
        }
        out[i] = (unsigned char)v;
        somma += (out[i] < 128) ? out[i] : 256u - out[i];
    }
    return somma;
}

int eximg_scrivi_png(const EximgBitmap *bm, EximgScrivi scrivi, void *chi)
{
    static unsigned char su[RIGA_MAX], cur[RIGA_MAX];
    static unsigned char prova[RIGA_MAX + 1], meglio[RIGA_MAX + 1];
    static const unsigned char firma[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    unsigned char ihdr[13], z[4];
    unsigned int  w, h, bpp, n, x, y, i, a = 1, b = 0;
    int           alfa = 0, f;

    if (!dimensioni_buone(bm) || !scrivi) return 0;
    w = bm->larghezza;
    h = bm->altezza;
    for (i = 0; i < w * h; i++)
        if ((bm->px[i] >> 24) != 0xFF) { alfa = 1; break; }
    bpp = alfa ? 4 : 3;
    n = w * bpp;

    if (!scrivi(chi, firma, 8)) return 0;
    be32(ihdr, w);
    be32(ihdr + 4, h);
    ihdr[8]  = 8;                           /* bits per channel */
    ihdr[9]  = (unsigned char)(alfa ? 6 : 2);
    ihdr[10] = ihdr[11] = ihdr[12] = 0;     /* deflate, adaptive filters, no interlace */
    if (!pezzo(scrivi, chi, "IHDR", ihdr, 13)) return 0;

    g_idat.scrivi = scrivi;
    g_idat.chi = chi;
    g_idat.n = 0;
    g_idat.guasto = 0;
    z[0] = 0x78; z[1] = 0x9C;               /* zlib: deflate, 32K window, check bits */
    if (!idat_metti(&g_idat, z, 2)) return 0;
    if (!defl_apri(idat_metti, &g_idat)) return 0;

    for (i = 0; i < n; i++) su[i] = 0;
    for (y = 0; y < h; y++) {
        const unsigned int *s = bm->px + y * w;
        unsigned int migliore = 0xFFFFFFFFu;

        for (x = 0; x < w; x++) {
            unsigned char *p = cur + x * bpp;
            p[0] = (unsigned char)(s[x] >> 16);
            p[1] = (unsigned char)(s[x] >> 8);
            p[2] = (unsigned char)s[x];
            if (alfa) p[3] = (unsigned char)(s[x] >> 24);
        }
        /* The Adler-32 is over the data BEFORE compression, filter bytes
         * included: it is what the decoder gets back out of inflate. */
        for (f = 0; f < 5; f++) {
            unsigned int somma = filtra(f, cur, su, prova + 1, n, bpp);
            if (somma < migliore) {
                migliore = somma;
                prova[0] = (unsigned char)f;
                for (i = 0; i <= n; i++) meglio[i] = prova[i];
            }
        }
        for (i = 0; i <= n; i++) {
            a = (a + meglio[i]) % 65521u;
            b = (b + a) % 65521u;
        }
        if (!defl_dati(meglio, n + 1)) return 0;
        for (i = 0; i < n; i++) su[i] = cur[i];
    }
    if (!defl_fine() || g_idat.guasto) return 0;
    be32(z, (b << 16) | a);
    if (!idat_metti(&g_idat, z, 4) || !idat_svuota(&g_idat)) return 0;
    return pezzo(scrivi, chi, "IEND", 0, 0);
}
