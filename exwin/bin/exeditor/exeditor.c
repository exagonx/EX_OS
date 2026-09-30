/* =============================================================================
 * exwin/bin/exeditor/exeditor.c
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
 *     /exwin/bin/exeditor [FILE...]
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
#include "exrtf_vista.h"

/* +0.001 a ogni modifica: `exeditor -version` la stampa. Vedi EX_VERSIONE in libc.h. */
#define VERSIONE_APP "0.006"
EX_VERSIONE("exeditor", VERSIONE_APP);

#define FIN_W       640
#define FIN_H       420

/* La barra dei menu occupa i primi 20 pixel; l'area comincia sotto. */
#define MENU_H      20
#define SCHEDE_H    22          /* la barra delle schede sotto i menu */
#define AREA_X      4
#define AREA_Y      (MENU_H + SCHEDE_H + 4)
#define BASSO       24          /* la riga di stato in fondo */
#define BARRA_W     16          /* la barra di scorrimento a destra */

#define PERC_MAX    192

#define ID_SALVA      1
#define ID_NUOVO      2
#define ID_RICARICA   3
#define ID_APRI       4
#define ID_SALVACOME  5
#define ID_ESCI       6
#define ID_NUOVO_RTF  7

#define ID_TAGLIA     10
#define ID_COPIA      11
#define ID_INCOLLA    12
#define ID_CANCELLA   13
#define ID_SELTUTTO   14
#define ID_ANNULLA    15
#define ID_CERCA      16
#define ID_AVANTI     17
#define ID_INDIETRO   18
#define ID_SOSTITUISCI 19

#define ID_ISTRUZIONI 20
#define ID_INFO       21

#define ID_BARRA      30
#define ID_SCHEDE     31
#define ID_CHIUDI     32

/* The RTF format bar and the Formato menu (@RTF). The bar's three switches
 * send their new state; the menu's entries switch. */
#define ID_R_G        40
#define ID_R_C        41
#define ID_R_S        42
#define ID_R_FAM      43
#define ID_R_CORPO    44
#define ID_R_COLORE   45
#define ID_R_SIN      46
#define ID_R_CEN      47
#define ID_R_DES      48
#define ID_R_GIU      49
#define ID_M_G        50
#define ID_M_C        51
#define ID_M_S        52

static char g_perc[PERC_MAX] = "";
static int  g_parziale = 0;     /* letto SOLO IN PARTE: non si salva */
static char g_avviso[96] = "";

static ExFinestra g_f, g_area, g_stato, g_menu, g_barra, g_schede;

/* The window's client size, for the RTF view that has no control to resize. */
static int g_fw = FIN_W, g_fh = FIN_H;

/* The chosen tab is a rich text document (@RTF): the view shows it instead of
 * the text area. See "RTF MODE" below. */
static int g_rtf = 0;
static ExRtfVista g_vista;
static int rtf_salva(void);
static int rtf_carica(const char *percorso);
static void vista_disegna_se(void);

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

static void doc_apri(const char *perc);

/* ! APRIRE NON BUTTA PIU' VIA NIENTE (29 settembre 2026): il file va in una
 * scheda sua, quindi la domanda «il testo e' cambiato, aprire lo stesso?»
 * non serve piu'. */
static void apri_con_dialogo(void)
{
    char nuovo[PERC_MAX];

    strncpy(nuovo, g_perc, PERC_MAX - 1);
    nuovo[PERC_MAX - 1] = '\0';

    if (!ex_dlg_apri(nuovo, PERC_MAX)) {
        strcpy(g_avviso, "apertura annullata");
        return;
    }
    doc_apri(nuovo);
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
    if (g_rtf) return rtf_salva();

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
static void doc_segui_titolo(void);

static void stato_aggiorna(void)
{
    char s[200];

    if (g_schede) doc_segui_titolo();
    const char *nome = g_perc[0] ? g_perc : "(senza nome)";
    unsigned int r = 0, c = 0;

    if (g_avviso[0]) {
        sprintf(s, "%s  -  %s", nome, g_avviso);
    } else if (g_rtf) {
        static const char *const AL[4] = { "a sinistra", "centrato", "a destra", "giustificato" };
        const ExRtfDoc *d = g_vista.doc;
        unsigned int p = exrtf_paragrafo(d, g_vista.cur), a = exrtf_vista_allineamento(&g_vista);

        sprintf(s, "%s%s  -  RTF  -  paragrafo %u/%u, %s%s",
                g_vista.modificato ? "*" : "", nome, p + 1, d->par_n, AL[a < 4 ? a : 0],
                g_parziale ? "  [PARZIALE: non si salva]" : "");
    } else {
        ex_area_cursore(g_area, &r, &c);
        sprintf(s, "%s%s  -  riga %u/%u  col %u%s",
                ex_area_modificato(g_area) ? "*" : "", nome,
                r, ex_area_righe(g_area), c,
                g_parziale ? "  [PARZIALE: non si salva]" : "");
    }

    ex_testo_metti(g_stato, s);
}

/* =============================================================================
 * LA BARRA DI SCORRIMENTO — chiesta il 23 settembre 2026
 *
 * Senza, un testo piu' lungo della finestra si scorreva solo muovendoci
 * dentro il cursore, e niente diceva quanto fosse lungo ne' in che punto si
 * stesse guardando.
 *
 * ! LA BARRA SEGUE L'AREA, E NON IL CONTRARIO, TRANNE QUANDO LA SI TOCCA. Chi
 * sposta la vista di solito e' il cursore — un tasto, un Invio in fondo alla
 * pagina — e l'area lo sa gia' fare da se'. Quindi prima di ogni disegno la
 * barra si rimette dove l'area dice (barra_allinea); e quando e' la barra ad
 * essere trascinata, l'area parte da quella riga (ex_area_mostra_da) senza
 * spostare il cursore: il primo tasto riporta la vista dove si scrive, come
 * in ogni editor.
 * ============================================================================= */
static void barra_allinea(void)
{
    unsigned int vis = 0;
    unsigned int prima = ex_area_vista(g_area, &vis);
    unsigned int n = ex_area_righe(g_area);

    if (!g_barra) return;
    ex_scorri_limiti(g_barra, n > vis ? n - vis : 0, vis);
    ex_scorri_vai(g_barra, prima);
}

static void ridisegna(void)
{
    stato_aggiorna();
    barra_allinea();
    ex_procedura_base(g_f, EXM_DISEGNA, 0, 0);
    vista_disegna_se();
    ex_aggiorna(g_f);
}

/* ! UNA SOLA FUNZIONE PER USCIRE, chiamata da tre parti — il menu, Ctrl+Q e il
 * pulsante di chiusura. Erano tre copie della stessa domanda, e tre copie di
 * una domanda che difende il lavoro di qualcuno divergono alla prima modifica:
 * ne resterebbe una che non chiede piu' niente, e nessuno se ne accorgerebbe
 * finche' non perde un testo. */
static int doc_modificati(void);

static void esci_se_si_puo(void)
{
    int  n = doc_modificati();
    char q[96];

    if (n == 1) strcpy(q, "Un file e' cambiato. Uscire senza salvare?");
    else        snprintf(q, sizeof(q), "%d file sono cambiati. Uscire senza salvarli?", n);
    if (n && !ex_dlg_conferma("Modifiche non salvate", q, "Esci", "Torna al testo")) {
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
                  "si copia qui e si incolla in un altro editor.  "
                  "Ctrl+F cerca, F3 la successiva, Shift+F3 la precedente; "
                  "Ctrl+H sostituisce.  Un file .rtf si apre in modalita' RTF "
                  "(anche File > Nuovo documento RTF): Ctrl+B, Ctrl+I, Ctrl+U e "
                  "la barra sopra il testo cambiano il carattere; si salva in "
                  "RTF, e WordPad o LibreOffice lo aprono.");
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

static void area_segna_modificata(void);
static void area_segna_modificata_presto(void) { area_segna_modificata(); }

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
    /* il testo rimesso non e' quello del file: e' modificato */
    area_segna_modificata_presto();
    return 1;
}

/* La pila degli annullamenti di un file che si chiude o si ricarica. */
static void passo_tutti_via(void)
{
    while (g_passi_n > 0) passo_scarta_vecchio();
    g_passi_byte = 0;
}

/* =============================================================================
 * PIU' FILE, UNO PER SCHEDA (29 settembre 2026, @EDIT-SCHEDE)
 *
 * ! UN'AREA SOLA, E I DOCUMENTI CHE CI ENTRANO A TURNO. Il toolkit tiene due
 * aree di testo per programma (AREA_MAX), e ognuna vuole il suo mezzo mega:
 * un'area per file non ci starebbe. Quindi la scheda scelta sta nell'area, e
 * le altre aspettano in memoria col loro testo, il percorso, il cursore, la
 * vista, il segno «modificato» e la loro pila di annullamenti. Cambiare
 * scheda e' scambiare: la scheda che lascia si mette via (doc_metti_via), quella
 * che arriva si rimette nell'area (doc_riprendi).
 *
 * ! LE VARIABILI DI PRIMA (g_perc, g_parziale, g_passi) SONO QUELLE DELLA
 * SCHEDA SCELTA, e il resto del programma non e' cambiato: salva, cerca,
 * annulla lavorano come prima, sul file che si vede.
 * ============================================================================= */
#define DOC_MAX 12

typedef struct {
    int          usato;
    char         perc[PERC_MAX];
    int          parziale;
    char        *testo;             /* le righe unite da '\n', mentre aspetta */
    int          modificato;
    unsigned int riga, col, vista;
    Passo        passi[ANNULLA_MAX];
    int          passi_n;
    unsigned int passi_byte;
    /* A rich text document (@RTF) keeps its text in its own buffers the whole
     * time, and while it waits only the view's place is kept. */
    int          rtf;
    ExRtfDoc     rd;
    unsigned int v_cur, v_anc, v_prima;
    ExRtfStile   v_stile;
} Doc;

static Doc g_doc[DOC_MAX];
static int g_ndoc = 0;              /* quante schede, nell'ordine della barra */
static int g_attivo = 0;            /* quale, fra 0 e g_ndoc - 1 */

/* =============================================================================
 * RTF MODE (@RTF stage 3, 30 September 2026)
 *
 * ! A TAB IS TEXT OR RICH TEXT, and it is decided when it opens: a file that
 * begins with "{\rtf", or a new one whose name ends in .rtf, is rich text;
 * anything else is text as always. File > "Nuovo documento RTF" makes an
 * empty one. The text area and the view never show together: the chosen tab
 * decides which is visible, each with its bar (the area's scroll bar, or the
 * format bar).
 *
 * ! ONE VIEW, MANY DOCUMENTS, as with the area: g_vista is pointed at the
 * chosen tab's document, and a tab that is left keeps only where its caret
 * and its first line were (vista_metti_via). Its text never moves: every rich
 * text tab has its own buffers. The lines are laid out again on return - they
 * depend on the window's width anyway.
 *
 * ! WHAT IS NOT THERE YET, declared: undo, find and replace; pictures, tables
 * and lists in a file that is read (their text comes, the rest does not - see
 * lib/exrtf/exrtf.h). Typing is ASCII, as in the text area: the keyboard
 * service sends a window nothing else.
 * ============================================================================= */
#define RTF_TESTO    (256u * 1024u)
#define RTF_PEZZI    8192u
#define RTF_PAR      8192u
#define RTF_RIGHE    16384u
#define RTF_BARRA_H  26

static ExRtfRiga *g_righe = 0;
static ExFinestra g_rg, g_rc, g_rs, g_rfam, g_rcorpo, g_rcol;
static ExFinestra g_rsin, g_rcen, g_rdes, g_rgiu;
static int        g_mod_visto = -1;     /* the "modified" the tab title shows */

static const unsigned int CORPI[] = { 8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 36, 48, 72 };
#define CORPI_N   (sizeof(CORPI) / sizeof(CORPI[0]))
static const char *const COLORE_NOME[] = {
    "Nero", "Rosso", "Verde", "Blu", "Arancio", "Viola", "Grigio", "Bordeaux"
};
static const unsigned int COLORE[] = {
    0x000000, 0xC00000, 0x008000, 0x0000C0, 0xE07000, 0x800080, 0x808080, 0x800000
};
#define COLORI_N  (sizeof(COLORE) / sizeof(COLORE[0]))

static int doc_nuovo(void);

static int rtf_buffer(Doc *D)
{
    char          *t = (char *)malloc(RTF_TESTO);
    ExRtfPezzo    *p = (ExRtfPezzo *)malloc(RTF_PEZZI * sizeof(ExRtfPezzo));
    unsigned char *a = (unsigned char *)malloc(RTF_PAR);

    if (!g_righe) g_righe = (ExRtfRiga *)malloc(RTF_RIGHE * sizeof(ExRtfRiga));
    if (!t || !p || !a || !g_righe) {
        free(t); free(p); free(a);
        return 0;
    }
    exrtf_prepara(&D->rd, t, RTF_TESTO, p, RTF_PEZZI, a, RTF_PAR);
    return 1;
}

static void rtf_libera(Doc *D)
{
    if (!D->rtf) return;
    free(D->rd.testo);
    free(D->rd.pezzi);
    free(D->rd.allinea);
    memset(&D->rd, 0, sizeof(D->rd));
    D->rtf = 0;
}

/* Where the view goes: under the format bar, down to the status line. */
static void vista_rett(int *x, int *y, int *w, int *h)
{
    *x = AREA_X;
    *y = AREA_Y + RTF_BARRA_H;
    *w = g_fw - AREA_X * 2;
    *h = g_fh - *y - BASSO;
    if (*w < 60) *w = 60;
    if (*h < 30) *h = 30;
}

static void vista_collega(int i)
{
    Doc *D = &g_doc[i];
    int  x, y, w, h;

    exrtf_vista_prepara(&g_vista, &D->rd, g_righe, RTF_RIGHE);
    g_vista.stile      = D->v_stile;
    g_vista.modificato = D->modificato;
    vista_rett(&x, &y, &w, &h);
    exrtf_vista_posto(&g_vista, x, y, w, h);
    g_vista.cur   = D->v_cur <= D->rd.testo_n ? D->v_cur : D->rd.testo_n;
    g_vista.anc   = D->v_anc <= D->rd.testo_n ? D->v_anc : D->rd.testo_n;
    g_vista.prima = D->v_prima < g_vista.righe_n ? D->v_prima : 0;
    g_mod_visto   = -1;
}

static void vista_metti_via(Doc *D)
{
    D->v_cur      = g_vista.cur;
    D->v_anc      = g_vista.anc;
    D->v_prima    = g_vista.prima;
    D->v_stile    = g_vista.stile;
    D->modificato = g_vista.modificato;
}

/* The format bar shows the style under the caret. 1 if something changed
 * (then the window is redrawn whole: the bar's controls draw only that way). */
static int barra_rtf_segui(void)
{
    const ExRtfStile *s;
    unsigned int      i, k = 0;
    int               cambiato = 0;

    if (!g_rtf || !g_rg) return 0;
    s = exrtf_vista_stile(&g_vista);
    if (ex_acceso(g_rg) != (int)s->grassetto)    { ex_accendi(g_rg, s->grassetto);    cambiato = 1; }
    if (ex_acceso(g_rc) != (int)s->corsivo)      { ex_accendi(g_rc, s->corsivo);      cambiato = 1; }
    if (ex_acceso(g_rs) != (int)s->sottolineato) { ex_accendi(g_rs, s->sottolineato); cambiato = 1; }
    if (ex_voce_scelta(g_rfam) != s->famiglia)   { ex_voce_scegli(g_rfam, s->famiglia); cambiato = 1; }
    for (i = 1; i < CORPI_N; i++) {
        unsigned int di = CORPI[i] > s->corpo ? CORPI[i] - s->corpo : s->corpo - CORPI[i];
        unsigned int dk = CORPI[k] > s->corpo ? CORPI[k] - s->corpo : s->corpo - CORPI[k];
        if (di < dk) k = i;
    }
    if (ex_voce_scelta(g_rcorpo) != k) { ex_voce_scegli(g_rcorpo, k); cambiato = 1; }
    for (i = 0; i < COLORI_N && COLORE[i] != s->colore; i++) ;
    if (i < COLORI_N && ex_voce_scelta(g_rcol) != i) { ex_voce_scegli(g_rcol, i); cambiato = 1; }
    return cambiato;
}

/* Text area or view, and their bars; the keys go with them. */
static void modo_mostra(void)
{
    ExFinestra barra[10];
    int        i;

    barra[0] = g_rg;   barra[1] = g_rc;     barra[2] = g_rs;
    barra[3] = g_rfam; barra[4] = g_rcorpo; barra[5] = g_rcol;
    barra[6] = g_rsin; barra[7] = g_rcen;   barra[8] = g_rdes; barra[9] = g_rgiu;
    ex_mostra(g_area, !g_rtf);
    if (g_barra) ex_mostra(g_barra, !g_rtf);
    for (i = 0; i < 10; i++) if (barra[i]) ex_mostra(barra[i], g_rtf);
    /* ! THE HIDDEN AREA MUST LOSE THE FOCUS, or the keys would go on being
     * typed into a text nobody sees. With no control focused they come to
     * this program, which gives them to the view. */
    ex_tab_contenuto(g_f, g_rtf);           /* Tab is a letter of the text */
    if (g_rtf) { ex_fuoco_via(g_f); barra_rtf_segui(); }
    else       ex_fuoco(g_area);
}

static void vista_disegna_se(void)
{
    if (!g_rtf || !g_vista.doc) return;
    g_vista.fuoco = (ex_fuoco_chi(g_f) == 0);
    exrtf_vista_disegna(&g_vista, g_f);
}

static void stato_aggiorna(void);
static void ridisegna(void);

/* After the view changed: only the view and the status line, unless the
 * format bar or the tab's asterisk changed too. No flash of the window's grey
 * under the text at every key. */
static long vista_aggiorna(void)
{
    if (barra_rtf_segui() || g_mod_visto != g_vista.modificato) {
        g_mod_visto = g_vista.modificato;
        ridisegna();
        return EX_NON_RIDISEGNARE;
    }
    stato_aggiorna();
    vista_disegna_se();
    ex_ridisegna(g_stato);
    ex_aggiorna(g_f);
    return EX_NON_RIDISEGNARE;
}

/* Tab i becomes an empty rich text document (the view shows it). */
static int rtf_diventa(int i)
{
    Doc *D = &g_doc[i];

    if (!D->rtf) {
        if (!rtf_buffer(D)) {
            strcpy(g_avviso, "memoria finita: il documento RTF non si apre");
            return 0;
        }
        D->rtf = 1;
    } else {
        exrtf_prepara(&D->rd, D->rd.testo, D->rd.testo_max, D->rd.pezzi, D->rd.pezzi_max,
                      D->rd.allinea, D->rd.par_max);
    }
    exrtf_stile_base(&D->v_stile);
    D->v_cur = D->v_anc = D->v_prima = 0;
    D->modificato = 0;
    passo_tutti_via();
    ex_area_svuota(g_area);
    ex_area_pulita(g_area);
    g_rtf = 1;
    vista_collega(i);
    modo_mostra();
    return 1;
}

static void rtf_nuovo_doc(void)
{
    if (!doc_nuovo()) return;
    if (rtf_diventa(g_attivo)) strcpy(g_avviso, "documento RTF nuovo");
}

/* A file that begins "{\rtf" is rich text; one that is not there yet is, if
 * its name ends in .rtf. */
static int e_file_rtf(const char *perc)
{
    char         b[16];
    int          fd = open(perc, O_RDONLY, 0), n;
    unsigned int l = (unsigned int)strlen(perc);

    if (fd >= 0) {
        n = (int)read(fd, b, sizeof(b));
        close(fd);
        return n > 0 && exrtf_e_rtf(b, (unsigned int)n);
    }
    return l > 4 && perc[l - 4] == '.' &&
           (perc[l - 3] | 32) == 'r' && (perc[l - 2] | 32) == 't' && (perc[l - 1] | 32) == 'f';
}

/* Reads the chosen tab's file into its document. 0: not there (a new one). */
static int rtf_carica(const char *perc)
{
    Doc         *D = &g_doc[g_attivo];
    int          fd = open(perc, O_RDONLY, 0);
    long         n;
    unsigned int k = 0;
    char        *b;

    if (fd < 0) return 0;
    n = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (n < 0) n = 0;
    b = (char *)malloc((unsigned int)n + 1);
    if (!b) {
        close(fd);
        strcpy(g_avviso, "memoria finita: il file non si legge");
        g_parziale = 1;
        return 1;
    }
    while (k < (unsigned int)n) {
        int r = (int)read(fd, b + k, (unsigned int)n - k);
        if (r <= 0) break;
        k += (unsigned int)r;
    }
    close(fd);

    exrtf_prepara(&D->rd, D->rd.testo, D->rd.testo_max, D->rd.pezzi, D->rd.pezzi_max,
                  D->rd.allinea, D->rd.par_max);
    exrtf_leggi(&D->rd, b, k);
    free(b);
    /* ! READ IN PART, NOT SAVED: the same rule as the text area. Saving what
     * fit would cut the rest of the user's document without showing it. */
    g_parziale = D->rd.troncato || k < (unsigned int)n;
    exrtf_vista_nuovo(&g_vista);
    barra_rtf_segui();              /* the bar shows the first letter's style */
    g_mod_visto = -1;
    return 1;
}

static int rtf_salva(void)
{
    Doc         *D = &g_doc[g_attivo];
    unsigned int max = D->rd.testo_n * 16 + D->rd.pezzi_n * 64 + 8192, n;
    char        *b = (char *)malloc(max);
    int          fd;

    if (!b) { strcpy(g_avviso, "memoria finita: non salvato"); return 0; }
    n = exrtf_scrivi(&D->rd, b, max);
    if (!n) { free(b); strcpy(g_avviso, "non salvato: l'RTF non entra nella memoria"); return 0; }
    fd = open(g_perc, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { free(b); sprintf(g_avviso, "non riesco a scrivere %s", g_perc); return 0; }
    if (write(fd, b, n) != (ssize_t)n) {
        close(fd);
        free(b);
        strcpy(g_avviso, "scrittura interrotta: il file e' incompleto");
        return 0;
    }
    close(fd);
    free(b);
    g_vista.modificato = 0;
    sprintf(g_avviso, "salvato: RTF, %u byte", n);
    return 1;
}

/* A command of the format bar or of the Formato menu. */
static void formato(unsigned int id, long lp)
{
    if (!g_rtf) {
        strcpy(g_avviso, "il formato c'e' nei documenti RTF (File > Nuovo documento RTF)");
        return;
    }
    switch (id) {
    case ID_R_G: exrtf_vista_cambia(&g_vista, EXRTF_C_GRASSETTO, (unsigned int)ex_acceso(g_rg)); break;
    case ID_R_C: exrtf_vista_cambia(&g_vista, EXRTF_C_CORSIVO, (unsigned int)ex_acceso(g_rc)); break;
    case ID_R_S: exrtf_vista_cambia(&g_vista, EXRTF_C_SOTTOLINEATO, (unsigned int)ex_acceso(g_rs)); break;
    case ID_M_G: exrtf_vista_cambia(&g_vista, EXRTF_C_GRASSETTO, EXRTF_INVERTI); break;
    case ID_M_C: exrtf_vista_cambia(&g_vista, EXRTF_C_CORSIVO, EXRTF_INVERTI); break;
    case ID_M_S: exrtf_vista_cambia(&g_vista, EXRTF_C_SOTTOLINEATO, EXRTF_INVERTI); break;
    case ID_R_FAM:
        if (lp >= 0 && lp <= EXRTF_MONO) exrtf_vista_cambia(&g_vista, EXRTF_C_FAMIGLIA, (unsigned int)lp);
        break;
    case ID_R_CORPO:
        if (lp >= 0 && (unsigned long)lp < CORPI_N)
            exrtf_vista_cambia(&g_vista, EXRTF_C_CORPO, CORPI[lp]);
        break;
    case ID_R_COLORE:
        if (lp >= 0 && (unsigned long)lp < COLORI_N)
            exrtf_vista_cambia(&g_vista, EXRTF_C_COLORE, COLORE[lp]);
        break;
    case ID_R_SIN: case ID_R_CEN: case ID_R_DES: case ID_R_GIU:
        exrtf_vista_allinea(&g_vista, id - ID_R_SIN);
        break;
    default: break;
    }
    /* ! THE KEYS GO BACK TO THE TEXT: a combo or a switch chosen with the mouse
     * took the focus, and the next letter typed would go to it. */
    ex_fuoco_via(g_f);
    barra_rtf_segui();
}

/* The format bar: B, I, U as switches (they show the style under the caret),
 * typeface, size, colour, and the four alignments. Hidden until a rich text
 * tab is chosen. */
static void barra_rtf_crea(void)
{
    int y = AREA_Y + 2, i;

    g_rg = ex_crea("spunta", "G", EX_FIGLIO, 4, y, 34, 20, g_f, ID_R_G, 0);
    g_rc = ex_crea("spunta", "C", EX_FIGLIO, 40, y, 34, 20, g_f, ID_R_C, 0);
    g_rs = ex_crea("spunta", "S", EX_FIGLIO, 76, y, 34, 20, g_f, ID_R_S, 0);
    g_rfam = ex_crea("combo", "", EX_FIGLIO, 116, y, 76, 20, g_f, ID_R_FAM, 0);
    ex_voce_aggiungi(g_rfam, "Serif");
    ex_voce_aggiungi(g_rfam, "Sans");
    ex_voce_aggiungi(g_rfam, "Mono");
    g_rcorpo = ex_crea("combo", "", EX_FIGLIO, 196, y, 52, 20, g_f, ID_R_CORPO, 0);
    for (i = 0; i < (int)CORPI_N; i++) {
        char t[8];
        snprintf(t, sizeof(t), "%u", CORPI[i]);
        ex_voce_aggiungi(g_rcorpo, t);
    }
    g_rcol = ex_crea("combo", "", EX_FIGLIO, 252, y, 92, 20, g_f, ID_R_COLORE, 0);
    for (i = 0; i < (int)COLORI_N; i++) ex_voce_aggiungi(g_rcol, COLORE_NOME[i]);
    g_rsin = ex_crea("pulsante", "Sin", EX_FIGLIO, 350, y, 40, 20, g_f, ID_R_SIN, 0);
    g_rcen = ex_crea("pulsante", "Cen", EX_FIGLIO, 392, y, 40, 20, g_f, ID_R_CEN, 0);
    g_rdes = ex_crea("pulsante", "Des", EX_FIGLIO, 434, y, 40, 20, g_f, ID_R_DES, 0);
    g_rgiu = ex_crea("pulsante", "Giu", EX_FIGLIO, 476, y, 40, 20, g_f, ID_R_GIU, 0);
    ex_voce_scegli(g_rfam, 0);
    ex_voce_scegli(g_rcorpo, 4);                /* 12 */
    ex_voce_scegli(g_rcol, 0);
}

static const char *base_nome(const char *p)
{
    const char *u = strrchr(p, '/');
    return (u && u[1]) ? u + 1 : p;
}

/* Il titolo di una scheda: il nome del file, con * se e' modificato. */
static void doc_titolo(int i, int modificato)
{
    char t[40];

    snprintf(t, sizeof(t), "%s%s", modificato ? "*" : "",
             g_doc[i].perc[0] ? base_nome(g_doc[i].perc) : "senza nome");
    if (strcmp(t, ex_voce_testo(g_schede, (unsigned int)i)) != 0) {
        ex_voce_rinomina(g_schede, (unsigned int)i, t);
    }
}

/* Il titolo della scheda scelta segue il file e il suo asterisco. */
static void doc_segui_titolo(void)
{
    strncpy(g_doc[g_attivo].perc, g_perc, PERC_MAX - 1);
    g_doc[g_attivo].perc[PERC_MAX - 1] = '\0';
    doc_titolo(g_attivo, g_rtf ? g_vista.modificato : ex_area_modificato(g_area));
}

/* La scheda scelta lascia l'area: tutto quel che serve per riaverla. */
static void vista_metti_via(Doc *D);

static int doc_metti_via(void)
{
    Doc          *D = &g_doc[g_attivo];
    unsigned int  byte = 0;
    char         *t;

    if (g_rtf) {                    /* its text never leaves its buffers */
        strncpy(D->perc, g_perc, PERC_MAX - 1);
        D->perc[PERC_MAX - 1] = '\0';
        D->parziale = g_parziale;
        vista_metti_via(D);
        doc_titolo(g_attivo, D->modificato);
        return 1;
    }
    t = testo_di_adesso(&byte);

    /* ! SENZA MEMORIA PER IL TESTO LA SCHEDA NON SI LASCIA: rimetterla
     * dopo darebbe una scheda vuota, cioe' il file perso senza una parola. */
    if (!t) {
        strcpy(g_avviso, "memoria finita: resto su questa scheda");
        return 0;
    }

    strncpy(D->perc, g_perc, PERC_MAX - 1);
    D->perc[PERC_MAX - 1] = '\0';
    D->parziale   = g_parziale;
    D->modificato = ex_area_modificato(g_area);
    ex_area_cursore(g_area, &D->riga, &D->col);
    D->vista      = ex_area_vista(g_area, 0);
    D->testo      = t;
    memcpy(D->passi, g_passi, sizeof(g_passi));
    D->passi_n    = g_passi_n;
    D->passi_byte = g_passi_byte;
    memset(g_passi, 0, sizeof(g_passi));
    g_passi_n = 0;
    g_passi_byte = 0;
    /* il titolo si aggiorna anche qui: una scheda aperta e subito lasciata
     * (edit a b c) non passa mai dalla riga di stato */
    doc_titolo(g_attivo, D->modificato);
    return 1;
}

/* Rimette nell'area il testo t (righe unite da '\n'). */
static void area_da_testo(const char *t)
{
    char         riga[512];
    unsigned int i = 0, a;

    ex_area_svuota(g_area);
    while (t && t[i]) {
        a = 0;
        while (t[i] && t[i] != '\n' && a < sizeof(riga) - 1) riga[a++] = t[i++];
        riga[a] = '\0';
        ex_area_aggiungi(g_area, riga);
        if (t[i] == '\n') i++;
    }
}

/* Il testo e' cambiato rispetto al file: l'area lo deve sapere. Si
 * riscrive la prima riga uguale a se stessa, che accende il segno. */
static void area_segna_modificata(void)
{
    char r[512];

    strncpy(r, ex_area_riga(g_area, 0), sizeof(r) - 1);
    r[sizeof(r) - 1] = '\0';
    ex_area_riga_metti(g_area, 0, r);
}

/* La scheda i torna nell'area. */
static void vista_collega(int i);
static void modo_mostra(void);

static void doc_riprendi(int i)
{
    Doc *D = &g_doc[i];

    g_attivo = i;
    if (D->rtf) {
        g_rtf = 1;
        strncpy(g_perc, D->perc, PERC_MAX - 1);
        g_perc[PERC_MAX - 1] = '\0';
        g_parziale = D->parziale;
        vista_collega(i);
        ex_voce_scegli(g_schede, (unsigned int)i);
        modo_mostra();
        return;
    }
    g_rtf = 0;
    area_da_testo(D->testo);
    if (D->testo) { free(D->testo); D->testo = 0; }
    ex_area_pulita(g_area);
    if (D->modificato) area_segna_modificata();
    ex_area_vai(g_area, D->riga ? D->riga - 1 : 0, D->col ? D->col - 1 : 0);
    ex_area_mostra_da(g_area, D->vista);
    strncpy(g_perc, D->perc, PERC_MAX - 1);
    g_perc[PERC_MAX - 1] = '\0';
    g_parziale = D->parziale;
    memcpy(g_passi, D->passi, sizeof(g_passi));
    g_passi_n    = D->passi_n;
    g_passi_byte = D->passi_byte;
    ex_voce_scegli(g_schede, (unsigned int)i);
    modo_mostra();
}

static void doc_scegli(int i)
{
    if (i == g_attivo || i < 0 || i >= g_ndoc) return;
    if (!doc_metti_via()) { ex_voce_scegli(g_schede, (unsigned int)g_attivo); return; }
    doc_riprendi(i);
    g_avviso[0] = '\0';
}

/* Una scheda nuova, vuota, in fondo, e scelta. 0 se non ci sta. */
static int doc_nuovo(void)
{
    Doc *D;

    if (g_ndoc >= DOC_MAX || !ex_voce_aggiungi(g_schede, "senza nome")) {
        sprintf(g_avviso, "al massimo %d file aperti: chiudine uno", DOC_MAX);
        return 0;
    }
    if (g_ndoc > 0 && !doc_metti_via()) {
        ex_voce_togli(g_schede, (unsigned int)g_ndoc);
        return 0;
    }
    D = &g_doc[g_ndoc];
    memset(D, 0, sizeof(*D));
    D->usato = 1;
    g_attivo = g_ndoc++;
    ex_area_svuota(g_area);
    ex_area_pulita(g_area);
    g_perc[0] = '\0';
    g_parziale = 0;
    g_rtf = 0;
    ex_voce_scegli(g_schede, (unsigned int)g_attivo);
    modo_mostra();
    return 1;
}

/* La scheda scelta e' vuota, senza nome e intatta: un file aperto la prende
 * invece di aggiungerne un'altra accanto (l'editor aperto senza file). */
static int doc_vergine(void)
{
    if (g_rtf) return 0;
    return g_perc[0] == '\0' && !ex_area_modificato(g_area) &&
           ex_area_righe(g_area) <= 1 && ex_area_riga(g_area, 0)[0] == '\0';
}

/* Apre `perc` in una scheda: quella dove e' gia' aperto, se c'e'. */
static void doc_apri(const char *perc)
{
    int i;

    for (i = 0; i < g_ndoc; i++) {
        const char *p = (i == g_attivo) ? g_perc : g_doc[i].perc;
        if (p[0] && strcmp(p, perc) == 0) {
            doc_scegli(i);
            sprintf(g_avviso, "era gia' aperto");
            return;
        }
    }
    if (!doc_vergine() && !doc_nuovo()) return;
    strncpy(g_perc, perc, PERC_MAX - 1);
    g_perc[PERC_MAX - 1] = '\0';
    if (e_file_rtf(g_perc)) {
        if (!rtf_diventa(g_attivo)) return;         /* it said why */
        if (rtf_carica(g_perc))
            sprintf(g_avviso, "aperto: documento RTF, %u paragrafi%s", g_vista.doc->par_n,
                    g_parziale ? ", SOLO IN PARTE" : "");
        else
            strcpy(g_avviso, "non c'era: documento RTF nuovo");
        return;
    }
    if (carica(g_perc)) sprintf(g_avviso, "aperto: %u righe", ex_area_righe(g_area));
    else                strcpy(g_avviso, "non c'era: file nuovo");
    passo_tutti_via();
}

/* ! CHIUDERE UNA SCHEDA MODIFICATA CHIEDE, come uscire. L'ultima non sparisce:
 * resta una scheda vuota, come un editor appena aperto. */
static void doc_chiudi(int i)
{
    int k, mod;

    if (i < 0 || i >= g_ndoc) return;
    if (i != g_attivo) doc_scegli(i);
    if (i != g_attivo) return;                  /* non si e' potuta scegliere */
    mod = g_rtf ? g_vista.modificato : ex_area_modificato(g_area);
    if (mod) {
        char q[PERC_MAX + 64];
        snprintf(q, sizeof(q), "%s e' cambiato. Chiudere senza salvare?",
                 g_perc[0] ? base_nome(g_perc) : "Il testo senza nome");
        if (!ex_dlg_conferma("Modifiche non salvate", q, "Chiudi", "Annulla")) {
            strcpy(g_avviso, "non chiuso");
            return;
        }
    }
    passo_tutti_via();
    if (g_doc[i].rtf) rtf_libera(&g_doc[i]);
    g_rtf = 0;
    if (g_ndoc == 1) {                          /* l'ultima: resta vuota */
        modo_mostra();
        ex_area_svuota(g_area);
        ex_area_pulita(g_area);
        g_perc[0] = '\0';
        g_parziale = 0;
        strcpy(g_avviso, "chiuso");
        return;
    }
    for (k = i; k + 1 < g_ndoc; k++) g_doc[k] = g_doc[k + 1];
    memset(&g_doc[g_ndoc - 1], 0, sizeof(Doc));
    g_ndoc--;
    ex_voce_togli(g_schede, (unsigned int)i);
    /* la vicina di destra, o l'ultima */
    g_attivo = (i < g_ndoc) ? i : g_ndoc - 1;
    doc_riprendi(g_attivo);
    strcpy(g_avviso, "chiuso");
}

/* Quanti file modificati, contando anche le schede che aspettano. */
static int doc_modificati(void)
{
    int i, n = 0;

    for (i = 0; i < g_ndoc; i++)
        n += (i != g_attivo) ? g_doc[i].modificato :
             g_rtf ? g_vista.modificato : ex_area_modificato(g_area);
    return n;
}

static void informazioni(void)
{
    char t[640];

    exinfo_testo(t, sizeof(t), "ExEditor", VERSIONE_APP,
                 "L'editor di testo di EX-OS, sul toolkit ExWin.  Il testo, "
                 "il cursore e lo scorrimento sono del controllo areatesto, "
                 "e nei documenti RTF della vista di lib/exrtf; qui dentro "
                 "c'e' leggere un file, scriverlo e decidere cosa fare quando "
                 "va storto.");
    ex_dlg_avviso("Informazioni su", t);
}

/* =============================================================================
 * FIND AND REPLACE (@EDIT-CERCA, 26 September 2026)
 *
 * ! THE DECISIONS ARE gfedit'S, taken once already (bin/gfedit/gf_edit.c):
 * the next occurrence is the first STRICTLY after the cursor, or F3 would stay
 * on the same one; past the end the search starts again from the top and
 * says so; what was found stays selected, so a typed word replaces it. The
 * two editors answer the same way, which is the reason to copy decisions and
 * not code (the text lives in a different structure here).
 *
 * ! THE STATE SURVIVES THE DIALOG: the searched text, the replacement, and
 * «replacing one at a time». After «Una», F3 replaces the next one too, as
 * asked; a new Ctrl+F ends that mode.
 *
 * Case-sensitive, like gfedit. A line that would grow past the area's 200
 * columns is cut by the toolkit, like any other writing.
 * ============================================================================= */
#define AGO_MAX 80
static char g_ago[AGO_MAX] = "";
static char g_nuovo[AGO_MAX] = "";
static int  g_sost_una = 0;             /* F3 replaces, after «Una» */

/* ! THE CURSOR FROM ZERO. ex_area_cursore() counts from ONE — it feeds the
 * status line, «riga 1/1 col 1» — while ex_area_vai() and
 * ex_area_seleziona() count from zero. Mixing them sent every search one
 * line down: «Una» replaced nothing (tools/prova_edit_cerca.sh, 27 Sept). */
static void cursore0(unsigned int *r, unsigned int *c)
{
    ex_area_cursore(g_area, r, c);
    if (*r) (*r)--;
    if (*c) (*c)--;
}

/* The first occurrence at or after (r, c); 1 if found. */
static int trova_da(unsigned int r, unsigned int c, unsigned int *fr, unsigned int *fc)
{
    unsigned int n = ex_area_righe(g_area), k;

    for (k = r; k < n; k++) {
        const char *riga = ex_area_riga(g_area, k);
        const char *p;

        if (!riga) continue;
        if (k == r && c > strlen(riga)) continue;
        p = strstr(riga + (k == r ? c : 0), g_ago);
        if (p) { *fr = k; *fc = (unsigned int)(p - riga); return 1; }
    }
    return 0;
}

static int avanti(void)
{
    unsigned int r, c, fr, fc;

    if (!g_ago[0]) return 0;
    cursore0(&r, &c);
    if (!trova_da(r, c, &fr, &fc)) {
        if (!trova_da(0, 0, &fr, &fc)) {
            snprintf(g_avviso, sizeof(g_avviso), "'%s' non c'e'", g_ago);
            return 0;
        }
        strcpy(g_avviso, "ricerca ripresa dall'inizio");
    }
    ex_area_seleziona(g_area, fr, fc, fc + (unsigned int)strlen(g_ago));
    return 1;
}

static int indietro(void)
{
    unsigned int r, c, k, l = (unsigned int)strlen(g_ago);
    int          giro;

    if (!g_ago[0]) return 0;
    cursore0(&r, &c);

    /* The last occurrence that ENDS before the selection starts: the cursor
     * is at the end of what was found, so «before» is c - l on its line. */
    for (giro = 0; giro < 2; giro++) {
        unsigned int n = ex_area_righe(g_area);
        unsigned int da = giro ? n : r + 1;

        for (k = da; k-- > 0; ) {
            const char *riga = ex_area_riga(g_area, k), *p, *ultima = 0;

            if (!riga) continue;
            for (p = strstr(riga, g_ago); p; p = strstr(p + 1, g_ago)) {
                unsigned int col = (unsigned int)(p - riga);
                if (!giro && k == r && col + l >= c) break;
                ultima = p;
            }
            if (ultima) {
                unsigned int col = (unsigned int)(ultima - riga);
                if (giro) strcpy(g_avviso, "ricerca ripresa dalla fine");
                ex_area_seleziona(g_area, k, col, col + l);
                return 1;
            }
        }
    }
    snprintf(g_avviso, sizeof(g_avviso), "'%s' non c'e'", g_ago);
    return 0;
}

/* Replaces, in one line, the occurrence at column c (or every one, when
 * c < 0). Returns how many. */
static int sostituisci_in(unsigned int r, int c)
{
    const char  *riga = ex_area_riga(g_area, r);
    char         nuova[512];
    unsigned int l = (unsigned int)strlen(g_ago), n = 0, i = 0;
    int          fatte = 0, tutte = c < 0;

    if (!riga) return 0;
    while (riga[i] && n + 1 < sizeof(nuova)) {
        /* ! «ONLY THAT ONE» IS ITS OWN FLAG. It was c = -2 after the first
         * replacement — and c < 0 means «every one», so «Una» replaced the
         * whole line. Seen in tools/prova_edit_cerca.sh, 27 September 2026. */
        if (strncmp(riga + i, g_ago, l) == 0 &&
            (tutte || (fatte == 0 && (unsigned int)c == i))) {
            unsigned int k;
            for (k = 0; g_nuovo[k] && n + 1 < sizeof(nuova); k++) nuova[n++] = g_nuovo[k];
            i += l;
            fatte++;
            continue;
        }
        nuova[n++] = riga[i++];
    }
    nuova[n] = '\0';
    if (fatte) ex_area_riga_metti(g_area, r, nuova);
    return fatte;
}

static void cerca(void)
{
    if (!ex_dlg_chiedi("Cerca", "Testo da cercare:", "Cerca", g_ago, AGO_MAX)) return;
    g_sost_una = 0;
    avanti();
}

/* The occurrence under the selection, replaced; then the next one found. */
static void sostituisci_una(void)
{
    unsigned int r, c, l = (unsigned int)strlen(g_ago);
    const char  *riga;

    cursore0(&r, &c);
    riga = ex_area_riga(g_area, r);
    if (!riga || c < l || strncmp(riga + c - l, g_ago, l) != 0) {
        if (!avanti()) return;          /* nothing selected yet: find it */
        cursore0(&r, &c);
    }
    annulla_segna();
    sostituisci_in(r, (int)(c - l));
    ex_area_vai(g_area, r, c - l + (unsigned int)strlen(g_nuovo));
    if (avanti()) strcpy(g_avviso, "sostituita; F3 sostituisce la prossima");
    else          strcpy(g_avviso, "sostituita l'ultima");
}

static void sostituisci(void)
{
    static const char *const voci[3] = { "Tutte", "Una", "Annulla" };
    unsigned int n, k, quante = 0;
    char         t[200];
    int          r;

    if (!ex_dlg_chiedi("Sostituisci", "Testo da cercare:", "Avanti", g_ago, AGO_MAX)) return;
    if (!g_ago[0]) return;
    if (!ex_dlg_chiedi("Sostituisci", "Sostituire con:", "Avanti", g_nuovo, AGO_MAX)) return;

    n = ex_area_righe(g_area);
    for (k = 0; k < n; k++) {
        const char *riga = ex_area_riga(g_area, k), *p;
        if (!riga) continue;
        for (p = strstr(riga, g_ago); p; p = strstr(p + strlen(g_ago), g_ago)) quante++;
    }
    if (quante == 0) { snprintf(g_avviso, sizeof(g_avviso), "'%s' non c'e'", g_ago); return; }

    snprintf(t, sizeof(t), "Trovate %u occorrenze di '%s'.", quante, g_ago);
    r = ex_dlg_scegli("Sostituisci", t, voci, 3);

    if (r == 0) {
        unsigned int fatte = 0;
        annulla_segna();
        for (k = 0; k < n; k++) fatte += (unsigned int)sostituisci_in(k, -1);
        g_sost_una = 0;
        snprintf(g_avviso, sizeof(g_avviso), "sostituite %u occorrenze", fatte);
    } else if (r == 1) {
        g_sost_una = 1;
        sostituisci_una();
    }
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
        if (wp == ID_NUOVO)     { doc_nuovo();        break; }
        if (wp == ID_NUOVO_RTF) { rtf_nuovo_doc();    break; }
        if ((wp >= ID_R_G && wp <= ID_R_GIU) || (wp >= ID_M_G && wp <= ID_M_S)) {
            formato(wp, lp);
            break;
        }
        if (wp == ID_CHIUDI)    { doc_chiudi(g_attivo); break; }
        if (wp == ID_SCHEDE)    { doc_scegli((int)lp); break; }
        if (wp == ID_RICARICA)  {
            if (g_perc[0] && g_rtf) rtf_carica(g_perc);
            else if (g_perc[0]) { carica(g_perc); passo_tutti_via(); }
            else strcpy(g_avviso, "niente da ricaricare: non c'e' un file");
            break;
        }
        if (wp == ID_APRI)      { apri_con_dialogo(); break; }
        if (wp == ID_SALVACOME) { salva_come();       break; }
        if (wp == ID_ESCI)      { esci_se_si_puo();   break; }

        /* The edit menu on a rich text tab: the view does it. */
        if (g_rtf && (wp == ID_TAGLIA || wp == ID_COPIA || wp == ID_INCOLLA ||
                      wp == ID_CANCELLA || wp == ID_SELTUTTO)) {
            if (wp == ID_TAGLIA)   exrtf_vista_taglia(&g_vista);
            if (wp == ID_COPIA)    exrtf_vista_copia(&g_vista);
            if (wp == ID_INCOLLA)  exrtf_vista_incolla(&g_vista);
            if (wp == ID_CANCELLA && g_vista.cur != g_vista.anc) exrtf_vista_tasto(&g_vista, KBD_K_DEL);
            if (wp == ID_SELTUTTO) exrtf_vista_tutto(&g_vista);
            break;
        }
        if (g_rtf && (wp == ID_ANNULLA || wp == ID_CERCA || wp == ID_AVANTI ||
                      wp == ID_INDIETRO || wp == ID_SOSTITUISCI)) {
            strcpy(g_avviso, "nei documenti RTF annulla e cerca non ci sono ancora");
            break;
        }
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
        if (wp == ID_CERCA)      { cerca();       break; }
        if (wp == ID_AVANTI)     { if (g_sost_una) sostituisci_una(); else avanti(); break; }
        if (wp == ID_INDIETRO)   { indietro();    break; }
        if (wp == ID_SOSTITUISCI) { sostituisci(); break; }

        if (wp == ID_BARRA)      { ex_area_mostra_da(g_area, (unsigned int)lp); break; }
        if (wp == ID_ISTRUZIONI) { istruzioni();  break; }
        if (wp == ID_INFO)       { informazioni(); break; }
        return 0;

    case EXM_TASTO:
        /* ! QUI ARRIVANO SOLO LE SCORCIATOIE. Le lettere, le frecce, il
         * Backspace e l'Invio li ha gia' mangiati l'area di testo: se sono
         * arrivate fin qui, non erano per lei. */
        g_avviso[0] = '\0';
        c = wp & KBD_KEY_MASK;

        /* ! ON A RICH TEXT TAB THE KEYS ARE THE VIEW'S, but for the program's
         * own shortcuts: the view would take Ctrl+S as nothing. */
        if (g_rtf) {
            if (wp & KBD_MOD_CTRL) {
                if (c == 's' || c == 'S') { salva();            break; }
                if (c == 'q' || c == 'Q') { esci_se_si_puo();   break; }
                if (c == 'n' || c == 'N') { doc_nuovo();        break; }
                if (c == 'o' || c == 'O') { apri_con_dialogo(); break; }
                if (c == 'z' || c == 'Z' || c == 'f' || c == 'F' || c == 'h' || c == 'H') {
                    strcpy(g_avviso, "nei documenti RTF annulla e cerca non ci sono ancora");
                    break;
                }
            }
            if (exrtf_vista_tasto(&g_vista, wp)) return vista_aggiorna();
            return ex_procedura_base(f, msg, wp, lp);
        }

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
            if (c == 'n' || c == 'N') { doc_nuovo();           break; }
            if (c == 'o' || c == 'O') { apri_con_dialogo();    break; }
            if (c == 'f' || c == 'F') { cerca();               break; }
            if (c == 'h' || c == 'H') { sostituisci();         break; }
        }
        /* F3 and Shift+F3: the next and the previous. After «Una», F3 also
         * replaces — see sostituisci(). */
        if (c == KBD_K_F(3)) {
            if (wp & KBD_MOD_SHIFT) indietro();
            else if (g_sost_una)    sostituisci_una();
            else                    avanti();
            break;
        }
        return ex_procedura_base(f, msg, wp, lp);

    /* La X di una scheda, o Ctrl+W (@TOOLKIT-SCHEDE). */
    case EXM_SCHEDA_CHIUDI:
        g_avviso[0] = '\0';
        doc_chiudi((int)lp);
        break;

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

        g_fw = w;
        g_fh = h;
        if (g_rtf) {
            int vx, vy, vw, vh;
            vista_rett(&vx, &vy, &vw, &vh);
            exrtf_vista_posto(&g_vista, vx, vy, vw, vh);
        }

        ex_misura(g_area, w - AREA_X * 2 - BARRA_W, h - AREA_Y - BASSO);
        if (g_schede) ex_misura(g_schede, w - AREA_X * 2, SCHEDE_H);
        if (g_barra) {
            ex_sposta(g_barra, w - AREA_X - BARRA_W, AREA_Y);
            ex_misura(g_barra, BARRA_W, h - AREA_Y - BASSO);
        }
        ex_sposta(g_stato, 6, h - 22);
        ex_misura(g_stato, w - 12, 16);
        break;
    }

    /* ! IL DISEGNO CHE ARRIVA DAL TOOLKIT — dopo ogni tasto battuto
     * nell'area — passa di qui per rimettere la barra dove l'area e' andata;
     * senza, la barra resterebbe ferma mentre si scrive in fondo al testo. */
    case EXM_DISEGNA: {
        long r;

        /* ! E LA RIGA DI STATO CON LEI: prima di qui nessuno la rifaceva
         * dopo un tasto battuto nell'area, e «riga 1/512» restava scritto
         * mentre si scendeva di pagina in pagina. */
        stato_aggiorna();
        barra_allinea();
        r = ex_procedura_base(f, msg, wp, lp);
        /* The view over the grey the base painted; ex_aggiorna puts an open
         * menu or combo back on top of it. */
        if (g_rtf) { vista_disegna_se(); ex_aggiorna(g_f); }
        return r;
    }

    /* The mouse on the rich text view (@RTF). */
    case EXM_MOUSE_GIU:
        if (g_rtf && exrtf_vista_clic(&g_vista, EX_X(lp), EX_Y(lp), (wp & KBD_MOD_SHIFT) != 0)) {
            ex_fuoco_via(g_f);
            return vista_aggiorna();
        }
        return ex_procedura_base(f, msg, wp, lp);
    case EXM_MOUSE_MOSSO:
        if (g_rtf && exrtf_vista_trascina(&g_vista, EX_X(lp), EX_Y(lp))) return vista_aggiorna();
        return ex_procedura_base(f, msg, wp, lp);
    case EXM_MOUSE_SU:
        if (g_rtf) exrtf_vista_su(&g_vista);
        return ex_procedura_base(f, msg, wp, lp);
    case EXM_DOPPIOCLIC:
        if (g_rtf && exrtf_vista_doppio(&g_vista, EX_X(lp), EX_Y(lp))) return vista_aggiorna();
        return ex_procedura_base(f, msg, wp, lp);
    case EXM_ROTELLA:
        if (g_rtf) { exrtf_vista_rotella(&g_vista, (int)wp); return vista_aggiorna(); }
        return ex_procedura_base(f, msg, wp, lp);

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    ridisegna();
    return 0;
}

int main(int argc, char **argv)
{
    ExMsg m;


    /* ! EX_AUTO E EX_RIDIM, e sono due richieste diverse. EX_AUTO dice «mettila
     * tu», ed e' cio' che permette di aprire due editor senza che il secondo
     * finisca esattamente sopra il primo; EX_RIDIM dice che la finestra si puo'
     * tirare per l'angolo, e impegna a rispondere a EXM_MISURA. */
    g_f = ex_crea("finestra", "ExEditor",
                  EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                  EX_AUTO, EX_AUTO, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("exeditor: il server a finestre non risponde.\n");
        printf("      Avvialo con:  exwin\n");
        return 1;
    }

    /* ! I MENU HANNO PRESO IL POSTO DEI CINQUE PULSANTI, e non ci si e'
     * aggiunti accanto. Una fila di pulsanti che fa le stesse cose di un menu
     * e' due posti in cui leggere cosa sa fare il programma, e il secondo si
     * dimentica di crescere: «Taglia» non ci sarebbe mai finito. */
    g_menu = ex_menu(g_f);
    ex_menu_voce(g_menu, "File", "Nuovo\tCtrl+N",    ID_NUOVO);
    ex_menu_voce(g_menu, "File", "Nuovo documento RTF", ID_NUOVO_RTF);
    ex_menu_voce(g_menu, "File", "Apri...\tCtrl+O",  ID_APRI);
    ex_menu_voce(g_menu, "File", "Chiudi\tCtrl+W",   ID_CHIUDI);
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
    ex_menu_voce(g_menu, "Modifica", "-",               0);
    ex_menu_voce(g_menu, "Modifica", "Cerca...\tCtrl+F", ID_CERCA);
    ex_menu_voce(g_menu, "Modifica", "Trova successivo\tF3", ID_AVANTI);
    ex_menu_voce(g_menu, "Modifica", "Trova precedente\tShift+F3", ID_INDIETRO);
    ex_menu_voce(g_menu, "Modifica", "Sostituisci...\tCtrl+H", ID_SOSTITUISCI);

    ex_menu_voce(g_menu, "Formato", "Grassetto\tCtrl+B",    ID_M_G);
    ex_menu_voce(g_menu, "Formato", "Corsivo\tCtrl+I",      ID_M_C);
    ex_menu_voce(g_menu, "Formato", "Sottolineato\tCtrl+U", ID_M_S);
    ex_menu_voce(g_menu, "Formato", "-",                    0);
    ex_menu_voce(g_menu, "Formato", "Allinea a sinistra",   ID_R_SIN);
    ex_menu_voce(g_menu, "Formato", "Centra",               ID_R_CEN);
    ex_menu_voce(g_menu, "Formato", "Allinea a destra",     ID_R_DES);
    ex_menu_voce(g_menu, "Formato", "Giustifica",           ID_R_GIU);

    ex_menu_voce(g_menu, "Info", "Istruzioni",      ID_ISTRUZIONI);
    ex_menu_voce(g_menu, "Info", "Informazioni su", ID_INFO);

    /* Le schede: una per file, anche quando il file e' uno solo. */
    g_schede = ex_crea("tab", "", EX_FIGLIO, AREA_X, MENU_H + 2,
                       FIN_W - AREA_X * 2, SCHEDE_H, g_f, ID_SCHEDE, 0);
    ex_voci_schede(g_schede, 1);

    g_area = ex_crea("areatesto", "", EX_FIGLIO,
                     AREA_X, AREA_Y, FIN_W - AREA_X * 2 - BARRA_W,
                     FIN_H - AREA_Y - BASSO, g_f, 0, 0);
    if (!g_area) {
        printf("exeditor: non riesco a creare l'area di testo\n");
        return 1;
    }

    /* Piu' alta che larga: il toolkit la fa verticale da se'. */
    g_barra = ex_crea("scorrimento", "", EX_FIGLIO,
                      FIN_W - AREA_X - BARRA_W, AREA_Y, BARRA_W,
                      FIN_H - AREA_Y - BASSO, g_f, ID_BARRA, 0);

    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      6, FIN_H - 22, FIN_W - 12, 16, g_f, 0, 0);

    /* The rich text tabs' format bar, hidden while a text tab is chosen. */
    barra_rtf_crea();

    /* ! IL FUOCO ALL'AREA, ESPLICITAMENTE: e' l'unico controllo della finestra
     * che i tasti se li merita. La barra dei menu il fuoco non lo prende — un
     * menu che tenesse la tastiera renderebbe muta l'area — e risponde solo a
     * F10, che a menu chiuso non serve a nessun altro. */
    ex_fuoco(g_area);

    /* ! OGNI ARGOMENTO E' UN FILE, ognuno nella sua scheda: `edit a.c b.h`. */
    doc_nuovo();
    {
        int i;
        for (i = 1; i < argc; i++) {
            doc_apri(argv[i]);
            if (g_rtf)
                printf("exeditor: %s, RTF, %u paragrafi%s\n", g_perc, g_vista.doc->par_n,
                       g_parziale ? " (PARZIALE)" : "");
            else
                printf("exeditor: %s, %u righe%s\n", g_perc, ex_area_righe(g_area),
                       g_parziale ? " (PARZIALE)" : "");
        }
        if (argc < 2) printf("exeditor: file nuovo, senza nome\n");
        if (argc > 2) doc_scegli(0);
    }
    g_avviso[0] = '\0';

    ridisegna();

    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

/* =============================================================================
 * QUELLO CHE MANCA, DICHIARATO
 *
 * ! L'ANNULLAMENTO C'E' SOLO PER I COMANDI (taglia, incolla, cancella) nei
 * file di testo, e non c'e' nei documenti RTF, dove mancano anche Cerca e
 * Sostituisci: vedi RTF MODE.
 *
 * ! E GLI APPUNTI SONO SOLO TESTO. Una zona condivisa da 4 KB con dentro dei
 * byte: chi copia mille righe ne ritrova quante ce ne stanno. Un servizio
 * degli appunti con piu' formati e senza tetto e' un'altra cosa, e la vorra'
 * il giorno che qualcosa che non sia testo avra' da copiare.
 * ============================================================================= */
