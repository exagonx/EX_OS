/* =============================================================================
 * bin/gfedit/gfedit.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * GF Edit — editor di testo a schermo intero per EX-OS (/bin/gfedit)
 *
 * Riscrittura per EX-OS di GF_TEXTEDITOR, l'editor ncurses+pthread dello
 * stesso autore. Non è un porting: delle quattro fondamenta su cui
 * poggiava l'originale, EX-OS non ne ha nessuna.
 *
 *   ncurses/PDCurses  ->  gf_term.c, uno strato di ~400 righe che parla
 *                         al TTY in sequenze ANSI e tiene un frame
 *                         ombra per ridisegnare solo ciò che cambia
 *   pthread           ->  niente thread. L'evidenziazione sintattica è
 *                         sincrona (si calcola solo per le 21 righe
 *                         visibili, quindi costa poco), l'orologio
 *                         diventa l'uptime aggiornato a ogni tasto e
 *                         l'autosalvataggio si misura sull'uptime
 *                         invece che su un thread che conta i secondi
 *   stdio POSIX       ->  open/read/write/listdir della libc di EX-OS
 *   malloc/realloc/   ->  la free() di EX-OS è un no-op dichiarato
 *   free intensivi        (allocatore a bump su sbrk, vedi lib/libc.c).
 *                         Qui si alloca UNA volta per area, alla prima
 *                         apertura, e non si libera mai: è l'unico
 *                         schema che con un allocatore a bump non perde
 *                         memoria. Vedi GfTabData.
 *
 * Il resto — il modello a oggetti con il puntatore a funzione 'run', i
 * menu a tendina in stile MS-DOS EDIT, le aree multiple, find/replace,
 * l'undo, i linguaggi riconosciuti — è quello dell'originale.
 *
 * ▲ INCREMENTARE GF_VERSION DI 0.001 A OGNI MODIFICA ▲
 * Stessa regola e stesso formato della versione del kernel
 * (kernel/include/version.h): stringa con tre decimali, non un numero —
 * EX-OS non usa la virgola mobile.
 * ============================================================================= */

#ifndef GFEDIT_H
#define GFEDIT_H

#include "libc.h"
#include "kbd_proto.h"

#define GF_NAME     "GF Edit"
#define GF_VERSION  "0.001"
#define GF_AUTHOR   "Graziano Falcone"
#define GF_EMAIL    "exagonx@hotmail.com"
#define GF_LICENSE  "GPL 2.0"

/* =============================================================================
 * Geometria dello schermo
 *
 * Fissa a 80x25 e non chiesta al TTY: il VGA in modo testo di EX-OS non
 * ha altre dimensioni, e tty_getsize() risponde queste due costanti.
 * Chiederle a runtime darebbe l'illusione di un editor ridimensionabile
 * che poi non lo è — le tabelle qui sotto sono dimensionate a compilazione.
 *
 * Righe:  0  barra dei menu
 *         1  barra delle aree aperte
 *      2..22 testo (GF_TEXT_ROWS = 21 righe)
 *        23  barra di stato
 *        24  riga dei messaggi
 * ============================================================================= */
#define GF_ROWS         25
#define GF_COLS         80
#define GF_TEXT_TOP     2
#define GF_TEXT_BOTTOM  22
#define GF_TEXT_ROWS    (GF_TEXT_BOTTOM - GF_TEXT_TOP + 1)
#define GF_ROW_MENU     0
#define GF_ROW_TABS     1
#define GF_ROW_STATUS   23
#define GF_ROW_MSG      24

/* =============================================================================
 * Limiti del documento
 *
 * Il costo in memoria è tutto qui: una GfTabData è
 *   GF_MAX_LINES * GF_LINE_STRIDE + GF_UNDO_MAX * sizeof(GfUndo)
 * cioè ~120 KB, allocata alla prima apertura di un'area e mai liberata.
 * Con otto aree aperte insieme si arriva a ~1 MB su 32 MB di RAM.
 *
 * Le righe sono slot a lunghezza fissa, non stringhe riallocate: senza
 * una free() vera, ogni modifica a una riga perderebbe per sempre la
 * memoria della versione precedente — e un editor è fatto apposta per
 * modificare le righe molte volte. È la stessa scelta, e per lo stesso
 * motivo, di /bin/textline.
 * ============================================================================= */
#define GF_MAX_TABS     8
#define GF_MAX_LINES    512
#define GF_MAX_COL      200                 /* caratteri utili per riga */
#define GF_LINE_STRIDE  (GF_MAX_COL + 8)    /* +NUL e margine di allineamento */
#define GF_UNDO_MAX     64
#define GF_MAX_PATH     128
#define GF_MAX_NAME     64
#define GF_CLIP_MAX     4096
#define GF_FIND_MAX     64
#define GF_MAX_MATCHES  512
#define GF_DEFAULT_TAB  4

/* =============================================================================
 * Attributo di colore: un byte VGA, sfondo nei bit alti
 *
 * Lo sfondo è limitato a 0-7 di proposito. I colori 8-15 come SFONDO non
 * sono rappresentabili in una sequenza ANSI che vga.c sappia leggere
 * (40-47 coprono solo la serie scura), e sul VGA in modo testo il bit
 * alto dello sfondo è comunque il lampeggio, non un nono colore.
 * ============================================================================= */
#define GF_AT(fg, bg)   ((unsigned char)(((fg) & 0x0F) | (((bg) & 0x07) << 4)))

/* Colori VGA, ripetuti qui perché kernel/include/vga.h non è
 * compilabile in ring3 (include kernel.h). */
#define GF_NERO         0
#define GF_BLU          1
#define GF_VERDE        2
#define GF_CIANO        3
#define GF_ROSSO        4
#define GF_MAGENTA      5
#define GF_MARRONE      6
#define GF_GRIGIO       7
#define GF_GRIGIO_SCURO 8
#define GF_BLU_CH       9
#define GF_VERDE_CH     10
#define GF_CIANO_CH     11
#define GF_ROSSO_CH     12
#define GF_MAGENTA_CH   13
#define GF_GIALLO       14
#define GF_BIANCO       15

/* Tavolozza dell'interfaccia — l'aspetto è quello di MS-DOS EDIT:
 * testo su fondo blu, barre su fondo grigio. */
#define CP_TESTO        GF_AT(GF_GRIGIO,      GF_BLU)
#define CP_KEYWORD      GF_AT(GF_BIANCO,      GF_BLU)
#define CP_STRINGA      GF_AT(GF_GIALLO,      GF_BLU)
#define CP_COMMENTO     GF_AT(GF_VERDE_CH,    GF_BLU)
#define CP_NUMERO       GF_AT(GF_MAGENTA_CH,  GF_BLU)
#define CP_FUNZIONE     GF_AT(GF_CIANO_CH,    GF_BLU)
#define CP_PREPROC      GF_AT(GF_ROSSO_CH,    GF_BLU)
#define CP_SELEZIONE    GF_AT(GF_BLU,         GF_GRIGIO)
#define CP_MENU         GF_AT(GF_NERO,        GF_GRIGIO)
#define CP_MENU_SEL     GF_AT(GF_GRIGIO,      GF_NERO)
#define CP_MENU_TASTO   GF_AT(GF_ROSSO,       GF_GRIGIO)
#define CP_STATO        GF_AT(GF_NERO,        GF_GRIGIO)
#define CP_TAB_ATTIVA   GF_AT(GF_BIANCO,      GF_BLU)
#define CP_TAB_INATTIVA GF_AT(GF_GRIGIO_SCURO,GF_BLU)
#define CP_DIALOGO      GF_AT(GF_NERO,        GF_GRIGIO)
#define CP_DLG_TITOLO   GF_AT(GF_BIANCO,      GF_ROSSO)
#define CP_DLG_CAMPO    GF_AT(GF_BIANCO,      GF_NERO)
#define CP_MSG          GF_AT(GF_GIALLO,      GF_NERO)

/* =============================================================================
 * Linguaggi riconosciuti e classi di token
 * ============================================================================= */
typedef enum {
    GF_LANG_NONE = 0,
    GF_LANG_C,
    GF_LANG_CPP,
    GF_LANG_BASIC,
    GF_LANG_ASM
} GfLingua;

typedef enum {
    GF_TOK_NORMALE = 0,
    GF_TOK_KEYWORD,
    GF_TOK_STRINGA,
    GF_TOK_COMMENTO,
    GF_TOK_NUMERO,
    GF_TOK_FUNZIONE,
    GF_TOK_PREPROC
} GfToken;

/* =============================================================================
 * Undo — giornale di operazioni, non istantanee del documento
 *
 * L'originale teneva otto COPIE COMPLETE del buffer per area. Qui
 * costerebbero 800 KB per area, e con otto aree si arriverebbe a più
 * memoria di quanta ne usi tutto il resto del sistema. Il giornale
 * registra invece la singola riga toccata, che per un editor di testo è
 * l'unità naturale di modifica.
 *
 * Le operazioni composte (incolla, cancella selezione, sostituisci
 * tutto) non sono un caso a parte: emettono più record legati dallo
 * stesso numero di GRUPPO, e l'annullamento ne consuma l'intero gruppo.
 * Il numero di gruppo è monotono e non torna mai indietro, quindi due
 * gruppi distinti non possono confondersi nemmeno quando il giornale
 * gira su sé stesso.
 *
 * Quando il giornale è pieno il record più vecchio viene scartato: si
 * perde la coda della storia, mai la testa. Un gruppo troncato a metà
 * viene riconosciuto e l'annullamento si ferma lì invece di applicare
 * mezza operazione.
 * ============================================================================= */
typedef enum {
    GF_U_MODIFICA = 0,   /* la riga 'row' conteneva 'text' */
    GF_U_INSERITA,       /* la riga 'row' è stata inserita: va tolta */
    GF_U_CANCELLATA      /* la riga 'row' conteneva 'text' ed è stata tolta */
} GfUndoOp;

typedef struct {
    unsigned char op;
    unsigned int  gruppo;
    int           row;
    int           cur_row, cur_col;   /* dov'era il cursore prima */
    char          text[GF_LINE_STRIDE];
} GfUndo;

/* =============================================================================
 * Dati pesanti di un'area, allocati una volta sola con malloc()
 *
 * Separati da GfTab perché GfTab sta nell'oggetto principale (in BSS) e
 * deve restare piccola: otto aree da 120 KB in BSS sarebbero un
 * megabyte di immagine da azzerare al caricamento anche per aprire un
 * file solo.
 * ============================================================================= */
typedef struct {
    char   text[GF_MAX_LINES][GF_LINE_STRIDE];
    GfUndo undo[GF_UNDO_MAX];
} GfTabData;

/* =============================================================================
 * Un'area (tab): un file aperto
 * ============================================================================= */
typedef struct {
    int        in_use;
    char       filename[GF_MAX_NAME];
    char       filepath[GF_MAX_PATH];
    int        has_path;              /* 0 = mai salvato su disco */
    GfTabData *d;                     /* NULL finché l'area non è mai servita */

    int num_lines;
    int modified;
    int insert_mode;                  /* 1 = inserimento, 0 = sovrascrittura */
    int troncato;                     /* 1 = al caricamento si è perso testo */

    int cursor_row, cursor_col;
    int view_top,   view_left;

    /* Selezione: un'ancora più il cursore. L'originale teneva due
     * coppie di coordinate, ma una delle due era sempre il cursore. */
    int sel_active;
    int sel_row, sel_col;

    GfLingua lingua;

    /* Riga per riga: 1 se la riga COMINCIA dentro un commento di
     * blocco. È tutto lo stato multi-riga di cui l'evidenziatore ha
     * bisogno, e permette di colorare le sole righe visibili senza
     * rianalizzare il documento dall'inizio a ogni ridisegno — che è il
     * lavoro per cui l'originale accendeva un thread. */
    unsigned char in_commento[GF_MAX_LINES];

    int          undo_head;           /* prossimo slot da scrivere */
    int          undo_count;          /* record validi (0..GF_UNDO_MAX) */
    unsigned int undo_gruppo;         /* numero del gruppo in corso */
    unsigned int undo_incompleto;     /* gruppo di cui il giornale ha perso pezzi */

    /* Le funzioni che modificano il documento registrano l'undo da sole:
     * lasciarlo al chiamante significherebbe che basta dimenticarsene in
     * un posto perché l'annullamento rovini il file invece di ripararlo.
     * Questo interruttore serve a chi NON deve registrare — il
     * caricamento da disco riempie il documento riga per riga, e senza
     * spegnerlo il giornale nascerebbe già pieno della storia di un
     * file appena aperto. Lo spegne anche gf_undo_applica: annullare un
     * annullamento non è un rifare, è un anello. */
    int          undo_attivo;
} GfTab;

/* =============================================================================
 * Appunti e stato di ricerca
 * ============================================================================= */
typedef struct {
    char data[GF_CLIP_MAX];
    int  len;
    int  a_righe;    /* 1 = il taglio comprendeva righe intere */
} GfClipboard;

typedef struct {
    char needle[GF_FIND_MAX];
    char replacement[GF_FIND_MAX];
    int  ignora_caso;
    int  match_row[GF_MAX_MATCHES];
    int  match_col[GF_MAX_MATCHES];
    int  n_match;
    int  match_corrente;
} GfFind;

/* =============================================================================
 * Opzioni
 * ============================================================================= */
typedef struct {
    int tab_width;
    int developing;        /* 1 = evidenziazione sintattica attiva */
    int autosave_sec;      /* 0 = disattivo */
    int mostra_barra_msg;
} GfOpzioni;

/* =============================================================================
 * L'oggetto principale
 *
 * Il puntatore a funzione 'run' è quello dell'originale: in C non c'è
 * overloading a oggetti, e l'illusione si ottiene salvando il metodo
 * dentro la struttura. Si usa allo stesso modo:
 *
 *     GfEdit *ed = gf_costruttore();
 *     ed->newopenfile(ed, "/prova.txt");
 *     ed->run(ed);
 *     gf_distruttore(ed);
 * ============================================================================= */
typedef struct GfEdit {
    GfTab       tabs[GF_MAX_TABS];
    int         tab_corrente;
    int         n_aperte;

    GfOpzioni   opz;
    GfClipboard clip;
    GfFind      find;

    int          running;
    unsigned int caratteri_digitati;
    unsigned int ultimo_autosave_ms;

    char directory[GF_MAX_PATH];    /* directory di lavoro dei dialoghi */
    char messaggio[GF_COLS];        /* riga 24 */

    void (*run)(struct GfEdit *self);
    void (*newopenfile)(struct GfEdit *self, const char *filepath);
} GfEdit;

/* =============================================================================
 * API pubblica (gf_main.c / gf_core.c)
 * ============================================================================= */
GfEdit *gf_costruttore(void);
void    gf_distruttore(GfEdit *self);
void    gf_run(GfEdit *self);
void    gf_newopenfile(GfEdit *self, const char *filepath);

/* =============================================================================
 * gf_term.c — lo strato che sostituisce ncurses
 * ============================================================================= */
int      gf_term_init(void);              /* 0 = ok, <0 = errore */
void     gf_term_fine(void);
void     gf_term_pulisci(unsigned char attr);
void     gf_term_cella(int r, int c, char ch, unsigned char attr);
void     gf_term_scrivi(int r, int c, const char *s, unsigned char attr);
void     gf_term_scrivi_n(int r, int c, const char *s, int n, unsigned char attr);
void     gf_term_riempi(int r, int c, int n, char ch, unsigned char attr);
void     gf_term_riquadro(int r, int c, int h, int w, unsigned char attr);
void     gf_term_cursore(int r, int c, int visibile);
void     gf_term_flush(void);
unsigned gf_getkey(void);

/* Piccole utilità di formattazione: la libc di EX-OS non ha snprintf, e
 * costruire le barre di stato concatenando a mano sarebbe illeggibile.
 * Supporta %s %d %c %% e la larghezza minima (%5d). */
int  gf_fmt(char *buf, int size, const char *fmt, ...);
void gf_strlcpy(char *dst, const char *src, int size);

/* =============================================================================
 * gf_buffer.c — documento, righe, selezione, undo, appunti
 * ============================================================================= */
GfTab *gf_corrente(GfEdit *self);
char  *gf_riga(GfTab *t, int i);
int    gf_riga_len(GfTab *t, int i);
int    gf_tab_prepara(GfTab *t);                 /* alloca d, 0 = ok */
void   gf_tab_azzera(GfTab *t, GfLingua lingua);
int    gf_tab_libera_indice(GfEdit *self);

void   gf_riga_imposta(GfTab *t, int i, const char *s);
int    gf_riga_inserisci(GfTab *t, int pos, const char *s);
int    gf_riga_cancella(GfTab *t, int pos);

void   gf_undo_apri(GfTab *t);
int    gf_undo_applica(GfTab *t);    /* 0 ok, -1 niente da annullare, -2 storia incompleta */

/* Colonna di SCHERMO di una posizione in byte, tabulazioni espanse.
 * Documento e schermo non usano la stessa unità — nel documento il
 * cursore sta al byte N, a video una tabulazione può valerne otto — e
 * ogni conversione fra i due mondi passa da qui, così la regola di
 * espansione è scritta una volta sola invece che una per modulo. */
int    gf_col_schermo(GfEdit *self, const char *s, int byte_col);

void   gf_sel_normalizza(GfTab *t, int *r1, int *c1, int *r2, int *c2);
int    gf_sel_estrai(GfTab *t, char *out, int max);
void   gf_sel_cancella(GfEdit *self);
void   gf_clip_imposta(GfEdit *self, const char *data, int len, int a_righe);

/* =============================================================================
 * gf_fileio.c
 * ============================================================================= */
GfLingua gf_rileva_lingua(const char *path);
int      gf_carica(GfTab *t, const char *path);   /* 0 ok, -1 assente */
int      gf_salva(GfTab *t);                      /* 0 ok, <0 errno */
void     gf_path_padre(char *path);
void     gf_path_unisci(char *dst, int size, const char *dir, const char *nome);
const char *gf_basename(const char *path);

/* =============================================================================
 * gf_syntax.c
 * ============================================================================= */
void gf_ricalcola_commenti(GfTab *t);
void gf_evidenzia_riga(GfTab *t, int idx, unsigned char *out, int max);

/* =============================================================================
 * gf_ui.c — disegno e dialoghi
 * ============================================================================= */
void gf_componi(GfEdit *self);   /* riempie il frame ombra, non riversa a video */
void gf_disegna(GfEdit *self);   /* gf_componi + flush */
void gf_msg(GfEdit *self, const char *s);
void gf_dlg_messaggio(GfEdit *self, const char *titolo, const char **righe, int n);
int  gf_dlg_conferma(GfEdit *self, const char *titolo, const char *testo,
                     const char **opzioni, int n_opzioni);
int  gf_dlg_stringa(GfEdit *self, const char *titolo, const char *etichetta,
                    char *buf, int size);
int  gf_dlg_testo_scorrevole(GfEdit *self, const char *titolo,
                             const char **righe, int n);
int  gf_dlg_sfoglia(GfEdit *self, const char *titolo, char *out, int size,
                    int permetti_nuovo);
void gf_menu(GfEdit *self, int menu_iniziale);
int  gf_menu_da_lettera(char ch);   /* indice del menu di Alt+<lettera>, -1 se nessuno */

/* =============================================================================
 * gf_edit.c — movimento, editing, azioni
 * ============================================================================= */
void gf_tasto(GfEdit *self, unsigned key);
void gf_aggiusta_vista(GfEdit *self);
void gf_tab_successiva(GfEdit *self);
void gf_tab_precedente(GfEdit *self);

void gf_az_nuovo(GfEdit *self);
void gf_az_apri(GfEdit *self);
void gf_az_salva(GfEdit *self);
void gf_az_salva_come(GfEdit *self);
void gf_az_chiudi(GfEdit *self);
void gf_az_esci(GfEdit *self);
void gf_az_copia(GfEdit *self);
void gf_az_taglia(GfEdit *self);
void gf_az_incolla(GfEdit *self);
void gf_az_annulla(GfEdit *self);
void gf_az_seleziona_tutto(GfEdit *self);
void gf_az_cerca(GfEdit *self);
void gf_az_cerca_avanti(GfEdit *self);
void gf_az_cerca_indietro(GfEdit *self);
void gf_az_sostituisci(GfEdit *self);
void gf_az_vai_a_riga(GfEdit *self);
void gf_az_tab_width(GfEdit *self);
void gf_az_autosave(GfEdit *self);
void gf_az_developing(GfEdit *self);
void gf_az_lingua(GfEdit *self);
void gf_az_istruzioni(GfEdit *self);
void gf_az_info(GfEdit *self);
void gf_az_licenza(GfEdit *self);

/* =============================================================================
 * Piccole utilità condivise (gf_buffer.c)
 * ============================================================================= */
int  gf_e_spazio(char c);
int  gf_e_cifra(char c);
int  gf_e_alpha(char c);
int  gf_e_ident(char c);
char gf_minuscolo(char c);
int  gf_str_uguale_ci(const char *a, const char *b);

#endif /* GFEDIT_H */
