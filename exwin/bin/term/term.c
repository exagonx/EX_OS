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
static const char *g_prog = "/bin/sh";

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

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

    /* In mezzo allo schermo, se ci sta. Una finestra sempre nell'angolo in
     * alto a sinistra fa si' che due terminali si sovrappongano esattamente. */
    ex_schermo(&sw, &sh);
    x = ((int)sw > FIN_W) ? ((int)sw - FIN_W) / 2 : 0;
    y = ((int)sh > FIN_H) ? ((int)sh - FIN_H) / 3 : 0;

    /* ! IL TRATTINO E' QUELLO CORTO, ED E' UNA REGOLA NON UN GUSTO. Il font
     * del server e' a 256 caratteri, uno per byte: una stringa in UTF-8 esce a
     * schermo come i suoi byte, e il trattino lungo diventava «ZCO». Nei
     * commenti si scrive come si vuole; in cio' che va a schermo, ASCII. */
    sprintf(titolo, "Terminale - %s", g_prog);

    g_f = ex_crea("finestra", titolo, EX_TITOLO | EX_BORDO | EX_CHIUDI,
                  x, y, FIN_W, FIN_H, 0, 0, proc);
    if (g_f == 0) {
        printf("term: il server a finestre non risponde.\n");
        printf("      Avvialo con:  exwin\n");
        return 1;
    }

    /* ! IL TITOLO DEL CONTROLLO E' IL PROGRAMMA DA AVVIARE, non un'etichetta:
     * e' la convenzione del controllo «terminale», ed e' scritta in exwin.h
     * accanto all'elenco delle classi. */
    t = ex_crea("terminale", g_prog, EX_FIGLIO,
                BORDO, BORDO, AREA_W, AREA_H, g_f, 0, 0);
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
 * ! E LA FINESTRA NON SI RIDIMENSIONA, quindi le colonne sono 80 per sempre.
 * La zona condivisa ha misura fissa: cambiarla vuole una stretta di mano
 * ordinata col server, e WIN_EV_MISURA e' gia' nel protocollo ma non e' ancora
 * usato da nessuno.
 * ============================================================================= */
