/* =============================================================================
 * exwin/bin/pm/pm.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il program manager: scrivania, barra delle applicazioni, menu di avvio
 *
 *     /exwin/bin/pm            la scrivania
 *     /exwin/bin/pm -s FILE    con un'immagine di sfondo
 *
 * ! STA IN /exwin E NON IN /bin, ED E' UNA DECISIONE, NON UN VEZZO. I
 * programmi di /bin si lanciano da una shell e parlano con un terminale;
 * questi vogliono il server a finestre, e lanciati da una shell senza server
 * non fanno niente. Tenerli mescolati vorrebbe dire un `ls /bin` in cui meta'
 * dei nomi non si puo' usare li' dove si sta guardando. La stessa ragione per
 * cui i driver stanno in /dev e non in /bin.
 *
 * ! E L'ELENCO DELLE APPLICAZIONI E' UN FILE, NON UNA TABELLA COMPILATA.
 * /exwin/lib/applicazioni.txt: aggiungerne una e' una riga. Un elenco dentro
 * il binario vorrebbe dire rifare il program manager per ogni applicazione
 * nuova — e chi installa un programma non ha i sorgenti.
 *
 * ! LA BARRA STA SOPRA A TUTTO (EX_SOPRA). Se una finestra qualunque potesse
 * coprirla, l'unico modo di tornare al menu sarebbe spostare quella finestra —
 * e con una finestra a schermo intero non si potrebbe affatto.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"

#define BARRA_H     28
#define MENU_W      220
#define VOCE_H      24
#define APP_MAX     16

#define ID_AVVIO    1
#define ID_VOCE     100     /* ID_VOCE + n = la voce n del menu */
#define ID_ESCI     90
#define ID_SPEGNI   91
#define ID_GESTISCI 92

/* La finestra che gestisce l'elenco. */
#define ID_G_LISTA  1
#define ID_G_AGG    2
#define ID_G_TOGLI  3
#define ID_G_AUTO   4
#define ID_G_SALVA  5
#define ID_G_CHIUDI 6

#define GEST_W      460
#define GEST_H      356


typedef struct {
    char nome[32];
    char percorso[96];
} App;

static App          g_app[APP_MAX];
static unsigned int g_app_n = 0;
static unsigned int g_da_cdrom = 0;

/* L'applicazione che parte da sola, e da dove si e' letto l'elenco.
 *
 * ! IL PERCORSO DEL FILE SI RICORDA, e non si ricalcola al momento di
 * salvare: l'elenco puo' venire da /exwin o da /cdrom, e riscrivere «quello
 * che avrei cercato per primo» vorrebbe dire, avviando dal CD, provare a
 * scrivere in un posto da cui non si e' letto. */
static char g_avvio[96]  = "";
static char g_elenco[96] = "";

static ExFinestra g_barra, g_menu = 0;
static unsigned int g_sw, g_sh;

/* -----------------------------------------------------------------------------
 * L'elenco delle applicazioni
 *
 * ! UNA RIGA SBAGLIATA SI SALTA, NON FERMA TUTTO. Un file di elenco e' una
 * cosa che si modifica a mano: un refuso non deve lasciare senza scrivania chi
 * lo ha fatto, deve solo far mancare quella voce.
 * --------------------------------------------------------------------------- */
static void taglia(char *s)
{
    int i = (int)strlen(s);

    while (i > 0 && (s[i-1] == ' ' || s[i-1] == '\t' ||
                     s[i-1] == '\n' || s[i-1] == '\r')) s[--i] = '\0';
}

static void applicazioni_leggi(const char *percorso)
{
    int fd = open(percorso, O_RDONLY);

    if (fd >= 0 && strncmp(percorso, "/cdrom", 6) == 0) g_da_cdrom = 1;
    if (fd >= 0) {
        strncpy(g_elenco, percorso, sizeof(g_elenco) - 1);
        g_elenco[sizeof(g_elenco) - 1] = '\0';
    }
    char buf[2048];
    int n, i, r = 0;
    char riga[160];

    if (fd < 0) return;
    n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    for (i = 0; i <= n; i++) {
        if (buf[i] != '\n' && buf[i] != '\0') {
            if (r + 1 < (int)sizeof(riga)) riga[r++] = buf[i];
            continue;
        }
        riga[r] = '\0';
        r = 0;

        {
            char *barra = strchr(riga, '|');
            char *p;

            /* ! LE DIRETTIVE SI RICONOSCONO PRIMA DELLE VOCI, e si distinguono
             * per l'assenza della barra verticale: un'applicazione che si
             * chiamasse «avvio» resta una voce, perche' la barra ce l'ha. */
            if (riga[0] == '@') {
                if (strncmp(riga, "@avvio", 6) == 0) {
                    char *q = riga + 6;

                    while (*q == ' ' || *q == '\t') q++;
                    taglia(q);
                    strncpy(g_avvio, q, sizeof(g_avvio) - 1);
                    g_avvio[sizeof(g_avvio) - 1] = '\0';
                }
                continue;
            }

            if (riga[0] == '#' || riga[0] == '\0' || !barra) continue;
            if (g_app_n >= APP_MAX) break;

            *barra = '\0';
            p = barra + 1;
            while (*p == ' ' || *p == '\t') p++;

            taglia(riga);
            taglia(p);
            if (riga[0] == '\0' || p[0] == '\0') continue;

            strncpy(g_app[g_app_n].nome, riga, sizeof(g_app[0].nome) - 1);
            strncpy(g_app[g_app_n].percorso, p, sizeof(g_app[0].percorso) - 1);
            g_app_n++;
        }
    }
}

/* -----------------------------------------------------------------------------
 * Riscrivere l'elenco
 *
 * ! IL FILE SI RISCRIVE INTERO, E I COMMENTI SI RIMETTONO. Un file di
 * configurazione che dopo la prima modifica da un programma perde le righe che
 * spiegano com'e' fatto e' un file che nessuno sa piu' correggere a mano — e
 * questo si corregge a mano proprio quando il program manager non parte.
 * Rimetterli e' una dozzina di righe scritte qui.
 *
 * ! E NON SI SCRIVE SOPRA QUELLO CHE NON SI E' RIUSCITI A SCRIVERE. Si crea un
 * file accanto e lo si sposta al posto del vecchio: se la scrittura fallisce a
 * meta' — disco pieno, corrente che va via — l'elenco di prima e' ancora
 * intero. Riscrivere in luogo lascerebbe una scrivania senza applicazioni e
 * nessun indizio sul perche'.
 * --------------------------------------------------------------------------- */
static const char INTESTAZIONE[] =
    "# L'elenco delle applicazioni grafiche di EX-OS.\n"
    "#\n"
    "# Una riga per applicazione:   nome mostrato | percorso dell'eseguibile\n"
    "# Le righe che cominciano con # e quelle vuote si saltano.\n"
    "#\n"
    "# Le direttive cominciano con @ e non hanno la barra verticale:\n"
    "#   @avvio <percorso>   l'applicazione che parte da sola con la scrivania\n"
    "#\n"
    "# Lo riscrive il menu di avvio, voce Applicazioni..., e resta\n"
    "# modificabile a mano.\n"
    "\n";

static int scrivi_tutto(int fd, const char *s)
{
    int n = (int)strlen(s), fatti = 0, k;

    while (fatti < n) {
        k = (int)write(fd, s + fatti, (unsigned int)(n - fatti));
        if (k <= 0) return 0;
        fatti += k;
    }
    return 1;
}

/* ! QUALE PASSO E' FALLITO SI DICE, e non si lascia indovinare. «Non salvato»
 * puo' voler dire quattro cose diverse — non posso creare, non posso scrivere,
 * non posso rinominare, non so nemmeno dove — e sono quattro riparazioni
 * diverse. Il numero che finisce nel messaggio e' errno. */
static char g_perche[64] = "";

/* Rende 1 se ha salvato, 0 se no. */
static int applicazioni_scrivi(void)
{
    char         tmp[112];
    int          fd;
    unsigned int i;
    int          ok = 1;

    g_perche[0] = '\0';

    if (g_elenco[0] == '\0') { strcpy(g_perche, "non so da dove l'ho letto"); return 0; }

    strncpy(tmp, g_elenco, sizeof(tmp) - 5);
    tmp[sizeof(tmp) - 5] = '\0';
    strcat(tmp, ".nuo");

    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { sprintf(g_perche, "non creo il file accanto (%d)", errno); return 0; }

    ok = scrivi_tutto(fd, INTESTAZIONE);

    if (ok && g_avvio[0]) {
        char r[128];

        sprintf(r, "@avvio %s\n\n", g_avvio);
        ok = scrivi_tutto(fd, r);
    }

    for (i = 0; ok && i < g_app_n; i++) {
        char r[160];

        sprintf(r, "%s | %s\n", g_app[i].nome, g_app[i].percorso);
        ok = scrivi_tutto(fd, r);
    }

    close(fd);

    if (!ok) {
        sprintf(g_perche, "non scrivo (%d)", errno);
        unlink(tmp);
        return 0;
    }
    /* ! IL rename DI EX-OS NON SOSTITUISCE, e POSIX invece si'. Con la
     * destinazione gia' presente rende -EEXIST, e il salvataggio falliva
     * SEMPRE — su un sistema installato, dove l'elenco c'e' per definizione.
     * Il messaggio diceva «sola lettura, o permessi mancanti», che erano
     * tutt'e due false: l'ha smentito il numero, 17, messo li' apposta per
     * smettere di indovinare.
     *
     * ! TOGLIERE PRIMA APRE UNA FINESTRA, E LA SI DICHIARA. Fra unlink e
     * rename l'elenco non esiste: se la corrente va via li' in mezzo, al
     * riavvio la scrivania non ha applicazioni. Ma il file NUOVO e' gia'
     * scritto per intero accanto — `applicazioni.txt.nuo` — quindi si ripara
     * rinominandolo a mano. E' meno bello di uno scambio atomico e molto
     * meglio di scrivere in luogo, dove una caduta a meta' lascia un elenco
     * troncato che nessuno sa di dover riparare. */
    unlink(g_elenco);

    if (rename(tmp, g_elenco) != 0) {
        sprintf(g_perche, "non rinomino (%d)", errno);
        return 0;
    }
    return 1;
}

/* =============================================================================
 * «Applicazioni...» — aggiungere e togliere voci dal menu
 *
 * ! LA VOCE STA NEL MENU E NON IN UN PROGRAMMA A PARTE, ed e' la richiesta
 * presa alla lettera: un programma esterno per gestire il menu sarebbe una
 * voce del menu che serve a gestire il menu, cioe' qualcosa che si puo'
 * togliere per sbaglio lasciando la scrivania senza modo di rimettercela.
 * Dentro il program manager c'e' sempre.
 *
 * ! E LE MODIFICHE VALGONO SUBITO, SENZA RIAVVIARE LA SCRIVANIA. L'elenco in
 * memoria e' lo stesso che disegna il menu: si tocca quello, e il menu dopo e'
 * gia' diverso. Salvare serve a farlo durare, non a farlo valere.
 * ============================================================================= */
static ExFinestra g_gest = 0, g_gest_lista = 0, g_gest_stato = 0, g_gest_dove = 0;

static void gest_mostra(void)
{
    unsigned int i;
    char         r[160];

    if (!g_gest_lista) return;

    ex_lista_svuota(g_gest_lista);

    for (i = 0; i < g_app_n; i++) {
        /* ! IL SEGNO DELL'AVVIO AUTOMATICO SI VEDE NELL'ELENCO, e non in un
         * pannello a parte: la domanda «quale parte da sola?» si fa guardando
         * la stessa riga su cui si sta per premere «Togli». */
        int automatica = (g_avvio[0] != '\0' &&
                          strcmp(g_avvio, g_app[i].percorso) == 0);

        sprintf(r, "%s %-18s %s", automatica ? "*" : " ",
                g_app[i].nome, g_app[i].percorso);
        ex_lista_aggiungi(g_gest_lista, r);
    }

    /* Un avvio automatico che NON e' fra le voci si mostra lo stesso: e' una
     * configurazione legittima (un pannello, un orologio) e nasconderla
     * vorrebbe dire un programma che parte e nessun posto dove vederlo. */
    if (g_avvio[0]) {
        unsigned int k;
        int          fra_le_voci = 0;

        for (k = 0; k < g_app_n; k++)
            if (strcmp(g_avvio, g_app[k].percorso) == 0) fra_le_voci = 1;

        if (!fra_le_voci) {
            sprintf(r, "* (solo avvio)      %s", g_avvio);
            ex_lista_aggiungi(g_gest_lista, r);
        }
    }
}

static void gest_dico(const char *t)
{
    if (g_gest_stato) ex_testo_metti(g_gest_stato, t);
}

static void gest_aggiungi(void)
{
    char perc[96] = "/exwin/bin/";
    char nome[32] = "";

    if (!ex_dlg_apri(perc, sizeof(perc))) { gest_dico("aggiunta annullata"); return; }
    if (g_app_n >= APP_MAX) { gest_dico("l'elenco e' pieno"); return; }

    /* Il nome proposto e' l'ultimo pezzo del percorso: quasi sempre e' quello
     * giusto, e chi vuole un altro lo corregge invece di batterlo tutto. */
    {
        int i = (int)strlen(perc);

        while (i > 0 && perc[i - 1] != '/') i--;
        strncpy(nome, perc + i, sizeof(nome) - 1);
        nome[sizeof(nome) - 1] = '\0';
    }

    if (!ex_dlg_riga("Nome nel menu", "Come si deve chiamare la voce:",
                     nome, sizeof(nome))) {
        gest_dico("aggiunta annullata");
        return;
    }
    if (nome[0] == '\0') { gest_dico("senza nome non si aggiunge"); return; }

    strncpy(g_app[g_app_n].nome, nome, sizeof(g_app[0].nome) - 1);
    g_app[g_app_n].nome[sizeof(g_app[0].nome) - 1] = '\0';
    strncpy(g_app[g_app_n].percorso, perc, sizeof(g_app[0].percorso) - 1);
    g_app[g_app_n].percorso[sizeof(g_app[0].percorso) - 1] = '\0';
    g_app_n++;

    gest_mostra();
    gest_dico("aggiunta: ricordati di salvare");
}

static void gest_togli(void)
{
    unsigned int s = ex_lista_scelta(g_gest_lista);
    unsigned int k;

    if (s >= g_app_n) { gest_dico("scegli prima una voce"); return; }

    /* ! TOGLIENDO LA VOCE CHE PARTE DA SOLA SI TOGLIE ANCHE L'AVVIO. Lasciarlo
     * puntato a un programma che non e' piu' nel menu non sarebbe sbagliato in
     * se' — l'avvio e' indipendente — ma qui l'utente ha detto «via questa», e
     * un programma che continua a partire dopo che lo si e' tolto e' l'ultima
     * cosa che si aspetta. */
    if (g_avvio[0] && strcmp(g_avvio, g_app[s].percorso) == 0) g_avvio[0] = '\0';

    for (k = s; k + 1 < g_app_n; k++) g_app[k] = g_app[k + 1];
    g_app_n--;

    gest_mostra();
    gest_dico("tolta: ricordati di salvare");
}

static void gest_auto(void)
{
    unsigned int s = ex_lista_scelta(g_gest_lista);

    if (s >= g_app_n) { gest_dico("scegli prima una voce"); return; }

    /* Premuta sulla voce che gia' parte da sola: la si toglie. E' l'unico modo
     * ovvio di dire «nessuna» senza aggiungere un pulsante che serve una volta
     * nella vita. */
    if (g_avvio[0] && strcmp(g_avvio, g_app[s].percorso) == 0) {
        g_avvio[0] = '\0';
        gest_dico("nessun avvio automatico: ricordati di salvare");
    } else {
        strncpy(g_avvio, g_app[s].percorso, sizeof(g_avvio) - 1);
        g_avvio[sizeof(g_avvio) - 1] = '\0';
        gest_dico("partira' da sola: ricordati di salvare");
    }

    gest_mostra();
}

static void gest_salva(void)
{
    if (applicazioni_scrivi()) { gest_dico("salvato."); return; }

    /* ! IL MOTIVO NON SI RICAVA DA g_da_cdrom, E QUI CI SI E' SBAGLIATI UNA
     * VOLTA. Quella variabile dice solo se il percorso comincia con /cdrom —
     * ma avviando DAL CD la radice E' il CD, quindi l'elenco si legge da
     * /exwin/lib/... e la variabile resta falsa. Il messaggio che si
     * appoggiava a lei diceva la cosa sbagliata proprio nel caso piu' comune:
     * provare il sistema da CD.
     *
     * Le due cause vere sono «il supporto e' di sola lettura» e «non ho i
     * permessi», e da qui non si distinguono senza chiederlo al sistema. Si
     * dicono tutt'e due: chi legge sa quale delle due lo riguarda. */
    {
        char m[128];

        sprintf(m, "non salvato: %s", g_perche[0] ? g_perche
                                    : "sola lettura, o permessi mancanti");
        gest_dico(m);
    }
}

static long gest_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        ex_distruggi(g_gest);
        g_gest = g_gest_lista = g_gest_stato = g_gest_dove = 0;
        return 0;

    case EXM_COMANDO:
        if (wp == ID_G_AGG)    { gest_aggiungi(); break; }
        if (wp == ID_G_TOGLI)  { gest_togli();    break; }
        if (wp == ID_G_AUTO)   { gest_auto();     break; }
        if (wp == ID_G_SALVA)  { gest_salva();    break; }
        if (wp == ID_G_CHIUDI) {
            ex_distruggi(g_gest);
            g_gest = g_gest_lista = g_gest_stato = g_gest_dove = 0;
            return 0;
        }
        if (wp == ID_G_LISTA)  break;      /* una riga scelta: niente da fare */
        return 0;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
    return 0;
}

static void gest_apri(void)
{
    int x, y;

    /* Gia' aperta: si porta davanti invece di aprirne una seconda, che sarebbe
     * due elenchi della stessa cosa che si contraddicono. */
    if (g_gest) { ex_mostra(g_gest, 1); return; }

    x = (int)g_sw > GEST_W ? ((int)g_sw - GEST_W) / 2 : 0;
    y = (int)g_sh > GEST_H ? ((int)g_sh - GEST_H) / 2 : 0;

    g_gest = ex_crea("finestra", "Applicazioni del menu",
                     EX_TITOLO | EX_BORDO | EX_CHIUDI,
                     x, y, GEST_W, GEST_H, 0, 0, gest_proc);
    if (!g_gest) return;

    g_gest_lista = ex_crea("lista", "", EX_FIGLIO,
                           6, 6, GEST_W - 12, GEST_H - 126,
                           g_gest, ID_G_LISTA, 0);

    ex_crea("pulsante", "Aggiungi...", EX_FIGLIO,
            6, GEST_H - 114, 110, 24, g_gest, ID_G_AGG, 0);
    ex_crea("pulsante", "Togli", EX_FIGLIO,
            122, GEST_H - 114, 80, 24, g_gest, ID_G_TOGLI, 0);
    ex_crea("pulsante", "Avvio automatico", EX_FIGLIO,
            208, GEST_H - 114, 150, 24, g_gest, ID_G_AUTO, 0);

    ex_crea("etichetta", "L'asterisco segna cio' che parte da solo.", EX_FIGLIO,
            6, GEST_H - 86, GEST_W - 12, 16, g_gest, 0, 0);

    /* ! IL PERCORSO DEL FILE HA UN'ETICHETTA SUA, e non divide la riga con i
     * messaggi. Prima erano la stessa: aprendo si leggeva il percorso, al
     * primo messaggio spariva — e proprio quando serviva sapere QUALE file non
     * si era riuscito a scrivere. */
    g_gest_dove = ex_crea("etichetta", "", EX_FIGLIO,
                          6, GEST_H - 66, GEST_W - 12, 16, g_gest, 0, 0);

    /* ! E LO STATO PRENDE TUTTA LA LARGHEZZA, su una riga sua. Stretto accanto
     * ai pulsanti, un messaggio un po' lungo finiva SOTTO di loro: si leggeva
     * «non salvato: non riesco a scrivere» e il resto spariva sotto «Salva». */
    g_gest_stato = ex_crea("etichetta", "", EX_FIGLIO,
                           6, GEST_H - 46, GEST_W - 12, 16, g_gest, 0, 0);

    ex_crea("pulsante", "Salva", EX_FIGLIO,
            GEST_W - 176, GEST_H - 28, 80, 24, g_gest, ID_G_SALVA, 0);
    ex_crea("pulsante", "Chiudi", EX_FIGLIO,
            GEST_W - 90, GEST_H - 28, 80, 24, g_gest, ID_G_CHIUDI, 0);

    gest_mostra();
    if (g_gest_dove && g_elenco[0]) ex_testo_metti(g_gest_dove, g_elenco);
    ex_procedura_base(g_gest, EXM_DISEGNA, 0, 0);
}

/* -----------------------------------------------------------------------------
 * Il menu di avvio
 *
 * ! E' UNA FINESTRA, NON UN DISEGNO SULLA BARRA. Cosi' sta sopra alle altre
 * senza casi particolari, si chiude distruggendola, e i clic sulle sue voci
 * arrivano come EXM_COMANDO — cioe' con lo stesso meccanismo di tutto il
 * resto, invece che con un calcolo di coordinate a mano.
 * --------------------------------------------------------------------------- */
static long menu_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp);

static void menu_chiudi(void)
{
    if (g_menu) { ex_distruggi(g_menu); g_menu = 0; }
}

static void menu_apri(void)
{
    unsigned int i;
    int h;

    if (g_menu) { menu_chiudi(); return; }   /* premuto due volte: si chiude */

    /* ! «ESCI» E «SPEGNI» CI SONO ANCHE SENZA APPLICAZIONI. Un menu che si
     * rifiutasse di aprirsi perche' l'elenco e' vuoto lascerebbe senza modo
     * di uscire dalla grafica — e uscire e' proprio cio' che si vuole fare
     * quando non c'e' niente da avviare. */
    /* Tre voci fisse adesso — «Applicazioni...», «Esci», «Spegni» — e non
     * piu' due. */
    h = (int)(g_app_n * VOCE_H) + 8 + 3 * VOCE_H + 6;

    g_menu = ex_crea("finestra", "", EX_BORDO | EX_SOPRA,
                     4, (int)g_sh - BARRA_H - h - 2, MENU_W, h, 0, 0, menu_proc);
    if (!g_menu) return;

    for (i = 0; i < g_app_n; i++)
        ex_crea("pulsante", g_app[i].nome, EX_FIGLIO,
                4, 4 + (int)i * VOCE_H, MENU_W - 8, VOCE_H - 2,
                g_menu, ID_VOCE + i, 0);

    /* Una riga a separare le applicazioni da cio' che spegne le cose: sono
     * due categorie diverse, e un clic sbagliato costa molto di piu' da una
     * parte che dall'altra. */
    ex_crea("separatore", "", EX_FIGLIO,
            6, 6 + (int)g_app_n * VOCE_H, MENU_W - 12, 2, g_menu, 0, 0);

    /* ! «APPLICAZIONI...» STA SOTTO LA RIGA, con «Esci» e «Spegni», e non fra
     * le applicazioni: non e' un programma da avviare, e' una cosa che la
     * scrivania sa fare. Metterla in mezzo alle voci vorrebbe anche dire che
     * si sposta ogni volta che se ne aggiunge una. */
    ex_crea("pulsante", "Applicazioni...", EX_FIGLIO,
            4, 10 + (int)g_app_n * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_GESTISCI, 0);
    ex_crea("pulsante", "Esci", EX_FIGLIO,
            4, 10 + (int)(g_app_n + 1) * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_ESCI, 0);
    ex_crea("pulsante", "Spegni", EX_FIGLIO,
            4, 10 + (int)(g_app_n + 2) * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_SPEGNI, 0);

    ex_procedura_base(g_menu, EXM_DISEGNA, 0, 0);
}

static void avvia(unsigned int n)
{
    char *argv[2];

    if (n >= g_app_n) return;

    argv[0] = g_app[n].percorso;
    argv[1] = 0;

    /* ! SE NON PARTE SI DICE, e non si resta zitti: un menu in cui premere una
     * voce non fa niente e non spiega niente e' peggio di un menu senza quella
     * voce. Il messaggio va sulla seriale perche' qui non c'e' un terminale a
     * cui dirlo — la scrivania e' l'unica cosa a video. */
    if (spawn_ex(argv[0], argv, 0, 0, 0) < 0) {
        char m[160];
        sprintf(m, "pm: non riesco ad avviare %s", argv[0]);
        log_seriale(m);
    }
}

static long menu_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO) {
        menu_chiudi();

        /* ! «ESCI» CHIUDE SOLO LA SCRIVANIA, NON IL SERVER. Il server e' di
         * chi lo ha avviato — `exwin`, o kernel.cfg — e potrebbe avere altre
         * finestre aperte: chiuderlo da qui vorrebbe dire portarsi via il
         * lavoro di qualcun altro. Chi torna alla shell trova la console di
         * testo con Alt+Fn, che e' li' apposta. */
        if (wp == ID_ESCI) {
            log_seriale("pm: uscita chiesta dal menu");
            ex_esci(0);
            return 0;
        }

        /* ! SPEGNERE SINCRONIZZA I DISCHI, e lo fa il kernel: qui si chiede e
         * basta. Se rende, vuol dire che ha rifiutato — e allora si dice,
         * invece di lasciare una scrivania che sembra aver ignorato il
         * comando. */
        if (wp == ID_SPEGNI) {
            log_seriale("pm: spegnimento chiesto dal menu");
            reboot(EXOS_RB_POWEROFF);
            log_seriale("pm: il kernel ha rifiutato lo spegnimento");
            return 0;
        }

        if (wp == ID_GESTISCI) { gest_apri(); return 0; }

        /* ! QUI SOTTO CI ARRIVA SOLO CIO' CHE NON E' UNA VOCE FISSA, e il
         * controllo serve: `avvia` prende `wp - ID_VOCE` senza segno, quindi
         * un id piu' piccolo di ID_VOCE diventerebbe un numero enorme. Il
         * confronto lo ferma prima. */
        if (wp >= ID_VOCE) avvia(wp - ID_VOCE);
        return 0;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

/* -----------------------------------------------------------------------------
 * La barra
 * --------------------------------------------------------------------------- */
static long barra_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO && wp == ID_AVVIO) { menu_apri(); return 0; }
    return ex_procedura_base(f, msg, wp, lp);
}

/* -----------------------------------------------------------------------------
 * La scrivania si ridisegna da se'
 *
 * ! SENZA UNA PROCEDURA, IL RIDISEGNO DI DEFAULT LA RIEMPIE DI GRIGIO. Fino al
 * 18 agosto 2026 la scrivania si creava con `0` al posto della procedura, e il
 * colore e l'immagine si mettevano UNA VOLTA subito dopo. Poi bastava un clic
 * sullo sfondo — che fa riordinare le finestre e quindi chiedere un ridisegno —
 * perche' ex_procedura_base ripulisse tutto col grigio di una finestra vuota:
 * la scrivania si cancellava, immagine compresa, e non tornava piu'.
 *
 * ! DISEGNARE UNA VOLTA VA BENE FINCHE' NESSUNO CHIEDE DI RIDISEGNARE, ed e' la
 * forma piu' facile di questo errore: funziona perfettamente finche' non si
 * tocca niente. Chi possiede dei pixel deve saperli rifare su richiesta.
 *
 * ! E SI RILEGGE IL FILE OGNI VOLTA, che e' il prezzo dichiarato: tenere
 * l'immagine decodificata vorrebbe dire una copia da 1,8 MB in un processo che
 * la usa quando lo scoprono. Succede quando la scrivania viene scoperta, non a
 * ogni fotogramma.
 *
 * Rende 0 se c'era un'immagine e non si e' potuta leggere.
 * --------------------------------------------------------------------------- */
static ExFinestra  g_scr = 0;
static const char *g_sfondo = 0;

static int scrivania_disegna(void)
{
    if (!g_scr) return 1;

    ex_riempi(g_scr, 0, 0, (int)g_sw, (int)g_sh - BARRA_H, EX_SCRIVANIA);
    if (g_sfondo && !ex_immagine(g_scr, g_sfondo, 0, 0)) return 0;
    return 1;
}

static long scr_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    /* ! E SI MANDA AL SERVER SUBITO. Disegnare riempie la zona condivisa; e'
     * ex_aggiorna che dice al server di ricomporla. Senza, la scrivania
     * sarebbe giusta nella memoria del processo e grigia sullo schermo — che
     * e' esattamente il difetto di prima, con una causa in piu' da cercare. */
    if (msg == EXM_DISEGNA) { scrivania_disegna(); ex_aggiorna(f); return 0; }
    return ex_procedura_base(f, msg, wp, lp);
}

int main(int argc, char **argv)
{
    ExMsg m;
    const char *sfondo = 0;
    int i;

    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) sfondo = argv[++i];

    ex_schermo(&g_sw, &g_sh);
    if (g_sw == 0) {
        printf("pm: il server a finestre non risponde, o lo schermo e' in testo\n");
        return 1;
    }

    /* ! SI CERCA IN DUE POSTI, E L'ORDINE CONTA. Su un sistema installato
     * l'albero sta in /exwin; avviando dal CD sta sotto /cdrom, perche' il
     * kernel monta il lettore come radice solo quando non c'e' il floppy.
     * Cercare solo il primo vorrebbe dire una scrivania senza applicazioni
     * ogni volta che si prova il CD — e il menu vuoto non dice perche'. */
    applicazioni_leggi("/exwin/lib/applicazioni.txt");
    if (g_app_n == 0 && g_avvio[0] == '\0')
        applicazioni_leggi("/cdrom/exwin/lib/applicazioni.txt");

    /* ! E I PERCORSI DELLE APPLICAZIONI SEGUONO L'ELENCO. Se l'elenco l'abbiamo
     * trovato sul CD, anche i programmi stanno li': un elenco che dice
     * /exwin/bin/filemgr letto da /cdrom fa premere una voce che non avvia
     * niente. */
    if (g_app_n && g_da_cdrom) {
        unsigned int k;
        for (k = 0; k < g_app_n; k++)
            if (strncmp(g_app[k].percorso, "/exwin/", 7) == 0) {
                char t[96];
                strcpy(t, "/cdrom");
                strncat(t, g_app[k].percorso, sizeof(t) - 8);
                strncpy(g_app[k].percorso, t, sizeof(g_app[0].percorso) - 1);
            }
    }

    /* La scrivania: una finestra come le altre, con lo stile che la tiene
     * sotto. E' il motivo per cui uno sfondo non e' un caso a parte. */
    {
        ExFinestra scr = ex_crea("finestra", "", EX_SFONDO,
                                 0, 0, (int)g_sw, (int)g_sh - BARRA_H,
                                 0, 0, scr_proc);
        if (!scr) {
            /* ! IL CONSIGLIO E' «exwin», NON IL DRIVER A MANO, e la
             * differenza non e' comodita': wserver.drv avviato cosi' nasce
             * sulla console della shell e le contende la tastiera. E' exwin
             * che lo fa ripartire su una console sua. Un messaggio che
             * suggerisce il comando sbagliato costa piu' di un messaggio che
             * non dice niente. */
            printf("pm: il server a finestre non risponde.\n");
            printf("    Avvialo con:  exwin\n");
            return 1;
        }
        g_scr = scr;
        g_sfondo = sfondo;

        if (!scrivania_disegna()) {
            char msg[160];
            sprintf(msg, "pm: %s: formato non riconosciuto", sfondo);
            log_seriale(msg);
        }
        ex_aggiorna(scr);
    }

    g_barra = ex_crea("finestra", "", EX_SOPRA,
                      0, (int)g_sh - BARRA_H, (int)g_sw, BARRA_H,
                      0, 0, barra_proc);
    if (!g_barra) return 1;

    ex_crea("pulsante", "Avvio", EX_FIGLIO, 2, 2, 70, BARRA_H - 4,
            g_barra, ID_AVVIO, 0);
    /* =====================================================================
     * ! L'ANGOLO DESTRO E' DELL'OROLOGIO, e l'orologio e' un PROCESSO A
     * PARTE. Qui c'era la scritta «EX-OS», che non diceva niente che non si
     * sapesse gia'. La data e l'ora invece cambiano da sole, e devono farlo
     * qualunque cosa stia facendo il program manager: dentro di lui si
     * aggiornerebbero solo quando lui ha tempo.
     *
     * ! LO AVVIA LA SCRIVANIA PERCHE' E' ARREDAMENTO, non un'applicazione:
     * sta nella barra come il pulsante «Avvio», e nessuno dovrebbe doverlo
     * avviare a mano. Non passa dall'elenco delle applicazioni proprio per
     * questo — non e' una cosa che si sceglie.
     *
     * ! E SE NON PARTE RESTA LA SCRITTA. Un angolo vuoto non direbbe se
     * l'orologio manca o se e' l'ora a non funzionare; «EX-OS» al suo posto
     * dice che la barra e' quella di sempre e che l'orologio non c'e'.
     * ===================================================================== */
    {
        static const char *const dove[] = {
            "/exwin/bin/orologio",
            "/cdrom/exwin/bin/orologio"
        };
        int partito = 0, k;

        for (k = 0; k < 2 && !partito; k++) {
            char *av[2];

            av[0] = (char *)dove[k];
            av[1] = 0;
            if (spawn_ex(av[0], av, 0, 0, 0) >= 0) partito = 1;
        }

        if (!partito) {
            log_seriale("pm: l'orologio non parte, resta la scritta");
            ex_crea("etichetta", "EX-OS", EX_FIGLIO, (int)g_sw - 56, 6, 50, 16,
                    g_barra, 0, 0);
        }
    }

    ex_procedura_base(g_barra, EXM_DISEGNA, 0, 0);

    printf("pm: scrivania attiva, %u applicazioni nel menu\n", g_app_n);

    /* =====================================================================
     * ! L'AVVIO AUTOMATICO PARTE QUANDO LA SCRIVANIA E' PRONTA, NON PRIMA.
     * Un programma grafico avviato mentre la barra e la scrivania non
     * esistono ancora chiederebbe una finestra a un server che non ha
     * nessuno sotto: nel migliore dei casi nasce dietro la scrivania, nel
     * peggiore non nasce e non si capisce perche'. Qui sopra c'e' gia'
     * tutto, e la riga dopo entra nel ciclo dei messaggi.
     *
     * ! E SE NON PARTE SI DICE. E' l'unico programma che nessuno ha chiesto
     * esplicitamente in quel momento: se fallisse in silenzio, chi lo ha
     * configurato penserebbe che la direttiva non sia stata nemmeno letta.
     * ===================================================================== */
    if (g_avvio[0]) {
        char *av[2];
        char  perc[96];

        strncpy(perc, g_avvio, sizeof(perc) - 1);
        perc[sizeof(perc) - 1] = '\0';

        /* I percorsi seguono l'elenco, come per le voci: vedi sopra. */
        if (g_da_cdrom && strncmp(perc, "/exwin/", 7) == 0) {
            char t[96];

            strcpy(t, "/cdrom");
            strncat(t, g_avvio, sizeof(t) - 8);
            strncpy(perc, t, sizeof(perc) - 1);
            perc[sizeof(perc) - 1] = '\0';
        }

        av[0] = perc;
        av[1] = 0;

        if (spawn_ex(av[0], av, 0, 0, 0) < 0) {
            char msg[160];

            sprintf(msg, "pm: avvio automatico fallito: %s", perc);
            log_seriale(msg);
            printf("pm: avvio automatico fallito: %s\n", perc);
        } else {
            printf("pm: avvio automatico: %s\n", perc);
        }
    }

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}
