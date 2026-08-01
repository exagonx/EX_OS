/* ============================================================================
 * gf_internal.h
 *
 * Header INTERNO condiviso tra i moduli sorgente di GF_TEXTEDITOR
 * (gf_buffer.c, gf_fileio.c, gf_ui.c, gf_edit.c, gf_texteditor.c).
 *
 * Non e' l'header pubblico dell'oggetto (quello resta gf_texteditor.h,
 * l'unico che chi usa l'editor dall'esterno deve includere): qui ci sono
 * le strutture dati e i prototipi di funzione che i moduli si scambiano
 * tra loro, ma che non fanno parte dell'API pubblica.
 *
 * Autore : Graziano Falcone  <exagonx@hotmail.com>
 * Licenza: GNU GPL v2 (vedi Help->License nel programma, e il file
 *          COPYING distribuito insieme al codice sorgente)
 * ==========================================================================*/
#ifndef GF_INTERNAL_H
#define GF_INTERNAL_H

#include "gf_texteditor.h"

#if defined(_WIN32) || defined(__MINGW32__)
#   define GF_ON_WINDOWS 1
#else
#   define GF_ON_LINUX 1
#endif

#if defined(GF_ON_WINDOWS)
#   ifndef PDC_WIDE
#       define PDC_WIDE   /* PDCursesMod: build con supporto wide-char    */
#   endif
#   ifndef PDC_FORCE_UTF8
#       define PDC_FORCE_UTF8   /* PDCursesMod: interpreta l'input come UTF-8 */
#   endif
#   include <curses.h>          /* fornita da PDCursesMod su Windows      */
#   include <windows.h>         /* per GetKeyState() (stato CAPS/NUM)     */
#   include <direct.h>          /* _getcwd, usata come fallback           */
#else
#   include <ncursesw/ncurses.h>
#   include <sys/ioctl.h>
#   include <linux/kd.h>        /* KDGKBLED: stato hardware CAPS/NUM lock */
#endif

#include <locale.h>
#include <strings.h>  /* strcasecmp */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>

/* ---------------------------------------------------------------------- *
 * Timeout del ciclo principale e coppie di colori ncurses
 * ---------------------------------------------------------------------- */
#define GF_MAIN_TIMEOUT_MS 500

/* Riga di schermo dove inizia/finisce l'area di testo (sopra: menu+tab bar,
 * sotto: status bar). Usate sia dal rendering (gf_ui.c) sia dal calcolo
 * di Pag-Su/Pag-Giu (gf_edit.c). */
#define GF_TEXT_TOP    2
#define GF_TEXT_BOTTOM (LINES - 2)

#define CP_NORMAL        1
#define CP_KEYWORD       2
#define CP_STRING        3
#define CP_COMMENT       4
#define CP_NUMBER        5
#define CP_FUNCTION      6
#define CP_PREPROC       7
#define CP_MENUBAR       8
#define CP_MENUSEL       9
#define CP_STATUSBAR     10
#define CP_TABACTIVE     11
#define CP_TABINACTIVE   12
#define CP_SELECTION     13
#define CP_DIALOG        14

/* Codici tasto "sintetici" per combinazioni non standard, restituiti dal
 * parser di sequenze di escape gf_getch_extended() (definita in
 * gf_texteditor.c) */
enum {
    GF_KEY_CTRL_HOME = 1001,
    GF_KEY_CTRL_END,
    GF_KEY_SHIFT_HOME,
    GF_KEY_SHIFT_END,
    GF_KEY_SHIFT_F3,
    GF_KEY_ALT_F,
    GF_KEY_ALT_M,
    GF_KEY_ALT_O,
    GF_KEY_ALT_H
};

/* Una voce restituita dallo sfoglia-directory (gf_fileio.c), usata anche
 * dal dialog di navigazione in gf_ui.c */
typedef struct {
    char name[256];
    int  is_dir;
} GF_DIRENTRY;

/* ============================================================================
 *  gf_buffer.c - buffer riga/documento, UTF-8, undo, clipboard, selezione
 * ==========================================================================*/
int   gf_is_utf8_cont(unsigned char b);
int   gf_utf8_seqlen(unsigned char lead);
int   gf_utf8_prev_boundary(GF_LINE *ln, int col);
int   gf_utf8_next_boundary(GF_LINE *ln, int col);
int   gf_utf8_count_chars(GF_LINE *ln, int from_byte, int to_byte);

void  gf_line_init(GF_LINE *ln);
void  gf_line_free(GF_LINE *ln);
void  gf_line_ensure_capacity(GF_LINE *ln, int needed);
void  gf_line_set(GF_LINE *ln, const char *s);
void  gf_line_insert_char(GF_LINE *ln, int col, char c);
void  gf_line_delete_char(GF_LINE *ln, int col);

void  gf_doc_init(GF_DOCBUFFER *doc);
void  gf_doc_free(GF_DOCBUFFER *doc);
void  gf_doc_ensure_capacity(GF_DOCBUFFER *doc, int needed);
void  gf_doc_insert_line(GF_DOCBUFFER *doc, int idx, const char *text);
void  gf_doc_delete_line(GF_DOCBUFFER *doc, int idx);
void  gf_doc_deep_copy(GF_DOCBUFFER *dst, const GF_DOCBUFFER *src);

void  gf_area_free_undo_stack(GF_FILEAREA *area);
void  gf_area_push_undo(GF_FILEAREA *area);
void  gf_area_reset(GF_FILEAREA *area, GF_LANGUAGE lang);
int   gf_find_free_area_index(GF_TEXTEDITOR *self);
GF_FILEAREA *gf_current(GF_TEXTEDITOR *self);

void  gf_get_selection_bounds(GF_FILEAREA *area, int *r1, int *c1, int *r2, int *c2);
void  gf_clipboard_set(GF_CLIPBOARD *cb, const char *data, int len);
char *gf_extract_range(GF_FILEAREA *area, int r1, int c1, int r2, int c2);
void  gf_delete_range(GF_FILEAREA *area, int r1, int c1, int r2, int c2);

/* ============================================================================
 *  gf_fileio.c - caricamento/salvataggio su disco, sfoglia-directory
 * ==========================================================================*/
GF_LANGUAGE gf_detect_language(const char *path);
int   gf_list_directory(const char *path, GF_DIRENTRY **out_entries);
void  gf_path_parent(char *path);
int   gf_load_file_into_area(GF_FILEAREA *area, const char *path);
int   gf_save_area_to_disk(GF_FILEAREA *area);

/* ============================================================================
 *  gf_ui.c - dialoghi, rendering, sistema di menu a tendina
 * ==========================================================================*/
void  gf_dialog_message(const char *title, const char **lines, int nlines);
int   gf_dialog_prompt_string(const char *title, const char *label, char *outbuf, int outbuf_size);
int   gf_dialog_choice(const char *title, const char *message, const char **options, int noptions);
int   gf_dialog_browse(GF_TEXTEDITOR *self, const char *title, char *out_path, int out_size, int allow_new_name);
void  gf_dialog_scrollable_text(const char *title, const char **lines, int nlines);
void  gf_init_colors(void);
void  gf_draw_all(GF_TEXTEDITOR *self);
void  gf_refresh_clock(GF_TEXTEDITOR *self);
void  gf_run_menu(GF_TEXTEDITOR *self, int start_menu);

/* ============================================================================
 *  gf_edit.c - movimento cursore, editing, find/replace, syntax highlight,
 *              azioni dei menu File/Modifica/Option/Help
 * ==========================================================================*/
void  gf_request_rehighlight(GF_TEXTEDITOR *self);
void *gf_highlight_thread_func(void *arg);
void  gf_handle_edit_key(GF_TEXTEDITOR *self, int ch);
void  gf_next_tab(GF_TEXTEDITOR *self);
void  gf_prev_tab(GF_TEXTEDITOR *self);

void  gf_action_new(GF_TEXTEDITOR *self);
void  gf_action_open(GF_TEXTEDITOR *self);
void  gf_action_save(GF_TEXTEDITOR *self);
void  gf_action_save_as(GF_TEXTEDITOR *self);
void  gf_action_close(GF_TEXTEDITOR *self);
void  gf_action_exit(GF_TEXTEDITOR *self);
void  gf_action_copy(GF_TEXTEDITOR *self);
void  gf_action_cut(GF_TEXTEDITOR *self);
void  gf_action_paste(GF_TEXTEDITOR *self);
void  gf_action_delete_selection(GF_TEXTEDITOR *self);
void  gf_action_undo(GF_TEXTEDITOR *self);
void  gf_action_find(GF_TEXTEDITOR *self);
void  gf_action_replace(GF_TEXTEDITOR *self);
void  gf_action_set_tabwidth(GF_TEXTEDITOR *self);
void  gf_action_set_autosave(GF_TEXTEDITOR *self);
void  gf_action_set_color(GF_TEXTEDITOR *self);
void  gf_action_toggle_developing(GF_TEXTEDITOR *self);
void  gf_action_set_language(GF_TEXTEDITOR *self);
void  gf_action_help_instructions(GF_TEXTEDITOR *self);
void  gf_action_help_about(GF_TEXTEDITOR *self);
void  gf_action_help_license(GF_TEXTEDITOR *self);

/* ============================================================================
 *  gf_texteditor.c - lettura tasti di basso livello, stato tasti, core
 * ==========================================================================*/
int   gf_getch_extended(void);
int   gf_get_kb_led_state(int *caps_on, int *num_on);

#endif /* GF_INTERNAL_H */
