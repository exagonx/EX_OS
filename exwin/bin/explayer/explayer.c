/* =============================================================================
 * exwin/bin/explayer/explayer.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il lettore audio di ExWin (9 ottobre 2026)
 *
 *     explayer [file.wav | file.mp3 ...]
 *
 * Un elenco di brani, cinque pulsanti — precedente, suona, pausa, ferma,
 * successivo — una barra che dice a che punto e' il brano e si trascina per
 * andare altrove, e tre contatori: il tempo trascorso, quello che resta, e
 * quanto dura tutto l'elenco. I file dati sulla riga di comando entrano
 * nell'elenco e il primo parte: e' cosi' che lo apre il file manager al
 * doppio clic su un .wav o un .mp3 (/exwin/lib/tipi.txt).
 *
 * ! IL SUONO NON LO FA QUESTO PROGRAMMA, lo fa /exwin/lib/exsuono.so: qui ci
 * sono la finestra e l'elenco. Chi vuole fare un suono da un altro programma
 * usa la stessa libreria (vedi lib/exsuono/exsuono.h).
 *
 * ! IL CICLO DEI MESSAGGI NON E' QUELLO SOLITO, E SI VEDE IN FONDO AL FILE. La
 * libreria non ha un filo suo: il brano va avanti solo se le si da' la parola
 * spesso (exsuono_passo), e la sveglia del toolkit batte ogni 200 ms — troppo
 * poco per un anello che tiene un terzo di secondo di suono. Percio' qui si
 * guarda la posta senza dormire, si fa un passo, e si dorme venti millisecondi.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"
#include "exsuono.h"
#include "kbd_proto.h"

#define VERSIONE_APP "0.001"
EX_VERSIONE("explayer", VERSIONE_APP);

#define FIN_W       440
#define FIN_H       380
#define BRANI_MAX   200
#define PERC_MAX    200

#define ID_APRI     1
#define ID_SVUOTA   2
#define ID_ESCI     3
#define ID_INFO     4
#define ID_PREC     10
#define ID_SUONA    11
#define ID_PAUSA    12
#define ID_FERMA    13
#define ID_SUCC     14
#define ID_BARRA    20
#define ID_ELENCO   21

static ExWindow g_f, g_menu, g_elenco, g_barra;
static ExWindow g_titolo, g_trascorso, g_resta, g_totale;

static char         g_brano[BRANI_MAX][PERC_MAX];
static unsigned int g_durata[BRANI_MAX];        /* ms; 0 = non si sa */
static int          g_n = 0;
static int          g_qui = -1;                 /* il brano aperto, -1 = nessuno */
static int          g_suona = 0;                /* 1 = in corso, 0 = fermo o in pausa */
static int          g_esci = 0;
static unsigned int g_dur_qui = 0;              /* durata del brano aperto, ms */
static unsigned int g_detto = 0xFFFFFFFFu;      /* l'ultimo secondo scritto a schermo */
static int          g_trascino = 0;             /* giri da saltare dopo un trascinamento */

static const char *nome_di(const char *percorso)
{
    const char *b = strrchr(percorso, '/');

    return b ? b + 1 : percorso;
}

static void tempo(char *out, const char *prima, unsigned int ms)
{
    unsigned int s = ms / 1000u;

    if (s >= 3600u) sprintf(out, "%s%u:%02u:%02u", prima, s / 3600u, (s / 60u) % 60u, s % 60u);
    else            sprintf(out, "%s%02u:%02u", prima, s / 60u, s % 60u);
}

/* I tre contatori e la barra. `forza` li riscrive anche se il secondo non e'
 * cambiato. */
static void contatori(int forza)
{
    char t[64];
    unsigned int pos = (g_qui >= 0) ? exsuono_posizione() : 0, tot = 0;
    int i;

    if (!forza && pos / 1000u == g_detto) return;
    g_detto = pos / 1000u;

    for (i = 0; i < g_n; i++) tot += g_durata[i];

    tempo(t, "Trascorso  ", pos);
    ex_set_text(g_trascorso, t);
    tempo(t, "Rimanente  ", g_dur_qui > pos ? g_dur_qui - pos : 0);
    ex_set_text(g_resta, t);
    tempo(t, "Elenco  ", tot);
    ex_set_text(g_totale, t);

    if (g_trascino == 0) ex_scroll_set_pos(g_barra, pos / 1000u);
    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    ex_update(g_f);
}

static void titolo(void)
{
    char t[PERC_MAX + 32];

    if (g_qui < 0) strcpy(t, "Nessun brano");
    else sprintf(t, "%d di %d:  %s", g_qui + 1, g_n, nome_di(g_brano[g_qui]));
    ex_set_text(g_titolo, t);
}

static void ferma(void)
{
    exsuono_chiudi();
    g_qui = -1;
    g_suona = 0;
    g_dur_qui = 0;
    ex_scroll_set_range(g_barra, 0, 0);
    titolo();
    contatori(1);
}

/* La finestra disegnata ADESSO, senza aspettare il giro della posta: prima
 * di un lavoro che la terrebbe ferma, perche' si legga cosa sta facendo. */
static void mostra_ora(const char *cosa)
{
    ex_set_text(g_titolo, cosa);
    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    ex_update(g_f);
}

/* Apre il brano `i` e lo fa partire. Se non si apre lo dice e resta fermo. */
static void suona_brano(int i)
{
    ExSuonoInfo info;
    int r;

    if (i < 0 || i >= g_n) { ferma(); return; }

    exsuono_chiudi();
    /* Il file si legge tutto prima di suonare: da una chiavetta sono secondi. */
    ex_list_select(g_elenco, (unsigned int)i);
    mostra_ora("Apro il brano...");
    r = exsuono_apri(g_brano[i], &info);
    if (r != 0) {
        g_qui = -1; g_suona = 0;
        titolo();
        ex_dlg_avviso("Lettore audio",
                      r == EXSUONO_NO_SCHEDA
                          ? "La scheda audio non risponde. Da una console:  audio -i"
                      : r == EXSUONO_OCCUPATA
                          ? "La scheda audio sta suonando per un altro programma. Riprova fra poco."
                          : "Questo file non si apre, o non e' un WAV ne' un MP3 che so leggere.");
        contatori(1);
        return;
    }
    g_qui = i;
    g_dur_qui = info.durata_ms;
    g_durata[i] = info.durata_ms;
    ex_scroll_set_range(g_barra, g_dur_qui / 1000u, 0);
    ex_list_select(g_elenco, (unsigned int)i);
    exsuono_suona();
    g_suona = 1;
    titolo();
    contatori(1);
}

static void aggiungi(const char *percorso)
{
    unsigned int ms = 0;

    if (g_n >= BRANI_MAX || strlen(percorso) >= PERC_MAX) return;
    strcpy(g_brano[g_n], percorso);
    /* La durata si chiede subito, per il totale dell'elenco: la libreria la
     * sa dire senza toccare il brano che sta suonando. */
    g_durata[g_n] = (exsuono_durata(percorso, &ms) == 0) ? ms : 0;
    ex_list_add(g_elenco, nome_di(percorso));
    g_n++;
}

static void apri_file(void)
{
    char p[PERC_MAX];

    p[0] = 0;
    if (!ex_dlg_apri(p, sizeof(p))) return;
    aggiungi(p);
    if (g_qui < 0) suona_brano(g_n - 1);
    else contatori(1);
}

static void svuota(void)
{
    ferma();
    g_n = 0;
    ex_list_clear(g_elenco);
    contatori(1);
}

static void info(void)
{
    char t[512];

    exinfo_testo(t, sizeof(t), "Lettore audio", VERSIONE_APP,
                 "Suona file WAV e MP3, uno dopo l'altro.  La barra si trascina "
                 "per andare a un punto del brano; Invio o un doppio clic su una "
                 "riga dell'elenco la fanno partire.  Il suono lo fa la libreria "
                 "exsuono.so, che ogni programma puo' usare.");
    ex_dlg_avviso("Informazioni su", t);
}

static long proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CLOSE:
        g_esci = 1;
        return 0;

    case EXM_KEY: {
        unsigned int k = wp & KBD_KEY_MASK;

        /* La barra spaziatrice ferma e riprende, come su ogni lettore. Con
         * il fuoco su un pulsante non arriva qui: la prende il toolkit e
         * preme quello (exwin.so 0.016). */
        if (k == ' ') { proc(f, EXM_COMMAND, g_suona ? ID_PAUSA : ID_SUONA, 0); return 0; }
        if ((wp & KBD_MOD_CTRL) && (k == 'q' || k == 17)) { g_esci = 1; return 0; }
        if ((wp & KBD_MOD_CTRL) && (k == 'o' || k == 15)) { apri_file(); return 0; }
        return ex_default_proc(f, msg, wp, lp);
    }

    case EXM_COMMAND:
        switch (wp) {
        case ID_APRI:   apri_file(); break;
        case ID_SVUOTA: svuota(); break;
        case ID_ESCI:   g_esci = 1; break;
        case ID_INFO:   info(); break;

        case ID_SUONA:
            if (g_qui < 0) {
                int da = (int)ex_list_get_selected(g_elenco);

                suona_brano((da >= 0 && da < g_n) ? da : 0);
            } else {
                exsuono_suona();
                g_suona = 1;
            }
            break;
        case ID_PAUSA:
            if (g_qui >= 0) { exsuono_pausa(); g_suona = 0; }
            break;
        case ID_FERMA:
            ferma();
            break;
        case ID_PREC:
            /* Come su ogni lettore: nei primi tre secondi va al brano prima,
             * dopo riparte da capo quello che c'e'. */
            if (g_qui >= 0 && exsuono_posizione() > 3000u) { exsuono_vai(0); contatori(1); }
            else if (g_qui > 0) suona_brano(g_qui - 1);
            else if (g_qui == 0) { exsuono_vai(0); contatori(1); }
            break;
        case ID_SUCC:
            if (g_qui >= 0 && g_qui + 1 < g_n) suona_brano(g_qui + 1);
            break;

        case ID_BARRA:
            /* La barra manda il valore nuovo a ogni movimento: si va li'. */
            if (g_qui >= 0) {
                exsuono_vai((unsigned int)lp * 1000u);
                g_trascino = 15;        /* per un po' la barra e' di chi la tiene */
                g_detto = 0xFFFFFFFFu;
            }
            break;

        case ID_ELENCO:
            if (EX_IS_OPEN(lp)) suona_brano((int)ex_list_get_selected(g_elenco));
            break;
        }
        return 0;

    default:
        return ex_default_proc(f, msg, wp, lp);
    }
}

int main(int argc, char **argv)
{
    ExMsg m;
    int   i, giri = 0;

    g_f = ex_create("window", "Lettore audio", EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                    EX_AUTO, EX_AUTO, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("explayer: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }

    g_menu = ex_menu_bar(g_f);
    ex_menu_add_item(g_menu, "File", "Aggiungi...\tCtrl+O", ID_APRI);
    ex_menu_add_item(g_menu, "File", "Svuota l'elenco", ID_SVUOTA);
    ex_menu_add_item(g_menu, "File", "-", 0);
    ex_menu_add_item(g_menu, "File", "Esci\tCtrl+Q", ID_ESCI);
    ex_menu_add_item(g_menu, "Info", "Informazioni su", ID_INFO);

    g_titolo = ex_create("label", "Nessun brano", EX_CHILD, 10, 28, FIN_W - 20, 18, g_f, 0, 0);

    ex_create("button", "|<",    EX_CHILD,  10, 52, 60, 26, g_f, ID_PREC,  0);
    ex_create("button", "Suona", EX_CHILD,  76, 52, 80, 26, g_f, ID_SUONA, 0);
    ex_create("button", "Pausa", EX_CHILD, 162, 52, 80, 26, g_f, ID_PAUSA, 0);
    ex_create("button", "Ferma", EX_CHILD, 248, 52, 80, 26, g_f, ID_FERMA, 0);
    ex_create("button", ">|",    EX_CHILD, 334, 52, 60, 26, g_f, ID_SUCC,  0);

    g_barra = ex_create("scrollbar", "", EX_CHILD, 10, 88, FIN_W - 20, 16, g_f, ID_BARRA, 0);

    g_trascorso = ex_create("label", "", EX_CHILD,  10, 110, 140, 18, g_f, 0, 0);
    g_resta     = ex_create("label", "", EX_CHILD, 155, 110, 140, 18, g_f, 0, 0);
    g_totale    = ex_create("label", "", EX_CHILD, 300, 110, 130, 18, g_f, 0, 0);

    g_elenco = ex_create("list", "", EX_CHILD, 10, 134, FIN_W - 20, FIN_H - 144, g_f, ID_ELENCO, 0);

    /* Prima la finestra, poi i brani: di ognuno si legge tutto il file per
     * saperne la durata, e da una chiavetta lenta sono secondi. Senza questo
     * la finestra restava un rettangolo vuoto finche' non erano letti. */
    if (argc > 1) mostra_ora("Leggo i brani...");
    for (i = 1; i < argc; i++)
        if (argv[i][0] != '-') aggiungi(argv[i]);

    titolo();
    contatori(1);
    if (g_n > 0) suona_brano(0);

    /* Il ciclo: la posta senza dormire, un passo al suono, venti millisecondi
     * di sonno. Vedi la nota in testa al file. */
    while (!g_esci) {
        while (ex_peek_message(&m)) {
            ex_dispatch(&m);
            if (g_esci) break;
        }
        if (g_esci) break;

        if (g_qui >= 0 && g_suona) {
            if (!exsuono_passo()) {
                /* Finito: il prossimo, o ci si ferma in fondo all'elenco. */
                if (g_qui + 1 < g_n) suona_brano(g_qui + 1);
                else                 ferma();
            }
        }
        if (g_trascino > 0) g_trascino--;
        if (++giri >= 10) { giri = 0; contatori(0); }
        usleep(20000);
    }

    exsuono_chiudi();
    return 0;
}
