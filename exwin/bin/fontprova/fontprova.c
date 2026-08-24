/* =============================================================================
 * exwin/bin/fontprova/fontprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La prova dei font, che si guarda
 *
 * ! IL RASTERIZZATORE E' GIA' PROVATO CONTRO FreeType, MA NON DENTRO EX-OS.
 * Quel confronto gira sull'host: dice che i glifi vengono giusti, e non dice
 * niente su exfont.so caricata a caldo, sulla cache, sulla fusione col fondo o
 * sul fatto che i file dei font siano leggibili dal CD. Questa finestra prova
 * il giro intero, ed e' fatta per essere FOTOGRAFATA — le prove grafiche di
 * questo sistema si misurano nei pixel.
 *
 * ! E DISEGNA ANCHE LA RIGA COL FONT DI SISTEMA, in cima. Serve da metro: se
 * il TrueType non si carica, restano solo quella e le scritte di errore, e si
 * capisce subito dove si e' fermato invece di guardare una finestra vuota.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"

/* +0.001 a ogni modifica: `fontprova -version` la stampa. Vedi EX_VERSIONE in libc.h. */
#define VERSIONE_APP "0.001"
EX_VERSIONE("fontprova", VERSIONE_APP);

#define FIN_W   720
/* ! LA BARRA DEI MENU NON RESTRINGE L'AREA DEL CLIENT: il toolkit la mette in
 * cima e larga quanto la finestra, ma il posto glielo deve lasciare chi scrive
 * il programma. Venti pixel sono MENU_BARRA_H di lib/exwin/exwin.c. */
#define MENU_H  20
#define FIN_H   (440 + MENU_H)

#define ID_INFO 1

/* Il campione: maiuscole, minuscole, cifre, accentate e un po' di punti.
 *
 * ! LE ACCENTATE CI SONO APPOSTA. Sono glifi COMPOSTI — un riferimento alla
 * lettera piu' uno all'accento — e sono l'unica parte del contenitore che una
 * riga di sole lettere inglesi non proverebbe. */
static const char *CAMPIONE = "Aa Bb Gg Qq 0123 e' a` o` .,;:!? Ciao EX-OS";

static ExFinestra g_f;

static struct {
    const char *file;
    const char *nome;
    int         corpo;
} G[] = {
    { "/exwin/font/LiberationSans-Regular.ttf",  "Sans 14",   14 },
    { "/exwin/font/LiberationSans-Bold.ttf",     "Sans 18 g", 18 },
    { "/exwin/font/LiberationSerif-Regular.ttf", "Serif 20",  20 },
    { "/exwin/font/LiberationSerif-Italic.ttf",  "Serif 26 c",26 },
    { "/exwin/font/LiberationMono-Regular.ttf",  "Mono 16",   16 },
    { "/exwin/font/LiberationSans-Regular.ttf",  "Sans 32",   32 }
};

#define QUANTI  ((int)(sizeof(G) / sizeof(G[0])))

static ExFont g_font[QUANTI];

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        ex_esci(0);
        return 0;

    case EXM_COMANDO:
        if (wp == ID_INFO) {
            char t[512];

            exinfo_testo(t, sizeof(t), "Prova dei font", VERSIONE_APP,
                         "Disegna la stessa frase con il font di sistema 8x16 "
                         "e con i TrueType a corpi diversi: serve a guardare "
                         "la fusione dei bordi, che si vede solo confrontando "
                         "un fondo chiaro e uno scuro.");
            ex_dlg_avviso("Informazioni su", t);
        }
        return 0;

    case EXM_DISEGNA: {
        int i, y = 6 + MENU_H;

        /* Sotto la barra dei menu: il fondo comincia da li'. */
        ex_riempi(f, 0, MENU_H, FIN_W, FIN_H - MENU_H, EX_GRIGIO);

        /* Il metro: il font di sistema, che c'e' sempre. */
        ex_scrivi(f, 8, y, "font di sistema 8x16:", EX_NERO);
        ex_scrivi(f, 8 + ex_larghezza_testo(EX_FONT_SISTEMA,
                                            "font di sistema 8x16: "),
                  y, CAMPIONE, EX_NERO);
        y += 22;

        ex_riempi(f, 6, y, FIN_W - 12, 2, EX_GRIGIO_SC);
        y += 8;

        for (i = 0; i < QUANTI; i++) {
            if (g_font[i] == 0) {
                ex_scrivi(f, 8, y, "NON CARICATO:", EX_ROSSO);
                ex_scrivi(f, 8 + ex_larghezza_testo(EX_FONT_SISTEMA,
                                                    "NON CARICATO: "),
                          y, G[i].file, EX_ROSSO);
                y += 20;
                continue;
            }

            /* ! LA RIGA AVANZA DI QUANTO DICE IL FONT, non di una misura
             * scelta qui. E' l'interlinea che il disegnatore ha messo nel
             * file: usare il corpo darebbe righe che si toccano, ed e'
             * l'errore che si vede su un paragrafo e mai su una parola. */
            ex_scrivi_con(f, g_font[i], 8, y, CAMPIONE, EX_NERO);

            /* L'etichetta a destra, col font di sistema, per sapere quale e'. */
            ex_scrivi(f, FIN_W - 8 - ex_larghezza_testo(EX_FONT_SISTEMA, G[i].nome),
                      y, G[i].nome, EX_BLU);

            y += ex_font_altezza(g_font[i]) + 6;
        }

        /* ! UNA RIGA SU FONDO SCURO, per guardare la FUSIONE. L'antialiasing
         * si vede solo dove i bordi devono mescolarsi con qualcosa: su un
         * fondo grigio chiaro con testo nero e su uno scuro con testo bianco
         * gli errori di fusione vengono opposti, e uno dei due si nota. */
        if (g_font[0]) {
            ex_riempi(f, 6, y, FIN_W - 12, ex_font_altezza(g_font[0]) + 6, EX_BLU);
            ex_scrivi_con(f, g_font[0], 10, y + 3, CAMPIONE, EX_BIANCO);
        }

        ex_aggiorna(f);
        return 0;
    }

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }
}

int main(int argc, char **argv)
{
    ExMsg m;
    int   i;

    (void)argc; (void)argv;

    g_f = ex_crea("finestra", "Prova dei font", EX_TITOLO | EX_BORDO | EX_CHIUDI,
                  EX_AUTO, EX_AUTO, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("fontprova: il server a finestre non risponde.\n");
        printf("           Avvialo con:  exwin\n");
        return 1;
    }

    {
        ExFinestra menu = ex_menu(g_f);

        ex_menu_voce(menu, "Info", "Informazioni su", ID_INFO);
    }

    for (i = 0; i < QUANTI; i++) {
        g_font[i] = ex_font_apri(G[i].file, G[i].corpo);

        /* Si dice anche sulla seriale: una fotografia dice CHE non si e'
         * caricato, il log dice quale e a che corpo. */
        printf("fontprova: %s corpo %d -> %s\n", G[i].file, G[i].corpo,
               g_font[i] ? "aperto" : "NON aperto");
    }

    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    proc(g_f, EXM_DISEGNA, 0, 0);

    while (ex_prendi_msg(&m)) ex_smista(&m);

    for (i = 0; i < QUANTI; i++)
        if (g_font[i]) ex_font_chiudi(g_font[i]);

    return 0;
}
