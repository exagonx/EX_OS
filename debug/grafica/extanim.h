/* =============================================================================
 * debug/grafica/extanim.h  ->  destinazione: lib/exwin/extanim.h
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * LE ANIMAZIONI DI ExWin
 *
 * Ogni effetto e' una funzione che disegna UN fotogramma: riceve il tempo
 * normalizzato t in millesimi (0 = inizio, 1000 = fine) e scrive su una
 * ExTela. Non c'e' un orologio qui dentro: chi anima (wserver per le
 * finestre, il toolkit per i controlli) decide quando chiamare e con quale t,
 * dal proprio uptime_ms() — vedi implementazione_ANIM001.txt per il ciclo
 * di disegno, la frequenza (60 Hz se il server ce la fa, altrimenti si
 * saltano fotogrammi e non si rallenta) e il modo di spegnerle.
 *
 * Due famiglie:
 *   - effetti di FINESTRA (anim_01..05, 11): lavorano sull'immagine gia'
 *     disegnata della finestra (la sua zona condivisa, che il server ha) e la
 *     posano trasformata sullo schermo. Li chiama wserver nel compositore.
 *   - effetti di CONTROLLO (anim_06..10): si disegnano sopra o al posto di
 *     un controllo. Li chiama il toolkit, dentro disegna_oggetto_nudo.
 *
 * ! TUTTO IN INTERI, come extema: le curve sono polinomi in millesimi e
 * seni da tabella (ext_seno). Niente FPU nel compositore.
 * ============================================================================= */
#ifndef EXTANIM_H
#define EXTANIM_H

#include "extema.h"

/* -----------------------------------------------------------------------------
 * Le curve: t e risultato in millesimi. exa_rimbalzo e exa_molla escono
 * oltre 1000 a meta' strada (e' il loro scopo): chi le usa per un'alfa deve
 * stringerle con exa_stringi.
 * --------------------------------------------------------------------------- */
int exa_stringi(int v, int lo, int hi);
int exa_lineare(int t);
int exa_esce(int t);        /* cubica d'uscita: parte veloce, arriva piano   */
int exa_entra_esce(int t);  /* cubica: piano, veloce, piano                  */
int exa_rimbalzo(int t);    /* "back": va oltre del 10% e torna              */
int exa_molla(int t);       /* oscillazione smorzata attorno all'arrivo      */
int exa_lerp(int a, int b, int t);          /* a + (b - a) * t / 1000        */

/* -----------------------------------------------------------------------------
 * Posare immagini (sorgente 0x00RRGGBB, `passo` pixel per riga)
 * --------------------------------------------------------------------------- */
void exa_posa(ExTela *d, int x, int y, const unsigned int *src, int sw, int sh,
              int passo, int alfa);
/* scalata con filtro bilineare a 8 bit di frazione, nel rettangolo dw x dh */
void exa_posa_scalata(ExTela *d, int x, int y, int dw, int dh,
                      const unsigned int *src, int sw, int sh, int passo, int alfa);
/* una riga sola della sorgente, allargata o stretta a dw pixel */
void exa_posa_riga(ExTela *d, int x, int y, int dw, const unsigned int *riga,
                   int sw, int alfa);

/* -----------------------------------------------------------------------------
 * Gli effetti (un file anim_NN_*.c ciascuno)
 * --------------------------------------------------------------------------- */
/* 01 */ void exa_dissolvenza(ExTela *d, int x, int y, const unsigned int *src,
                              int sw, int sh, int passo, int t, int apre);
/* 02 */ void exa_zoom(ExTela *d, int x, int y, const unsigned int *src,
                       int sw, int sh, int passo, int t, int apre);
/* 03 */ void exa_scivola(ExTela *d, int x, int y, const unsigned int *src,
                          int sw, int sh, int passo, int t);
/* 04 */ void exa_genio(ExTela *d, int x, int y, const unsigned int *src,
                        int sw, int sh, int passo, int ix, int iy, int t);
/* 05 */ void exa_cristallo(ExTela *d, const ExTema *m, int x, int y,
                            const unsigned int *src, int sw, int sh, int passo, int t);
/* 06 */ void exa_onda(ExTela *d, const ExTema *m, int x, int y, int w, int h,
                       int px, int py, int t);
/* 07 */ void exa_respiro(ExTela *d, const ExTema *m, int x, int y, int w, int h, int t);
/* 08 */ void exa_tendina(ExTela *d, int x, int y, const unsigned int *src,
                          int sw, int sh, int passo, int t);
/* 09 */ void exa_selezione(ExTela *d, const ExTema *m, int x, int w,
                            int y_da, int y_a, int h, int t);
/* 10 */ void exa_luccichio(ExTela *d, const ExTema *m, int x, int y, int w, int h, int t);
/* 11 */ void exa_cambio(ExTela *d, int x, int y, const unsigned int *vecchia,
                         const unsigned int *nuova, int sw, int sh, int passo,
                         int verso, int t);

#endif /* EXTANIM_H */
