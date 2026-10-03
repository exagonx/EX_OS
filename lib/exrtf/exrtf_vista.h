/* =============================================================================
 * lib/exrtf/exrtf_vista.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * THE RICH TEXT VIEW (@RTF stage 2, 30 September 2026)
 *
 * An ExRtfDoc shown and edited inside a rectangle of an ExWin window: lines
 * wrapped by words, each run in its own typeface, size, colour and underline,
 * each paragraph with its alignment; caret, selection with the mouse and with
 * Shift, the clipboard, a vertical scroll bar.
 *
 * ! IT IS NOT A TOOLKIT CONTROL, AND ON PURPOSE. Controls in exwin.so have no
 * procedure of their own, and one that knew about styled runs would tie the
 * toolkit to this model. The view draws with the toolkit's public calls
 * (ex_fill_rect, ex_draw_text_font, ex_font_find), and the program hands it the
 * messages of its rectangle - as EXBrowser does with its page:
 *
 *     EXM_PAINT      exrtf_vista_disegna(&v, f)
 *     EXM_KEY        if (exrtf_vista_tasto(&v, wp)) ...redraw...
 *     EXM_MOUSE_DOWN    exrtf_vista_clic(&v, x, y, shift)
 *     EXM_MOUSE_MOVE  exrtf_vista_trascina(&v, x, y)
 *     EXM_DOUBLE_CLICK   exrtf_vista_doppio(&v, x, y)
 *     EXM_WHEEL      exrtf_vista_rotella(&v, (int)wp)
 *     EXM_SIZE       exrtf_vista_posto(&v, ...)
 *
 * The keys reach the program when no control has the focus (ex_clear_focus).
 *
 * ! THE LINES ARE THE CALLER'S MEMORY, like the document's buffers: the view
 * allocates nothing, and a document with more lines than righe_max shows its
 * first righe_max.
 * ============================================================================= */
#ifndef EXRTF_VISTA_H
#define EXRTF_VISTA_H

#include "exrtf.h"
#include "exwin.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned int  inizio, fine;     /* bytes; fine excludes the '\n' */
    int           y, h, base;       /* y from the top of the document */
    int           largo;            /* width of the text, trailing spaces out */
    unsigned int  spazi;            /* spaces inside, for a justified line */
    unsigned char allinea;          /* EXRTF_*: a paragraph's last line is
                                     * never justified */
} ExRtfRiga;

typedef struct {
    ExRtfDoc    *doc;
    ExRtfRiga   *righe;
    unsigned int righe_n, righe_max;
    int          x, y, w, h;        /* the rectangle in the window */
    unsigned int prima;             /* the first line shown */
    unsigned int cur, anc;          /* caret and anchor: equal = no selection */
    ExRtfStile   stile;             /* what the next typed letter looks like */
    int          x_voluta;          /* the column kept by Up and Down, -1 none */
    int          fuoco;             /* 1: draw the caret (the program says) */
    int          modificato;        /* the document changed since it was 0 */
    int          trascina;          /* the button is down in the text */
} ExRtfVista;

/* The value that switches bold, italic or underline on or off (the state at
 * the start of the selection, or of the typing style, decides which). */
#define EXRTF_INVERTI 0xFFFFFFFFu

void exrtf_vista_prepara(ExRtfVista *v, ExRtfDoc *d, ExRtfRiga *righe, unsigned int righe_max);

/* After the document was loaded or replaced: caret at the start, no
 * selection, the typing style from the first letter, lines laid out again. */
void exrtf_vista_nuovo(ExRtfVista *v);

/* The rectangle, in window coordinates; lays the lines out again. */
void exrtf_vista_posto(ExRtfVista *v, int x, int y, int w, int h);

/* Lays the lines out again (after a change made outside the view). */
void exrtf_vista_impagina(ExRtfVista *v);

void exrtf_vista_disegna(ExRtfVista *v, ExWindow f);

/* 1 if the key was the view's (the view must be redrawn), 0 if it is the
 * program's (Ctrl+S, F-keys, Esc...). */
int  exrtf_vista_tasto(ExRtfVista *v, unsigned int k);

/* Mouse, in window coordinates. 1 if the point was inside the view. */
int  exrtf_vista_clic(ExRtfVista *v, int x, int y, int shift);
int  exrtf_vista_trascina(ExRtfVista *v, int x, int y);
int  exrtf_vista_doppio(ExRtfVista *v, int x, int y);
void exrtf_vista_su(ExRtfVista *v);             /* the button went up */
void exrtf_vista_rotella(ExRtfVista *v, int scatti);

/* One thing of the style (EXRTF_C_*) on the selection, or on the typing
 * style when nothing is selected. EXRTF_INVERTI switches B, I, U. */
void exrtf_vista_cambia(ExRtfVista *v, int cosa, unsigned int valore);

/* The alignment of the paragraphs of the selection, or of the caret's. */
void exrtf_vista_allinea(ExRtfVista *v, unsigned int allineamento);

/* For the program's buttons: the style under the caret, and the alignment of
 * its paragraph. */
const ExRtfStile *exrtf_vista_stile(const ExRtfVista *v);
unsigned int      exrtf_vista_allineamento(const ExRtfVista *v);

/* The selection as plain text on the clipboard; cut also removes it. */
int  exrtf_vista_copia(ExRtfVista *v);
int  exrtf_vista_taglia(ExRtfVista *v);
int  exrtf_vista_incolla(ExRtfVista *v);
void exrtf_vista_tutto(ExRtfVista *v);

/* The pixel size of a size in points (96 dots per inch, as Windows). */
int  exrtf_pixel(unsigned int punti);

#ifdef __cplusplus
}
#endif

#endif /* EXRTF_VISTA_H */
