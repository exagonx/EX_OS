/* =============================================================================
 * tools/prove/docprova.c - the Word 6/95 reader on the host (9 October 2026)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     cc -I lib/exrtf -o /tmp/docprova tools/prove/docprova.c lib/exrtf/doc95.c lib/exrtf/rtf.c
 *     /tmp/docprova file.doc            prints what was read
 *     /tmp/docprova file.doc out.rtf    and writes it as RTF
 *
 * The file to read is made by tools/prove/doc95-genera.py, which writes a
 * Word 6 file by hand; the same file is given to LibreOffice, and what the
 * two read is compared.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "exrtf.h"

static char       g_testo[1 << 20];
static ExRtfPezzo g_pezzi[20000];
static ExRtfPar   g_par[20000];
static char       g_out[4 << 20];

int main(int argc, char **argv)
{
    ExRtfDoc d;
    FILE *f;
    unsigned char *b, *lav;
    long n;
    int r;
    unsigned int i;

    if (argc < 2 || !(f = fopen(argv[1], "rb"))) { fprintf(stderr, "uso: docprova file.doc [out.rtf]\n"); return 2; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b = malloc((size_t)n + 1); lav = malloc((size_t)n + 1);
    if (fread(b, 1, (size_t)n, f) != (size_t)n) return 2;
    fclose(f);

    exrtf_prepara(&d, g_testo, sizeof(g_testo), g_pezzi, 20000, g_par, 20000);
    r = exrtf_leggi_doc(&d, b, (unsigned int)n, lav, (unsigned int)n);
    printf("esito %d  testo %u byte, %u pezzi, %u paragrafi%s\n", r, d.testo_n, d.pezzi_n, d.par_n,
           d.troncato ? " TRONCATO" : "");
    if (r != 1) return 1;
    printf("pagina %u x %u, margini %u %u %u %u\n", d.carta_w, d.carta_h, d.marg_sin, d.marg_des, d.marg_su, d.marg_giu);
    for (i = 0; i < d.font_n; i++) printf("font %u: [%s] famiglia %u\n", i, d.font_nome[i], d.font_fam[i]);
    for (i = 0; i < d.pezzi_n && i < 200; i++) {
        const ExRtfStile *s = &d.pezzi[i].stile;
        printf("pezzo %2u: fam %u corpo %2u %s%s%s col %06X [", i, s->famiglia, s->corpo,
               s->grassetto ? "G" : "-", s->corsivo ? "C" : "-", s->sottolineato ? "S" : "-", s->colore);
        fwrite(d.testo + d.pezzi[i].inizio, 1, d.pezzi[i].lung, stdout);
        printf("]\n");
    }
    for (i = 0; i < d.par_n && i < 200; i++) {
        const ExRtfPar *P = &d.par[i];
        unsigned int k;
        printf("par %2u: all %u sin %d des %d prima %d sp %u/%u int %u tab", i, P->allinea, P->sin, P->des,
               P->prima, P->sp_prima, P->sp_dopo, P->interlinea);
        for (k = 0; k < P->tab_n; k++) printf(" %u", P->tab[k]);
        printf("\n");
    }
    if (argc > 2) {
        unsigned int k = exrtf_scrivi(&d, g_out, sizeof(g_out));
        f = fopen(argv[2], "wb");
        if (f) { fwrite(g_out, 1, k, f); fclose(f); }
    }
    return 0;
}
