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
#include "exinfo.h"
#include "kbd_proto.h"

#define FIN_W       640
#define FIN_H       420

/* La barra dei menu occupa i primi 20 pixel; l'area comincia sotto. */
#define MENU_H      20
#define AREA_X      4
#define AREA_Y      (MENU_H + 4)
#define BASSO       24          /* la riga di stato in fondo */

#define PERC_MAX    192

#define ID_SALVA      1
#define ID_NUOVO      2
#define ID_RICARICA   3
#define ID_APRI       4
#define ID_SALVACOME  5
#define ID_ESCI       6

#define ID_TAGLIA     10
#define ID_COPIA      11
#define ID_INCOLLA    12
#define ID_CANCELLA   13
#define ID_SELTUTTO   14
#define ID_ANNULLA    15

#define ID_ISTRUZIONI 20
#define ID_INFO       21

static char g_perc[PERC_MAX] = "";
static int  g_parziale = 0;     /* letto SOLO IN PARTE: non si salva */
static char g_avviso[96] = "";

static ExFinestra g_f, g_area, g_stato, g_menu;

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

/* ! UNA SOLA FUNZIONE PER USCIRE, chiamata da tre parti — il menu, Ctrl+Q e il
 * pulsante di chiusura. Erano tre copie della stessa domanda, e tre copie di
 * una domanda che difende il lavoro di qualcuno divergono alla prima modifica:
 * ne resterebbe una che non chiede piu' niente, e nessuno se ne accorgerebbe
 * finche' non perde un testo. */
static void esci_se_si_puo(void)
{
    if (ex_area_modificato(g_area) &&
        !ex_dlg_conferma("Modifiche non salvate",
                         "Il testo e' cambiato. Uscire senza salvare?",
                         "Esci", "Torna al testo")) {
        strcpy(g_avviso, "non uscito: il testo e' ancora qui");
        return;
    }
    ex_esci(0);
}

static void istruzioni(void)
{
    ex_dlg_avviso("Istruzioni",
                  "F10 apre i menu, le frecce li girano, Invio sceglie.  "
                  "Shift piu' le frecce sceglie il testo; Ctrl+S salva, "
                  "Ctrl+Q esce.  Gli appunti sono di tutta la scrivania: "
                  "si copia qui e si incolla in un altro editor.");
}

/* =============================================================================
 * L'ANNULLAMENTO
 *
 * ! SI TIENE IL TESTO INTERO, NON LE OPERAZIONI, e per una volta la soluzione
 * grossolana e' quella giusta. Un elenco di operazioni — «tolti 12 byte a riga
 * 4 colonna 7» — e' piu' piccolo, ma va tenuto d'accordo con ogni cosa che
 * modifica il testo: sbagliarne una vuol dire un annullamento che ricostruisce
 * un testo che non e' mai esistito, cioe' un difetto che si scopre dopo aver
 * perso del lavoro. L'area sta in 512 righe da 200 colonne, quindi il caso
 * peggiore e' cento chilobyte: si copia e non si sbaglia.
 *
 * ! IL TETTO C'E' SU TUTT'E DUE LE COSE — quanti passi e quanti byte — e
 * quando si sfora si butta il PIU' VECCHIO. Un annullamento che smette di
 * funzionare perche' la memoria e' finita sarebbe peggio di uno corto.
 *
 * ! QUELLO CHE NON SI ANNULLA, DICHIARATO: la digitazione. I tasti se li
 * mangia il controllo areatesto e a questo programma non arrivano mai — c'e'
 * scritto anche accanto a EXM_TASTO qui sotto. Si annullano i tre COMANDI che
 * modificano il testo — taglia, incolla, cancella — e nient'altro: nemmeno il
 * caricamento di un file, che ha gia' la sua domanda prima di buttare via il
 * lavoro. Per la digitazione servirebbe che fosse il controllo areatesto a
 * segnare i passi, non l'applicazione.
 *
 * ! E NON C'E' IL «RIPETI». Annullare un annullamento vuole una seconda pila,
 * e vuole soprattutto decidere quando si svuota: qui si e' fermata la prima
 * volta.
 * ============================================================================= */
#define ANNULLA_MAX     16
#define ANNULLA_BYTE    (192u * 1024u)

typedef struct {
    char        *testo;     /* le righe unite da '\n' */
    unsigned int byte;
} Passo;

static Passo        g_passi[ANNULLA_MAX];
static int          g_passi_n = 0;
static unsigned int g_passi_byte = 0;

static void passo_libera(int i)
{
    if (!g_passi[i].testo) return;
    free(g_passi[i].testo);
    g_passi[i].testo = 0;
    g_passi_byte -= g_passi[i].byte;
    g_passi[i].byte = 0;
}

/* Butta il piu' vecchio e fa scorrere gli altri. */
static void passo_scarta_vecchio(void)
{
    int i;

    if (g_passi_n == 0) return;
    passo_libera(0);
    for (i = 1; i < g_passi_n; i++) g_passi[i - 1] = g_passi[i];
    g_passi_n--;
    g_passi[g_passi_n].testo = 0;
    g_passi[g_passi_n].byte  = 0;
}

/* Il testo di adesso, in un blocco solo. Rende 0 se non c'e' memoria. */
static char *testo_di_adesso(unsigned int *byte)
{
    unsigned int n = ex_area_righe(g_area), i, tot = 1;
    char        *b;

    for (i = 0; i < n; i++) {
        const char *r = ex_area_riga(g_area, i);
        unsigned int k = 0;

        while (r && r[k]) k++;
        tot += k + 1;                       /* la riga piu' il suo a capo */
    }

    b = (char *)malloc(tot);
    if (!b) return 0;

    tot = 0;
    for (i = 0; i < n; i++) {
        const char *r = ex_area_riga(g_area, i);
        unsigned int k = 0;

        while (r && r[k]) b[tot++] = r[k++];
        b[tot++] = '\n';
    }
    b[tot] = '\0';
    *byte = tot;
    return b;
}

/* Si chiama PRIMA di ogni cosa che modifica il testo. */
static void annulla_segna(void)
{
    unsigned int byte = 0;
    char        *t = testo_di_adesso(&byte);

    if (!t) return;             /* senza memoria si rinuncia al passo, non al
                                 * comando: meglio un annullamento in meno che
                                 * un'operazione che non si fa */

    if (g_passi_n >= ANNULLA_MAX) passo_scarta_vecchio();
    while (g_passi_n > 0 && g_passi_byte + byte > ANNULLA_BYTE)
        passo_scarta_vecchio();

    g_passi[g_passi_n].testo = t;
    g_passi[g_passi_n].byte  = byte;
    g_passi_byte += byte;
    g_passi_n++;
}

/* Rimette il testo del passo piu' recente. Rende 0 se non c'era niente. */
static int annulla_fai(void)
{
    char        *t;
    unsigned int i, a;
    char         riga[512];

    if (g_passi_n == 0) return 0;

    t = g_passi[g_passi_n - 1].testo;
    ex_area_svuota(g_area);

    i = 0;
    while (t[i]) {
        a = 0;
        while (t[i] && t[i] != '\n' && a < sizeof(riga) - 1) riga[a++] = t[i++];
        riga[a] = '\0';
        ex_area_aggiungi(g_area, riga);
        if (t[i] == '\n') i++;
    }

    g_passi_n--;
    passo_libera(g_passi_n);
    return 1;
}

static void informazioni(void)
{
    char t[640];

    exinfo_testo(t, sizeof(t), "Editor",
                 "L'editor di testo di EX-OS, sul toolkit ExWin.  Il testo, "
                 "il cursore e lo scorrimento sono del controllo areatesto; "
                 "qui dentro c'e' solo leggere un file, scriverlo e decidere "
                 "cosa fare quando va storto.");
    ex_dlg_avviso("Informazioni su", t);
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    unsigned int c;

    switch (msg) {
    /* ! IL MENU E I PULSANTI ARRIVANO QUI ALLO STESSO MODO, con lo stesso id.
     * E' il motivo per cui aggiungere i menu non ha voluto una riga di codice
     * nuovo qui dentro: una voce di menu E' un pulsante, detto in un altro
     * posto. */
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
        if (wp == ID_ESCI)      { esci_se_si_puo();   break; }

        if (wp == ID_ANNULLA)   {
            if (annulla_fai()) strcpy(g_avviso, "annullato");
            else               strcpy(g_avviso, "non c'e' niente da annullare");
            break;
        }

        if (wp == ID_TAGLIA)    {
            int n;

            annulla_segna();
            n = ex_area_taglia(g_area);
            if (n) sprintf(g_avviso, "tagliati %d byte", n);
            else   strcpy(g_avviso, "non c'e' niente di scelto");
            break;
        }
        if (wp == ID_COPIA)     {
            int n = ex_area_copia(g_area);
            if (n) sprintf(g_avviso, "copiati %d byte", n);
            else   strcpy(g_avviso, "non c'e' niente di scelto");
            break;
        }
        if (wp == ID_INCOLLA)   {
            int n;

            annulla_segna();
            n = ex_area_incolla(g_area);
            if (n) sprintf(g_avviso, "incollati %d byte", n);
            else   strcpy(g_avviso, "gli appunti sono vuoti");
            break;
        }
        if (wp == ID_CANCELLA)  {
            annulla_segna();
            if (!ex_area_cancella(g_area))
                strcpy(g_avviso, "non c'e' niente di scelto");
            break;
        }
        if (wp == ID_SELTUTTO)  { ex_area_seleziona_tutto(g_area); break; }

        if (wp == ID_ISTRUZIONI) { istruzioni();  break; }
        if (wp == ID_INFO)       { informazioni(); break; }
        return 0;

    case EXM_TASTO:
        /* ! QUI ARRIVANO SOLO LE SCORCIATOIE. Le lettere, le frecce, il
         * Backspace e l'Invio li ha gia' mangiati l'area di testo: se sono
         * arrivate fin qui, non erano per lei. */
        g_avviso[0] = '\0';
        c = wp & KBD_KEY_MASK;

        /* ! LE SCORCIATOIE LE ESEGUE L'APPLICAZIONE, NON IL MENU. Il menu le
         * SCRIVE — e' il tab nel testo della voce — ma non le cattura: un menu
         * che si prendesse Ctrl+S da solo se lo prenderebbe anche mentre si
         * scrive dentro una casella di testo, e non c'e' modo di sapere da
         * dentro il toolkit se in quel momento ha senso. */
        if (wp & KBD_MOD_CTRL) {
            if (c == 's' || c == 'S') { salva();               break; }
            if (c == 'x' || c == 'X') { annulla_segna();
                                        ex_area_taglia(g_area);  break; }
            if (c == 'c' || c == 'C') { ex_area_copia(g_area);   break; }
            if (c == 'v' || c == 'V') { annulla_segna();
                                        ex_area_incolla(g_area); break; }
            if (c == 'z' || c == 'Z') {
                if (annulla_fai()) strcpy(g_avviso, "annullato");
                else               strcpy(g_avviso, "non c'e' niente da annullare");
                break;
            }
            if (c == 'a' || c == 'A') { ex_area_seleziona_tutto(g_area); break; }
            if (c == 'q' || c == 'Q') { esci_se_si_puo();      break; }
        }
        return ex_procedura_base(f, msg, wp, lp);

    case EXM_CHIUDI:
        /* ! CHIUDERE IN SILENZIO UN TESTO MODIFICATO E' IL MODO PIU' FACILE DI
         * PERDERE IL LAVORO DI QUALCUNO. La risposta prudente e' «no»:
         * chiudere il dialogo o battere Esc lascia l'editor aperto col testo
         * dentro. */
        esci_se_si_puo();
        break;

    /* La finestra ha cambiato misura: l'area di testo prende tutto lo spazio
     * che resta fra la barra dei menu e la riga di stato. */
    case EXM_MISURA: {
        int w = EX_X(lp), h = EX_Y(lp);

        ex_misura(g_area, w - AREA_X * 2, h - AREA_Y - BASSO);
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

    if (argc >= 2) {
        strncpy(g_perc, argv[1], PERC_MAX - 1);
        g_perc[PERC_MAX - 1] = '\0';
    }

    /* ! EX_AUTO E EX_RIDIM, e sono due richieste diverse. EX_AUTO dice «mettila
     * tu», ed e' cio' che permette di aprire due editor senza che il secondo
     * finisca esattamente sopra il primo; EX_RIDIM dice che la finestra si puo'
     * tirare per l'angolo, e impegna a rispondere a EXM_MISURA. */
    g_f = ex_crea("finestra", "Editor",
                  EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                  EX_AUTO, EX_AUTO, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("edit: il server a finestre non risponde.\n");
        printf("      Avvialo con:  exwin\n");
        return 1;
    }

    /* ! I MENU HANNO PRESO IL POSTO DEI CINQUE PULSANTI, e non ci si e'
     * aggiunti accanto. Una fila di pulsanti che fa le stesse cose di un menu
     * e' due posti in cui leggere cosa sa fare il programma, e il secondo si
     * dimentica di crescere: «Taglia» non ci sarebbe mai finito. */
    g_menu = ex_menu(g_f);
    ex_menu_voce(g_menu, "File", "Nuovo",            ID_NUOVO);
    ex_menu_voce(g_menu, "File", "Apri...",          ID_APRI);
    ex_menu_voce(g_menu, "File", "-",                0);
    ex_menu_voce(g_menu, "File", "Salva\tCtrl+S",     ID_SALVA);
    ex_menu_voce(g_menu, "File", "Salva con nome...", ID_SALVACOME);
    ex_menu_voce(g_menu, "File", "Ricarica",         ID_RICARICA);
    ex_menu_voce(g_menu, "File", "-",                0);
    ex_menu_voce(g_menu, "File", "Esci\tCtrl+Q",      ID_ESCI);

    ex_menu_voce(g_menu, "Modifica", "Annulla\tCtrl+Z", ID_ANNULLA);
    ex_menu_voce(g_menu, "Modifica", "-",               0);
    ex_menu_voce(g_menu, "Modifica", "Taglia\tCtrl+X",  ID_TAGLIA);
    ex_menu_voce(g_menu, "Modifica", "Copia\tCtrl+C",   ID_COPIA);
    ex_menu_voce(g_menu, "Modifica", "Incolla\tCtrl+V", ID_INCOLLA);
    ex_menu_voce(g_menu, "Modifica", "-",              0);
    ex_menu_voce(g_menu, "Modifica", "Cancella\tCanc",  ID_CANCELLA);
    ex_menu_voce(g_menu, "Modifica", "Seleziona tutto\tCtrl+A", ID_SELTUTTO);

    ex_menu_voce(g_menu, "Info", "Istruzioni",      ID_ISTRUZIONI);
    ex_menu_voce(g_menu, "Info", "Informazioni su", ID_INFO);

    g_area = ex_crea("areatesto", "", EX_FIGLIO,
                     AREA_X, AREA_Y, FIN_W - AREA_X * 2,
                     FIN_H - AREA_Y - BASSO, g_f, 0, 0);
    if (!g_area) {
        printf("edit: non riesco a creare l'area di testo\n");
        return 1;
    }

    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      6, FIN_H - 22, FIN_W - 12, 16, g_f, 0, 0);

    /* ! IL FUOCO ALL'AREA, ESPLICITAMENTE: e' l'unico controllo della finestra
     * che i tasti se li merita. La barra dei menu il fuoco non lo prende — un
     * menu che tenesse la tastiera renderebbe muta l'area — e risponde solo a
     * F10, che a menu chiuso non serve a nessun altro. */
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
 * ! NIENTE ANNULLAMENTO. /bin/gfedit ce l'ha, a giornale di operazioni; qui
 * no, ed e' la cosa che manca di piu' adesso che c'e' un «Taglia» — un taglio
 * sbagliato non si rimette a posto.
 *
 * ! LA SELEZIONE SI FA COI TASTI, NON COL MOUSE. Shift piu' le frecce
 * funziona; trascinare il puntatore sul testo no, perche' il server manda il
 * bottone giu' e il bottone su ma non il movimento con il bottone premuto —
 * WIN_EV_MOUSE_MOSSO e' nel protocollo e nessuno lo manda ancora.
 *
 * ! E GLI APPUNTI SONO SOLO TESTO. Una zona condivisa da 4 KB con dentro dei
 * byte: chi copia mille righe ne ritrova quante ce ne stanno. Un servizio
 * degli appunti con piu' formati e senza tetto e' un'altra cosa, e la vorra'
 * il giorno che qualcosa che non sia testo avra' da copiare.
 * ============================================================================= */
