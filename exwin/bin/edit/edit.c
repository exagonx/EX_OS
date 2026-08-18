/* =============================================================================
 * exwin/bin/edit/edit.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * L'editor di testo grafico
 *
 *     /exwin/bin/edit [FILE]
 *
 * ! NON E' /bin/gfedit CON LE FINESTRE. gfedit vive su una console in modo
 * raw, si disegna da se' i menu a tendina e possiede lo schermo intero; qui lo
 * schermo e' di chi lo compone, i tasti arrivano solo quando la finestra ha il
 * fuoco, e non c'e' niente da spegnere quando si esce.
 *
 * ! E L'AREA DI TESTO NON E' PIU' DISEGNATA A MANO. Fino al 17 agosto 2026
 * questo file conteneva il buffer delle righe, il cursore, lo scorrimento in
 * due direzioni, l'inserimento, il Backspace, il Canc e il disegno: duecento
 * righe che oggi sono il controllo «areatesto» di ExWin. Quello che resta e'
 * cio' che e' DAVVERO dell'editor: leggere un file, scriverlo, e decidere cosa
 * fare quando qualcosa va storto.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "kbd_proto.h"

#define FIN_W       640
#define FIN_H       420

#define AREA_X      4
#define AREA_Y      26
#define AREA_W      (FIN_W - 8)
#define AREA_H      368

#define PERC_MAX    192

#define ID_SALVA      1
#define ID_NUOVO      2
#define ID_RICARICA   3
#define ID_APRI       4
#define ID_SALVACOME  5

static char g_perc[PERC_MAX] = "";
static int  g_parziale = 0;     /* letto SOLO IN PARTE: non si salva */
static char g_avviso[96] = "";

static ExFinestra g_f, g_area, g_stato;

/* -----------------------------------------------------------------------------
 * Caricare
 *
 * ! SI LEGGE A PEZZI E SI SPEZZA STRADA FACENDO, senza un buffer grande quanto
 * il file: un file da mezzo mega non deve chiedere mezzo mega di memoria che
 * poi non si puo' restituire.
 *
 * ! UN FILE PIU' GRANDE DEI LIMITI SI CARICA IN PARTE E IL SALVATAGGIO SI
 * BLOCCA. Salvare quello che si e' letto vorrebbe dire CANCELLARE il resto del
 * file dell'utente senza averlo mai mostrato: e' il modo piu' silenzioso che
 * un editor abbia di distruggere dei dati. E' anche perche' ex_area_aggiungi()
 * rende 0 quando l'area e' piena, invece di smettere in silenzio.
 * --------------------------------------------------------------------------- */
static int carica(const char *percorso)
{
    char buf[512], riga[256];
    int  fd, n, i;
    unsigned int col = 0;

    ex_area_svuota(g_area);
    g_parziale = 0;

    fd = open(percorso, O_RDONLY, 0);
    if (fd < 0) return 0;               /* non c'e': e' un file nuovo */

    while ((n = (int)read(fd, buf, sizeof buf)) > 0) {
        for (i = 0; i < n; i++) {
            char c = buf[i];

            if (c == '\r') continue;    /* i fine-riga di DOS non si vedono */

            if (c == '\n') {
                riga[col] = '\0';
                if (!ex_area_aggiungi(g_area, riga)) { g_parziale = 1; goto fine; }
                col = 0;
                continue;
            }

            if (col + 1 < sizeof(riga)) riga[col++] = c;
            else                        g_parziale = 1;
        }
    }
    riga[col] = '\0';
    if (col && !ex_area_aggiungi(g_area, riga)) g_parziale = 1;

fine:
    close(fd);
    ex_area_pulita(g_area);
    return 1;
}

/* -----------------------------------------------------------------------------
 * Salvare, e chiedere dove
 *
 * ! LE DUE FUNZIONI SI CHIAMANO A VICENDA, e non e' un giro infinito: salva()
 * chiama salva_come() solo quando il nome MANCA, e salva_come() ne mette uno
 * prima di richiamare salva(). Due passaggi al massimo.
 * --------------------------------------------------------------------------- */
static int salva(void);

static int salva_come(void)
{
    char nuovo[PERC_MAX];

    strncpy(nuovo, g_perc, PERC_MAX - 1);
    nuovo[PERC_MAX - 1] = '\0';

    if (!ex_dlg_salva(nuovo, PERC_MAX)) {
        strcpy(g_avviso, "salvataggio annullato");
        return 0;
    }

    strncpy(g_perc, nuovo, PERC_MAX - 1);
    g_perc[PERC_MAX - 1] = '\0';
    return salva();
}

static void apri_con_dialogo(void)
{
    char nuovo[PERC_MAX];

    /* ! ADESSO LA DOMANDA E' UNA DOMANDA. Fino al 18 agosto 2026 il primo
     * «Apri» avvisava e il secondo procedeva, perche' ExDlg aveva un dialogo
     * solo e con un pulsante solo. Con ex_dlg_conferma() si chiede una volta e
     * si risponde — e il dialogo e' modale, quindi non si puo' rispondere
     * continuando a scrivere nel testo. */
    if (ex_area_modificato(g_area) &&
        !ex_dlg_conferma("Modifiche non salvate",
                         "Il testo e' cambiato. Aprire un altro file?",
                         "Apri lo stesso", "Annulla")) {
        strcpy(g_avviso, "apertura annullata: il testo e' ancora quello");
        return;
    }

    strncpy(nuovo, g_perc, PERC_MAX - 1);
    nuovo[PERC_MAX - 1] = '\0';

    if (!ex_dlg_apri(nuovo, PERC_MAX)) {
        strcpy(g_avviso, "apertura annullata");
        return;
    }

    strncpy(g_perc, nuovo, PERC_MAX - 1);
    g_perc[PERC_MAX - 1] = '\0';

    if (carica(g_perc)) sprintf(g_avviso, "aperto: %u righe", ex_area_righe(g_area));
    else                strcpy(g_avviso, "non c'era: file nuovo");
}

static int salva(void)
{
    int fd;
    unsigned int i, n;

    if (g_parziale) {
        strcpy(g_avviso, "letto solo in parte: salvare cancellerebbe il resto");
        return 0;
    }
    if (g_perc[0] == '\0') return salva_come();

    fd = open(g_perc, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        sprintf(g_avviso, "non riesco a scrivere %s", g_perc);
        return 0;
    }

    n = ex_area_righe(g_area);
    for (i = 0; i < n; i++) {
        const char  *r = ex_area_riga(g_area, i);
        unsigned int l = (unsigned int)strlen(r);

        if ((l && write(fd, r, l) != (ssize_t)l) || write(fd, "\n", 1) != 1) {
            close(fd);
            strcpy(g_avviso, "scrittura interrotta: il file e' incompleto");
            return 0;
        }
    }
    close(fd);

    ex_area_pulita(g_area);
    sprintf(g_avviso, "salvato: %u righe", n);
    return 1;
}

/* -----------------------------------------------------------------------------
 * La riga di stato
 * --------------------------------------------------------------------------- */
static void stato_aggiorna(void)
{
    char s[200];
    const char *nome = g_perc[0] ? g_perc : "(senza nome)";
    unsigned int r = 0, c = 0;

    if (g_avviso[0]) {
        sprintf(s, "%s  -  %s", nome, g_avviso);
    } else {
        ex_area_cursore(g_area, &r, &c);
        sprintf(s, "%s%s  -  riga %u/%u  col %u%s",
                ex_area_modificato(g_area) ? "*" : "", nome,
                r, ex_area_righe(g_area), c,
                g_parziale ? "  [PARZIALE: non si salva]" : "");
    }

    ex_testo_metti(g_stato, s);
}

static void ridisegna(void)
{
    stato_aggiorna();
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_f);
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    unsigned int c;

    switch (msg) {
    case EXM_COMANDO:
        g_avviso[0] = '\0';
        if (wp == ID_SALVA)     { salva();            break; }
        if (wp == ID_NUOVO)     { ex_area_svuota(g_area); g_perc[0] = '\0';
                                  g_parziale = 0;     break; }
        if (wp == ID_RICARICA)  {
            if (g_perc[0]) carica(g_perc);
            else strcpy(g_avviso, "niente da ricaricare: non c'e' un file");
            break;
        }
        if (wp == ID_APRI)      { apri_con_dialogo(); break; }
        if (wp == ID_SALVACOME) { salva_come();       break; }
        return 0;

    case EXM_TASTO:
        /* ! QUI ARRIVANO SOLO LE SCORCIATOIE. Le lettere, le frecce, il
         * Backspace e l'Invio li ha gia' mangiati l'area di testo: se sono
         * arrivate fin qui, non erano per lei. */
        g_avviso[0] = '\0';
        c = wp & KBD_KEY_MASK;

        if (wp & KBD_MOD_CTRL) {
            if (c == 's' || c == 'S') { salva(); break; }
            if (c == 'q' || c == 'Q') {
                /* La stessa domanda della chiusura, e non e' una ripetizione
                 * da togliere: sono due modi di uscire, e devono difendere il
                 * testo allo stesso modo. */
                if (ex_area_modificato(g_area) &&
                    !ex_dlg_conferma("Modifiche non salvate",
                                     "Il testo e' cambiato. Uscire senza salvare?",
                                     "Esci", "Torna al testo")) {
                    strcpy(g_avviso, "non uscito: il testo e' ancora qui");
                    break;
                }
                ex_esci(0);
                return 0;
            }
        }
        return ex_procedura_base(f, msg, wp, lp);

    case EXM_CHIUDI:
        /* ! CHIUDERE IN SILENZIO UN TESTO MODIFICATO E' IL MODO PIU' FACILE DI
         * PERDERE IL LAVORO DI QUALCUNO, e adesso c'e' come chiederlo davvero.
         * La risposta prudente e' «no»: chiudere il dialogo o battere Esc
         * lascia l'editor aperto col testo dentro. */
        if (ex_area_modificato(g_area) &&
            !ex_dlg_conferma("Modifiche non salvate",
                             "Il testo e' cambiato. Uscire senza salvare?",
                             "Esci", "Torna al testo")) {
            strcpy(g_avviso, "non uscito: il testo e' ancora qui");
            break;
        }
        ex_esci(0);
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    ridisegna();
    return 0;
}

int main(int argc, char **argv)
{
    ExMsg m;

    if (argc >= 2) {
        strncpy(g_perc, argv[1], PERC_MAX - 1);
        g_perc[PERC_MAX - 1] = '\0';
    }

    g_f = ex_crea("finestra", "Editor", EX_TITOLO | EX_BORDO | EX_CHIUDI,
                  30, 30, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("edit: il server a finestre non risponde.\n");
        printf("      Avvialo con:  exwin\n");
        return 1;
    }

    ex_crea("pulsante", "Nuovo",      EX_FIGLIO,   4, 2, 60, 20, g_f, ID_NUOVO,     0);
    ex_crea("pulsante", "Apri",       EX_FIGLIO,  68, 2, 60, 20, g_f, ID_APRI,      0);
    ex_crea("pulsante", "Salva",      EX_FIGLIO, 132, 2, 60, 20, g_f, ID_SALVA,     0);
    ex_crea("pulsante", "Salva come", EX_FIGLIO, 196, 2, 96, 20, g_f, ID_SALVACOME, 0);
    ex_crea("pulsante", "Ricarica",   EX_FIGLIO, 296, 2, 74, 20, g_f, ID_RICARICA,  0);

    g_area = ex_crea("areatesto", "", EX_FIGLIO,
                     AREA_X, AREA_Y, AREA_W, AREA_H, g_f, 0, 0);
    if (!g_area) {
        printf("edit: non riesco a creare l'area di testo\n");
        return 1;
    }

    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      6, FIN_H - 22, AREA_W - 4, 16, g_f, 0, 0);

    /* ! IL FUOCO ALL'AREA, ESPLICITAMENTE: senza andrebbe al pulsante «Nuovo»,
     * che e' il primo controllo creato che lo accetta, e battere non
     * scriverebbe niente. */
    ex_fuoco(g_area);

    if (g_perc[0]) {
        if (carica(g_perc))
            printf("edit: %s, %u righe%s\n", g_perc, ex_area_righe(g_area),
                   g_parziale ? " (PARZIALE)" : "");
        else
            printf("edit: %s non c'e': file nuovo\n", g_perc);
    } else {
        printf("edit: file nuovo, senza nome\n");
    }

    ridisegna();

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

/* =============================================================================
 * QUELLO CHE MANCA, DICHIARATO
 *
 * ! NIENTE ANNULLAMENTO, NIENTE SELEZIONE, NIENTE APPUNTI. /bin/gfedit ha
 * l'annullamento, a giornale di operazioni; qui si aggiungera' quando ci sara'
 * un servizio degli appunti, perche' una selezione che non si puo' copiare da
 * nessuna parte serve a poco.
 *
 * ! E MANCA UN DIALOGO CON «SI'/NO». ExDlg ne ha uno con un pulsante solo,
 * quindi «vuoi perdere le modifiche?» si chiede facendo premere due volte lo
 * stesso pulsante. Funziona, e si vede che e' un ripiego.
 * ============================================================================= */
