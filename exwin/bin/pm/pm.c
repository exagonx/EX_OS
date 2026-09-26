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
#include "exinfo.h"

/* +0.001 a ogni modifica: `pm -version` la stampa. Vedi EX_VERSIONE in libc.h. */
#define VERSIONE_APP "0.005"
EX_VERSIONE("pm", VERSIONE_APP);

#define BARRA_H     28
#define MENU_W      220
#define VOCE_H      24
#define APP_MAX     16

#define ID_AVVIO    1
#define ID_VOCE     100     /* ID_VOCE + n = la voce n del menu */
/* ! LE CATEGORIE HANNO UNA FASCIA LORO, E LONTANA. ID_CAT + n non deve poter
 * cadere dentro ID_VOCE + n, o premere una categoria avvierebbe un programma:
 * sedici applicazioni al massimo (APP_MAX), quindi duecento e' fuori portata
 * con margine. */
#define ID_CAT      200     /* ID_CAT + n = la categoria n */
#define ICONA_LATO   16     /* in una voce alta 24, con l'aria intorno */
#define ID_ESCI     90
#define ID_SPEGNI   91
#define ID_RIAVVIA  95
#define ID_GESTISCI 92
#define ID_INFO     93
#define ID_IMPOST   94
#define ID_REGISTRO 96

/* Le quattro risoluzioni: gli stessi nomi che accetta /dev/svga.drv, e
 * nello stesso ordine della tabella dentro Stage 2. */
#define ID_RIS      100     /* ..103: uno per modo */

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

    /* ! IL PERCORSO DELL'ICONA, NON L'ICONA, e la differenza conta: qui si
     * tiene una riga di file, e l'icona si apre quando il menu si disegna la
     * prima volta. Aprirle tutte all'avvio vorrebbe dire leggere e
     * decodificare venti file per una scrivania che magari nessuno apre. */
    char icona[96];
    ExIcona ic;                 /* 0 = non ancora aperta, o non c'e' */

    /* Vuota = la voce sta in cima al menu; piena = sta dentro quella
     * categoria, che nel menu si apre di lato. */
    char categoria[32];
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

/* Il sottomenu di una categoria, e quale: vedi sotto_apri(). */
static ExFinestra   g_sotto = 0;
static unsigned int g_sotto_cat = 0;
static int          g_menu_y = 0;       /* dove comincia il menu, per il lato */

/* ! «RIAVVIA» SI RICORDA, NON SI ESEGUE SUBITO. Il perche' sta accanto a
 * ID_RIAVVIA in barra_proc: prima la scrivania se ne va per bene, poi la
 * macchina riparte. Lo esegue main(), dopo il ciclo dei messaggi. */
static int g_riavvia = 0;
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
    /* ! IL FILE SI LEGGE IN UN BOCCONE, E IL BOCCONE DEV'ESSERE PIU' GRANDE
     * DEL FILE. Erano 2048 byte, e il 22 settembre 2026 il file e' arrivato a
     * 2897 aggiungendo le righe che spiegano icone e categorie: la read si
     * fermava a meta' del commento in testa e le VOCI, che stanno in fondo,
     * non si leggevano nemmeno. Il menu si apriva senza una sola applicazione
     * e senza dire niente — il difetto piu' fastidioso di tutti, perche'
     * somiglia a «il file non c'e'».
     *
     * ! E ADESSO SI LEGGE FINCHE' C'E', poi si DICE se non ci stava. Un tetto
     * resta (non si alloca), ma un tetto superato che si annuncia e' un
     * limite; un tetto superato in silenzio e' un guasto. */
    char buf[8192];
    int n, i, r = 0, letti = 0;
    char riga[160];

    if (fd < 0) return;

    while (letti < (int)sizeof(buf) - 1) {
        int q = (int)read(fd, buf + letti, (unsigned int)((int)sizeof(buf) - 1 - letti));

        if (q <= 0) break;
        letti += q;
    }
    n = letti;

    /* Se il file continua oltre il buffer, le voci che restano fuori
     * sparirebbero senza un perche': lo si scrive sulla seriale, che qui e'
     * l'unico posto dove si possa dire qualcosa. */
    {
        char resto[8];

        if (read(fd, resto, 1) == 1)
            log_seriale("pm: applicazioni.txt e' piu' lungo di 8 KB: "
                        "le voci in fondo non le vedo");
    }

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

            /* ! IL TERZO CAMPO E' FACOLTATIVO, e le righe vecchie restano
             * righe buone: un file scritto prima che le icone esistessero si
             * legge tale e quale, e quelle voci semplicemente non ne hanno
             * una. Un formato nuovo che invalidasse il file di ieri sarebbe
             * un aggiornamento che spegne la scrivania. */
            {
                char *b2 = strchr(p, '|');
                char *ico = 0;

                if (b2) {
                    *b2 = '\0';
                    ico = b2 + 1;
                    while (*ico == ' ' || *ico == '\t') ico++;
                    taglia(ico);
                }

                taglia(riga);
                taglia(p);
                if (riga[0] == '\0' || p[0] == '\0') continue;

                memset(&g_app[g_app_n], 0, sizeof(App));

                /* ! LA CATEGORIA E' UNA BARRA NEL NOME, e non una direttiva
                 * nuova: «Grafica/Editor» vuol dire la voce «Editor» dentro
                 * «Grafica». Un file che elenca le voci in ordine descrive
                 * gia' un albero se i nomi lo dicono, e chi lo apre con un
                 * editore di testo capisce cos'e' senza leggere niente. */
                {
                    char *slash = strchr(riga, '/');

                    if (slash) {
                        *slash = '\0';
                        taglia(riga);
                        strncpy(g_app[g_app_n].categoria, riga,
                                sizeof(g_app[0].categoria) - 1);
                        memmove(riga, slash + 1, strlen(slash + 1) + 1);
                        taglia(riga);
                        if (riga[0] == '\0') continue;
                    }
                }

                strncpy(g_app[g_app_n].nome, riga, sizeof(g_app[0].nome) - 1);
                strncpy(g_app[g_app_n].percorso, p, sizeof(g_app[0].percorso) - 1);
                if (ico && ico[0])
                    strncpy(g_app[g_app_n].icona, ico, sizeof(g_app[0].icona) - 1);
                g_app_n++;
            }
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

        /* ! SI RISCRIVE TUTTO QUEL CHE SI E' LETTO, categoria e icona
         * comprese: un file riscritto da questo programma che perdesse i
         * campi che non sa mostrare sarebbe un programma che cancella il
         * lavoro di chi ha scritto il file a mano. */
        if (g_app[i].categoria[0] && g_app[i].icona[0])
            sprintf(r, "%s/%s | %s | %s\n", g_app[i].categoria, g_app[i].nome,
                    g_app[i].percorso, g_app[i].icona);
        else if (g_app[i].categoria[0])
            sprintf(r, "%s/%s | %s\n", g_app[i].categoria, g_app[i].nome,
                    g_app[i].percorso);
        else if (g_app[i].icona[0])
            sprintf(r, "%s | %s | %s\n", g_app[i].nome, g_app[i].percorso,
                    g_app[i].icona);
        else
            sprintf(r, "%s | %s\n", g_app[i].nome, g_app[i].percorso);
        ok = scrivi_tutto(fd, r);
    }

    close(fd);

    if (!ok) {
        sprintf(g_perche, "non scrivo (%d)", errno);
        unlink(tmp);
        return 0;
    }
    /* ! LO SCAMBIO E' ATOMICO, E NON SI CANCELLA PRIMA. Fino alla 0.184 il
     * rename di EX-OS non sostituiva, quindi qui bisognava togliere di mezzo
     * la destinazione — e fra il togliere e lo scambiare l'elenco NON
     * ESISTEVA. Adesso vfs_rename sostituisce tenendo il lucchetto del
     * filesystem per tutta l'operazione: quella finestra non c'e' piu', e non
     * si poteva chiuderla da qui.
     *
     * Se lo scambio fallisce, il file nuovo resta accanto col suffisso .nuo e
     * quello vecchio e' intatto: si ripara rinominandolo a mano. */
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

/* =============================================================================
 * THE SYSTEM LOG (@EXWIN-LOG, 26 September 2026)
 *
 * What the programs of the desktop print — wserver, and everything started
 * from this menu, which inherits the console of the graphics — has stayed in
 * that console's cells since 22 September instead of scrolling over the
 * windows. console_testo() gives it back (SYS_CONSOLE_TESTO), and this window
 * shows it, reading again once a second.
 *
 * ! IT IS NOT MODAL, although «modal» was in the request. On EX-OS a modal
 * window blocks every window of its PROCESS, and the taskbar belongs to this
 * process: a modal log would freeze the taskbar for as long as it is open.
 * It stays above the others instead (EX_SOPRA is not used either: a log is
 * read side by side with the program that writes it).
 *
 * ! THE TEXT IS REPLACED ONLY WHEN IT CHANGED, and the reader's place is
 * kept: at the bottom it follows the new lines, higher up it stays where it
 * was. Refilling every second regardless would throw the reader back to the
 * top while reading.
 * ============================================================================= */
#define REG_W       640
#define REG_H       360
#define REG_BUF     8192        /* 128 columns x 48 rows, with the newlines */

static ExFinestra   g_reg = 0, g_reg_area = 0;
static unsigned int g_reg_firma = 0;

static unsigned int firma(const char *p, int n)
{
    unsigned int h = 2166136261u;
    int i;

    for (i = 0; i < n; i++) h = (h ^ (unsigned char)p[i]) * 16777619u;
    return h ^ (unsigned int)n;
}

static void registro_leggi(int forza)
{
    static char  buf[REG_BUF];
    int          n = console_testo(buf, REG_BUF - 1);
    unsigned int f, vis = 0, prima, righe;
    int          in_fondo;
    char        *riga, *dopo;

    if (!g_reg_area) return;
    if (n < 0) n = sprintf(buf, "Il registro non si legge: %s\n",
                           n == -1 ? "la grafica non e' accesa"
                                   : "la scrivania e' di un altro utente");
    buf[n] = '\0';

    f = firma(buf, n);
    if (!forza && f == g_reg_firma) return;
    g_reg_firma = f;

    prima    = ex_area_vista(g_reg_area, &vis);
    righe    = ex_area_righe(g_reg_area);
    in_fondo = forza || prima + vis >= righe;

    ex_area_svuota(g_reg_area);
    for (riga = buf; *riga; riga = dopo) {
        dopo = strchr(riga, '\n');
        if (dopo) *dopo++ = '\0'; else dopo = riga + strlen(riga);
        if (!ex_area_aggiungi(g_reg_area, riga)) break;
    }

    righe = ex_area_righe(g_reg_area);
    if (in_fondo) ex_area_mostra_da(g_reg_area, righe > vis ? righe - vis : 0);
    else          ex_area_mostra_da(g_reg_area, prima);
    ex_procedura_base(g_reg, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_reg);
}

static long reg_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CHIUDI:
        ex_distruggi(g_reg);
        g_reg = g_reg_area = 0;
        return 0;
    case EXM_TEMPO:
        registro_leggi(0);
        return 0;
    case EXM_MISURA:
        ex_misura(g_reg_area, EX_X(lp) - 8, EX_Y(lp) - 8);
        break;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

static void registro_apri(void)
{
    if (g_reg) { registro_leggi(1); return; }

    g_reg = ex_crea("finestra", "Registro di sistema",
                    EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                    EX_AUTO, EX_AUTO, REG_W, REG_H, 0, 0, reg_proc);
    if (!g_reg) return;
    g_reg_area = ex_crea("areatesto", "", EX_FIGLIO, 4, 4, REG_W - 8, REG_H - 8,
                         g_reg, 0, 0);
    if (!g_reg_area) { ex_distruggi(g_reg); g_reg = 0; return; }

    ex_sveglia(g_reg, 1000);
    registro_leggi(1);
}

/* =============================================================================
 * SHUTTING DOWN AND RESTARTING WITH THE GRAPHICS ON (@GRAFICA-MODALE, 26 Sept)
 *
 * Asked: from the desktop, «Riavvia» and «Spegni» scrolled text like a
 * terminal. They did, for two reasons: «Riavvia» first switched the desktop
 * off (the server put the screen back to text mode and died) and rebooted
 * after, so the kernel's messages scrolled on the text screen; «Spegni»
 * called reboot() at once, without asking any program to close.
 *
 * Now both go the same way, and the window server is the LAST to stop:
 *   1. a window says what is happening — no close button: it is not a
 *      question;
 *   2. every other program is asked to close, as with its X button
 *      (ex_chiudi_le_altre), and the window lists who is left, from the list
 *      the taskbar already follows;
 *   3. when nobody is left: «syncing the disks», and reboot(), which syncs.
 *
 * ! A PROGRAM THAT DOES NOT CLOSE DOES NOT HOLD THE MACHINE HOSTAGE, and does
 * not lose its work in silence either: after ARR_ATTESA seconds two buttons
 * appear, «Procedi comunque» and «Annulla». A program asking «save?» gets
 * the time to be answered.
 * ============================================================================= */
#define ARR_W       440
#define ARR_H       130
#define ARR_ATTESA  20          /* seconds before offering to go on anyway */
#define ID_ARR_VAI  120
#define ID_ARR_NO   121

static ExFinestra   g_arr = 0, g_arr_testo = 0;
static int          g_arr_cosa = -1;        /* EXOS_RB_*, -1 = none in progress */
static unsigned int g_arr_da = 0;           /* uptime_ms() when it started */
static int          g_arr_pulsanti = 0;

static void arr_dico(const char *t)
{
    if (!g_arr) return;
    ex_testo_metti(g_arr_testo, t);
    ex_procedura_base(g_arr, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_arr);
}

static void arr_fine(void)
{
    if (g_arr) ex_distruggi(g_arr);
    g_arr = g_arr_testo = 0;
    g_arr_cosa = -1;
    g_arr_pulsanti = 0;
}

static void arr_esegui(void)
{
    int cosa = g_arr_cosa;

    arr_dico(cosa == EXOS_RB_RESTART ? "Sincronizzo i dischi e riavvio..."
                                     : "Sincronizzo i dischi e spengo...");
    log_seriale(cosa == EXOS_RB_RESTART ? "pm: riavvio, con la grafica accesa"
                                        : "pm: spengo, con la grafica accesa");
    reboot(cosa);

    /* ! BACK HERE ONLY IF THE KERNEL SAID NO: from ExWin, when the desktop
     * is not on a console of this machine. Said in a window: a menu that does
     * nothing looks broken. The programs are already closed — that part
     * cannot be undone, and the text says so. */
    arr_fine();
    log_seriale("pm: il kernel ha rifiutato");
    ex_dlg_avviso(cosa == EXOS_RB_RESTART ? "Riavvio" : "Spegnimento",
                  "I programmi sono stati chiusi, ma il sistema non si "
                  "riavvia ne' si spegne da qui.\n\n"
                  "Lo puo' chiedere root, oppure chi sta a una console di "
                  "questa macchina: da una sessione remota no.");
}

/* The windows of other programs still open, their titles into `nomi`. */
static int arr_restano(char *nomi, unsigned int max)
{
    ExVoceFin    v[16];
    int          n = ex_finestre_elenco(v, 16), i, quante = 0;
    unsigned int io = (unsigned int)getpid();

    if (n > 16) n = 16;
    nomi[0] = '\0';
    for (i = 0; i < n; i++) {
        if (v[i].pid == io) continue;
        if (quante++) strncat(nomi, ", ", max - strlen(nomi) - 1);
        strncat(nomi, v[i].titolo, max - strlen(nomi) - 1);
    }
    return quante;
}

static void arr_controlla(void)
{
    char         nomi[160], t[240];
    unsigned int s;

    if (g_arr_cosa < 0 || !g_arr) return;
    if (arr_restano(nomi, sizeof(nomi)) == 0) { arr_esegui(); return; }

    s = (uptime_ms() - g_arr_da) / 1000;
    snprintf(t, sizeof(t), "Aspetto che si chiudano (%u s): %s", s, nomi);
    arr_dico(t);

    if (s >= ARR_ATTESA && !g_arr_pulsanti) {
        g_arr_pulsanti = 1;
        ex_crea("pulsante", "Procedi comunque", EX_FIGLIO,
                ARR_W - 300, ARR_H - 36, 150, 26, g_arr, ID_ARR_VAI, 0);
        ex_crea("pulsante", "Annulla", EX_FIGLIO,
                ARR_W - 140, ARR_H - 36, 120, 26, g_arr, ID_ARR_NO, 0);
        ex_procedura_base(g_arr, EXM_DISEGNA, 0, 0);
        ex_aggiorna(g_arr);
    }
}

static long arr_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_TEMPO) { arr_controlla(); return 0; }
    if (msg == EXM_COMANDO && wp == ID_ARR_VAI) { arr_esegui(); return 0; }
    if (msg == EXM_COMANDO && wp == ID_ARR_NO) {
        arr_fine();
        log_seriale("pm: arresto annullato");
        return 0;
    }
    if (msg == EXM_CHIUDI) return 0;        /* no X: it is not a question */
    return ex_procedura_base(f, msg, wp, lp);
}

static void arresta(int cosa)
{
    const char *titolo = cosa == EXOS_RB_RESTART ? "Riavvio" : "Spegnimento";

    if (g_arr_cosa >= 0) return;            /* one at a time */

    /* ! AT THE BOTTOM, ABOVE THE TASKBAR, NOT IN THE MIDDLE: the middle is
     * where the programs ask «save the changes?» — and that is exactly when
     * this window has something to say. Centred, it sat under the editor's
     * question and could not be seen (tried in QEMU, 26 September 2026). */
    g_arr = ex_crea("finestra", titolo, EX_TITOLO | EX_BORDO | EX_SOPRA,
                    ((int)g_sw - ARR_W) / 2, (int)g_sh - BARRA_H - ARR_H - 30,
                    ARR_W, ARR_H, 0, 0, arr_proc);
    if (!g_arr) {
        /* No window: do it the old way rather than not at all. */
        reboot(cosa);
        return;
    }
    g_arr_testo = ex_crea("etichetta", "", EX_FIGLIO, 16, 20, ARR_W - 32, 48,
                          g_arr, 0, 0);
    g_arr_cosa = cosa;
    g_arr_da   = uptime_ms();
    g_arr_pulsanti = 0;

    arr_dico("Chiedo ai programmi di chiudersi...");
    log_seriale(cosa == EXOS_RB_RESTART ? "pm: riavvio chiesto dal menu"
                                        : "pm: spegnimento chiesto dal menu");
    ex_chiudi_le_altre();
    ex_sveglia(g_arr, 1000);
}

static void menu_chiudi(void)
{
    /* ! IL SOTTOMENU SE NE VA COL PADRE. E' una finestra a se', quindi
     * chiudere il menu senza chiudere lui lascerebbe sullo schermo un elenco
     * che non appartiene piu' a niente - e che a premerlo avvierebbe ancora i
     * suoi programmi. */
    if (g_sotto) { ex_distruggi(g_sotto); g_sotto = 0; }
    if (g_menu)  { ex_distruggi(g_menu);  g_menu = 0; }
}

/* =============================================================================
 * LE CATEGORIE, E IL SOTTOMENU CHE SI APRE DI LATO
 *
 * ! LE CATEGORIE NON SONO UN ELENCO A PARTE: si ricavano dalle voci, nell'ordine
 * in cui compaiono nel file. Tenerne una seconda lista vorrebbe dire una lista
 * da tenere d'accordo con la prima, e una categoria rimasta vuota perche'
 * qualcuno ha tolto l'ultima applicazione che c'era dentro.
 * ========================================================================== */
static char         g_cat[APP_MAX][32];
static unsigned int g_cat_n;

static void categorie_raccogli(void)
{
    unsigned int i, k;

    g_cat_n = 0;
    for (i = 0; i < g_app_n; i++) {
        if (!g_app[i].categoria[0]) continue;

        for (k = 0; k < g_cat_n; k++)
            if (strcmp(g_cat[k], g_app[i].categoria) == 0) break;

        if (k == g_cat_n && g_cat_n < APP_MAX) {
            strncpy(g_cat[g_cat_n], g_app[i].categoria, sizeof(g_cat[0]) - 1);
            g_cat[g_cat_n][sizeof(g_cat[0]) - 1] = '\0';
            g_cat_n++;
        }
    }
}

/* L'icona di una voce, aperta la prima volta che serve.
 *
 * ! SI APRE UNA VOLTA SOLA ANCHE SE NON C'E'. Un percorso sbagliato rende 0, e
 * senza questa memoria il menu riproverebbe ad aprire quel file a ogni
 * disegno: una lettura fallita per voce, ogni volta. */
static ExIcona icona_di(App *a)
{
    if (a->ic == 0 && a->icona[0]) {
        a->ic = ex_icona_apri(a->icona);
        if (a->ic == 0) a->icona[0] = '\0';   /* non ci si riprova */
    }
    return a->ic;
}

/* L'icona che rappresenta una categoria: quella della sua prima voce.
 *
 * ! UNA CATEGORIA NON HA UN FILE SUO, e darle una riga nel formato vorrebbe
 * dire un secondo tipo di riga da spiegare a chi scrive il file a mano. La
 * prima voce che ci sta dentro e' una scelta che si capisce da sola. */
static ExIcona icona_categoria(unsigned int c)
{
    unsigned int i;

    for (i = 0; i < g_app_n; i++)
        if (strcmp(g_app[i].categoria, g_cat[c]) == 0)
            return icona_di(&g_app[i]);
    return 0;
}

static void sotto_chiudi(void)
{
    if (g_sotto) { ex_distruggi(g_sotto); g_sotto = 0; }
}

static long menu_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp);

/* Apre l'elenco di una categoria ACCANTO alla sua voce.
 *
 * ! SI APRE AL CLIC E NON AL PASSAGGIO DEL PUNTATORE, e non e' una scelta di
 * gusto: il server manda il bottone giu' e il bottone su, non il movimento
 * (WIN_EV_MOUSE_MOSSO e' nel protocollo e non lo manda ancora nessuno). Un
 * sottomenu che si aprisse al passaggio, qui, non si aprirebbe mai.
 *
 * ! E STA ACCANTO, NON SOPRA: il menu principale resta visibile, cosi' si vede
 * DA DOVE si e' scesi. E' la stessa forma dei menu a cascata di ogni scrivania
 * dal 1990 in poi. */
static void sotto_apri(unsigned int c, int y_voce)
{
    unsigned int i, n = 0;
    int h, y;

    sotto_chiudi();
    if (c >= g_cat_n) return;

    for (i = 0; i < g_app_n; i++)
        if (strcmp(g_app[i].categoria, g_cat[c]) == 0) n++;
    if (n == 0) return;

    h = (int)n * VOCE_H + 8;

    /* ! SI ALZA SE NON CI STA, invece di finire sotto il bordo dello schermo.
     * Una categoria in fondo al menu con dentro otto voci uscirebbe dallo
     * schermo, e le ultime non si potrebbero premere. */
    y = y_voce;
    if (y + h > (int)g_sh - BARRA_H) y = (int)g_sh - BARRA_H - h - 2;
    if (y < 0) y = 0;

    g_sotto = ex_crea("finestra", "", EX_BORDO | EX_SOPRA,
                      4 + MENU_W + 2, y, MENU_W, h, 0, 0, menu_proc);
    if (!g_sotto) return;

    g_sotto_cat = c;

    n = 0;
    for (i = 0; i < g_app_n; i++) {
        ExFinestra b;

        if (strcmp(g_app[i].categoria, g_cat[c]) != 0) continue;

        b = ex_crea("pulsante", g_app[i].nome, EX_FIGLIO,
                    4, 4 + (int)n * VOCE_H, MENU_W - 8, VOCE_H - 2,
                    g_sotto, ID_VOCE + i, 0);
        ex_icona_metti(b, icona_di(&g_app[i]), ICONA_LATO);
        n++;
    }

    ex_procedura_base(g_sotto, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_sotto);
}

static void menu_apri(void)
{
    unsigned int i, riga = 0;
    int h;

    if (g_menu) { menu_chiudi(); return; }   /* premuto due volte: si chiude */

    categorie_raccogli();

    /* ! «ESCI» E «SPEGNI» CI SONO ANCHE SENZA APPLICAZIONI. Un menu che si
     * rifiutasse di aprirsi perche' l'elenco e' vuoto lascerebbe senza modo
     * di uscire dalla grafica — e uscire e' proprio cio' che si vuole fare
     * quando non c'e' niente da avviare. */
    /* Quattro voci fisse adesso — «Applicazioni...», «Informazioni su»,
     * «Esci», «Spegni». */
    /* Sette voci sotto la riga: Applicazioni, Informazioni, Impostazioni,
     * Registro, Esci, Riavvia, Spegni. Il numero sta qui e non sparso nelle
     * posizioni. */
    /* Quante righe in cima: una per categoria, piu' le voci che non ne hanno
     * nessuna. Le altre stanno nei sottomenu e non occupano posto qui. */
    {
        unsigned int fuori = 0;

        for (i = 0; i < g_app_n; i++)
            if (!g_app[i].categoria[0]) fuori++;

        h = (int)((g_cat_n + fuori) * VOCE_H) + 8 + 7 * VOCE_H + 6;
    }

    g_menu_y = (int)g_sh - BARRA_H - h - 2;
    g_menu = ex_crea("finestra", "", EX_BORDO | EX_SOPRA,
                     4, g_menu_y, MENU_W, h, 0, 0, menu_proc);
    if (!g_menu) return;

    /* ! PRIMA LE CATEGORIE, POI LE VOCI SCIOLTE. Le prime aprono un elenco, le
     * seconde avviano un programma: mescolarle vorrebbe dire due gesti diversi
     * a righe alterne. La freccia in fondo al nome dice quali sono quali. */
    for (i = 0; i < g_cat_n; i++) {
        char        t[40];
        ExFinestra  b;

        sprintf(t, "%s...", g_cat[i]);
        b = ex_crea("pulsante", t, EX_FIGLIO,
                    4, 4 + (int)riga * VOCE_H, MENU_W - 8, VOCE_H - 2,
                    g_menu, ID_CAT + i, 0);
        ex_icona_metti(b, icona_categoria(i), ICONA_LATO);
        riga++;
    }

    for (i = 0; i < g_app_n; i++) {
        ExFinestra b;

        if (g_app[i].categoria[0]) continue;    /* sta in un sottomenu */

        b = ex_crea("pulsante", g_app[i].nome, EX_FIGLIO,
                    4, 4 + (int)riga * VOCE_H, MENU_W - 8, VOCE_H - 2,
                    g_menu, ID_VOCE + i, 0);
        ex_icona_metti(b, icona_di(&g_app[i]), ICONA_LATO);
        riga++;
    }

    /* Una riga a separare le applicazioni da cio' che spegne le cose: sono
     * due categorie diverse, e un clic sbagliato costa molto di piu' da una
     * parte che dall'altra. */
    ex_crea("separatore", "", EX_FIGLIO,
            6, 6 + (int)riga * VOCE_H, MENU_W - 12, 2, g_menu, 0, 0);

    /* ! «APPLICAZIONI...» STA SOTTO LA RIGA, con «Esci» e «Spegni», e non fra
     * le applicazioni: non e' un programma da avviare, e' una cosa che la
     * scrivania sa fare. Metterla in mezzo alle voci vorrebbe anche dire che
     * si sposta ogni volta che se ne aggiunge una. */
    ex_crea("pulsante", "Applicazioni...", EX_FIGLIO,
            4, 10 + (int)riga * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_GESTISCI, 0);
    /* ! «INFORMAZIONI SU» STA CON LE COSE CHE SA FARE LA SCRIVANIA, sotto la
     * riga, e non fra le applicazioni: la scrivania non ha una barra dei menu
     * dove metterla — la sua barra e' quella delle finestre aperte — e questo
     * e' l'unico menu che ha. */
    ex_crea("pulsante", "Informazioni su", EX_FIGLIO,
            4, 10 + (int)(riga + 1) * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_INFO, 0);
    ex_crea("pulsante", "Impostazioni...", EX_FIGLIO,
            4, 10 + (int)(riga + 2) * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_IMPOST, 0);
    /* The desktop's log (@EXWIN-LOG): with the things the desktop knows how
     * to do, before the ones that stop it. */
    ex_crea("pulsante", "Registro di sistema", EX_FIGLIO,
            4, 10 + (int)(riga + 3) * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_REGISTRO, 0);
    ex_crea("pulsante", "Esci", EX_FIGLIO,
            4, 10 + (int)(riga + 4) * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_ESCI, 0);
    /* ! «RIAVVIA» STA FRA «ESCI» E «SPEGNI», ed e' il posto giusto per due
     * ragioni. La prima e' l'ordine di gravita': si esce dalla scrivania, si
     * riavvia la macchina, si spegne la macchina — ogni voce costa piu' della
     * precedente, e chi sbaglia mira di una riga sbaglia di poco.
     *
     * La seconda e' che oggi «Esci» e «Riavvia» sono quasi la stessa cosa, e
     * si e' visto usandolo: uscendo dalla scrivania la scheda torna in modo
     * testo e `exwin` rifiuta di ripartire — la modalita' grafica la imposta
     * Stage 2 col BIOS, e da qui quella porta e' chiusa (vedi DIREZIONE.md).
     * Per rivedere la scrivania bisogna riavviare, e finche' e' cosi' quel
     * comando deve stare nel menu invece che in una console di testo che chi
     * e' nella grafica non sta guardando. */
    ex_crea("pulsante", "Riavvia", EX_FIGLIO,
            4, 10 + (int)(riga + 5) * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_RIAVVIA, 0);
    ex_crea("pulsante", "Spegni", EX_FIGLIO,
            4, 10 + (int)(riga + 6) * VOCE_H, MENU_W - 8, VOCE_H - 2,
            g_menu, ID_SPEGNI, 0);

    ex_procedura_base(g_menu, EXM_DISEGNA, 0, 0);
}

/* =============================================================================
 * LE IMPOSTAZIONI — per ora una sola: la risoluzione
 *
 * ! IL MECCANISMO C'ERA GIA' TUTTO, e questa finestra non lo rifa': lo chiama.
 * `/dev/svga.drv <modo>` scrive due cose che devono restare d'accordo — la
 * voce `svga` in kernel.cfg, che e' la configurazione, e un byte dentro Stage
 * 2 marcato dalla firma 'SVGAMODE', che e' il recapito per chi si avvia prima
 * che esista un filesystem. Rifarlo qui vorrebbe dire una seconda verita'
 * accanto a quella vera.
 *
 * ! E IL NUOVO MODO ARRIVA AL RIAVVIO, non adesso: la modalita' video la
 * imposta Stage 2 con il BIOS, in modo reale, e quando la scrivania gira
 * quella porta e' chiusa da un pezzo. Dirlo e' meta' del lavoro — una finestra
 * che sembra non aver fatto niente e' peggio di una che non c'e'.
 * ========================================================================== */
static ExFinestra g_impost = 0;

static const char *const MODI[4] = { "testo", "640x480", "800x600", "1024x768" };

static long impost_proc(ExFinestra f, unsigned int msg,
                        unsigned int wp, long lp)
{
    if (msg == EXM_CHIUDI) { ex_distruggi(f); g_impost = 0; return 0; }

    if (msg == EXM_COMANDO && wp >= ID_RIS && wp < ID_RIS + 4) {
        const char *modo = MODI[wp - ID_RIS];
        char       *av[3];
        char        m[256];
        int         pid, st = 0;

        av[0] = "/dev/svga.drv";
        av[1] = (char *)modo;
        av[2] = 0;

        pid = spawn_ex(av[0], av, environ, 0, 0);
        if (pid < 0) {
            sprintf(m, "Non riesco a lanciare %s.", av[0]);
            ex_dlg_avviso("Risoluzione", m);
            return 0;
        }
        waitpid(pid, &st, 0);

        /* ! SI GUARDA COM'E' ANDATA. Una finestra che dice «fatto» comunque
         * manderebbe l'utente a riavviare per niente.
         *
         * ! E NON SI INDOVINA IL PERCHE'. Il motivo piu' comune non e' la
         * scheda: e' che il sistema gira da CD, e li' LOADER.BIN non si puo'
         * riscrivere — `svga` dice «filesystem in sola lettura». Scrivere
         * «la scheda non lo offre» manderebbe a cercare un difetto dove non
         * c'e'. Il motivo esatto lo stampa svga sulla console di testo; qui
         * si dicono i due casi veri e si rimanda li'. */
        if (st != 0)
            sprintf(m, "%s non e' stato impostato.\n\n"
                       "Da CD non si puo': il file di avvio e' in sola "
                       "lettura. Su un sistema installato, la scheda "
                       "potrebbe non offrire quel modo.\n\n"
                       "Il motivo esatto e' sulla console di testo.", modo);
        else
            sprintf(m, "Risoluzione impostata su %s.\n\n"
                       "Arriva al PROSSIMO RIAVVIO: il modo video lo sceglie "
                       "l'avvio, prima che esista la scrivania.", modo);
        ex_dlg_avviso("Risoluzione", m);
        return 0;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

static void impostazioni_apri(void)
{
    unsigned int sw = 0, sh = 0;
    char         t[96];
    int          i;

    /* Gia' aperta: si rimette davanti facendola rivedere. Non c'e' un
     * «portami in primo piano» nel toolkit, e ex_mostra basta: il server mette
     * davanti cio' che torna visibile. */
    if (g_impost) { ex_mostra(g_impost, 1); return; }

    g_impost = ex_crea("finestra", "Impostazioni",
                       EX_TITOLO | EX_BORDO | EX_CHIUDI,
                       EX_AUTO, EX_AUTO, 300, 200, 0, 0, impost_proc);
    if (!g_impost) return;

    ex_schermo(&sw, &sh);
    sprintf(t, "Risoluzione: adesso %ux%u", sw, sh);
    ex_crea("etichetta", t, EX_FIGLIO, 12, 10, 276, 18, g_impost, 0, 0);

    ex_crea("etichetta", "Si applica al prossimo riavvio.", EX_FIGLIO,
            12, 30, 276, 18, g_impost, 0, 0);

    for (i = 0; i < 4; i++)
        ex_crea("pulsante", MODI[i], EX_FIGLIO,
                12 + (i % 2) * 140, 58 + (i / 2) * 34, 132, 28,
                g_impost, (unsigned int)(ID_RIS + i), 0);

    ex_crea("etichetta", "\"testo\" spegne la grafica all'avvio.", EX_FIGLIO,
            12, 132, 276, 18, g_impost, 0, 0);

    ex_procedura_base(g_impost, EXM_DISEGNA, 0, 0);
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
    /* ! L'AMBIENTE SI PASSA ANCHE QUI, ed e' l'ultimo anello: `envp` nullo
     * vuol dire ambiente VUOTO. Un'applicazione avviata dal menu deve sapere
     * dov'e' la casa di chi l'ha premuta, o si tiene i suoi file dove capita. */
    if (spawn_ex(argv[0], argv, environ, 0, 0) < 0) {
        char m[160];
        sprintf(m, "pm: non riesco ad avviare %s", argv[0]);
        log_seriale(m);
    }
}

static long menu_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO) {
        /* ! UNA CATEGORIA NON CHIUDE IL MENU, LO ALLARGA, ed e' l'unica voce
         * che si comporta cosi'. Tutte le altre fanno qualcosa e se ne vanno;
         * questa apre l'elenco accanto, e il menu deve restare per far vedere
         * da dove si e' scesi. Per questo il controllo sta PRIMA della
         * chiusura, invece che fra le altre voci. */
        if (wp >= ID_CAT && wp < ID_CAT + APP_MAX) {
            unsigned int c = wp - ID_CAT;

            /* Premuta due volte: si richiude, come il pulsante «Avvio». */
            if (g_sotto && g_sotto_cat == c) { sotto_chiudi(); return 0; }

            /* La voce `c` sta alla riga `c` del menu (le categorie sono le
             * prime): il sottomenu si apre alla sua altezza. */
            sotto_apri(c, g_menu_y + 4 + (int)c * VOCE_H);
            return 0;
        }

        menu_chiudi();

        /* =================================================================
         * ! «ESCI» SPEGNE LA SCRIVANIA INTERA, dal 25 agosto 2026.
         *
         * Qui c'era scritto il contrario, e la ragione non era sciocca: il
         * server e' di chi lo ha avviato e potrebbe avere altre finestre
         * aperte, quindi chiuderlo da qui sembrava portarsi via il lavoro di
         * qualcun altro. In pratica lasciava una macchina con la grafica
         * accesa e NESSUNO dentro: per spegnerla davvero bisognava sapere
         * cosa uccidere, e chi non lo sapeva restava con uno schermo che non
         * rispondeva piu' a niente.
         *
         * ! ALLE APPLICAZIONI SI CHIEDE, NON LE SI UCCIDE: il server manda a
         * ognuna la stessa chiusura della crocetta e aspetta. Chi ha da
         * salvare fa in tempo; il server rimette il modo testo e muore.
         *
         * ! E NON SI CHIAMA ex_esci() DOPO. Il server sta per mandare a QUESTA
         * finestra la sua chiusura, come a tutte le altre: uscire subito
         * vorrebbe dire sparire prima di aver ricevuto il messaggio che si e'
         * appena chiesto di ricevere.
         * ================================================================= */
        if (wp == ID_ESCI) {
            log_seriale("pm: spegnimento della scrivania chiesto dal menu");
            ex_spegni_scrivania();
            return 0;
        }

        /* =================================================================
         * ! RIAVVIA E' «ESCI» PIU' UNA COSA, E L'ORDINE E' TUTTO.
         *
         * Non si chiama reboot() qui. Qui si chiede alla scrivania di
         * spegnersi — esattamente come «Esci» — e ci si SEGNA che dopo si
         * riavvia: il riavvio vero lo fa main(), quando il ciclo dei messaggi
         * e' finito.
         *
         * Il perche' e' che ex_spegni_scrivania() e' un messaggio, non una
         * chiamata: manda WIN_MSG_SPEGNI e torna subito. E' il SERVER a fare
         * il lavoro — chiede a ogni applicazione la stessa chiusura della
         * crocetta, le aspetta, rimette il modo testo e muore. Riavviando su
         * questa riga si porterebbe via un editor con del testo non salvato
         * mentre gli si sta ancora chiedendo se ha finito, e il riavvio dal
         * menu lo si chiede proprio quando si sta ancora lavorando.
         *
         * ! E SI RIAVVIA CON LO SCHERMO GIA' RIMESSO A TESTO, che e' l'altra
         * meta' del regalo: cosi' se il riavvio venisse rifiutato — capita a
         * chi non e' root da una sessione remota — si resta davanti a una
         * console leggibile invece che a uno schermo grafico senza piu'
         * nessuno dentro.
         * ================================================================= */
        /* ! SINCE 26 SEPTEMBER 2026 THE GRAPHICS STAYS ON UNTIL THE END: see
         * arresta(). What is written above about «Esci» first and reboot
         * after was the reason the reboot scrolled text (@GRAFICA-MODALE). */
        if (wp == ID_RIAVVIA) { arresta(EXOS_RB_RESTART); return 0; }

        /* ! SPEGNERE SINCRONIZZA I DISCHI, e lo fa il kernel: qui si chiede e
         * basta. Se rende, vuol dire che ha rifiutato — e allora si dice,
         * invece di lasciare una scrivania che sembra aver ignorato il
         * comando. */
        if (wp == ID_SPEGNI) { arresta(EXOS_RB_POWEROFF); return 0; }

        if (wp == ID_GESTISCI) { gest_apri(); return 0; }
        if (wp == ID_REGISTRO) { registro_apri(); return 0; }

        if (wp == ID_IMPOST) {
            impostazioni_apri();
            return 0;
        }

        if (wp == ID_INFO) {
            char t[512];

            exinfo_testo(t, sizeof(t), "Scrivania", VERSIONE_APP,
                         "La scrivania di EX-OS: la barra delle finestre, il "
                         "menu di avvio e l'elenco delle applicazioni.  Le "
                         "applicazioni sono processi a se': se una muore, la "
                         "scrivania resta.");
            ex_dlg_avviso("Informazioni su", t);
            return 0;
        }

        /* ! QUI SOTTO CI ARRIVA SOLO CIO' CHE NON E' UNA VOCE FISSA, e il
         * controllo serve: `avvia` prende `wp - ID_VOCE` senza segno, quindi
         * un id piu' piccolo di ID_VOCE diventerebbe un numero enorme. Il
         * confronto lo ferma prima. */
        if (wp >= ID_VOCE) avvia(wp - ID_VOCE);
        return 0;
    }
    return ex_procedura_base(f, msg, wp, lp);
}

/* =============================================================================
 * THE OPEN PROGRAMS ON THE TASKBAR (@FIN-ICONA, 26 September 2026)
 *
 * One entry per open window, minimized or not: the taskbar shows what is
 * running and switches between programs. A click on an entry restores it if
 * minimized and brings it to the front.
 *
 * ! THE LIST IS THE SERVER'S, and arrives by itself as EXM_FINESTRE every
 * time it changes (ex_finestre_segui). Nothing here is remembered between
 * two lists except the icons, so a program that dies while minimized
 * cannot leave an entry behind.
 *
 * ! THE ICON COMES FROM THE PROGRAM'S NAME: the pid in the list gives the
 * process name (procinfo), and the name finds the line of
 * applicazioni.txt whose path ends with it. A program that is not in the
 * menu shows its title without an icon — the second of the two roads in
 * @FIN-ICONA: it costs no new message, and is wrong only for programs the
 * menu does not know.
 *
 * ! WHEN THEY DO NOT FIT, ENTRIES SHRINK TO THE ICON ALONE, as in Windows 95:
 * that is where the icon is worth more than the title. Past that, the last
 * ones are not shown (sixteen windows at 640 pixels).
 * ============================================================================= */
#define VB_X0        76         /* after «Avvio» */
#define VB_OROLOGIO 178         /* the clock, a process of its own, is here */
#define VB_MAX_W    160
#define VB_MIN_W     28
#define VB_ICONA     16

typedef struct {
    ExVoceFin v;
    ExIcona   ic;
    int       x, w;
} VoceBarra;

static VoceBarra g_vb[16];
static int       g_vb_n = 0;

/* The last path component, without the directories. */
static const char *base_di(const char *p)
{
    const char *b = p;

    for (; *p; p++) if (*p == '/') b = p + 1;
    return b;
}

static ExIcona icona_del_pid(unsigned int pid)
{
    ProcInfo     pi[PROCINFO_MAX_BATCH];
    unsigned int start = 0, i;
    int          n;
    const char  *nome = 0;

    while (!nome && (n = procinfo(pi, PROCINFO_MAX_BATCH, start)) > 0) {
        for (i = 0; i < (unsigned int)n; i++)
            if (pi[i].pid == pid) { nome = base_di(pi[i].name); break; }
        start += (unsigned int)n;
        if (nome) {
            /* pi is overwritten by the next call: keep the name. */
            static char tieni[PROCINFO_NAME_MAX];
            strncpy(tieni, nome, sizeof(tieni) - 1);
            tieni[sizeof(tieni) - 1] = '\0';
            nome = tieni;
        }
    }
    if (!nome || !nome[0]) return 0;

    for (i = 0; i < g_app_n; i++)
        if (strcmp(base_di(g_app[i].percorso), nome) == 0) return icona_di(&g_app[i]);
    return 0;
}

static void vb_disponi(void)
{
    int x1 = (int)g_sw - VB_OROLOGIO, spazio = x1 - VB_X0, w, i, quanti;

    if (g_vb_n == 0 || spazio <= 0) return;
    w = spazio / g_vb_n;
    if (w > VB_MAX_W) w = VB_MAX_W;
    if (w < VB_MIN_W) w = VB_MIN_W;
    quanti = spazio / w;

    for (i = 0; i < g_vb_n; i++) {
        g_vb[i].x = VB_X0 + i * w;
        g_vb[i].w = (i < quanti) ? w - 2 : 0;      /* 0 = not shown */
    }
}

static void vb_leggi(void)
{
    ExVoceFin v[16];
    int       n = ex_finestre_elenco(v, 16), i;

    if (n > 16) n = 16;
    for (i = 0; i < n; i++) {
        g_vb[i].v  = v[i];
        g_vb[i].ic = icona_del_pid(v[i].pid);
    }
    g_vb_n = n;
    vb_disponi();
}

static void vb_disegna(ExFinestra f)
{
    int x1 = (int)g_sw - VB_OROLOGIO, i;
    int y = 2, h = BARRA_H - 4;

    ex_riempi(f, VB_X0, 0, x1 - VB_X0, BARRA_H, EX_GRIGIO);

    for (i = 0; i < g_vb_n; i++) {
        VoceBarra *b = &g_vb[i];
        int        tx = b->x + 4, spazio, n;
        char       t[48];
        unsigned int ridotta = b->v.stato & EX_VF_RIDOTTA;

        if (b->w <= 0) continue;

        /* ! THE ONE WITH THE FOCUS IS PRESSED IN, the others stick out: the
         * rule of the toolkit — what sticks out can be pressed. */
        ex_riempi(f, b->x, y, b->w, h, EX_GRIGIO);
        if ((b->v.stato & EX_VF_FUOCO) && !ridotta) ex_incavo(f, b->x, y, b->w, h);
        else                                       ex_rilievo(f, b->x, y, b->w, h);

        if (b->ic) {
            ex_icona_disegna(f, b->ic, tx, y + (h - VB_ICONA) / 2, VB_ICONA, EX_GRIGIO);
            tx += VB_ICONA + 4;
        }

        /* The title, cut to what fits: the system font is 8 pixels wide. */
        spazio = b->x + b->w - 4 - tx;
        n = spazio / 8;
        if (n <= 0) continue;
        if (n > (int)sizeof(t) - 1) n = (int)sizeof(t) - 1;
        strncpy(t, b->v.titolo, (size_t)n);
        t[n] = '\0';
        /* A minimized one is written in grey: it is there, but not on screen. */
        ex_scrivi(f, tx, y + (h - 16) / 2, t, ridotta ? 0x00606060 : EX_NERO);
    }
}

static int vb_sotto(int x, int y)
{
    int i;

    if (y < 2 || y >= BARRA_H - 2) return -1;
    for (i = 0; i < g_vb_n; i++)
        if (g_vb[i].w > 0 && x >= g_vb[i].x && x < g_vb[i].x + g_vb[i].w) return i;
    return -1;
}

/* -----------------------------------------------------------------------------
 * La barra
 * --------------------------------------------------------------------------- */
static long barra_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO && wp == ID_AVVIO) { menu_apri(); return 0; }

    /* Ctrl+Alt+Canc (@TASTI-SISTEMA): the three roads, and «Annulla» last —
     * Enter pressed without reading must do nothing. A second Ctrl+Alt+Canc
     * within five seconds restarts from the window server, even while this
     * question is open. */
    if (msg == EXM_SISTEMA) {
        /* ! SHORT CAPTIONS: four buttons share the dialog's 420 pixels, and
         * «Esci dalla sessione» pushed «Annulla» out of it. The words go in
         * the question instead. */
        static const char *const voci[4] = {
            "Riavvia", "Esci", "Spegni", "Annulla"
        };
        int r = ex_dlg_scegli("Ctrl+Alt+Canc",
                              "Riavviare il sistema, uscire dalla sessione "
                              "grafica o spegnere? Ctrl+Alt+Canc di nuovo "
                              "entro 5 secondi riavvia subito.", voci, 4);

        if (r == 0) arresta(EXOS_RB_RESTART);
        else if (r == 1) { log_seriale("pm: uscita dalla sessione (Ctrl+Alt+Canc)");
                           ex_spegni_scrivania(); }
        else if (r == 2) arresta(EXOS_RB_POWEROFF);
        return 0;
    }

    if (msg == EXM_FINESTRE) {
        vb_leggi();
        vb_disegna(f);
        ex_aggiorna(f);
        arr_controlla();            /* a shutdown in progress watches it too */
        return 0;
    }

    if (msg == EXM_MOUSE_GIU) {
        int i = vb_sotto(EX_X(lp), EX_Y(lp));

        if (i >= 0) { ex_finestra_attiva(g_vb[i].v.id); return 0; }
    }

    if (msg == EXM_DISEGNA) {
        long r = ex_procedura_base(f, msg, wp, lp);

        vb_disegna(f);
        ex_aggiorna(f);
        return r;
    }
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

    /* From now on the server tells the taskbar which windows are open. */
    ex_finestre_segui(g_barra);
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
            if (spawn_ex(av[0], av, environ, 0, 0) >= 0) partito = 1;
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

        if (spawn_ex(av[0], av, environ, 0, 0) < 0) {
            char msg[160];

            sprintf(msg, "pm: avvio automatico fallito: %s", perc);
            log_seriale(msg);
            printf("pm: avvio automatico fallito: %s\n", perc);
        } else {
            printf("pm: avvio automatico: %s\n", perc);
        }
    }

    while (ex_prendi_msg(&m)) ex_smista(&m);

    /* =========================================================================
     * ! QUI LA NOSTRA FINESTRA E' CHIUSA, MA IL SERVER STA ANCORA LAVORANDO.
     *
     * Il ciclo dei messaggi finisce appena arriva a NOI la chiusura, e il
     * server la manda a tutte le finestre insieme: quando usciamo di li' le
     * altre applicazioni possono avere ancora qualche decimo di secondo per
     * salvare, e la scheda video e' ancora in grafica. Riavviare adesso
     * sarebbe riavviare in mezzo al lavoro che si e' appena chiesto di fare.
     *
     * ! COSA SI ASPETTA E' UN FATTO, NON UN TEMPO. Il kernel sa chi tiene la
     * console della grafica, e il server la lascia — console_grafica(2) — solo
     * dopo aver rimesso il modo testo. La domanda «c'e' ancora una grafica?»
     * ha quindi una risposta esatta, e si aspetta quella. E' la stessa attesa
     * che fa `exwin --attendi`, per la stessa ragione.
     *
     * ! CON UN TETTO, PERO'. Un server bloccato non deve poter tenere in
     * ostaggio un riavvio gia' chiesto: dopo il tempo si va avanti lo stesso.
     * ======================================================================= */
    if (g_riavvia) {
        int giri;

        for (giri = 0; giri < 100 && console_grafica(0) >= 0; giri++)
            usleep(50000);

        /* ! E ADESSO LO SCHERMO E' GIA' TESTO, che e' l'altra meta' del
         * regalo: se il riavvio venisse rifiutato — capita a chi non e' root
         * da una sessione remota — si resta davanti a una console leggibile
         * invece che a uno schermo grafico senza piu' nessuno dentro.
         *
         * ! SINCRONIZZARE I DISCHI LO FA IL KERNEL: qui si chiede e basta. Se
         * reboot() rende, ha rifiutato. */
        log_seriale("pm: la scrivania e' uscita, riavvio la macchina");
        reboot(EXOS_RB_RESTART);
        log_seriale("pm: il kernel ha rifiutato il riavvio");
        printf("pm: il sistema non si riavvia da qui.\n");
        printf("    Lo puo' chiedere root, oppure chi sta a una console di\n");
        printf("    questa macchina: da una sessione remota no.\n");
        return 1;
    }
    return 0;
}
