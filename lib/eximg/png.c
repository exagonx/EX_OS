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
 * Tutte le profondita' (1, 2, 4, 8, 16 bit) e i cinque tipi di colore:
 *     0  grigio            2  RGB           3  tavolozza
 *     4  grigio + alfa     6  RGBA
 * e l'interlacciamento Adam7.
 *
 * ! FINO AL 28 SETTEMBRE 2026 SOLO 8 BIT E NIENTE ADAM7, e la ragione era
 * scritta qui: un pezzo mai esercitato e' un pezzo di cui non si sa se
 * funziona. Poi il visualizzatore (@IMMAGINI) ha mostrato «formato non
 * riconosciuto» su una PNG qualunque — ImageMagick salva un'immagine di pochi
 * colori a tavolozza da 1 bit — e un lettore che rifiuta i file comuni fa
 * sembrare rotte le immagini. La risposta alla vecchia obiezione e' la prova:
 * tools/prova_png.sh fa scrivere a ImageMagick ogni combinazione di tipo,
 * profondita' e interlacciamento e confronta i pixel con i suoi.
 *
 * ! I 16 BIT SI LEGGONO E SI TRONCANO A 8 (il byte alto): lo schermo ne ha 8,
 * e tenerne di piu' in memoria non cambierebbe un pixel visibile.
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

/* =============================================================================
 * Le righe sotto gli 8 bit, i 16 bit, e le sette passate di Adam7
 * (28 settembre 2026, @IMMAGINI)
 * ============================================================================= */

/* I byte di una riga di `w` pixel, senza il byte del filtro. */
static unsigned int riga_byte(unsigned int w, unsigned int canali, unsigned int prof)
{
    return (w * canali * prof + 7u) / 8u;
}

/* Il campione `c` del pixel `x`, portato a 8 bit. `scala` = 0 per un indice di
 * tavolozza, che resta com'e'.
 * ! SOTTO GLI 8 BIT IL GRIGIO SI MOLTIPLICA, NON SI SPOSTA: a 1 bit il bianco
 * e' 1, e 1 << 7 darebbe 128 invece di 255. */
static unsigned int campione(const unsigned char *dati, unsigned int x, unsigned int c,
                             unsigned int canali, unsigned int prof, int scala)
{
    unsigned int i = x * canali + c, v, max;

    if (prof == 8)  return dati[i];
    if (prof == 16) return dati[i * 2];             /* il byte alto */
    v = (dati[(i * prof) / 8u] >> (8u - prof - (i * prof) % 8u)) & ((1u << prof) - 1u);
    if (!scala) return v;
    max = (1u << prof) - 1u;
    return v * 255u / max;
}

/* Dove comincia la passata `p` di Adam7 e di quanto salta. Senza
 * interlacciamento, una passata sola che copre tutto. */
static void passata_passi(unsigned int p, unsigned int interl, unsigned int *x0, unsigned int *y0,
                          unsigned int *dx, unsigned int *dy)
{
    static const unsigned char X0[7] = { 0, 4, 0, 2, 0, 1, 0 };
    static const unsigned char Y0[7] = { 0, 0, 4, 0, 2, 0, 1 };
    static const unsigned char DX[7] = { 8, 8, 4, 4, 2, 2, 1 };
    static const unsigned char DY[7] = { 8, 8, 8, 4, 4, 2, 2 };

    if (!interl) { *x0 = *y0 = 0; *dx = *dy = 1; return; }
    *x0 = X0[p]; *y0 = Y0[p]; *dx = DX[p]; *dy = DY[p];
}

/* Quanti pixel ha la passata `p`, in larghezza e altezza. Puo' essere zero: su
 * un'immagine piccola alcune passate sono vuote, e non hanno nemmeno il byte
 * del filtro. */
static void passata_misura(unsigned int p, unsigned int interl, unsigned int w, unsigned int h,
                           unsigned int *pw, unsigned int *ph)
{
    unsigned int x0, y0, dx, dy;

    passata_passi(p, interl, &x0, &y0, &dx, &dy);
    *pw = w > x0 ? (w - x0 + dx - 1u) / dx : 0;
    *ph = h > y0 ? (h - y0 + dy - 1u) / dy : 0;
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

    /* Le combinazioni che la specifica ammette; il resto e' un file rotto. */
    switch (tipo) {
    case 0: canali = 1; if (prof != 1 && prof != 2 && prof != 4 && prof != 8 && prof != 16) return 0; break;
    case 3: canali = 1; if (prof != 1 && prof != 2 && prof != 4 && prof != 8) return 0; break;
    case 2: canali = 3; if (prof != 8 && prof != 16) return 0; break;
    case 4: canali = 2; if (prof != 8 && prof != 16) return 0; break;
    case 6: canali = 4; if (prof != 8 && prof != 16) return 0; break;
    default: return 0;
    }
    if (interlacciato > 1) return 0;
    if (tipo == 3 && n_tavolozza == 0) return 0;

    /* ! bpp E' «DI QUANTO SI TORNA INDIETRO» PER IL FILTRO, in byte, e sotto
     * gli 8 bit vale 1: la specifica lo vuole cosi', non la divisione. */
    bpp = (canali * prof) / 8u;
    if (bpp == 0) bpp = 1;

    /* I byte da decomprimere: una passata sola, o le sette di Adam7, ognuna
     * con le sue righe e il suo byte di filtro per riga. */
    attesi = 0;
    for (i = 0; i < (interlacciato ? 7u : 1u); i++) {
        unsigned int pw, ph;
        passata_misura(i, interlacciato, larg, alt, &pw, &ph);
        if (pw && ph) attesi += ph * (riga_byte(pw, canali, prof) + 1u);
    }

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

    /* --- togliere i filtri e comporre l'ARGB, passata per passata --------- */
    bm->larghezza = larg;
    bm->altezza   = alt;
    bm->px = (unsigned int *)eximg_memoria(larg * alt * 4u);
    if (!bm->px) return 0;

    {
        unsigned char *riga = grezzo;
        unsigned int   pass;

        for (pass = 0; pass < (interlacciato ? 7u : 1u); pass++) {
            unsigned int pw, ph, rb, y, x0, y0, dx, dy;
            unsigned char *prec = 0;

            passata_misura(pass, interlacciato, larg, alt, &pw, &ph);
            if (!pw || !ph) continue;
            rb = riga_byte(pw, canali, prof);
            passata_passi(pass, interlacciato, &x0, &y0, &dx, &dy);

            for (y = 0; y < ph; y++, riga += rb + 1u) {
                unsigned char *dati = riga + 1;
                unsigned int   x;

                if (sfiltra(riga[0], dati, prec, rb, bpp) != 0) return 0;
                prec = dati;

                for (x = 0; x < pw; x++) {
                    unsigned int r, g, b;

                    switch (tipo) {
                    case 0: r = g = b = campione(dati, x, 0, 1, prof, 1); break;
                    case 2: r = campione(dati, x, 0, 3, prof, 1);
                            g = campione(dati, x, 1, 3, prof, 1);
                            b = campione(dati, x, 2, 3, prof, 1); break;
                    case 4: r = g = b = campione(dati, x, 0, 2, prof, 1); break;  /* alfa ignorata */
                    case 6: r = campione(dati, x, 0, 4, prof, 1);                /* alfa ignorata */
                            g = campione(dati, x, 1, 4, prof, 1);
                            b = campione(dati, x, 2, 4, prof, 1); break;
                    default: {                                                   /* tavolozza */
                        unsigned int k = campione(dati, x, 0, 1, prof, 0);
                        if (k >= n_tavolozza) k = 0;
                        r = tavolozza[k*3]; g = tavolozza[k*3+1]; b = tavolozza[k*3+2];
                        break;
                    }
                    }
                    bm->px[(y0 + y * dy) * larg + x0 + x * dx] = (r << 16) | (g << 8) | b;
                }
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
