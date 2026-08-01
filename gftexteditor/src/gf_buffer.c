/* ============================================================================
 * gf_buffer.c
 *
 * Parte dell'implementazione di GF_TEXTEDITOR - vedi gf_texteditor.h per
 * la descrizione dell'oggetto e gf_internal.h per le strutture e i
 * prototipi condivisi tra i moduli.
 *
 * Buffer riga/documento (GF_LINE/GF_DOCBUFFER), gestione UTF-8 a livello
 * di byte, ring buffer di Undo, appunti (clipboard), estrazione/cancellazione
 * di intervalli di testo selezionato.
 *
 * Autore : Graziano Falcone  <exagonx@hotmail.com>
 * Licenza: GNU GPL v2 (vedi Help->License nel programma, e il file
 *          COPYING distribuito insieme al codice sorgente)
 * ==========================================================================*/

#define _GNU_SOURCE
#include "gf_internal.h"

int gf_is_utf8_cont(unsigned char b)
{
    return (b & 0xC0) == 0x80;
}

int gf_utf8_seqlen(unsigned char lead)
{
    if ((lead & 0x80) == 0x00) return 1;   /* 0xxxxxxx - ASCII           */
    if ((lead & 0xE0) == 0xC0) return 2;   /* 110xxxxx - 2 byte          */
    if ((lead & 0xF0) == 0xE0) return 3;   /* 1110xxxx - 3 byte          */
    if ((lead & 0xF8) == 0xF0) return 4;   /* 11110xxx - 4 byte          */
    return 1;
}

int gf_utf8_prev_boundary(GF_LINE *ln, int col)
{
    int c = col - 1;
    while (c > 0 && gf_is_utf8_cont((unsigned char)ln->text[c])) c--;
    return c;
}

int gf_utf8_next_boundary(GF_LINE *ln, int col)
{
    if (col >= ln->length) return ln->length;
    int len = gf_utf8_seqlen((unsigned char)ln->text[col]);
    int c = col + len;
    if (c > ln->length) c = ln->length;
    return c;
}

int gf_utf8_count_chars(GF_LINE *ln, int from_byte, int to_byte)
{
    int count = 0, pos = from_byte;
    while (pos < to_byte && pos < ln->length) {
        pos += gf_utf8_seqlen((unsigned char)ln->text[pos]);
        count++;
    }
    return count;
}

void gf_line_init(GF_LINE *ln)
{
    ln->capacity = 32;
    ln->text = (char *)malloc((size_t)ln->capacity);
    ln->text[0] = '\0';
    ln->length = 0;
    ln->hl_capacity = 32;
    ln->hl = (unsigned char *)calloc((size_t)ln->hl_capacity, 1);
}

void gf_line_free(GF_LINE *ln)
{
    free(ln->text);
    free(ln->hl);
    ln->text = NULL;
    ln->hl = NULL;
    ln->length = ln->capacity = ln->hl_capacity = 0;
}

void gf_line_ensure_capacity(GF_LINE *ln, int needed)
{
    if (needed + 1 > ln->capacity) {
        while (ln->capacity < needed + 1) ln->capacity *= 2;
        ln->text = (char *)realloc(ln->text, (size_t)ln->capacity);
    }
    if (needed + 1 > ln->hl_capacity) {
        while (ln->hl_capacity < needed + 1) ln->hl_capacity *= 2;
        ln->hl = (unsigned char *)realloc(ln->hl, (size_t)ln->hl_capacity);
    }
}

void gf_line_set(GF_LINE *ln, const char *s)
{
    int len = (int)strlen(s);
    gf_line_ensure_capacity(ln, len);
    memcpy(ln->text, s, (size_t)len + 1);
    ln->length = len;
    memset(ln->hl, GF_TOK_NORMAL, (size_t)ln->hl_capacity);
}

void gf_line_insert_char(GF_LINE *ln, int col, char c)
{
    if (col < 0) col = 0;
    if (col > ln->length) col = ln->length;
    gf_line_ensure_capacity(ln, ln->length + 1);
    memmove(ln->text + col + 1, ln->text + col, (size_t)(ln->length - col) + 1);
    ln->text[col] = c;
    ln->length++;
    ln->text[ln->length] = '\0';
}

void gf_line_delete_char(GF_LINE *ln, int col)
{
    if (col < 0 || col >= ln->length) return;
    memmove(ln->text + col, ln->text + col + 1, (size_t)(ln->length - col));
    ln->length--;
}

void gf_doc_init(GF_DOCBUFFER *doc)
{
    doc->capacity = 64;
    doc->lines = (GF_LINE *)malloc(sizeof(GF_LINE) * (size_t)doc->capacity);
    doc->num_lines = 0;
}

void gf_doc_free(GF_DOCBUFFER *doc)
{
    int i;
    for (i = 0; i < doc->num_lines; i++) gf_line_free(&doc->lines[i]);
    free(doc->lines);
    doc->lines = NULL;
    doc->num_lines = doc->capacity = 0;
}

void gf_doc_ensure_capacity(GF_DOCBUFFER *doc, int needed)
{
    if (needed > doc->capacity) {
        while (doc->capacity < needed) doc->capacity *= 2;
        doc->lines = (GF_LINE *)realloc(doc->lines, sizeof(GF_LINE) * (size_t)doc->capacity);
    }
}

void gf_doc_insert_line(GF_DOCBUFFER *doc, int idx, const char *text)
{
    if (idx < 0) idx = 0;
    if (idx > doc->num_lines) idx = doc->num_lines;
    gf_doc_ensure_capacity(doc, doc->num_lines + 1);
    memmove(&doc->lines[idx + 1], &doc->lines[idx],
            sizeof(GF_LINE) * (size_t)(doc->num_lines - idx));
    gf_line_init(&doc->lines[idx]);
    gf_line_set(&doc->lines[idx], text);
    doc->num_lines++;
}

void gf_doc_delete_line(GF_DOCBUFFER *doc, int idx)
{
    if (idx < 0 || idx >= doc->num_lines) return;
    gf_line_free(&doc->lines[idx]);
    memmove(&doc->lines[idx], &doc->lines[idx + 1],
            sizeof(GF_LINE) * (size_t)(doc->num_lines - idx - 1));
    doc->num_lines--;
}

void gf_doc_deep_copy(GF_DOCBUFFER *dst, const GF_DOCBUFFER *src)
{
    dst->capacity = (src->num_lines > 0) ? src->num_lines : 1;
    dst->lines = (GF_LINE *)malloc(sizeof(GF_LINE) * (size_t)dst->capacity);
    dst->num_lines = src->num_lines;
    int i;
    for (i = 0; i < src->num_lines; i++) {
        GF_LINE *s = &src->lines[i];
        GF_LINE *d = &dst->lines[i];
        d->capacity = s->capacity;
        d->text = (char *)malloc((size_t)d->capacity);
        memcpy(d->text, s->text, (size_t)s->length + 1);
        d->length = s->length;
        d->hl_capacity = s->hl_capacity;
        d->hl = (unsigned char *)malloc((size_t)d->hl_capacity);
        memcpy(d->hl, s->hl, (size_t)d->hl_capacity);
    }
}

void gf_area_free_undo_stack(GF_FILEAREA *area)
{
    int i;
    for (i = 0; i < GF_UNDO_MAX; i++) gf_doc_free(&area->undo_stack[i]);
    area->undo_count = 0;
    area->undo_head = 0;
}

void gf_area_push_undo(GF_FILEAREA *area)
{
    int idx = area->undo_head;
    if (area->undo_count == GF_UNDO_MAX) {
        /* il ring e' pieno: questo slot contiene lo stato piu' vecchio,
         * che stiamo per scartare definitivamente per far posto al nuovo */
        gf_doc_free(&area->undo_stack[idx]);
    }
    gf_doc_deep_copy(&area->undo_stack[idx], &area->doc);
    area->undo_cursor_row[idx] = area->cursor_row;
    area->undo_cursor_col[idx] = area->cursor_col;
    area->undo_head = (area->undo_head + 1) % GF_UNDO_MAX;
    if (area->undo_count < GF_UNDO_MAX) area->undo_count++;
}

void gf_area_reset(GF_FILEAREA *area, GF_LANGUAGE lang)
{
    gf_area_free_undo_stack(area); /* difensivo: libera eventuali snapshot residui */
    memset(area, 0, sizeof(*area));
    area->in_use = 1;
    gf_doc_init(&area->doc);
    gf_doc_insert_line(&area->doc, 0, "");
    strncpy(area->filename, "senzanome.txt", GF_MAX_FILENAME - 1);
    area->has_path = 0;
    area->insert_mode = 1;
    area->language = lang;
    area->cursor_row = area->cursor_col = 0;
    area->view_top = area->view_left = 0;
}

int gf_find_free_area_index(GF_TEXTEDITOR *self)
{
    int i;
    for (i = 0; i < GF_MAX_TABS; i++)
        if (!self->tabs[i].in_use) return i;
    return -1;
}

GF_FILEAREA *gf_current(GF_TEXTEDITOR *self)
{
    return &self->tabs[self->current_tab];
}

void gf_get_selection_bounds(GF_FILEAREA *area, int *r1, int *c1, int *r2, int *c2)
{
    int sr = area->sel_start_row, sc = area->sel_start_col;
    int er = area->sel_end_row,   ec = area->sel_end_col;
    if (sr > er || (sr == er && sc > ec)) {
        *r1 = er; *c1 = ec; *r2 = sr; *c2 = sc;
    } else {
        *r1 = sr; *c1 = sc; *r2 = er; *c2 = ec;
    }
}

void gf_clipboard_set(GF_CLIPBOARD *cb, const char *data, int len)
{
    if (len + 1 > cb->capacity) {
        cb->capacity = len + 64;
        cb->data = (char *)realloc(cb->data, (size_t)cb->capacity);
    }
    memcpy(cb->data, data, (size_t)len);
    cb->data[len] = '\0';
    cb->length = len;
}

char *gf_extract_range(GF_FILEAREA *area, int r1, int c1, int r2, int c2)
{
    int cap = 256, len = 0;
    char *out = (char *)malloc((size_t)cap);
    out[0] = '\0';

    if (r1 == r2) {
        GF_LINE *ln = &area->doc.lines[r1];
        int a = c1, b = c2; if (b > ln->length) b = ln->length;
        if (b > a) {
            int n = b - a;
            if (len + n + 1 > cap) { cap = len + n + 1; out = (char *)realloc(out, (size_t)cap); }
            memcpy(out + len, ln->text + a, (size_t)n);
            len += n; out[len] = '\0';
        }
    } else {
        GF_LINE *first = &area->doc.lines[r1];
        int n = first->length - c1;
        if (len + n + 2 > cap) { cap = len + n + 64; out = (char *)realloc(out, (size_t)cap); }
        memcpy(out + len, first->text + c1, (size_t)n); len += n;
        out[len++] = '\n'; out[len] = '\0';

        int r;
        for (r = r1 + 1; r < r2; r++) {
            GF_LINE *mid = &area->doc.lines[r];
            n = mid->length;
            if (len + n + 2 > cap) { cap = len + n + 64; out = (char *)realloc(out, (size_t)cap); }
            memcpy(out + len, mid->text, (size_t)n); len += n;
            out[len++] = '\n'; out[len] = '\0';
        }
        GF_LINE *last = &area->doc.lines[r2];
        int b = c2; if (b > last->length) b = last->length;
        if (len + b + 1 > cap) { cap = len + b + 1; out = (char *)realloc(out, (size_t)cap); }
        memcpy(out + len, last->text, (size_t)b); len += b;
        out[len] = '\0';
    }
    return out;
}

void gf_delete_range(GF_FILEAREA *area, int r1, int c1, int r2, int c2)
{
    if (r1 == r2) {
        GF_LINE *ln = &area->doc.lines[r1];
        int b = c2; if (b > ln->length) b = ln->length;
        int n = b - c1, i;
        for (i = 0; i < n; i++) gf_line_delete_char(ln, c1);
    } else {
        GF_LINE *first = &area->doc.lines[r1];
        GF_LINE *last  = &area->doc.lines[r2];
        int lastlen = last->length;
        int b = c2; if (b > lastlen) b = lastlen;
        int tail_len = lastlen - b;
        first->length = c1;
        first->text[c1] = '\0';
        gf_line_ensure_capacity(first, first->length + tail_len);
        memcpy(first->text + first->length, last->text + b, (size_t)tail_len);
        first->length += tail_len;
        first->text[first->length] = '\0';
        int r;
        for (r = r2; r > r1; r--) gf_doc_delete_line(&area->doc, r);
    }
    area->cursor_row = r1;
    area->cursor_col = c1;
    area->sel_active = 0;
    area->modified = 1;
}

