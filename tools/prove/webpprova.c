/* =============================================================================
 * tools/prove/webpprova.c — il decodificatore WebP di eximg, sull'host
 * (@IMG-FORMATI, 30 settembre 2026)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     webpprova immagine.webp riferimento.rgba esatto|perdita
 *
 * Decodifica con lib/eximg/webp.c e vp8.c e confronta con i pixel RGBA (quattro
 * byte per pixel) che ha dato libwebp attraverso ImageMagick. Senza perdita
 * (VP8L) dev'essere IDENTICO, alfa compresa; con perdita (VP8) si guardano la
 * differenza media e la massima, come per il JPEG: libwebp interpola il
 * colore, noi lo ripetiamo.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eximg_interno.h"

void *eximg_memoria(unsigned int byte) { return byte ? malloc(byte) : 0; }

static unsigned char *leggi(const char *p, unsigned int *n)
{
    FILE *f = fopen(p, "rb");
    unsigned char *b;
    long l;

    if (!f) return 0;
    fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)l + 1);
    *n = (unsigned int)fread(b, 1, (size_t)l, f);
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    unsigned int n, nr, i, max = 0, maxa = 0;
    unsigned char *d, *rif;
    EximgBitmap bm;
    double somma = 0;
    int esatto;

    if (argc < 4) { fprintf(stderr, "uso: webpprova a.webp a.rgba esatto|perdita\n"); return 2; }
    esatto = strcmp(argv[3], "esatto") == 0;
    d = leggi(argv[1], &n);
    rif = leggi(argv[2], &nr);
    if (!d || !rif) { printf("NO  %s: non si legge\n", argv[1]); return 1; }
    memset(&bm, 0, sizeof(bm));
    if (!eximg_webp(d, n, &bm)) { printf("NO  %s: rifiutata\n", argv[1]); return 1; }
    if (nr != bm.larghezza * bm.altezza * 4u) {
        printf("NO  %s: %ux%u, il riferimento ha %u byte\n", argv[1], bm.larghezza, bm.altezza, nr);
        return 1;
    }
    for (i = 0; i < bm.larghezza * bm.altezza; i++) {
        unsigned int p = bm.px[i], c, a;
        int v[3];
        v[0] = (int)((p >> 16) & 255) - rif[i * 4];
        v[1] = (int)((p >> 8) & 255) - rif[i * 4 + 1];
        v[2] = (int)(p & 255) - rif[i * 4 + 2];
        a = (unsigned int)abs((int)(p >> 24) - rif[i * 4 + 3]);
        if (a > maxa) maxa = a;
        /* a fully transparent pixel's colour does not matter */
        if (rif[i * 4 + 3] == 0) continue;
        for (c = 0; c < 3; c++) {
            unsigned int x = (unsigned int)(v[c] < 0 ? -v[c] : v[c]);
            somma += x;
            if (x > max) max = x;
        }
    }
    somma /= (double)bm.larghezza * bm.altezza * 3.0;
    {
        int ok = esatto ? (max == 0 && maxa == 0) : (somma <= 3.0 && max <= 96u && maxa <= 2u);
        printf("%s  %s: %ux%u, differenza media %.2f, massima %u, alfa massima %u\n",
               ok ? "OK" : "NO", argv[1], bm.larghezza, bm.altezza, somma, max, maxa);
        return ok ? 0 : 1;
    }
}
