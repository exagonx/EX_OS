/* =============================================================================
 * lib/eximg/gif.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * GIF — 87a e 89a, il primo fotogramma
 *
 * ! IL PEZZO DIFFICILE E' LZW, E NON E' QUELLO DI zlib. Il deflate del PNG ha
 * un albero di Huffman e finestre di ripetizione; qui c'e' un DIZIONARIO CHE
 * CRESCE MENTRE SI LEGGE, e la larghezza del codice cambia sotto i piedi —
 * 9 bit, poi 10, poi 11, fino a 12 — ogni volta che il dizionario raddoppia.
 * Sono due compressioni diverse che condividono solo il nome «LZW/deflate»
 * nella testa di chi non le ha scritte.
 *
 * ! I CODICI SI LEGGONO DAL BIT MENO SIGNIFICATIVO, e attraversano i confini
 * dei blocchi: il flusso e' spezzato in pezzi lunghi al massimo 255 byte, e un
 * codice di 12 bit puo' cominciare nell'ultimo byte di un pezzo e finire nel
 * primo del pezzo dopo. Trattare i pezzi come immagini separate darebbe
 * un'immagine giusta all'inizio e spazzatura da meta' in poi — che e' il modo
 * peggiore di sbagliare, perche' sembra un problema di dati e non di codice.
 *
 * ! eximg_carica() DA' IL PRIMO FOTOGRAMMA, e resta cosi'. Le animazioni
 * (@NAV-GIF, 28 settembre 2026) passano da eximg_anima_*, in fondo al file:
 * un iteratore che compone un fotogramma alla volta. L'OROLOGIO NON STA QUI —
 * era l'obiezione scritta in questo punto fino a oggi, ed era giusta: la
 * libreria dice «il prossimo, e quanto deve restare a schermo», e quando
 * chiederlo lo decide chi ha una sveglia (il navigatore, il visualizzatore).
 *
 * ! E LA TRASPARENZA C'E', perche' senza non si vede il difetto: le GIF del web
 * sono quasi tutte icone su fondo trasparente, e senza il canale alfa
 * comparirebbero dentro un rettangolo del colore che il disegnatore aveva
 * scelto come «invisibile» — spesso nero.
 * ============================================================================= */

#include <stdlib.h>
#include "eximg_interno.h"

/* Il dizionario: 4096 voci, ognuna «il codice prima» piu' «un byte». La
 * stringa si ricostruisce all'indietro, quindi serve anche la lunghezza. */
#define GIF_CODICI      4096
#define GIF_PILA        4096

static unsigned int le16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

/* -----------------------------------------------------------------------------
 * Il flusso a blocchi: i pezzi da 255 byte visti come un nastro solo
 * --------------------------------------------------------------------------- */
typedef struct {
    const unsigned char *d;
    unsigned int         n;
    unsigned int         pos;      /* dove siamo nel file */
    unsigned int         resta;    /* byte non ancora consumati del pezzo */
    unsigned int         accum;    /* i bit in attesa */
    unsigned int         bit;      /* quanti ne abbiamo */
    int                  finito;
} Nastro;

static int prossimo_byte(Nastro *s, unsigned char *out)
{
    while (s->resta == 0) {
        if (s->pos >= s->n) { s->finito = 1; return 0; }
        s->resta = s->d[s->pos++];
        if (s->resta == 0) { s->finito = 1; return 0; }   /* blocco terminatore */
    }
    if (s->pos >= s->n) { s->finito = 1; return 0; }
    *out = s->d[s->pos++];
    s->resta--;
    return 1;
}

/* Rende -1 quando i bit finiscono: e' una fine, non un errore. */
static int leggi_codice(Nastro *s, unsigned int larghezza)
{
    while (s->bit < larghezza) {
        unsigned char b;

        if (!prossimo_byte(s, &b)) return -1;
        s->accum |= (unsigned int)b << s->bit;
        s->bit += 8;
    }
    {
        unsigned int c = s->accum & ((1u << larghezza) - 1u);

        s->accum >>= larghezza;
        s->bit    -= larghezza;
        return (int)c;
    }
}

/* -----------------------------------------------------------------------------
 * LZW: dai codici agli indici della tavolozza
 *
 * `pos` punta al byte della «misura minima» dei codici, subito dopo il
 * descrittore (e la tavolozza locale). Scrive al piu' `totale` indici e dice
 * quanti in *scritti. Rende 0 solo se la misura minima e' impossibile: un
 * flusso che finisce prima si tiene per quel che ha dato, come fa ogni
 * lettore di GIF — il resto dell'immagine resta del colore 0.
 * --------------------------------------------------------------------------- */
static int gif_lzw(const unsigned char *d, unsigned int n, unsigned int pos,
                   unsigned char *indici, unsigned int totale, unsigned int *scritti_out)
{
    static unsigned short prima[GIF_CODICI];
    static unsigned char  ultimo[GIF_CODICI];
    static unsigned char  pila[GIF_PILA];
    unsigned int  min, pulisci, fine, larghezza, prossimo, i;
    unsigned int  scritti = 0;
    int           precedente = -1, primo = 0;
    Nastro        s;

    if (pos >= n) return 0;
    min = d[pos++];
    if (min < 2 || min > 11) return 0;

    pulisci   = 1u << min;
    fine      = pulisci + 1u;
    prossimo  = fine + 1u;
    larghezza = min + 1u;

    for (i = 0; i < pulisci; i++) { prima[i] = 0xFFFF; ultimo[i] = (unsigned char)i; }

    s.d = d; s.n = n; s.pos = pos; s.resta = 0;
    s.accum = 0; s.bit = 0; s.finito = 0;

    for (;;) {
        int c = leggi_codice(&s, larghezza);
        unsigned int p_n = 0;
        int cur;

        if (c < 0) break;
        if ((unsigned int)c == fine) break;

        if ((unsigned int)c == pulisci) {
            /* ! IL CODICE DI PULIZIA RIPORTA TUTTO ALL'INIZIO, larghezza
             * compresa. Dimenticare la larghezza e' il difetto classico:
             * i primi codici dopo la pulizia si leggono con quella
             * vecchia e l'immagine diventa spazzatura da li' in poi. */
            prossimo   = fine + 1u;
            larghezza  = min + 1u;
            precedente = -1;
            continue;
        }

        cur = c;

        /* ! IL CASO «CODICE NON ANCORA NEL DIZIONARIO» ESISTE DAVVERO,
         * e non e' un file guasto: e' la sequenza KwKwK, che il
         * compressore emette prima che il decompressore abbia potuto
         * costruire quella voce. Si ricostruisce dal precedente piu' il
         * proprio primo carattere. */
        if ((unsigned int)cur >= prossimo) {
            if (precedente < 0) break;
            pila[p_n++] = (unsigned char)primo;
            cur = precedente;
        }

        while (cur >= 0 && (unsigned int)cur >= pulisci) {
            if (p_n >= GIF_PILA) { p_n = 0; break; }
            pila[p_n++] = ultimo[cur];
            cur = (prima[cur] == 0xFFFF) ? -1 : (int)prima[cur];
        }
        if (cur < 0) break;
        if (p_n >= GIF_PILA) break;
        pila[p_n++] = ultimo[cur];
        primo = ultimo[cur];

        while (p_n > 0 && scritti < totale)
            indici[scritti++] = pila[--p_n];

        if (precedente >= 0 && prossimo < GIF_CODICI) {
            prima[prossimo]  = (unsigned short)precedente;
            ultimo[prossimo] = (unsigned char)primo;
            prossimo++;
            if (prossimo == (1u << larghezza) && larghezza < 12)
                larghezza++;
        }
        precedente = c;

        if (scritti >= totale) break;
    }
    *scritti_out = scritti;
    return 1;
}

/* -----------------------------------------------------------------------------
 * Il decodificatore
 * --------------------------------------------------------------------------- */
int eximg_gif(const unsigned char *d, unsigned int n, EximgBitmap *bm)
{
    unsigned int  pos, larg, alt;
    unsigned char tav_glob[256 * 3];
    unsigned int  n_glob = 0;
    int           trasparente = -1;

    if (n < 13) return 0;
    if (d[0] != 'G' || d[1] != 'I' || d[2] != 'F' || d[3] != '8') return 0;
    if ((d[4] != '7' && d[4] != '9') || d[5] != 'a') return 0;

    larg = le16(d + 6);
    alt  = le16(d + 8);

    pos = 13;
    if (d[10] & 0x80) {
        n_glob = 2u << (d[10] & 7);
        if (pos + n_glob * 3 > n) return 0;
        { unsigned int i; for (i = 0; i < n_glob * 3; i++) tav_glob[i] = d[pos + i]; }
        pos += n_glob * 3;
    }

    /* ! I BLOCCHI SI SCORRONO FINO AL PRIMO DESCRITTORE DI IMMAGINE, e prima
     * di quello puo' esserci di tutto: commenti, testo, applicazioni. Il pezzo
     * che conta e' il Graphic Control, perche' porta l'indice trasparente. */
    while (pos < n) {
        unsigned char etichetta = d[pos];

        if (etichetta == 0x3B) return 0;               /* fine, senza immagini */

        if (etichetta == 0x21) {                       /* estensione */
            unsigned int tipo;

            if (pos + 2 > n) return 0;
            tipo = d[pos + 1];
            pos += 2;

            if (tipo == 0xF9 && pos < n && d[pos] >= 4) {
                /* Graphic Control Extension: il flag 0x01 dice che c'e' un
                 * colore trasparente, e il quarto byte quale. */
                if (pos + 5 > n) return 0;
                if (d[pos + 1] & 0x01) trasparente = (int)d[pos + 4];
            }

            /* Si saltano i sotto-blocchi, qualunque cosa siano. */
            while (pos < n && d[pos] != 0) {
                pos += 1u + d[pos];
                if (pos > n) return 0;
            }
            pos++;
            continue;
        }

        if (etichetta == 0x2C) break;                  /* eccola */

        return 0;                                       /* byte che non sta li' */
    }

    if (pos + 10 > n) return 0;

    /* Il descrittore dell'immagine. La sua misura vince su quella dello
     * schermo logico: e' l'immagine che si disegna. */
    {
        unsigned int ix = le16(d + pos + 1), iy = le16(d + pos + 2 + 1);
        unsigned int iw = le16(d + pos + 5), ih = le16(d + pos + 7);
        unsigned char flag = d[pos + 9];
        unsigned char tav[256 * 3];
        unsigned int  n_tav = n_glob;
        int           intreccia = (flag & 0x40) != 0;
        unsigned int  i;

        (void)ix; (void)iy;

        if (iw == 0 || ih == 0) { iw = larg; ih = alt; }
        if (iw == 0 || ih == 0) return 0;
        if (iw > EXIMG_LATO_MAX || ih > EXIMG_LATO_MAX) return 0;

        for (i = 0; i < n_glob * 3; i++) tav[i] = tav_glob[i];
        pos += 10;

        if (flag & 0x80) {                              /* tavolozza locale */
            n_tav = 2u << (flag & 7);
            if (pos + n_tav * 3 > n) return 0;
            for (i = 0; i < n_tav * 3; i++) tav[i] = d[pos + i];
            pos += n_tav * 3;
        }
        if (n_tav == 0) return 0;

        if (pos >= n) return 0;

        /* --- LZW ---------------------------------------------------------- */
        {
            unsigned char *indici;
            unsigned int   scritti = 0, totale = iw * ih;

            indici = (unsigned char *)eximg_memoria(totale);
            if (!indici) return 0;
            if (!gif_lzw(d, n, pos, indici, totale, &scritti)) return 0;

            /* --- dagli indici ai pixel ------------------------------------ */
            bm->px = (unsigned int *)eximg_memoria(totale * 4u);
            if (!bm->px) return 0;

            for (i = 0; i < totale; i++) {
                unsigned int idx = (i < scritti) ? indici[i] : 0u;
                unsigned int r, g, b, a = 0xFF000000u;

                if (idx >= n_tav) idx = 0;
                r = tav[idx * 3 + 0];
                g = tav[idx * 3 + 1];
                b = tav[idx * 3 + 2];
                if (trasparente >= 0 && (int)idx == trasparente) a = 0;

                bm->px[i] = a | (r << 16) | (g << 8) | b;
            }

            /* ! L'INTRECCIO SI SBROGLIA ALLA FINE, e non mentre si scrive: le
             * righe arrivano nell'ordine 0,8,16... poi 4,12... poi 2,6... poi
             * i dispari. Rimetterle a posto qui costa una passata; farlo
             * durante la decodifica vorrebbe dire l'aritmetica delle quattro
             * passate dentro il ciclo piu' delicato del file. */
            if (intreccia) {
                unsigned int *ordinata =
                    (unsigned int *)eximg_memoria(totale * 4u);
                static const unsigned int inizio[4] = { 0, 4, 2, 1 };
                static const unsigned int passo[4]  = { 8, 8, 4, 2 };
                unsigned int p, y, sorgente = 0;

                if (!ordinata) return 0;

                for (p = 0; p < 4; p++) {
                    for (y = inizio[p]; y < ih; y += passo[p]) {
                        unsigned int x;

                        for (x = 0; x < iw; x++)
                            ordinata[y * iw + x] = bm->px[sorgente * iw + x];
                        sorgente++;
                    }
                }
                for (i = 0; i < totale; i++) bm->px[i] = ordinata[i];
            }

            bm->larghezza = iw;
            bm->altezza   = ih;
            return 1;
        }
    }
}

/* =============================================================================
 * LE ANIMAZIONI (@NAV-GIF, 28 settembre 2026)
 *
 * Un iteratore: eximg_anima_apri() prende il file (e ne tiene una copia),
 * eximg_anima_passo() compone il fotogramma dopo sulla TELA — lo schermo
 * logico della GIF — e dice quanti millisecondi deve restare, a fine file
 * ricomincia. La tela e' della libreria: chi chiama la guarda, la scala, e non
 * la libera.
 *
 * ! LO SMALTIMENTO E' META' DELL'ANIMAZIONE. Ogni fotogramma dice cosa fare
 * del suo rettangolo PRIMA del prossimo: lasciarlo (0, 1), cancellarlo al
 * trasparente (2), rimettere com'era prima di lui (3). Chi lo ignora vede le
 * animazioni fatte di pezzi piccoli sporcarsi a ogni giro — la palla che
 * rimbalza lascia la scia.
 *
 * ! LA MEMORIA SI PRENDE UNA VOLTA, all'apertura: tela, copia per lo
 * smaltimento 3 e indici sono della misura dello schermo logico. Un passo non
 * alloca niente, e un'animazione che gira per ore non consuma memoria (su
 * EX-OS free() non restituisce niente al sistema: vedi @DIF-GROSSI).
 * ============================================================================= */
#define ANIMA_PIXEL_MAX (2048u * 2048u)

struct EximgAnim {
    unsigned char *d;               /* la copia del file */
    unsigned int   n;
    unsigned int   w, h;            /* lo schermo logico */
    unsigned int   inizio;          /* il primo blocco dopo la tavolozza globale */
    unsigned int   pos;             /* dove si legge il prossimo */
    unsigned char  tav_glob[256 * 3];
    unsigned int   n_glob;
    unsigned int  *tela, *salva;
    unsigned char *indici;
    int            disp;            /* lo smaltimento del fotogramma a schermo */
    unsigned int   rx, ry, rw, rh;  /* e il suo rettangolo */
    unsigned int   visti;           /* fotogrammi composti in questo giro */
};

/* Salta i sotto-blocchi da `pos`, terminatore compreso. 0 se il file finisce. */
static int salta_blocchi(const unsigned char *d, unsigned int n, unsigned int *pos)
{
    while (*pos < n && d[*pos] != 0) {
        *pos += 1u + d[*pos];
        if (*pos > n) return 0;
    }
    if (*pos >= n) return 0;
    (*pos)++;
    return 1;
}

/* Quanti descrittori d'immagine ci sono, fermandosi a due: basta sapere se e'
 * un'animazione. */
static unsigned int conta_fotogrammi(const unsigned char *d, unsigned int n, unsigned int pos)
{
    unsigned int quanti = 0;

    while (pos < n && quanti < 2) {
        if (d[pos] == 0x3B) break;
        if (d[pos] == 0x21) {
            pos += 2;
            if (!salta_blocchi(d, n, &pos)) break;
            continue;
        }
        if (d[pos] != 0x2C || pos + 10 > n) break;
        {
            unsigned char flag = d[pos + 9];
            pos += 10;
            if (flag & 0x80) pos += 3u * (2u << (flag & 7));
            pos++;                          /* la misura minima dei codici */
            if (pos > n || !salta_blocchi(d, n, &pos)) break;
        }
        quanti++;
    }
    return quanti;
}

void eximg_anima_chiudi(EximgAnim *a)
{
    if (!a) return;
    free(a->d); free(a->tela); free(a->salva); free(a->indici);
    free(a);
}

int eximg_anima_apri(const unsigned char *d, unsigned int n, EximgAnim **out)
{
    EximgAnim *a;
    unsigned int pos = 13, i, w, h;

    if (!out) return 0;
    *out = 0;
    if (!d || n < 13 || d[0] != 'G' || d[1] != 'I' || d[2] != 'F' || d[3] != '8') return 0;
    w = le16(d + 6);
    h = le16(d + 8);
    if (w == 0 || h == 0 || w > EXIMG_LATO_MAX || h > EXIMG_LATO_MAX || w * h > ANIMA_PIXEL_MAX) return 0;
    if (d[10] & 0x80) pos += 3u * (2u << (d[10] & 7));
    if (pos > n || conta_fotogrammi(d, n, pos) < 2) return 0;   /* ferma: eximg_carica basta */

    a = (EximgAnim *)calloc(1, sizeof(*a));
    if (!a) return 0;
    a->d = (unsigned char *)malloc(n);
    a->tela = (unsigned int *)malloc(w * h * 4u);
    a->salva = (unsigned int *)malloc(w * h * 4u);
    a->indici = (unsigned char *)malloc(w * h);
    if (!a->d || !a->tela || !a->salva || !a->indici) { eximg_anima_chiudi(a); return 0; }
    for (i = 0; i < n; i++) a->d[i] = d[i];
    a->n = n; a->w = w; a->h = h;
    if (d[10] & 0x80) {
        a->n_glob = 2u << (d[10] & 7);
        for (i = 0; i < a->n_glob * 3u; i++) a->tav_glob[i] = d[13 + i];
    }
    a->inizio = a->pos = pos;
    for (i = 0; i < w * h; i++) a->tela[i] = 0;   /* trasparente */
    a->disp = 0;
    *out = a;
    return 1;
}

int eximg_anima_passo(EximgAnim *a, EximgBitmap *vista, unsigned int *ms)
{
    const unsigned char *d;
    unsigned int n, giri = 0;
    int trasparente = -1, disp = 0;
    unsigned int ritardo = 0;

    if (!a || !vista) return 0;
    d = a->d; n = a->n;

    for (;;) {
        unsigned int pos = a->pos;

        /* a fine file si ricomincia; due giri a vuoto vogliono dire un file
         * senza fotogrammi leggibili, e ci si ferma */
        if (pos >= n || d[pos] == 0x3B) {
            if (a->visti == 0 || ++giri > 1) return 0;
            a->pos = a->inizio;
            a->visti = 0;
            continue;
        }
        if (d[pos] == 0x21) {
            if (pos + 2 > n) { a->pos = n; continue; }
            if (d[pos + 1] == 0xF9 && pos + 7 <= n && d[pos + 2] >= 4) {
                disp = (d[pos + 3] >> 2) & 7;
                ritardo = le16(d + pos + 4);
                trasparente = (d[pos + 3] & 0x01) ? (int)d[pos + 6] : -1;
            }
            pos += 2;
            if (!salta_blocchi(d, n, &pos)) pos = n;
            a->pos = pos;
            continue;
        }
        if (d[pos] != 0x2C || pos + 10 > n) { a->pos = n; continue; }

        {
            unsigned int ix = le16(d + pos + 1), iy = le16(d + pos + 3);
            unsigned int iw = le16(d + pos + 5), ih = le16(d + pos + 7);
            unsigned char flag = d[pos + 9];
            const unsigned char *tav = a->tav_glob;
            unsigned int n_tav = a->n_glob, scritti = 0, x, y, i;

            pos += 10;
            if (flag & 0x80) {
                n_tav = 2u << (flag & 7);
                if (pos + n_tav * 3u > n) { a->pos = n; continue; }
                tav = d + pos;
                pos += n_tav * 3u;
            }

            /* Lo smaltimento del fotogramma di PRIMA, adesso. */
            if (a->visti > 0) {
                if (a->disp == 2) {
                    for (y = a->ry; y < a->ry + a->rh && y < a->h; y++)
                        for (x = a->rx; x < a->rx + a->rw && x < a->w; x++) a->tela[y * a->w + x] = 0;
                } else if (a->disp == 3) {
                    for (i = 0; i < a->w * a->h; i++) a->tela[i] = a->salva[i];
                }
            } else {
                for (i = 0; i < a->w * a->h; i++) a->tela[i] = 0;
            }
            if (disp == 3) for (i = 0; i < a->w * a->h; i++) a->salva[i] = a->tela[i];

            /* ! UN FOTOGRAMMA PIU' GRANDE DELLA TELA SI TAGLIA: gli indici
             * sono della misura della tela, e i pixel fuori non si vedrebbero
             * comunque. */
            if (iw && ih && iw * ih <= a->w * a->h && n_tav &&
                gif_lzw(d, n, pos, a->indici, iw * ih, &scritti)) {
                int intreccia = (flag & 0x40) != 0;
                static const unsigned int inizio[4] = { 0, 4, 2, 1 };
                static const unsigned int passo[4]  = { 8, 8, 4, 2 };
                unsigned int riga_src = 0, p;

                for (p = 0; p < (intreccia ? 4u : 1u); p++) {
                    for (y = intreccia ? inizio[p] : 0; y < ih; y += intreccia ? passo[p] : 1, riga_src++) {
                        for (x = 0; x < iw; x++) {
                            unsigned int k = riga_src * iw + x, idx, tx = ix + x, ty = iy + y;
                            if (k >= scritti) continue;
                            idx = a->indici[k];
                            if ((int)idx == trasparente || idx >= n_tav) continue;
                            if (tx >= a->w || ty >= a->h) continue;
                            a->tela[ty * a->w + tx] = 0xFF000000u | ((unsigned int)tav[idx * 3] << 16) |
                                                     ((unsigned int)tav[idx * 3 + 1] << 8) | tav[idx * 3 + 2];
                        }
                    }
                }
            }

            pos++;                                  /* la misura minima */
            if (!salta_blocchi(d, n, &pos)) pos = n;
            a->pos = pos;
            a->disp = disp;
            a->rx = ix; a->ry = iy; a->rw = iw; a->rh = ih;
            a->visti++;

            vista->larghezza = a->w;
            vista->altezza   = a->h;
            vista->px        = a->tela;
            /* ! UN RITARDO DI 0 O 1 CENTESIMO NON E' «IL PIU' VELOCE POSSIBILE»:
             * i browser lo leggono come 100 ms da vent'anni, e le GIF del web
             * sono fatte contando su questo. */
            if (ms) *ms = ritardo <= 1 ? 100u : ritardo * 10u;
            return 1;
        }
    }
}
