/* =============================================================================
 * lib/exgioco/exgioco.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * What the ExWin games have in common (@GIOCHI, 3 October 2026): EXKlondike,
 * EXSpider, EXMajong and EXGO. Compiled INTO each game, like lib/eximg/scrivi.c
 * in Pennello: it is a few hundred lines, and a shared library would be one
 * more file to install for four programs.
 *
 *   tela.c   a picture in memory (ARGB, opaque) that the game draws into and
 *            then lays on its window with ex_pixmap: ExWin has rectangles and
 *            text, a game wants rounded cards and shaded stones. Plus the
 *            random numbers, the profile file and the throttled ex_update.
 *   carte.c  French playing cards, drawn by program at any size.
 *
 * ! THE PICTURE COVERS THE CLIENT AREA UNDER THE MENU BAR: tela_mostra()
 * places it at y = GIOCO_MENU_H, and the coordinates inside it start at 0.
 * ============================================================================= */
#ifndef EXGIOCO_H
#define EXGIOCO_H

#include "exwin.h"

#define GIOCO_MENU_H   20          /* the menu bar, drawn by the toolkit */

/* --- The picture ----------------------------------------------------------- */
typedef struct {
    unsigned int *px;               /* w * h pixels, 0xAARRGGBB */
    int           w, h;
} Tela;

int  tela_crea(Tela *t, int w, int h);          /* 0 if there is no memory */
void tela_libera(Tela *t);

void tela_rett(Tela *t, int x, int y, int w, int h, unsigned int c);
void tela_bordo(Tela *t, int x, int y, int w, int h, unsigned int c);
/* A vertical gradient from c1 (top) to c2 (bottom). */
void tela_sfuma(Tela *t, int x, int y, int w, int h, unsigned int c1, unsigned int c2);
/* A veil of colour c over a rectangle, by alfa (0..255): to light up what is
 * chosen or to dim what cannot be taken. */
void tela_velo(Tela *t, int x, int y, int w, int h, unsigned int c, int alfa);
/* A filled rectangle with rounded corners of radius r, antialiased. */
void tela_tondo(Tela *t, int x, int y, int w, int h, int r, unsigned int c);
/* A filled disc, antialiased. With luce != 0 it is shaded like a stone lit
 * from the top left: c in the middle of the light, darker at the edge. */
void tela_disco(Tela *t, int cx, int cy, int r, unsigned int c, int luce);
/* A thick line between two points, antialiased enough for a board. */
void tela_linea(Tela *t, int x0, int y0, int x1, int y1, unsigned int c);

/* A shape given as a test: fn(u, v) says whether the point (u, v) of the unit
 * square is inside. Drawn in the box x, y, w, h with 4 x 4 samples per pixel. */
typedef int (*Forma)(float u, float v);
void tela_forma(Tela *t, int x, int y, int w, int h, Forma fn, unsigned int c);

/* Another picture (or a part of it) laid on this one. With alfa the source
 * alpha is honoured (0 = not drawn), without it the pixels are copied. */
void tela_copia(Tela *t, int dx, int dy, const Tela *s, int sx, int sy,
                int w, int h, int alfa);

/* Text with a font of the toolkit (ex_font_find), top-left at x, y. */
void tela_testo(Tela *t, ExFont f, int x, int y, const char *s, unsigned int c);
/* The same, centred on cx. */
void tela_testo_c(Tela *t, ExFont f, int cx, int y, const char *s, unsigned int c);

/* a over b by alfa (0..255). */
unsigned int colore_mescola(unsigned int a, unsigned int b, int alfa);
unsigned int colore_scuro(unsigned int c, int quanto);    /* quanto 0..255 */

/* Lays the rectangle x, y, w, h of the picture on the window, under the menu
 * bar. Then gioco_annuncia() tells the server. */
void tela_mostra(ExWindow f, const Tela *t, int x, int y, int w, int h);

/* ! THE SERVER IS TOLD AT MOST EVERY 50 ms, the lesson of Pennello: a card
 * dragged across the table is a hundred moves a second, and every ex_update
 * is a recomposition of the whole screen. With forza = 0 a call that comes
 * too soon is left for gioco_tempo() (from EXM_TIMER) or for the next one. */
void gioco_annuncia(ExWindow f, int forza);
void gioco_tempo(ExWindow f);

/* --- Random numbers ------------------------------------------------------- */
void         caso_semina(void);
unsigned int caso(void);
unsigned int caso_fino(unsigned int n);          /* 0 .. n-1 */

/* --- The player's choices: $HOME/.exwin/config/<nome>.cfg ----------------- */
/* Reads «chiave = valore» lines; returns 1 if the key was there. */
int  gioco_cfg_leggi(const char *nome, const char *chiave, char *val, unsigned int max);
int  gioco_cfg_intero(const char *nome, const char *chiave, int predefinito);
/* Rewrites the whole file with `testo`. */
void gioco_cfg_scrivi(const char *nome, const char *testo);

/* "m:ss" or "h:mm:ss" for a number of seconds. */
void gioco_tempo_testo(char *out, unsigned int secondi);

/* =============================================================================
 * carte.c — the cards
 *
 * A card is 0..51: seme = c / 13 (0 picche, 1 cuori, 2 quadri, 3 fiori),
 * valore = c % 13 (0 = asso ... 12 = re). CARTA_DORSO draws the back.
 * Each face is drawn once, the first time it is needed, and kept.
 * ============================================================================= */
#define CARTA_DORSO   (-1)
#define SEME(c)       ((c) / 13)
#define VALORE(c)     ((c) % 13)
#define ROSSA(c)      (SEME(c) == 1 || SEME(c) == 2)

int  carte_misura(int w, int h);                  /* 0 if there is no memory */
int  carte_w(void);
int  carte_h(void);
void carta_disegna(Tela *t, int x, int y, int carta);
/* The empty place of a pile: a rounded outline; with segno, a faint letter
 * («A» for a foundation, «K» for an empty column). */
void carta_posto(Tela *t, int x, int y, const char *segno);
/* A thin frame around a card, to show what is chosen. */
void carta_evidenzia(Tela *t, int x, int y, unsigned int c);

#endif /* EXGIOCO_H */
