/* ============================================================================
 * gf_ui.c
 *
 * Parte dell'implementazione di GF_TEXTEDITOR - vedi gf_texteditor.h per
 * la descrizione dell'oggetto e gf_internal.h per le strutture e i
 * prototipi condivisi tra i moduli.
 *
 * Finestre di dialogo modali (messaggio, prompt, scelta multipla, sfoglia-
 * directory, testo scorrevole), rendering completo dello schermo (barra
 * menu, barra schede, barra di stato, area di testo) e sistema di menu a
 * tendina con il relativo dispatch verso le azioni implementate in gf_edit.c.
 *
 * Autore : Graziano Falcone  <exagonx@hotmail.com>
 * Licenza: GNU GPL v2 (vedi Help->License nel programma, e il file
 *          COPYING distribuito insieme al codice sorgente)
 * ==========================================================================*/

#define _GNU_SOURCE
#include "gf_internal.h"

void gf_dialog_message(const char *title, const char **lines, int nlines)
{
    int maxw = (int)strlen(title) + 4;
    int i;
    for (i = 0; i < nlines; i++) {
        int l = (int)strlen(lines[i]);
        if (l + 4 > maxw) maxw = l + 4;
    }
    int h = nlines + 4;
    int scr_h, scr_w;
    getmaxyx(stdscr, scr_h, scr_w);
    if (maxw > scr_w - 4) maxw = scr_w - 4;
    if (maxw < 20) maxw = 20;
    if (h > scr_h - 4) h = scr_h - 4;
    int starty = (scr_h - h) / 2;
    int startx = (scr_w - maxw) / 2;

    WINDOW *win = newwin(h, maxw, starty, startx);
    wbkgd(win, COLOR_PAIR(CP_DIALOG));
    box(win, 0, 0);
    wattron(win, A_BOLD);
    mvwprintw(win, 0, 2, " %s ", title);
    wattroff(win, A_BOLD);
    for (i = 0; i < nlines && i < h - 3; i++)
        mvwprintw(win, i + 1, 2, "%.*s", maxw - 4, lines[i]);
    mvwprintw(win, h - 2, 2, "Premere un tasto per continuare...");
    wrefresh(win);
    wtimeout(stdscr, GF_MAIN_TIMEOUT_MS); /* ripristina il timeout periodico */
    wgetch(win);
    delwin(win);
    touchwin(stdscr);
    refresh();
}

int gf_dialog_prompt_string(const char *title, const char *label,
                                    char *outbuf, int outbuf_size)
{
    int scr_h, scr_w;
    getmaxyx(stdscr, scr_h, scr_w);
    int w = scr_w - 10;
    if (w < 30) w = scr_w - 2;
    int h = 5;
    int starty = (scr_h - h) / 2;
    int startx = (scr_w - w) / 2;

    WINDOW *win = newwin(h, w, starty, startx);
    keypad(win, TRUE);
    wbkgd(win, COLOR_PAIR(CP_DIALOG));
    box(win, 0, 0);
    wattron(win, A_BOLD);
    mvwprintw(win, 0, 2, " %s ", title);
    wattroff(win, A_BOLD);
    mvwprintw(win, 1, 2, "%.*s", w - 4, label);

    char buf[1024];
    int len = 0;
    buf[0] = '\0';
    if (outbuf[0]) { strncpy(buf, outbuf, sizeof(buf) - 1); len = (int)strlen(buf); }

    curs_set(1);
    int cancelled = 0;
    for (;;) {
        mvwprintw(win, 2, 2, "%*s", w - 4, "");
        mvwprintw(win, 2, 2, "%.*s", w - 4, buf);
        mvwprintw(win, h - 2, 2, "[Invio=Conferma  Esc=Annulla]");
        wmove(win, 2, 2 + len);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == 27) { cancelled = 1; break; }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) break;
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (len > 0) buf[--len] = '\0';
            continue;
        }
        if (ch >= 32 && ch < 127 && len < (int)sizeof(buf) - 1 && len < w - 6) {
            buf[len++] = (char)ch;
            buf[len] = '\0';
        }
    }
    curs_set(0);
    delwin(win);
    touchwin(stdscr);
    refresh();

    if (cancelled) return -1;
    strncpy(outbuf, buf, (size_t)outbuf_size - 1);
    outbuf[outbuf_size - 1] = '\0';
    return 0;
}

int gf_dialog_choice(const char *title, const char *message,
                             const char **options, int noptions)
{
    int scr_h, scr_w;
    getmaxyx(stdscr, scr_h, scr_w);

    /* larghezza necessaria per affiancare TUTTE le opzioni sulla stessa riga
     * (il ciclo di disegno piu' sotto usa "x=2" iniziale e avanza di
     * strlen(options[i])+3 per ciascuna voce, con un margine di 2 a destra) */
    int options_row_w = 4;
    int i;
    for (i = 0; i < noptions; i++) options_row_w += (int)strlen(options[i]) + 3;

    int msg_w = (int)strlen(message) + 6;

    int w = (options_row_w > msg_w) ? options_row_w : msg_w;
    if (w > scr_w - 4) w = scr_w - 4;
    int h = 5;
    int starty = (scr_h - h) / 2;
    int startx = (scr_w - w) / 2;

    WINDOW *win = newwin(h, w, starty, startx);
    keypad(win, TRUE);
    wbkgd(win, COLOR_PAIR(CP_DIALOG));
    box(win, 0, 0);
    wattron(win, A_BOLD);
    mvwprintw(win, 0, 2, " %s ", title);
    wattroff(win, A_BOLD);
    mvwprintw(win, 1, 2, "%.*s", w - 4, message);

    int sel = 0;
    for (;;) {
        int x = 2;
        for (i = 0; i < noptions; i++) {
            if (i == sel) wattron(win, COLOR_PAIR(CP_MENUSEL));
            mvwprintw(win, 3, x, " %s ", options[i]);
            if (i == sel) wattroff(win, COLOR_PAIR(CP_MENUSEL));
            x += (int)strlen(options[i]) + 3;
        }
        wrefresh(win);
        int ch = wgetch(win);
        if (ch == 27) { sel = -1; break; }
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) break;
        if (ch == KEY_LEFT && sel > 0) sel--;
        if (ch == KEY_RIGHT && sel < noptions - 1) sel++;
        if (ch == '\t') sel = (sel + 1) % noptions;
    }
    delwin(win);
    touchwin(stdscr);
    refresh();
    return sel;
}

int gf_dialog_browse(GF_TEXTEDITOR *self, const char *title,
                             char *out_path, int out_size, int allow_new_name)
{
    char curdir[GF_MAX_FILEPATH];
    strncpy(curdir, self->current_directory, sizeof(curdir) - 1);
    curdir[sizeof(curdir) - 1] = '\0';

    int result = -1;
    int sel = 0, top = 0;

    for (;;) {
        GF_DIRENTRY *entries = NULL;
        int n = gf_list_directory(curdir, &entries);
        if (n < 0) {
            /* directory non apribile (permessi, cancellata nel frattempo,
             * ecc.): torniamo alla home come ripiego ragionevole */
            const char *lines[] = { "Impossibile aprire la cartella.", "Torno alla home." };
            gf_dialog_message("Sfoglia", lines, 2);
            const char *home = getenv("HOME");
            strncpy(curdir, home ? home : "/", sizeof(curdir) - 1);
            continue;
        }

        int has_parent = strcmp(curdir, "/") != 0;
        int total_rows = (has_parent ? 1 : 0) + n + (allow_new_name ? 1 : 0);
        if (total_rows == 0) total_rows = 1; /* cartella vuota: evita divisioni per zero */

        int scr_h, scr_w;
        getmaxyx(stdscr, scr_h, scr_w);
        int h = scr_h - 6;
        if (h < 5) h = 5;
        int w = scr_w - 10;
        if (w < 30) w = scr_w - 2;
        int starty = (scr_h - h) / 2;
        int startx = (scr_w - w) / 2;
        int visible = h - 4;

        if (sel >= total_rows) sel = total_rows - 1;
        if (sel < 0) sel = 0;

        int done_dir = 0, request_reload = 0;
        while (!done_dir) {
            if (sel < top) top = sel;
            if (sel > top + visible - 1) top = sel - visible + 1;

            WINDOW *win = newwin(h, w, starty, startx);
            keypad(win, TRUE);
            wbkgd(win, COLOR_PAIR(CP_DIALOG));
            box(win, 0, 0);
            wattron(win, A_BOLD);
            mvwprintw(win, 0, 2, " %s ", title);
            wattroff(win, A_BOLD);
            mvwprintw(win, 1, 2, "%.*s", w - 4, curdir);

            int row;
            for (row = 0; row < visible; row++) {
                int idx = top + row;
                if (idx >= total_rows) break;
                char label[300];
                int is_special_new = 0, is_parent = 0, is_dir_entry = 0;
                int entry_idx = idx - (has_parent ? 1 : 0);

                if (has_parent && idx == 0) {
                    is_parent = 1;
                    snprintf(label, sizeof(label), "[..] (cartella superiore)");
                } else if (allow_new_name && idx == total_rows - 1) {
                    is_special_new = 1;
                    snprintf(label, sizeof(label), "[Nuovo file...]");
                } else {
                    is_dir_entry = entries[entry_idx].is_dir;
                    snprintf(label, sizeof(label), "%s%s", entries[entry_idx].name,
                             is_dir_entry ? "/" : "");
                }
                (void)is_parent;

                if (idx == sel) wattron(win, COLOR_PAIR(CP_MENUSEL));
                else if (is_dir_entry) wattron(win, A_BOLD);
                mvwprintw(win, 3 + row, 2, "%-*.*s", w - 4, w - 4, label);
                if (idx == sel) wattroff(win, COLOR_PAIR(CP_MENUSEL));
                else if (is_dir_entry) wattroff(win, A_BOLD);
                (void)is_special_new;
            }
            mvwprintw(win, h - 1, 2, "[Invio=Apri/Seleziona  Esc=Annulla]");
            wrefresh(win);

            int ch = wgetch(win);
            delwin(win);
            touchwin(stdscr);

            if (ch == KEY_UP) { if (sel > 0) sel--; }
            else if (ch == KEY_DOWN) { if (sel < total_rows - 1) sel++; }
            else if (ch == KEY_PPAGE) { sel -= visible; if (sel < 0) sel = 0; }
            else if (ch == KEY_NPAGE) { sel += visible; if (sel > total_rows - 1) sel = total_rows - 1; }
            else if (ch == 27) { done_dir = 1; result = -1; }
            else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
                if (has_parent && sel == 0) {
                    gf_path_parent(curdir);
                    sel = 0; top = 0;
                    done_dir = 1; request_reload = 1;
                } else if (allow_new_name && sel == total_rows - 1) {
                    char namebuf[256] = "";
                    int rc = gf_dialog_prompt_string("Nuovo file", "Nome del file:", namebuf, sizeof(namebuf));
                    if (rc == 0 && namebuf[0]) {
                        snprintf(out_path, (size_t)out_size, "%s/%s", curdir, namebuf);
                        strncpy(self->current_directory, curdir, sizeof(self->current_directory) - 1);
                        result = 0;
                    }
                    done_dir = 1;
                } else {
                    int entry_idx = sel - (has_parent ? 1 : 0);
                    if (entries[entry_idx].is_dir) {
                        if (!strcmp(curdir, "/"))
                            snprintf(curdir, sizeof(curdir), "/%s", entries[entry_idx].name);
                        else {
                            char tmp[GF_MAX_FILEPATH];
                            snprintf(tmp, sizeof(tmp), "%.900s/%.100s", curdir, entries[entry_idx].name);
                            strncpy(curdir, tmp, sizeof(curdir) - 1);
                        }
                        sel = 0; top = 0;
                        done_dir = 1; request_reload = 1;
                    } else {
                        snprintf(out_path, (size_t)out_size, "%s/%s", curdir, entries[entry_idx].name);
                        strncpy(self->current_directory, curdir, sizeof(self->current_directory) - 1);
                        result = 0;
                        done_dir = 1;
                    }
                }
            }
        }
        free(entries);
        refresh();
        if (request_reload) continue;
        break;
    }
    return result;
}

void gf_dialog_scrollable_text(const char *title, const char **lines, int nlines)
{
    int scr_h, scr_w;
    getmaxyx(stdscr, scr_h, scr_w);
    int h = scr_h - 4;
    if (h < 5) h = 5;
    int w = scr_w - 6;
    if (w < 20) w = scr_w;
    int starty = (scr_h - h) / 2;
    int startx = (scr_w - w) / 2;
    int visible = h - 3;

    int top = 0, done = 0;
    while (!done) {
        WINDOW *win = newwin(h, w, starty, startx);
        keypad(win, TRUE);
        wbkgd(win, COLOR_PAIR(CP_DIALOG));
        box(win, 0, 0);
        wattron(win, A_BOLD);
        mvwprintw(win, 0, 2, " %s ", title);
        wattroff(win, A_BOLD);

        int row;
        for (row = 0; row < visible; row++) {
            int idx = top + row;
            if (idx >= nlines) break;
            mvwprintw(win, 1 + row, 2, "%-*.*s", w - 4, w - 4, lines[idx]);
        }
        mvwprintw(win, h - 1, 2, "[Su/Giu/PgUp/PgDn per scorrere - Esc o Invio per chiudere]  %d/%d",
                  top + 1, nlines);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == KEY_UP) { if (top > 0) top--; }
        else if (ch == KEY_DOWN) { if (top < nlines - 1) top++; }
        else if (ch == KEY_PPAGE) { top -= visible; if (top < 0) top = 0; }
        else if (ch == KEY_NPAGE) { top += visible; if (top > nlines - visible) top = nlines - visible; if (top < 0) top = 0; }
        else if (ch == 27 || ch == '\n' || ch == '\r' || ch == KEY_ENTER) done = 1;

        delwin(win);
        touchwin(stdscr);
    }
    refresh();
}

void gf_init_colors(void)
{
    start_color();
    use_default_colors();
    init_pair(CP_NORMAL,      COLOR_WHITE,   COLOR_BLACK);
    init_pair(CP_KEYWORD,     COLOR_BLUE,    COLOR_BLACK);
    init_pair(CP_STRING,      COLOR_YELLOW,  COLOR_BLACK);
    init_pair(CP_COMMENT,     COLOR_GREEN,   COLOR_BLACK);
    init_pair(CP_NUMBER,      COLOR_MAGENTA, COLOR_BLACK);
    init_pair(CP_FUNCTION,    COLOR_CYAN,    COLOR_BLACK);
    init_pair(CP_PREPROC,     COLOR_RED,     COLOR_BLACK);
    init_pair(CP_MENUBAR,     COLOR_BLACK,   COLOR_WHITE);
    init_pair(CP_MENUSEL,     COLOR_BLACK,   COLOR_YELLOW);
    init_pair(CP_STATUSBAR,   COLOR_BLACK,   COLOR_WHITE);
    init_pair(CP_TABACTIVE,   COLOR_WHITE,   COLOR_BLUE);
    init_pair(CP_TABINACTIVE, COLOR_BLACK,   COLOR_CYAN);
    init_pair(CP_SELECTION,   COLOR_BLACK,   COLOR_WHITE);
    init_pair(CP_DIALOG,      COLOR_WHITE,   COLOR_BLUE);
    /* coppie aggiuntive selezionabili dal menu Option->Text color (15..19) */
    init_pair(15, COLOR_WHITE,   COLOR_BLACK);
    init_pair(16, COLOR_GREEN,   COLOR_BLACK);
    init_pair(17, COLOR_CYAN,    COLOR_BLACK);
    init_pair(18, COLOR_YELLOW,  COLOR_BLACK);
    init_pair(19, COLOR_WHITE,   COLOR_BLUE);
}

static int gf_color_for_token(unsigned char tok)
{
    switch (tok) {
        case GF_TOK_KEYWORD:  return CP_KEYWORD;
        case GF_TOK_STRING:   return CP_STRING;
        case GF_TOK_COMMENT:  return CP_COMMENT;
        case GF_TOK_NUMBER:   return CP_NUMBER;
        case GF_TOK_FUNCTION: return CP_FUNCTION;
        case GF_TOK_PREPROC:  return CP_PREPROC;
        default: return -1; /* usa il colore di testo di default */
    }
}

#define GF_MENU_FILE_X     1
#define GF_MENU_EDIT_X     8
#define GF_MENU_OPTION_X   18
#define GF_MENU_HELP_X     27

/* dichiarazioni forward: gf_run_menu le usa prima della loro definizione */
static void gf_dispatch_file_action(GF_TEXTEDITOR *self, int item);
static void gf_dispatch_edit_action(GF_TEXTEDITOR *self, int item);
static void gf_dispatch_option_action(GF_TEXTEDITOR *self, int item);
static void gf_dispatch_help_action(GF_TEXTEDITOR *self, int item);

static const char *gf_menu_titles[] = { "File", "Modifica", "Option", "Help" };
static const int   gf_menu_x[]      = { GF_MENU_FILE_X, GF_MENU_EDIT_X, GF_MENU_OPTION_X, GF_MENU_HELP_X };

static const char *gf_menu_file_items[]   = { "New", "Open", "Save", "Save As", "Close", "Exit" };
/* Sentinella per una riga separatrice non selezionabile in un menu a
 * tendina: si confronta per indirizzo di puntatore (non per contenuto),
 * quindi non puo' essere confusa con una voce di testo reale. */
static const char GF_MENU_SEPARATOR[] = "";

static const char *gf_menu_edit_items[] = {
    "Undo", "Copy", "Cut", "Paste", "Del", "Find", "Replace",
    GF_MENU_SEPARATOR,
    "File successivo (F8)", "File precedente (F9)"
};
static const char *gf_menu_option_items[] = { "Tab spaces...", "Text color...", "Toggle developing mode", "Language...", "Autosave timer..." };
static const char *gf_menu_help_items[]   = { "Instruction", "About", "License" };


static void gf_draw_menubar(GF_TEXTEDITOR *self)
{
    (void)self;
    attron(COLOR_PAIR(CP_MENUBAR));
    mvhline(0, 0, ' ', COLS);
    int i;
    for (i = 0; i < 4; i++) {
        /* il primo carattere di ogni voce e' la lettera mnemonica
         * (Alt+F, Alt+M, Alt+O, Alt+H): lo mostriamo sottolineato */
        attron(A_UNDERLINE);
        mvaddch(0, gf_menu_x[i], gf_menu_titles[i][0]);
        attroff(A_UNDERLINE);
        mvprintw(0, gf_menu_x[i] + 1, "%s", gf_menu_titles[i] + 1);
    }
    attroff(COLOR_PAIR(CP_MENUBAR));
}

static void gf_draw_tabbar(GF_TEXTEDITOR *self)
{
    attron(COLOR_PAIR(CP_TABINACTIVE));
    mvhline(1, 0, ' ', COLS);
    attroff(COLOR_PAIR(CP_TABINACTIVE));

    if (self->num_open == 0) {
        attron(COLOR_PAIR(CP_TABINACTIVE));
        mvprintw(1, 1, "(nessun file aperto - File->New o File->Open)");
        attroff(COLOR_PAIR(CP_TABINACTIVE));
        return;
    }
    int x = 0, i;
    for (i = 0; i < GF_MAX_TABS; i++) {
        if (!self->tabs[i].in_use) continue;
        char label[GF_MAX_FILENAME + 4];
        snprintf(label, sizeof(label), " %s%s ", self->tabs[i].filename,
                 self->tabs[i].modified ? "*" : "");
        int pair = (i == self->current_tab) ? CP_TABACTIVE : CP_TABINACTIVE;
        attron(COLOR_PAIR(pair));
        mvprintw(1, x, "%s", label);
        attroff(COLOR_PAIR(pair));
        x += (int)strlen(label);
        if (x >= COLS) break;
    }
}

static void gf_draw_statusbar(GF_TEXTEDITOR *self)
{
    attron(COLOR_PAIR(CP_STATUSBAR));
    mvhline(LINES - 1, 0, ' ', COLS);

    char timebuf[32];
    pthread_mutex_lock(&self->buffer_mutex);
    strncpy(timebuf, self->clock_string, sizeof(timebuf) - 1);
    timebuf[sizeof(timebuf) - 1] = '\0';
    pthread_mutex_unlock(&self->buffer_mutex);

    int caps = 0, num = 0;
    int led_ok = gf_get_kb_led_state(&caps, &num);

    GF_FILEAREA *area = (self->num_open > 0) ? gf_current(self) : NULL;

    char left[256 + GF_MAX_FINDSTR + 64];
    if (area) {
        char findinfo[GF_MAX_FINDSTR + 64] = "";
        if (self->find.needle_len > 0 && self->find.num_matches > 0) {
            snprintf(findinfo, sizeof(findinfo), " | \"%s\": %d/%d (r%d,c%d)",
                      self->find.needle,
                      self->find.current_match + 1,
                      self->find.num_matches,
                      self->find.match_row[self->find.current_match] + 1,
                      self->find.match_col[self->find.current_match] + 1);
        } else if (self->find.needle_len > 0 && self->find.num_matches == 0) {
            snprintf(findinfo, sizeof(findinfo), " | \"%s\": nessuna occorrenza", self->find.needle);
        }
        snprintf(left, sizeof(left), "R:%d C:%d  |  Car.digitati:%lu  |  %s:%s%s",
                 area->cursor_row + 1, area->cursor_col + 1,
                 self->chars_typed,
                 area->insert_mode ? "INS" : "OVR",
                 (area->language != GF_LANG_NONE) ? " Dev" : "",
                 findinfo);
    } else {
        snprintf(left, sizeof(left), "Nessun file aperto");
    }

    char right[128];
    if (led_ok)
        snprintf(right, sizeof(right), "NUM:%s CAPS:%s  %s",
                 num ? "ON" : "off", caps ? "ON" : "off", timebuf);
    else
        snprintf(right, sizeof(right), "NUM:N/D CAPS:N/D  %s", timebuf);

    mvprintw(LINES - 1, 1, "%.*s", COLS - (int)strlen(right) - 3, left);
    mvprintw(LINES - 1, COLS - (int)strlen(right) - 1, "%s", right);
    attroff(COLOR_PAIR(CP_STATUSBAR));
}

static void gf_adjust_scroll(GF_FILEAREA *area)
{
    int text_h = GF_TEXT_BOTTOM - GF_TEXT_TOP + 1;
    int text_w = COLS;
    if (text_h < 1) text_h = 1;

    if (area->cursor_row < area->view_top) area->view_top = area->cursor_row;
    if (area->cursor_row > area->view_top + text_h - 1) area->view_top = area->cursor_row - text_h + 1;
    if (area->cursor_col < area->view_left) area->view_left = area->cursor_col;
    if (area->cursor_col > area->view_left + text_w - 1) area->view_left = area->cursor_col - text_w + 1;
    if (area->view_top < 0) area->view_top = 0;
    if (area->view_left < 0) area->view_left = 0;
}

static int gf_pos_in_selection(GF_FILEAREA *area, int row, int col)
{
    if (!area->sel_active) return 0;
    int r1, c1, r2, c2;
    gf_get_selection_bounds(area, &r1, &c1, &r2, &c2);
    if (row < r1 || row > r2) return 0;
    if (row == r1 && col < c1) return 0;
    if (row == r2 && col >= c2) return 0;
    return 1;
}

static void gf_draw_textarea(GF_TEXTEDITOR *self)
{
    int text_h = GF_TEXT_BOTTOM - GF_TEXT_TOP + 1;
    int screen_row, i;

    for (screen_row = 0; screen_row < text_h; screen_row++) {
        move(GF_TEXT_TOP + screen_row, 0);
        clrtoeol();
    }

    if (self->num_open == 0) return;

    GF_FILEAREA *area = gf_current(self);
    gf_adjust_scroll(area);

    pthread_mutex_lock(&self->buffer_mutex);
    for (screen_row = 0; screen_row < text_h; screen_row++) {
        int doc_row = area->view_top + screen_row;
        if (doc_row >= area->doc.num_lines) break;
        GF_LINE *ln = &area->doc.lines[doc_row];

        int base_pair = self->options.text_color_pair;
        int byte_pos = area->view_left;
        for (i = 0; i < COLS; i++) {
            if (byte_pos >= ln->length) break;
            unsigned char lead = (unsigned char)ln->text[byte_pos];
            int seqlen = gf_utf8_seqlen(lead);
            if (byte_pos + seqlen > ln->length) seqlen = ln->length - byte_pos;

            int pair = base_pair;
            if (self->options.developing_mode && byte_pos < ln->hl_capacity) {
                int tokpair = gf_color_for_token(ln->hl[byte_pos]);
                if (tokpair > 0) pair = tokpair;
            }
            int selected = gf_pos_in_selection(area, doc_row, byte_pos);
            if (selected) attron(COLOR_PAIR(CP_SELECTION));
            else attron(COLOR_PAIR(pair));
            /* stampiamo l'intera sequenza di byte del carattere in un solo
             * colpo, in un'unica cella di schermo: e' cio' che permette
             * a ncursesw di ricomporre correttamente il glifo UTF-8
             * (es. lettere accentate a 2 byte) invece di spezzarlo */
            mvprintw(GF_TEXT_TOP + screen_row, i, "%.*s", seqlen, ln->text + byte_pos);
            if (selected) attroff(COLOR_PAIR(CP_SELECTION));
            else attroff(COLOR_PAIR(pair));

            byte_pos += seqlen;
        }
    }
    pthread_mutex_unlock(&self->buffer_mutex);
}

static void gf_position_cursor(GF_TEXTEDITOR *self)
{
    if (self->num_open == 0) { move(GF_TEXT_TOP, 0); return; }
    GF_FILEAREA *area = gf_current(self);
    int cur_screen_row = GF_TEXT_TOP + (area->cursor_row - area->view_top);
    GF_LINE *ln = &area->doc.lines[area->cursor_row];
    int cur_screen_col;
    if (area->cursor_col >= area->view_left)
        cur_screen_col = gf_utf8_count_chars(ln, area->view_left, area->cursor_col);
    else
        cur_screen_col = -1; /* cursore scrollato fuori a sinistra */
    if (cur_screen_row >= GF_TEXT_TOP && cur_screen_row <= GF_TEXT_BOTTOM &&
        cur_screen_col >= 0 && cur_screen_col < COLS) {
        move(cur_screen_row, cur_screen_col);
    }
}

void gf_draw_all(GF_TEXTEDITOR *self)
{
    erase();
    gf_draw_menubar(self);
    gf_draw_tabbar(self);
    gf_draw_textarea(self);
    gf_draw_statusbar(self);
    gf_position_cursor(self);
    refresh();
}

/* Aggiorna solo la barra di stato (dove vive l'orologio) senza erase() e
 * senza ridisegnare menu/schede/testo. Usata dal ciclo principale ai
 * risvegli periodici in cui non e' arrivato nessun tasto (altrimenti un
 * ridisegno completo ogni 500ms produce uno sfarfallio visibile, pur non
 * cambiando nulla nel contenuto). */
void gf_refresh_clock(GF_TEXTEDITOR *self)
{
    gf_draw_statusbar(self);
    gf_position_cursor(self);
    refresh();
}

static void gf_draw_dropdown(int menu_index, int sel, const char **items, int nitems)
{
    int x = gf_menu_x[menu_index];
    int w = 4, i;
    for (i = 0; i < nitems; i++) {
        int l = (int)strlen(items[i]) + 2;
        if (l > w) w = l;
    }
    int h = nitems + 2;
    if (x + w >= COLS) x = COLS - w - 1;

    WINDOW *win = newwin(h, w, 1, x);
    wbkgd(win, COLOR_PAIR(CP_MENUBAR));
    box(win, 0, 0);
    for (i = 0; i < nitems; i++) {
        if (items[i] == GF_MENU_SEPARATOR) {
            mvwaddch(win, 1 + i, 0, ACS_LTEE);
            mvwhline(win, 1 + i, 1, ACS_HLINE, w - 2);
            mvwaddch(win, 1 + i, w - 1, ACS_RTEE);
            continue;
        }
        if (i == sel) wattron(win, COLOR_PAIR(CP_MENUSEL));
        mvwprintw(win, 1 + i, 1, "%-*s", w - 2, items[i]);
        if (i == sel) wattroff(win, COLOR_PAIR(CP_MENUSEL));
    }
    wrefresh(win);
    delwin(win);
}

void gf_run_menu(GF_TEXTEDITOR *self, int start_menu)
{
    const char **items_table[4] = { gf_menu_file_items, gf_menu_edit_items,
                                     gf_menu_option_items, gf_menu_help_items };
    int nitems_table[4] = {
        (int)(sizeof(gf_menu_file_items)   / sizeof(gf_menu_file_items[0])),
        (int)(sizeof(gf_menu_edit_items)   / sizeof(gf_menu_edit_items[0])),
        (int)(sizeof(gf_menu_option_items) / sizeof(gf_menu_option_items[0])),
        (int)(sizeof(gf_menu_help_items)   / sizeof(gf_menu_help_items[0]))
    };

    int cur_menu = start_menu, cur_item = 0;
    int done = 0;
    curs_set(0);

    for (;;) {
        gf_draw_all(self);
        attron(COLOR_PAIR(CP_MENUSEL));
        mvprintw(0, gf_menu_x[cur_menu], "%s", gf_menu_titles[cur_menu]);
        attroff(COLOR_PAIR(CP_MENUSEL));
        gf_draw_dropdown(cur_menu, cur_item, items_table[cur_menu], nitems_table[cur_menu]);
        refresh();

        int ch;
        do { ch = gf_getch_extended(); } while (ch == ERR); /* ignora i risvegli a vuoto */
        if (ch == KEY_LEFT) { cur_menu = (cur_menu + 3) % 4; cur_item = 0; }
        else if (ch == KEY_RIGHT) { cur_menu = (cur_menu + 1) % 4; cur_item = 0; }
        else if (ch == GF_KEY_ALT_F) { cur_menu = 0; cur_item = 0; }
        else if (ch == GF_KEY_ALT_M) { cur_menu = 1; cur_item = 0; }
        else if (ch == GF_KEY_ALT_O) { cur_menu = 2; cur_item = 0; }
        else if (ch == GF_KEY_ALT_H) { cur_menu = 3; cur_item = 0; }
        else if (ch == KEY_UP) {
            do { cur_item = (cur_item - 1 + nitems_table[cur_menu]) % nitems_table[cur_menu]; }
            while (items_table[cur_menu][cur_item] == GF_MENU_SEPARATOR);
        }
        else if (ch == KEY_DOWN) {
            do { cur_item = (cur_item + 1) % nitems_table[cur_menu]; }
            while (items_table[cur_menu][cur_item] == GF_MENU_SEPARATOR);
        }
        else if (ch == 27 || ch == KEY_F(10)) { done = 1; }
        else if (ch == '\n' || ch == KEY_ENTER) {
            done = 1;
            switch (cur_menu) {
                case 0: gf_dispatch_file_action(self, cur_item); break;
                case 1: gf_dispatch_edit_action(self, cur_item); break;
                case 2: gf_dispatch_option_action(self, cur_item); break;
                case 3: gf_dispatch_help_action(self, cur_item); break;
            }
        }
        if (done) break;
    }
    curs_set(1);
}

static void gf_dispatch_file_action(GF_TEXTEDITOR *self, int item)
{
    switch (item) {
        case 0: gf_action_new(self); break;
        case 1: gf_action_open(self); break;
        case 2: gf_action_save(self); break;
        case 3: gf_action_save_as(self); break;
        case 4: gf_action_close(self); break;
        case 5: gf_action_exit(self); break;
    }
}

static void gf_dispatch_edit_action(GF_TEXTEDITOR *self, int item)
{
    switch (item) {
        case 0: gf_action_undo(self); break;
        case 1: gf_action_copy(self); break;
        case 2: gf_action_cut(self); break;
        case 3: gf_action_paste(self); break;
        case 4: gf_action_delete_selection(self); break;
        case 5: gf_action_find(self); break;
        case 6: gf_action_replace(self); break;
        /* case 7 = separatore, mai raggiungibile: la navigazione lo salta */
        case 8: gf_next_tab(self); break;
        case 9: gf_prev_tab(self); break;
    }
}

static void gf_dispatch_option_action(GF_TEXTEDITOR *self, int item)
{
    switch (item) {
        case 0: gf_action_set_tabwidth(self); break;
        case 1: gf_action_set_color(self); break;
        case 2: gf_action_toggle_developing(self); break;
        case 3: gf_action_set_language(self); break;
        case 4: gf_action_set_autosave(self); break;
    }
}

static void gf_dispatch_help_action(GF_TEXTEDITOR *self, int item)
{
    switch (item) {
        case 0: gf_action_help_instructions(self); break;
        case 1: gf_action_help_about(self); break;
        case 2: gf_action_help_license(self); break;
    }
}

