/* =============================================================================
 * lib/exfont/ttf.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il contenitore TrueType. La descrizione di cosa fa e perche' sta in ttf.h.
 *
 * ! TUTTO PASSA DA leggi16/leggi32, E NON E' PIGNOLERIA. Un font e' un file, e
 * un file puo' essere troncato, corrotto o costruito apposta. Se anche una
 * sola lettura saltasse il controllo, basterebbe uno scostamento inventato per
 * leggere memoria che non e' del font — e il rimedio non e' «stare attenti»,
 * e' non avere nessun'altra strada per leggere un numero.
 * ============================================================================= */

#include "ttf.h"

/* ! QUESTO FILE NON INCLUDE LA libc, ED E' UNA PROPRIETA' DA TENERE. Legge
 * byte e rende numeri: non alloca, non scrive, non chiama il sistema. Cosi' si
 * compila e si prova SULL'HOST con un `cc` e basta — che e' come si e'
 * verificato inflate contro zlib — invece di volere un giro di costruzione e
 * novanta secondi di avvio per ogni riga cambiata. L'unica cosa che serviva
 * era azzerare una struttura, e sono tre righe. */
static void azzera(void *p, unsigned int n)
{
    unsigned char *b = (unsigned char *)p;
    while (n--) *b++ = 0;
}

/* Il tetto alla ricorsione dei glifi composti. Un file puo' dichiarare un
 * glifo che contiene se' stesso: senza tetto sarebbe uno stack esaurito, che
 * su questo sistema vuol dire un processo morto senza spiegazione. */
#define COMPOSTI_MAX    5

/* Quanti punti puo' avere un contorno. Nessun glifo di un font vero ci arriva
 * vicino; il numero c'e' perche' i punti si contano da un campo del FILE. */
#define PUNTI_MAX       1024

/* -----------------------------------------------------------------------------
 * Le letture controllate. Fuori dai limiti rendono zero.
 *
 * ! ZERO E' UNA RISPOSTA SICURA QUI, e vale la pena dire perche'. Uno
 * scostamento zero dentro una tabella dice «all'inizio», che al massimo
 * disegna il glifo sbagliato; una lunghezza zero dice «vuoto», che fa uscire i
 * cicli. Nessuno dei due indicizza fuori.
 * --------------------------------------------------------------------------- */
static unsigned int leggi16(const unsigned char *d, unsigned int n, unsigned int o)
{
    if (o + 2 > n) return 0;
    return ((unsigned int)d[o] << 8) | (unsigned int)d[o + 1];
}

static int leggi16s(const unsigned char *d, unsigned int n, unsigned int o)
{
    unsigned int v = leggi16(d, n, o);
    return (v & 0x8000u) ? (int)v - 65536 : (int)v;
}

static unsigned int leggi32(const unsigned char *d, unsigned int n, unsigned int o)
{
    if (o + 4 > n) return 0;
    return ((unsigned int)d[o] << 24) | ((unsigned int)d[o + 1] << 16) |
           ((unsigned int)d[o + 2] << 8) | (unsigned int)d[o + 3];
}

/* Cerca una tabella nella directory. Rende 1 e riempie scostamento e
 * lunghezza, oppure 0.
 *
 * ! SI CONTROLLA CHE LA TABELLA STIA DENTRO IL FILE, qui e una volta sola. Chi
 * la usa dopo sa che l'intervallo e' buono, e non deve ricontrollarlo a ogni
 * campo — che e' il modo in cui un controllo prima o poi si dimentica. */
static int tabella(const unsigned char *d, unsigned int n, unsigned int n_tab,
                   const char *tag, unsigned int *off, unsigned int *len)
{
    unsigned int i;

    for (i = 0; i < n_tab; i++) {
        unsigned int rec = 12 + i * 16;

        if (rec + 16 > n) return 0;
        if (d[rec] == (unsigned char)tag[0] && d[rec + 1] == (unsigned char)tag[1] &&
            d[rec + 2] == (unsigned char)tag[2] && d[rec + 3] == (unsigned char)tag[3]) {
            unsigned int o = leggi32(d, n, rec + 8);
            unsigned int l = leggi32(d, n, rec + 12);

            /* o + l puo' traboccare: si confronta senza sommare. */
            if (o > n || l > n - o) return 0;
            *off = o; *len = l;
            return 1;
        }
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * La cmap: da carattere a glifo
 *
 * ! SI CERCA LA SOTTOTABELLA (3,1) — Windows, Unicode BMP — E POI (0,x). E'
 * l'ordine che usano tutti perche' e' quello che i font hanno davvero: la (3,1)
 * c'e' in ogni font pensato per Windows, cioe' in ogni font. La (3,0) e'
 * «simbolo» e mappa i caratteri in F000-F0FF: si prende per ultima e solo se
 * non c'e' altro, perche' un font di icone e' meglio di niente.
 *
 * ! E SI ACCETTA SOLO IL FORMATO 4. E' quello che copre il piano base in ogni
 * font reale. Il formato 12 serve oltre FFFF — emoji e piani alti — e questo
 * sistema dichiara Latin-1: il giorno che servira' si aggiunge, e sara' una
 * funzione accanto a questa, non una modifica a questa.
 * --------------------------------------------------------------------------- */
static void cerca_cmap(TtfFont *f, unsigned int off, unsigned int len)
{
    const unsigned char *d = f->dati;
    unsigned int         n = f->n;
    unsigned int         n_sub, i, migliore = 0, punteggio = 0;

    n_sub = leggi16(d, n, off + 2);

    for (i = 0; i < n_sub; i++) {
        unsigned int rec  = off + 4 + i * 8;
        unsigned int piat = leggi16(d, n, rec);
        unsigned int cod  = leggi16(d, n, rec + 2);
        unsigned int so   = leggi32(d, n, rec + 4);
        unsigned int p    = 0;

        if (so >= len) continue;
        if (leggi16(d, n, off + so) != 4) continue;     /* solo il formato 4 */

        if (piat == 3 && cod == 1) p = 3;               /* Unicode BMP */
        else if (piat == 0)        p = 2;               /* Unicode */
        else if (piat == 3 && cod == 0) p = 1;          /* simbolo */

        if (p > punteggio) { punteggio = p; migliore = off + so; }
    }

    if (punteggio) {
        f->off_cmap4 = migliore;
        f->len_cmap4 = leggi16(d, n, migliore + 2);
    }
}

int ttf_apri(const unsigned char *dati, unsigned int n, TtfFont *f)
{
    unsigned int versione, n_tab;
    unsigned int off, len;

    if (!f) return 0;
    azzera(f, (unsigned int)sizeof(*f));
    if (!dati || n < 12) return 0;

    versione = leggi32(dati, n, 0);

    /* 0x00010000 e' TrueType; 'true' lo usava Apple. 'OTTO' e' OpenType/CFF e
     * si rifiuta apposta — vedi ttf.h. 'ttcf' e' una raccolta di font, che
     * vorrebbe scegliere quale: non oggi. */
    if (versione != 0x00010000u && versione != 0x74727565u) return 0;

    n_tab = leggi16(dati, n, 4);
    if (n_tab == 0 || n_tab > 512) return 0;

    f->dati = dati;
    f->n    = n;

    /* --- head: la scala, e in che formato sta loca --- */
    if (!tabella(dati, n, n_tab, "head", &off, &len) || len < 54) return 0;
    f->unita_em   = leggi16(dati, n, off + 18);
    f->loca_lunga = leggi16(dati, n, off + 50) ? 1u : 0u;

    /* ! UNITA' PER EM ZERO SAREBBE UNA DIVISIONE PER ZERO nel rasterizzatore,
     * e il posto dove fermarla e' qui: il font si rifiuta, invece di produrre
     * una scala che non esiste. I valori veri sono 1000, 2048 o poco altro. */
    if (f->unita_em < 16 || f->unita_em > 16384) return 0;

    /* --- maxp: quanti glifi --- */
    if (!tabella(dati, n, n_tab, "maxp", &off, &len) || len < 6) return 0;
    f->n_glifi = leggi16(dati, n, off + 4);
    if (f->n_glifi == 0) return 0;

    /* --- hhea: le linee, e quante metriche complete ci sono --- */
    if (!tabella(dati, n, n_tab, "hhea", &off, &len) || len < 36) return 0;
    f->ascesa     = leggi16s(dati, n, off + 4);
    f->discesa    = leggi16s(dati, n, off + 6);
    f->interlinea = leggi16s(dati, n, off + 8);
    f->n_metriche = leggi16(dati, n, off + 34);
    if (f->n_metriche == 0 || f->n_metriche > f->n_glifi) return 0;

    /* --- hmtx, loca, glyf --- */
    if (!tabella(dati, n, n_tab, "hmtx", &f->off_hmtx, &f->len_hmtx)) return 0;
    if (!tabella(dati, n, n_tab, "loca", &f->off_loca, &f->len_loca)) return 0;
    if (!tabella(dati, n, n_tab, "glyf", &f->off_glyf, &f->len_glyf)) return 0;

    /* ! loca HA n_glifi + 1 VOCI, non n_glifi: l'ultima serve a sapere dove
     * FINISCE l'ultimo glifo. Un file che ne dichiara meno avrebbe l'ultimo
     * glifo lungo quanto vuole il caso. */
    {
        unsigned int voci = f->n_glifi + 1;
        unsigned int serve = f->loca_lunga ? voci * 4 : voci * 2;

        if (f->len_loca < serve) return 0;
    }

    /* --- cmap --- */
    if (!tabella(dati, n, n_tab, "cmap", &off, &len)) return 0;
    cerca_cmap(f, off, len);
    if (f->off_cmap4 == 0) return 0;

    return 1;
}

unsigned int ttf_glifo_di(const TtfFont *f, unsigned int codice)
{
    const unsigned char *d;
    unsigned int n, t, seg2, i, fine, inizio, ro, g;
    int delta;

    if (!f || !f->off_cmap4 || codice > 0xFFFFu) return 0;

    d = f->dati; n = f->n; t = f->off_cmap4;
    seg2 = leggi16(d, n, t + 6);
    if (seg2 < 2) return 0;

    /* ! LA RICERCA E' LINEARE, ED E' UNA SCELTA MISURABILE. I segmenti di un
     * font Latin sono qualche decina: una ricerca binaria su venti elementi
     * risparmia tre confronti e costa il codice per sbagliarla. E comunque
     * sopra c'e' una cache: questa funzione si chiama una volta per glifo
     * NUOVO, non a ogni disegno. */
    for (i = 0; i < seg2; i += 2) {
        fine = leggi16(d, n, t + 14 + i);
        if (codice <= fine) break;
    }
    if (i >= seg2) return 0;

    inizio = leggi16(d, n, t + 16 + seg2 + i);
    if (codice < inizio) return 0;

    delta = leggi16s(d, n, t + 16 + seg2 * 2 + i);
    ro    = leggi16(d, n, t + 16 + seg2 * 3 + i);

    if (ro == 0) {
        g = (codice + (unsigned int)delta) & 0xFFFFu;
    } else {
        /* Lo scostamento e' relativo alla POSIZIONE del campo, non alla
         * tabella: e' la stranezza storica di questo formato. */
        unsigned int pos = t + 16 + seg2 * 3 + i + ro + (codice - inizio) * 2;

        g = leggi16(d, n, pos);
        if (g != 0) g = (g + (unsigned int)delta) & 0xFFFFu;
    }

    return (g < f->n_glifi) ? g : 0;
}

unsigned int ttf_avanzamento(const TtfFont *f, unsigned int glifo)
{
    if (!f || glifo >= f->n_glifi) return 0;

    /* ! I GLIFI OLTRE n_metriche HANNO TUTTI L'AVANZAMENTO DELL'ULTIMO, ed e'
     * il modo in cui un font monospazio sta in poche centinaia di byte invece
     * che in migliaia: si scrive una metrica sola e tutti gli altri glifi ci
     * si appoggiano. Dimenticarlo dava larghezza zero a quasi tutti i glifi di
     * Liberation Mono, che e' esattamente il caso in cui si nota subito. */
    if (glifo >= f->n_metriche) glifo = f->n_metriche - 1;

    return leggi16(f->dati, f->n, f->off_hmtx + glifo * 4);
}

/* Lo scostamento e la lunghezza del contorno di un glifo dentro glyf. Rende 0
 * se il glifo e' vuoto — lo spazio lo e', e non e' un errore. */
static int glifo_dati(const TtfFont *f, unsigned int glifo,
                      unsigned int *off, unsigned int *len)
{
    unsigned int a, b;

    if (glifo >= f->n_glifi) return 0;

    if (f->loca_lunga) {
        a = leggi32(f->dati, f->n, f->off_loca + glifo * 4);
        b = leggi32(f->dati, f->n, f->off_loca + glifo * 4 + 4);
    } else {
        a = leggi16(f->dati, f->n, f->off_loca + glifo * 2) * 2;
        b = leggi16(f->dati, f->n, f->off_loca + glifo * 2 + 2) * 2;
    }

    if (b <= a) return 0;                       /* vuoto: lo spazio */
    if (b > f->len_glyf) return 0;

    *off = f->off_glyf + a;
    *len = b - a;
    return 1;
}

/* Un contorno semplice: punti con bandiere, e le curve ricavate da quali punti
 * stanno SULLA curva e quali no. */
static int contorno_semplice(const TtfFont *f, unsigned int off,
                             int dx, int dy, TtfComando *out, int max, int n_out)
{
    const unsigned char *d = f->dati;
    unsigned int         n = f->n;
    int                  n_cont, i, c, p;
    unsigned int         o, n_punti, n_istr;
    static unsigned char bandiere[PUNTI_MAX];
    static int           px[PUNTI_MAX], py[PUNTI_MAX];
    unsigned int         fine_cont[64];

    n_cont = leggi16s(d, n, off);
    if (n_cont <= 0 || n_cont > 64) return n_out;

    for (i = 0; i < n_cont; i++)
        fine_cont[i] = leggi16(d, n, off + 10 + (unsigned int)i * 2);

    n_punti = fine_cont[n_cont - 1] + 1;
    if (n_punti == 0 || n_punti > PUNTI_MAX) return n_out;

    o = off + 10 + (unsigned int)n_cont * 2;
    n_istr = leggi16(d, n, o);
    o += 2 + n_istr;                    /* le istruzioni di hinting: si saltano */

    /* Le bandiere, con la ripetizione compressa. */
    for (i = 0; i < (int)n_punti; ) {
        unsigned char b;

        if (o >= n) return n_out;
        b = d[o++];
        bandiere[i++] = b;

        if (b & 0x08) {                 /* il byte dopo dice quante ripetizioni */
            unsigned int r;

            if (o >= n) return n_out;
            r = d[o++];
            while (r-- && i < (int)n_punti) bandiere[i++] = b;
        }
    }

    /* Le x, poi le y: due passate, ognuna con il suo schema di compressione. */
    {
        int v = 0;

        for (i = 0; i < (int)n_punti; i++) {
            unsigned char b = bandiere[i];

            if (b & 0x02) {             /* un byte, segno nella bandiera */
                unsigned int u = (o < n) ? d[o] : 0; o++;
                v += (b & 0x10) ? (int)u : -(int)u;
            } else if (!(b & 0x10)) {   /* due byte con segno */
                v += leggi16s(d, n, o); o += 2;
            }                           /* altrimenti: uguale al precedente */
            px[i] = v;
        }

        v = 0;
        for (i = 0; i < (int)n_punti; i++) {
            unsigned char b = bandiere[i];

            if (b & 0x04) {
                unsigned int u = (o < n) ? d[o] : 0; o++;
                v += (b & 0x20) ? (int)u : -(int)u;
            } else if (!(b & 0x20)) {
                v += leggi16s(d, n, o); o += 2;
            }
            py[i] = v;
        }
    }

    /* Da punti a comandi, un contorno per volta.
     *
     * ! I PUNTI FUORI CURVA CONSECUTIVI HANNO UN PUNTO IMPLICITO IN MEZZO, ed
     * e' la regola che distingue un contorno giusto da uno con gli spigoli:
     * fra due controlli di seguito, il punto sulla curva e' il loro punto
     * medio. Chi la dimentica vede lettere tondeggianti diventare poligoni. */
    p = 0;
    for (c = 0; c < n_cont; c++) {
        int primo = p, ultimo = (int)fine_cont[c];
        int sx, sy, j, ho_ctrl = 0, ctlx = 0, ctly = 0;

        if (ultimo < primo || ultimo >= (int)n_punti) break;

        /* Il punto di partenza: il primo che sta SULLA curva. Se non ce n'e'
         * nessuno, il medio fra l'ultimo e il primo. */
        if (bandiere[primo] & 0x01) {
            sx = px[primo]; sy = py[primo]; j = primo + 1;
        } else if (bandiere[ultimo] & 0x01) {
            sx = px[ultimo]; sy = py[ultimo]; j = primo;
        } else {
            sx = (px[primo] + px[ultimo]) / 2;
            sy = (py[primo] + py[ultimo]) / 2;
            j = primo;
        }

        if (n_out >= max) return n_out;
        out[n_out].tipo = TTF_MOSSA;
        out[n_out].x = sx + dx; out[n_out].y = sy + dy;
        n_out++;

        for (; j <= ultimo; j++) {
            int qx = px[j], qy = py[j];

            if (bandiere[j] & 0x01) {           /* sulla curva */
                if (n_out >= max) return n_out;
                if (ho_ctrl) {
                    out[n_out].tipo = TTF_CURVA;
                    out[n_out].cx = ctlx + dx; out[n_out].cy = ctly + dy;
                    ho_ctrl = 0;
                } else {
                    out[n_out].tipo = TTF_LINEA;
                }
                out[n_out].x = qx + dx; out[n_out].y = qy + dy;
                n_out++;
            } else {                            /* fuori curva: un controllo */
                if (ho_ctrl) {
                    int mx = (ctlx + qx) / 2, my = (ctly + qy) / 2;

                    if (n_out >= max) return n_out;
                    out[n_out].tipo = TTF_CURVA;
                    out[n_out].cx = ctlx + dx; out[n_out].cy = ctly + dy;
                    out[n_out].x  = mx + dx;   out[n_out].y  = my + dy;
                    n_out++;
                }
                ctlx = qx; ctly = qy; ho_ctrl = 1;
            }
        }

        /* La chiusura torna al punto di partenza, con l'ultimo controllo se
         * ce n'e' uno in sospeso. */
        if (n_out >= max) return n_out;
        if (ho_ctrl) {
            out[n_out].tipo = TTF_CURVA;
            out[n_out].cx = ctlx + dx; out[n_out].cy = ctly + dy;
            out[n_out].x  = sx + dx;   out[n_out].y  = sy + dy;
            n_out++;
            if (n_out >= max) return n_out;
        }
        out[n_out].tipo = TTF_CHIUDI;
        out[n_out].x = sx + dx; out[n_out].y = sy + dy;
        n_out++;

        p = ultimo + 1;
    }

    return n_out;
}

static int contorno_ric(const TtfFont *f, unsigned int glifo, int dx, int dy,
                        TtfComando *out, int max, int n_out, int giu);

/* Un glifo composto: riferimenti ad altri glifi, ognuno con uno spostamento. */
static int contorno_composto(const TtfFont *f, unsigned int off, unsigned int len,
                             int dx, int dy, TtfComando *out, int max,
                             int n_out, int giu)
{
    const unsigned char *d = f->dati;
    unsigned int         n = f->n;
    unsigned int         o = off + 10;
    unsigned int         bandiere, indice;
    int                  a1, a2;

    (void)len;

    do {
        bandiere = leggi16(d, n, o);
        indice   = leggi16(d, n, o + 2);
        o += 4;

        if (bandiere & 0x0001) {            /* argomenti a 16 bit */
            a1 = leggi16s(d, n, o); a2 = leggi16s(d, n, o + 2); o += 4;
        } else {
            a1 = (int)(signed char)(o < n ? d[o] : 0);
            a2 = (int)(signed char)(o + 1 < n ? d[o + 1] : 0);
            o += 2;
        }

        /* ! LA SCALA SI SALTA, E SI DICE. I bit 0x08/0x40/0x80 portano una
         * matrice: nei font reali servono a lettere ruotate o strette che
         * Liberation non ha. Saltare i valori e' obbligatorio comunque, o lo
         * scostamento del componente successivo cadrebbe in mezzo ai numeri. */
        if (bandiere & 0x0008)      o += 2;         /* scala unica */
        else if (bandiere & 0x0040) o += 4;         /* x e y separate */
        else if (bandiere & 0x0080) o += 8;         /* matrice 2x2 */

        /* Gli argomenti sono spostamenti solo col bit 0x0002; altrimenti sono
         * numeri di punto da far combaciare, che qui non si trattano. */
        if (bandiere & 0x0002)
            n_out = contorno_ric(f, indice, dx + a1, dy + a2,
                                 out, max, n_out, giu + 1);
        else
            n_out = contorno_ric(f, indice, dx, dy, out, max, n_out, giu + 1);

    } while ((bandiere & 0x0020) && o < n && n_out < max);  /* altri componenti */

    return n_out;
}

static int contorno_ric(const TtfFont *f, unsigned int glifo, int dx, int dy,
                        TtfComando *out, int max, int n_out, int giu)
{
    unsigned int off, len;
    int          n_cont;

    if (giu > COMPOSTI_MAX) return n_out;
    if (!glifo_dati(f, glifo, &off, &len)) return n_out;
    if (len < 10) return n_out;

    n_cont = leggi16s(f->dati, f->n, off);

    if (n_cont >= 0)
        return contorno_semplice(f, off, dx, dy, out, max, n_out);

    return contorno_composto(f, off, len, dx, dy, out, max, n_out, giu);
}

int ttf_contorno(const TtfFont *f, unsigned int glifo,
                 TtfComando *out, int max)
{
    if (!f || !out || max <= 0) return 0;
    return contorno_ric(f, glifo, 0, 0, out, max, 0, 0);
}
