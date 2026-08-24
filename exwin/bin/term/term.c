/* =============================================================================
 * exwin/bin/term/term.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il terminale in finestra
 *
 *     /exwin/bin/term              apre una shell in una finestra
 *     /exwin/bin/term /bin/gfedit  apre quel programma invece della shell
 *
 * ! E' QUASI TUTTO NEL TOOLKIT, e questo file e' quello che ci sta intorno. Il
 * controllo «terminale» di ExWin apre due pipe, avvia il programma, fa l'eco
 * di cio' che si batte e disegna la griglia: qui c'e' una finestra della
 * misura giusta e la decisione di cosa fare quando il programma esce.
 *
 * ! LA SHELL GIRA SU UNA PIPE, NON SUL tty DELLA CONSOLE, ed e' tutto il punto
 * del terminale in finestra. Una shell che legge il descrittore 0 della
 * console si contende la tastiera con chiunque altro stia su quella console;
 * dietro una pipe quella domanda non esiste — i tasti li da' il server alla
 * finestra col fuoco, e da li' vanno nella pipe di QUELLA shell. E' cosi' che
 * se ne possono aprire due senza che si disturbino.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"

/* +0.001 a ogni modifica: `term -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("term", "0.001");

/* ! LA FINESTRA E' UN MULTIPLO ESATTO DELLA CELLA. Il font e' 8x16 e il
 * controllo calcola le colonne come larghezza/8 e le righe come altezza/16:
 * una misura qualunque lascerebbe una striscia nera in fondo e a destra, che
 * sembra un difetto di disegno e invece e' aritmetica. 80x25 celle. */
#define COLONNE     80
#define RIGHE       25
#define CAR_W       8
#define CAR_H       16

#define AREA_W      (COLONNE * CAR_W)   /* 640 */
#define AREA_H      (RIGHE * CAR_H)     /* 400 */
#define BORDO       2

#define FIN_W       (AREA_W + BORDO * 2)
#define FIN_H       (AREA_H + BORDO * 2)

static ExFinestra g_f;
static ExFinestra g_t;                  /* il controllo «terminale» */
static const char *g_prog = "/bin/sh";

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

    /* =====================================================================
     * ! LA GRIGLIA SI ARROTONDA ALLA CELLA, E NON E' PIGNOLERIA. Il font e'
     * 8x16: un'area larga 645 pixel fa 80 colonne e avanza una striscia nera
     * di 5 pixel a destra, che sembra un difetto di disegno e invece e'
     * aritmetica. Si tira l'angolo dove si vuole e la griglia prende la misura
     * intera piu' vicina, sotto.
     *
     * ! E LA MISURA CHE ARRIVA E' QUELLA CONCESSA, non quella chiesta: se il
     * server ha stretto la finestra perche' non ci stava nello schermo, e'
     * questa la sola misura da cui partire. Leggerla da lp e non dal proprio
     * ricordo e' la differenza fra un terminale giusto e uno che disegna fuori
     * dalla propria zona di pixel.
     * ===================================================================== */
    case EXM_MISURA: {
        int w = EX_X(lp) - BORDO * 2;
        int h = EX_Y(lp) - BORDO * 2;

        w = (w / CAR_W) * CAR_W;
        h = (h / CAR_H) * CAR_H;
        if (w < CAR_W * 20) w = CAR_W * 20;
        if (h < CAR_H * 4)  h = CAR_H * 4;

        ex_misura(g_t, w, h);
        return 0;
    }

    /* ! QUANDO IL PROGRAMMA DENTRO ESCE, SI CHIUDE ANCHE LA FINESTRA. E'
     * l'unica risposta che non mente: una finestra di terminale intorno a una
     * shell morta accetta i tasti, li mostra nella griglia e non risponde —
     * sembra bloccata mentre e' semplicemente vuota. Battere `exit` in una
     * shell chiude il terminale, come su qualunque altro sistema. */
    case EXM_TERMFINITO:
        printf("term: %s e' uscito, chiudo la finestra\n", g_prog);
        ex_esci(0);
        return 0;
    }

    return ex_procedura_base(f, msg, wp, lp);
}

int main(int argc, char **argv)
{
    ExMsg        m;
    ExFinestra   t;
    unsigned int sw = 0, sh = 0;
    int          x, y;
    char         titolo[96];

    if (argc >= 2) g_prog = argv[1];

    /* ! LA POSIZIONE LA SCEGLIE IL SERVER, dal 18 agosto 2026. Qui c'era «in
     * mezzo allo schermo», con accanto scritto che due terminali si sarebbero
     * sovrapposti esattamente: era vero, e restava vero. Il server li mette a
     * cascata perche' e' l'unico a sapere quante finestre ci sono gia'. */
    ex_schermo(&sw, &sh);
    x = EX_AUTO;
    y = EX_AUTO;
    (void)sw; (void)sh;

    /* ! IL TRATTINO E' QUELLO CORTO, ED E' UNA REGOLA NON UN GUSTO. Il font
     * del server e' a 256 caratteri, uno per byte: una stringa in UTF-8 esce a
     * schermo come i suoi byte, e il trattino lungo diventava «ZCO». Nei
     * commenti si scrive come si vuole; in cio' che va a schermo, ASCII. */
    sprintf(titolo, "Terminale - %s", g_prog);

    /* ! EX_RIDIM SI CHIEDE, e chi lo chiede si impegna a gestire EXM_MISURA:
     * senza quella risposta la griglia resterebbe 80x25 dentro una finestra
     * grande il doppio. Vedi exwin.h accanto allo stile. */
    g_f = ex_crea("finestra", titolo,
                  EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                  x, y, FIN_W, FIN_H, 0, 0, proc);
    if (g_f == 0) {
        printf("term: il server a finestre non risponde.\n");
        printf("      Avvialo con:  exwin\n");
        return 1;
    }

    /* ! IL TITOLO DEL CONTROLLO E' IL PROGRAMMA DA AVVIARE, non un'etichetta:
     * e' la convenzione del controllo «terminale», ed e' scritta in exwin.h
     * accanto all'elenco delle classi. */
    g_t = ex_crea("terminale", g_prog, EX_FIGLIO,
                  BORDO, BORDO, AREA_W, AREA_H, g_f, 0, 0);
    t = g_t;
    if (t == 0) {
        printf("term: non riesco ad avviare %s nella finestra\n", g_prog);
        ex_distruggi(g_f);
        return 1;
    }

    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_f);

    printf("term: %s aperto in una finestra %dx%d (%d colonne per %d righe)\n",
           g_prog, FIN_W, FIN_H, COLONNE, RIGHE);

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

/* =============================================================================
 * QUELLO CHE MANCA, DICHIARATO
 *
 * ! NIENTE Ctrl+C, e non e' una dimenticanza: mandare un segnale attraverso
 * una pipe non si puo'. Finche' non c'e' un modo di chiedere al kernel
 * «interrompi quel processo», un programma avviato qui dentro si ferma solo
 * uscendo da se' — o chiudendo la finestra, che lo lascia orfano.
 *
 * ! NIENTE CRONOLOGIA NE' FRECCE. Dietro una pipe non esistono: le fa la line
 * discipline, e una pipe non ne ha nessuna. L'editing di riga lo fa il
 * controllo, e si ferma al Backspace.
 *
 * ! LA FINESTRA SI RIDIMENSIONA, dal 18 agosto 2026: si tira l'angolo in basso
 * a destra e le colonne cambiano davvero — anche per il programma dentro, che
 * la misura nuova la trova nel pty. Quello che NON si puo' ancora fare e'
 * accorgersene mentre si e' fermi ad aspettare: non c'e' un SIGWINCH, quindi un
 * programma a schermo pieno rilegge la misura quando gli pare — in pratica al
 * prossimo tasto. La shell lo fa al prompt.
 *
 * ! E IL TESTO NON SI RIMANDA A CAPO quando la finestra si stringe. Le righe
 * gia' scritte restano tagliate dov'erano: rifluirle vorrebbe dire tenere il
 * testo per righe logiche e non per celle, che e' un terminale diverso da
 * questo.
 * ============================================================================= */
