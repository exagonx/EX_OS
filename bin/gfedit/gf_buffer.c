/* =============================================================================
 * bin/gfedit/gf_buffer.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * GF Edit — il documento: righe, aree, selezione, annullamento, appunti.
 *
 * Tutto ciò che modifica il testo passa da qui, e da nessun'altra parte:
 * è quello che permette al giornale dell'undo di registrarsi da solo
 * (vedi undo_attivo in gfedit.h) invece di dipendere dalla disciplina
 * di chi chiama.
 * ============================================================================= */

#include "gfedit.h"

/* =============================================================================
 * Utilità sui caratteri — la libc di EX-OS non ha <ctype.h>
 * ============================================================================= */
int gf_e_spazio(char c) { return c == ' ' || c == '\t'; }
int gf_e_cifra(char c)  { return c >= '0' && c <= '9'; }

int gf_e_alpha(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int gf_e_ident(char c)
{
    return gf_e_alpha(c) || gf_e_cifra(c) || c == '_';
}

char gf_minuscolo(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

int gf_str_uguale_ci(const char *a, const char *b)
{
    while (*a && *b) {
        if (gf_minuscolo(*a) != gf_minuscolo(*b)) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* =============================================================================
 * Accesso alle righe
 *
 * gf_riga() non torna mai NULL nemmeno per un indice fuori scala: dà una
 * riga vuota di servizio. Un editor chiede la riga corrente in decine di
 * punti, e propagare un NULL in tutti significherebbe un controllo per
 * chiamata — con la certezza statistica di dimenticarne uno e di
 * scoprirlo con un page fault invece che con un carattere fuori posto.
 * ============================================================================= */
static char riga_vuota[GF_LINE_STRIDE];

char *gf_riga(GfTab *t, int i)
{
    if (!t || !t->d || i < 0 || i >= GF_MAX_LINES) {
        riga_vuota[0] = '\0';
        return riga_vuota;
    }
    return t->d->text[i];
}

int gf_riga_len(GfTab *t, int i)
{
    return (int)strlen(gf_riga(t, i));
}

GfTab *gf_corrente(GfEdit *self)
{
    return &self->tabs[self->tab_corrente];
}

int gf_col_schermo(GfEdit *self, const char *s, int byte_col)
{
    int tabw = self->opz.tab_width;
    int dc = 0, i;

    for (i = 0; i < byte_col && s[i]; i++) {
        if (s[i] == '\t') dc += tabw - (dc % tabw);
        else              dc++;
    }
    return dc;
}

/* =============================================================================
 * Allocazione dei dati di un'area
 *
 * Una malloc() sola, alla prima volta che l'area serve, e mai una free():
 * l'allocatore di EX-OS è a bump su sbrk e la free() è dichiaratamente un
 * no-op (lib/libc.c). Riaprire un file in un'area già usata riusa la
 * stessa memoria; chiuderla non la restituisce, ma non la spreca
 * nemmeno — il tetto resta GF_MAX_TABS blocchi per tutta la sessione.
 * ============================================================================= */
int gf_tab_prepara(GfTab *t)
{
    if (t->d) return 0;

    t->d = (GfTabData *)malloc(sizeof(GfTabData));
    if (!t->d) return -1;

    memset(t->d, 0, sizeof(GfTabData));
    return 0;
}

void gf_tab_azzera(GfTab *t, GfLingua lingua)
{
    t->num_lines   = 1;
    t->modified    = 0;
    t->insert_mode = 1;
    t->troncato    = 0;
    t->cursor_row  = 0;
    t->cursor_col  = 0;
    t->view_top    = 0;
    t->view_left   = 0;
    t->sel_active  = 0;
    t->sel_row     = 0;
    t->sel_col     = 0;
    t->lingua      = lingua;

    t->undo_head       = 0;
    t->undo_count      = 0;
    t->undo_gruppo     = 0;
    t->undo_incompleto = 0;
    t->undo_attivo     = 1;

    if (t->d) {
        memset(t->d->text, 0, sizeof(t->d->text));
        memset(t->in_commento, 0, sizeof(t->in_commento));
    }
}

int gf_tab_libera_indice(GfEdit *self)
{
    int i;

    for (i = 0; i < GF_MAX_TABS; i++) {
        if (!self->tabs[i].in_use) return i;
    }
    return -1;
}

/* =============================================================================
 * Giornale dell'annullamento
 * ============================================================================= */
void gf_undo_apri(GfTab *t)
{
    t->undo_gruppo++;
}

static void undo_registra(GfTab *t, GfUndoOp op, int row, const char *testo)
{
    GfUndo *u;

    if (!t->d || !t->undo_attivo) return;

    /* Ring pieno: il record più vecchio viene scartato. Se apparteneva a
     * un gruppo di cui restano altri pezzi, quel gruppo non è più
     * annullabile per intero e va marcato — applicarne metà lascerebbe
     * il documento in uno stato che l'utente non ha mai visto. */
    if (t->undo_count == GF_UNDO_MAX) {
        GfUndo *vecchio = &t->d->undo[t->undo_head];
        t->undo_incompleto = vecchio->gruppo;
        t->undo_count--;
    }

    u = &t->d->undo[t->undo_head];
    t->undo_head = (t->undo_head + 1) % GF_UNDO_MAX;
    t->undo_count++;

    u->op      = (unsigned char)op;
    u->gruppo  = t->undo_gruppo;
    u->row     = row;
    u->cur_row = t->cursor_row;
    u->cur_col = t->cursor_col;

    if (testo) gf_strlcpy(u->text, testo, GF_LINE_STRIDE);
    else       u->text[0] = '\0';
}

/* Record più recente (in coda al ring), o NULL. */
static GfUndo *undo_cima(GfTab *t)
{
    int idx;

    if (!t->d || t->undo_count == 0) return NULL;
    idx = (t->undo_head - 1 + GF_UNDO_MAX) % GF_UNDO_MAX;
    return &t->d->undo[idx];
}

/* =============================================================================
 * Modifiche elementari
 *
 * Tutte e tre registrano da sole nel giornale, PRIMA di toccare il testo:
 * è l'unico momento in cui lo stato precedente esiste ancora.
 * ============================================================================= */
void gf_riga_imposta(GfTab *t, int i, const char *s)
{
    if (!t->d || i < 0 || i >= GF_MAX_LINES) return;

    undo_registra(t, GF_U_MODIFICA, i, t->d->text[i]);
    gf_strlcpy(t->d->text[i], s, GF_MAX_COL + 1);
    t->modified = 1;
}

int gf_riga_inserisci(GfTab *t, int pos, const char *s)
{
    int i;

    if (!t->d) return -1;
    if (t->num_lines >= GF_MAX_LINES) return -1;
    if (pos < 0) pos = 0;
    if (pos > t->num_lines) pos = t->num_lines;

    for (i = t->num_lines; i > pos; i--) {
        memcpy(t->d->text[i], t->d->text[i - 1], GF_LINE_STRIDE);
        t->in_commento[i] = t->in_commento[i - 1];
    }

    gf_strlcpy(t->d->text[pos], s ? s : "", GF_MAX_COL + 1);
    t->num_lines++;
    t->modified = 1;

    undo_registra(t, GF_U_INSERITA, pos, NULL);
    return 0;
}

int gf_riga_cancella(GfTab *t, int pos)
{
    int i;

    if (!t->d || pos < 0 || pos >= t->num_lines) return -1;

    /* Un documento non è mai davvero vuoto: resta sempre una riga, anche
     * se di lunghezza zero. Senza, il cursore non avrebbe dove stare e
     * ogni funzione dovrebbe trattare il caso "nessuna riga". */
    if (t->num_lines == 1) {
        gf_riga_imposta(t, 0, "");
        return 0;
    }

    undo_registra(t, GF_U_CANCELLATA, pos, t->d->text[pos]);

    for (i = pos; i < t->num_lines - 1; i++) {
        memcpy(t->d->text[i], t->d->text[i + 1], GF_LINE_STRIDE);
        t->in_commento[i] = t->in_commento[i + 1];
    }
    t->d->text[t->num_lines - 1][0] = '\0';
    t->num_lines--;
    t->modified = 1;
    return 0;
}

/* =============================================================================
 * gf_undo_applica — disfa l'ultimo gruppo
 *
 * I record si consumano in ordine inverso a quello di registrazione: è
 * l'unico che riporta esattamente allo stato di partenza quando un
 * gruppo contiene inserimenti e cancellazioni mescolati, perché ogni
 * record parla di indici di riga validi nel momento in cui è stato
 * scritto, e quel momento si ricostruisce solo tornando indietro.
 * ============================================================================= */
int gf_undo_applica(GfTab *t)
{
    GfUndo      *u = undo_cima(t);
    unsigned int gruppo;
    int          cur_row, cur_col;
    int          attivo_prima;

    if (!u) return -1;
    if (u->gruppo == t->undo_incompleto) return -2;

    gruppo  = u->gruppo;
    cur_row = u->cur_row;
    cur_col = u->cur_col;

    /* Disfare non si registra: sarebbe un anello, e il primo undo
     * riempirebbe il giornale della propria immagine speculare. */
    attivo_prima   = t->undo_attivo;
    t->undo_attivo = 0;

    while ((u = undo_cima(t)) != NULL && u->gruppo == gruppo) {
        switch ((GfUndoOp)u->op) {
            case GF_U_MODIFICA:
                if (u->row >= 0 && u->row < GF_MAX_LINES)
                    gf_strlcpy(t->d->text[u->row], u->text, GF_MAX_COL + 1);
                break;

            case GF_U_INSERITA:
                gf_riga_cancella(t, u->row);
                break;

            case GF_U_CANCELLATA:
                gf_riga_inserisci(t, u->row, u->text);
                break;
        }

        cur_row = u->cur_row;
        cur_col = u->cur_col;
        t->undo_head = (t->undo_head - 1 + GF_UNDO_MAX) % GF_UNDO_MAX;
        t->undo_count--;
    }

    t->undo_attivo = attivo_prima;

    if (t->num_lines < 1) t->num_lines = 1;
    if (cur_row >= t->num_lines) cur_row = t->num_lines - 1;
    if (cur_row < 0) cur_row = 0;
    if (cur_col > gf_riga_len(t, cur_row)) cur_col = gf_riga_len(t, cur_row);
    if (cur_col < 0) cur_col = 0;

    t->cursor_row = cur_row;
    t->cursor_col = cur_col;
    t->sel_active = 0;
    t->modified   = 1;
    return 0;
}

/* =============================================================================
 * Selezione
 *
 * Un'ancora (sel_row, sel_col) più il cursore. L'originale teneva due
 * coppie di coordinate, ma una delle due era sempre il cursore: due
 * copie della stessa cosa che potevano divergere.
 * ============================================================================= */
void gf_sel_normalizza(GfTab *t, int *r1, int *c1, int *r2, int *c2)
{
    if (t->sel_row < t->cursor_row ||
        (t->sel_row == t->cursor_row && t->sel_col <= t->cursor_col)) {
        *r1 = t->sel_row;    *c1 = t->sel_col;
        *r2 = t->cursor_row; *c2 = t->cursor_col;
    } else {
        *r1 = t->cursor_row; *c1 = t->cursor_col;
        *r2 = t->sel_row;    *c2 = t->sel_col;
    }
}

int gf_sel_estrai(GfTab *t, char *out, int max)
{
    int r1, c1, r2, c2, r, n = 0;

    if (!t->sel_active) return 0;
    gf_sel_normalizza(t, &r1, &c1, &r2, &c2);

    for (r = r1; r <= r2 && n < max - 1; r++) {
        const char *riga = gf_riga(t, r);
        int len = (int)strlen(riga);
        int da  = (r == r1) ? c1 : 0;
        int a   = (r == r2) ? c2 : len;
        int i;

        if (da > len) da = len;
        if (a  > len) a  = len;

        for (i = da; i < a && n < max - 1; i++) out[n++] = riga[i];
        if (r < r2 && n < max - 1) out[n++] = '\n';
    }

    out[n] = '\0';
    return n;
}

void gf_sel_cancella(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    int    r1, c1, r2, c2;
    char   coda[GF_LINE_STRIDE];
    char   fusa[GF_LINE_STRIDE];
    const char *riga;
    int    len, i;

    if (!t->sel_active) return;
    gf_sel_normalizza(t, &r1, &c1, &r2, &c2);

    /* La riga finale contribuisce la propria CODA, che va agganciata a
     * ciò che resta della riga iniziale: è la sola parte non contigua
     * dell'operazione, e va copiata prima che le righe in mezzo
     * spariscano sotto i piedi. */
    riga = gf_riga(t, r2);
    len  = (int)strlen(riga);
    if (c2 > len) c2 = len;
    gf_strlcpy(coda, riga + c2, GF_LINE_STRIDE);

    riga = gf_riga(t, r1);
    len  = (int)strlen(riga);
    if (c1 > len) c1 = len;

    for (i = 0; i < c1 && i < GF_MAX_COL; i++) fusa[i] = riga[i];
    fusa[i] = '\0';
    {
        int j = 0;
        while (coda[j] && i < GF_MAX_COL) fusa[i++] = coda[j++];
        fusa[i] = '\0';
    }

    for (i = r2; i > r1; i--) gf_riga_cancella(t, i);
    gf_riga_imposta(t, r1, fusa);

    t->cursor_row = r1;
    t->cursor_col = c1;
    t->sel_active = 0;
}

/* =============================================================================
 * Appunti
 * ============================================================================= */
void gf_clip_imposta(GfEdit *self, const char *data, int len, int a_righe)
{
    if (len > GF_CLIP_MAX - 1) len = GF_CLIP_MAX - 1;
    if (len < 0) len = 0;

    memcpy(self->clip.data, data, (size_t)len);
    self->clip.data[len] = '\0';
    self->clip.len       = len;
    self->clip.a_righe   = a_righe;
}
