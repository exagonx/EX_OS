/* =============================================================================
 * lib/eximg/jpg.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * JPEG baseline — il formato che non somiglia agli altri
 *
 * PNG e BMP hanno i pixel dentro, in un ordine o nell'altro. Un JPEG no: ha i
 * COEFFICIENTI di una trasformata, quantizzati e poi compressi con Huffman. Per
 * arrivare a un pixel bisogna rifare la strada al contrario tutta intera, e
 * ogni pezzo puo' sbagliare in un modo suo:
 *
 *     bit  ->  Huffman  ->  zigzag  ->  dequantizza  ->  IDCT  ->  YCbCr->RGB
 *
 * ! SOLO BASELINE SEQUENZIALE (SOF0), ED E' DICHIARATO. Il JPEG progressivo
 * (SOF2) non e' una variante del formato: e' una SECONDA decodifica, con i
 * coefficienti sparsi su piu' passate da ricomporre prima di trasformare
 * alcunche'. Vale piu' o meno quanto tutto questo file. Chi ha un progressivo
 * lo risalva sequenziale — e il messaggio glielo dice invece di disegnare
 * spazzatura.
 *
 * ! NIENTE ARITMETICO, NIENTE 12 BIT, NIENTE CMYK. Il primo non si incontra
 * (era brevettato), il secondo nemmeno, il terzo vuole anche la trasformazione
 * dei colori Adobe. Rifiutarli e' una riga; leggerli male sono immagini
 * sbagliate che sembrano un difetto nostro.
 *
 * ! E TUTTO E' A NUMERI INTERI. La virgola mobile qui costerebbe due volte: le
 * istruzioni x87 su un Pentium sono lente, e ogni processo che le usa fa
 * salvare lo stato FPU al cambio di contesto — il costo che il compositore ha
 * appena imparato a evitare con emms. Un decodificatore che disegna uno sfondo
 * non ha nessun bisogno di essere l'unico a farlo pagare a tutti.
 * ============================================================================= */

#include "eximg_interno.h"

/* -----------------------------------------------------------------------------
 * I limiti, dichiarati in un posto solo
 * --------------------------------------------------------------------------- */
#define COMP_MAX        3           /* grigio o YCbCr: il CMYK si rifiuta */
#define CAMP_MAX        2           /* fattori di campionamento 1 o 2     */
#define HUFF_TAB        4           /* la specifica ne ammette quattro    */

typedef struct {
    /* Il metodo del punto 'F.2.2.3' della specifica: per ogni lunghezza di
     * codice, il primo codice valido e il piu' grande. Decodificare vuol dire
     * aggiungere un bit per volta finche' il valore non scende sotto maxcode. */
    int          mincode[17];
    int          maxcode[18];
    int          valptr[17];
    unsigned char valori[256];
    int          presente;
} TabHuff;

typedef struct {
    int id, h, v, tq;               /* identificativo, campionamento, quant. */
    int td, ta;                     /* tabelle di Huffman DC e AC            */
    int dc_prec;                    /* il DC dell'ultimo blocco: e' differenziale */
    unsigned char *piano;           /* i campioni decodificati               */
    unsigned int   pl, pa;          /* misure del piano                      */
} Comp;

typedef struct {
    const unsigned char *d;
    unsigned int n, pos;
    unsigned int bit_buf;
    int          bit_n;
    int          finiti;            /* i dati sono finiti: si rende zero */
} Bit;

typedef struct {
    Bit           b;
    unsigned int  quant[HUFF_TAB][64];
    TabHuff       hdc[HUFF_TAB], hac[HUFF_TAB];
    Comp          comp[COMP_MAX];
    int           n_comp;
    unsigned int  larg, alt;
    int           hmax, vmax;
    unsigned int  ri;               /* intervallo di restart, in MCU */
} Jpg;

/* Lo zigzag: l'ordine in cui i 64 coefficienti stanno nel file.
 *
 * ! NON E' UN DETTAGLIO ESTETICO. Serve a mettere vicini i coefficienti che
 * dopo la quantizzazione sono quasi sempre zero, cosi' che la codifica a corse
 * li possa saltare in blocco. Sbagliarne l'ordine da' un'immagine che ha i
 * colori giusti e la forma sbagliata. */
static const unsigned char ZIGZAG[64] = {
     0, 1, 8,16, 9, 2, 3,10,
    17,24,32,25,18,11, 4, 5,
    12,19,26,33,40,48,41,34,
    27,20,13, 6, 7,14,21,28,
    35,42,49,56,57,50,43,36,
    29,22,15,23,30,37,44,51,
    58,59,52,45,38,31,39,46,
    53,60,61,54,47,55,62,63
};

/* -----------------------------------------------------------------------------
 * I bit, e i due tranelli del flusso entropico
 * --------------------------------------------------------------------------- */

/* ! UN BYTE 0xFF DENTRO I DATI E' SEGUITO DA UNO 0x00 CHE NON ESISTE. Si chiama
 * byte stuffing: 0xFF apre un marcatore, quindi un 0xFF che fa parte dei dati
 * viene scritto come FF 00 e il secondo byte va buttato leggendo. Chi non lo
 * fa perde l'allineamento dei bit al primo 0xFF — cioe' su quasi ogni foto, e
 * a meta' immagine. */
static int prendi_bit(Bit *b)
{
    if (b->bit_n == 0) {
        unsigned int c;

        if (b->pos >= b->n) { b->finiti = 1; return 0; }
        c = b->d[b->pos++];

        if (c == 0xFF) {
            unsigned int c2 = (b->pos < b->n) ? b->d[b->pos] : 0xD9;

            if (c2 == 0x00) {
                b->pos++;                   /* lo zero di riempimento */
            } else {
                /* Un marcatore vero: i dati di questa scansione sono finiti.
                 * Non si consuma, cosi' chi guarda i marcatori lo ritrova. */
                b->finiti = 1;
                b->pos--;
                return 0;
            }
        }

        b->bit_buf = c;
        b->bit_n   = 8;
    }

    b->bit_n--;
    return (int)((b->bit_buf >> b->bit_n) & 1u);
}

static int prendi_bits(Bit *b, int quanti)
{
    int v = 0;

    while (quanti-- > 0) v = (v << 1) | prendi_bit(b);
    return v;
}

/* ! IL NUMERO NON E' IN COMPLEMENTO A DUE: e' il codice 'EXTEND' della
 * specifica. Con `s` bit, i valori con il bit alto a zero sono NEGATIVI e vanno
 * riportati sotto: senza questo passaggio meta' delle differenze di luminosita'
 * hanno il segno sbagliato, e l'immagine viene a bande chiare e scure. */
static int estendi(int v, int s)
{
    if (s == 0) return 0;
    return (v < (1 << (s - 1))) ? v - (1 << s) + 1 : v;
}

static int huff_decodifica(Bit *b, const TabHuff *t)
{
    int codice = 0, l;

    if (!t->presente) return -1;

    for (l = 1; l <= 16; l++) {
        codice = (codice << 1) | prendi_bit(b);
        if (b->finiti) return -1;

        if (t->maxcode[l] >= 0 && codice <= t->maxcode[l]) {
            int i = t->valptr[l] + codice - t->mincode[l];

            if (i < 0 || i > 255) return -1;
            return t->valori[i];
        }
    }
    return -1;      /* un codice piu' lungo di 16 bit non esiste */
}

/* -----------------------------------------------------------------------------
 * L'IDCT
 *
 * ! E' LA VERSIONE SEPARABILE E DIRETTA, NON LA PIU' VELOCE, E LA SCELTA E'
 * CONSAPEVOLE. Le IDCT veloci (AAN, Loeffler) fanno lo stesso lavoro con un
 * quinto delle moltiplicazioni, ma sono una successione di passaggi che non
 * somigliano piu' alla formula: se sbagliano, si sbagliano in modo che nessuna
 * lettura del codice rivela. Questa e' la definizione, riga per colonna, e si
 * confronta con la matematica guardandola.
 *
 * Sta tutta in una funzione apposta: il giorno che un Pentium 133 vero dicesse
 * che e' troppo lenta, si sostituisce senza toccare nient'altro.
 *
 * ! E LA SCORCIATOIA CHE CONTA DAVVERO E' UN'ALTRA: un blocco in cui tutti i
 * coefficienti tranne il primo sono zero e' un blocco di colore UNIFORME, e
 * nelle immagini vere sono la maggioranza. Li' non si trasforma niente.
 * --------------------------------------------------------------------------- */

/* cos((2i+1) k pi / 16) * C(k) * 4096, con C(0) = 1/sqrt(2) gia' dentro.
 *
 * ! LA SCALA E' 4096 E NON 8192, E IL MOTIVO E' L'UNICO CHE CONTA: LA SECONDA
 * PASSATA. Le due passate moltiplicano per la tabella una volta ciascuna,
 * quindi la scala si applica due volte; con 8192 la somma delle colonne esce
 * dai 32 bit su coefficienti che un file guasto puo' benissimo contenere, e un
 * trabocco con segno non e' un numero sbagliato, e' comportamento indefinito.
 * Con 4096 e i coefficienti limitati qui sotto, il margine e' 16 volte sulle
 * righe e 2 sulle colonne — calcolato, non sperato.
 *
 * Undici bit di frazione restano molto piu' di quanto serva a un risultato che
 * finisce in un byte. */
static const short COS8[8][8] = {
 { 2896, 4017, 3784, 3406, 2896, 2276, 1567,  799},
 { 2896, 3406, 1567, -799,-2896,-4017,-3784,-2276},
 { 2896, 2276,-1567,-4017,-2896,  799, 3784, 3406},
 { 2896,  799,-3784,-2276, 2896, 3406,-1567,-4017},
 { 2896, -799,-3784, 2276, 2896,-3406,-1567, 4017},
 { 2896,-2276,-1567, 4017,-2896, -799, 3784,-3406},
 { 2896,-3406, 1567,  799,-2896, 4017,-3784, 2276},
 { 2896,-4017, 3784,-3406, 2896,-2276, 1567, -799}
};

/* ! I COEFFICIENTI SI LIMITANO PRIMA DI TRASFORMARLI, ED E' UNA DIFESA, NON UN
 * ARROTONDAMENTO. Un blocco di byte veri non produce coefficienti oltre ~1020
 * in valore assoluto: quattro volte tanto e' abbondanza per qualunque immagine
 * vera. Ma quantizzato e dequantizzato non e' un numero che decidiamo noi —
 * sono due numeri presi dal file, moltiplicati fra loro — e senza questo
 * limite un file scritto apposta sceglie l'entrata della somma qui sotto. */
#define COEF_MAX    4095

static void idct8x8(const int *in, unsigned char *fuori, unsigned int passo)
{
    int tmp[64], i, j, k;

    /* Il blocco uniforme: solo il coefficiente continuo, che si vede subito e
     * nelle immagini vere e' il caso piu' frequente. */
    for (k = 1; k < 64; k++) if (in[k]) break;
    if (k == 64) {
        int v = ((in[0] + 4) >> 3) + 128;

        if (v < 0) v = 0; else if (v > 255) v = 255;
        for (j = 0; j < 8; j++)
            for (i = 0; i < 8; i++) fuori[j * passo + i] = (unsigned char)v;
        return;
    }

    /* Le righe: dopo lo spostamento la scala e' tornata a uno. */
    for (j = 0; j < 8; j++) {
        for (i = 0; i < 8; i++) {
            int s = 0;
            for (k = 0; k < 8; k++) s += in[j * 8 + k] * COS8[i][k];
            tmp[j * 8 + i] = (s + 2048) >> 12;
        }
    }

    /* Le colonne. Qui la scala e' 4096 della tabella per il 4 che manca alla
     * formula (il fattore 1/4 davanti alla doppia somma): 2^14 in tutto. */
    for (i = 0; i < 8; i++) {
        for (j = 0; j < 8; j++) {
            int s = 0, v;
            for (k = 0; k < 8; k++) s += tmp[k * 8 + i] * COS8[j][k];
            v = ((s + 8192) >> 14) + 128;
            if (v < 0) v = 0; else if (v > 255) v = 255;
            fuori[j * passo + i] = (unsigned char)v;
        }
    }
}

/* -----------------------------------------------------------------------------
 * Un blocco: Huffman, zigzag, dequantizzazione, IDCT
 * --------------------------------------------------------------------------- */
/* Dequantizza limitando, e la moltiplicazione si fa larga.
 *
 * ! I DUE FATTORI VENGONO TUTT'E DUE DAL FILE — il coefficiente dalla
 * scansione, il passo dalla tabella di quantizzazione — e il loro prodotto sta
 * comodamente fuori dai 32 bit: 32767 per 65535 sono piu' di due miliardi. Il
 * prodotto si fa quindi a 64 bit e si limita subito: e' l'unico posto di
 * questo file dove serve, e costa una moltiplicazione larga per coefficiente
 * non nullo. */
static int dequantizza(int v, unsigned int q)
{
    long long p = (long long)v * (long long)q;

    if (p >  COEF_MAX) return  COEF_MAX;
    if (p < -COEF_MAX) return -COEF_MAX;
    return (int)p;
}

static int blocco(Jpg *j, Comp *c, unsigned char *dest, unsigned int passo)
{
    int coef[64], i, t, s, r;

    for (i = 0; i < 64; i++) coef[i] = 0;

    /* Il continuo, che e' una DIFFERENZA rispetto al blocco precedente della
     * stessa componente. */
    t = huff_decodifica(&j->b, &j->hdc[c->td]);
    if (t < 0 || t > 15) return 0;

    s = estendi(prendi_bits(&j->b, t), t);
    c->dc_prec += s;

    /* ! IL CONTINUO SI ACCUMULA DA UN BLOCCO ALL'ALTRO, quindi su un file
     * guasto cresce senza fermarsi: e' una somma di differenze che nessuno
     * controlla. Limitarlo qui costa un confronto e toglie l'unico modo che ha
     * di diventare enorme. */
    if (c->dc_prec >  32767) c->dc_prec =  32767;
    if (c->dc_prec < -32767) c->dc_prec = -32767;

    coef[0] = dequantizza(c->dc_prec, j->quant[c->tq][0]);

    /* Gli alternati, a corse di zeri. */
    for (i = 1; i < 64; ) {
        int rs = huff_decodifica(&j->b, &j->hac[c->ta]);

        if (rs < 0) return 0;

        r = rs >> 4;
        s = rs & 15;

        if (s == 0) {
            if (r != 15) break;         /* 0x00 = fine del blocco */
            i += 16;                    /* 0xF0 = sedici zeri     */
            continue;
        }

        i += r;
        if (i > 63) return 0;

        coef[ZIGZAG[i]] = dequantizza(estendi(prendi_bits(&j->b, s), s),
                                      j->quant[c->tq][i]);
        i++;
    }

    idct8x8(coef, dest, passo);
    return 1;
}

/* -----------------------------------------------------------------------------
 * YCbCr -> RGB, a interi
 *
 * ! I COEFFICIENTI SONO QUELLI DI JFIF, non quelli della televisione moderna
 * (BT.709): un JPEG dice YCbCr e intende questi. Usare gli altri da' un
 * risultato che si vede solo affiancando le due immagini, ed e' il genere di
 * errore che nessuno trova mai.
 * --------------------------------------------------------------------------- */
static unsigned int ycbcr(int y, int cb, int cr)
{
    int r, g, b;

    cb -= 128; cr -= 128;

    r = y + ((91881 * cr) >> 16);
    g = y - ((22554 * cb + 46802 * cr) >> 16);
    b = y + ((116130 * cb) >> 16);

    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;

    return ((unsigned int)r << 16) | ((unsigned int)g << 8) | (unsigned int)b;
}

/* -----------------------------------------------------------------------------
 * La scansione
 * --------------------------------------------------------------------------- */
static int scansione(Jpg *j)
{
    unsigned int mcux, mcuy, mx, my;
    int i;
    unsigned int contate = 0;

    mcux = (j->larg + (unsigned int)(j->hmax * 8) - 1u) /
           (unsigned int)(j->hmax * 8);
    mcuy = (j->alt  + (unsigned int)(j->vmax * 8) - 1u) /
           (unsigned int)(j->vmax * 8);

    for (my = 0; my < mcuy; my++) {
        for (mx = 0; mx < mcux; mx++) {

            /* ! I MARCATORI DI RIPARTENZA AZZERANO IL DC E BUTTANO I BIT
             * AVANZATI. Sono li' apposta perche' un errore di trasmissione non
             * rovini tutto il resto dell'immagine: ignorarli su un file che li
             * usa vuol dire vedere la foto giusta fino al primo e storta dopo. */
            if (j->ri && contate == j->ri) {
                unsigned int p = j->b.pos;

                j->b.bit_n = 0;
                j->b.finiti = 0;
                while (p + 1 < j->b.n &&
                       !(j->b.d[p] == 0xFF && j->b.d[p+1] >= 0xD0 &&
                         j->b.d[p+1] <= 0xD7)) p++;
                if (p + 1 < j->b.n) j->b.pos = p + 2;

                for (i = 0; i < j->n_comp; i++) j->comp[i].dc_prec = 0;
                contate = 0;
            }

            for (i = 0; i < j->n_comp; i++) {
                Comp *c = &j->comp[i];
                int bx, by;

                for (by = 0; by < c->v; by++) {
                    for (bx = 0; bx < c->h; bx++) {
                        unsigned int px = (mx * (unsigned int)c->h + (unsigned int)bx) * 8u;
                        unsigned int py = (my * (unsigned int)c->v + (unsigned int)by) * 8u;

                        if (px + 8u > c->pl || py + 8u > c->pa) return 0;

                        if (!blocco(j, c, c->piano + py * c->pl + px, c->pl))
                            return 0;
                    }
                }
            }
            contate++;
        }
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * Il file: i marcatori
 * --------------------------------------------------------------------------- */
int eximg_jpg(const unsigned char *d, unsigned int n, EximgBitmap *bm)
{
    Jpg j;
    unsigned int p = 2, i, k, x, y;
    int c, visto_sof = 0;

    if (n < 4 || d[0] != 0xFF || d[1] != 0xD8) return 0;    /* SOI */

    for (i = 0; i < HUFF_TAB; i++) {
        j.hdc[i].presente = 0;
        j.hac[i].presente = 0;
        for (k = 0; k < 64; k++) j.quant[i][k] = 1;
    }
    j.n_comp = 0; j.larg = 0; j.alt = 0; j.ri = 0;
    j.hmax = 1; j.vmax = 1;
    for (i = 0; i < COMP_MAX; i++) j.comp[i].piano = 0;

    while (p + 3 < n) {
        unsigned int len;

        if (d[p] != 0xFF) return 0;         /* fuori sincronia: non e' nostro */
        c = d[p + 1];
        p += 2;

        if (c == 0xD8 || c == 0x01 || (c >= 0xD0 && c <= 0xD7)) continue;
        if (c == 0xD9) break;               /* EOI */

        if (p + 1 >= n) return 0;
        len = ((unsigned int)d[p] << 8) | d[p + 1];
        if (len < 2 || p + len > n) return 0;

        switch (c) {
        case 0xC0:                          /* SOF0: baseline */
        case 0xC1: {                        /* SOF1: sequenziale esteso */
            unsigned int q = p + 2;

            if (len < 8) return 0;
            if (d[q] != 8) return 0;        /* solo 8 bit per campione */
            j.alt  = ((unsigned int)d[q+1] << 8) | d[q+2];
            j.larg = ((unsigned int)d[q+3] << 8) | d[q+4];
            j.n_comp = d[q+5];

            if (j.n_comp != 1 && j.n_comp != 3) return 0;
            if (j.larg == 0 || j.alt == 0) return 0;
            if (j.larg > EXIMG_LATO_MAX || j.alt > EXIMG_LATO_MAX) return 0;
            if (len < 8u + 3u * (unsigned int)j.n_comp) return 0;

            for (i = 0; i < (unsigned int)j.n_comp; i++) {
                const unsigned char *e = d + q + 6 + i * 3u;

                j.comp[i].id = e[0];
                j.comp[i].h  = e[1] >> 4;
                j.comp[i].v  = e[1] & 15;
                j.comp[i].tq = e[2];
                j.comp[i].dc_prec = 0;

                if (j.comp[i].h < 1 || j.comp[i].h > CAMP_MAX ||
                    j.comp[i].v < 1 || j.comp[i].v > CAMP_MAX ||
                    j.comp[i].tq >= HUFF_TAB) return 0;

                if (j.comp[i].h > j.hmax) j.hmax = j.comp[i].h;
                if (j.comp[i].v > j.vmax) j.vmax = j.comp[i].v;
            }
            visto_sof = 1;
            break;
        }

        /* ! IL PROGRESSIVO SI RIFIUTA PER NOME. Senza questo controllo
         * arriverebbe alla scansione, che leggerebbe i suoi coefficienti
         * parziali come se fossero completi: un'immagine di rumore invece di
         * un messaggio che dice cosa fare. */
        case 0xC2: case 0xC3: case 0xC5: case 0xC6: case 0xC7:
        case 0xC9: case 0xCA: case 0xCB: case 0xCD: case 0xCE: case 0xCF:
            return 0;

        case 0xC4: {                        /* DHT */
            unsigned int q = p + 2, fine = p + len;

            while (q < fine) {
                TabHuff *t;
                int classe, id, codice = 0, tot = 0, l;
                unsigned char conta[17];

                if (q >= n) return 0;
                classe = d[q] >> 4;
                id     = d[q] & 15;
                q++;

                if (id >= HUFF_TAB || classe > 1) return 0;
                if (q + 16u > fine) return 0;

                t = classe ? &j.hac[id] : &j.hdc[id];

                for (l = 1; l <= 16; l++) { conta[l] = d[q + l - 1]; tot += conta[l]; }
                q += 16;

                if (tot > 256 || q + (unsigned int)tot > fine) return 0;

                for (l = 0; l < tot; l++) t->valori[l] = d[q + l];
                q += (unsigned int)tot;

                /* mincode/maxcode/valptr, come nella specifica. */
                {
                    int k2 = 0;

                    for (l = 1; l <= 16; l++) {
                        if (conta[l] == 0) {
                            t->maxcode[l] = -1;
                        } else {
                            t->valptr[l]  = k2;
                            t->mincode[l] = codice;
                            k2      += conta[l];
                            codice  += conta[l];
                            t->maxcode[l] = codice - 1;
                        }
                        codice <<= 1;
                    }
                    t->maxcode[17] = 0x7FFFFFFF;
                }
                t->presente = 1;
            }
            break;
        }

        case 0xDB: {                        /* DQT */
            unsigned int q = p + 2, fine = p + len;

            while (q < fine) {
                int prec = d[q] >> 4, id = d[q] & 15;

                q++;
                if (id >= HUFF_TAB) return 0;
                if (q + (prec ? 128u : 64u) > fine) return 0;

                for (i = 0; i < 64; i++) {
                    j.quant[id][i] = prec
                        ? (((unsigned int)d[q + i*2] << 8) | d[q + i*2 + 1])
                        : d[q + i];
                }
                q += prec ? 128u : 64u;
            }
            break;
        }

        case 0xDD:                          /* DRI */
            if (len < 4) return 0;
            j.ri = ((unsigned int)d[p+2] << 8) | d[p+3];
            break;

        case 0xDA: {                        /* SOS: comincia l'immagine */
            unsigned int q = p + 2;
            int ns;

            if (!visto_sof) return 0;
            if (len < 6) return 0;

            ns = d[q];
            if (ns != j.n_comp) return 0;
            if (len < 6u + 2u * (unsigned int)ns) return 0;

            for (i = 0; i < (unsigned int)ns; i++) {
                int id = d[q + 1 + i*2], td = d[q + 2 + i*2] >> 4;
                int ta = d[q + 2 + i*2] & 15;
                int trovata = 0;

                if (td >= HUFF_TAB || ta >= HUFF_TAB) return 0;

                for (k = 0; k < (unsigned int)j.n_comp; k++) {
                    if (j.comp[k].id == id) {
                        j.comp[k].td = td;
                        j.comp[k].ta = ta;
                        trovata = 1;
                    }
                }
                if (!trovata) return 0;
            }

            /* I piani, uno per componente, arrotondati all'MCU intera.
             *
             * ! SI ALLOCA PER MCU INTERE E NON PER LA MISURA DELL'IMMAGINE.
             * Un'immagine 33x17 in 4:2:0 ha MCU da 16x16: l'ultima colonna e
             * l'ultima riga di blocchi escono dal bordo, e sono blocchi che
             * ESISTONO nel file e vanno decodificati. Allocare la misura vera
             * vorrebbe dire scrivere fuori proprio sull'ultimo blocco. */
            for (i = 0; i < (unsigned int)j.n_comp; i++) {
                Comp *cc = &j.comp[i];
                unsigned int mcux = (j.larg + (unsigned int)(j.hmax*8) - 1u) /
                                    (unsigned int)(j.hmax*8);
                unsigned int mcuy = (j.alt  + (unsigned int)(j.vmax*8) - 1u) /
                                    (unsigned int)(j.vmax*8);

                cc->pl = mcux * (unsigned int)cc->h * 8u;
                cc->pa = mcuy * (unsigned int)cc->v * 8u;
                cc->piano = (unsigned char *)eximg_memoria(cc->pl * cc->pa);
                if (!cc->piano) return 0;
            }

            j.b.d = d; j.b.n = n; j.b.pos = q + 1u + 2u*(unsigned int)ns + 3u;
            j.b.bit_buf = 0; j.b.bit_n = 0; j.b.finiti = 0;

            if (!scansione(&j)) return 0;
            goto pronto;
        }

        default:
            break;                          /* APPn, COM e il resto: si salta */
        }

        p += len;
    }

    return 0;                               /* niente SOS: non c'e' immagine */

pronto:
    bm->larghezza = j.larg;
    bm->altezza   = j.alt;
    bm->px = (unsigned int *)eximg_memoria(j.larg * j.alt * 4u);
    if (!bm->px) return 0;

    for (y = 0; y < j.alt; y++) {
        for (x = 0; x < j.larg; x++) {
            if (j.n_comp == 1) {
                unsigned int v = j.comp[0].piano[y * j.comp[0].pl + x];

                bm->px[y * j.larg + x] = (v << 16) | (v << 8) | v;
            } else {
                /* ! IL CROMA SI RIPETE, NON SI INTERPOLA. Con 4:2:0 c'e' un
                 * campione di colore ogni quattro pixel, e stenderlo a
                 * ripetizione da' i bordi colorati a scaletta che si vedono
                 * negli ingrandimenti. Interpolare e' meglio e costa un altro
                 * pezzo di codice: quando servira', si tocca solo qui. */
                unsigned int vy, vb, vr;
                Comp *cy = &j.comp[0], *cb = &j.comp[1], *cr = &j.comp[2];

                vy = cy->piano[(y * (unsigned int)cy->v / (unsigned int)j.vmax) * cy->pl +
                               (x * (unsigned int)cy->h / (unsigned int)j.hmax)];
                vb = cb->piano[(y * (unsigned int)cb->v / (unsigned int)j.vmax) * cb->pl +
                               (x * (unsigned int)cb->h / (unsigned int)j.hmax)];
                vr = cr->piano[(y * (unsigned int)cr->v / (unsigned int)j.vmax) * cr->pl +
                               (x * (unsigned int)cr->h / (unsigned int)j.hmax)];

                bm->px[y * j.larg + x] = ycbcr((int)vy, (int)vb, (int)vr);
            }
        }
    }

    return 1;
}
