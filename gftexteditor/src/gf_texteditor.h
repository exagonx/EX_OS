/* ============================================================================
 * gf_texteditor.h
 *
 * GF_TEXTEDITOR - Editor di testo a schermo intero per terminale Linux
 * (ncurses + pthread), stile MS-DOS EDIT.
 *
 * Autore : Graziano Falcone
 * Email  : exagonx@hotmail.com
 * Sito   : exagonx.altervista.org
 * Licenza: GNU GPL v2
 *
 * Uso tipico (stile "oggetto"):
 *
 *     GF_TEXTEDITOR *MyTextEditor = gf_texteditor_constructor();
 *     MyTextEditor->run(MyTextEditor);
 *     gf_texteditor_destroy(MyTextEditor);
 *
 * In C non esiste overloading a oggetti reale: l'illusione e' ottenuta
 * salvando un puntatore a funzione "run" dentro la struttura, assegnato
 * dal costruttore. Tutte le altre operazioni sono funzioni gf_texteditor_*
 * che ricevono sempre il puntatore all'oggetto come primo parametro
 * (equivalente del "this").
 * ==========================================================================*/

#ifndef GF_TEXTEDITOR_H
#define GF_TEXTEDITOR_H

#include <pthread.h>

/* ---------------------------------------------------------------------- *
 * Limiti e costanti di configurazione
 * ---------------------------------------------------------------------- */
#define GF_MAX_TABS          8      /* numero massimo di file/aree aperte  */
#define GF_MAX_FILENAME      256
#define GF_MAX_FILEPATH      1024
#define GF_MAX_FINDSTR       256
#define GF_UNDO_MAX          8      /* livelli di rollback (Undo) per area */
#define GF_DEFAULT_TABWIDTH  4
#define GF_MAX_MATCHES       4096   /* occorrenze massime tracciate da Find */

/* ---------------------------------------------------------------------- *
 * Linguaggi riconosciuti in modalita' "developing" (syntax highlighting)
 * ---------------------------------------------------------------------- */
typedef enum {
    GF_LANG_NONE = 0,
    GF_LANG_C,
    GF_LANG_CPP,
    GF_LANG_BASIC
} GF_LANGUAGE;

/* Classi di token riconosciute dall'analizzatore sintattico */
typedef enum {
    GF_TOK_NORMAL = 0,
    GF_TOK_KEYWORD,
    GF_TOK_STRING,
    GF_TOK_COMMENT,
    GF_TOK_NUMBER,
    GF_TOK_FUNCTION,
    GF_TOK_PREPROC
} GF_TOKENCLASS;

/* ---------------------------------------------------------------------- *
 * Una riga del documento (buffer documento = elenco di GF_LINE)
 * ---------------------------------------------------------------------- */
typedef struct {
    char *text;             /* contenuto della riga, terminato da '\0'    */
    int   length;            /* numero di caratteri validi in text         */
    int   capacity;          /* dimensione allocata di text                */
    unsigned char *hl;       /* classe di evidenziazione per ogni carattere*/
    int   hl_capacity;       /* dimensione allocata di hl                  */
} GF_LINE;

/* Buffer documento: intero contenuto del file in memoria */
typedef struct {
    GF_LINE *lines;
    int      num_lines;
    int      capacity;
} GF_DOCBUFFER;

/* ---------------------------------------------------------------------- *
 * Un'area/tab: rappresenta un singolo file aperto
 * ---------------------------------------------------------------------- */
typedef struct {
    int           in_use;                     /* area occupata o libera   */
    char          filename[GF_MAX_FILENAME];  /* solo nome file           */
    char          filepath[GF_MAX_FILEPATH];  /* percorso completo        */
    int           has_path;                   /* 0 = mai salvato su disco */
    GF_DOCBUFFER  doc;                         /* buffer documento         */
    int           modified;                    /* modifiche non salvate    */
    int           insert_mode;                 /* 1 = INS attivo           */

    /* Cursore nel documento (coordinate logiche, non di schermo) */
    int cursor_row;
    int cursor_col;

    /* Buffer di visualizzazione: offset di scroll della finestra di testo */
    int view_top;
    int view_left;

    /* Selezione testo corrente */
    int sel_active;
    int sel_start_row, sel_start_col;
    int sel_end_row,   sel_end_col;

    /* Linguaggio per syntax highlighting */
    GF_LANGUAGE language;

    /* --- rollback (Undo): ring buffer degli ultimi GF_UNDO_MAX stati del
     * documento, presi come istantanea COMPLETA prima di ogni modifica.
     * undo_head e' l'indice del PROSSIMO slot libero da scrivere;
     * undo_count e' quanti stati validi sono attualmente memorizzati
     * (0..GF_UNDO_MAX). Quando il ring e' pieno, il piu' vecchio viene
     * scartato per far posto al nuovo. */
    GF_DOCBUFFER undo_stack[GF_UNDO_MAX];
    int          undo_cursor_row[GF_UNDO_MAX];
    int          undo_cursor_col[GF_UNDO_MAX];
    int          undo_head;
    int          undo_count;
} GF_FILEAREA;

/* ---------------------------------------------------------------------- *
 * Appunti (clipboard) condivisi tra le aree
 * ---------------------------------------------------------------------- */
typedef struct {
    char *data;
    int   length;
    int   capacity;
} GF_CLIPBOARD;

/* ---------------------------------------------------------------------- *
 * Stato di ricerca/sostituzione (Find / Replace, F3 / Shift+F3)
 * ---------------------------------------------------------------------- */
typedef struct {
    char needle[GF_MAX_FINDSTR];
    char replacement[GF_MAX_FINDSTR];
    int  needle_len;

    /* posizioni di tutte le occorrenze trovate nel documento corrente */
    int  match_row[GF_MAX_MATCHES];
    int  match_col[GF_MAX_MATCHES];
    int  num_matches;
    int  current_match;     /* indice (0-based) dell'occorrenza corrente  */
} GF_FINDSTATE;

/* ---------------------------------------------------------------------- *
 * Opzioni configurabili dal menu Option
 * ---------------------------------------------------------------------- */
typedef struct {
    int tab_width;          /* numero di spazi per tabulazione            */
    int text_color_pair;    /* coppia colore ncurses per testo normale    */
    int developing_mode;    /* 1 = syntax highlighting attivo             */
    int autosave_seconds;   /* intervallo di autosalvataggio, 0=disattivo */
} GF_OPTIONS;

/* ---------------------------------------------------------------------- *
 * Oggetto principale GF_TEXTEDITOR
 * ---------------------------------------------------------------------- */
typedef struct GF_TEXTEDITOR {
    GF_FILEAREA tabs[GF_MAX_TABS];
    int         current_tab;
    int         num_open;

    GF_OPTIONS    options;
    GF_CLIPBOARD  clipboard;
    GF_FINDSTATE  find;

    volatile int  running;             /* 0 = richiesta di uscita          */
    unsigned long chars_typed;         /* contatore caratteri digitati     */

    /* --- sincronizzazione con il thread di analisi sintattica --- */
    pthread_t        highlight_thread;
    pthread_mutex_t  buffer_mutex;
    pthread_cond_t   highlight_cond;
    volatile int     highlight_dirty;     /* richiesta di rianalisi        */
    volatile int     thread_should_exit;  /* condiviso da entrambi i thread */

    /* --- thread dedicato all'aggiornamento di data/ora in autonomia ---
     * Opera indipendentemente dall'input utente: ogni secondo "sveglia"
     * il ciclo principale (bloccato in attesa di un tasto) cosi' che la
     * barra di stato venga ridisegnata e mostri l'ora corrente anche
     * senza che l'utente prema alcun tasto. */
    pthread_t        clock_thread;
    char             clock_string[32];   /* mantenuta dal thread orologio, protetta da buffer_mutex */
    volatile int     autosave_elapsed;   /* secondi trascorsi dall'ultimo autosalvataggio */

    /* directory "corrente": usata da New per creare il file li', e come
     * punto di partenza quando si apre lo sfoglia-directory in Open e
     * Save As. Viene aggiornata ogni volta che l'utente naviga altrove. */
    char             current_directory[GF_MAX_FILEPATH];

    /* --- puntatore a funzione "metodo" run, assegnato dal costruttore --- */
    void (*run)(struct GF_TEXTEDITOR *self);

    /* --- apre un file se esiste, altrimenti prepara una nuova area gia'
     * destinata a quel percorso (creata al primo Save): utile per aprire
     * un file specifico prima ancora di chiamare run(). --- */
    void (*newopenfile)(struct GF_TEXTEDITOR *self, const char *filepath);
} GF_TEXTEDITOR;

/* ---------------------------------------------------------------------- *
 * API pubblica
 * ---------------------------------------------------------------------- */
GF_TEXTEDITOR *gf_texteditor_constructor(void);
void           gf_texteditor_destroy(GF_TEXTEDITOR *self);

/* gf_texteditor_run e' anche accessibile via self->run(self) */
void gf_texteditor_run(GF_TEXTEDITOR *self);

/* Se si vuole aprire subito un file passandolo da linea di comando */
int gf_texteditor_open_file(GF_TEXTEDITOR *self, const char *filepath);

/* Apre "filepath" se esiste gia'; altrimenti prepara una nuova area vuota
 * gia' destinata a quel percorso (verra' creato su disco al primo Save).
 * Accessibile anche via self->newopenfile(self, filepath). */
void gf_texteditor_newopenfile(GF_TEXTEDITOR *self, const char *filepath);

#endif /* GF_TEXTEDITOR_H */
