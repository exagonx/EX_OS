/* =============================================================================
 * lib/eximg/ico.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ICO — un contenitore, non un formato
 *
 * ! LA COSA DA CAPIRE E' CHE UN .ico NON E' UN'IMMAGINE: E' UN ELENCO DI
 * IMMAGINI. La stessa icona a 16x16, 32x32 e 48x48, ognuna magari con una
 * profondita' diversa, e chi la usa sceglie quella che gli serve. Trattarlo
 * come un formato solo vuol dire prendere la prima che capita — che nei file
 * veri e' spesso la piu' piccola, cioe' quella che a schermo si vede peggio.
 *
 * ! E DENTRO NON C'E' UN BMP, C'E' MEZZO BMP. Manca l'intestazione di FILE
 * (i 14 byte con 'BM' e l'offset dei pixel): si comincia direttamente dal
 * BITMAPINFOHEADER. Passare questi byte al lettore BMP darebbe «non e' mio»,
 * ed e' il motivo per cui l'ICO ha un lettore invece di una riga.
 *
 * ! L'ALTEZZA NELL'INTESTAZIONE E' IL DOPPIO DI QUELLA VERA, e questo e' il
 * dettaglio che si sbaglia. Il DIB descrive due bitmap impilate: i colori e
 * sotto la maschera di trasparenza a 1 bit. Chi legge biHeight e lo prende per
 * buono ottiene un'icona alta il doppio, con la meta' di sotto piena di
 * spazzatura in bianco e nero — e a colpo d'occhio sembra un difetto dei
 * filtri, non della misura.
 *
 * ! DENTRO PUO' ANCHE ESSERCI UN PNG, dal 2007 in poi, e in quel caso i byte
 * si passano al lettore PNG che abbiamo gia'. Un formato dentro l'altro non e'
 * un caso strano da tollerare: per le icone grandi (256x256) e' la regola.
 * ============================================================================= */

#include "eximg_interno.h"

static unsigned int le16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* Legge il DIB di UNA voce: intestazione da 40 byte, tavolozza, pixel.
 * `d` punta al BITMAPINFOHEADER, `n` e' quanto ne resta. */
static int leggi_dib(const unsigned char *d, unsigned int n, EximgBitmap *bm)
{
    unsigned int intest, larg, alt2, alt, bit, n_tav, riga, dati, i, j;
    const unsigned char *tav, *px;

    if (n < 40) return 0;

    intest = le32(d);
    if (intest < 40 || intest > n) return 0;    /* BITMAPCOREHEADER: no */

    larg = le32(d + 4);
    alt2 = le32(d + 8);
    bit  = le16(d + 14);

    /* ! LA COMPRESSIONE DEVE ESSERE ZERO. Un ICO con dentro un RLE esiste in
     * teoria e non si incontra mai; leggerlo come non compresso darebbe
     * un'immagine di rumore invece di un rifiuto. */
    if (le32(d + 16) != 0) return 0;

    if (larg == 0 || alt2 == 0) return 0;
    if (larg > EXIMG_LATO_MAX || alt2 > EXIMG_LATO_MAX) return 0;

    alt = alt2 / 2u;                    /* colori + maschera, vedi sopra */
    if (alt == 0) return 0;

    if (bit != 1 && bit != 4 && bit != 8 && bit != 24 && bit != 32) return 0;

    /* La tavolozza sta subito dopo l'intestazione, e la sua misura la dice il
     * campo biClrUsed — se e' zero vale il massimo per quella profondita'. */
    n_tav = (bit <= 8) ? le32(d + 32) : 0;
    if (bit <= 8 && n_tav == 0) n_tav = 1u << bit;
    if (n_tav > 256) return 0;

    tav  = d + intest;
    px   = tav + n_tav * 4u;            /* le voci sono BGRA, quattro byte */
    riga = ((larg * bit + 31u) / 32u) * 4u;     /* righe allineate a 32 bit */

    dati = (unsigned int)(px - d);
    if (dati > n || riga > (n - dati) / alt) return 0;

    bm->larghezza = larg;
    bm->altezza   = alt;
    bm->px        = (unsigned int *)eximg_memoria(larg * alt * 4u);
    if (!bm->px) return 0;

    for (j = 0; j < alt; j++) {
        /* Sottosopra, come in ogni DIB: la prima riga dei dati e' l'ultima
         * dell'immagine. */
        const unsigned char *r = px + (alt - 1u - j) * riga;

        for (i = 0; i < larg; i++) {
            unsigned int c, k;

            switch (bit) {
            case 32: c = ((unsigned int)r[i*4+2] << 16) |
                         ((unsigned int)r[i*4+1] << 8)  | r[i*4];
                     break;
            case 24: c = ((unsigned int)r[i*3+2] << 16) |
                         ((unsigned int)r[i*3+1] << 8)  | r[i*3];
                     break;
            case 8:  k = r[i];                                  goto tavola;
            case 4:  k = (i & 1) ? (r[i>>1] & 0x0Fu) : (r[i>>1] >> 4);
                     goto tavola;
            default: k = (r[i>>3] >> (7 - (i & 7))) & 1u;
            tavola:
                     if (k >= n_tav) k = 0;
                     c = ((unsigned int)tav[k*4+2] << 16) |
                         ((unsigned int)tav[k*4+1] << 8)  | tav[k*4];
                     break;
            }

            bm->px[j * larg + i] = c;
        }
    }

    /* ! LA MASCHERA A 1 BIT SI SALTA, come si ignora l'alfa del PNG: il server
     * compone finestre opache e non c'e' niente su cui fondere. Il giorno che
     * sapra' fondere, e' li' che si andra' a prenderla. */
    return 1;
}

int eximg_ico(const unsigned char *d, unsigned int n, EximgBitmap *bm)
{
    unsigned int quante, i, scelta = 0, area_max = 0, bit_max = 0;
    unsigned int off = 0, len = 0;

    if (n < 6) return 0;
    if (le16(d) != 0) return 0;                 /* riservato, e' sempre zero */
    if (le16(d + 2) != 1) return 0;             /* 1 = icona, 2 = cursore */

    quante = le16(d + 4);
    if (quante == 0) return 0;
    if (6u + quante * 16u > n) return 0;

    /* ! SI SCEGLIE LA PIU' GRANDE, E A PARI MISURA QUELLA CON PIU' COLORI.
     * Prendere la prima vorrebbe dire lasciare la scelta all'ordine in cui il
     * programma che ha scritto il file le ha messe — cioe' a nessuno. Ingrandire
     * una 16x16 si vede; rimpicciolire una 48x48 no. */
    for (i = 0; i < quante; i++) {
        const unsigned char *e = d + 6 + i * 16u;
        unsigned int w = e[0] ? e[0] : 256u;    /* zero vuol dire 256 */
        unsigned int h = e[1] ? e[1] : 256u;
        unsigned int b = le16(e + 6);
        unsigned int area = w * h;

        if (area > area_max || (area == area_max && b > bit_max)) {
            area_max = area;
            bit_max  = b;
            scelta   = i;
        }
    }

    {
        const unsigned char *e = d + 6 + scelta * 16u;
        len = le32(e + 8);
        off = le32(e + 12);
    }

    /* ! I CONFINI SI CONTROLLANO CON UNA SOTTRAZIONE, NON CON UNA SOMMA:
     * `off + len > n` puo' traboccare con due numeri presi dal file e
     * diventare vero quando dovrebbe essere falso — cioe' lasciar passare
     * proprio il caso che si voleva fermare. */
    if (off >= n || len == 0 || len > n - off) return 0;

    /* Il PNG dentro l'ICO: dal 2007, e per le icone grandi e' la regola. */
    if (len > 8 && d[off] == 137 && d[off+1] == 'P' &&
        d[off+2] == 'N' && d[off+3] == 'G') {
        return eximg_png(d + off, len, bm);
    }

    return leggi_dib(d + off, len, bm);
}
