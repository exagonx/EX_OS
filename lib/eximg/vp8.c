/* =============================================================================
 * lib/eximg/vp8.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * VP8 key frames: the lossy half of WebP (@IMG-FORMATI, 30 September 2026).
 * The specification is RFC 6386; the constant tables are in webp_tabelle.h.
 *
 * The frame is 16x16 macroblocks, each predicted from the pixels already
 * decoded above and to the left (four ways for the whole block, ten for each
 * 4x4 piece, four for the colour), plus a residual: DCT coefficients read
 * with a boolean arithmetic decoder whose probabilities depend on the
 * neighbours. Then a loop filter smooths the block edges.
 *
 * ! THE NUMBERING OF THE 4x4 MODES IS libwebp's, NOT THE RFC's: the
 * probability table was read out of libwebp (see webp_tabelle.h), where
 * RD and VR come before LD. The modes mean the same; only their numbers
 * differ, and the table must be indexed with the numbers it was built for.
 *
 * ! PREDICTION READS UNFILTERED PIXELS: the loop filter runs over the whole
 * frame once every macroblock is rebuilt, in the same macroblock order, and
 * gives the same result as filtering row by row behind the decoder.
 *
 * ! THE COLOUR IS DOUBLED BY REPEATING IT (each chroma sample covers 2x2
 * pixels). libwebp interpolates by default; the difference is at colour edges
 * only, and the test measures it as the JPEG test does.
 * ============================================================================= */
#include "eximg_interno.h"
#include "webp_interno.h"
#include "webp_tabelle.h"

/* --------------------------------------------------------------------------
 * The boolean decoder (RFC 6386, section 7)
 * -------------------------------------------------------------------------- */
typedef struct {
    const unsigned char *d;
    unsigned int         n, pos;
    unsigned int         valore, ampiezza;
    int                  contati;
} Bool;

static void bool_inizia(Bool *b, const unsigned char *d, unsigned int n)
{
    b->d = d; b->n = n; b->pos = 2;
    b->valore = ((n > 0 ? d[0] : 0u) << 8) | (n > 1 ? d[1] : 0u);
    b->ampiezza = 255;
    b->contati = 0;
}

static int bit(Bool *b, int prob)
{
    unsigned int split = 1 + (((b->ampiezza - 1) * (unsigned int)prob) >> 8);
    unsigned int SPLIT = split << 8;
    int r;

    if (b->valore >= SPLIT) { r = 1; b->ampiezza -= split; b->valore -= SPLIT; }
    else { r = 0; b->ampiezza = split; }
    while (b->ampiezza < 128) {
        b->valore <<= 1;
        b->ampiezza <<= 1;
        if (++b->contati == 8) {
            b->contati = 0;
            if (b->pos < b->n) b->valore |= b->d[b->pos];
            b->pos++;
        }
    }
    return r;
}

static int valore(Bool *b, int n)
{
    int v = 0;
    while (n--) v = (v << 1) | bit(b, 128);
    return v;
}

static int con_segno(Bool *b, int n)
{
    int v = valore(b, n);
    return bit(b, 128) ? -v : v;
}

/* --------------------------------------------------------------------------
 * Modes (libwebp's numbering)
 * -------------------------------------------------------------------------- */
enum { B_DC, B_TM, B_VE, B_HE, B_RD, B_VR, B_LD, B_VL, B_HD, B_HU };
#define DC_PRED B_DC
#define TM_PRED B_TM
#define V_PRED  B_VE
#define H_PRED  B_HE

static const signed char ALBERO_4[18] = {
    -B_DC, 1, -B_TM, 2, -B_VE, 3, 4, 6, -B_HE, 5, -B_RD, -B_VR, -B_LD, 7, -B_VL, 8, -B_HD, -B_HU
};

static const unsigned char ZIGZAG[16] = { 0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15 };
static const unsigned char BANDA[17] = { 0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7, 0 };
static const unsigned char CAT3[] = { 173, 148, 140, 0 };
static const unsigned char CAT4[] = { 176, 155, 140, 135, 0 };
static const unsigned char CAT5[] = { 180, 157, 141, 134, 130, 0 };
static const unsigned char CAT6[] = { 254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129, 0 };
static const unsigned char *const CAT[4] = { CAT3, CAT4, CAT5, CAT6 };

/* --------------------------------------------------------------------------
 * The frame being decoded
 * -------------------------------------------------------------------------- */
typedef struct { int y1[2], y2[2], uv[2]; } Quant;
typedef struct { int limite, interno, hev, dentro; } Filtro;

typedef struct {
    int           mbw, mbh;
    unsigned char *Y, *U, *V;               /* whole planes, macroblock aligned */
    int           ys, uvs;                  /* their strides */
    unsigned char prob[4][8][3][11];
    int           segmenti, aggiorna_mappa, assoluto;
    int           seg_q[4], seg_f[4];
    unsigned char seg_prob[3];
    int           semplice, livello, nitidezza, usa_delta, ref_delta[4], mode_delta[4];
    int           usa_salto, prob_salto;
    Quant         q[4];
    Filtro        f[4][2];
    /* per macroblock, for the loop filter */
    unsigned char *mb_seg, *mb_i4, *mb_dentro;
} Frame;

static int lim127(int v) { return v < 0 ? 0 : v > 127 ? 127 : v; }

/* --------------------------------------------------------------------------
 * Coefficients
 * -------------------------------------------------------------------------- */
static int grande(Bool *b, const unsigned char *p)
{
    int v;

    if (!bit(b, p[3])) {
        if (!bit(b, p[4])) v = 2;
        else v = 3 + bit(b, p[5]);
    } else if (!bit(b, p[6])) {
        if (!bit(b, p[7])) v = 5 + bit(b, 159);
        else { v = 7 + 2 * bit(b, 165); v += bit(b, 145); }
    } else {
        const unsigned char *t;
        int b1 = bit(b, p[8]), b0 = bit(b, p[9 + b1]), cat = 2 * b1 + b0;
        v = 0;
        for (t = CAT[cat]; *t; t++) v += v + bit(b, *t);
        v += 3 + (8 << cat);
    }
    return v;
}

/* One 4x4 block's coefficients, dequantized, in natural order into out.
 * Returns the position after the last one read (first if none). */
static int coefficienti(Bool *b, unsigned char prob[8][3][11], int ctx, const int *dq,
                        int first, short *out)
{
    const unsigned char *p = prob[BANDA[first]][ctx];
    int n = first;

    for (; n < 16; n++) {
        int v;
        if (!bit(b, p[0])) return n;
        while (!bit(b, p[1])) {
            p = prob[BANDA[++n]][0];
            if (n == 16) return 16;
        }
        if (!bit(b, p[2])) { v = 1; p = prob[BANDA[n + 1]][1]; }
        else { v = grande(b, p); p = prob[BANDA[n + 1]][2]; }
        if (bit(b, 128)) v = -v;
        out[ZIGZAG[n]] = (short)(v * dq[n > 0]);
    }
    return 16;
}

/* --------------------------------------------------------------------------
 * The inverse transforms (RFC 6386 section 14, as libwebp computes them)
 * -------------------------------------------------------------------------- */
#define MUL1(a) ((((a) * 20091) >> 16) + (a))
#define MUL2(a) (((a) * 35468) >> 16)

static unsigned char lim255(int v) { return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void idct_somma(const short *in, unsigned char *dst, int passo)
{
    int C[16], *t = C, i;

    for (i = 0; i < 4; i++, in++, t += 4) {
        int a = in[0] + in[8], b = in[0] - in[8];
        int c = MUL2(in[4]) - MUL1(in[12]), d = MUL1(in[4]) + MUL2(in[12]);
        t[0] = a + d; t[1] = b + c; t[2] = b - c; t[3] = a - d;
    }
    t = C;
    for (i = 0; i < 4; i++, t++, dst += passo) {
        int dc = t[0] + 4;
        int a = dc + t[8], b = dc - t[8];
        int c = MUL2(t[4]) - MUL1(t[12]), d = MUL1(t[4]) + MUL2(t[12]);
        dst[0] = lim255(dst[0] + ((a + d) >> 3));
        dst[1] = lim255(dst[1] + ((b + c) >> 3));
        dst[2] = lim255(dst[2] + ((b - c) >> 3));
        dst[3] = lim255(dst[3] + ((a - d) >> 3));
    }
}

static void wht(const short *in, short *out)
{
    int t[16], i;

    for (i = 0; i < 4; i++) {
        int a0 = in[0 + i] + in[12 + i], a1 = in[4 + i] + in[8 + i];
        int a2 = in[4 + i] - in[8 + i], a3 = in[0 + i] - in[12 + i];
        t[0 + i] = a0 + a1; t[8 + i] = a0 - a1; t[4 + i] = a3 + a2; t[12 + i] = a3 - a2;
    }
    for (i = 0; i < 4; i++, out += 64) {
        int dc = t[0 + i * 4] + 3;
        int a0 = dc + t[3 + i * 4], a1 = t[1 + i * 4] + t[2 + i * 4];
        int a2 = t[1 + i * 4] - t[2 + i * 4], a3 = dc - t[3 + i * 4];
        out[0] = (short)((a0 + a1) >> 3);
        out[16] = (short)((a3 + a2) >> 3);
        out[32] = (short)((a0 - a1) >> 3);
        out[48] = (short)((a3 - a2) >> 3);
    }
}

/* --------------------------------------------------------------------------
 * Prediction, into a work area with a border: row -1 above, column -1 left,
 * and for luma 4 more columns right of row -1 (the "top right" pixels).
 * -------------------------------------------------------------------------- */
#define LW 21                       /* luma work: x -1..19 */
#define CW 9                        /* chroma work: x -1..7 */
#define AVG3(a, b, c) ((unsigned char)(((a) + 2 * (b) + (c) + 2) >> 2))
#define AVG2(a, b)    ((unsigned char)(((a) + (b) + 1) >> 1))

static void pred_blocco(unsigned char *p, int passo, int lato, int modo, int ha_su, int ha_sx)
{
    int x, y, s = 0;

    switch (modo) {
    case DC_PRED:
        if (ha_su && ha_sx) {
            for (x = 0; x < lato; x++) s += p[x - passo] + p[x * passo - 1];
            s = (s + lato) >> (lato == 16 ? 5 : 4);
        } else if (ha_su) {
            for (x = 0; x < lato; x++) s += p[x - passo];
            s = (s + lato / 2) >> (lato == 16 ? 4 : 3);
        } else if (ha_sx) {
            for (x = 0; x < lato; x++) s += p[x * passo - 1];
            s = (s + lato / 2) >> (lato == 16 ? 4 : 3);
        } else s = 128;
        for (y = 0; y < lato; y++) for (x = 0; x < lato; x++) p[y * passo + x] = (unsigned char)s;
        break;
    case V_PRED:
        for (y = 0; y < lato; y++) for (x = 0; x < lato; x++) p[y * passo + x] = p[x - passo];
        break;
    case H_PRED:
        for (y = 0; y < lato; y++) for (x = 0; x < lato; x++) p[y * passo + x] = p[y * passo - 1];
        break;
    default:                                         /* TM */
        for (y = 0; y < lato; y++)
            for (x = 0; x < lato; x++)
                p[y * passo + x] = lim255(p[y * passo - 1] + p[x - passo] - p[-passo - 1]);
        break;
    }
}

static void pred4(unsigned char *p, int modo)
{
    const unsigned char *t = p - LW;
    int X = t[-1], A = t[0], B = t[1], C = t[2], D = t[3], E = t[4], F = t[5], G = t[6], H = t[7];
    int I = p[-1], J = p[LW - 1], K = p[2 * LW - 1], L = p[3 * LW - 1];
    int x, y;
#define DST(x, y) p[(x) + (y) * LW]

    switch (modo) {
    case B_DC: {
        int dc = 4;
        for (x = 0; x < 4; x++) dc += t[x] + p[x * LW - 1];
        dc >>= 3;
        for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) DST(x, y) = (unsigned char)dc;
        break;
    }
    case B_TM:
        for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) DST(x, y) = lim255(p[y * LW - 1] + t[x] - X);
        break;
    case B_VE: {
        unsigned char v[4];
        v[0] = AVG3(X, A, B); v[1] = AVG3(A, B, C); v[2] = AVG3(B, C, D); v[3] = AVG3(C, D, E);
        for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) DST(x, y) = v[x];
        break;
    }
    case B_HE: {
        unsigned char r[4];
        r[0] = AVG3(X, I, J); r[1] = AVG3(I, J, K); r[2] = AVG3(J, K, L); r[3] = AVG3(K, L, L);
        for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) DST(x, y) = r[y];
        break;
    }
    case B_RD:
        DST(0, 3) = AVG3(J, K, L);
        DST(1, 3) = DST(0, 2) = AVG3(I, J, K);
        DST(2, 3) = DST(1, 2) = DST(0, 1) = AVG3(X, I, J);
        DST(3, 3) = DST(2, 2) = DST(1, 1) = DST(0, 0) = AVG3(A, X, I);
        DST(3, 2) = DST(2, 1) = DST(1, 0) = AVG3(B, A, X);
        DST(3, 1) = DST(2, 0) = AVG3(C, B, A);
        DST(3, 0) = AVG3(D, C, B);
        break;
    case B_LD:
        DST(0, 0) = AVG3(A, B, C);
        DST(1, 0) = DST(0, 1) = AVG3(B, C, D);
        DST(2, 0) = DST(1, 1) = DST(0, 2) = AVG3(C, D, E);
        DST(3, 0) = DST(2, 1) = DST(1, 2) = DST(0, 3) = AVG3(D, E, F);
        DST(3, 1) = DST(2, 2) = DST(1, 3) = AVG3(E, F, G);
        DST(3, 2) = DST(2, 3) = AVG3(F, G, H);
        DST(3, 3) = AVG3(G, H, H);
        break;
    case B_VR:
        DST(0, 0) = DST(1, 2) = AVG2(X, A);
        DST(1, 0) = DST(2, 2) = AVG2(A, B);
        DST(2, 0) = DST(3, 2) = AVG2(B, C);
        DST(3, 0) = AVG2(C, D);
        DST(0, 3) = AVG3(K, J, I);
        DST(0, 2) = AVG3(J, I, X);
        DST(0, 1) = DST(1, 3) = AVG3(I, X, A);
        DST(1, 1) = DST(2, 3) = AVG3(X, A, B);
        DST(2, 1) = DST(3, 3) = AVG3(A, B, C);
        DST(3, 1) = AVG3(B, C, D);
        break;
    case B_VL:
        DST(0, 0) = AVG2(A, B);
        DST(1, 0) = DST(0, 2) = AVG2(B, C);
        DST(2, 0) = DST(1, 2) = AVG2(C, D);
        DST(3, 0) = DST(2, 2) = AVG2(D, E);
        DST(0, 1) = AVG3(A, B, C);
        DST(1, 1) = DST(0, 3) = AVG3(B, C, D);
        DST(2, 1) = DST(1, 3) = AVG3(C, D, E);
        DST(3, 1) = DST(2, 3) = AVG3(D, E, F);
        DST(3, 2) = AVG3(E, F, G);
        DST(3, 3) = AVG3(F, G, H);
        break;
    case B_HU:
        DST(0, 0) = AVG2(I, J);
        DST(2, 0) = DST(0, 1) = AVG2(J, K);
        DST(2, 1) = DST(0, 2) = AVG2(K, L);
        DST(1, 0) = AVG3(I, J, K);
        DST(3, 0) = DST(1, 1) = AVG3(J, K, L);
        DST(3, 1) = DST(1, 2) = AVG3(K, L, L);
        DST(3, 2) = DST(2, 2) = DST(0, 3) = DST(1, 3) = DST(2, 3) = DST(3, 3) = (unsigned char)L;
        break;
    default:                                          /* B_HD */
        DST(0, 0) = DST(2, 1) = AVG2(I, X);
        DST(0, 1) = DST(2, 2) = AVG2(J, I);
        DST(0, 2) = DST(2, 3) = AVG2(K, J);
        DST(0, 3) = AVG2(L, K);
        DST(3, 0) = AVG3(A, B, C);
        DST(2, 0) = AVG3(X, A, B);
        DST(1, 0) = DST(3, 1) = AVG3(I, X, A);
        DST(1, 1) = DST(3, 2) = AVG3(J, I, X);
        DST(1, 2) = DST(3, 3) = AVG3(K, J, I);
        DST(1, 3) = AVG3(L, K, J);
        break;
    }
#undef DST
}

/* --------------------------------------------------------------------------
 * The loop filter (RFC 6386 section 15, as libwebp computes it)
 * -------------------------------------------------------------------------- */
static int ass(int v) { return v < 0 ? -v : v; }
static int sclip1(int v) { return v < -128 ? -128 : v > 127 ? 127 : v; }
static int sclip2(int v) { return v < -16 ? -16 : v > 15 ? 15 : v; }

static void filtro2(unsigned char *p, int s)
{
    int p1 = p[-2 * s], p0 = p[-s], q0 = p[0], q1 = p[s];
    int a = 3 * (q0 - p0) + sclip1(p1 - q1);
    int a1 = sclip2((a + 4) >> 3), a2 = sclip2((a + 3) >> 3);
    p[-s] = lim255(p0 + a2);
    p[0] = lim255(q0 - a1);
}

static void filtro4(unsigned char *p, int s)
{
    int p1 = p[-2 * s], p0 = p[-s], q0 = p[0], q1 = p[s];
    int a = 3 * (q0 - p0);
    int a1 = sclip2((a + 4) >> 3), a2 = sclip2((a + 3) >> 3), a3 = (a1 + 1) >> 1;
    p[-2 * s] = lim255(p1 + a3);
    p[-s] = lim255(p0 + a2);
    p[0] = lim255(q0 - a1);
    p[s] = lim255(q1 - a3);
}

static void filtro6(unsigned char *p, int s)
{
    int p2 = p[-3 * s], p1 = p[-2 * s], p0 = p[-s], q0 = p[0], q1 = p[s], q2 = p[2 * s];
    int a = sclip1(3 * (q0 - p0) + sclip1(p1 - q1));
    int a1 = (27 * a + 63) >> 7, a2 = (18 * a + 63) >> 7, a3 = (9 * a + 63) >> 7;
    p[-3 * s] = lim255(p2 + a3);
    p[-2 * s] = lim255(p1 + a2);
    p[-s] = lim255(p0 + a1);
    p[0] = lim255(q0 - a1);
    p[s] = lim255(q1 - a2);
    p[2 * s] = lim255(q2 - a3);
}

static int hev(const unsigned char *p, int s, int t)
{
    return ass(p[-2 * s] - p[-s]) > t || ass(p[s] - p[0]) > t;
}

static int serve(const unsigned char *p, int s, int t)
{
    return 4 * ass(p[-s] - p[0]) + ass(p[-2 * s] - p[s]) <= t;
}

static int serve2(const unsigned char *p, int s, int t, int it)
{
    if (4 * ass(p[-s] - p[0]) + ass(p[-2 * s] - p[s]) > t) return 0;
    return ass(p[-4 * s] - p[-3 * s]) <= it && ass(p[-3 * s] - p[-2 * s]) <= it &&
           ass(p[-2 * s] - p[-s]) <= it && ass(p[3 * s] - p[2 * s]) <= it &&
           ass(p[2 * s] - p[s]) <= it && ass(p[s] - p[0]) <= it;
}

/* hs: across the edge; vs: along it */
static void giro(unsigned char *p, int hs, int vs, int n, int lim, int it, int ht, int bordo)
{
    int t2 = 2 * lim + 1;
    while (n-- > 0) {
        if (serve2(p, hs, t2, it)) {
            if (hev(p, hs, ht)) filtro2(p, hs);
            else if (bordo) filtro6(p, hs);
            else filtro4(p, hs);
        }
        p += vs;
    }
}

static void giro_semplice(unsigned char *p, int hs, int vs, int lim)
{
    int t2 = 2 * lim + 1, i;
    for (i = 0; i < 16; i++, p += vs) if (serve(p, hs, t2)) filtro2(p, hs);
}

static void filtra(Frame *F)
{
    int mx, my, k;

    for (my = 0; my < F->mbh; my++)
        for (mx = 0; mx < F->mbw; mx++) {
            int i = my * F->mbw + mx;
            const Filtro *f = &F->f[F->mb_seg[i]][F->mb_i4[i]];
            int lim = f->limite, it = f->interno, ht = f->hev, dentro = f->dentro || F->mb_dentro[i];
            unsigned char *y = F->Y + my * 16 * F->ys + mx * 16;
            unsigned char *u = F->U + my * 8 * F->uvs + mx * 8;
            unsigned char *v = F->V + my * 8 * F->uvs + mx * 8;

            if (lim == 0) continue;
            if (F->semplice) {
                if (mx > 0) giro_semplice(y, 1, F->ys, lim + 4);
                if (dentro) for (k = 4; k < 16; k += 4) giro_semplice(y + k, 1, F->ys, lim);
                if (my > 0) giro_semplice(y, F->ys, 1, lim + 4);
                if (dentro) for (k = 4; k < 16; k += 4) giro_semplice(y + k * F->ys, F->ys, 1, lim);
                continue;
            }
            if (mx > 0) {
                giro(y, 1, F->ys, 16, lim + 4, it, ht, 1);
                giro(u, 1, F->uvs, 8, lim + 4, it, ht, 1);
                giro(v, 1, F->uvs, 8, lim + 4, it, ht, 1);
            }
            if (dentro) {
                for (k = 4; k < 16; k += 4) giro(y + k, 1, F->ys, 16, lim, it, ht, 0);
                giro(u + 4, 1, F->uvs, 8, lim, it, ht, 0);
                giro(v + 4, 1, F->uvs, 8, lim, it, ht, 0);
            }
            if (my > 0) {
                giro(y, F->ys, 1, 16, lim + 4, it, ht, 1);
                giro(u, F->uvs, 1, 8, lim + 4, it, ht, 1);
                giro(v, F->uvs, 1, 8, lim + 4, it, ht, 1);
            }
            if (dentro) {
                for (k = 4; k < 16; k += 4) giro(y + k * F->ys, F->ys, 1, 16, lim, it, ht, 0);
                giro(u + 4 * F->uvs, F->uvs, 1, 8, lim, it, ht, 0);
                giro(v + 4 * F->uvs, F->uvs, 1, 8, lim, it, ht, 0);
            }
        }
}

/* --------------------------------------------------------------------------
 * The frame header
 * -------------------------------------------------------------------------- */
static void testa(Frame *F, Bool *b)
{
    int i;

    valore(b, 1);                           /* colour space */
    valore(b, 1);                           /* clamping */
    F->segmenti = valore(b, 1);
    F->aggiorna_mappa = 0;
    for (i = 0; i < 3; i++) F->seg_prob[i] = 255;
    for (i = 0; i < 4; i++) F->seg_q[i] = F->seg_f[i] = 0;
    F->assoluto = 0;
    if (F->segmenti) {
        F->aggiorna_mappa = valore(b, 1);
        if (valore(b, 1)) {
            F->assoluto = valore(b, 1);
            for (i = 0; i < 4; i++) F->seg_q[i] = valore(b, 1) ? con_segno(b, 7) : 0;
            for (i = 0; i < 4; i++) F->seg_f[i] = valore(b, 1) ? con_segno(b, 6) : 0;
        }
        if (F->aggiorna_mappa)
            for (i = 0; i < 3; i++) F->seg_prob[i] = (unsigned char)(valore(b, 1) ? valore(b, 8) : 255);
    }
    F->semplice = valore(b, 1);
    F->livello = valore(b, 6);
    F->nitidezza = valore(b, 3);
    F->usa_delta = valore(b, 1);
    for (i = 0; i < 4; i++) F->ref_delta[i] = F->mode_delta[i] = 0;
    if (F->usa_delta && valore(b, 1)) {
        for (i = 0; i < 4; i++) if (valore(b, 1)) F->ref_delta[i] = con_segno(b, 6);
        for (i = 0; i < 4; i++) if (valore(b, 1)) F->mode_delta[i] = con_segno(b, 6);
    }
}

static void quantizzatori(Frame *F, Bool *b)
{
    int base = valore(b, 7), i;
    int dy1 = valore(b, 1) ? con_segno(b, 4) : 0;
    int dy2dc = valore(b, 1) ? con_segno(b, 4) : 0;
    int dy2ac = valore(b, 1) ? con_segno(b, 4) : 0;
    int duvdc = valore(b, 1) ? con_segno(b, 4) : 0;
    int duvac = valore(b, 1) ? con_segno(b, 4) : 0;

    for (i = 0; i < 4; i++) {
        int q = base;
        Quant *Q = &F->q[i];
        if (F->segmenti) { q = F->seg_q[i]; if (!F->assoluto) q += base; }
        Q->y1[0] = VP8_DC_Q[lim127(q + dy1)];
        Q->y1[1] = VP8_AC_Q[lim127(q)];
        Q->y2[0] = VP8_DC_Q[lim127(q + dy2dc)] * 2;
        Q->y2[1] = (VP8_AC_Q[lim127(q + dy2ac)] * 101581) >> 16;
        if (Q->y2[1] < 8) Q->y2[1] = 8;
        Q->uv[0] = VP8_DC_Q[q + duvdc < 0 ? 0 : q + duvdc > 117 ? 117 : q + duvdc];
        Q->uv[1] = VP8_AC_Q[lim127(q + duvac)];
    }
}

static void forze_filtro(Frame *F)
{
    int s, i4;

    for (s = 0; s < 4; s++)
        for (i4 = 0; i4 < 2; i4++) {
            Filtro *f = &F->f[s][i4];
            int lv = F->livello;
            if (F->segmenti) { lv = F->seg_f[s]; if (!F->assoluto) lv += F->livello; }
            if (F->usa_delta) { lv += F->ref_delta[0]; if (i4) lv += F->mode_delta[0]; }
            lv = lv < 0 ? 0 : lv > 63 ? 63 : lv;
            f->dentro = i4;
            if (lv > 0) {
                int il = lv;
                if (F->nitidezza > 0) {
                    il >>= F->nitidezza > 4 ? 2 : 1;
                    if (il > 9 - F->nitidezza) il = 9 - F->nitidezza;
                }
                if (il < 1) il = 1;
                f->interno = il;
                f->limite = 2 * lv + il;
                f->hev = lv >= 40 ? 2 : lv >= 15 ? 1 : 0;
            } else f->limite = 0;
            if (F->livello == 0) f->limite = 0;     /* no filter at all */
        }
}

/* --------------------------------------------------------------------------
 * The frame
 * -------------------------------------------------------------------------- */
int webp_vp8(const unsigned char *d, unsigned int n,
             unsigned int *w, unsigned int *h, unsigned int **px)
{
    static Frame  FF;
    Frame        *F = &FF;
    Bool          b, part[8];
    unsigned int  prima, i, np, pos;
    int           mx, my, t, bb, c, p;
    unsigned char *su_modi, sx_modi[4], *su_nz, sx_nz, *su_dc, sx_dc;

    if (n < 10) return 0;
    if (d[0] & 1) return 0;                             /* not a key frame */
    prima = ((unsigned int)d[0] | ((unsigned int)d[1] << 8) | ((unsigned int)d[2] << 16)) >> 5;
    if (d[3] != 0x9D || d[4] != 0x01 || d[5] != 0x2A) return 0;
    *w = ((unsigned int)d[6] | ((unsigned int)d[7] << 8)) & 0x3FFF;
    *h = ((unsigned int)d[8] | ((unsigned int)d[9] << 8)) & 0x3FFF;
    if (*w == 0 || *h == 0 || *w > EXIMG_LATO_MAX || *h > EXIMG_LATO_MAX) return 0;
    if (prima > n - 10) return 0;

    bool_inizia(&b, d + 10, prima);
    testa(F, &b);
    np = 1u << valore(&b, 2);
    quantizzatori(F, &b);
    valore(&b, 1);                                      /* refresh entropy probs */
    for (t = 0; t < 4; t++)
        for (bb = 0; bb < 8; bb++)
            for (c = 0; c < 3; c++)
                for (p = 0; p < 11; p++) {
                    int k = ((t * 8 + bb) * 3 + c) * 11 + p;
                    F->prob[t][bb][c][p] = (unsigned char)(bit(&b, VP8_COEFF_UPDATE[k]) ?
                                                           valore(&b, 8) : VP8_COEFF_DEFAULT[k]);
                }
    F->usa_salto = valore(&b, 1);
    F->prob_salto = F->usa_salto ? valore(&b, 8) : 0;
    forze_filtro(F);

    /* The token partitions. */
    pos = 10 + prima;
    if (pos + 3 * (np - 1) > n) return 0;
    {
        unsigned int dati = pos + 3 * (np - 1);
        for (i = 0; i < np; i++) {
            unsigned int l = i + 1 < np ? (unsigned int)d[pos + 3 * i] | ((unsigned int)d[pos + 3 * i + 1] << 8) |
                                          ((unsigned int)d[pos + 3 * i + 2] << 16)
                                        : n - dati;
            if (dati > n || l > n - dati) l = dati > n ? 0 : n - dati;
            bool_inizia(&part[i], d + dati, l);
            dati += l;
        }
    }

    F->mbw = (int)((*w + 15) >> 4);
    F->mbh = (int)((*h + 15) >> 4);
    F->ys = F->mbw * 16;
    F->uvs = F->mbw * 8;
    F->Y = (unsigned char *)webp_prendi((unsigned int)(F->ys * F->mbh * 16));
    F->U = (unsigned char *)webp_prendi((unsigned int)(F->uvs * F->mbh * 8));
    F->V = (unsigned char *)webp_prendi((unsigned int)(F->uvs * F->mbh * 8));
    F->mb_seg = (unsigned char *)webp_prendi((unsigned int)(F->mbw * F->mbh));
    F->mb_i4 = (unsigned char *)webp_prendi((unsigned int)(F->mbw * F->mbh));
    F->mb_dentro = (unsigned char *)webp_prendi((unsigned int)(F->mbw * F->mbh));
    su_modi = (unsigned char *)webp_prendi((unsigned int)F->mbw * 4u);
    su_nz = (unsigned char *)webp_prendi((unsigned int)F->mbw * 9u);
    su_dc = (unsigned char *)webp_prendi((unsigned int)F->mbw);
    if (!F->Y || !F->U || !F->V || !F->mb_seg || !F->mb_i4 || !F->mb_dentro ||
        !su_modi || !su_nz || !su_dc) return 0;

    for (my = 0; my < F->mbh; my++) {
        Bool *tb = &part[my & (np - 1)];

        for (i = 0; i < 4; i++) sx_modi[i] = B_DC;
        sx_nz = 0;          /* bits: 0-3 luma rows, 4-5 u, 6-7 v */
        sx_dc = 0;
        for (mx = 0; mx < F->mbw; mx++) {
            int seg = 0, salta = 0, i4, ymodo = 0, uvmodo, k, ha_coeff = 0;
            unsigned char imodi[16];
            static short coef[25][16];
            unsigned char *snz = su_nz + mx * 9;
            const Quant *Q;

            /* modes, from the first partition */
            if (F->aggiorna_mappa)
                seg = !bit(&b, F->seg_prob[0]) ? bit(&b, F->seg_prob[1]) : bit(&b, F->seg_prob[2]) + 2;
            if (F->usa_salto) salta = bit(&b, F->prob_salto);
            i4 = !bit(&b, 145);
            if (!i4) {
                ymodo = bit(&b, 156) ? (bit(&b, 128) ? TM_PRED : H_PRED) : (bit(&b, 163) ? V_PRED : DC_PRED);
                for (k = 0; k < 4; k++) { su_modi[mx * 4 + k] = (unsigned char)ymodo; sx_modi[k] = (unsigned char)ymodo; }
            } else {
                int x, y;
                for (y = 0; y < 4; y++) {
                    int m = sx_modi[y];
                    for (x = 0; x < 4; x++) {
                        const unsigned char *pr = VP8_BMODE_PROB + (su_modi[mx * 4 + x] * 10 + m) * 9;
                        int j = ALBERO_4[bit(&b, pr[0])];
                        while (j > 0) j = ALBERO_4[2 * j + bit(&b, pr[j])];
                        m = -j;
                        su_modi[mx * 4 + x] = (unsigned char)m;
                        imodi[y * 4 + x] = (unsigned char)m;
                    }
                    sx_modi[y] = (unsigned char)m;
                }
            }
            uvmodo = !bit(&b, 142) ? DC_PRED : !bit(&b, 114) ? V_PRED : bit(&b, 183) ? TM_PRED : H_PRED;
            F->mb_seg[my * F->mbw + mx] = (unsigned char)seg;
            F->mb_i4[my * F->mbw + mx] = (unsigned char)i4;
            Q = &F->q[F->segmenti ? seg : 0];

            /* residuals, from the token partition */
            for (k = 0; k < 25; k++) for (i = 0; i < 16; i++) coef[k][i] = 0;
            if (!salta) {
                int first = 0, ti = 3, x, y;
                if (!i4) {
                    short dc[16];
                    int nz;
                    for (i = 0; i < 16; i++) dc[i] = 0;
                    nz = coefficienti(tb, F->prob[1], su_dc[mx] + sx_dc, Q->y2, 0, dc);
                    su_dc[mx] = sx_dc = (unsigned char)(nz > 0);
                    if (nz > 1) wht(dc, &coef[0][0]);
                    else {
                        int dc0 = (dc[0] + 3) >> 3;
                        for (i = 0; i < 16; i++) coef[i][0] = (short)dc0;
                    }
                    first = 1;
                    ti = 0;
                }
                for (y = 0; y < 4; y++) {
                    int l = (sx_nz >> y) & 1;
                    for (x = 0; x < 4; x++) {
                        int ctx = l + snz[x];
                        int nz = coefficienti(tb, F->prob[ti], ctx, Q->y1, first, coef[y * 4 + x]);
                        l = nz > first;
                        snz[x] = (unsigned char)l;
                        if (nz > first || coef[y * 4 + x][0]) ha_coeff = 1;
                    }
                    sx_nz = (unsigned char)((sx_nz & ~(1 << y)) | (l << y));
                }
                for (c = 0; c < 2; c++)                      /* u, then v */
                    for (y = 0; y < 2; y++) {
                        int l = (sx_nz >> (4 + c * 2 + y)) & 1;
                        for (x = 0; x < 2; x++) {
                            int ctx = l + snz[4 + c * 2 + x];
                            int nz = coefficienti(tb, F->prob[2], ctx, Q->uv, 0, coef[16 + c * 4 + y * 2 + x]);
                            l = nz > 0;
                            snz[4 + c * 2 + x] = (unsigned char)l;
                            if (l) ha_coeff = 1;
                        }
                        sx_nz = (unsigned char)((sx_nz & ~(1 << (4 + c * 2 + y))) | (l << (4 + c * 2 + y)));
                    }
            } else {
                for (k = 0; k < 8; k++) snz[k] = 0;
                sx_nz = 0;
                if (!i4) { su_dc[mx] = 0; sx_dc = 0; }
            }
            F->mb_dentro[my * F->mbw + mx] = (unsigned char)ha_coeff;

            /* rebuild: the work areas with their borders */
            {
                static unsigned char ly[17 * LW], cu[9 * CW], cv[9 * CW];
                unsigned char *L0 = ly + LW + 1, *U0 = cu + CW + 1, *V0 = cv + CW + 1;
                unsigned char *Yd = F->Y + my * 16 * F->ys + mx * 16;
                unsigned char *Ud = F->U + my * 8 * F->uvs + mx * 8;
                unsigned char *Vd = F->V + my * 8 * F->uvs + mx * 8;
                int x, y;

                /* row above (and its corner, and 4 more to the right) */
                for (x = -1; x < 20; x++) {
                    int v = 127;
                    if (my > 0) {
                        if (x < 0) v = mx > 0 ? Yd[-F->ys - 1] : 129;
                        else if (x < 16) v = Yd[-F->ys + x];
                        else if (mx + 1 < F->mbw) v = Yd[-F->ys + x];
                        else v = Yd[-F->ys + 15];
                    }
                    L0[-LW + x] = (unsigned char)v;
                }
                for (y = 0; y < 16; y++) L0[y * LW - 1] = mx > 0 ? Yd[y * F->ys - 1] : 129;
                /* the top right, again above rows 4, 8 and 12 of the last column */
                for (y = 3; y < 15; y += 4) for (x = 16; x < 20; x++) L0[y * LW + x] = L0[-LW + x];
                for (x = -1; x < 8; x++) {
                    int vu = 127, vv = 127;
                    if (my > 0) {
                        if (x < 0) { vu = mx > 0 ? Ud[-F->uvs - 1] : 129; vv = mx > 0 ? Vd[-F->uvs - 1] : 129; }
                        else { vu = Ud[-F->uvs + x]; vv = Vd[-F->uvs + x]; }
                    }
                    U0[-CW + x] = (unsigned char)vu;
                    V0[-CW + x] = (unsigned char)vv;
                }
                for (y = 0; y < 8; y++) {
                    U0[y * CW - 1] = mx > 0 ? Ud[y * F->uvs - 1] : 129;
                    V0[y * CW - 1] = mx > 0 ? Vd[y * F->uvs - 1] : 129;
                }

                if (i4) {
                    for (k = 0; k < 16; k++) {
                        unsigned char *q = L0 + (k >> 2) * 4 * LW + (k & 3) * 4;
                        pred4(q, imodi[k]);
                        idct_somma(coef[k], q, LW);
                    }
                } else {
                    pred_blocco(L0, LW, 16, ymodo, my > 0, mx > 0);
                    for (k = 0; k < 16; k++)
                        idct_somma(coef[k], L0 + (k >> 2) * 4 * LW + (k & 3) * 4, LW);
                }
                pred_blocco(U0, CW, 8, uvmodo, my > 0, mx > 0);
                pred_blocco(V0, CW, 8, uvmodo, my > 0, mx > 0);
                for (k = 0; k < 4; k++) {
                    idct_somma(coef[16 + k], U0 + (k >> 1) * 4 * CW + (k & 1) * 4, CW);
                    idct_somma(coef[20 + k], V0 + (k >> 1) * 4 * CW + (k & 1) * 4, CW);
                }
                for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) Yd[y * F->ys + x] = L0[y * LW + x];
                for (y = 0; y < 8; y++)
                    for (x = 0; x < 8; x++) { Ud[y * F->uvs + x] = U0[y * CW + x]; Vd[y * F->uvs + x] = V0[y * CW + x]; }
            }
        }
    }
    if (F->livello > 0) filtra(F);

    /* YUV to RGB, as libwebp: 14-bit fixed point */
    *px = (unsigned int *)webp_prendi(*w * *h * 4u);
    if (!*px) return 0;
    for (i = 0; i < *h; i++) {
        unsigned int x;
        for (x = 0; x < *w; x++) {
            int Yv = F->Y[i * (unsigned int)F->ys + x];
            int Uv = F->U[(i >> 1) * (unsigned int)F->uvs + (x >> 1)];
            int Vv = F->V[(i >> 1) * (unsigned int)F->uvs + (x >> 1)];
            int yy = (Yv * 19077) >> 8;
            int r = yy + ((Vv * 26149) >> 8) - 14234;
            int g = yy - ((Uv * 6419) >> 8) - ((Vv * 13320) >> 8) + 8708;
            int bl = yy + ((Uv * 33050) >> 8) - 17685;
            r = r < 0 ? 0 : r >= (256 << 6) ? 255 : r >> 6;
            g = g < 0 ? 0 : g >= (256 << 6) ? 255 : g >> 6;
            bl = bl < 0 ? 0 : bl >= (256 << 6) ? 255 : bl >> 6;
            (*px)[i * *w + x] = 0xFF000000u | ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)bl;
        }
    }
    return 1;
}
