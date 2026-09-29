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
#define VERSIONE_APP "0.011"
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
#define ID_SFONDO_SCEGLI 110
#define ID_SFONDO_NIENTE 111
#define ID_SFONDO_MODO   112     /* ..115: angolo, centro, allarga, ripeti */

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
 *
 * ! SINCE 27 SEPTEMBER IT IS THE RING, not the screen: console_registro()
 * gives the last 32 KB written, each line with its time and pid, so what
 * scrolled off is still here. On an older kernel (no syscall 215) the window
 * falls back to the screen, console_testo(), as before.
 * ============================================================================= */
#define REG_W       640
#define REG_H       360
#define REG_BUF     32769       /* the kernel's ring (32 KB), and the '\0' */

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
    static int   anello = 1;
    int          n = anello ? console_registro(buf, REG_BUF - 1) : -2;
    unsigned int f, vis = 0, prima, righe;
    int          in_fondo;
    char        *riga, *dopo;

    if (!g_reg_area) return;
    if (n < -1) {                 /* not «no graphics»: no ring, or no right */
        int t = console_testo(buf, REG_BUF - 1);
        if (t >= 0) { anello = 0; n = t; }
    }
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
static ExFinestra g_impost_sfondo = 0;  /* the label with the current image */

/* Defined with the desktop, further down (@PM-SFONDO). */
static void sfondo_scegli(void);
static void sfondo_niente(void);
static void sfondo_etichetta(void);
static void sfondo_modo(int modo);

/* How the image sits on the desktop: the word in pm.cfg, and the button. The
 * order is the toolkit's, EX_IMM_ANGOLO..EX_IMM_RIPETI. */
static const char *const DISPOSIZIONI[4]   = { "angolo", "centro", "allarga", "ripeti" };
static const char *const DISPOSIZIONI_T[4] = { "Angolo", "Centro", "Allarga", "Ripeti" };
static int g_sfondo_modo = EX_IMM_ANGOLO;

static const char *const MODI[4] = { "testo", "640x480", "800x600", "1024x768" };

static long impost_proc(ExFinestra f, unsigned int msg,
                        unsigned int wp, long lp)
{
    if (msg == EXM_CHIUDI) { ex_distruggi(f); g_impost = g_impost_sfondo = 0; return 0; }

    if (msg == EXM_COMANDO && wp == ID_SFONDO_SCEGLI) { sfondo_scegli(); return 0; }
    if (msg == EXM_COMANDO && wp == ID_SFONDO_NIENTE) { sfondo_niente(); return 0; }
    if (msg == EXM_COMANDO && wp >= ID_SFONDO_MODO && wp < ID_SFONDO_MODO + 4) {
        sfondo_modo((int)(wp - ID_SFONDO_MODO));
        return 0;
    }

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
                       EX_AUTO, EX_AUTO, 300, 316, 0, 0, impost_proc);
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

    /* The desktop image (@PM-SFONDO): applied at once, kept for next time. */
    ex_crea("separatore", "", EX_FIGLIO, 12, 160, 276, 2, g_impost, 0, 0);
    g_impost_sfondo = ex_crea("etichetta", "", EX_FIGLIO, 12, 172, 276, 18,
                              g_impost, 0, 0);
    sfondo_etichetta();
    ex_crea("pulsante", "Scegli...", EX_FIGLIO, 12, 200, 132, 28,
            g_impost, ID_SFONDO_SCEGLI, 0);
    ex_crea("pulsante", "Nessuno", EX_FIGLIO, 152, 200, 132, 28,
            g_impost, ID_SFONDO_NIENTE, 0);
    for (i = 0; i < 4; i++)
        ex_crea("pulsante", DISPOSIZIONI_T[i], EX_FIGLIO, 12 + i * 70, 236, 64, 28,
                g_impost, (unsigned int)(ID_SFONDO_MODO + i), 0);

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

static void desk_disegna(void);      /* the icons: see @PM-DESKTOP below */

static int scrivania_disegna(void)
{
    int ok = 1;

    if (!g_scr) return 1;

    ex_riempi(g_scr, 0, 0, (int)g_sw, (int)g_sh - BARRA_H, EX_SCRIVANIA);
    if (g_sfondo &&
        !ex_immagine_disponi(g_scr, g_sfondo, 0, 0, (int)g_sw,
                             (int)g_sh - BARRA_H, g_sfondo_modo, EX_SCRIVANIA))
        ok = 0;
    desk_disegna();
    return ok;
}

/* =============================================================================
 * THE DESKTOP'S ICONS (@PM-DESKTOP and @PM-UNITA, 27 September 2026)
 *
 * On the left, in columns from the top: what is in the profile's «desktop»
 * directory — $HOME/desktop, /root/desktop for root — files, directories and
 * links to programs. On the right, one column: the mounted drives (CD, USB
 * sticks, floppies, disks). Click selects; double click opens:
 *
 *     a drive or a directory   the file manager, INSIDE it
 *     a link (.lnk)            the program it names
 *     a file                   by its extension: the editor, Archivi, the
 *                              browser; anything else, the file manager on
 *                              the desktop directory
 *
 * ! A LINK IS A SMALL TEXT FILE, «nome.lnk»: EX-OS has no symbolic links on
 * FAT, and a text file anyone can write by hand is the simplest thing that
 * works everywhere:
 *
 *     percorso = /exwin/bin/edit        (required)
 *     nome     = Editor                 (optional: the caption)
 *     icona    = /exwin/icon/...ico     (optional: otherwise the program's
 *                                        own, from applicazioni.txt)
 *
 * ! THE DRIVES ARE READ FROM THE KERNEL'S MOUNT TABLE (mountinfo), and the
 * list is looked at again every DESK_GIRO ms: a CD put in, a stick mounted
 * by automount (started by /boot/avvio.sh) appear on their own. Nothing is
 * redrawn when nothing changed — a signature of names says so.
 *
 * ! WHERE THERE IS NO ICON FILE, A PICTOGRAM IS DRAWN: /exwin/icon/tipi/ is
 * still empty, and a desktop cannot show nothing. The day the drawings exist
 * (cartella.ico, file.ico...), they win.
 * ============================================================================= */
#define DESK_MAX    48
#define DESK_CELLA  76          /* one icon with its caption */
#define DESK_ICONA  32
#define DESK_GIRO   2000

enum { DV_CARTELLA, DV_FILE, DV_LNK, DV_CD, DV_FLOPPY, DV_USB, DV_DISCO };

typedef struct {
    char    nome[40];           /* the caption */
    char    perc[160];          /* what it is: file, directory, mount point */
    char    apri[160];          /* for a link: the program */
    int     tipo;               /* DV_* */
    ExIcona ic;                 /* 0 = draw the pictogram */
    int     x, y;
} VoceDesk;

static VoceDesk     g_dv[DESK_MAX];
static int          g_dv_n = 0;
static int          g_dv_sel = -1;
static unsigned int g_dv_firma = 0;
static char         g_desk_dir[160];

static void desk_dir_trova(void)
{
    const char *casa = getenv("HOME");

    if (!casa || !casa[0] || strcmp(casa, "/") == 0)
        casa = (getuid() == 0) ? "/root" : "";
    snprintf(g_desk_dir, sizeof(g_desk_dir), "%s/desktop", casa);
    /* Made if missing, so there is a place to put things: on a read-only
     * system it simply fails, and the desktop shows the drives only. */
    if (casa[0]) mkdir(casa, 0700);
    mkdir(g_desk_dir, 0755);
}

static unsigned int desk_firma_agg(unsigned int h, const char *s)
{
    while (*s) h = (h ^ (unsigned char)*s++) * 16777619u;
    return (h ^ '|') * 16777619u;
}

/* The program's icon from applicazioni.txt, matching the path. */
static ExIcona desk_icona_app(const char *prog)
{
    unsigned int i;

    for (i = 0; i < g_app_n; i++)
        if (strcmp(g_app[i].percorso, prog) == 0) return icona_di(&g_app[i]);
    return 0;
}

static void desk_lnk_leggi(VoceDesk *v)
{
    char buf[512], *riga, *dopo;
    int  fd = open(v->perc, O_RDONLY, 0), n;

    v->apri[0] = '\0';
    if (fd < 0) return;
    n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    for (riga = buf; riga && *riga; riga = dopo) {
        char *ug, *val, *e;

        dopo = strchr(riga, '\n');
        if (dopo) *dopo++ = '\0';
        ug = strchr(riga, '=');
        if (!ug || riga[0] == '#') continue;
        *ug = '\0';
        val = ug + 1;
        while (*val == ' ' || *val == '\t') val++;
        e = val + strlen(val);
        while (e > val && (e[-1] == ' ' || e[-1] == '\r')) *--e = '\0';
        e = riga + strlen(riga);
        while (e > riga && e[-1] == ' ') *--e = '\0';

        if (strcmp(riga, "percorso") == 0) {
            strncpy(v->apri, val, sizeof(v->apri) - 1);
            v->apri[sizeof(v->apri) - 1] = '\0';
        } else if (strcmp(riga, "nome") == 0 && val[0]) {
            strncpy(v->nome, val, sizeof(v->nome) - 1);
            v->nome[sizeof(v->nome) - 1] = '\0';
        } else if (strcmp(riga, "icona") == 0 && val[0]) {
            v->ic = ex_icona_apri(val);
        }
    }
    if (!v->ic && v->apri[0]) v->ic = desk_icona_app(v->apri);
}

static int desk_tipo_unita(const MountInfo *m)
{
    const char *d = m->dev, *p = m->punto;

    if (strstr(d, "cd") || strstr(d, "atapi") || strstr(p, "cdrom")) return DV_CD;
    if (d[0] == 'f' && d[1] == 'd') return DV_FLOPPY;
    if (strstr(d, "usb") || strstr(p, "USB") || strstr(p, "usb")) return DV_USB;
    return DV_DISCO;
}

/* Reads drives and the desktop directory; returns the signature. With
 * `tieni` the entries are kept, otherwise only the signature is computed. */
static unsigned int desk_leggi(int tieni)
{
    MountInfo    mi[8];
    unsigned int h = 2166136261u, start = 0;
    int          n, i, k = 0;
    DIR         *d;

    if (tieni) g_dv_n = 0;

    /* The drives: everything mounted except the root. */
    while ((n = mountinfo(mi, 8, start)) > 0) {
        for (i = 0; i < n; i++) {
            const char *nome;

            if (strcmp(mi[i].punto, "/") == 0) continue;
            h = desk_firma_agg(h, mi[i].punto);
            if (!tieni || g_dv_n >= DESK_MAX) continue;
            nome = strrchr(mi[i].punto, '/');
            nome = (nome && nome[1]) ? nome + 1 : mi[i].punto;
            memset(&g_dv[g_dv_n], 0, sizeof(VoceDesk));
            strncpy(g_dv[g_dv_n].nome, nome, sizeof(g_dv[0].nome) - 1);
            strncpy(g_dv[g_dv_n].perc, mi[i].punto, sizeof(g_dv[0].perc) - 1);
            g_dv[g_dv_n].tipo = desk_tipo_unita(&mi[i]);
            g_dv_n++;
            k++;
        }
        start += (unsigned int)n;
        if (n < 8) break;
    }

    /* The profile's desktop directory. */
    d = opendir(g_desk_dir);
    if (d) {
        struct dirent *e;

        while ((e = readdir(d)) != 0) {
            VoceDesk *v;
            size_t    l;

            if (e->d_name[0] == '.') continue;          /* . .. and hidden */
            h = desk_firma_agg(h, e->d_name);
            if (!tieni || g_dv_n >= DESK_MAX) continue;
            v = &g_dv[g_dv_n];
            memset(v, 0, sizeof(*v));
            strncpy(v->nome, e->d_name, sizeof(v->nome) - 1);
            snprintf(v->perc, sizeof(v->perc), "%s/%s", g_desk_dir, e->d_name);
            l = strlen(v->nome);
            if (e->d_type == DT_DIR) {
                v->tipo = DV_CARTELLA;
            } else if (l > 4 && strcmp(v->nome + l - 4, ".lnk") == 0) {
                v->tipo = DV_LNK;
                v->nome[l - 4] = '\0';                  /* the caption */
                desk_lnk_leggi(v);
            } else {
                v->tipo = DV_FILE;
            }
            g_dv_n++;
        }
        closedir(d);
    }
    (void)k;
    return h;
}

/* Drives on the right, one column; desktop entries on the left, columns
 * filled from the top. */
static void desk_disponi(void)
{
    int alto = (int)g_sh - BARRA_H, ya = 8, xs = 8, ys = 8, i;

    for (i = 0; i < g_dv_n; i++) {
        VoceDesk *v = &g_dv[i];

        if (v->tipo >= DV_CD) {
            v->x = (int)g_sw - DESK_CELLA - 8;
            v->y = ya;
            ya += DESK_CELLA;
        } else {
            if (ys + DESK_CELLA > alto) { ys = 8; xs += DESK_CELLA; }
            v->x = xs;
            v->y = ys;
            ys += DESK_CELLA;
        }
    }
}

static void desk_cerchio(int cx, int cy, int r, unsigned int c)
{
    int dy;

    for (dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
        ex_riempi(g_scr, cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}

/* The pictograms, 32x32, for what has no icon file. */
static void desk_pittogramma(int tipo, int x, int y)
{
    switch (tipo) {
    case DV_CARTELLA:
        ex_riempi(g_scr, x + 2, y + 6, 12, 4, 0x00D0A030);
        ex_riempi(g_scr, x + 2, y + 9, 28, 19, 0x00E8C050);
        ex_rilievo(g_scr, x + 2, y + 9, 28, 19);
        break;
    case DV_LNK:
    case DV_FILE:
        ex_riempi(g_scr, x + 6, y + 2, 20, 28, EX_BIANCO);
        ex_riquadro_disegna(g_scr, x + 6, y + 2, 20, 28, EX_NERO);
        ex_riempi(g_scr, x + 9, y + 9, 14, 1, 0x00808080);
        ex_riempi(g_scr, x + 9, y + 13, 14, 1, 0x00808080);
        ex_riempi(g_scr, x + 9, y + 17, 10, 1, 0x00808080);
        if (tipo == DV_LNK) {                   /* the little arrow box */
            ex_riempi(g_scr, x + 4, y + 20, 10, 10, EX_BIANCO);
            ex_riquadro_disegna(g_scr, x + 4, y + 20, 10, 10, EX_NERO);
            ex_riempi(g_scr, x + 7, y + 23, 5, 2, EX_BLU);
            ex_riempi(g_scr, x + 10, y + 23, 2, 5, EX_BLU);
        }
        break;
    case DV_CD:
        desk_cerchio(x + 16, y + 16, 14, 0x00C0C0C8);
        desk_cerchio(x + 16, y + 16, 12, 0x00E0E0F0);
        desk_cerchio(x + 16, y + 16, 4, 0x00606060);
        desk_cerchio(x + 16, y + 16, 2, EX_SCRIVANIA);
        break;
    case DV_FLOPPY:
        ex_riempi(g_scr, x + 3, y + 3, 26, 26, 0x00303050);
        ex_riempi(g_scr, x + 9, y + 3, 14, 9, 0x00B0B0B0);
        ex_riempi(g_scr, x + 7, y + 17, 18, 12, EX_BIANCO);
        break;
    case DV_USB:
        ex_riempi(g_scr, x + 10, y + 2, 12, 8, 0x00B0B0B0);
        ex_riempi(g_scr, x + 8, y + 10, 16, 20, 0x00303030);
        ex_riempi(g_scr, x + 14, y + 14, 4, 4, 0x0040C040);
        break;
    default:                                    /* DV_DISCO */
        ex_riempi(g_scr, x + 1, y + 8, 30, 18, 0x00909090);
        ex_rilievo(g_scr, x + 1, y + 8, 30, 18);
        ex_riempi(g_scr, x + 24, y + 20, 4, 3, 0x0040C040);
        break;
    }
}

static void desk_disegna(void)
{
    int i;

    for (i = 0; i < g_dv_n; i++) {
        VoceDesk *v = &g_dv[i];
        int       ix = v->x + (DESK_CELLA - DESK_ICONA) / 2, iy = v->y + 4;
        char      t[12];
        int       l, tx;

        if (v->ic) ex_icona_disegna(g_scr, v->ic, ix, iy, DESK_ICONA, EX_SCRIVANIA);
        else       desk_pittogramma(v->tipo, ix, iy);

        /* The caption, cut to nine characters with «~», white on a dark
         * shadow so it reads on any image; blue when selected. */
        strncpy(t, v->nome, 9);
        t[9] = '\0';
        if (strlen(v->nome) > 9) t[8] = '~';
        l = (int)strlen(t);
        tx = v->x + (DESK_CELLA - l * 8) / 2;
        if (i == g_dv_sel) ex_riempi(g_scr, tx - 2, iy + DESK_ICONA + 2, l * 8 + 4, 18, EX_BLU);
        else               ex_scrivi(g_scr, tx + 1, iy + DESK_ICONA + 4, t, EX_NERO);
        ex_scrivi(g_scr, tx, iy + DESK_ICONA + 3, t, EX_BIANCO);
    }
}

static int desk_sotto(int x, int y)
{
    int i;

    for (i = 0; i < g_dv_n; i++)
        if (x >= g_dv[i].x && x < g_dv[i].x + DESK_CELLA &&
            y >= g_dv[i].y && y < g_dv[i].y + DESK_CELLA) return i;
    return -1;
}

/* Starts a program of ExWin with one argument; from the CD the programs live
 * under /cdrom, as the menu's do. */
static void desk_lancia(const char *prog, const char *arg)
{
    char  p[200];
    char *av[3];
    struct stat st;

    strncpy(p, prog, sizeof(p) - 1);
    p[sizeof(p) - 1] = '\0';
    if (stat(p, &st) != 0 && strncmp(prog, "/exwin/", 7) == 0)
        snprintf(p, sizeof(p), "/cdrom%s", prog);
    av[0] = p;
    av[1] = (char *)arg;
    av[2] = 0;
    if (spawn_ex(av[0], av, environ, 0, 0) < 0) {
        char m[260];
        snprintf(m, sizeof(m), "Non riesco ad avviare %s.", prog);
        ex_dlg_avviso("Scrivania", m);
    }
}

static void desk_apri(int i)
{
    VoceDesk   *v = &g_dv[i];
    const char *e;

    switch (v->tipo) {
    case DV_LNK:
        if (v->apri[0]) desk_lancia(v->apri, 0);
        else ex_dlg_avviso("Scrivania", "Il collegamento non dice che cosa "
                           "aprire: manca la riga \"percorso = ...\".");
        return;
    case DV_FILE:
        /* ! LE ASSOCIAZIONI SONO QUELLE DI tipi.txt (29 settembre 2026,
         * @ASSOCIAZIONI): qui c'era un elenco suo, diverso — le immagini col
         * navigatore — e un file sconosciuto apriva il file manager. */
        (void)e;
        {
            char prog[200];
            if (ex_apri_file(v->perc, prog, sizeof(prog)) < 0) {
                char m[260];
                snprintf(m, sizeof(m), "Non riesco ad avviare %s.", prog);
                ex_dlg_avviso("Scrivania", m);
            }
        }
        return;
    default:                    /* a directory or a drive: the file manager */
        desk_lancia("/exwin/bin/filemgr", v->perc);
        return;
    }
}

/* Every DESK_GIRO ms: anything new? Read again and redraw only then. */
static void desk_controlla(int forza)
{
    unsigned int f = desk_leggi(0);

    if (!forza && f == g_dv_firma) return;
    g_dv_firma = desk_leggi(1);
    if (g_dv_sel >= g_dv_n) g_dv_sel = -1;
    desk_disponi();
    if (g_scr) { scrivania_disegna(); ex_aggiorna(g_scr); }
}



/* =============================================================================
 * THE RIGHT BUTTON ON THE DESKTOP (29 September 2026, @DESK-MENU)
 *
 * Asked for: create, rename, delete and copy folders and files from the
 * desktop. Everything happens in the desktop directory ($HOME/desktop); the
 * drives only offer "Apri", since renaming or deleting a mount point from
 * here is never what was meant.
 *
 * ! THE CLIPBOARD IS THIS PROCESS'S, one path at a time: Copia or Taglia
 * remember it, Incolla puts it on the desktop. A pasted name that is already
 * there becomes "copia di NAME" rather than overwriting it - the VFS
 * truncates on open, and what is lost that way does not come back.
 * ============================================================================= */
#define DESK_PROF   16          /* nesting a copy or a delete goes down to */

static char g_dv_app[160] = "";         /* the path copied or cut, "" = none */
static int  g_dv_taglia = 0;

static int desk_e_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int desk_copia_file(const char *da, const char *a)
{
    static char buf[4096];
    int fa, fb, n;

    fa = open(da, O_RDONLY, 0);
    if (fa < 0) return -1;
    fb = open(a, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fb < 0) { close(fa); return -1; }
    while ((n = (int)read(fa, buf, sizeof(buf))) > 0)
        if ((int)write(fb, buf, (unsigned int)n) != n) { n = -1; break; }
    close(fa);
    close(fb);
    return n < 0 ? -1 : 0;
}

/* Copies a file or a whole directory; 0 = done. */
static int desk_copia(const char *da, const char *a, int giu)
{
    DIR           *d;
    struct dirent *e;
    int            r = 0;

    if (!desk_e_dir(da)) return desk_copia_file(da, a);
    if (giu > DESK_PROF) return -1;
    /* ! NOT INTO ITSELF: a folder pasted inside its own subtree would copy
     * forever, each level holding the one before. */
    if (strncmp(a, da, strlen(da)) == 0 && a[strlen(da)] == '/') return -1;
    if (mkdir(a, 0755) < 0 && errno != EEXIST) return -1;
    d = opendir(da);
    if (!d) return -1;
    while (r == 0 && (e = readdir(d)) != 0) {
        char s[200], t[200];

        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(s, sizeof(s), "%s/%s", da, e->d_name);
        snprintf(t, sizeof(t), "%s/%s", a, e->d_name);
        r = desk_copia(s, t, giu + 1);
    }
    closedir(d);
    return r;
}

/* Deletes a file or a whole directory; 0 = done. The directory is read again
 * from the start after every removal: readdir over a directory being emptied
 * is not something to trust. */
static int desk_cancella(const char *p, int giu)
{
    if (!desk_e_dir(p)) return unlink(p) == 0 ? 0 : -1;
    if (giu > DESK_PROF) return -1;
    for (;;) {
        DIR           *d = opendir(p);
        struct dirent *e;
        char           s[200];
        int            trovato = 0;

        if (!d) return -1;
        while ((e = readdir(d)) != 0) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            snprintf(s, sizeof(s), "%s/%s", p, e->d_name);
            trovato = 1;
            break;
        }
        closedir(d);
        if (!trovato) break;
        if (desk_cancella(s, giu + 1) != 0) return -1;
    }
    return rmdir(p) == 0 ? 0 : -1;
}

static const char *desk_base(const char *p)
{
    const char *u = strrchr(p, '/');
    return u ? u + 1 : p;
}

/* The name as typed, checked: not empty, no "/", not taken. */
static int desk_nome_nuovo(const char *titolo, const char *domanda,
                           const char *ok, char *nome, unsigned int max,
                           char *perc, unsigned int pmax)
{
    if (!ex_dlg_chiedi(titolo, domanda, ok, nome, max) || !nome[0]) return 0;
    if (strchr(nome, '/') || !strcmp(nome, ".") || !strcmp(nome, "..")) {
        ex_dlg_avviso(titolo, "Un nome non contiene \"/\" e non e' \".\" "
                      "ne' \"..\".");
        return 0;
    }
    snprintf(perc, pmax, "%s/%s", g_desk_dir, nome);
    if (access(perc, 0) == 0) {
        char m[160];
        snprintf(m, sizeof(m), "%s esiste gia' sulla scrivania.", nome);
        ex_dlg_avviso(titolo, m);
        return 0;
    }
    return 1;
}

static void desk_errore(const char *titolo, const char *cosa)
{
    char m[260];
    snprintf(m, sizeof(m), "%s: %s.", cosa, strerror(errno));
    ex_dlg_avviso(titolo, m);
}

static void desk_nuovo(int cartella)
{
    char nome[40] = "", p[200];
    int  fd;

    if (!desk_nome_nuovo(cartella ? "Nuova cartella" : "Nuovo file",
                         "Il nome:", "Crea", nome, sizeof(nome), p, sizeof(p)))
        return;
    if (cartella) {
        if (mkdir(p, 0755) < 0) desk_errore("Nuova cartella", nome);
        return;
    }
    fd = open(p, O_WRONLY | O_CREAT, 0644);
    if (fd < 0) desk_errore("Nuovo file", nome);
    else close(fd);
}

static void desk_rinomina(VoceDesk *v)
{
    char nome[40], p[200], domanda[80];

    strncpy(nome, desk_base(v->perc), sizeof(nome) - 1);
    nome[sizeof(nome) - 1] = '\0';
    snprintf(domanda, sizeof(domanda), "Il nome nuovo di %s:", nome);
    if (!desk_nome_nuovo("Rinomina", domanda, "Rinomina", nome, sizeof(nome),
                         p, sizeof(p)))
        return;
    if (rename(v->perc, p) != 0) desk_errore("Rinomina", v->nome);
}

static void desk_cancella_voce(VoceDesk *v)
{
    char m[200];
    int  dir = desk_e_dir(v->perc);

    snprintf(m, sizeof(m), dir ? "Cancello la cartella %s e tutto quello che "
             "contiene?" : "Cancello %s?", desk_base(v->perc));
    if (!ex_dlg_conferma("Cancella", m, "Cancella", "Annulla")) return;
    if (desk_cancella(v->perc, 0) != 0) desk_errore("Cancella", v->nome);
    if (!strcmp(g_dv_app, v->perc)) g_dv_app[0] = '\0';
}

static void desk_incolla(void)
{
    char a[200];
    const char *b;
    int  k;

    if (!g_dv_app[0]) {
        ex_dlg_avviso("Incolla", "Non c'e' niente da incollare: prima Copia "
                      "o Taglia.");
        return;
    }
    if (access(g_dv_app, 0) != 0) {
        ex_dlg_avviso("Incolla", "Quello che era stato copiato non c'e' piu'.");
        g_dv_app[0] = '\0';
        return;
    }
    b = desk_base(g_dv_app);
    snprintf(a, sizeof(a), "%s/%s", g_desk_dir, b);
    if (!strcmp(a, g_dv_app) && g_dv_taglia) { g_dv_app[0] = '\0'; return; }
    for (k = 1; access(a, 0) == 0 && k < 100; k++) {
        if (k == 1) snprintf(a, sizeof(a), "%s/copia di %s", g_desk_dir, b);
        else        snprintf(a, sizeof(a), "%s/copia %d di %s", g_desk_dir, k, b);
    }
    if (access(a, 0) == 0) { ex_dlg_avviso("Incolla", "Troppe copie."); return; }

    /* Cut: a rename when it can (same drive), otherwise copy and delete. */
    if (g_dv_taglia && rename(g_dv_app, a) == 0) { g_dv_app[0] = '\0'; return; }
    if (desk_copia(g_dv_app, a, 0) != 0) {
        desk_errore("Incolla", b);
        return;
    }
    if (g_dv_taglia) {
        if (desk_cancella(g_dv_app, 0) != 0) desk_errore("Taglia", b);
        g_dv_app[0] = '\0';
    }
}

static void desk_menu(ExFinestra f, int x, int y)
{
    static const char *const VUOTO[] = { "Nuova cartella", "Nuovo file",
                                         "-", "Incolla" };
    static const char *const VOCE[]  = { "Apri", "-", "Rinomina", "Copia",
                                         "Taglia", "Cancella", "-",
                                         "Nuova cartella", "Nuovo file",
                                         "Incolla" };
    static const char *const UNITA[] = { "Apri" };
    int       i = desk_sotto(x, y), k;
    VoceDesk *v;

    if (i != g_dv_sel) { g_dv_sel = i; scrivania_disegna(); ex_aggiorna(f); }
    if (i < 0) {
        k = ex_menu_comparsa(f, x, y, VUOTO, 4);
        if (k == 0) desk_nuovo(1);
        else if (k == 1) desk_nuovo(0);
        else if (k == 3) desk_incolla();
    } else if (g_dv[i].tipo >= DV_CD) {
        if (ex_menu_comparsa(f, x, y, UNITA, 1) == 0) desk_apri(i);
        return;
    } else {
        v = &g_dv[i];
        k = ex_menu_comparsa(f, x, y, VOCE, 10);
        switch (k) {
        case 0: desk_apri(i); return;
        case 2: desk_rinomina(v); break;
        case 3:
        case 4:
            strncpy(g_dv_app, v->perc, sizeof(g_dv_app) - 1);
            g_dv_app[sizeof(g_dv_app) - 1] = '\0';
            g_dv_taglia = (k == 4);
            return;
        case 5: desk_cancella_voce(v); break;
        case 7: desk_nuovo(1); break;
        case 8: desk_nuovo(0); break;
        case 9: desk_incolla(); break;
        default: return;
        }
    }
    desk_controlla(1);          /* what changed shows at once, not in 2 s */
}
static long scr_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    /* ! E SI MANDA AL SERVER SUBITO. Disegnare riempie la zona condivisa; e'
     * ex_aggiorna che dice al server di ricomporla. Senza, la scrivania
     * sarebbe giusta nella memoria del processo e grigia sullo schermo — che
     * e' esattamente il difetto di prima, con una causa in piu' da cercare. */
    if (msg == EXM_DISEGNA) { scrivania_disegna(); ex_aggiorna(f); return 0; }

    /* The icons (@PM-DESKTOP, @PM-UNITA): a click selects, a double click
     * opens, and the clock looks for new drives and files. */
    if (msg == EXM_TEMPO) { desk_controlla(0); return 0; }
    if (msg == EXM_MOUSE_GIU || msg == EXM_DOPPIOCLIC) {
        int i = desk_sotto(EX_X(lp), EX_Y(lp));

        if (i != g_dv_sel) {
            g_dv_sel = i;
            scrivania_disegna();
            ex_aggiorna(f);
        }
        if (msg == EXM_DOPPIOCLIC && i >= 0) desk_apri(i);
        return 0;
    }
    if (msg == EXM_MOUSE_DESTRO) { desk_menu(f, EX_X(lp), EX_Y(lp)); return 0; }
    return ex_procedura_base(f, msg, wp, lp);
}

/* =============================================================================
 * THE DESKTOP IMAGE, CHOSEN FROM THE SETTINGS (@PM-SFONDO, 27 September 2026)
 *
 * Saved in the PROFILE of whoever is logged in — $HOME/.exwin/config/pm.cfg,
 * so /root/.exwin/config/pm.cfg for root and /home/<utente>/.exwin/config/
 * pm.cfg for a user (asked on 27 September 2026: one desktop per person, not
 * one per machine). Read at start unless `pm -s FILE` says otherwise.
 * Applied at once: the desktop is repainted.
 *
 * ! AN IMAGE THAT DOES NOT LOAD IS NOT KEPT: the desktop goes back to the one
 * before and says so. Saved anyway, the next start would open on a grey
 * desktop and nobody would know why.
 * ============================================================================= */
static char g_sfondo_buf[160];

/* The profile: $HOME, as login sets it. Without login (the live CD) HOME is
 * «/», and then root's profile is /root. Fills `dir` with .../.exwin/config
 * and `cfg` with the file; `crea` makes the directories on the way. */
static void profilo_cfg(char *dir, char *cfg, unsigned int max, int crea)
{
    const char *casa = getenv("HOME");
    char        t[160];

    if (!casa || !casa[0] || strcmp(casa, "/") == 0)
        casa = (getuid() == 0) ? "/root" : "/";
    if (crea) {
        mkdir(casa, 0700);
        snprintf(t, sizeof(t), "%s/.exwin", strcmp(casa, "/") ? casa : "");
        mkdir(t, 0755);
    }
    snprintf(dir, max, "%s/.exwin/config", strcmp(casa, "/") ? casa : "");
    if (crea) mkdir(dir, 0755);
    snprintf(cfg, max, "%s/pm.cfg", dir);
}

static void sfondo_leggi_cfg(void)
{
    char buf[256], *p, *e, dir[160], cfg[160];
    int  fd, n;

    profilo_cfg(dir, cfg, sizeof(cfg), 0);
    fd = open(cfg, O_RDONLY, 0);

    if (fd < 0) return;
    n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    if ((p = strstr(buf, "disposizione")) && (p = strchr(p, '='))) {
        int k;
        for (p++; *p == ' ' || *p == '\t'; p++) ;
        for (k = 0; k < 4; k++)
            if (!strncmp(p, DISPOSIZIONI[k], strlen(DISPOSIZIONI[k])))
                g_sfondo_modo = k;
    }
    p = strstr(buf, "sfondo");
    if (!p || !(p = strchr(p, '='))) return;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    e = p;
    while (*e && *e != '\n' && *e != '\r') e++;
    while (e > p && e[-1] == ' ') e--;
    if (e == p || (size_t)(e - p) >= sizeof(g_sfondo_buf)) return;
    memcpy(g_sfondo_buf, p, (size_t)(e - p));
    g_sfondo_buf[e - p] = '\0';
    g_sfondo = g_sfondo_buf;
}

static int sfondo_salva(void)
{
    char t[220], dir[160], cfg[160];
    int  fd, n;
    struct stat st;

    profilo_cfg(dir, cfg, sizeof(cfg), 1);
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    fd = open(cfg, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    n = snprintf(t, sizeof(t), "# pm: la scrivania. Lo riscrive Impostazioni.\n"
                               "sfondo = %s\ndisposizione = %s\n",
                 g_sfondo ? g_sfondo : "", DISPOSIZIONI[g_sfondo_modo]);
    if (write(fd, t, (unsigned int)n) != n) { close(fd); return 0; }
    close(fd);
    return 1;
}

static void sfondo_etichetta(void)
{
    char t[200];

    if (!g_impost_sfondo) return;
    snprintf(t, sizeof(t), "Sfondo (%s): %s", DISPOSIZIONI[g_sfondo_modo],
             g_sfondo ? g_sfondo : "(nessuno)");
    ex_testo_metti(g_impost_sfondo, t);
    ex_procedura_base(g_impost, EXM_DISEGNA, 0, 0);
}

static void sfondo_applica(const char *nuovo)
{
    const char *prima = g_sfondo;
    static char vecchio[160];

    if (prima) { strncpy(vecchio, prima, sizeof(vecchio) - 1); vecchio[sizeof(vecchio) - 1] = '\0'; }
    if (nuovo) {
        strncpy(g_sfondo_buf, nuovo, sizeof(g_sfondo_buf) - 1);
        g_sfondo_buf[sizeof(g_sfondo_buf) - 1] = '\0';
        g_sfondo = g_sfondo_buf;
    } else {
        g_sfondo = 0;
    }

    if (!scrivania_disegna()) {
        char m[240];
        snprintf(m, sizeof(m), "%s non si legge come immagine: resta lo "
                               "sfondo di prima.", nuovo);
        ex_dlg_avviso("Sfondo", m);
        if (prima) { strcpy(g_sfondo_buf, vecchio); g_sfondo = g_sfondo_buf; }
        else       g_sfondo = 0;
        scrivania_disegna();
    }
    ex_aggiorna(g_scr);
    sfondo_etichetta();

    if (!sfondo_salva()) {
        char dir[160], cfg[160], m[300];
        profilo_cfg(dir, cfg, sizeof(cfg), 0);
        snprintf(m, sizeof(m), "Lo sfondo vale adesso, ma non riesco a scrivere "
                 "%s (sistema in sola lettura?): al prossimo avvio torna quello "
                 "di prima.", cfg);
        ex_dlg_avviso("Sfondo", m);
    }
}

static void sfondo_scegli(void)
{
    char p[160];

    strncpy(p, g_sfondo ? g_sfondo : "/", sizeof(p) - 1);
    p[sizeof(p) - 1] = '\0';
    if (!ex_dlg_apri(p, sizeof(p))) return;
    sfondo_applica(p);
}

static void sfondo_niente(void) { sfondo_applica(0); }

/* Angolo, Centro, Allarga, Ripeti: the same image, laid out again. */
static void sfondo_modo(int modo)
{
    g_sfondo_modo = modo;
    scrivania_disegna();
    ex_aggiorna(g_scr);
    sfondo_etichetta();
    if (!sfondo_salva())
        ex_dlg_avviso("Sfondo", "La disposizione vale adesso, ma pm.cfg non si "
                      "scrive (sistema in sola lettura?).");
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
        if (!g_sfondo) sfondo_leggi_cfg();     /* -s wins over the saved one */
        desk_dir_trova();
        desk_controlla(1);
        ex_sveglia(scr, DESK_GIRO);

        if (!scrivania_disegna()) {
            char msg[160];
            snprintf(msg, sizeof(msg), "pm: %s: formato non riconosciuto",
                     g_sfondo ? g_sfondo : "?");
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
