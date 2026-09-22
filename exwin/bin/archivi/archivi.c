/* =============================================================================
 * exwin/bin/archivi/archivi.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The graphical archiver — WinZip's half of the request
 *
 *     /exwin/bin/archivi [ARCHIVIO.ZIP]
 *
 * ! THERE IS NO ZIP IN THIS FILE, and that is the whole reason the library
 * exists. Everything about the format lives in lib/exzip, and this program
 * calls exactly the nine functions /bin/zip calls. The request asked for a
 * library "with the functions the two have in common", and this is what makes
 * that real rather than a good intention: written the other way round, the
 * second program rewrites the format and from that day the two disagree about
 * what a ZIP is.
 *
 * ! IT HAS TWO STATES, AND THEY ARE THE LIBRARY'S. Either it is READING an
 * archive - the list is what is inside, and things can be extracted - or it is
 * BUILDING one - the list is what has been put in so far, and it is not an
 * archive until "Finisci" writes the catalogue. That is exactly what exzip can
 * do (crea, aggiungi..., finisci), and pretending otherwise in the window
 * would mean promising something the disk cannot keep.
 *
 * ! ADDING TO AN ARCHIVE THAT ALREADY EXISTS IS NOT POSSIBLE YET, and the menu
 * says so instead of doing something surprising: appending means re-reading
 * the catalogue, moving it to the end and leaving the old entries where they
 * are. It is written down in @ZIP as the next thing.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exzip.h"
#include "exinfo.h"
#include "kbd_proto.h"

/* +0.001 a ogni modifica: `archivi -version` la stampa. Vedi EX_VERSIONE. */
#define VERSIONE_APP "0.001"
EX_VERSIONE("archivi", VERSIONE_APP);

#define FIN_W       640
#define FIN_H       420

#define MENU_H      20
#define AREA_X      4
#define AREA_Y      (MENU_H + 20)   /* sotto il menu e l'intestazione */
#define BASSO       24

#define PERC_MAX    320

#define ID_APRI       1
#define ID_NUOVO      2
#define ID_FINISCI    3
#define ID_CHIUDI     4
#define ID_ESCI       5

#define ID_ESTRAI     10
#define ID_ESTRAI_TUT 11
#define ID_AGGIUNGI   12

#define ID_ISTRUZIONI 20
#define ID_INFO       21

#define ID_LISTA      30

static ExFinestra g_f, g_lista, g_stato, g_menu;

static ExZip *g_z;                  /* the archive being read or built */
static int    g_creo;               /* 1 = building, 0 = reading */
static char   g_perc[PERC_MAX] = "";
static char   g_avviso[128] = "";
static char   g_dove[PERC_MAX] = "";   /* where the last extraction went */

/* While building, the list has to say something and the library does not keep
 * the names: the window keeps its own copy of what it has put in. */
static unsigned int g_messi;

/* -----------------------------------------------------------------------------
 * The list
 *
 * ! THE COLUMNS ARE SPACES, NOT PIXELS. The list control draws with a fixed
 * pitch of eight, so a row padded with spaces lines up by construction - and
 * it keeps lining up the day the font changes, which a column measured in
 * pixels would not.
 *
 * ! AND THE WHOLE ROW HAS TO FIT IN 63 CHARACTERS, which is not a taste: it is
 * LISTA_TESTO_MAX in lib/exwin/exwin.c, 64 with the '\0'. Past that the
 * control cuts, and it cuts in silence - the first try put the date in column
 * 64 and the window showed "202" where a date should be. The file manager
 * lives inside the same ceiling with its four columns.
 *
 *     30 name + 9 bytes + 9 in the archive + 10 date + 1 method = 62 with the
 *     spaces between them.
 *
 * ! THE TIME IS THE FIRST THING THAT HAD TO GO. Of what a ZIP records about an
 * entry, the day is what someone looks for; the minute is what they look for
 * once a year, and it costs six characters of the name.
 * --------------------------------------------------------------------------- */
#define C_NOME  30

static void riga_intestazione(void)
{
    char t[128];

    /* ! LA DATA E' A SINISTRA ANCHE NELL'INTESTAZIONE. Con «%10s» la parola
     * «data» finiva incolonnata sulla FINE della data invece che sul suo
     * inizio: sei pixel che fanno sembrare storta una tabella dritta. */
    sprintf(t, "%-*s %9s %9s %-10s %s", C_NOME, "nome", "byte", "in .zip",
            "data", "m");
    ex_riempi(g_f, AREA_X, MENU_H + 2, FIN_W - AREA_X * 2, 16, EX_GRIGIO);
    ex_scrivi(g_f, AREA_X + 4, MENU_H + 2, t, EX_NERO);
}

static void mostra(void)
{
    ExZipVoce    v;
    char         riga[160];
    unsigned int i, n;

    ex_lista_svuota(g_lista);

    if (!g_z) return;
    if (g_creo) {
        /* Building: the library does not tell what is inside yet - it is not
         * an archive until the catalogue is written. What the window shows is
         * what it has put in itself. */
        return;
    }

    n = ex_zip_quante(g_z);
    for (i = 0; i < n; i++) {
        char nome[C_NOME + 1];
        unsigned int k;

        if (!ex_zip_voce(g_z, i, &v)) continue;

        /* ! A NAME TOO LONG IS CUT WITH A «~», so that a cut name cannot be
         * mistaken for a short one. The archive still holds the whole name -
         * extraction uses that, not what is on the screen. */
        for (k = 0; k < C_NOME && v.nome[k]; k++) nome[k] = v.nome[k];
        nome[k] = '\0';
        if (v.nome[k]) nome[C_NOME - 1] = '~';

        /* ! «m» E' IL METODO IN UNA LETTERA: «s» store, «d» deflate. Una
         * parola intera non ci sta, e sapere QUALE dei due e' cio' che spiega
         * perche' un file occupa quanto occupa. */
        sprintf(riga, "%-*s %9lu %9lu %04u-%02u-%02u %c",
                C_NOME, nome, v.dim, v.dim_c,
                v.anno, v.mese, v.giorno,
                v.metodo == EXZIP_DEFLATE ? 'd' : 's');
        ex_lista_aggiungi(g_lista, riga);
    }
}

static void stato(void)
{
    char t[256];

    if (!g_z)        sprintf(t, "nessun archivio aperto.  %s", g_avviso);
    else if (g_creo) sprintf(t, "creo %s - %u file dentro.  %s",
                             g_perc, g_messi, g_avviso);
    else             sprintf(t, "%s - %u file.  %s",
                             g_perc, ex_zip_quante(g_z), g_avviso);

    ex_testo_metti(g_stato, t);
}

/* ! IT GOES THROUGH THE WINDOW PROCEDURE, not straight to the base painting:
 * the header row is added to the drawing in ONE place, the EXM_DISEGNA branch
 * of proc(). See the note there for what happens when it is not. */
static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp);

static void ridisegna(void)
{
    proc(g_f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_f);
}

static void titolo(void)
{
    char t[PERC_MAX + 32];

    if (g_perc[0]) sprintf(t, "Archivi - %s", g_perc);
    else           strcpy(t, "Archivi");
    ex_titolo(g_f, t);
}

/* -----------------------------------------------------------------------------
 * Opening and closing
 * --------------------------------------------------------------------------- */

/* ! AN ARCHIVE BEING BUILT IS NOT ABANDONED IN SILENCE. Without the catalogue
 * the file on the disk is not an archive at all, so whoever walks away from
 * one has to be told what they are about to lose. */
static int lascia_andare(void)
{
    if (!g_z || !g_creo) return 1;

    if (!ex_dlg_conferma("Archivio non finito",
                         "L'archivio che stai creando non e' ancora stato "
                         "chiuso: senza il catalogo nessuno potra' aprirlo. "
                         "Lo abbandoni?",
                         "Abbandona", "Torna indietro"))
        return 0;

    ex_zip_chiudi(g_z);
    g_z = 0;
    g_creo = 0;
    g_perc[0] = '\0';
    g_messi = 0;
    return 1;
}

static void chiudi_archivio(void)
{
    if (!lascia_andare()) return;

    if (g_z) { ex_zip_chiudi(g_z); g_z = 0; }
    g_creo = 0;
    g_perc[0] = '\0';
    g_messi = 0;
    strcpy(g_avviso, "chiuso");
    mostra();
    titolo();
    stato();
}

static void apri(const char *perc)
{
    ExZip *z = ex_zip_apri(perc);

    if (!z) {
        ex_dlg_avviso("Non si apre", ex_zip_errore());
        return;
    }

    if (g_z) ex_zip_chiudi(g_z);
    g_z = z;
    g_creo = 0;
    strncpy(g_perc, perc, PERC_MAX - 1);
    g_perc[PERC_MAX - 1] = '\0';
    g_messi = 0;
    g_avviso[0] = '\0';

    mostra();
    titolo();
    stato();
}

static void apri_con_dialogo(void)
{
    char p[PERC_MAX];

    if (!lascia_andare()) return;

    strncpy(p, g_perc[0] ? g_perc : "/", PERC_MAX - 1);
    p[PERC_MAX - 1] = '\0';

    if (!ex_dlg_apri(p, sizeof(p))) return;
    apri(p);
}

static void nuovo(void)
{
    char p[PERC_MAX];

    if (!lascia_andare()) return;

    strncpy(p, g_perc[0] ? g_perc : "/", PERC_MAX - 1);
    p[PERC_MAX - 1] = '\0';

    /* ! LO STESSO DIALOGO DEL SALVATAGGIO, CON ALTRE PAROLE, e con dentro
     * «Nuova cartella»: chi crea un archivio spesso vuole metterlo in un posto
     * che non c'e' ancora. E' ex_dlg_percorso, del 22 settembre 2026. */
    if (!ex_dlg_percorso("Archivio nuovo", "Archivio:", "Crea", p, sizeof(p)))
        return;

    if (g_z) { ex_zip_chiudi(g_z); g_z = 0; }

    g_z = ex_zip_crea(p);
    if (!g_z) {
        ex_dlg_avviso("Non si crea", ex_zip_errore());
        stato();
        return;
    }

    g_creo = 1;
    g_messi = 0;
    strncpy(g_perc, p, PERC_MAX - 1);
    g_perc[PERC_MAX - 1] = '\0';
    strcpy(g_avviso, "aggiungi i file, poi File/Finisci");

    mostra();
    titolo();
    stato();
}

static void finisci(void)
{
    if (!g_z || !g_creo) {
        strcpy(g_avviso, "non sto creando nessun archivio");
        stato();
        return;
    }

    if (!ex_zip_finisci(g_z)) {
        ex_dlg_avviso("Non si chiude", ex_zip_errore());
        stato();
        return;
    }

    ex_zip_chiudi(g_z);
    g_z = 0;
    g_creo = 0;

    /* ! E SI RIAPRE SUBITO IN LETTURA. Chi ha finito di costruire un archivio
     * vuole vedere che c'e' dentro davvero - e questa e' anche la prova che il
     * catalogo appena scritto si rilegge. */
    apri(g_perc);
    strcpy(g_avviso, "archivio finito");
    stato();
}

/* -----------------------------------------------------------------------------
 * Adding and extracting
 * --------------------------------------------------------------------------- */
static const char *nome_corto(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void aggiungi(void)
{
    char p[PERC_MAX];

    if (!g_z || !g_creo) {
        /* ! SI DICE PERCHE' NON SI PUO', invece di lasciare una voce di menu
         * che non fa niente. */
        ex_dlg_avviso("Non ci posso aggiungere",
                      "Si puo' aggiungere solo a un archivio che si sta "
                      "creando (File, Nuovo...). Aggiungere a uno gia' fatto "
                      "vuol dire riscriverne il catalogo, e non lo so ancora "
                      "fare.");
        return;
    }

    strncpy(p, "/", PERC_MAX - 1);
    p[PERC_MAX - 1] = '\0';
    if (!ex_dlg_apri(p, sizeof(p))) return;

    if (!ex_zip_aggiungi(g_z, p, nome_corto(p))) {
        ex_dlg_avviso("Non l'ho aggiunto", ex_zip_errore());
        return;
    }

    g_messi++;
    ex_lista_aggiungi(g_lista, nome_corto(p));
    sprintf(g_avviso, "aggiunto %s", nome_corto(p));
    stato();
}

/* Asks where to extract, once, and remembers it.
 *
 * ! LA CARTELLA SI SCEGLIE, NON SI INDOVINA, e il dialogo che lo chiede e'
 * quello dei file con le parole cambiate: dentro c'e' «Nuova cartella», che e'
 * esattamente cio' che serve a chi estrae un archivio in un posto nuovo. */
static int chiedi_dove(void)
{
    char p[PERC_MAX];
    unsigned int l;

    strncpy(p, g_dove[0] ? g_dove : "/", PERC_MAX - 1);
    p[PERC_MAX - 1] = '\0';

    l = (unsigned int)strlen(p);
    if (l && p[l - 1] != '/' && l + 2 < sizeof(p)) { p[l] = '/'; p[l + 1] = '\0'; }

    if (!ex_dlg_percorso("Dove estrarre", "Cartella:", "Estrai", p, sizeof(p)))
        return 0;

    strncpy(g_dove, p, PERC_MAX - 1);
    g_dove[PERC_MAX - 1] = '\0';
    return 1;
}

/* Refuses a name that climbs out of the destination. Same check as /bin/zip,
 * and it is here as well because the two programs are two front doors to the
 * same archive: a defence that lives in only one of them is a defence that
 * depends on which door was used. */
static int nome_pericoloso(const char *n)
{
    unsigned int i;

    if (n[0] == '/' || n[0] == '\\') return 1;

    for (i = 0; n[i]; i++)
        if (n[i] == '.' && n[i + 1] == '.' &&
            (n[i + 2] == '/' || n[i + 2] == '\0') &&
            (i == 0 || n[i - 1] == '/'))
            return 1;

    return 0;
}

static void cartelle_per(const char *perc)
{
    char p[PERC_MAX];
    unsigned int i;

    strncpy(p, perc, PERC_MAX - 1);
    p[PERC_MAX - 1] = '\0';

    for (i = 1; p[i]; i++) {
        if (p[i] != '/') continue;
        p[i] = '\0';
        mkdir(p, 0755);
        p[i] = '/';
    }
}

/* Extracts one entry into g_dove. Returns 1, or 0 having said why. */
static int estrai_una(unsigned int i, char *perche, unsigned int max)
{
    ExZipVoce v;
    char      perc[PERC_MAX];

    if (!ex_zip_voce(g_z, i, &v)) return 0;

    if (nome_pericoloso(v.nome)) {
        snprintf(perche, max, "%s: esce dalla cartella scelta", v.nome);
        return 0;
    }

    if (snprintf(perc, sizeof(perc), "%s/%s", g_dove, v.nome) >= (int)sizeof(perc)) {
        snprintf(perche, max, "%s: percorso troppo lungo", v.nome);
        return 0;
    }

    cartelle_per(perc);

    if (!ex_zip_estrai(g_z, i, perc)) {
        snprintf(perche, max, "%s: %s", v.nome, ex_zip_errore());
        return 0;
    }
    return 1;
}

static void estrai_scelto(void)
{
    unsigned int s;
    char         perche[192];

    if (!g_z || g_creo) { strcpy(g_avviso, "apri prima un archivio"); stato(); return; }

    s = ex_lista_scelta(g_lista);
    if (s >= ex_zip_quante(g_z)) { strcpy(g_avviso, "scegli una riga"); stato(); return; }

    if (!chiedi_dove()) return;

    if (estrai_una(s, perche, sizeof(perche))) {
        sprintf(g_avviso, "estratto in %s", g_dove);
    } else {
        ex_dlg_avviso("Non estratto", perche);
        strcpy(g_avviso, "non estratto");
    }
    stato();
}

static void estrai_tutto(void)
{
    unsigned int i, n, fatti = 0, saltati = 0;
    char         perche[192], primo[192];

    if (!g_z || g_creo) { strcpy(g_avviso, "apri prima un archivio"); stato(); return; }

    if (!chiedi_dove()) return;

    n = ex_zip_quante(g_z);
    primo[0] = '\0';

    for (i = 0; i < n; i++) {
        if (estrai_una(i, perche, sizeof(perche))) { fatti++; continue; }
        saltati++;
        /* ! IL PRIMO ERRORE E' QUELLO CHE SI MOSTRA, non l'ultimo: gli altri
         * ne sono quasi sempre la conseguenza, e una finestra per ognuno su
         * mille file sarebbe un dialogo da chiudere mille volte. */
        if (!primo[0]) strncpy(primo, perche, sizeof(primo) - 1);
    }

    if (saltati) {
        char t[320];

        sprintf(t, "Estratti %u file su %u.\n\nIl primo che non e' andato:\n%s",
                fatti, n, primo);
        ex_dlg_avviso("Qualcosa non e' uscito", t);
    }

    sprintf(g_avviso, "%u estratti in %s%s", fatti, g_dove,
            saltati ? ", qualcuno no" : "");
    stato();
}

/* -----------------------------------------------------------------------------
 * Info
 * --------------------------------------------------------------------------- */
static void istruzioni(void)
{
    ex_dlg_avviso("Istruzioni",
        "File, Apri... mostra cosa c'e' dentro un archivio.\n"
        "Comandi, Estrai tutto lo svuota in una cartella a scelta;\n"
        "Invio o doppio clic estraggono una riga sola.\n\n"
        "Per farne uno: File, Nuovo..., poi Comandi, Aggiungi file...\n"
        "quante volte serve, e infine File, Finisci - senza quello il\n"
        "file non e' un archivio e nessuno lo aprira'.\n\n"
        "Si scrive senza comprimere (store); in lettura capisce anche\n"
        "deflate, che e' il metodo di quasi tutti gli archivi veri.");
}

static void informazioni(void)
{
    char t[512];

    exinfo_testo(t, sizeof(t), "Archivi", VERSIONE_APP,
                 "Apre e crea archivi ZIP. Il formato lo sa exzip.so, "
                 "la stessa libreria che usa il comando `zip`.");
    ex_dlg_avviso("Informazioni su", t);
}

/* -----------------------------------------------------------------------------
 * La procedura della finestra
 * --------------------------------------------------------------------------- */
static void esci_se_si_puo(void)
{
    if (!lascia_andare()) return;
    if (g_z) ex_zip_chiudi(g_z);
    ex_esci(0);
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    unsigned int c;

    switch (msg) {
    case EXM_COMANDO:
        g_avviso[0] = '\0';

        if (wp == ID_APRI)       { apri_con_dialogo(); break; }
        if (wp == ID_NUOVO)      { nuovo();            break; }
        if (wp == ID_FINISCI)    { finisci();          break; }
        if (wp == ID_CHIUDI)     { chiudi_archivio();  break; }
        if (wp == ID_ESCI)       { esci_se_si_puo();   return 0; }

        if (wp == ID_ESTRAI)     { estrai_scelto();    break; }
        if (wp == ID_ESTRAI_TUT) { estrai_tutto();     break; }
        if (wp == ID_AGGIUNGI)   { aggiungi();         break; }

        if (wp == ID_ISTRUZIONI) { istruzioni();       break; }
        if (wp == ID_INFO)       { informazioni();     break; }

        /* Doppio clic su una riga: estrae quella. Il clic semplice sceglie e
         * basta, come nel dialogo dei file e nel file manager. */
        if (wp == ID_LISTA) {
            if (EX_APRIRE(lp)) estrai_scelto();
            break;
        }
        return 0;

    case EXM_TASTO:
        g_avviso[0] = '\0';
        c = wp & KBD_KEY_MASK;

        if (wp & KBD_MOD_CTRL) {
            if (c == 'o' || c == 'O') { apri_con_dialogo(); break; }
            if (c == 'n' || c == 'N') { nuovo();            break; }
            if (c == 'q' || c == 'Q') { esci_se_si_puo();   return 0; }
        }
        /* L'Invio sulla lista arriva qui: la lista non se lo mangia. */
        if (c == '\n' || c == '\r') { estrai_scelto(); break; }
        return ex_procedura_base(f, msg, wp, lp);

    case EXM_CHIUDI:
        esci_se_si_puo();
        return 0;

    /* ! THE HEADER ROW IS REPAINTED HERE, and not only after something
     * changes. ex_procedura_base() wipes the client area and knows nothing
     * about a row this program draws itself: anything that brings this window
     * back would erase it - and what brings it back is the server, every time
     * a dialog that covered it closes.
     *
     * ! AND THIS IS THE SECOND TIME IN ONE DAY. The same defect was found this
     * morning in the path line of lib/exdlg/exdlg.c, written down, and then
     * made again here in a brand new program a few hours later. Whatever a
     * window draws of its own belongs in its own EXM_DISEGNA; drawing it only
     * where it changes works right up until someone opens a dialog on top. */
    case EXM_DISEGNA:
        ex_procedura_base(f, msg, wp, lp);
        riga_intestazione();
        return 0;

    case EXM_MISURA: {
        int w = EX_X(lp), h = EX_Y(lp);

        ex_misura(g_lista, w - AREA_X * 2, h - AREA_Y - BASSO);
        ex_sposta(g_stato, 6, h - 22);
        ex_misura(g_stato, w - 12, 16);
        break;
    }

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    ridisegna();
    return 0;
}

int main(int argc, char **argv)
{
    ExMsg m;

    g_f = ex_crea("finestra", "Archivi",
                  EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                  EX_AUTO, EX_AUTO, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("archivi: il server a finestre non risponde.\n");
        printf("         Avvialo con:  exwin\n");
        return 1;
    }

    g_menu = ex_menu(g_f);
    ex_menu_voce(g_menu, "File", "Apri...\tCtrl+O", ID_APRI);
    ex_menu_voce(g_menu, "File", "Nuovo...\tCtrl+N", ID_NUOVO);
    ex_menu_voce(g_menu, "File", "Finisci",          ID_FINISCI);
    ex_menu_voce(g_menu, "File", "-",                0);
    ex_menu_voce(g_menu, "File", "Chiudi",           ID_CHIUDI);
    ex_menu_voce(g_menu, "File", "Esci\tCtrl+Q",     ID_ESCI);

    ex_menu_voce(g_menu, "Comandi", "Estrai il file scelto\tInvio", ID_ESTRAI);
    ex_menu_voce(g_menu, "Comandi", "Estrai tutto...",              ID_ESTRAI_TUT);
    ex_menu_voce(g_menu, "Comandi", "-",                            0);
    ex_menu_voce(g_menu, "Comandi", "Aggiungi file...",             ID_AGGIUNGI);

    ex_menu_voce(g_menu, "Info", "Istruzioni",      ID_ISTRUZIONI);
    ex_menu_voce(g_menu, "Info", "Informazioni su", ID_INFO);

    g_lista = ex_crea("lista", "", EX_FIGLIO,
                      AREA_X, AREA_Y, FIN_W - AREA_X * 2,
                      FIN_H - AREA_Y - BASSO, g_f, ID_LISTA, 0);
    if (!g_lista) {
        printf("archivi: non riesco a creare l'elenco\n");
        return 1;
    }

    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      6, FIN_H - 22, FIN_W - 12, 16, g_f, 0, 0);

    ex_fuoco(g_lista);

    if (argc >= 2) apri(argv[1]);
    else           stato();

    ridisegna();

    while (ex_prendi_msg(&m)) ex_smista(&m);

    if (g_z) ex_zip_chiudi(g_z);
    return 0;
}
