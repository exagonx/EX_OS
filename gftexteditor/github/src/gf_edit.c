/* ============================================================================
 * gf_edit.c
 *
 * Parte dell'implementazione di GF_TEXTEDITOR - vedi gf_texteditor.h per
 * la descrizione dell'oggetto e gf_internal.h per le strutture e i
 * prototipi condivisi tra i moduli.
 *
 * Movimento del cursore, inserimento/cancellazione di testo (UTF-8 aware),
 * ricerca/sostituzione, evidenziazione sintattica (C/C++/BASIC) su thread
 * separato, e tutte le azioni invocabili dai menu File/Modifica/Option/Help.
 *
 * Autore : Graziano Falcone  <exagonx@hotmail.com>
 * Licenza: GNU GPL v2 (vedi Help->License nel programma, e il file
 *          COPYING distribuito insieme al codice sorgente)
 * ==========================================================================*/

#define _GNU_SOURCE
#include "gf_internal.h"

static const char *gf_kw_c[] = {
    "int","char","float","double","void","short","long","unsigned","signed",
    "struct","union","enum","typedef","static","const","volatile","extern",
    "register","auto","if","else","for","while","do","switch","case",
    "default","break","continue","return","goto","sizeof",
    /* estensioni C++ */
    "class","public","private","protected","virtual","new","delete",
    "namespace","using","template","try","catch","throw","this","inline",
    "friend","operator","bool","true","false","nullptr",
    NULL
};

static const char *gf_kw_basic[] = {
    "PRINT","INPUT","IF","THEN","ELSE","GOTO","GOSUB","RETURN","FOR","TO",
    "STEP","NEXT","DIM","LET","END","REM","DATA","READ","RESTORE","STOP",
    "CLS","LOCATE","COLOR","SUB","FUNCTION","DO","LOOP","WHILE","WEND",
    "EXIT","AS","INTEGER","STRING","DOUBLE","SINGLE","CALL","OPEN","CLOSE",
    "AND","OR","NOT","MOD","TRUE","FALSE","SHARED","CONST","TYPE",
    NULL
};

static int gf_match_keyword(const char *word, const char **table, int case_insensitive)
{
    int i;
    for (i = 0; table[i]; i++) {
        if (case_insensitive) { if (!strcasecmp(word, table[i])) return 1; }
        else                  { if (!strcmp(word, table[i])) return 1; }
    }
    return 0;
}

static void gf_highlight_line_c(GF_LINE *ln, int *in_comment)
{
    int i = 0;
    memset(ln->hl, GF_TOK_NORMAL, (size_t)ln->length);

    /* riga di preprocessore: l'intera riga e' PREPROC */
    int j = 0;
    while (j < ln->length && isspace((unsigned char)ln->text[j])) j++;
    if (!*in_comment && j < ln->length && ln->text[j] == '#') {
        memset(ln->hl, GF_TOK_PREPROC, (size_t)ln->length);
        return;
    }

    while (i < ln->length) {
        if (*in_comment) {
            ln->hl[i] = GF_TOK_COMMENT;
            if (i + 1 < ln->length && ln->text[i] == '*' && ln->text[i + 1] == '/') {
                ln->hl[i + 1] = GF_TOK_COMMENT;
                i += 2; *in_comment = 0; continue;
            }
            i++; continue;
        }
        if (i + 1 < ln->length && ln->text[i] == '/' && ln->text[i + 1] == '*') {
            ln->hl[i] = ln->hl[i + 1] = GF_TOK_COMMENT;
            i += 2; *in_comment = 1; continue;
        }
        if (i + 1 < ln->length && ln->text[i] == '/' && ln->text[i + 1] == '/') {
            while (i < ln->length) { ln->hl[i] = GF_TOK_COMMENT; i++; }
            continue;
        }
        if (ln->text[i] == '"' || ln->text[i] == '\'') {
            char quote = ln->text[i];
            ln->hl[i] = GF_TOK_STRING; i++;
            while (i < ln->length) {
                ln->hl[i] = GF_TOK_STRING;
                if (ln->text[i] == '\\' && i + 1 < ln->length) { ln->hl[i + 1] = GF_TOK_STRING; i += 2; continue; }
                if (ln->text[i] == quote) { i++; break; }
                i++;
            }
            continue;
        }
        if (isdigit((unsigned char)ln->text[i])) {
            while (i < ln->length && (isalnum((unsigned char)ln->text[i]) || ln->text[i] == '.')) {
                ln->hl[i] = GF_TOK_NUMBER; i++;
            }
            continue;
        }
        if (isalpha((unsigned char)ln->text[i]) || ln->text[i] == '_') {
            int start = i;
            while (i < ln->length && (isalnum((unsigned char)ln->text[i]) || ln->text[i] == '_')) i++;
            int wlen = i - start;
            char word[256];
            if (wlen >= (int)sizeof(word)) wlen = (int)sizeof(word) - 1;
            memcpy(word, ln->text + start, (size_t)wlen);
            word[wlen] = '\0';

            int k2 = i;
            while (k2 < ln->length && isspace((unsigned char)ln->text[k2])) k2++;
            int is_func = (k2 < ln->length && ln->text[k2] == '(');

            GF_TOKENCLASS cls;
            if (gf_match_keyword(word, gf_kw_c, 0)) cls = GF_TOK_KEYWORD;
            else if (is_func) cls = GF_TOK_FUNCTION;
            else cls = GF_TOK_NORMAL;

            int t;
            for (t = start; t < i; t++) ln->hl[t] = (unsigned char)cls;
            continue;
        }
        i++;
    }
}

static void gf_highlight_line_basic(GF_LINE *ln)
{
    int i = 0;
    memset(ln->hl, GF_TOK_NORMAL, (size_t)ln->length);
    while (i < ln->length) {
        if (ln->text[i] == '\'') {
            while (i < ln->length) { ln->hl[i] = GF_TOK_COMMENT; i++; }
            continue;
        }
        if (ln->text[i] == '"') {
            ln->hl[i] = GF_TOK_STRING; i++;
            while (i < ln->length) {
                ln->hl[i] = GF_TOK_STRING;
                if (ln->text[i] == '"') { i++; break; }
                i++;
            }
            continue;
        }
        if (isdigit((unsigned char)ln->text[i])) {
            while (i < ln->length && (isalnum((unsigned char)ln->text[i]) || ln->text[i] == '.')) {
                ln->hl[i] = GF_TOK_NUMBER; i++;
            }
            continue;
        }
        if (isalpha((unsigned char)ln->text[i]) || ln->text[i] == '_') {
            int start = i;
            while (i < ln->length && (isalnum((unsigned char)ln->text[i]) || ln->text[i] == '_' || ln->text[i] == '$')) i++;
            int wlen = i - start;
            char word[256];
            if (wlen >= (int)sizeof(word)) wlen = (int)sizeof(word) - 1;
            memcpy(word, ln->text + start, (size_t)wlen);
            word[wlen] = '\0';

            /* REM come commento a inizio riga logica */
            if (!strcasecmp(word, "REM")) {
                int t;
                for (t = start; t < ln->length; t++) ln->hl[t] = GF_TOK_COMMENT;
                i = ln->length;
                continue;
            }

            int k2 = i;
            while (k2 < ln->length && isspace((unsigned char)ln->text[k2])) k2++;
            int is_func = (k2 < ln->length && ln->text[k2] == '(');

            GF_TOKENCLASS cls;
            if (gf_match_keyword(word, gf_kw_basic, 1)) cls = GF_TOK_KEYWORD;
            else if (is_func) cls = GF_TOK_FUNCTION;
            else cls = GF_TOK_NORMAL;

            int t;
            for (t = start; t < i; t++) ln->hl[t] = (unsigned char)cls;
            continue;
        }
        i++;
    }
}

static void gf_highlight_area(GF_FILEAREA *area)
{
    int in_comment = 0, i;
    for (i = 0; i < area->doc.num_lines; i++) {
        GF_LINE *ln = &area->doc.lines[i];
        gf_line_ensure_capacity(ln, ln->length);
        if (area->language == GF_LANG_C || area->language == GF_LANG_CPP)
            gf_highlight_line_c(ln, &in_comment);
        else if (area->language == GF_LANG_BASIC)
            gf_highlight_line_basic(ln);
        else
            memset(ln->hl, GF_TOK_NORMAL, (size_t)ln->length);
    }
}

void *gf_highlight_thread_func(void *arg)
{
    GF_TEXTEDITOR *self = (GF_TEXTEDITOR *)arg;
    for (;;) {
        pthread_mutex_lock(&self->buffer_mutex);
        while (!self->highlight_dirty && !self->thread_should_exit)
            pthread_cond_wait(&self->highlight_cond, &self->buffer_mutex);
        if (self->thread_should_exit) { pthread_mutex_unlock(&self->buffer_mutex); break; }
        self->highlight_dirty = 0;
        GF_FILEAREA *area = gf_current(self);
        if (area->in_use && self->options.developing_mode)
            gf_highlight_area(area);
        pthread_mutex_unlock(&self->buffer_mutex);
    }
    return NULL;
}

void gf_request_rehighlight(GF_TEXTEDITOR *self)
{
    pthread_mutex_lock(&self->buffer_mutex);
    self->highlight_dirty = 1;
    pthread_cond_signal(&self->highlight_cond);
    pthread_mutex_unlock(&self->buffer_mutex);
}

static void gf_recompute_matches(GF_FILEAREA *area, GF_FINDSTATE *find)
{
    find->num_matches = 0;
    if (find->needle_len == 0) { find->current_match = -1; return; }
    int r;
    for (r = 0; r < area->doc.num_lines && find->num_matches < GF_MAX_MATCHES; r++) {
        GF_LINE *ln = &area->doc.lines[r];
        int c = 0;
        while (c <= ln->length - find->needle_len) {
            if (!strncmp(ln->text + c, find->needle, (size_t)find->needle_len)) {
                find->match_row[find->num_matches] = r;
                find->match_col[find->num_matches] = c;
                find->num_matches++;
                if (find->num_matches >= GF_MAX_MATCHES) break;
                c += find->needle_len;
            } else {
                c++;
            }
        }
    }
    find->current_match = find->num_matches > 0 ? 0 : -1;
}

static void gf_find_next(GF_TEXTEDITOR *self, int forward)
{
    GF_FILEAREA *area = gf_current(self);
    GF_FINDSTATE *find = &self->find;
    if (find->num_matches == 0) return;
    if (forward) find->current_match = (find->current_match + 1) % find->num_matches;
    else         find->current_match = (find->current_match - 1 + find->num_matches) % find->num_matches;
    area->cursor_row = find->match_row[find->current_match];
    area->cursor_col = find->match_col[find->current_match];
    area->sel_active = 0;
}

static int gf_replace_one(GF_FILEAREA *area, int row, int col, int needle_len, const char *replacement)
{
    gf_delete_range(area, row, col, row, col + needle_len);
    GF_LINE *ln = &area->doc.lines[row];
    int i, rlen = (int)strlen(replacement);
    for (i = 0; i < rlen; i++) gf_line_insert_char(ln, col + i, replacement[i]);
    area->modified = 1;
    return rlen - needle_len;
}

static void gf_action_replace_apply(GF_TEXTEDITOR *self, int replace_all)
{
    GF_FILEAREA *area = gf_current(self);
    GF_FINDSTATE *find = &self->find;
    if (find->num_matches == 0) return;

    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_push_undo(area);
    if (!replace_all) {
        int idx = (find->current_match >= 0) ? find->current_match : 0;
        gf_replace_one(area, find->match_row[idx], find->match_col[idx],
                       find->needle_len, find->replacement);
    } else {
        /* sostituisce partendo dall'ultima occorrenza verso la prima, cosi'
         * gli offset di colonna delle occorrenze precedenti restano validi */
        int i;
        for (i = find->num_matches - 1; i >= 0; i--)
            gf_replace_one(area, find->match_row[i], find->match_col[i],
                           find->needle_len, find->replacement);
    }
    pthread_mutex_unlock(&self->buffer_mutex);
    gf_recompute_matches(area, find);
    gf_request_rehighlight(self);
}

void gf_action_new(GF_TEXTEDITOR *self)
{
    int idx = gf_find_free_area_index(self);
    if (idx < 0) {
        const char *lines[] = { "Tutte le aree disponibili sono occupate.",
                                 "Chiudere un file prima di aprirne uno nuovo." };
        gf_dialog_message("New", lines, 2);
        return;
    }
    char namebuf[GF_MAX_FILENAME] = "";
    if (gf_dialog_prompt_string("New", "Nome del nuovo file:", namebuf, sizeof(namebuf)) != 0)
        return;
    if (namebuf[0] == '\0') return;

    char fullpath[GF_MAX_FILEPATH];
    if (namebuf[0] == '/')
        strncpy(fullpath, namebuf, sizeof(fullpath) - 1);
    else
        snprintf(fullpath, sizeof(fullpath), "%.900s/%.100s", self->current_directory, namebuf);
    fullpath[sizeof(fullpath) - 1] = '\0';

    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_reset(&self->tabs[idx], gf_detect_language(fullpath));
    strncpy(self->tabs[idx].filepath, fullpath, GF_MAX_FILEPATH - 1);
    self->tabs[idx].has_path = 1;
    const char *slash = strrchr(fullpath, '/');
    strncpy(self->tabs[idx].filename, slash ? slash + 1 : fullpath, GF_MAX_FILENAME - 1);
    pthread_mutex_unlock(&self->buffer_mutex);

    self->current_tab = idx;
    self->num_open++;
    gf_request_rehighlight(self);
}

void gf_action_open(GF_TEXTEDITOR *self)
{
    char pathbuf[GF_MAX_FILEPATH] = "";
    if (gf_dialog_browse(self, "Open", pathbuf, sizeof(pathbuf), 0) != 0)
        return;
    if (pathbuf[0] == '\0') return;

    int idx = gf_find_free_area_index(self);
    if (idx < 0) {
        const char *lines[] = { "Tutte le aree disponibili sono occupate." };
        gf_dialog_message("Open", lines, 1);
        return;
    }

    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_reset(&self->tabs[idx], GF_LANG_NONE);
    int ok = gf_load_file_into_area(&self->tabs[idx], pathbuf);
    if (ok) self->tabs[idx].language = gf_detect_language(pathbuf);
    pthread_mutex_unlock(&self->buffer_mutex);

    if (!ok) {
        gf_doc_free(&self->tabs[idx].doc);
        memset(&self->tabs[idx], 0, sizeof(self->tabs[idx]));
        const char *lines[] = { "Impossibile aprire il file specificato." };
        gf_dialog_message("Errore", lines, 1);
        return;
    }
    self->current_tab = idx;
    self->num_open++;
    gf_request_rehighlight(self);
}

void gf_action_save(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->in_use) return;
    if (!area->has_path) { /* nessun percorso ancora assegnato: si comporta come Save As */
        gf_action_save_as(self);
        return;
    }
    pthread_mutex_lock(&self->buffer_mutex);
    int ok = gf_save_area_to_disk(area);
    pthread_mutex_unlock(&self->buffer_mutex);
    if (!ok) {
        const char *lines[] = { "Salvataggio del file non riuscito." };
        gf_dialog_message("Errore", lines, 1);
    }
}

void gf_action_save_as(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->in_use) return;

    if (area->has_path) {
        char dir[GF_MAX_FILEPATH];
        strncpy(dir, area->filepath, sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        gf_path_parent(dir);
        strncpy(self->current_directory, dir, sizeof(self->current_directory) - 1);
    }

    char pathbuf[GF_MAX_FILEPATH] = "";
    if (gf_dialog_browse(self, "Save As", pathbuf, sizeof(pathbuf), 1) != 0)
        return;
    if (pathbuf[0] == '\0') return;

    pthread_mutex_lock(&self->buffer_mutex);
    strncpy(area->filepath, pathbuf, GF_MAX_FILEPATH - 1);
    area->has_path = 1;
    const char *slash = strrchr(pathbuf, '/');
    strncpy(area->filename, slash ? slash + 1 : pathbuf, GF_MAX_FILENAME - 1);
    area->language = gf_detect_language(pathbuf);
    int ok = gf_save_area_to_disk(area);
    pthread_mutex_unlock(&self->buffer_mutex);

    if (!ok) {
        const char *lines[] = { "Salvataggio del file non riuscito." };
        gf_dialog_message("Errore", lines, 1);
    }
    gf_request_rehighlight(self);
}

static int gf_confirm_save_dialog(const char *filename)
{
    char msg[400];
    snprintf(msg, sizeof(msg), "Il file '%s' e' stato modificato. Salvare?", filename);
    const char *opts[] = { "Si", "No", "Annulla" };
    int r = gf_dialog_choice("Conferma", msg, opts, 3);
    if (r == 0) return 1;
    if (r == 1) return 0;
    return -1;
}

void gf_action_close(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->in_use) return;

    if (area->modified) {
        int rc = gf_confirm_save_dialog(area->filename);
        if (rc == -1) return;
        if (rc == 1) {
            if (!area->has_path) gf_action_save_as(self);
            else {
                pthread_mutex_lock(&self->buffer_mutex);
                gf_save_area_to_disk(area);
                pthread_mutex_unlock(&self->buffer_mutex);
            }
        }
    }

    pthread_mutex_lock(&self->buffer_mutex);
    gf_doc_free(&area->doc);
    gf_area_free_undo_stack(area);
    memset(area, 0, sizeof(*area));
    pthread_mutex_unlock(&self->buffer_mutex);
    self->num_open--;

    int i;
    for (i = 0; i < GF_MAX_TABS; i++) {
        if (self->tabs[i].in_use) { self->current_tab = i; break; }
    }
}

void gf_action_exit(GF_TEXTEDITOR *self)
{
    int i;
    for (i = 0; i < GF_MAX_TABS; i++) {
        if (self->tabs[i].in_use && self->tabs[i].modified) {
            self->current_tab = i;
            int rc = gf_confirm_save_dialog(self->tabs[i].filename);
            if (rc == -1) return; /* annulla l'uscita */
            if (rc == 1) {
                if (!self->tabs[i].has_path) gf_action_save_as(self);
                else {
                    pthread_mutex_lock(&self->buffer_mutex);
                    gf_save_area_to_disk(&self->tabs[i]);
                    pthread_mutex_unlock(&self->buffer_mutex);
                }
            }
        }
    }
    self->running = 0;
}

void gf_action_copy(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->in_use || !area->sel_active) return;
    int r1, c1, r2, c2;
    gf_get_selection_bounds(area, &r1, &c1, &r2, &c2);
    pthread_mutex_lock(&self->buffer_mutex);
    char *text = gf_extract_range(area, r1, c1, r2, c2);
    pthread_mutex_unlock(&self->buffer_mutex);
    gf_clipboard_set(&self->clipboard, text, (int)strlen(text));
    free(text);
}

void gf_action_cut(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->in_use || !area->sel_active) return;
    int r1, c1, r2, c2;
    gf_get_selection_bounds(area, &r1, &c1, &r2, &c2);
    pthread_mutex_lock(&self->buffer_mutex);
    char *text = gf_extract_range(area, r1, c1, r2, c2);
    gf_area_push_undo(area);
    gf_delete_range(area, r1, c1, r2, c2);
    pthread_mutex_unlock(&self->buffer_mutex);
    gf_clipboard_set(&self->clipboard, text, (int)strlen(text));
    free(text);
    gf_request_rehighlight(self);
}

void gf_action_delete_selection(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->in_use || !area->sel_active) return;
    int r1, c1, r2, c2;
    gf_get_selection_bounds(area, &r1, &c1, &r2, &c2);
    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_push_undo(area);
    gf_delete_range(area, r1, c1, r2, c2);
    pthread_mutex_unlock(&self->buffer_mutex);
    gf_request_rehighlight(self);
}

void gf_action_undo(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->in_use) return;
    if (area->undo_count == 0) {
        const char *lines[] = { "Nessuna modifica da annullare." };
        gf_dialog_message("Undo", lines, 1);
        return;
    }
    int idx = (area->undo_head - 1 + GF_UNDO_MAX) % GF_UNDO_MAX;

    pthread_mutex_lock(&self->buffer_mutex);
    gf_doc_free(&area->doc);
    area->doc = area->undo_stack[idx];        /* trasferimento di proprieta' */
    area->undo_stack[idx].lines = NULL;       /* evita una doppia free       */
    area->undo_stack[idx].num_lines = 0;
    area->undo_stack[idx].capacity = 0;

    area->cursor_row = area->undo_cursor_row[idx];
    area->cursor_col = area->undo_cursor_col[idx];
    if (area->cursor_row >= area->doc.num_lines) area->cursor_row = area->doc.num_lines - 1;
    if (area->cursor_row < 0) area->cursor_row = 0;
    if (area->cursor_col > area->doc.lines[area->cursor_row].length)
        area->cursor_col = area->doc.lines[area->cursor_row].length;
    area->sel_active = 0;
    area->modified = 1;
    pthread_mutex_unlock(&self->buffer_mutex);

    area->undo_head = idx;
    area->undo_count--;
    gf_request_rehighlight(self);
}

void gf_action_paste(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->in_use || self->clipboard.length == 0) return;

    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_push_undo(area);
    if (area->sel_active) {
        int r1, c1, r2, c2;
        gf_get_selection_bounds(area, &r1, &c1, &r2, &c2);
        gf_delete_range(area, r1, c1, r2, c2);
    }
    const char *p = self->clipboard.data;
    int row = area->cursor_row, col = area->cursor_col;
    while (*p) {
        if (*p == '\n') {
            GF_LINE *ln = &area->doc.lines[row];
            char *tail = strdup(ln->text + col);
            ln->length = col;
            ln->text[col] = '\0';
            gf_doc_insert_line(&area->doc, row + 1, tail);
            free(tail);
            row++; col = 0;
        } else {
            gf_line_insert_char(&area->doc.lines[row], col, *p);
            col++;
        }
        p++;
    }
    area->cursor_row = row;
    area->cursor_col = col;
    area->modified = 1;
    pthread_mutex_unlock(&self->buffer_mutex);
    gf_request_rehighlight(self);
}

void gf_action_find(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->in_use) return;
    char needle[GF_MAX_FINDSTR] = "";
    strncpy(needle, self->find.needle, sizeof(needle) - 1);
    if (gf_dialog_prompt_string("Find", "Stringa da cercare:", needle, sizeof(needle)) != 0)
        return;
    if (needle[0] == '\0') return;

    strncpy(self->find.needle, needle, GF_MAX_FINDSTR - 1);
    self->find.needle_len = (int)strlen(self->find.needle);
    gf_recompute_matches(area, &self->find);

    if (self->find.num_matches == 0) {
        const char *lines[] = { "Nessuna occorrenza trovata." };
        gf_dialog_message("Find", lines, 1);
        return;
    }
    /* posiziona sulla prima occorrenza a partire dal cursore corrente */
    int i, best = 0;
    for (i = 0; i < self->find.num_matches; i++) {
        if (self->find.match_row[i] > area->cursor_row ||
            (self->find.match_row[i] == area->cursor_row && self->find.match_col[i] >= area->cursor_col)) {
            best = i; break;
        }
    }
    self->find.current_match = best;
    area->cursor_row = self->find.match_row[best];
    area->cursor_col = self->find.match_col[best];
    area->sel_active = 0;
}

void gf_action_replace(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->in_use) return;
    char needle[GF_MAX_FINDSTR] = "";
    char repl[GF_MAX_FINDSTR] = "";
    strncpy(needle, self->find.needle, sizeof(needle) - 1);

    if (gf_dialog_prompt_string("Replace", "Stringa da cercare:", needle, sizeof(needle)) != 0) return;
    if (needle[0] == '\0') return;
    if (gf_dialog_prompt_string("Replace", "Stringa sostitutiva:", repl, sizeof(repl)) != 0) return;

    strncpy(self->find.needle, needle, GF_MAX_FINDSTR - 1);
    self->find.needle_len = (int)strlen(self->find.needle);
    strncpy(self->find.replacement, repl, GF_MAX_FINDSTR - 1);
    gf_recompute_matches(area, &self->find);

    if (self->find.num_matches == 0) {
        const char *lines[] = { "Nessuna occorrenza trovata." };
        gf_dialog_message("Replace", lines, 1);
        return;
    }

    char msg[128];
    snprintf(msg, sizeof(msg), "Trovate %d occorrenze. Che tipo di sostituzione?", self->find.num_matches);
    const char *opts[] = { "Singola", "Tutte", "Annulla" };
    int rc = gf_dialog_choice("Replace", msg, opts, 3);
    if (rc == 0) gf_action_replace_apply(self, 0);
    else if (rc == 1) gf_action_replace_apply(self, 1);
}

void gf_action_set_tabwidth(GF_TEXTEDITOR *self)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", self->options.tab_width);
    if (gf_dialog_prompt_string("Option", "Numero di spazi per tabulazione (1-16):", buf, sizeof(buf)) != 0)
        return;
    int v = atoi(buf);
    if (v < 1) v = 1;
    if (v > 16) v = 16;
    self->options.tab_width = v;
}

void gf_action_set_autosave(GF_TEXTEDITOR *self)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", self->options.autosave_seconds);
    if (gf_dialog_prompt_string("Option", "Autosalvataggio ogni N secondi (0=disattivo):", buf, sizeof(buf)) != 0)
        return;
    int v = atoi(buf);
    if (v < 0) v = 0;
    if (v > 3600) v = 3600; /* limite ragionevole: max un'ora */
    self->options.autosave_seconds = v;
    self->autosave_elapsed = 0;
}

void gf_action_set_color(GF_TEXTEDITOR *self)
{
    const char *opts[] = { "Bianco", "Ciano", "Verde", "Giallo", "Magenta" };
    int rc = gf_dialog_choice("Option", "Colore del testo normale:", opts, 5);
    if (rc < 0) return;
    /* le coppie 15..19 vengono inizializzate in gf_init_colors() */
    self->options.text_color_pair = 15 + rc;
}

void gf_action_toggle_developing(GF_TEXTEDITOR *self)
{
    self->options.developing_mode = !self->options.developing_mode;
    gf_request_rehighlight(self);
}

void gf_action_set_language(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->in_use) return;
    const char *opts[] = { "Nessuno", "C", "C++", "BASIC" };
    int rc = gf_dialog_choice("Option", "Linguaggio per l'evidenziazione sintattica:", opts, 4);
    if (rc < 0) return;
    area->language = (GF_LANGUAGE)rc;
    gf_request_rehighlight(self);
}

void gf_action_help_instructions(GF_TEXTEDITOR *self)
{
    (void)self;
    const char *lines[] = {
        "=== MENU ===",
        "Alt+F, Alt+M, Alt+O, Alt+H aprono direttamente i menu File, Modifica,",
        "Option, Help (lettera sottolineata nella barra dei menu). In alternativa",
        "F10 attiva la barra dei menu; frecce per muoversi, Invio conferma, Esc annulla.",
        "",
        "=== MOVIMENTO CURSORE ===",
        "Frecce: spostano il cursore di un carattere/riga.",
        "Home / Fine: inizio/fine della riga corrente.",
        "Ctrl+Home / Ctrl+Fine: inizio/fine dell'intero documento.",
        "Pag-Su / Pag-Giu: scorrono di una schermata.",
        "",
        "=== SELEZIONE DEL TESTO ===",
        "Tenendo premuto Shift durante uno qualsiasi dei movimenti sopra",
        "(frecce, Home/Fine, Ctrl+Home/Fine, Pag-Su/Giu) si estende la selezione.",
        "",
        "=== MODIFICA DEL TESTO ===",
        "Ins: alterna modalita' inserimento/sovrascrittura.",
        "Canc: cancella il carattere sotto il cursore (o la selezione).",
        "Backspace: cancella il carattere a sinistra del cursore.",
        "Invio: va a capo. Tab: inserisce una tabulazione.",
        "Ctrl+Z: annulla l'ultima modifica (rollback, fino a 8 passi per file).",
        "Ctrl+S: salva subito il file corrente (equivalente a File->Save).",
        "",
        "=== RICERCA ===",
        "F3: cerca la prossima occorrenza. Shift+F3: cerca quella precedente.",
        "",
        "=== FILE MULTIPLI (SCHEDE) ===",
        "F8: passa al file successivo tra quelli aperti.",
        "F9: passa al file precedente.",
        "(Le stesse due azioni sono richiamabili anche da Modifica -> File",
        "successivo / File precedente). Copy/Cut/Paste condividono un unico",
        "buffer: si puo' copiare testo da un file e incollarlo in un altro.",
        "",
        "=== MENU FILE ===",
        "New, Open, Save, Save As, Close, Exit.",
        "Open e Save As aprono uno sfoglia-directory: frecce per muoversi,",
        "Invio su una cartella per entrarci, su '..' per risalire, su un file",
        "per selezionarlo. In Save As, l'ultima voce permette di digitare",
        "un nuovo nome nella cartella corrente. New crea il file nella",
        "cartella corrente (o al percorso assoluto specificato).",
        "",
        "=== MENU MODIFICA ===",
        "Undo, Copy, Cut, Paste, Del, Find, Replace, File successivo, File precedente.",
        "",
        "=== MENU OPTION ===",
        "Spaziatura tabulazione, colore testo, modalita' sviluppo (syntax",
        "highlighting), linguaggio per l'evidenziazione, timer di autosalvataggio",
        "(in secondi, 0 per disattivarlo; salva solo i file gia' salvati almeno",
        "una volta).",
    };
    gf_dialog_scrollable_text("Istruzioni", lines, (int)(sizeof(lines) / sizeof(lines[0])));
}

void gf_action_help_about(GF_TEXTEDITOR *self)
{
    (void)self;
    const char *lines[] = {
        "GF Text Editor",
        "",
        "Programmatore: Graziano Falcone",
        "E-mail: exagonx@hotmail.com",
        "Sito: exagonx.altervista.org",
        "",
        "Distribuito sotto licenza GNU General Public License v2.",
        "Selezionare 'Help -> License' per il testo completo.",
    };
    gf_dialog_message("About", lines, (int)(sizeof(lines) / sizeof(lines[0])));
}

void gf_action_help_license(GF_TEXTEDITOR *self)
{
    (void)self;
    const char *lines[] = {
        "GF Text Editor e' distribuito sotto licenza",
        "GNU General Public License, versione 2 (GPL v2).",
        "",
        "Il testo completo della licenza si trova nel file",
        "COPYING distribuito insieme al codice sorgente di",
        "questo programma.",
        "",
        "Non essendo modificabile senza alterarne la validita'",
        "legale, il testo della licenza non e' riprodotto qui:",
        "consultare il file COPYING oppure www.gnu.org/licenses/",
        "gpl-2.0.html per il testo ufficiale.",
    };
    gf_dialog_message("License", lines, (int)(sizeof(lines) / sizeof(lines[0])));
}

static void gf_start_or_extend_selection(GF_FILEAREA *area, int shift)
{
    if (shift) {
        if (!area->sel_active) {
            area->sel_active = 1;
            area->sel_start_row = area->cursor_row;
            area->sel_start_col = area->cursor_col;
        }
    } else {
        area->sel_active = 0;
    }
}

static void gf_commit_selection_end(GF_FILEAREA *area)
{
    if (area->sel_active) {
        area->sel_end_row = area->cursor_row;
        area->sel_end_col = area->cursor_col;
    }
}

static void gf_move_left(GF_FILEAREA *area, int shift)
{
    gf_start_or_extend_selection(area, shift);
    if (area->cursor_col > 0) {
        area->cursor_col = gf_utf8_prev_boundary(&area->doc.lines[area->cursor_row], area->cursor_col);
    } else if (area->cursor_row > 0) {
        area->cursor_row--;
        area->cursor_col = area->doc.lines[area->cursor_row].length;
    }
    if (!shift) area->sel_active = 0;
    gf_commit_selection_end(area);
}

static void gf_move_right(GF_FILEAREA *area, int shift)
{
    gf_start_or_extend_selection(area, shift);
    GF_LINE *ln = &area->doc.lines[area->cursor_row];
    if (area->cursor_col < ln->length) {
        area->cursor_col = gf_utf8_next_boundary(ln, area->cursor_col);
    } else if (area->cursor_row < area->doc.num_lines - 1) {
        area->cursor_row++;
        area->cursor_col = 0;
    }
    if (!shift) area->sel_active = 0;
    gf_commit_selection_end(area);
}

static void gf_move_up(GF_FILEAREA *area, int shift)
{
    gf_start_or_extend_selection(area, shift);
    if (area->cursor_row > 0) {
        area->cursor_row--;
        if (area->cursor_col > area->doc.lines[area->cursor_row].length)
            area->cursor_col = area->doc.lines[area->cursor_row].length;
    }
    if (!shift) area->sel_active = 0;
    gf_commit_selection_end(area);
}

static void gf_move_down(GF_FILEAREA *area, int shift)
{
    gf_start_or_extend_selection(area, shift);
    if (area->cursor_row < area->doc.num_lines - 1) {
        area->cursor_row++;
        if (area->cursor_col > area->doc.lines[area->cursor_row].length)
            area->cursor_col = area->doc.lines[area->cursor_row].length;
    }
    if (!shift) area->sel_active = 0;
    gf_commit_selection_end(area);
}

static void gf_move_home(GF_FILEAREA *area, int ctrl, int shift)
{
    gf_start_or_extend_selection(area, shift);
    if (ctrl) { area->cursor_row = 0; area->cursor_col = 0; }
    else area->cursor_col = 0;
    if (!shift) area->sel_active = 0;
    gf_commit_selection_end(area);
}

static void gf_move_end(GF_FILEAREA *area, int ctrl, int shift)
{
    gf_start_or_extend_selection(area, shift);
    if (ctrl) {
        area->cursor_row = area->doc.num_lines - 1;
        area->cursor_col = area->doc.lines[area->cursor_row].length;
    } else {
        area->cursor_col = area->doc.lines[area->cursor_row].length;
    }
    if (!shift) area->sel_active = 0;
    gf_commit_selection_end(area);
}

static void gf_move_page(GF_FILEAREA *area, int delta, int shift)
{
    gf_start_or_extend_selection(area, shift);
    area->cursor_row += delta;
    if (area->cursor_row < 0) area->cursor_row = 0;
    if (area->cursor_row > area->doc.num_lines - 1) area->cursor_row = area->doc.num_lines - 1;
    if (area->cursor_col > area->doc.lines[area->cursor_row].length)
        area->cursor_col = area->doc.lines[area->cursor_row].length;
    if (!shift) area->sel_active = 0;
    gf_commit_selection_end(area);
}

static void gf_delete_selection_if_any(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (!area->sel_active) return;
    int r1, c1, r2, c2;
    gf_get_selection_bounds(area, &r1, &c1, &r2, &c2);
    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_push_undo(area);
    gf_delete_range(area, r1, c1, r2, c2);
    area->cursor_row = r1;
    area->cursor_col = c1;
    area->sel_active = 0;
    area->modified = 1;
    pthread_mutex_unlock(&self->buffer_mutex);
}

static void gf_insert_char_at_cursor(GF_TEXTEDITOR *self, int ch)
{
    GF_FILEAREA *area = gf_current(self);
    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_push_undo(area);
    if (area->sel_active) {
        int r1, c1, r2, c2;
        gf_get_selection_bounds(area, &r1, &c1, &r2, &c2);
        gf_delete_range(area, r1, c1, r2, c2);
        area->cursor_row = r1;
        area->cursor_col = c1;
        area->sel_active = 0;
    }
    GF_LINE *ln = &area->doc.lines[area->cursor_row];
    if (area->insert_mode || area->cursor_col >= ln->length) {
        gf_line_insert_char(ln, area->cursor_col, (char)ch);
    } else {
        ln->text[area->cursor_col] = (char)ch;
    }
    area->cursor_col++;
    area->modified = 1;
    pthread_mutex_unlock(&self->buffer_mutex);
    self->chars_typed++;
    gf_request_rehighlight(self);
}

static void gf_read_utf8_and_insert(GF_TEXTEDITOR *self, unsigned char lead)
{
    int seqlen = gf_utf8_seqlen(lead);
    unsigned char bytes[4];
    bytes[0] = lead;
    int n = 1;

    if (seqlen > 1) {
        wtimeout(stdscr, 0);      /* lettura immediata non bloccante */
        while (n < seqlen) {
            int c = wgetch(stdscr);
            if (c == ERR) break;
            bytes[n++] = (unsigned char)c;
        }
        wtimeout(stdscr, GF_MAIN_TIMEOUT_MS); /* ripristina il timeout periodico */
    }

    GF_FILEAREA *area = gf_current(self);
    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_push_undo(area);
    if (area->sel_active) {
        int r1, c1, r2, c2;
        gf_get_selection_bounds(area, &r1, &c1, &r2, &c2);
        gf_delete_range(area, r1, c1, r2, c2);
        area->cursor_row = r1;
        area->cursor_col = c1;
        area->sel_active = 0;
    }
    GF_LINE *ln = &area->doc.lines[area->cursor_row];
    int i;
    for (i = 0; i < n; i++)
        gf_line_insert_char(ln, area->cursor_col + i, (char)bytes[i]);
    area->cursor_col += n;
    area->modified = 1;
    pthread_mutex_unlock(&self->buffer_mutex);
    self->chars_typed++;   /* conteggiato come 1 carattere logico, non n byte */
    gf_request_rehighlight(self);
}

static void gf_insert_newline_at_cursor(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_push_undo(area);
    if (area->sel_active) {
        int r1, c1, r2, c2;
        gf_get_selection_bounds(area, &r1, &c1, &r2, &c2);
        gf_delete_range(area, r1, c1, r2, c2);
        area->cursor_row = r1;
        area->cursor_col = c1;
        area->sel_active = 0;
    }
    GF_LINE *ln = &area->doc.lines[area->cursor_row];
    char *rest = strdup(ln->text + area->cursor_col);
    ln->text[area->cursor_col] = '\0';
    ln->length = area->cursor_col;
    gf_doc_insert_line(&area->doc, area->cursor_row + 1, rest);
    free(rest);
    area->cursor_row++;
    area->cursor_col = 0;
    area->modified = 1;
    pthread_mutex_unlock(&self->buffer_mutex);
    gf_request_rehighlight(self);
}

static void gf_backspace(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (area->sel_active) { gf_delete_selection_if_any(self); gf_request_rehighlight(self); return; }
    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_push_undo(area);
    if (area->cursor_col > 0) {
        GF_LINE *ln = &area->doc.lines[area->cursor_row];
        int prevpos = gf_utf8_prev_boundary(ln, area->cursor_col);
        int nbytes = area->cursor_col - prevpos, k;
        for (k = 0; k < nbytes; k++) gf_line_delete_char(ln, prevpos);
        area->cursor_col = prevpos;
        area->modified = 1;
    } else if (area->cursor_row > 0) {
        GF_LINE *prev = &area->doc.lines[area->cursor_row - 1];
        GF_LINE *cur = &area->doc.lines[area->cursor_row];
        int newcol = prev->length;
        gf_line_ensure_capacity(prev, prev->length + cur->length);
        memcpy(prev->text + prev->length, cur->text, (size_t)cur->length + 1);
        prev->length += cur->length;
        gf_doc_delete_line(&area->doc, area->cursor_row);
        area->cursor_row--;
        area->cursor_col = newcol;
        area->modified = 1;
    }
    pthread_mutex_unlock(&self->buffer_mutex);
    gf_request_rehighlight(self);
}

static void gf_delete_forward(GF_TEXTEDITOR *self)
{
    GF_FILEAREA *area = gf_current(self);
    if (area->sel_active) { gf_delete_selection_if_any(self); gf_request_rehighlight(self); return; }
    pthread_mutex_lock(&self->buffer_mutex);
    gf_area_push_undo(area);
    GF_LINE *ln = &area->doc.lines[area->cursor_row];
    if (area->cursor_col < ln->length) {
        int nextpos = gf_utf8_next_boundary(ln, area->cursor_col);
        int nbytes = nextpos - area->cursor_col, k;
        for (k = 0; k < nbytes; k++) gf_line_delete_char(ln, area->cursor_col);
        area->modified = 1;
    } else if (area->cursor_row < area->doc.num_lines - 1) {
        GF_LINE *next = &area->doc.lines[area->cursor_row + 1];
        gf_line_ensure_capacity(ln, ln->length + next->length);
        memcpy(ln->text + ln->length, next->text, (size_t)next->length + 1);
        ln->length += next->length;
        gf_doc_delete_line(&area->doc, area->cursor_row + 1);
        area->modified = 1;
    }
    pthread_mutex_unlock(&self->buffer_mutex);
    gf_request_rehighlight(self);
}

void gf_handle_edit_key(GF_TEXTEDITOR *self, int ch)
{
    if (self->num_open == 0) {
        /* nessun file aperto: solo F10 (menu) ha effetto, gestito dal chiamante */
        return;
    }
    GF_FILEAREA *area = gf_current(self);

    switch (ch) {
        case KEY_LEFT:  gf_move_left(area, 0); break;
        case KEY_RIGHT: gf_move_right(area, 0); break;
        case KEY_UP:    gf_move_up(area, 0); break;
        case KEY_DOWN:  gf_move_down(area, 0); break;
        case KEY_SLEFT: gf_move_left(area, 1); break;
        case KEY_SRIGHT:gf_move_right(area, 1); break;
        case KEY_SR:    gf_move_up(area, 1); break;
        case KEY_SF:    gf_move_down(area, 1); break;
        case KEY_HOME:  gf_move_home(area, 0, 0); break;
        case KEY_END:   gf_move_end(area, 0, 0); break;
        case KEY_SHOME: case GF_KEY_SHIFT_HOME: gf_move_home(area, 0, 1); break;
        case KEY_SEND:  case GF_KEY_SHIFT_END:  gf_move_end(area, 0, 1); break;
        case GF_KEY_CTRL_HOME: gf_move_home(area, 1, 0); break;
        case GF_KEY_CTRL_END:  gf_move_end(area, 1, 0); break;
        case KEY_PPAGE: gf_move_page(area, -(GF_TEXT_BOTTOM - GF_TEXT_TOP), 0); break;
        case KEY_NPAGE: gf_move_page(area, +(GF_TEXT_BOTTOM - GF_TEXT_TOP), 0); break;
        case KEY_IC:    area->insert_mode = !area->insert_mode; break;
        case KEY_DC:    gf_delete_forward(self); break;
        case KEY_BACKSPACE: case 127: case 8: gf_backspace(self); break;
        case '\n': case KEY_ENTER: gf_insert_newline_at_cursor(self); break;
        case '\t': gf_insert_char_at_cursor(self, '\t'); break;
        case KEY_F(3):  gf_find_next(self, 1); break;
        case GF_KEY_SHIFT_F3: gf_find_next(self, 0); break;
        case 26: case KEY_SUSPEND: gf_action_undo(self); break;  /* Ctrl+Z */
        case 19: gf_action_save(self); break;  /* Ctrl+S */
        default:
            if (ch >= 32 && ch < 256) gf_read_utf8_and_insert(self, (unsigned char)ch);
            break;
    }
}

void gf_next_tab(GF_TEXTEDITOR *self)
{
    if (self->num_open <= 1) return;
    int i = self->current_tab;
    do { i = (i + 1) % GF_MAX_TABS; } while (!self->tabs[i].in_use);
    self->current_tab = i;
    gf_request_rehighlight(self);
}

void gf_prev_tab(GF_TEXTEDITOR *self)
{
    if (self->num_open <= 1) return;
    int i = self->current_tab;
    do { i = (i + GF_MAX_TABS - 1) % GF_MAX_TABS; } while (!self->tabs[i].in_use);
    self->current_tab = i;
    gf_request_rehighlight(self);
}

