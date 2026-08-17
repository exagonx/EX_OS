/* =============================================================================
 * lib/eximg/inflate.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * DEFLATE (RFC 1951) — quello che serve a leggere un PNG
 *
 * ! NON E' zlib, E NON DEVE ESSERLO. Qui si DECOMPRIME soltanto: comprimere e'
 * l'altra meta' del formato e non serve a nessuno per mostrare un'immagine.
 * Duecento righe invece di ventimila, e non c'e' niente da mantenere che non
 * venga esercitato ogni volta che si apre un file.
 *
 * ! I DATI VENGONO DA FUORI, E OGNI LETTURA E' CONTROLLATA. Un decompressore e'
 * il punto piu' esposto di un lettore di immagini: interpreta lunghezze e
 * distanze scritte da qualcun altro, e una distanza piu' grande di quanto si e'
 * gia' prodotto e' esattamente il modo in cui si legge memoria altrui. Qui ogni
 * indice si confronta prima di essere usato, e al primo che non torna si smette
 * — con un errore, non con un dato inventato.
 *
 * ! E LA MEMORIA E' DEL CHIAMANTE. inflate() non alloca niente: riceve il
 * buffer di uscita e la sua misura, e se non basta si ferma. Su EX-OS `free()`
 * non restituisce niente, quindi una funzione che alloca dentro un ciclo e'
 * una perdita permanente.
 * ============================================================================= */

#include "inflate.h"

/* -----------------------------------------------------------------------------
 * Il lettore di bit
 *
 * ! DEFLATE LEGGE I BIT DAL MENO SIGNIFICATIVO, ma i codici di Huffman si
 * leggono dal PIU' significativo. Sono due convenzioni opposte nello stesso
 * formato, ed e' l'errore classico: chi le confonde ottiene un flusso che
 * comincia bene e degenera dopo qualche byte.
 * --------------------------------------------------------------------------- */
typedef struct {
    const unsigned char *dati;
    unsigned int         n;        /* byte disponibili */
    unsigned int         pos;      /* byte corrente */
    unsigned int         bit;      /* bit gia' consumati del byte corrente */
    int                  finito;   /* 1 = dati esauriti: da qui in poi errore */
} Bit;

static unsigned int bit1(Bit *b)
{
    unsigned int v;

    if (b->pos >= b->n) { b->finito = 1; return 0; }

    v = (b->dati[b->pos] >> b->bit) & 1u;
    if (++b->bit == 8u) { b->bit = 0; b->pos++; }
    return v;
}

static unsigned int bitn(Bit *b, unsigned int quanti)
{
    unsigned int v = 0, i;

    for (i = 0; i < quanti; i++) v |= bit1(b) << i;
    return v;
}

/* -----------------------------------------------------------------------------
 * Un albero di Huffman canonico
 *
 * ! NON SI COSTRUISCE UN ALBERO DI PUNTATORI. DEFLATE usa codici CANONICI: date
 * le lunghezze, i codici sono determinati. Bastano due tabelle — quanti codici
 * per lunghezza, e i simboli in ordine — e la decodifica e' un ciclo che
 * aggiunge un bit alla volta. Un albero vero costerebbe una allocazione per
 * nodo, e qui non si liberano.
 * --------------------------------------------------------------------------- */
#define LUNG_MAX    16      /* le lunghezze vanno da 1 a 15 */
#define SIMB_MAX    288     /* il massimo dell'alfabeto letterale/lunghezza */

typedef struct {
    unsigned short conta[LUNG_MAX];
    unsigned short simbolo[SIMB_MAX];
} Huff;

/* Rende 0 se le lunghezze descrivono un albero valido. */
static int huff_costruisci(Huff *h, const unsigned char *lung, unsigned int n)
{
    unsigned int i, sinistra, offs[LUNG_MAX];

    for (i = 0; i < LUNG_MAX; i++) h->conta[i] = 0;
    for (i = 0; i < n; i++) h->conta[lung[i]]++;

    /* La lunghezza 0 vuol dire «simbolo non usato»: non e' un codice. */
    h->conta[0] = 0;

    /* ! SI CONTROLLA CHE L'ALBERO SIA COMPLETO E NON SOVRACCARICO. Un albero
     * sovraccarico — piu' codici di quanti ne stiano in quella lunghezza —
     * viene da dati corrotti o costruiti apposta, e decodificarlo darebbe
     * simboli presi da fuori tabella. */
    sinistra = 1;
    for (i = 1; i < LUNG_MAX; i++) {
        sinistra <<= 1;
        if (h->conta[i] > sinistra) return -1;
        sinistra -= h->conta[i];
    }

    offs[1] = 0;
    for (i = 1; i < LUNG_MAX - 1; i++) offs[i + 1] = offs[i] + h->conta[i];

    for (i = 0; i < n; i++)
        if (lung[i]) h->simbolo[offs[lung[i]]++] = (unsigned short)i;

    return 0;
}

/* Rende il simbolo, o -1 se il flusso e' finito o il codice non esiste. */
static int huff_leggi(Bit *b, const Huff *h)
{
    int codice = 0, primo = 0, indice = 0;
    unsigned int l;

    for (l = 1; l < LUNG_MAX; l++) {
        codice |= (int)bit1(b);
        if (b->finito) return -1;

        {
            int conta = h->conta[l];

            if (codice - primo < conta) return h->simbolo[indice + (codice - primo)];
            indice += conta;
            primo   = (primo + conta) << 1;
            codice <<= 1;
        }
    }
    return -1;      /* piu' di 15 bit: non e' un codice valido */
}

/* -----------------------------------------------------------------------------
 * Le tabelle fisse di RFC 1951, e quelle delle lunghezze e distanze
 * --------------------------------------------------------------------------- */
static const unsigned short LUNG_BASE[29] = {
      3,  4,  5,  6,  7,  8,  9, 10, 11, 13, 15, 17, 19, 23, 27,
     31, 35, 43, 51, 59, 67, 83, 99,115,131,163,195,227,258
};
static const unsigned char LUNG_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0
};
static const unsigned short DIST_BASE[30] = {
       1,   2,   3,   4,   5,   7,   9,  13,   17,   25,
      33,  49,  65,  97, 129, 193, 257, 385,  513,  769,
    1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};
static const unsigned char DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};

/* L'ordine bizzarro in cui DEFLATE scrive le lunghezze dei codici di codice.
 * Non e' arbitrario: mette per primi quelli piu' probabili, cosi' la coda si
 * puo' omettere. */
static const unsigned char ORDINE[19] = {
    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15
};

/* -----------------------------------------------------------------------------
 * Un blocco compresso, dati i due alberi
 * --------------------------------------------------------------------------- */
static int blocco(Bit *b, const Huff *lett, const Huff *dist,
                  unsigned char *out, unsigned int max, unsigned int *scritti)
{
    for (;;) {
        int s = huff_leggi(b, lett);

        if (s < 0) return -1;
        if (s == 256) return 0;                 /* fine del blocco */

        if (s < 256) {
            if (*scritti >= max) return -1;     /* uscita piena */
            out[(*scritti)++] = (unsigned char)s;
            continue;
        }

        /* Una coppia lunghezza/distanza: si ricopia da cio' che si e' gia'
         * prodotto. */
        {
            unsigned int i, lung, d;
            int sd;

            s -= 257;
            if (s >= 29) return -1;
            lung = LUNG_BASE[s] + bitn(b, LUNG_EXTRA[s]);

            sd = huff_leggi(b, dist);
            if (sd < 0 || sd >= 30) return -1;
            d = DIST_BASE[sd] + bitn(b, DIST_EXTRA[sd]);

            /* ! LA DISTANZA NON PUO' SUPERARE QUANTO SI E' PRODOTTO, ed e' il
             * controllo piu' importante di tutto il file: senza, un flusso
             * costruito apposta farebbe leggere memoria PRIMA del buffer di
             * uscita — e ricopiarcela dentro, cioe' consegnarla a chi guarda
             * l'immagine. */
            if (d == 0 || d > *scritti) return -1;
            if (*scritti + lung > max)   return -1;

            for (i = 0; i < lung; i++) {
                out[*scritti] = out[*scritti - d];
                (*scritti)++;
            }
        }

        if (b->finito) return -1;
    }
}

/* -----------------------------------------------------------------------------
 * inflate — l'unica funzione pubblica
 * --------------------------------------------------------------------------- */
int inflate(const unsigned char *dati, unsigned int n,
            unsigned char *out, unsigned int max, unsigned int *prodotti)
{
    Bit b;
    unsigned int scritti = 0;
    Huff lett, dist;
    unsigned char lung[SIMB_MAX + 32];
    unsigned int i;

    if (!dati || !out || !prodotti) return -1;

    b.dati = dati; b.n = n; b.pos = 0; b.bit = 0; b.finito = 0;
    *prodotti = 0;

    for (;;) {
        unsigned int ultimo = bit1(&b);
        unsigned int tipo   = bitn(&b, 2);

        if (b.finito) return -1;

        if (tipo == 0) {
            /* Non compresso: si allinea al byte, poi lunghezza e complemento. */
            unsigned int len;

            if (b.bit) { b.bit = 0; b.pos++; }
            if (b.pos + 4 > b.n) return -1;

            len = (unsigned int)b.dati[b.pos] | ((unsigned int)b.dati[b.pos+1] << 8);
            /* Il complemento c'e' apposta per accorgersi di un flusso rotto:
             * controllarlo costa due righe e prende un intero blocco storto. */
            if ((len ^ 0xFFFFu) != ((unsigned int)b.dati[b.pos+2] |
                                    ((unsigned int)b.dati[b.pos+3] << 8)))
                return -1;
            b.pos += 4;

            if (b.pos + len > b.n || scritti + len > max) return -1;
            for (i = 0; i < len; i++) out[scritti++] = b.dati[b.pos++];

        } else if (tipo == 1) {
            /* Alberi FISSI: le lunghezze sono scritte nella specifica. */
            for (i = 0;   i < 144; i++) lung[i] = 8;
            for (i = 144; i < 256; i++) lung[i] = 9;
            for (i = 256; i < 280; i++) lung[i] = 7;
            for (i = 280; i < 288; i++) lung[i] = 8;
            if (huff_costruisci(&lett, lung, 288) != 0) return -1;

            for (i = 0; i < 30; i++) lung[i] = 5;
            if (huff_costruisci(&dist, lung, 30) != 0) return -1;

            if (blocco(&b, &lett, &dist, out, max, &scritti) != 0) return -1;

        } else if (tipo == 2) {
            /* Alberi DINAMICI: prima si legge l'albero che descrive gli
             * alberi. E' il pezzo che si sbaglia piu' facilmente, e la ragione
             * e' che i codici 16, 17 e 18 non sono lunghezze: sono RIPETIZIONI
             * di quello che viene prima o di zeri. */
            unsigned int hlit, hdist, hclen, k = 0;
            Huff cl;
            unsigned char cllung[19];

            hlit  = bitn(&b, 5) + 257;
            hdist = bitn(&b, 5) + 1;
            hclen = bitn(&b, 4) + 4;
            if (b.finito || hlit > 286 || hdist > 30) return -1;

            for (i = 0; i < 19; i++) cllung[i] = 0;
            for (i = 0; i < hclen; i++) cllung[ORDINE[i]] = (unsigned char)bitn(&b, 3);
            if (huff_costruisci(&cl, cllung, 19) != 0) return -1;

            while (k < hlit + hdist) {
                int s = huff_leggi(&b, &cl);
                unsigned int ripeti, valore;

                if (s < 0) return -1;

                if (s < 16) { lung[k++] = (unsigned char)s; continue; }

                if (s == 16) {
                    if (k == 0) return -1;      /* niente da ripetere */
                    valore = lung[k - 1];
                    ripeti = 3 + bitn(&b, 2);
                } else if (s == 17) {
                    valore = 0;
                    ripeti = 3 + bitn(&b, 3);
                } else {
                    valore = 0;
                    ripeti = 11 + bitn(&b, 7);
                }

                if (k + ripeti > hlit + hdist) return -1;
                while (ripeti--) lung[k++] = (unsigned char)valore;
            }

            if (huff_costruisci(&lett, lung, hlit) != 0) return -1;
            if (huff_costruisci(&dist, lung + hlit, hdist) != 0) return -1;
            if (blocco(&b, &lett, &dist, out, max, &scritti) != 0) return -1;

        } else {
            return -1;      /* tipo 3: non esiste */
        }

        if (ultimo) break;
        if (b.finito) return -1;
    }

    *prodotti = scritti;
    return 0;
}
