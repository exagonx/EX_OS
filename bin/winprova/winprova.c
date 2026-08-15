/* =============================================================================
 * bin/winprova/winprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La prova del toolkit: una finestra con tutti i controlli che esistono
 *
 *     /bin/winprova            apre la finestra
 *     /bin/winprova -s FILE    apre anche uno sfondo con quell'immagine
 *
 * ! NON E' UN ESEMPIO, E' UNA PROVA. Serve a far vedere che ogni controllo si
 * disegna dove e' stato messo e che un clic su un pulsante torna indietro come
 * EXM_COMANDO con l'id giusto — cioe' che la catena
 * server -> IPC -> libreria -> procedura sia intera. Un esempio si guarda; una
 * prova dice se qualcosa si e' rotto.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"

#define ID_OK       101
#define ID_ANNULLA  102

static ExFinestra g_etichetta;
static unsigned int g_premuti = 0;

static long procedura(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    (void)lp;

    switch (msg) {
    case EXM_COMANDO:
        g_premuti++;
        if (wp == ID_OK)      ex_testo_metti(g_etichetta, "premuto OK");
        if (wp == ID_ANNULLA) ex_testo_metti(g_etichetta, "premuto Annulla");
        printf("winprova: comando %u (premuti finora: %u)\n", wp, g_premuti);
        return 0;

    case EXM_CHIUDI:
        printf("winprova: chiusura chiesta dall'utente\n");
        ex_esci(0);
        return 0;
    }

    /* ! CIO' CHE NON SI GESTISCE VA ALLA BASE, e non si lascia cadere: e' la
     * base a disegnare i controlli e a ridisegnare la finestra. Una procedura
     * che restituisse sempre 0 darebbe una finestra vuota. */
    return ex_procedura_base(f, msg, wp, lp);
}

int main(int argc, char **argv)
{
    ExFinestra f, riq;
    ExMsg m;
    const char *sfondo = 0;
    int i, terminale = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) sfondo = argv[++i];
        if (strcmp(argv[i], "-t") == 0) terminale = 1;
    }

    /* ! UNA SHELL DENTRO UNA FINESTRA, e non e' un esempio in piu': e' la
     * prova che una shell puo' vivere su una PIPE invece che sul tty della
     * console — cioe' che due shell grafiche non si contendono niente. */
    if (terminale) {
        ExFinestra ft, t;

        ft = ex_crea("finestra", "Terminale", EX_TITOLO | EX_BORDO | EX_CHIUDI,
                     60, 50, 640, 400, 0, 0, 0);
        if (ft == 0) {
            printf("winprova: il server a finestre non risponde.\n");
            return 1;
        }
        t = ex_crea("terminale", "/bin/sh", EX_FIGLIO, 2, 2, 636, 396, ft, 0, 0);
        if (t == 0) {
            printf("winprova: non riesco ad avviare la shell nella finestra\n");
            return 1;
        }

        ex_procedura_base(ft, EXM_DISEGNA, 0, 0);
        printf("winprova: terminale aperto, la shell gira dentro la finestra\n");

        while (ex_prendi_msg(&m)) ex_smista(&m);
        return 0;
    }

    /* Lo sfondo e' una finestra come le altre, con lo stile che la tiene
     * sotto: e' il motivo per cui un'immagine di scrivania non ha avuto
     * bisogno di un meccanismo suo. */
    if (sfondo) {
        unsigned int sw = 0, sh = 0;
        ExFinestra s;

        ex_schermo(&sw, &sh);
        s = ex_crea("finestra", "", EX_SFONDO, 0, 0, (int)sw, (int)sh,
                    0, 0, 0);
        if (s) {
            if (!ex_immagine(s, sfondo, 0, 0))
                printf("winprova: %s: formato non riconosciuto\n", sfondo);
            ex_aggiorna(s);
        }
    }

    f = ex_crea("finestra", "Prova del toolkit",
                EX_TITOLO | EX_BORDO | EX_CHIUDI,
                80, 60, 360, 220, 0, 0, procedura);
    if (f == 0) {
        printf("winprova: il server a finestre non risponde.\n");
        printf("          Avvialo:  /cdrom/dev/wserver.drv &\n");
        return 1;
    }

    ex_crea("intestazione", "Anagrafica", EX_FIGLIO,
            0, 0, 360, 22, f, 0, 0);

    ex_crea("etichetta", "Nome:", EX_FIGLIO,  14,  38,  60, 16, f, 0, 0);
    ex_crea("testo",     "Graziano", EX_FIGLIO, 80, 34, 250, 22, f, 0, 0);

    ex_crea("etichetta", "Sistema:", EX_FIGLIO, 14,  70,  70, 16, f, 0, 0);
    ex_crea("testo",     "EX-OS", EX_FIGLIO,   80,  66, 250, 22, f, 0, 0);

    ex_crea("separatore", "", EX_FIGLIO, 14, 100, 330, 2, f, 0, 0);

    riq = ex_crea("riquadro", "Esito", EX_FIGLIO, 14, 112, 330, 50, f, 0, 0);
    g_etichetta = ex_crea("etichetta", "nessun pulsante premuto", EX_FIGLIO,
                          12, 22, 300, 16, riq, 0, 0);

    ex_crea("pulsante", "OK",      EX_FIGLIO, 170, 178, 80, 26, f, ID_OK, 0);
    ex_crea("pulsante", "Annulla", EX_FIGLIO, 260, 178, 80, 26, f, ID_ANNULLA, 0);

    /* Il primo disegno: da qui in poi lo rifa' il ciclo dei messaggi. */
    ex_procedura_base(f, EXM_DISEGNA, 0, 0);

    printf("winprova: finestra aperta. Premi un pulsante o chiudi la finestra.\n");

    while (ex_prendi_msg(&m)) ex_smista(&m);

    ex_distruggi(f);
    printf("winprova: uscita, %u comandi ricevuti\n", g_premuti);
    return 0;
}
