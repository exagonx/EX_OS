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
#include "kbd_proto.h"
#include "exwin.h"

#define ID_OK       101
#define ID_ANNULLA  102

#define PASSO       40      /* di quanto cresce a ogni freccia */

static ExFinestra g_etichetta;
static ExFinestra g_int, g_sep, g_riq, g_t1, g_t2, g_ok, g_ann;
static unsigned int g_premuti = 0;
static int g_w = 360, g_h = 220;        /* la misura che la finestra ha ADESSO */

/* =============================================================================
 * ! LA PROVA DEL RIDIMENSIONAMENTO SI FA A TASTI, NON COL MOUSE, e non e' un
 * ripiego: i movimenti relativi grandi del monitor di QEMU si perdono per
 * strada, quindi una prova che dipende da DOVE si clicca non e' ripetibile.
 * Con le frecce la misura la decide QUESTO file, e quella che arriva indietro
 * in EXM_MISURA e' quella che il server ha davvero concesso — cioe' un numero
 * che si confronta, non un'impressione.
 *
 * ! E SI SCRIVE SULLA SERIALE, non con printf: un'applicazione grafica gira su
 * una console che non e' quella della shell, e cio' che stampa li' non lo legge
 * nessuno da fuori.
 * ============================================================================= */
static void rifai_disposizione(int w, int h)
{
    ex_misura(g_int, w, 22);

    ex_misura(g_t1, w - 110, 22);
    ex_misura(g_t2, w - 110, 22);

    ex_misura(g_sep, w - 28, 2);

    ex_misura(g_riq, w - 28, 50);
    ex_misura(g_etichetta, w - 56, 16);

    ex_sposta(g_ok,  w - 190, h - 42);
    ex_sposta(g_ann, w - 100, h - 42);
}

static long procedura(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    /* ! LE FRECCE ARRIVANO QUI ANCHE CON IL FUOCO IN UNA CASELLA, ed e'
     * voluto: una casella di testo consuma i caratteri stampabili, non i tasti
     * speciali. Vedi tasto_al_fuoco() in exwin.c. */
    case EXM_TASTO:
        switch (wp & KBD_KEY_MASK) {
        case KBD_K_RIGHT: ex_misura(f, g_w + PASSO, g_h); return 0;
        case KBD_K_LEFT:  ex_misura(f, g_w - PASSO, g_h); return 0;
        case KBD_K_DOWN:  ex_misura(f, g_w, g_h + PASSO); return 0;
        case KBD_K_UP:    ex_misura(f, g_w, g_h - PASSO); return 0;
        }
        break;

    case EXM_MISURA: {
        char riga[80];

        g_w = EX_X(lp);
        g_h = EX_Y(lp);
        rifai_disposizione(g_w, g_h);

        sprintf(riga, "winprova: adesso e' %dx%d", g_w, g_h);
        log_seriale(riga);
        return 0;
    }

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

    /* ! EX_RIDIM SI CHIEDE, e chi lo chiede si impegna a rifare la propria
     * disposizione in EXM_MISURA. Qui e' anche la prova che quella catena —
     * presa, zona nuova, messaggio, ridisegno — sia intera. */
    f = ex_crea("finestra", "Prova del toolkit",
                EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                80, 60, g_w, g_h, 0, 0, procedura);
    if (f == 0) {
        /* ! IL CONSIGLIO E' «exwin», e le due cose che diceva prima erano
         * tutt'e due sbagliate: il percorso /cdrom/... non esiste quando si
         * avvia DAL CD (li' la radice e' il CD), e avviare wserver.drv a mano
         * lo fa nascere sulla console della shell, dove si contende la
         * tastiera con lei. Chi seguiva questo consiglio non arrivava da
         * nessuna parte, e dava la colpa al server. */
        printf("winprova: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }

    g_int = ex_crea("intestazione", "Anagrafica", EX_FIGLIO,
                    0, 0, g_w, 22, f, 0, 0);

    ex_crea("etichetta", "Nome:", EX_FIGLIO,  14,  38,  60, 16, f, 0, 0);
    g_t1 = ex_crea("testo", "Graziano", EX_FIGLIO, 80, 34, g_w - 110, 22, f, 0, 0);

    ex_crea("etichetta", "Sistema:", EX_FIGLIO, 14,  70,  70, 16, f, 0, 0);
    g_t2 = ex_crea("testo", "EX-OS", EX_FIGLIO,  80,  66, g_w - 110, 22, f, 0, 0);

    g_sep = ex_crea("separatore", "", EX_FIGLIO, 14, 100, g_w - 28, 2, f, 0, 0);

    riq = ex_crea("riquadro", "Esito", EX_FIGLIO, 14, 112, g_w - 28, 50, f, 0, 0);
    g_riq = riq;
    g_etichetta = ex_crea("etichetta", "nessun pulsante premuto", EX_FIGLIO,
                          12, 22, g_w - 56, 16, riq, 0, 0);

    g_ok  = ex_crea("pulsante", "OK",      EX_FIGLIO,
                    g_w - 190, g_h - 42, 80, 26, f, ID_OK, 0);
    g_ann = ex_crea("pulsante", "Annulla", EX_FIGLIO,
                    g_w - 100, g_h - 42, 80, 26, f, ID_ANNULLA, 0);

    /* Il primo disegno: da qui in poi lo rifa' il ciclo dei messaggi. */
    ex_procedura_base(f, EXM_DISEGNA, 0, 0);

    printf("winprova: finestra aperta %dx%d. Le frecce la ridimensionano;\n"
           "          premi un pulsante o chiudi la finestra.\n", g_w, g_h);

    while (ex_prendi_msg(&m)) ex_smista(&m);

    ex_distruggi(f);
    printf("winprova: uscita, %u comandi ricevuti\n", g_premuti);
    return 0;
}
