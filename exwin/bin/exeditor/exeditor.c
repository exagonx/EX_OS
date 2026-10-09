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
#define VERSIONE_APP "0.008"
EX_VERSIONE("exeditor", VERSIONE_APP);

/* ! LA FINESTRA NASCE LARGA QUANTO UNA COLONNA DI A4, se lo schermo lo
 * permette (9 ottobre 2026): 700 pixel fanno stare i 643 del testo di una
 * pagina coi suoi margini di 2 cm, e la barra degli strumenti intera. Su uno
 * schermo da 640 resta quella di prima. Lo decide main(). */
static int g_w0 = 640, g_h0 = 420;
#define FIN_W       g_w0
#define FIN_H       g_h0

/* La barra dei menu occupa i primi 20 pixel; l'area comincia sotto. */
#define MENU_H      20
#define SCHEDE_H    22          /* la barra delle schede sotto i menu */
#define AREA_X      4
#define AREA_Y      (MENU_H + SCHEDE_H + 4)
#define BASSO       24          /* la riga di stato in fondo */
/* ! LA BARRA DELL'EDITOR NON C'E' PIU' (9 ottobre 2026). L'utente: «a destra
 * ci sono DUE barre di scorrimento, una accanto all'altra». Quella di qui era
 * nata il 23 settembre, quando l'area di testo del toolkit non ne aveva; poi
 * l'area ha avuto le sue (verticale e orizzontale) e questa e' rimasta
 * accanto, a fare la stessa cosa. Resta quella dell'area, che e' di tutti i
 * programmi; l'area prende la larghezza che la barra occupava. */
#define BARRA_W     0

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
/* The paragraph and the page (9 October 2026): indent less and more, the two
 * windows, line spacing, "no tab stops"; then what is shown. */
#define ID_R_RM       53
#define ID_R_RP       54
#define ID_F_PAR      55
#define ID_F_PAG      56
#define ID_F_INT1     57
#define ID_F_INT15    58
#define ID_F_INT2     59
#define ID_F_TABVIA   60
#define ID_V_BARRA    61
#define ID_V_RIGH     62
#define ID_V_PAG      63

static char g_perc[PERC_MAX] = "";
static int  g_parziale = 0;     /* letto SOLO IN PARTE: non si salva */
static char g_avviso[96] = "";

static ExWindow g_f, g_area, g_stato, g_menu, g_barra, g_schede;

/* The window's client size, for the RTF view that has no control to resize. */
static int g_fw = 640, g_fh = 420;

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
 * un editor abbia di distruggere dei dati. E' anche perche' ex_textarea_add_line()
 * rende 0 quando l'area e' piena, invece di smettere in silenzio.
 * --------------------------------------------------------------------------- */
static int carica(const char *percorso)
{
    char buf[512], riga[256];
    int  fd, n, i;
    unsigned int col = 0;

    ex_textarea_clear(g_area);
    g_parziale = 0;

    fd = open(percorso, O_RDONLY, 0);
    if (fd < 0) return 0;               /* non c'e': e' un file nuovo */

    while ((n = (int)read(fd, buf, sizeof buf)) > 0) {
        for (i = 0; i < n; i++) {
            char c = buf[i];

            if (c == '\r') continue;    /* i fine-riga di DOS non si vedono */

            if (c == '\n') {
                riga[col] = '\0';
                if (!ex_textarea_add_line(g_area, riga)) { g_parziale = 1; goto fine; }
                col = 0;
                continue;
            }

            if (col + 1 < sizeof(riga)) riga[col++] = c;
            else                        g_parziale = 1;
        }
    }
    riga[col] = '\0';
    if (col && !ex_textarea_add_line(g_area, riga)) g_parziale = 1;

fine:
    close(fd);
    ex_textarea_set_unmodified(g_area);
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

    n = ex_textarea_line_count(g_area);
    for (i = 0; i < n; i++) {
        const char  *r = ex_textarea_line(g_area, i);
        unsigned int l = (unsigned int)strlen(r);

        if ((l && write(fd, r, l) != (ssize_t)l) || write(fd, "\n", 1) != 1) {
            close(fd);
            strcpy(g_avviso, "scrittura interrotta: il file e' incompleto");
            return 0;
        }
    }
    close(fd);

    ex_textarea_set_unmodified(g_area);
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

        char pag[32];

        pag[0] = '\0';
        if (g_vista.pagina)
            snprintf(pag, sizeof(pag), "pag. %u/%u, ", exrtf_vista_pagina_di(&g_vista), g_vista.pagine);
        sprintf(s, "%s%s  -  RTF  -  %sparagrafo %u/%u, %s%s",
                g_vista.modificato ? "*" : "", nome, pag, p + 1, d->par_n, AL[a < 4 ? a : 0],
                g_parziale ? "  [PARZIALE: non si salva]" : "");
    } else {
        ex_textarea_get_cursor(g_area, &r, &c);
        sprintf(s, "%s%s  -  riga %u/%u  col %u%s",
                ex_textarea_is_modified(g_area) ? "*" : "", nome,
                r, ex_textarea_line_count(g_area), c,
                g_parziale ? "  [PARZIALE: non si salva]" : "");
    }

    ex_set_text(g_stato, s);
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
 * essere trascinata, l'area parte da quella riga (ex_textarea_scroll_to) senza
 * spostare il cursore: il primo tasto riporta la vista dove si scrive, come
 * in ogni editor.
 * ============================================================================= */
static void barra_allinea(void)
{
    unsigned int vis = 0;
    unsigned int prima = ex_textarea_get_view(g_area, &vis);
    unsigned int n = ex_textarea_line_count(g_area);

    if (!g_barra) return;
    ex_scroll_set_range(g_barra, n > vis ? n - vis : 0, vis);
    ex_scroll_set_pos(g_barra, prima);
}

static void ridisegna(void)
{
    stato_aggiorna();
    barra_allinea();
    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    vista_disegna_se();
    ex_update(g_f);
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
    ex_quit(0);
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
                  "RTF, e WordPad o LibreOffice lo aprono.  Sotto la barra "
                  "degli strumenti c'e' il righello, in centimetri: i "
                  "triangoli sono i rientri del paragrafo e si trascinano, "
                  "un clic mette una tabulazione, trascinarla via la toglie.  "
                  "Formato > Paragrafo e Formato > Pagina chiedono rientri, "
                  "interlinea, carta e margini.  Visualizza accende e spegne "
                  "barra, righello e vista pagina.  Un .doc di Word 6 o 95 "
                  "si apre e si salva come RTF; quelli di Word 97 e dopo no.");
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
 * scritto anche accanto a EXM_KEY qui sotto. Si annullano i tre COMANDI che
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
    unsigned int n = ex_textarea_line_count(g_area), i, tot = 1;
    char        *b;

    for (i = 0; i < n; i++) {
        const char *r = ex_textarea_line(g_area, i);
        unsigned int k = 0;

        while (r && r[k]) k++;
        tot += k + 1;                       /* la riga piu' il suo a capo */
    }

    b = (char *)malloc(tot);
    if (!b) return 0;

    tot = 0;
    for (i = 0; i < n; i++) {
        const char *r = ex_textarea_line(g_area, i);
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
    ex_textarea_clear(g_area);

    i = 0;
    while (t[i]) {
        a = 0;
        while (t[i] && t[i] != '\n' && a < sizeof(riga) - 1) riga[a++] = t[i++];
        riga[a] = '\0';
        ex_textarea_add_line(g_area, riga);
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
    int          da_doc;            /* read from a Word file: saved as RTF, elsewhere */
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
static ExWindow g_rfam, g_rcorpo, g_rcol;
static int        g_mod_visto = -1;     /* the "modified" the tab title shows */

/* =============================================================================
 * THE TWO BARS (9 October 2026, asked by the user: «la doppia barra, uno
 * strumenti e l'altro righello e tabulazioni»)
 *
 * Under the tabs a rich text document has the TOOL bar - small pictures for
 * new, open, save, the clipboard, bold italic underline, typeface size and
 * colour, the four alignments, indent less and more - and under it the RULER,
 * in centimetres, with the paragraph's indents and tab stops as marks that
 * are dragged. Each can be switched off from the Visualizza menu, and the
 * choice is kept in $HOME/.exwin/config/exeditor.cfg.
 *
 * ! THE BUTTONS ARE PLAIN BUTTONS, AND WHAT IS ON SHOWS UNDERNEATH. The
 * toolkit has no button that stays pressed; a blue line under G, C, S and
 * under the alignment in use says the same thing (barra_segni).
 * ============================================================================= */
#define RIGH_H       22
enum { BT_NUOVO, BT_APRI, BT_SALVA, BT_TAGLIA, BT_COPIA, BT_INCOLLA,
       BT_G, BT_C, BT_S, BT_SIN, BT_CEN, BT_DES, BT_GIU, BT_RM, BT_RP, BT_N };
static ExWindow g_bt[BT_N];
static int      g_bt_x[BT_N];
static int      g_vedi_barra = 1, g_vedi_righello = 1, g_vedi_pagina = 1;
static int      g_seg_g = -1, g_seg_c = -1, g_seg_s = -1, g_seg_al = -1;
static void     righello_disegna(void);
static void     barra_segni(void);

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
    ExRtfPar      *a = (ExRtfPar *)malloc(RTF_PAR * sizeof(ExRtfPar));

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
    free(D->rd.par);
    memset(&D->rd, 0, sizeof(D->rd));
    D->rtf = 0;
}

/* Where the view goes: under the format bar, down to the status line. */
static void vista_rett(int *x, int *y, int *w, int *h)
{
    *x = AREA_X;
    *y = AREA_Y + (g_vedi_barra ? RTF_BARRA_H : 0) + (g_vedi_righello ? RIGH_H : 0);
    *w = g_fw - AREA_X * 2;
    *h = g_fh - *y - BASSO;
    if (*w < 60) *w = 60;
    if (*h < 30) *h = 30;
}

static void vista_collega(int i)
{
    Doc *D = &g_doc[i];
    int  x, y, w, h;

    g_vista.pagina = g_vedi_pagina;
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
    int               cambiato = 0, al;

    if (!g_rtf || !g_rfam) return 0;
    s = exrtf_vista_stile(&g_vista);
    al = (int)exrtf_vista_allineamento(&g_vista);
    if (g_seg_g != (int)s->grassetto || g_seg_c != (int)s->corsivo ||
        g_seg_s != (int)s->sottolineato || g_seg_al != al) {
        g_seg_g = s->grassetto; g_seg_c = s->corsivo; g_seg_s = s->sottolineato; g_seg_al = al;
        cambiato = 1;
    }
    if (ex_item_get_selected(g_rfam) != s->famiglia)   { ex_item_select(g_rfam, s->famiglia); cambiato = 1; }
    for (i = 1; i < CORPI_N; i++) {
        unsigned int di = CORPI[i] > s->corpo ? CORPI[i] - s->corpo : s->corpo - CORPI[i];
        unsigned int dk = CORPI[k] > s->corpo ? CORPI[k] - s->corpo : s->corpo - CORPI[k];
        if (di < dk) k = i;
    }
    if (ex_item_get_selected(g_rcorpo) != k) { ex_item_select(g_rcorpo, k); cambiato = 1; }
    for (i = 0; i < COLORI_N && COLORE[i] != s->colore; i++) ;
    if (i < COLORI_N && ex_item_get_selected(g_rcol) != i) { ex_item_select(g_rcol, i); cambiato = 1; }
    return cambiato;
}

/* The blue line under what is on. Drawn after the window: the buttons are
 * the toolkit's, the line is ours. */
static void barra_segni(void)
{
    int i, y = AREA_Y + 2 + 21;

    if (!g_rtf || !g_vedi_barra || !g_bt[BT_G]) return;
    for (i = BT_G; i <= BT_GIU; i++) {
        int acceso = i == BT_G ? g_seg_g == 1 : i == BT_C ? g_seg_c == 1 :
                     i == BT_S ? g_seg_s == 1 : g_seg_al == i - BT_SIN;

        ex_fill_rect(g_f, g_bt_x[i] + 2, y, 20, 2, acceso ? EX_BLUE : EX_GRAY);
    }
}

/* Text area or view, and their bars; the keys go with them. */
static void modo_mostra(void)
{
    int i, barra = g_rtf && g_vedi_barra;

    ex_show(g_area, !g_rtf);
    if (g_barra) ex_show(g_barra, !g_rtf);
    for (i = 0; i < BT_N; i++) if (g_bt[i]) ex_show(g_bt[i], barra);
    if (g_rfam)   ex_show(g_rfam, barra);
    if (g_rcorpo) ex_show(g_rcorpo, barra);
    if (g_rcol)   ex_show(g_rcol, barra);
    /* ! THE HIDDEN AREA MUST LOSE THE FOCUS, or the keys would go on being
     * typed into a text nobody sees. With no control focused they come to
     * this program, which gives them to the view. */
    ex_tab_content(g_f, g_rtf);           /* Tab is a letter of the text */
    if (g_rtf) { ex_clear_focus(g_f); g_seg_g = -1; barra_rtf_segui(); }
    else       ex_set_focus(g_area);
}

static void vista_disegna_se(void)
{
    if (!g_rtf || !g_vista.doc) return;
    g_vista.fuoco = (ex_get_focus(g_f) == 0);
    exrtf_vista_disegna(&g_vista, g_f);
    righello_disegna();
    barra_segni();
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
        return EX_NO_REDRAW;
    }
    stato_aggiorna();
    vista_disegna_se();
    ex_redraw(g_stato);
    ex_update(g_f);
    return EX_NO_REDRAW;
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
                      D->rd.par, D->rd.par_max);
    }
    exrtf_stile_base(&D->v_stile);
    D->v_cur = D->v_anc = D->v_prima = 0;
    D->modificato = 0;
    passo_tutti_via();
    ex_textarea_clear(g_area);
    ex_textarea_set_unmodified(g_area);
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

static int e_ole(const char *b)
{
    static const unsigned char F[8] = { 0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1 };
    int i;

    for (i = 0; i < 8; i++) if ((unsigned char)b[i] != F[i]) return 0;
    return 1;
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
        /* ! AND A WORD FILE, by its first eight bytes (an OLE2 compound
         * file): it opens as rich text too, read by lib/exrtf/doc95.c. */
        if (n >= 8 && e_ole(b)) return 1;
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
                  D->rd.par, D->rd.par_max);
    D->da_doc = 0;
    if (k >= 8 && e_ole(b)) {
        /* ! A WORD 6/95 FILE IS READ, NEVER WRITTEN. What was opened is saved
         * as RTF, under another name: salva() asks for it (da_doc). A Word
         * file this program cannot read opens EMPTY and cannot be saved over
         * the original - the same rule as a file read in part. */
        unsigned char *lav = (unsigned char *)malloc(k + 1);
        int esito = lav ? exrtf_leggi_doc(&D->rd, (const unsigned char *)b, k, lav, k) : -3;

        free(lav);
        free(b);
        if (esito == 1) {
            D->da_doc = 1;
            g_parziale = D->rd.troncato;
            strcpy(g_avviso, "documento di Word 6/95: si salva come RTF");
        } else {
            g_parziale = 1;
            strcpy(g_avviso, esito == -1 ? "e' un .doc di Word 97 o successivo: non lo so leggere" :
                             esito == -2 ? "il documento e' cifrato: non lo so leggere" :
                                           "non e' un documento di Word 6/95 che so leggere");
        }
        exrtf_vista_nuovo(&g_vista);
        barra_rtf_segui();
        g_mod_visto = -1;
        return 1;
    }
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
    unsigned int max = D->rd.testo_n * 16 + D->rd.pezzi_n * 64 + D->rd.par_n * 96 + 8192, n;
    char        *b;
    int          fd;

    /* A document that came from a Word file: its name becomes .rtf and the
     * place is asked, once. The .doc is never written over. */
    if (D->da_doc) {
        unsigned int l = (unsigned int)strlen(g_perc);
        char nuovo[PERC_MAX];

        strncpy(nuovo, g_perc, PERC_MAX - 1);
        nuovo[PERC_MAX - 1] = '\0';
        if (l > 4 && nuovo[l - 4] == '.') strcpy(nuovo + l - 4, ".rtf");
        else if (l + 5 < PERC_MAX) strcat(nuovo, ".rtf");
        if (!ex_dlg_salva(nuovo, PERC_MAX)) { strcpy(g_avviso, "salvataggio annullato"); return 0; }
        strncpy(g_perc, nuovo, PERC_MAX - 1);
        g_perc[PERC_MAX - 1] = '\0';
        D->da_doc = 0;
    }
    b = (char *)malloc(max);

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

static void paragrafo_apri(void);
static void pagina_apri(void);

/* A command of the format bar or of the Formato menu. */
static void formato(unsigned int id, long lp)
{
    if (!g_rtf) {
        strcpy(g_avviso, "il formato c'e' nei documenti RTF (File > Nuovo documento RTF)");
        return;
    }
    switch (id) {
    case ID_R_G: case ID_M_G: exrtf_vista_cambia(&g_vista, EXRTF_C_GRASSETTO, EXRTF_INVERTI); break;
    case ID_R_C: case ID_M_C: exrtf_vista_cambia(&g_vista, EXRTF_C_CORSIVO, EXRTF_INVERTI); break;
    case ID_R_S: case ID_M_S: exrtf_vista_cambia(&g_vista, EXRTF_C_SOTTOLINEATO, EXRTF_INVERTI); break;
    /* Indent less and more: a step of 1,25 cm, the default tab of Word. */
    case ID_R_RM: case ID_R_RP: {
        int sin = exrtf_vista_par(&g_vista)->sin, passo = 709;

        sin = id == ID_R_RP ? (sin / passo + 1) * passo : ((sin + passo - 1) / passo - 1) * passo;
        exrtf_vista_par_cambia(&g_vista, EXRTF_P_SIN, sin < 0 ? 0 : sin);
        break;
    }
    case ID_F_INT1:  exrtf_vista_par_cambia(&g_vista, EXRTF_P_INTERLINEA, 100); break;
    case ID_F_INT15: exrtf_vista_par_cambia(&g_vista, EXRTF_P_INTERLINEA, 150); break;
    case ID_F_INT2:  exrtf_vista_par_cambia(&g_vista, EXRTF_P_INTERLINEA, 200); break;
    case ID_F_TABVIA: exrtf_vista_tab(&g_vista, 0, 0); break;
    case ID_F_PAR: paragrafo_apri(); return;
    case ID_F_PAG: pagina_apri();    return;
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
    ex_clear_focus(g_f);
    barra_rtf_segui();
}

/* The tool bar. Hidden until a rich text tab is chosen. The pictures are in
 * /exwin/icon/strumenti (tools/exeditor-icone.py draws them); without them
 * the buttons carry a letter, so a system with no icons still works. */
static void barra_rtf_crea(void)
{
    static const struct { unsigned int id; const char *icona; const char *lettera; } B[BT_N] = {
        { ID_NUOVO_RTF, "nuovo", "N" },   { ID_APRI, "apri", "A" },       { ID_SALVA, "salva", "S" },
        { ID_TAGLIA, "taglia", "X" },     { ID_COPIA, "copia", "C" },     { ID_INCOLLA, "incolla", "V" },
        { ID_R_G, "grassetto", "G" },     { ID_R_C, "corsivo", "C" },     { ID_R_S, "sottolineato", "S" },
        { ID_R_SIN, "sinistra", "<" },    { ID_R_CEN, "centro", "=" },    { ID_R_DES, "destra", ">" },
        { ID_R_GIU, "giustifica", "#" },  { ID_R_RM, "rientro_meno", "-" }, { ID_R_RP, "rientro_piu", "+" }
    };
    int y = AREA_Y + 2, i, x = 4;

    for (i = 0; i < BT_N; i++) {
        char   p[96];
        ExIcon ic;

        if (i == BT_TAGLIA || i == BT_G || i == BT_RM) x += 6;
        if (i == BT_SIN) {
            /* typeface, size, colour sit between the letters and the lines */
            int k;

            x += 6;
            g_rfam = ex_create("combo", "", EX_CHILD, x, y, 72, 20, g_f, ID_R_FAM, 0);
            ex_item_add(g_rfam, "Serif");
            ex_item_add(g_rfam, "Sans");
            ex_item_add(g_rfam, "Mono");
            x += 74;
            g_rcorpo = ex_create("combo", "", EX_CHILD, x, y, 46, 20, g_f, ID_R_CORPO, 0);
            for (k = 0; k < (int)CORPI_N; k++) {
                char t[8];
                snprintf(t, sizeof(t), "%u", CORPI[k]);
                ex_item_add(g_rcorpo, t);
            }
            x += 48;
            g_rcol = ex_create("combo", "", EX_CHILD, x, y, 84, 20, g_f, ID_R_COLORE, 0);
            for (k = 0; k < (int)COLORI_N; k++) ex_item_add(g_rcol, COLORE_NOME[k]);
            x += 84 + 6;
        }
        snprintf(p, sizeof(p), "/exwin/icon/strumenti/%s.ico", B[i].icona);
        ic = ex_icon_open(p);
        g_bt[i] = ex_create("button", ic ? "" : B[i].lettera, EX_CHILD, x, y, 24, 20, g_f, B[i].id, 0);
        if (ic && g_bt[i]) ex_set_icon(g_bt[i], ic, 16);
        g_bt_x[i] = x;
        x += 25;
    }
    ex_item_select(g_rfam, 0);
    ex_item_select(g_rcorpo, 4);                /* 12 */
    ex_item_select(g_rcol, 0);
}

/* =============================================================================
 * THE RULER
 *
 * Centimetres from the left margin of the text, which is where the view says
 * its column starts (exrtf_vista_colonna): in page layout the white part is
 * the column and the grey the margins of the paper. On it, the paragraph of
 * the caret:
 *
 *     a triangle pointing down, on top     the first line
 *     a triangle pointing up, at the left  the left indent (moves both)
 *     a triangle pointing up, at the right the right indent
 *     a small L                            a tab stop
 *
 * ! THE MOUSE: a mark is dragged; a click on the empty part puts a tab stop
 * there; a tab stop dragged off the ruler goes away, as in Word. Things snap
 * to an eighth of a centimetre - a ruler that keeps 1,2496 cm is a ruler
 * nobody can line two paragraphs up with.
 * ============================================================================= */
#define RG_NIENTE 0
#define RG_PRIMA  1
#define RG_SIN    2
#define RG_DES    3
#define RG_TAB    4
static int          g_rg_cosa = RG_NIENTE;
static unsigned int g_rg_tab = 0;       /* the stop being dragged, 0 = off the ruler */

static void righello_rett(int *x, int *y, int *w, int *h)
{
    *x = AREA_X;
    *y = AREA_Y + (g_vedi_barra ? RTF_BARRA_H : 0);
    *w = g_fw - AREA_X * 2;
    *h = RIGH_H;
}

static void tri_giu(int x, int y, unsigned int c)       /* point down, top at y */
{
    int i;
    for (i = 0; i < 5; i++) ex_fill_rect(g_f, x - 4 + i, y + i, 9 - 2 * i, 1, c);
}

static void tri_su(int x, int y, unsigned int c)        /* point up, base at y + 4 */
{
    int i;
    for (i = 0; i < 5; i++) ex_fill_rect(g_f, x - i, y + i, 2 * i + 1, 1, c);
}

static void righello_disegna(void)
{
    const ExRtfPar *P;
    ExFont fo;
    int    x, y, w, h, cx, cw, q, a, b;

    if (!g_rtf || !g_vedi_righello || !g_vista.doc) return;
    righello_rett(&x, &y, &w, &h);
    exrtf_vista_colonna(&g_vista, &cx, &cw);
    P = exrtf_vista_par(&g_vista);
    fo = ex_font_find(EXRTF_SANS, 10, 0, 0);

    ex_fill_rect(g_f, x, y, w, h, EX_GRAY);
    ex_fill_rect(g_f, x, y + 3, w, h - 6, 0x00A8A8A8);
    a = cx < x ? x : cx;
    b = cx + cw > x + w ? x + w : cx + cw;
    if (b > a) ex_fill_rect(g_f, a, y + 3, b - a, h - 6, EX_WHITE);

    /* quarter centimetres: a number at each whole one */
    for (q = 1; ; q++) {
        int tx = cx + exrtf_px(q * EXRTF_CM / 4);

        if (tx >= cx + cw || tx >= x + w) break;
        if (tx < x) continue;
        if (q % 4 == 0) {
            char t[8];

            snprintf(t, sizeof(t), "%d", q / 4);
            ex_draw_text_font(g_f, fo, tx - ex_text_width(fo, t) / 2, y + 4, t, EX_DARK_GRAY);
        } else if (q % 2 == 0) {
            ex_fill_rect(g_f, tx, y + 8, 1, 6, EX_DARK_GRAY);
        } else {
            ex_fill_rect(g_f, tx, y + 10, 1, 2, EX_DARK_GRAY);
        }
    }

    for (q = 0; q < (int)P->tab_n; q++) {
        int tx = cx + exrtf_px(P->tab[q]);

        if (tx < x || tx + 6 > x + w) continue;
        ex_fill_rect(g_f, tx, y + h - 11, 2, 7, EX_BLACK);
        ex_fill_rect(g_f, tx, y + h - 6, 6, 2, EX_BLACK);
    }
    q = cx + exrtf_px(P->sin + P->prima);
    if (q >= x && q < x + w) tri_giu(q, y, EX_BLUE);
    q = cx + exrtf_px(P->sin);
    if (q >= x && q < x + w) tri_su(q, y + h - 5, EX_BLUE);
    q = cx + cw - exrtf_px(P->des);
    if (q >= x && q < x + w) tri_su(q, y + h - 5, EX_BLUE);
}

static int rg_vicino(int a, int b, int quanto) { return a - b <= quanto && b - a <= quanto; }

/* The twips of a window x, snapped to an eighth of a centimetre. */
static int rg_twips(int wx)
{
    int cx, cw, t;

    exrtf_vista_colonna(&g_vista, &cx, &cw);
    t = exrtf_twips(wx - cx);
    if (t >= 0) t = (t + 35) / 71 * 71;
    else        t = -((-t + 35) / 71 * 71);
    return t;
}

static void righello_muovi(int wx, int wy)
{
    const ExRtfPar *P = exrtf_vista_par(&g_vista);
    int x, y, w, h, t = rg_twips(wx);

    righello_rett(&x, &y, &w, &h);
    switch (g_rg_cosa) {
    case RG_PRIMA: exrtf_vista_par_cambia(&g_vista, EXRTF_P_PRIMA, t - P->sin); break;
    case RG_SIN:   exrtf_vista_par_cambia(&g_vista, EXRTF_P_SIN, t); break;
    case RG_DES:   exrtf_vista_par_cambia(&g_vista, EXRTF_P_DES, (int)exrtf_colonna(g_vista.doc) - t); break;
    case RG_TAB: {
        int fuori = wy < y - 20 || wy > y + h + 20 || t <= 0 || t >= (int)exrtf_colonna(g_vista.doc);

        if (g_rg_tab) exrtf_vista_tab(&g_vista, g_rg_tab, 0);
        g_rg_tab = fuori ? 0 : (unsigned int)t;
        if (g_rg_tab) exrtf_vista_tab(&g_vista, g_rg_tab, 1);
        break;
    }
    default: break;
    }
}

/* The button went down at (wx, wy). 1 if it was the ruler's. */
static int righello_giu(int wx, int wy)
{
    const ExRtfPar *P;
    int x, y, w, h, cx, cw, i, sopra;

    if (!g_rtf || !g_vedi_righello || !g_vista.doc) return 0;
    righello_rett(&x, &y, &w, &h);
    if (wx < x || wx >= x + w || wy < y || wy >= y + h) return 0;
    exrtf_vista_colonna(&g_vista, &cx, &cw);
    P = exrtf_vista_par(&g_vista);
    sopra = wy < y + h / 2;

    g_rg_cosa = RG_NIENTE;
    if (sopra && rg_vicino(wx, cx + exrtf_px(P->sin + P->prima), 5)) g_rg_cosa = RG_PRIMA;
    else if (!sopra && rg_vicino(wx, cx + exrtf_px(P->sin), 5))      g_rg_cosa = RG_SIN;
    else if (rg_vicino(wx, cx + cw - exrtf_px(P->des), 5))           g_rg_cosa = RG_DES;
    else if (rg_vicino(wx, cx + exrtf_px(P->sin + P->prima), 5))     g_rg_cosa = RG_PRIMA;
    else if (rg_vicino(wx, cx + exrtf_px(P->sin), 5))                g_rg_cosa = RG_SIN;
    else {
        for (i = 0; i < (int)P->tab_n; i++)
            if (rg_vicino(wx, cx + exrtf_px(P->tab[i]) + 2, 5)) {
                g_rg_cosa = RG_TAB;
                g_rg_tab = P->tab[i];
                return 1;
            }
        if (wx > cx && wx < cx + cw) {          /* a new stop, already in hand */
            int t = rg_twips(wx);

            if (t > 0) {
                g_rg_cosa = RG_TAB;
                g_rg_tab = (unsigned int)t;
                exrtf_vista_tab(&g_vista, g_rg_tab, 1);
            }
        }
    }
    return 1;
}

/* =============================================================================
 * THE TWO WINDOWS: Paragrafo and Pagina
 *
 * Lengths are typed in centimetres, with a comma or a point; space above and
 * below a paragraph in points, as every word processor asks them.
 * ============================================================================= */
#define ID_D_OK   1
#define ID_D_NO   2
static ExWindow g_dp, g_dp_sin, g_dp_des, g_dp_prima, g_dp_sp, g_dp_sd, g_dp_int;
static ExWindow g_dg, g_dg_formato, g_dg_verso, g_dg_m[4];

static void cm_scrivi(char *out, unsigned int max, int twips)
{
    int c = twips * 100 / EXRTF_CM, n = c < 0 ? -c : c;

    snprintf(out, max, "%s%d,%02d", c < 0 ? "-" : "", n / 100, n % 100);
}

/* "1,25" or "1.25" or "-0,5": twips. What is not a number is 0. */
static int cm_leggi(const char *t)
{
    int segno = 1, intero = 0, cent = 0, cifre = 0;

    if (!t) return 0;
    while (*t == ' ') t++;
    if (*t == '-') { segno = -1; t++; }
    while (*t >= '0' && *t <= '9') { if (intero < 1000) intero = intero * 10 + (*t - '0'); t++; }
    if (*t == ',' || *t == '.') {
        t++;
        while (*t >= '0' && *t <= '9') {
            if (cifre < 2) { cent = cent * 10 + (*t - '0'); cifre++; }
            t++;
        }
        if (cifre == 1) cent *= 10;
    }
    return segno * ((intero * 100 + cent) * EXRTF_CM / 100);
}

static int punti_leggi(const char *t) { return t ? atoi(t) * 20 : 0; }

static ExWindow campo(ExWindow f, const char *etichetta, int y, const char *valore)
{
    ex_create("label", etichetta, EX_CHILD, 12, y + 4, 170, 16, f, 0, 0);
    return ex_create("textbox", valore, EX_CHILD, 190, y, 80, 22, f, 0, 0);
}

static void finestra_via(ExWindow *f)
{
    if (*f) ex_destroy(*f);
    *f = 0;
    ridisegna();
    if (g_rtf) ex_clear_focus(g_f);
}

static long paragrafo_proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_CLOSE || (msg == EXM_COMMAND && wp == ID_D_NO)) { finestra_via(&g_dp); return 0; }
    if (msg == EXM_COMMAND && wp == ID_D_OK) {
        static const int INT[3] = { 100, 150, 200 };
        unsigned int k = ex_item_get_selected(g_dp_int);
        int sin = cm_leggi(ex_get_text(g_dp_sin)), des = cm_leggi(ex_get_text(g_dp_des));
        int prima = cm_leggi(ex_get_text(g_dp_prima));
        int sp = punti_leggi(ex_get_text(g_dp_sp)), sd = punti_leggi(ex_get_text(g_dp_sd));

        /* the left indent first: the first line is counted from it */
        exrtf_vista_par_cambia(&g_vista, EXRTF_P_PRIMA, 0);
        exrtf_vista_par_cambia(&g_vista, EXRTF_P_SIN, sin);
        exrtf_vista_par_cambia(&g_vista, EXRTF_P_DES, des);
        exrtf_vista_par_cambia(&g_vista, EXRTF_P_PRIMA, prima);
        exrtf_vista_par_cambia(&g_vista, EXRTF_P_SP_PRIMA, sp);
        exrtf_vista_par_cambia(&g_vista, EXRTF_P_SP_DOPO, sd);
        exrtf_vista_par_cambia(&g_vista, EXRTF_P_INTERLINEA, INT[k < 3 ? k : 0]);
        finestra_via(&g_dp);
        return 0;
    }
    return ex_default_proc(f, msg, wp, lp);
}

static void paragrafo_apri(void)
{
    const ExRtfPar *P = exrtf_vista_par(&g_vista);
    char t[24];

    if (g_dp) return;
    g_dp = ex_create("window", "Paragrafo", EX_CAPTION | EX_BORDER | EX_CLOSEBOX | EX_MODAL,
                     EX_AUTO, EX_AUTO, 290, 230, 0, 0, paragrafo_proc);
    if (!g_dp) return;
    cm_scrivi(t, sizeof(t), P->sin);    g_dp_sin   = campo(g_dp, "Rientro sinistro (cm)", 10, t);
    cm_scrivi(t, sizeof(t), P->des);    g_dp_des   = campo(g_dp, "Rientro destro (cm)", 38, t);
    cm_scrivi(t, sizeof(t), P->prima);  g_dp_prima = campo(g_dp, "Prima riga (cm)", 66, t);
    snprintf(t, sizeof(t), "%d", P->sp_prima / 20); g_dp_sp = campo(g_dp, "Spazio prima (punti)", 94, t);
    snprintf(t, sizeof(t), "%d", P->sp_dopo / 20);  g_dp_sd = campo(g_dp, "Spazio dopo (punti)", 122, t);
    ex_create("label", "Interlinea", EX_CHILD, 12, 154, 170, 16, g_dp, 0, 0);
    g_dp_int = ex_create("combo", "", EX_CHILD, 190, 150, 80, 22, g_dp, 0, 0);
    ex_item_add(g_dp_int, "Singola");
    ex_item_add(g_dp_int, "1,5 righe");
    ex_item_add(g_dp_int, "Doppia");
    ex_item_select(g_dp_int, P->interlinea >= 175 ? 2 : P->interlinea > 100 ? 1 : 0);
    ex_create("button", "Va bene", EX_CHILD, 74, 188, 90, 26, g_dp, ID_D_OK, 0);
    ex_create("button", "Annulla", EX_CHILD, 180, 188, 90, 26, g_dp, ID_D_NO, 0);
    ex_set_focus(g_dp_sin);
    ex_default_proc(g_dp, EXM_PAINT, 0, 0);
    ex_update(g_dp);
}

static const struct { const char *nome; unsigned int w, h; } CARTE[] = {
    { "A4 (21 x 29,7 cm)", 11906, 16838 }, { "A5 (14,8 x 21 cm)", 8391, 11906 },
    { "Letter (8,5 x 11 in)", 12240, 15840 }, { "Legal (8,5 x 14 in)", 12240, 20160 }
};
#define CARTE_N (sizeof(CARTE) / sizeof(CARTE[0]))

static long pagina_proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_CLOSE || (msg == EXM_COMMAND && wp == ID_D_NO)) { finestra_via(&g_dg); return 0; }
    if (msg == EXM_COMMAND && wp == ID_D_OK) {
        ExRtfDoc    *d = g_vista.doc;
        unsigned int k = ex_item_get_selected(g_dg_formato), steso = ex_item_get_selected(g_dg_verso);
        int          m[4], i;

        if (k >= CARTE_N) k = 0;
        for (i = 0; i < 4; i++) {
            m[i] = cm_leggi(ex_get_text(g_dg_m[i]));
            if (m[i] < 0) m[i] = 0;
            if (m[i] > 4000) m[i] = 4000;           /* seven centimetres */
        }
        d->carta_w = steso ? CARTE[k].h : CARTE[k].w;
        d->carta_h = steso ? CARTE[k].w : CARTE[k].h;
        d->marg_sin = (unsigned int)m[0]; d->marg_des = (unsigned int)m[1];
        d->marg_su  = (unsigned int)m[2]; d->marg_giu = (unsigned int)m[3];
        g_vista.modificato = 1;
        exrtf_vista_impagina(&g_vista);
        finestra_via(&g_dg);
        return 0;
    }
    return ex_default_proc(f, msg, wp, lp);
}

static void pagina_apri(void)
{
    static const char *const M[4] = { "Margine sinistro (cm)", "Margine destro (cm)",
                                      "Margine in alto (cm)", "Margine in basso (cm)" };
    const ExRtfDoc *d = g_vista.doc;
    unsigned int    v[4], i, k = 0, steso = d->carta_w > d->carta_h;
    unsigned int    lato_corto = steso ? d->carta_h : d->carta_w, lato_lungo = steso ? d->carta_w : d->carta_h;
    char            t[24];

    if (g_dg) return;
    g_dg = ex_create("window", "Pagina", EX_CAPTION | EX_BORDER | EX_CLOSEBOX | EX_MODAL,
                     EX_AUTO, EX_AUTO, 290, 236, 0, 0, pagina_proc);
    if (!g_dg) return;
    for (i = 0; i < CARTE_N; i++) {
        unsigned int dw = CARTE[i].w > lato_corto ? CARTE[i].w - lato_corto : lato_corto - CARTE[i].w;
        unsigned int dh = CARTE[i].h > lato_lungo ? CARTE[i].h - lato_lungo : lato_lungo - CARTE[i].h;
        if (dw < 60 && dh < 60) k = i;
    }
    ex_create("label", "Formato", EX_CHILD, 12, 14, 80, 16, g_dg, 0, 0);
    g_dg_formato = ex_create("combo", "", EX_CHILD, 100, 10, 170, 22, g_dg, 0, 0);
    for (i = 0; i < CARTE_N; i++) ex_item_add(g_dg_formato, CARTE[i].nome);
    ex_item_select(g_dg_formato, k);
    ex_create("label", "Verso", EX_CHILD, 12, 42, 80, 16, g_dg, 0, 0);
    g_dg_verso = ex_create("combo", "", EX_CHILD, 100, 38, 170, 22, g_dg, 0, 0);
    ex_item_add(g_dg_verso, "In piedi");
    ex_item_add(g_dg_verso, "Steso");
    ex_item_select(g_dg_verso, steso);
    v[0] = d->marg_sin; v[1] = d->marg_des; v[2] = d->marg_su; v[3] = d->marg_giu;
    for (i = 0; i < 4; i++) {
        cm_scrivi(t, sizeof(t), (int)v[i]);
        g_dg_m[i] = campo(g_dg, M[i], 70 + (int)i * 28, t);
    }
    ex_create("button", "Va bene", EX_CHILD, 74, 194, 90, 26, g_dg, ID_D_OK, 0);
    ex_create("button", "Annulla", EX_CHILD, 180, 194, 90, 26, g_dg, ID_D_NO, 0);
    ex_default_proc(g_dg, EXM_PAINT, 0, 0);
    ex_update(g_dg);
}

/* What is shown, kept from one run to the next. */
static int vedi_cfg(char *out, int max, int fai)
{
    const char *casa = getenv("HOME");
    char d[PERC_MAX];

    if (!casa || !casa[0] || strcmp(casa, "/") == 0) casa = "/root";
    if (fai) {
        snprintf(d, sizeof(d), "%s/.exwin", casa);        mkdir(d, 0700);
        snprintf(d, sizeof(d), "%s/.exwin/config", casa); mkdir(d, 0700);
    }
    return snprintf(out, (size_t)max, "%s/.exwin/config/exeditor.cfg", casa) < max;
}

static int vedi_voce(const char *t, const char *chiave, int pred)
{
    const char *q = strstr(t, chiave);

    if (!q || !(q = strchr(q, '='))) return pred;
    q++;
    while (*q == ' ') q++;
    return *q == 's' || *q == 'S' || *q == '1';
}

static void vedi_leggi(void)
{
    char p[PERC_MAX], t[256];
    int  fd, n;

    if (!vedi_cfg(p, sizeof(p), 0)) return;
    fd = open(p, O_RDONLY, 0);
    if (fd < 0) return;
    n = (int)read(fd, t, sizeof(t) - 1);
    close(fd);
    if (n <= 0) return;
    t[n] = '\0';
    g_vedi_barra    = vedi_voce(t, "barra", 1);
    g_vedi_righello = vedi_voce(t, "righello", 1);
    g_vedi_pagina   = vedi_voce(t, "pagina", 1);
}

static void vedi_salva(void)
{
    char p[PERC_MAX], t[160];
    int  fd, n;

    if (!vedi_cfg(p, sizeof(p), 1)) return;
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;         /* a read-only system: the choice lasts this run */
    n = snprintf(t, sizeof(t), "# exeditor: lo riscrive il menu Visualizza.\n"
                               "barra = %s\nrighello = %s\npagina = %s\n",
                 g_vedi_barra ? "si" : "no", g_vedi_righello ? "si" : "no",
                 g_vedi_pagina ? "si" : "no");
    write(fd, t, (unsigned int)n);
    close(fd);
}

static void vedi_segna(void)
{
    ex_menu_check(g_menu, ID_V_BARRA, g_vedi_barra);
    ex_menu_check(g_menu, ID_V_RIGH, g_vedi_righello);
    ex_menu_check(g_menu, ID_V_PAG, g_vedi_pagina);
}

/* A command of the Visualizza menu. */
static void vedi(unsigned int id)
{
    if (id == ID_V_BARRA) g_vedi_barra = !g_vedi_barra;
    if (id == ID_V_RIGH)  g_vedi_righello = !g_vedi_righello;
    if (id == ID_V_PAG)   g_vedi_pagina = !g_vedi_pagina;
    vedi_segna();
    vedi_salva();
    if (g_rtf) {
        int x, y, w, h;

        g_vista.pagina = g_vedi_pagina;
        vista_rett(&x, &y, &w, &h);
        exrtf_vista_posto(&g_vista, x, y, w, h);
        modo_mostra();
    }
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
    if (strcmp(t, ex_item_text(g_schede, (unsigned int)i)) != 0) {
        ex_item_rename(g_schede, (unsigned int)i, t);
    }
}

/* Il titolo della scheda scelta segue il file e il suo asterisco. */
static void doc_segui_titolo(void)
{
    strncpy(g_doc[g_attivo].perc, g_perc, PERC_MAX - 1);
    g_doc[g_attivo].perc[PERC_MAX - 1] = '\0';
    doc_titolo(g_attivo, g_rtf ? g_vista.modificato : ex_textarea_is_modified(g_area));
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
    D->modificato = ex_textarea_is_modified(g_area);
    ex_textarea_get_cursor(g_area, &D->riga, &D->col);
    D->vista      = ex_textarea_get_view(g_area, 0);
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

    ex_textarea_clear(g_area);
    while (t && t[i]) {
        a = 0;
        while (t[i] && t[i] != '\n' && a < sizeof(riga) - 1) riga[a++] = t[i++];
        riga[a] = '\0';
        ex_textarea_add_line(g_area, riga);
        if (t[i] == '\n') i++;
    }
}

/* Il testo e' cambiato rispetto al file: l'area lo deve sapere. Si
 * riscrive la prima riga uguale a se stessa, che accende il segno. */
static void area_segna_modificata(void)
{
    char r[512];

    strncpy(r, ex_textarea_line(g_area, 0), sizeof(r) - 1);
    r[sizeof(r) - 1] = '\0';
    ex_textarea_set_line(g_area, 0, r);
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
        ex_item_select(g_schede, (unsigned int)i);
        modo_mostra();
        return;
    }
    g_rtf = 0;
    area_da_testo(D->testo);
    if (D->testo) { free(D->testo); D->testo = 0; }
    ex_textarea_set_unmodified(g_area);
    if (D->modificato) area_segna_modificata();
    ex_textarea_set_cursor(g_area, D->riga ? D->riga - 1 : 0, D->col ? D->col - 1 : 0);
    ex_textarea_scroll_to(g_area, D->vista);
    strncpy(g_perc, D->perc, PERC_MAX - 1);
    g_perc[PERC_MAX - 1] = '\0';
    g_parziale = D->parziale;
    memcpy(g_passi, D->passi, sizeof(g_passi));
    g_passi_n    = D->passi_n;
    g_passi_byte = D->passi_byte;
    ex_item_select(g_schede, (unsigned int)i);
    modo_mostra();
}

static void doc_scegli(int i)
{
    if (i == g_attivo || i < 0 || i >= g_ndoc) return;
    if (!doc_metti_via()) { ex_item_select(g_schede, (unsigned int)g_attivo); return; }
    doc_riprendi(i);
    g_avviso[0] = '\0';
}

/* Una scheda nuova, vuota, in fondo, e scelta. 0 se non ci sta. */
static int doc_nuovo(void)
{
    Doc *D;

    if (g_ndoc >= DOC_MAX || !ex_item_add(g_schede, "senza nome")) {
        sprintf(g_avviso, "al massimo %d file aperti: chiudine uno", DOC_MAX);
        return 0;
    }
    if (g_ndoc > 0 && !doc_metti_via()) {
        ex_item_remove(g_schede, (unsigned int)g_ndoc);
        return 0;
    }
    D = &g_doc[g_ndoc];
    memset(D, 0, sizeof(*D));
    D->usato = 1;
    g_attivo = g_ndoc++;
    ex_textarea_clear(g_area);
    ex_textarea_set_unmodified(g_area);
    g_perc[0] = '\0';
    g_parziale = 0;
    g_rtf = 0;
    ex_item_select(g_schede, (unsigned int)g_attivo);
    modo_mostra();
    return 1;
}

/* La scheda scelta e' vuota, senza nome e intatta: un file aperto la prende
 * invece di aggiungerne un'altra accanto (l'editor aperto senza file). */
static int doc_vergine(void)
{
    if (g_rtf) return 0;
    return g_perc[0] == '\0' && !ex_textarea_is_modified(g_area) &&
           ex_textarea_line_count(g_area) <= 1 && ex_textarea_line(g_area, 0)[0] == '\0';
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
    if (carica(g_perc)) sprintf(g_avviso, "aperto: %u righe", ex_textarea_line_count(g_area));
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
    mod = g_rtf ? g_vista.modificato : ex_textarea_is_modified(g_area);
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
        ex_textarea_clear(g_area);
        ex_textarea_set_unmodified(g_area);
        g_perc[0] = '\0';
        g_parziale = 0;
        strcpy(g_avviso, "chiuso");
        return;
    }
    for (k = i; k + 1 < g_ndoc; k++) g_doc[k] = g_doc[k + 1];
    memset(&g_doc[g_ndoc - 1], 0, sizeof(Doc));
    g_ndoc--;
    ex_item_remove(g_schede, (unsigned int)i);
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
             g_rtf ? g_vista.modificato : ex_textarea_is_modified(g_area);
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

/* ! THE CURSOR FROM ZERO. ex_textarea_get_cursor() counts from ONE — it feeds the
 * status line, «riga 1/1 col 1» — while ex_textarea_set_cursor() and
 * ex_textarea_select() count from zero. Mixing them sent every search one
 * line down: «Una» replaced nothing (tools/prova_edit_cerca.sh, 27 Sept). */
static void cursore0(unsigned int *r, unsigned int *c)
{
    ex_textarea_get_cursor(g_area, r, c);
    if (*r) (*r)--;
    if (*c) (*c)--;
}

/* The first occurrence at or after (r, c); 1 if found. */
static int trova_da(unsigned int r, unsigned int c, unsigned int *fr, unsigned int *fc)
{
    unsigned int n = ex_textarea_line_count(g_area), k;

    for (k = r; k < n; k++) {
        const char *riga = ex_textarea_line(g_area, k);
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
    ex_textarea_select(g_area, fr, fc, fc + (unsigned int)strlen(g_ago));
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
        unsigned int n = ex_textarea_line_count(g_area);
        unsigned int da = giro ? n : r + 1;

        for (k = da; k-- > 0; ) {
            const char *riga = ex_textarea_line(g_area, k), *p, *ultima = 0;

            if (!riga) continue;
            for (p = strstr(riga, g_ago); p; p = strstr(p + 1, g_ago)) {
                unsigned int col = (unsigned int)(p - riga);
                if (!giro && k == r && col + l >= c) break;
                ultima = p;
            }
            if (ultima) {
                unsigned int col = (unsigned int)(ultima - riga);
                if (giro) strcpy(g_avviso, "ricerca ripresa dalla fine");
                ex_textarea_select(g_area, k, col, col + l);
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
    const char  *riga = ex_textarea_line(g_area, r);
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
    if (fatte) ex_textarea_set_line(g_area, r, nuova);
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
    riga = ex_textarea_line(g_area, r);
    if (!riga || c < l || strncmp(riga + c - l, g_ago, l) != 0) {
        if (!avanti()) return;          /* nothing selected yet: find it */
        cursore0(&r, &c);
    }
    annulla_segna();
    sostituisci_in(r, (int)(c - l));
    ex_textarea_set_cursor(g_area, r, c - l + (unsigned int)strlen(g_nuovo));
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

    n = ex_textarea_line_count(g_area);
    for (k = 0; k < n; k++) {
        const char *riga = ex_textarea_line(g_area, k), *p;
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

static long proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    unsigned int c;
    int comando = (msg == EXM_COMMAND);

    switch (msg) {
    /* ! IL MENU E I PULSANTI ARRIVANO QUI ALLO STESSO MODO, con lo stesso id.
     * E' il motivo per cui aggiungere i menu non ha voluto una riga di codice
     * nuovo qui dentro: una voce di menu E' un pulsante, detto in un altro
     * posto. */
    case EXM_COMMAND:
        g_avviso[0] = '\0';
        if (wp == ID_SALVA)     { salva();            break; }
        if (wp == ID_NUOVO)     { doc_nuovo();        break; }
        if (wp == ID_NUOVO_RTF) { rtf_nuovo_doc();    break; }
        if (wp >= ID_R_G && wp <= ID_F_TABVIA) {
            formato(wp, lp);
            break;
        }
        if (wp >= ID_V_BARRA && wp <= ID_V_PAG) { vedi(wp); break; }
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
            n = ex_textarea_cut(g_area);
            if (n) sprintf(g_avviso, "tagliati %d byte", n);
            else   strcpy(g_avviso, "non c'e' niente di scelto");
            break;
        }
        if (wp == ID_COPIA)     {
            int n = ex_textarea_copy(g_area);
            if (n) sprintf(g_avviso, "copiati %d byte", n);
            else   strcpy(g_avviso, "non c'e' niente di scelto");
            break;
        }
        if (wp == ID_INCOLLA)   {
            int n;

            annulla_segna();
            n = ex_textarea_paste(g_area);
            if (n) sprintf(g_avviso, "incollati %d byte", n);
            else   strcpy(g_avviso, "gli appunti sono vuoti");
            break;
        }
        if (wp == ID_CANCELLA)  {
            annulla_segna();
            if (!ex_textarea_delete(g_area))
                strcpy(g_avviso, "non c'e' niente di scelto");
            break;
        }
        if (wp == ID_SELTUTTO)  { ex_textarea_select_all(g_area); break; }
        if (wp == ID_CERCA)      { cerca();       break; }
        if (wp == ID_AVANTI)     { if (g_sost_una) sostituisci_una(); else avanti(); break; }
        if (wp == ID_INDIETRO)   { indietro();    break; }
        if (wp == ID_SOSTITUISCI) { sostituisci(); break; }

        if (wp == ID_BARRA)      { ex_textarea_scroll_to(g_area, (unsigned int)lp); break; }
        if (wp == ID_ISTRUZIONI) { istruzioni();  break; }
        if (wp == ID_INFO)       { informazioni(); break; }
        return 0;

    case EXM_KEY:
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
            return ex_default_proc(f, msg, wp, lp);
        }

        /* ! LE SCORCIATOIE LE ESEGUE L'APPLICAZIONE, NON IL MENU. Il menu le
         * SCRIVE — e' il tab nel testo della voce — ma non le cattura: un menu
         * che si prendesse Ctrl+S da solo se lo prenderebbe anche mentre si
         * scrive dentro una casella di testo, e non c'e' modo di sapere da
         * dentro il toolkit se in quel momento ha senso. */
        if (wp & KBD_MOD_CTRL) {
            if (c == 's' || c == 'S') { salva();               break; }
            if (c == 'x' || c == 'X') { annulla_segna();
                                        ex_textarea_cut(g_area);  break; }
            if (c == 'c' || c == 'C') { ex_textarea_copy(g_area);   break; }
            if (c == 'v' || c == 'V') { annulla_segna();
                                        ex_textarea_paste(g_area); break; }
            if (c == 'z' || c == 'Z') {
                if (annulla_fai()) strcpy(g_avviso, "annullato");
                else               strcpy(g_avviso, "non c'e' niente da annullare");
                break;
            }
            if (c == 'a' || c == 'A') { ex_textarea_select_all(g_area); break; }
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
        return ex_default_proc(f, msg, wp, lp);

    /* La X di una scheda, o Ctrl+W (@TOOLKIT-SCHEDE). */
    case EXM_TAB_CLOSE:
        g_avviso[0] = '\0';
        doc_chiudi((int)lp);
        break;

    case EXM_CLOSE:
        /* ! CHIUDERE IN SILENZIO UN TESTO MODIFICATO E' IL MODO PIU' FACILE DI
         * PERDERE IL LAVORO DI QUALCUNO. La risposta prudente e' «no»:
         * chiudere il dialogo o battere Esc lascia l'editor aperto col testo
         * dentro. */
        esci_se_si_puo();
        break;

    /* La finestra ha cambiato misura: l'area di testo prende tutto lo spazio
     * che resta fra la barra dei menu e la riga di stato. */
    case EXM_SIZE: {
        int w = EX_X(lp), h = EX_Y(lp);

        g_fw = w;
        g_fh = h;
        if (g_rtf) {
            int vx, vy, vw, vh;
            vista_rett(&vx, &vy, &vw, &vh);
            exrtf_vista_posto(&g_vista, vx, vy, vw, vh);
        }

        ex_resize(g_area, w - AREA_X * 2 - BARRA_W, h - AREA_Y - BASSO);
        if (g_schede) ex_resize(g_schede, w - AREA_X * 2, SCHEDE_H);
        if (g_barra) {
            ex_move(g_barra, w - AREA_X - BARRA_W, AREA_Y);
            ex_resize(g_barra, BARRA_W, h - AREA_Y - BASSO);
        }
        ex_move(g_stato, 6, h - 22);
        ex_resize(g_stato, w - 12, 16);
        break;
    }

    /* ! IL DISEGNO CHE ARRIVA DAL TOOLKIT — dopo ogni tasto battuto
     * nell'area — passa di qui per rimettere la barra dove l'area e' andata;
     * senza, la barra resterebbe ferma mentre si scrive in fondo al testo. */
    case EXM_PAINT: {
        long r;

        /* ! E LA RIGA DI STATO CON LEI: prima di qui nessuno la rifaceva
         * dopo un tasto battuto nell'area, e «riga 1/512» restava scritto
         * mentre si scendeva di pagina in pagina. */
        stato_aggiorna();
        barra_allinea();
        r = ex_default_proc(f, msg, wp, lp);
        /* The view over the grey the base painted; ex_update puts an open
         * menu or combo back on top of it. */
        if (g_rtf) { vista_disegna_se(); ex_update(g_f); }
        return r;
    }

    /* The mouse on the rich text view (@RTF). */
    case EXM_MOUSE_DOWN:
        if (righello_giu(EX_X(lp), EX_Y(lp))) {
            ex_clear_focus(g_f);
            return vista_aggiorna();
        }
        if (g_rtf && exrtf_vista_clic(&g_vista, EX_X(lp), EX_Y(lp), (wp & KBD_MOD_SHIFT) != 0)) {
            ex_clear_focus(g_f);
            return vista_aggiorna();
        }
        return ex_default_proc(f, msg, wp, lp);
    case EXM_MOUSE_MOVE:
        if (g_rtf && g_rg_cosa != RG_NIENTE) {
            righello_muovi(EX_X(lp), EX_Y(lp));
            return vista_aggiorna();
        }
        if (g_rtf && exrtf_vista_trascina(&g_vista, EX_X(lp), EX_Y(lp))) return vista_aggiorna();
        return ex_default_proc(f, msg, wp, lp);
    case EXM_MOUSE_UP:
        if (g_rtf && g_rg_cosa != RG_NIENTE) {
            righello_muovi(EX_X(lp), EX_Y(lp));
            g_rg_cosa = RG_NIENTE;
            g_rg_tab = 0;
            return vista_aggiorna();
        }
        if (g_rtf) exrtf_vista_su(&g_vista);
        return ex_default_proc(f, msg, wp, lp);
    case EXM_DOUBLE_CLICK:
        if (g_rtf && exrtf_vista_doppio(&g_vista, EX_X(lp), EX_Y(lp))) return vista_aggiorna();
        return ex_default_proc(f, msg, wp, lp);
    case EXM_WHEEL:
        if (g_rtf) { exrtf_vista_rotella(&g_vista, (int)wp); return vista_aggiorna(); }
        return ex_default_proc(f, msg, wp, lp);

    default:
        return ex_default_proc(f, msg, wp, lp);
    }

    /* ! AFTER A BUTTON OF THE TOOL BAR THE KEYS GO BACK TO THE TEXT: the
     * button took the focus with the click, and the next letter typed would
     * press it again instead of being written. Not while one of the two
     * windows is open: the focus is theirs. */
    if (comando && g_rtf && !g_dp && !g_dg) ex_clear_focus(g_f);
    ridisegna();
    return 0;
}

int main(int argc, char **argv)
{
    ExMsg m;


    /* ! EX_AUTO E EX_RESIZABLE, e sono due richieste diverse. EX_AUTO dice «mettila
     * tu», ed e' cio' che permette di aprire due editor senza che il secondo
     * finisca esattamente sopra il primo; EX_RESIZABLE dice che la finestra si puo'
     * tirare per l'angolo, e impegna a rispondere a EXM_SIZE. */
    vedi_leggi();
    {
        unsigned int sw = 0, sh = 0;

        ex_screen_size(&sw, &sh);
        if (sw >= 800) g_w0 = 700;
        if (sh >= 600) g_h0 = 480;
        g_fw = g_w0;
        g_fh = g_h0;
    }

    g_f = ex_create("window", "ExEditor",
                  EX_CAPTION | EX_BORDER | EX_CLOSEBOX | EX_RESIZABLE,
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
    g_menu = ex_menu_bar(g_f);
    ex_menu_add_item(g_menu, "File", "Nuovo\tCtrl+N",    ID_NUOVO);
    ex_menu_add_item(g_menu, "File", "Nuovo documento RTF", ID_NUOVO_RTF);
    ex_menu_add_item(g_menu, "File", "Apri...\tCtrl+O",  ID_APRI);
    ex_menu_add_item(g_menu, "File", "Chiudi\tCtrl+W",   ID_CHIUDI);
    ex_menu_add_item(g_menu, "File", "-",                0);
    ex_menu_add_item(g_menu, "File", "Salva\tCtrl+S",     ID_SALVA);
    ex_menu_add_item(g_menu, "File", "Salva con nome...", ID_SALVACOME);
    ex_menu_add_item(g_menu, "File", "Ricarica",         ID_RICARICA);
    ex_menu_add_item(g_menu, "File", "-",                0);
    ex_menu_add_item(g_menu, "File", "Esci\tCtrl+Q",      ID_ESCI);

    ex_menu_add_item(g_menu, "Modifica", "Annulla\tCtrl+Z", ID_ANNULLA);
    ex_menu_add_item(g_menu, "Modifica", "-",               0);
    ex_menu_add_item(g_menu, "Modifica", "Taglia\tCtrl+X",  ID_TAGLIA);
    ex_menu_add_item(g_menu, "Modifica", "Copia\tCtrl+C",   ID_COPIA);
    ex_menu_add_item(g_menu, "Modifica", "Incolla\tCtrl+V", ID_INCOLLA);
    ex_menu_add_item(g_menu, "Modifica", "-",              0);
    ex_menu_add_item(g_menu, "Modifica", "Cancella\tCanc",  ID_CANCELLA);
    ex_menu_add_item(g_menu, "Modifica", "Seleziona tutto\tCtrl+A", ID_SELTUTTO);
    ex_menu_add_item(g_menu, "Modifica", "-",               0);
    ex_menu_add_item(g_menu, "Modifica", "Cerca...\tCtrl+F", ID_CERCA);
    ex_menu_add_item(g_menu, "Modifica", "Trova successivo\tF3", ID_AVANTI);
    ex_menu_add_item(g_menu, "Modifica", "Trova precedente\tShift+F3", ID_INDIETRO);
    ex_menu_add_item(g_menu, "Modifica", "Sostituisci...\tCtrl+H", ID_SOSTITUISCI);

    ex_menu_add_item(g_menu, "Formato", "Grassetto\tCtrl+B",    ID_M_G);
    ex_menu_add_item(g_menu, "Formato", "Corsivo\tCtrl+I",      ID_M_C);
    ex_menu_add_item(g_menu, "Formato", "Sottolineato\tCtrl+U", ID_M_S);
    ex_menu_add_item(g_menu, "Formato", "-",                    0);
    ex_menu_add_item(g_menu, "Formato", "Allinea a sinistra",   ID_R_SIN);
    ex_menu_add_item(g_menu, "Formato", "Centra",               ID_R_CEN);
    ex_menu_add_item(g_menu, "Formato", "Allinea a destra",     ID_R_DES);
    ex_menu_add_item(g_menu, "Formato", "Giustifica",           ID_R_GIU);
    ex_menu_add_item(g_menu, "Formato", "-",                    0);
    ex_menu_add_item(g_menu, "Formato", "Riduci il rientro",    ID_R_RM);
    ex_menu_add_item(g_menu, "Formato", "Aumenta il rientro",   ID_R_RP);
    ex_menu_add_item(g_menu, "Formato/Interlinea", "Singola",   ID_F_INT1);
    ex_menu_add_item(g_menu, "Formato/Interlinea", "1,5 righe", ID_F_INT15);
    ex_menu_add_item(g_menu, "Formato/Interlinea", "Doppia",    ID_F_INT2);
    ex_menu_add_item(g_menu, "Formato", "Togli le tabulazioni", ID_F_TABVIA);
    ex_menu_add_item(g_menu, "Formato", "-",                    0);
    ex_menu_add_item(g_menu, "Formato", "Paragrafo...",         ID_F_PAR);
    ex_menu_add_item(g_menu, "Formato", "Pagina...",            ID_F_PAG);

    ex_menu_add_item(g_menu, "Visualizza", "Barra degli strumenti", ID_V_BARRA);
    ex_menu_add_item(g_menu, "Visualizza", "Righello",              ID_V_RIGH);
    ex_menu_add_item(g_menu, "Visualizza", "Vista pagina",          ID_V_PAG);
    vedi_segna();

    ex_menu_add_item(g_menu, "Info", "Istruzioni",      ID_ISTRUZIONI);
    ex_menu_add_item(g_menu, "Info", "Informazioni su", ID_INFO);

    /* Le schede: una per file, anche quando il file e' uno solo. */
    g_schede = ex_create("tab", "", EX_CHILD, AREA_X, MENU_H + 2,
                       FIN_W - AREA_X * 2, SCHEDE_H, g_f, ID_SCHEDE, 0);
    ex_items_as_tabs(g_schede, 1);

    g_area = ex_create("textarea", "", EX_CHILD,
                     AREA_X, AREA_Y, FIN_W - AREA_X * 2 - BARRA_W,
                     FIN_H - AREA_Y - BASSO, g_f, 0, 0);
    if (!g_area) {
        printf("exeditor: non riesco a creare l'area di testo\n");
        return 1;
    }

    g_barra = 0;        /* vedi BARRA_W: la barra e' quella dell'area */

    g_stato = ex_create("label", "", EX_CHILD,
                      6, FIN_H - 22, FIN_W - 12, 16, g_f, 0, 0);

    /* The rich text tabs' format bar, hidden while a text tab is chosen. */
    barra_rtf_crea();

    /* ! IL FUOCO ALL'AREA, ESPLICITAMENTE: e' l'unico controllo della finestra
     * che i tasti se li merita. La barra dei menu il fuoco non lo prende — un
     * menu che tenesse la tastiera renderebbe muta l'area — e risponde solo a
     * F10, che a menu chiuso non serve a nessun altro. */
    ex_set_focus(g_area);

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
                printf("exeditor: %s, %u righe%s\n", g_perc, ex_textarea_line_count(g_area),
                       g_parziale ? " (PARZIALE)" : "");
        }
        if (argc < 2) printf("exeditor: file nuovo, senza nome\n");
        if (argc > 2) doc_scegli(0);
    }
    g_avviso[0] = '\0';

    ridisegna();

    while (ex_get_message(&m)) ex_dispatch(&m);
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
