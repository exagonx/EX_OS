/* =============================================================================
 * lib/eximg/png.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * PNG — la parte che serve davvero
 *
 * Profondita' 8 bit, e i cinque tipi di colore che si incontrano:
 *
 *     0  grigio            2  RGB           3  tavolozza
 *     4  grigio + alfa     6  RGBA
 *
 * ! NIENTE INTERLACCIAMENTO ADAM7, ED E' DICHIARATO. Un PNG interlacciato non
 * e' un'immagine con le righe in ordine diverso: sono SETTE immagini piu'
 * piccole, ognuna col suo filtro e la sua larghezza, da ricomporre. E' un
 * pezzo di codice a se' che si esercita solo con i file che lo usano — cioe'
 * quasi mai — e un pezzo mai esercitato e' un pezzo di cui non si sa se
 * funziona. Chi ne ha uno lo salva senza interlacciamento.
 *
 * ! NIENTE 16 BIT PER CANALE. Si troncherebbero comunque a 8 per andare a
 * schermo, e leggerli vorrebbe dire il doppio del codice per buttare via
 * meta' di quello che si e' letto.
 *
 * ! I FILTRI SONO IL CUORE, E SONO CINQUE. Ogni riga di un PNG e' preceduta da
 * un byte che dice come e' stata predetta dalla riga sopra e dal pixel a
 * sinistra. Sbagliarne uno solo da' un'immagine che comincia giusta e degenera
 * verso il basso — il difetto si vede, ma solo sapendo che esiste.
 * ============================================================================= */

#include "eximg_interno.h"
#include "inflate.h"

static unsigned int be32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8)  |  (unsigned int)p[3];
}

/* Il predittore di Paeth: sceglie fra sinistra, sopra e diagonale quello che
 * si discosta meno dalla loro combinazione. E' l'unico filtro che non e' una
 * sottrazione, ed e' quello che si sbaglia. */
static int paeth(int a, int b, int c)
{
    int p  = a + b - c;
    int pa = p > a ? p - a : a - p;
    int pb = p > b ? p - b : b - p;
    int pc = p > c ? p - c : c - p;

    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc)             return b;
    return c;
}

/* Toglie il filtro da una riga, sul posto. `bpp` e' i byte per pixel, che e'
 * anche di quanto ci si sposta indietro per «il pixel a sinistra». */
static int sfiltra(unsigned char tipo, unsigned char *riga,
                   const unsigned char *sopra, unsigned int len, unsigned int bpp)
{
    unsigned int i;

    switch (tipo) {
    case 0:                                             /* None */
        break;

    case 1:                                             /* Sub */
        for (i = bpp; i < len; i++) riga[i] = (unsigned char)(riga[i] + riga[i - bpp]);
        break;

    case 2:                                             /* Up */
        if (sopra) for (i = 0; i < len; i++) riga[i] = (unsigned char)(riga[i] + sopra[i]);
        break;

    case 3:                                             /* Average */
        for (i = 0; i < len; i++) {
            int a = (i >= bpp) ? riga[i - bpp] : 0;
            int b = sopra ? sopra[i] : 0;
            riga[i] = (unsigned char)(riga[i] + ((a + b) >> 1));
        }
        break;

    case 4:                                             /* Paeth */
        for (i = 0; i < len; i++) {
            int a = (i >= bpp) ? riga[i - bpp] : 0;
            int b = sopra ? sopra[i] : 0;
            int c = (sopra && i >= bpp) ? sopra[i - bpp] : 0;
            riga[i] = (unsigned char)(riga[i] + paeth(a, b, c));
        }
        break;

    default:
        /* ! UN FILTRO SCONOSCIUTO E' UN FILE ROTTO, non un caso da ignorare:
         * proseguire darebbe righe interpretate a caso, e l'immagine
         * sembrerebbe «quasi giusta». */
        return -1;
    }
    return 0;
}

int eximg_png(const unsigned char *d, unsigned int n, EximgBitmap *bm)
{
    static const unsigned char FIRMA[8] = { 137,'P','N','G',13,10,26,10 };
    unsigned int i, pos = 8;
    unsigned int larg = 0, alt = 0, canali = 0, bpp;
    unsigned char prof = 0, tipo = 0, interlacciato = 1;
    unsigned char tavolozza[256 * 3];
    unsigned int  n_tavolozza = 0;
    unsigned char *zlib_dati;
    unsigned int   zlib_n = 0;
    unsigned char *grezzo;
    unsigned int   grezzo_n = 0, attesi;

    if (n < 8) return 0;
    for (i = 0; i < 8; i++) if (d[i] != FIRMA[i]) return 0;

    /* ! I PEZZI SI SCORRONO DUE VOLTE: la prima per sapere quanto sono grandi
     * gli IDAT messi insieme, la seconda per copiarli. Un PNG puo' spezzare i
     * dati in decine di pezzi, e la specifica dice che vanno concatenati PRIMA
     * di decomprimere — trattarli uno per uno darebbe un flusso troncato a
     * ogni confine. */
    while (pos + 8 <= n) {
        unsigned int len = be32(d + pos);
        const unsigned char *t = d + pos + 4;

        if (pos + 12 + len > n) break;      /* pezzo troncato: si smette */

        if (t[0]=='I' && t[1]=='H' && t[2]=='D' && t[3]=='R') {
            if (len < 13) return 0;
            larg = be32(d + pos + 8);
            alt  = be32(d + pos + 12);
            prof = d[pos + 16];
            tipo = d[pos + 17];
            interlacciato = d[pos + 20];
        } else if (t[0]=='P' && t[1]=='L' && t[2]=='T' && t[3]=='E') {
            n_tavolozza = len / 3u;
            if (n_tavolozza > 256) n_tavolozza = 256;
            for (i = 0; i < n_tavolozza * 3u; i++) tavolozza[i] = d[pos + 8 + i];
        } else if (t[0]=='I' && t[1]=='D' && t[2]=='A' && t[3]=='T') {
            zlib_n += len;
        } else if (t[0]=='I' && t[1]=='E' && t[2]=='N' && t[3]=='D') {
            break;
        }

        pos += 12 + len;
    }

    if (larg == 0 || alt == 0 || zlib_n == 0) return 0;
    if (larg > EXIMG_LATO_MAX || alt > EXIMG_LATO_MAX) return 0;

    /* ! I LIMITI SI DICHIARANO E SI RIFIUTANO, non si troncano. Un'immagine
     * troncata a meta' e' un difetto che chi guarda attribuisce al file. */
    if (prof != 8) return 0;
    if (interlacciato != 0) return 0;

    switch (tipo) {
    case 0: canali = 1; break;      /* grigio */
    case 2: canali = 3; break;      /* RGB */
    case 3: canali = 1; break;      /* tavolozza: un indice per pixel */
    case 4: canali = 2; break;      /* grigio + alfa */
    case 6: canali = 4; break;      /* RGBA */
    default: return 0;
    }
    if (tipo == 3 && n_tavolozza == 0) return 0;

    bpp    = canali;
    attesi = alt * (larg * bpp + 1u);   /* ogni riga ha il byte del filtro */

    zlib_dati = (unsigned char *)eximg_memoria(zlib_n);
    grezzo    = (unsigned char *)eximg_memoria(attesi);
    if (!zlib_dati || !grezzo) return 0;

    /* Seconda passata: si concatenano gli IDAT. */
    pos = 8;
    while (pos + 8 <= n) {
        unsigned int len = be32(d + pos);
        const unsigned char *t = d + pos + 4;

        if (pos + 12 + len > n) break;
        if (t[0]=='I' && t[1]=='D' && t[2]=='A' && t[3]=='T') {
            for (i = 0; i < len; i++) zlib_dati[grezzo_n + i] = d[pos + 8 + i];
            grezzo_n += len;
        }
        pos += 12 + len;
    }

    /* ! SI SALTANO I DUE BYTE DI INTESTAZIONE zlib. Il flusso dentro un IDAT
     * e' zlib (RFC 1950), non DEFLATE nudo: due byte davanti e quattro di
     * Adler-32 in coda. inflate() vuole il DEFLATE, e passargli l'intestazione
     * gli fa leggere il primo blocco a partire dai bit sbagliati. */
    if (grezzo_n < 3) return 0;
    {
        unsigned int prodotti = 0;

        if (inflate(zlib_dati + 2, grezzo_n - 2, grezzo, attesi, &prodotti) != 0)
            return 0;
        if (prodotti != attesi) return 0;
    }

    /* --- togliere i filtri e comporre l'ARGB ------------------------------ */
    bm->larghezza = larg;
    bm->altezza   = alt;
    bm->px = (unsigned int *)eximg_memoria(larg * alt * 4u);
    if (!bm->px) return 0;

    {
        unsigned int y;
        unsigned char *prec = 0;

        for (y = 0; y < alt; y++) {
            unsigned char *riga = grezzo + y * (larg * bpp + 1u);
            unsigned char  f    = riga[0];
            unsigned char *dati = riga + 1;
            unsigned int   x;

            if (sfiltra(f, dati, prec, larg * bpp, bpp) != 0) return 0;
            prec = dati;

            for (x = 0; x < larg; x++) {
                const unsigned char *p = dati + x * bpp;
                unsigned int r, g, b;

                switch (tipo) {
                case 0: r = g = b = p[0];               break;
                case 2: r = p[0]; g = p[1]; b = p[2];   break;
                case 4: r = g = b = p[0];               break;   /* alfa ignorata */
                case 6: r = p[0]; g = p[1]; b = p[2];   break;   /* alfa ignorata */
                default: {                                       /* tavolozza */
                    unsigned int k = p[0];
                    if (k >= n_tavolozza) k = 0;
                    r = tavolozza[k*3]; g = tavolozza[k*3+1]; b = tavolozza[k*3+2];
                    break;
                }
                }
                bm->px[y * larg + x] = (r << 16) | (g << 8) | b;
            }
        }
    }

    /* ! L'ALFA SI IGNORA, ED E' DICHIARATO. Il server compone finestre opache:
     * non c'e' un canale alfa su cui fondere, e fingere di rispettarlo — per
     * esempio moltiplicando per il fondo — darebbe un risultato giusto solo
     * sopra a un colore piatto. Quando il server sapra' fondere, questa riga
     * diventera' una mescolanza vera. */
    return 1;
}
