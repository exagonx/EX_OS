/* =============================================================================
 * lib/exrtf/exrtf.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * A DOCUMENT WITH STYLES, AND RTF IN AND OUT (@RTF, 30 September 2026)
 *
 * The model exeditor's RTF mode works on, and that the text control of the
 * toolkit will draw:
 *
 *   - the TEXT, in UTF-8, one string; a paragraph ends with '\n';
 *   - the RUNS: pieces of text that share one style, in order, covering the
 *     text from start to end without holes;
 *   - the PARAGRAPHS: one alignment for each '\n'-terminated line (and one
 *     for the last line, which may have no '\n').
 *
 * A style is what the "base" RTF mode offers (asked by the user): typeface,
 * size, bold, italic, underline, colour. The typeface is one of the three
 * families the toolkit has (serif, sans, mono), and the name read from the
 * file is kept so that saving writes it back.
 *
 * ! WHAT IS READ AND WHAT IS SKIPPED. Read: \b \i \ul \ulnone \plain \f \fs
 * \cf \par \line \tab \pard \ql \qc \qr \qj, \li \ri \fi \tx \sb \sa \sl, the
 * page (\paperw \paperh \margl \margr \margt \margb), the font and colour
 * tables, \'hh (Windows-1252) and \uN (Unicode). Skipped whole, with their groups: \*
 * destinations, \stylesheet, \info, \pict, \header, \footer, \object and
 * similar. A document with tables or pictures opens with their text and
 * without them — the "base" mode is declared, not discovered.
 *
 * All the memory is the caller's: exrtf_prepara hands the buffers in, as
 * html_prepara does, so the library allocates nothing.
 * ============================================================================= */
#ifndef EXRTF_H
#define EXRTF_H

#ifdef __cplusplus
extern "C" {
#endif

#define EXRTF_SERIF     0
#define EXRTF_SANS      1
#define EXRTF_MONO      2

#define EXRTF_SINISTRA  0
#define EXRTF_CENTRO    1
#define EXRTF_DESTRA    2
#define EXRTF_GIUSTO    3

#define EXRTF_NOME_MAX  32

typedef struct {
    unsigned char  famiglia;        /* EXRTF_SERIF / _SANS / _MONO */
    unsigned char  grassetto, corsivo, sottolineato;
    unsigned short corpo;           /* in points */
    unsigned int   colore;          /* 0x00RRGGBB */
    unsigned char  font;            /* index in the document's font names */
} ExRtfStile;

typedef struct {
    unsigned int inizio, lung;      /* bytes of the text */
    ExRtfStile   stile;
} ExRtfPezzo;

/* =============================================================================
 * THE PARAGRAPH (9 October 2026: indents, tab stops, spacing)
 *
 * Until now a paragraph was one byte, its alignment. The user asked for the
 * ruler of a word processor, and a ruler is the picture of these numbers.
 * Lengths are in TWIPS, as in RTF and in Word (1440 to the inch, 567 to the
 * centimetre): the file's own unit, so reading and writing lose nothing.
 *
 *   sin, des   the left and right indent, from the page margins;
 *   prima      the first line, RELATIVE to sin: negative is a hanging indent;
 *   tab[]      the tab stops, from the left margin, ascending; past the last
 *              one the default stops go on (EXRTF_TAB_PASSO);
 *   interlinea 0 or 100 single, 150, 200: per cent of the line's height;
 *   sp_prima, sp_dopo   space above and below the paragraph.
 * ============================================================================= */
#define EXRTF_TAB_MAX    10
#define EXRTF_TAB_PASSO  720        /* default tab stops: half an inch */
#define EXRTF_CM         567        /* twips in a centimetre */

typedef struct {
    unsigned char  allinea;         /* EXRTF_SINISTRA.. */
    unsigned char  tab_n;
    unsigned char  interlinea;
    unsigned char  riserva;
    short          sin, des, prima;
    unsigned short sp_prima, sp_dopo;
    unsigned short tab[EXRTF_TAB_MAX];
} ExRtfPar;

typedef struct {
    char          *testo;           /* UTF-8, '\0'-terminated */
    unsigned int   testo_n, testo_max;
    ExRtfPezzo    *pezzi;
    unsigned int   pezzi_n, pezzi_max;
    ExRtfPar      *par;             /* one per paragraph */
    unsigned int   par_n, par_max;
    /* The page, in twips: A4 with 2 cm margins until the file says otherwise. */
    unsigned int   carta_w, carta_h;
    unsigned int   marg_sin, marg_des, marg_su, marg_giu;
    char           font_nome[16][EXRTF_NOME_MAX];
    unsigned char  font_fam[16];
    unsigned int   font_n;
    int            troncato;        /* a buffer was too small */
} ExRtfDoc;

/* Hands the buffers in and empties the document. */
void exrtf_prepara(ExRtfDoc *d, char *testo, unsigned int testo_max,
                   ExRtfPezzo *pezzi, unsigned int pezzi_max,
                   ExRtfPar *par, unsigned int par_max);

/* A paragraph with nothing set: left, no indents, no stops, single spacing. */
void exrtf_par_base(ExRtfPar *p);

/* The default style: serif, 12 points, black, nothing on. */
void exrtf_stile_base(ExRtfStile *s);

/* Appends text with a style, merging with the last run when the style is the
 * same; a '\n' in the text starts a new paragraph with the same alignment. */
void exrtf_aggiungi(ExRtfDoc *d, const char *t, unsigned int n, const ExRtfStile *s);

/* Reads RTF. 1 = read (maybe partly: see troncato), 0 = not RTF at all. */
int  exrtf_leggi(ExRtfDoc *d, const char *rtf, unsigned int n);

/* Writes RTF into out (at most max bytes, '\0'-terminated). Returns the
 * length, or 0 if it did not fit. */
unsigned int exrtf_scrivi(const ExRtfDoc *d, char *out, unsigned int max);

/* Reads a Word 6 or Word 95 .doc (lib/exrtf/doc95.c says what of it). `lavoro`
 * is working memory as big as the file: the text stream is copied there.
 * 1 = read; 0 = not such a file; -1 = Word 97 or later; -2 = encrypted;
 * -3 = damaged. */
int  exrtf_leggi_doc(ExRtfDoc *d, const unsigned char *file, unsigned int n,
                     unsigned char *lavoro, unsigned int lavoro_max);

/* 1 if the bytes look like RTF ("{\rtf"), to choose the mode on opening. */
int  exrtf_e_rtf(const char *t, unsigned int n);

/* =============================================================================
 * EDITING (@RTF stage 2). Positions are byte offsets in the text. Every call
 * keeps the model's rules: the runs cover the text in order, none is empty,
 * no two neighbours have the same style; there is one alignment for each '\n'
 * plus one. When a buffer is full the call does less (see troncato).
 * ============================================================================= */

/* Inserts n bytes at pos with style s. Returns how many went in. */
unsigned int exrtf_inserisci(ExRtfDoc *d, unsigned int pos, const char *t,
                             unsigned int n, const ExRtfStile *s);

/* Removes the bytes from da to a (a excluded). */
void exrtf_cancella(ExRtfDoc *d, unsigned int da, unsigned int a);

/* The style of the byte at pos; at the end of the text, the last byte's.
 * 0 if the document is empty (s is left as it is). */
int  exrtf_stile_a(const ExRtfDoc *d, unsigned int pos, ExRtfStile *s);

/* What exrtf_cambia changes. For the three switches the value is 0 or 1;
 * EXRTF_C_FAMIGLIA also picks (or adds) a font of that family in the
 * document's table, so that saving writes the right name. */
#define EXRTF_C_GRASSETTO     1
#define EXRTF_C_CORSIVO       2
#define EXRTF_C_SOTTOLINEATO  3
#define EXRTF_C_FAMIGLIA      4
#define EXRTF_C_CORPO         5
#define EXRTF_C_COLORE        6

/* Changes one thing of the style from da to a. */
void exrtf_cambia(ExRtfDoc *d, unsigned int da, unsigned int a, int cosa, unsigned int valore);

/* The same change on a lone style (the one the next typed letter will have). */
void exrtf_cambia_stile(ExRtfDoc *d, ExRtfStile *s, int cosa, unsigned int valore);

/* The paragraph the byte at pos is in (the '\n' ending it counts as in it). */
unsigned int exrtf_paragrafo(const ExRtfDoc *d, unsigned int pos);

/* Sets the alignment of every paragraph from da to a. */
void exrtf_allinea(ExRtfDoc *d, unsigned int da, unsigned int a, unsigned int allineamento);

/* What exrtf_par_cambia changes, on every paragraph from da to a. Values in
 * twips (EXRTF_P_INTERLINEA: per cent). Indents are kept inside the page. */
#define EXRTF_P_SIN         1
#define EXRTF_P_DES         2
#define EXRTF_P_PRIMA       3
#define EXRTF_P_INTERLINEA  4
#define EXRTF_P_SP_PRIMA    5
#define EXRTF_P_SP_DOPO     6

void exrtf_par_cambia(ExRtfDoc *d, unsigned int da, unsigned int a, int cosa, int valore);

/* Tab stops of the paragraphs from da to a: adds one at `twips` (kept in
 * order, no two closer than EXRTF_TAB_VICINO), removes the one nearest to
 * `twips` within that distance (1 if one was there), or removes them all. */
#define EXRTF_TAB_VICINO  90
void exrtf_tab_metti(ExRtfDoc *d, unsigned int da, unsigned int a, unsigned int twips);
int  exrtf_tab_togli(ExRtfDoc *d, unsigned int da, unsigned int a, unsigned int twips);
void exrtf_tab_via(ExRtfDoc *d, unsigned int da, unsigned int a);

/* The width the text may take on the page, in twips. */
unsigned int exrtf_colonna(const ExRtfDoc *d);

#ifdef __cplusplus
}
#endif

#endif /* EXRTF_H */
