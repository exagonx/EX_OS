/* =============================================================================
 * lib/exfont/ttf.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * TrueType — il contenitore e le metriche
 *
 * Questo file legge la STRUTTURA di un font TrueType: quali tabelle ci sono,
 * quale glifo disegna un carattere, quanto e' largo, e dove stanno i suoi
 * contorni. Non disegna niente — quello e' raster.c.
 *
 * ! IL FONT E' UN FILE, E UN FILE PUO' MENTIRE. Ogni scostamento che si legge
 * qui dentro finisce in un puntatore, e ogni numero in un conto che indicizza.
 * Un font arriva da un CD, ma domani arrivera' da una pagina web: la regola e'
 * che NESSUN valore letto dal file viene usato prima di essere controllato
 * contro la lunghezza vera dei dati. Le funzioni leggi16/leggi32 rendono zero
 * fuori dai limiti invece di leggere, cosi' un file troncato produce un font
 * vuoto e non una lettura a spasso per la memoria.
 *
 * ! SI LEGGE SOLO CIO' CHE SERVE, e va detto quali tabelle sono:
 *
 *     head   unita' per em, e in che formato sta «loca»
 *     maxp   quanti glifi ci sono
 *     hhea   quante metriche orizzontali, e le linee di ascesa e discesa
 *     hmtx   la larghezza d'avanzamento di ogni glifo
 *     loca   dove comincia il contorno di ogni glifo dentro «glyf»
 *     glyf   i contorni
 *     cmap   da carattere a numero di glifo
 *
 * Non si leggono `kern` ne' `GPOS` (le coppie con spaziatura corretta), ne'
 * `GSUB` (le legature): sono migliorie della resa, non condizioni per vedere
 * del testo, e si aggiungono senza toccare niente di quello che c'e'.
 *
 * ! IL CFF NON SI LEGGE, E NON E' UNA DIMENTICANZA. Un file «OTTO» ha i
 * contorni in curve cubiche dentro una tabella CFF, che e' un formato diverso
 * con un suo interprete di programmi PostScript. Liberation e' TrueType vero
 * — contorni quadratici in `glyf` — e per il resto e' meglio dire «non lo so
 * leggere» che leggere male.
 *
 * ! LE COORDINATE ESCONO IN UNITA' DEL FONT, non in pixel. La conversione la
 * fa chi rasterizza, che e' l'unico a sapere a che corpo sta disegnando; farla
 * qui vorrebbe dire rileggere il font a ogni cambio di misura.
 * ============================================================================= */
#ifndef TTF_H
#define TTF_H

#ifdef __cplusplus
extern "C" {
#endif

/* Un font aperto. I puntatori guardano DENTRO i dati passati a ttf_apri():
 * valgono finche' valgono quelli, esattamente come per il formato EXFN. */
typedef struct {
    const unsigned char *dati;
    unsigned int         n;

    unsigned int unita_em;      /* unita' per em: la scala del disegno */
    int          ascesa;        /* in unita' del font */
    int          discesa;       /* negativo, in unita' del font */
    int          interlinea;    /* lineGap */

    unsigned int n_glifi;
    unsigned int n_metriche;    /* quante voci complete ha hmtx */
    unsigned int loca_lunga;    /* 1 = scostamenti a 32 bit */

    unsigned int off_hmtx, len_hmtx;
    unsigned int off_loca, len_loca;
    unsigned int off_glyf, len_glyf;
    unsigned int off_cmap4;     /* la sottotabella scelta, formato 4 */
    unsigned int len_cmap4;
} TtfFont;

/* Un contorno estratto, gia' in punti e comandi. Le curve sono QUADRATICHE:
 * un punto di controllo solo, che e' cio' che distingue TrueType dal
 * PostScript. */
#define TTF_MOSSA       0       /* alza la penna e vai */
#define TTF_LINEA       1
#define TTF_CURVA       2       /* controllo in (cx,cy), fine in (x,y) */
#define TTF_CHIUDI      3

typedef struct {
    unsigned char tipo;
    int           x, y;         /* unita' del font */
    int           cx, cy;       /* solo per TTF_CURVA */
} TtfComando;

/* Riconosce un TrueType e prepara le tabelle. Rende 1 se il file e'
 * utilizzabile, 0 altrimenti — e su 0 la struttura resta azzerata.
 *
 * ! RENDE 0 ANCHE PER UN OpenType/CFF VALIDO, apposta: vedi il commento sopra.
 * «Non lo so leggere» e' una risposta, leggerlo male non lo e'. */
int ttf_apri(const unsigned char *dati, unsigned int n, TtfFont *f);

/* Da carattere Unicode a numero di glifo. Rende 0 — che nel TrueType e' il
 * glifo «mancante», il rettangolo vuoto — se il font non ha quel codice. */
unsigned int ttf_glifo_di(const TtfFont *f, unsigned int codice);

/* Di quanto avanzare dopo aver disegnato quel glifo, in unita' del font. */
unsigned int ttf_avanzamento(const TtfFont *f, unsigned int glifo);

/* Estrae i comandi di disegno di un glifo dentro `out`, al massimo `max`.
 * Rende quanti ne ha scritti, 0 per un glifo vuoto — lo spazio ha un contorno
 * vuoto, ed e' normale.
 *
 * ! I GLIFI COMPOSTI SI ESPANDONO QUI DENTRO. Una «a» con l'accento e' un
 * riferimento alla «a» piu' uno all'accento, con uno spostamento: chi
 * rasterizza riceve i contorni gia' messi al loro posto e non deve sapere che
 * esistono. La ricorsione ha un tetto, perche' un file puo' dichiarare un
 * glifo composto che contiene se' stesso. */
int ttf_contorno(const TtfFont *f, unsigned int glifo,
                 TtfComando *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* TTF_H */
