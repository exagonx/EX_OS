/* =============================================================================
 * tools/prove/ttfprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il banco di prova del lettore TrueType, che gira SULL'HOST.
 *
 * ! SI PROVA FUORI DA EX-OS PERCHE' IL PARSER NON SA COS'E' EX-OS. Legge byte
 * e rende numeri: non tocca il video, non fa syscall, non alloca. Provarlo
 * dentro una macchina virtuale vorrebbe dire un giro di costruzione e
 * novanta secondi di avvio per ogni riga cambiata — e' la stessa ragione per
 * cui inflate e' stato verificato contro zlib sull'host prima di entrare.
 *
 * ! E IL RIFERIMENTO E' DI QUALCUN ALTRO. Confrontare il mio parser con il mio
 * parser non prova niente. Qui il confronto e' con `fc-query --format
 * '%{charset}'`, cioe' con il parser di fontconfig: se i due sono d'accordo su
 * QUALI codici il font copre, la cmap e' letta bene. E' lo stesso patto di
 * zlib per inflate.
 *
 *     cc -o /tmp/ttfprova tools/prove/ttfprova.c lib/exfont/ttf.c \
 *        -I lib/exfont -I tools/prove
 *     /tmp/ttfprova <font.ttf> [codici|monospazio|charset]
 * ============================================================================= */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ttf.h"
#include "raster.h"

static unsigned char *leggi_file(const char *p, unsigned int *n)
{
    FILE          *fh = fopen(p, "rb");
    unsigned char *d;
    long           len;

    if (!fh) return 0;
    fseek(fh, 0, SEEK_END); len = ftell(fh); fseek(fh, 0, SEEK_SET);
    d = malloc((size_t)len);
    if (!d) { fclose(fh); return 0; }
    if (fread(d, 1, (size_t)len, fh) != (size_t)len) { free(d); fclose(fh); return 0; }
    fclose(fh);
    *n = (unsigned int)len;
    return d;
}

int main(int argc, char **argv)
{
    unsigned char *d;
    unsigned int   n;
    TtfFont        f;
    const char    *modo = (argc > 2) ? argv[2] : "riassunto";

    if (argc < 2) { fprintf(stderr, "uso: ttfprova <font.ttf> [modo]\n"); return 2; }

    d = leggi_file(argv[1], &n);
    if (!d) { fprintf(stderr, "non riesco a leggere %s\n", argv[1]); return 2; }

    if (!ttf_apri(d, n, &f)) {
        printf("RIFIUTATO: %s non e' un TrueType che so leggere\n", argv[1]);
        free(d);        /* anche qui: LeakSanitizer lo nota, ed ha ragione */
        return 1;
    }

    if (strcmp(modo, "charset") == 0) {
        /* Ogni codice del piano base che il font mappa, uno per riga in
         * esadecimale: si confronta con fc-query. */
        unsigned int c;

        for (c = 1; c <= 0xFFFFu; c++)
            if (ttf_glifo_di(&f, c) != 0) printf("%x\n", c);
        free(d);
        return 0;
    }

    if (strcmp(modo, "monospazio") == 0) {
        /* ! IN UN FONT MONOSPAZIO OGNI GLIFO DISEGNABILE HA LA STESSA
         * LARGHEZZA, ed e' un invariante che il file non puo' soddisfare per
         * caso: se hmtx fosse letto male, le larghezze sarebbero diverse fra
         * loro. Vale come prova indipendente della tabella delle metriche. */
        unsigned int c, w0 = 0, diverse = 0, provati = 0;

        for (c = 0x21; c <= 0x7E; c++) {
            unsigned int g = ttf_glifo_di(&f, c);
            unsigned int w;

            if (!g) continue;
            w = ttf_avanzamento(&f, g);
            provati++;
            if (w0 == 0) w0 = w;
            else if (w != w0) diverse++;
        }
        printf("larghezza comune %u su %u glifi provati, %u diverse -> %s\n",
               w0, provati, diverse, diverse ? "NON monospazio" : "monospazio");
        free(d);
        return diverse ? 1 : 0;
    }

    if (strcmp(modo, "disegna") == 0) {
        /* Il glifo in arte ASCII: la prova che nessun numero puo' dare, cioe'
         * «somiglia a quella lettera?». */
        const char   *testo = (argc > 3) ? argv[3] : "B";
        int           corpo = (argc > 4) ? atoi(argv[4]) : 24;
        static unsigned char cop[512 * 512];
        const char   *scala = " .:-=+*#%@";
        const char   *t;

        for (t = testo; *t; t++) {
            unsigned int g = ttf_glifo_di(&f, (unsigned char)*t);
            RasterMisure m;
            int          y, x;

            if (!raster_misura(&f, g, corpo, &m)) { printf("misura fallita\n"); continue; }
            printf("--- '%c' glifo %u  %dx%d  sinistra %d cima %d  avanza %d.%02d\n",
                   *t, g, m.larghezza, m.altezza, m.sinistra, m.cima,
                   m.avanzamento >> 6, ((m.avanzamento & 63) * 100) >> 6);
            if (m.larghezza <= 0 || m.altezza <= 0) continue;
            if ((long)m.larghezza * m.altezza > (long)sizeof(cop)) continue;
            if (!raster_glifo(&f, g, corpo, &m, cop)) { printf("disegno fallito\n"); continue; }

            for (y = 0; y < m.altezza; y++) {
                for (x = 0; x < m.larghezza; x++)
                    putchar(scala[cop[y * m.larghezza + x] * 9 / 255]);
                putchar('\n');
            }
        }
        free(d);
        return 0;
    }

    if (strcmp(modo, "pgm") == 0) {
        /* Lo stesso, ma in PGM: serve al confronto con FreeType. */
        unsigned int g = ttf_glifo_di(&f, (unsigned char)(argc > 3 ? argv[3][0] : 'B'));
        int          corpo = (argc > 4) ? atoi(argv[4]) : 24;
        static unsigned char cop[512 * 512];
        RasterMisure m;

        if (!raster_misura(&f, g, corpo, &m) || m.larghezza <= 0) { free(d); return 1; }
        if (!raster_glifo(&f, g, corpo, &m, cop)) { free(d); return 1; }
        printf("P5\n%d %d\n255\n", m.larghezza, m.altezza);
        fwrite(cop, 1, (size_t)m.larghezza * m.altezza, stdout);
        free(d);
        return 0;
    }

    printf("%s\n", argv[1]);
    printf("  unita' per em   %u\n", f.unita_em);
    printf("  glifi           %u\n", f.n_glifi);
    printf("  metriche hmtx   %u\n", f.n_metriche);
    printf("  ascesa/discesa  %d / %d  (interlinea %d)\n",
           f.ascesa, f.discesa, f.interlinea);
    printf("  loca            %s\n", f.loca_lunga ? "32 bit" : "16 bit");

    {
        const char *campione = "AWil .";
        const char *s;

        printf("  avanzamenti     ");
        for (s = campione; *s; s++) {
            unsigned int g = ttf_glifo_di(&f, (unsigned char)*s);
            printf("'%c'=%u ", *s, ttf_avanzamento(&f, g));
        }
        printf("\n");
    }

    {
        /* Quanti comandi produce una lettera con le curve, e uno spazio. */
        TtfComando   cmd[512];
        unsigned int g = ttf_glifo_di(&f, 'B');
        int          k = ttf_contorno(&f, g, cmd, 512);
        unsigned int gs = ttf_glifo_di(&f, ' ');
        int          ks = ttf_contorno(&f, gs, cmd, 512);
        unsigned int ga = ttf_glifo_di(&f, 0xE0);   /* a con accento grave */
        int          ka = ttf_contorno(&f, ga, cmd, 512);

        printf("  contorno 'B'    %d comandi\n", k);
        printf("  contorno ' '    %d comandi (vuoto: e' giusto)\n", ks);
        printf("  contorno 'a`'   %d comandi (glifo composto)\n", ka);
    }

    free(d);
    return 0;
}
