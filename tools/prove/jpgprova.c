/* =============================================================================
 * tools/prove/jpgprova.c — il decodificatore JPEG di eximg, sull'host
 * (@JPEG-PROGRESSIVO, 30 settembre 2026)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     jpgprova immagine.jpg riferimento.rgb
 *
 * Decodifica il JPEG con lib/eximg/jpg.c e lo confronta con i pixel RGB (tre
 * byte per pixel, riga dopo riga) che ha dato un decodificatore di
 * riferimento — ImageMagick, in tools/prova_jpg.sh. Due IDCT diverse non
 * danno gli stessi numeri: si guarda la differenza MEDIA per canale e la
 * MASSIMA, e i tetti sono larghi abbastanza per l'arrotondamento e stretti
 * abbastanza per un blocco sbagliato.
 *
 * Stampa «OK» o «NO» con i numeri, e rende 0 o 1.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eximg_interno.h"

/* La memoria di eximg, qui semplicemente malloc: il banco esce subito. */
void *eximg_memoria(unsigned int byte) { return byte ? malloc(byte) : 0; }

static unsigned char *leggi(const char *p, unsigned int *n)
{
    FILE          *f = fopen(p, "rb");
    unsigned char *b;
    long           l;

    if (!f) return 0;
    fseek(f, 0, SEEK_END); l = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)l + 1);
    *n = (unsigned int)fread(b, 1, (size_t)l, f);
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    unsigned int   nj, nr, i, max = 0;
    unsigned char *jp, *rif;
    EximgBitmap    bm;
    double         somma = 0;

    if (argc < 3) { fprintf(stderr, "uso: jpgprova a.jpg a.rgb\n"); return 2; }
    jp = leggi(argv[1], &nj);
    rif = leggi(argv[2], &nr);
    if (!jp || !rif) { printf("NO  %s: non si legge\n", argv[1]); return 1; }

    memset(&bm, 0, sizeof(bm));
    if (!eximg_jpg(jp, nj, &bm)) { printf("NO  %s: rifiutata\n", argv[1]); return 1; }
    if (nr != bm.larghezza * bm.altezza * 3u) {
        printf("NO  %s: %ux%u, il riferimento ha %u byte\n", argv[1],
               bm.larghezza, bm.altezza, nr);
        return 1;
    }
    for (i = 0; i < bm.larghezza * bm.altezza; i++) {
        unsigned int p = bm.px[i], c;
        int v[3];

        v[0] = (int)((p >> 16) & 255) - rif[i * 3];
        v[1] = (int)((p >> 8) & 255) - rif[i * 3 + 1];
        v[2] = (int)(p & 255) - rif[i * 3 + 2];
        for (c = 0; c < 3; c++) {
            unsigned int a = (unsigned int)(v[c] < 0 ? -v[c] : v[c]);
            somma += a;
            if (a > max) max = a;
        }
    }
    somma /= (double)bm.larghezza * bm.altezza * 3.0;
    /* ! 4 DI MEDIA E 64 DI MASSIMO: ImageMagick ricampiona il colore con un
     * filtro, noi lo ripetiamo; ai bordi di un colore netto la differenza e'
     * grande su pochi pixel, e un blocco sbagliato alza la MEDIA. */
    printf("%s  %s: %ux%u, differenza media %.2f, massima %u\n",
           (somma <= 4.0 && max <= 64u) ? "OK" : "NO", argv[1],
           bm.larghezza, bm.altezza, somma, max);
    return (somma <= 4.0 && max <= 64u) ? 0 : 1;
}
