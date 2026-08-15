/* =============================================================================
 * bin/gfedit/gf_edit.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * GF Edit — movimento del cursore, editing, ricerca, azioni dei menu.
 *
 * Ogni azione dell'utente apre UN gruppo di annullamento e poi modifica
 * quanto le serve: è così che incolla, cancella-selezione e sostituisci-
 * tutto si disfano in un colpo solo invece che una riga alla volta.
 * ============================================================================= */

#include "gfedit.h"

int gf_menu_da_lettera(char ch);

/* =============================================================================
 * Vista: tiene il cursore dentro le 21 righe e le 80 colonne visibili
 * ============================================================================= */
void gf_aggiusta_vista(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    int    cs;

    if (t->cursor_row < 0) t->cursor_row = 0;
    if (t->cursor_row >= t->num_lines) t->cursor_row = t->num_lines - 1;

    {
        int len = gf_riga_len(t, t->cursor_row);
        if (t->cursor_col > len) t->cursor_col = len;
        if (t->cursor_col < 0)   t->cursor_col = 0;
    }

    if (t->cursor_row < t->view_top) t->view_top = t->cursor_row;
    if (t->cursor_row >= t->view_top + GF_TEXT_ROWS)
        t->view_top = t->cursor_row - GF_TEXT_ROWS + 1;
    if (t->view_top < 0) t->view_top = 0;

    cs = gf_col_schermo(self, gf_riga(t, t->cursor_row), t->cursor_col);
    if (cs < t->view_left) t->view_left = cs;
    if (cs >= t->view_left + GF_COLS) t->view_left = cs - GF_COLS + 1;
    if (t->view_left < 0) t->view_left = 0;
}

/* Da chiamare dopo ogni modifica al testo: l'evidenziazione dipende da
 * uno stato che attraversa le righe (i commenti di blocco), e quello va
 * ricostruito o le righe sotto al punto modificato restano colorate
 * come prima della modifica. */
static void dopo_modifica(GfEdit *self)
{
    GfTab *t = gf_corrente(self);

    gf_ricalcola_commenti(t);
    self->caratteri_digitati++;
}

/* =============================================================================
 * Editing elementare
 * ============================================================================= */
static void inserisci_carattere(GfEdit *self, char c)
{
    GfTab *t = gf_corrente(self);
    char   nuova[GF_LINE_STRIDE];
    const char *s = gf_riga(t, t->cursor_row);
    int    len = (int)strlen(s);
    int    i, n = 0;

    if (len >= GF_MAX_COL) {
        gf_msg(self, "Riga piena: massimo 200 caratteri");
        return;
    }
    if (t->cursor_col > len) t->cursor_col = len;

    for (i = 0; i < t->cursor_col; i++) nuova[n++] = s[i];
    nuova[n++] = c;

    /* In sovrascrittura il carattere sotto il cursore sparisce, tranne a
     * fine riga dove non c'è nulla da sovrascrivere e il tasto si
     * comporta comunque da inserimento. */
    i = t->cursor_col + ((!t->insert_mode && t->cursor_col < len) ? 1 : 0);
    for (; i < len && n < GF_MAX_COL; i++) nuova[n++] = s[i];
    nuova[n] = '\0';

    gf_riga_imposta(t, t->cursor_row, nuova);
    t->cursor_col++;
}

static void spezza_riga(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    char   sinistra[GF_LINE_STRIDE];
    char   destra[GF_LINE_STRIDE];
    const char *s = gf_riga(t, t->cursor_row);
    int    len = (int)strlen(s);
    int    i;

    if (t->num_lines >= GF_MAX_LINES) {
        gf_msg(self, "Documento pieno: massimo 512 righe");
        return;
    }
    if (t->cursor_col > len) t->cursor_col = len;

    for (i = 0; i < t->cursor_col; i++) sinistra[i] = s[i];
    sinistra[t->cursor_col] = '\0';
    gf_strlcpy(destra, s + t->cursor_col, GF_LINE_STRIDE);

    gf_riga_imposta(t, t->cursor_row, sinistra);
    gf_riga_inserisci(t, t->cursor_row + 1, destra);

    t->cursor_row++;
    t->cursor_col = 0;
}

/* Attacca la riga 'row+1' in coda alla riga 'row'. Il cursore finisce
 * nel punto di giunzione, che è dove l'utente si aspetta di trovarlo
 * dopo un Backspace a inizio riga. */
static void unisci_righe(GfEdit *self, int row)
{
    GfTab *t = gf_corrente(self);
    char   fusa[GF_LINE_STRIDE];
    const char *a, *b;
    int    la, i, n;

    if (row < 0 || row + 1 >= t->num_lines) return;

    a  = gf_riga(t, row);
    la = (int)strlen(a);
    b  = gf_riga(t, row + 1);

    for (i = 0, n = 0; i < la && n < GF_MAX_COL; i++) fusa[n++] = a[i];
    for (i = 0; b[i] && n < GF_MAX_COL; i++) fusa[n++] = b[i];
    fusa[n] = '\0';

    if (la + (int)strlen(b) > GF_MAX_COL) {
        gf_msg(self, "Righe unite parzialmente: superato il limite di 200 caratteri");
    }

    gf_riga_imposta(t, row, fusa);
    gf_riga_cancella(t, row + 1);

    t->cursor_row = row;
    t->cursor_col = la;
}

/* =============================================================================
 * Movimento
 * ============================================================================= */
static void muovi(GfEdit *self, unsigned base, int shift)
{
    GfTab *t = gf_corrente(self);
    int    len;

    /* Shift + movimento estende la selezione; senza Shift la si abbandona.
     * L'ancora si pianta al primo movimento con Shift, non prima: è la
     * posizione da cui l'utente ha cominciato a selezionare. */
    if (shift) {
        if (!t->sel_active) {
            t->sel_active = 1;
            t->sel_row    = t->cursor_row;
            t->sel_col    = t->cursor_col;
        }
    } else {
        t->sel_active = 0;
    }

    switch (base) {
        case KBD_K_LEFT:
            if (t->cursor_col > 0) t->cursor_col--;
            else if (t->cursor_row > 0) {
                t->cursor_row--;
                t->cursor_col = gf_riga_len(t, t->cursor_row);
            }
            break;

        case KBD_K_RIGHT:
            len = gf_riga_len(t, t->cursor_row);
            if (t->cursor_col < len) t->cursor_col++;
            else if (t->cursor_row < t->num_lines - 1) {
                t->cursor_row++;
                t->cursor_col = 0;
            }
            break;

        case KBD_K_UP:
            if (t->cursor_row > 0) t->cursor_row--;
            break;

        case KBD_K_DOWN:
            if (t->cursor_row < t->num_lines - 1) t->cursor_row++;
            break;

        case KBD_K_HOME:
            t->cursor_col = 0;
            break;

        case KBD_K_END:
            t->cursor_col = gf_riga_len(t, t->cursor_row);
            break;

        case KBD_K_PGUP:
            t->cursor_row -= GF_TEXT_ROWS;
            t->view_top   -= GF_TEXT_ROWS;
            if (t->cursor_row < 0) t->cursor_row = 0;
            if (t->view_top < 0)   t->view_top = 0;
            break;

        case KBD_K_PGDN:
            t->cursor_row += GF_TEXT_ROWS;
            t->view_top   += GF_TEXT_ROWS;
            if (t->cursor_row >= t->num_lines) t->cursor_row = t->num_lines - 1;
            if (t->view_top > t->num_lines - 1) t->view_top = t->num_lines - 1;
            if (t->view_top < 0) t->view_top = 0;
            break;

        default:
            break;
    }

    gf_aggiusta_vista(self);
}

/* =============================================================================
 * Aree
 * ============================================================================= */
static void vai_a_tab(GfEdit *self, int idx)
{
    if (idx < 0 || idx >= GF_MAX_TABS) return;
    if (!self->tabs[idx].in_use) return;
    self->tab_corrente = idx;
}

void gf_tab_successiva(GfEdit *self)
{
    int i;

    for (i = 1; i <= GF_MAX_TABS; i++) {
        int k = (self->tab_corrente + i) % GF_MAX_TABS;
        if (self->tabs[k].in_use) { self->tab_corrente = k; return; }
    }
}

void gf_tab_precedente(GfEdit *self)
{
    int i;

    for (i = 1; i <= GF_MAX_TABS; i++) {
        int k = (self->tab_corrente + GF_MAX_TABS - i) % GF_MAX_TABS;
        if (self->tabs[k].in_use) { self->tab_corrente = k; return; }
    }
}

/* =============================================================================
 * gf_tasto — smistamento di un evento tasto
 * ============================================================================= */
void gf_tasto(GfEdit *self, unsigned key)
{
    GfTab   *t     = gf_corrente(self);
    unsigned base  = key & KBD_KEY_MASK;
    int      ctrl  = (key & KBD_MOD_CTRL)  != 0;
    int      shift = (key & KBD_MOD_SHIFT) != 0;
    int      alt   = (key & KBD_MOD_ALT)   != 0;

    self->messaggio[0] = '\0';

    /* --- Alt+lettera: apre un menu; Alt+cifra: passa a un'area ---------- */
    if (alt) {
        char ch = (char)(base & 0xFF);

        if (base < 0x100 && ch >= '1' && ch <= '8') { vai_a_tab(self, ch - '1'); return; }
        if (base < 0x100 && gf_minuscolo(ch) == 'x') { gf_az_esci(self); return; }
        if (base < 0x100) {
            int m = gf_menu_da_lettera(ch);
            if (m >= 0) { gf_menu(self, m); return; }
        }
        return;
    }

    /* --- tasti funzione ------------------------------------------------- */
    if (base >= KBD_K_F1 && base <= KBD_K_F12) {
        /* if/else invece di uno switch: KBD_K_F(n) è una macro con un
         * calcolo dentro, e le etichette di un case devono essere
         * costanti intere. */
        if (base == KBD_K_F(1)) { gf_az_istruzioni(self); return; }
        if (base == KBD_K_F(2)) { gf_az_salva(self); return; }
        if (base == KBD_K_F(3)) {
            if (shift) gf_az_cerca_indietro(self);
            else       gf_az_cerca_avanti(self);
            return;
        }
        if (base == KBD_K_F(6)) {
            if (shift) gf_tab_precedente(self);
            else       gf_tab_successiva(self);
            return;
        }
        if (base == KBD_K_F(10)) { gf_menu(self, 0); return; }
        return;
    }

    /* --- combinazioni con Ctrl ------------------------------------------ */
    if (ctrl) {
        switch (base) {
            case 'n': gf_az_nuovo(self); return;
            case 'o': gf_az_apri(self); return;
            case 's': gf_az_salva(self); return;
            case 'w': gf_az_chiudi(self); return;
            case 'q': gf_az_esci(self); return;
            case 'z': gf_az_annulla(self); return;
            case 'c': gf_az_copia(self); return;
            case 'x': gf_az_taglia(self); return;
            case 'v': gf_az_incolla(self); return;
            case 'a': gf_az_seleziona_tutto(self); return;
            case 'f': gf_az_cerca(self); return;
            case 'h': gf_az_sostituisci(self); return;
            case 'g': gf_az_vai_a_riga(self); return;

            /* Ctrl+Home / Ctrl+End: gli estremi del documento */
            case KBD_K_HOME:
                if (!shift) t->sel_active = 0;
                else if (!t->sel_active) {
                    t->sel_active = 1;
                    t->sel_row = t->cursor_row;
                    t->sel_col = t->cursor_col;
                }
                t->cursor_row = 0;
                t->cursor_col = 0;
                gf_aggiusta_vista(self);
                return;

            case KBD_K_END:
                if (!shift) t->sel_active = 0;
                else if (!t->sel_active) {
                    t->sel_active = 1;
                    t->sel_row = t->cursor_row;
                    t->sel_col = t->cursor_col;
                }
                t->cursor_row = t->num_lines - 1;
                t->cursor_col = gf_riga_len(t, t->cursor_row);
                gf_aggiusta_vista(self);
                return;

            case KBD_K_PGUP: gf_tab_precedente(self); return;
            case KBD_K_PGDN: gf_tab_successiva(self); return;

            default: return;
        }
    }

    /* --- movimento ------------------------------------------------------ */
    switch (base) {
        case KBD_K_LEFT: case KBD_K_RIGHT: case KBD_K_UP: case KBD_K_DOWN:
        case KBD_K_HOME: case KBD_K_END:   case KBD_K_PGUP: case KBD_K_PGDN:
            muovi(self, base, shift);
            return;

        case KBD_K_INS:
            t->insert_mode = !t->insert_mode;
            return;

        case 27:
            /* ESC abbandona la selezione. Se non ce n'è una, apre il
             * menu: è il comportamento di MS-DOS EDIT, e dà una via
             * d'uscita a chi non ha un F10 funzionante. */
            if (t->sel_active) { t->sel_active = 0; return; }
            gf_menu(self, 0);
            return;

        default:
            break;
    }

    /* --- modifiche ------------------------------------------------------
     *
     * Un'area troncata al caricamento si modifica liberamente: è il
     * SALVATAGGIO sul file d'origine a essere bloccato (vedi gf_salva),
     * perché è lì che si cancellerebbe la parte mai letta. Chi vuole
     * lavorare su quel pezzo lo fa e poi usa Salva con nome. */
    switch (base) {
        case '\n':
            gf_undo_apri(t);
            if (t->sel_active) gf_sel_cancella(self);
            spezza_riga(self);
            dopo_modifica(self);
            gf_aggiusta_vista(self);
            return;

        case '\b':
            gf_undo_apri(t);
            if (t->sel_active) {
                gf_sel_cancella(self);
            } else if (t->cursor_col > 0) {
                char nuova[GF_LINE_STRIDE];
                const char *s = gf_riga(t, t->cursor_row);
                int len = (int)strlen(s), i, n = 0;

                for (i = 0; i < len; i++) {
                    if (i == t->cursor_col - 1) continue;
                    nuova[n++] = s[i];
                }
                nuova[n] = '\0';
                gf_riga_imposta(t, t->cursor_row, nuova);
                t->cursor_col--;
            } else if (t->cursor_row > 0) {
                unisci_righe(self, t->cursor_row - 1);
            }
            dopo_modifica(self);
            gf_aggiusta_vista(self);
            return;

        case KBD_K_DEL: {
            const char *s = gf_riga(t, t->cursor_row);
            int len = (int)strlen(s);

            gf_undo_apri(t);
            if (t->sel_active) {
                gf_sel_cancella(self);
            } else if (t->cursor_col < len) {
                char nuova[GF_LINE_STRIDE];
                int i, n = 0;
                for (i = 0; i < len; i++) {
                    if (i == t->cursor_col) continue;
                    nuova[n++] = s[i];
                }
                nuova[n] = '\0';
                gf_riga_imposta(t, t->cursor_row, nuova);
            } else if (t->cursor_row < t->num_lines - 1) {
                unisci_righe(self, t->cursor_row);
            }
            dopo_modifica(self);
            gf_aggiusta_vista(self);
            return;
        }

        case '\t':
        default:
            if (base < 0x100 && ((unsigned char)base >= 32 || base == '\t')) {
                gf_undo_apri(t);
                if (t->sel_active) gf_sel_cancella(self);
                inserisci_carattere(self, (char)base);
                dopo_modifica(self);
                gf_aggiusta_vista(self);
            }
            return;
    }
}

/* =============================================================================
 * Azioni: file
 * ============================================================================= */
static void imposta_percorso(GfTab *t, const char *path)
{
    gf_strlcpy(t->filepath, path, GF_MAX_PATH);
    gf_strlcpy(t->filename, gf_basename(path), GF_MAX_NAME);
    t->has_path = 1;
    t->lingua   = gf_rileva_lingua(path);
}

/* Ritorna 1 se si può procedere (salvato o scartato), 0 se annullato. */
static int chiedi_salvataggio(GfEdit *self, GfTab *t)
{
    static const char *opzioni[] = { "Si", "No", "Annulla" };
    char testo[GF_COLS];
    int  scelta;

    if (!t->modified) return 1;

    gf_fmt(testo, sizeof(testo), "'%s' e' stato modificato. Salvare?",
           t->filename[0] ? t->filename : "(nuovo)");

    scelta = gf_dlg_conferma(self, "Modifiche non salvate", testo, opzioni, 3);
    if (scelta == 0) { gf_az_salva(self); return !t->modified; }
    if (scelta == 1) return 1;
    return 0;
}

void gf_az_nuovo(GfEdit *self)
{
    int idx = gf_tab_libera_indice(self);
    GfTab *t;

    if (idx < 0) {
        gf_msg(self, "Tutte e otto le aree sono occupate: chiudine una");
        return;
    }

    t = &self->tabs[idx];
    if (gf_tab_prepara(t) != 0) {
        gf_msg(self, "Memoria esaurita: impossibile aprire una nuova area");
        return;
    }

    gf_tab_azzera(t, GF_LANG_NONE);
    t->in_use      = 1;
    t->has_path    = 0;
    t->filename[0] = '\0';
    t->filepath[0] = '\0';

    self->tab_corrente = idx;
    self->n_aperte++;
    gf_msg(self, "Nuova area");
}

void gf_az_apri(GfEdit *self)
{
    char path[GF_MAX_PATH];
    int  idx;
    GfTab *t;

    if (!gf_dlg_sfoglia(self, "Apri file", path, sizeof(path), 0)) return;

    /* Se il file è già aperto si va lì invece di aprirne una seconda
     * copia: due aree sullo stesso file si sovrascriverebbero a vicenda
     * senza che nulla lo segnali. */
    for (idx = 0; idx < GF_MAX_TABS; idx++) {
        if (self->tabs[idx].in_use && self->tabs[idx].has_path &&
            strcmp(self->tabs[idx].filepath, path) == 0) {
            self->tab_corrente = idx;
            gf_msg(self, "File gia' aperto");
            return;
        }
    }

    idx = gf_tab_libera_indice(self);
    if (idx < 0) {
        gf_msg(self, "Tutte e otto le aree sono occupate: chiudine una");
        return;
    }

    t = &self->tabs[idx];
    if (gf_carica(t, path) == -2) {
        gf_msg(self, "Memoria esaurita: impossibile aprire il file");
        return;
    }

    imposta_percorso(t, path);
    t->in_use = 1;
    self->tab_corrente = idx;
    self->n_aperte++;

    {
        char m[GF_COLS];
        gf_fmt(m, sizeof(m), "'%s' aperto, %d righe", t->filename, t->num_lines);
        gf_msg(self, m);
    }
}

void gf_az_salva(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    int    r;

    if (!t->has_path) { gf_az_salva_come(self); return; }

    r = gf_salva(t);
    if (r == 0) {
        char m[GF_COLS];
        gf_fmt(m, sizeof(m), "'%s' salvato, %d righe", t->filename, t->num_lines);
        gf_msg(self, m);
        return;
    }

    if (r == -27) {
        gf_msg(self, "File troncato al caricamento: salvarlo cancellerebbe "
                     "cio' che non e' stato letto");
    } else {
        char m[GF_COLS];
        gf_fmt(m, sizeof(m), "Salvataggio fallito (errore %d)", -r);
        gf_msg(self, m);
    }
}

void gf_az_salva_come(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    char   path[GF_MAX_PATH];

    if (!gf_dlg_sfoglia(self, "Salva con nome", path, sizeof(path), 1)) return;

    imposta_percorso(t, path);

    /* Un'area troncata non è salvabile sul file di ORIGINE, ma su un
     * file nuovo sì: lì non si sta cancellando niente che esistesse. */
    t->troncato = 0;

    gf_az_salva(self);
}

void gf_az_chiudi(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    int    i;

    if (!chiedi_salvataggio(self, t)) return;

    /* d NON viene liberata: la free() di EX-OS è un no-op, e riusare il
     * blocco alla prossima apertura è l'unico modo di non consumare
     * altra memoria a ogni apri-chiudi. */
    t->in_use = 0;
    self->n_aperte--;

    if (self->n_aperte <= 0) {
        /* L'ultima area chiusa non lascia l'editor senza documento:
         * ne apre una nuova, come fa MS-DOS EDIT. */
        self->n_aperte = 0;
        gf_az_nuovo(self);
        return;
    }

    for (i = 0; i < GF_MAX_TABS; i++) {
        int k = (self->tab_corrente + i) % GF_MAX_TABS;
        if (self->tabs[k].in_use) { self->tab_corrente = k; return; }
    }
}

void gf_az_esci(GfEdit *self)
{
    int i;

    for (i = 0; i < GF_MAX_TABS; i++) {
        if (!self->tabs[i].in_use) continue;
        self->tab_corrente = i;
        if (!chiedi_salvataggio(self, &self->tabs[i])) return;
    }

    self->running = 0;
}

/* =============================================================================
 * Azioni: modifica
 * ============================================================================= */
void gf_az_copia(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    char   buf[GF_CLIP_MAX];
    int    n;

    if (!t->sel_active) {
        /* Senza selezione si copia la riga corrente: è la scorciatoia
         * che tutti si aspettano, e che rende Ctrl+C utile anche senza
         * aver selezionato niente. */
        gf_strlcpy(buf, gf_riga(t, t->cursor_row), GF_CLIP_MAX);
        gf_clip_imposta(self, buf, (int)strlen(buf), 1);
        gf_msg(self, "Riga copiata negli appunti");
        return;
    }

    n = gf_sel_estrai(t, buf, GF_CLIP_MAX);
    gf_clip_imposta(self, buf, n, 0);
    {
        char m[GF_COLS];
        gf_fmt(m, sizeof(m), "%d caratteri copiati", n);
        gf_msg(self, m);
    }
}

void gf_az_taglia(GfEdit *self)
{
    GfTab *t = gf_corrente(self);

    gf_az_copia(self);

    gf_undo_apri(t);
    if (t->sel_active) gf_sel_cancella(self);
    else               gf_riga_cancella(t, t->cursor_row);

    dopo_modifica(self);
    gf_aggiusta_vista(self);
    gf_msg(self, "Tagliato");
}

void gf_az_incolla(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    const char *p = self->clip.data;
    int    i;

    if (self->clip.len == 0) { gf_msg(self, "Appunti vuoti"); return; }

    gf_undo_apri(t);
    if (t->sel_active) gf_sel_cancella(self);

    if (self->clip.a_righe) {
        /* Copia di riga intera: si inserisce come riga sopra a quella
         * corrente, non spezzando il testo dove sta il cursore. */
        gf_riga_inserisci(t, t->cursor_row, p);
        t->cursor_col = 0;
        dopo_modifica(self);
        gf_aggiusta_vista(self);
        gf_msg(self, "Riga incollata");
        return;
    }

    for (i = 0; i < self->clip.len; i++) {
        if (p[i] == '\n') spezza_riga(self);
        else              inserisci_carattere(self, p[i]);
    }

    dopo_modifica(self);
    gf_aggiusta_vista(self);
    {
        char m[GF_COLS];
        gf_fmt(m, sizeof(m), "%d caratteri incollati", self->clip.len);
        gf_msg(self, m);
    }
}

void gf_az_annulla(GfEdit *self)
{
    GfTab *t = gf_corrente(self);

    switch (gf_undo_applica(t)) {
        case 0:
            gf_ricalcola_commenti(t);
            gf_aggiusta_vista(self);
            gf_msg(self, "Annullato");
            break;
        case -1:
            gf_msg(self, "Niente da annullare");
            break;
        default:
            gf_msg(self, "Cronologia esaurita: quell'operazione non e' piu' "
                         "annullabile per intero");
            break;
    }
}

void gf_az_seleziona_tutto(GfEdit *self)
{
    GfTab *t = gf_corrente(self);

    t->sel_active = 1;
    t->sel_row    = 0;
    t->sel_col    = 0;
    t->cursor_row = t->num_lines - 1;
    t->cursor_col = gf_riga_len(t, t->cursor_row);
    gf_aggiusta_vista(self);
}

/* =============================================================================
 * Ricerca
 * ============================================================================= */
static int confronta(GfEdit *self, const char *testo, const char *ago)
{
    int i;

    for (i = 0; ago[i]; i++) {
        char a = testo[i];
        char b = ago[i];
        if (!a) return 0;
        if (self->find.ignora_caso) { a = gf_minuscolo(a); b = gf_minuscolo(b); }
        if (a != b) return 0;
    }
    return 1;
}

static void trova_tutte(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    int    r;

    self->find.n_match = 0;
    if (!self->find.needle[0]) return;

    for (r = 0; r < t->num_lines; r++) {
        const char *s = gf_riga(t, r);
        int i;

        for (i = 0; s[i]; i++) {
            if (!confronta(self, s + i, self->find.needle)) continue;
            if (self->find.n_match >= GF_MAX_MATCHES) return;
            self->find.match_row[self->find.n_match] = r;
            self->find.match_col[self->find.n_match] = i;
            self->find.n_match++;
        }
    }
}

/* Porta il cursore all'occorrenza idx. */
static void vai_a_match(GfEdit *self, int idx)
{
    GfTab *t = gf_corrente(self);

    if (idx < 0 || idx >= self->find.n_match) return;

    self->find.match_corrente = idx;
    t->cursor_row = self->find.match_row[idx];
    t->cursor_col = self->find.match_col[idx];

    /* L'occorrenza trovata resta selezionata: è ciò che permette di
     * sostituirla premendo subito un tasto, e rende visibile QUALE
     * delle occorrenze si sia raggiunta. */
    t->sel_active = 1;
    t->sel_row    = t->cursor_row;
    t->sel_col    = t->cursor_col;
    t->cursor_col += (int)strlen(self->find.needle);

    gf_aggiusta_vista(self);

    {
        char m[GF_COLS];
        gf_fmt(m, sizeof(m), "Occorrenza %d di %d", idx + 1, self->find.n_match);
        gf_msg(self, m);
    }
}

void gf_az_cerca(GfEdit *self)
{
    if (!gf_dlg_stringa(self, "Trova", "Testo da cercare:",
                        self->find.needle, GF_FIND_MAX)) return;

    trova_tutte(self);
    if (self->find.n_match == 0) {
        gf_msg(self, "Nessuna occorrenza trovata");
        return;
    }

    self->find.match_corrente = -1;
    gf_az_cerca_avanti(self);
}

void gf_az_cerca_avanti(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    int    i;

    if (!self->find.needle[0]) { gf_az_cerca(self); return; }

    trova_tutte(self);
    if (self->find.n_match == 0) { gf_msg(self, "Nessuna occorrenza trovata"); return; }

    /* Prima occorrenza STRETTAMENTE dopo il cursore: senza il confronto
     * stretto, F3 ripetuto resterebbe fermo sulla stessa. */
    for (i = 0; i < self->find.n_match; i++) {
        if (self->find.match_row[i] > t->cursor_row ||
            (self->find.match_row[i] == t->cursor_row &&
             self->find.match_col[i] >= t->cursor_col)) {
            vai_a_match(self, i);
            return;
        }
    }

    vai_a_match(self, 0);
    gf_msg(self, "Ricerca ripresa dall'inizio del documento");
}

void gf_az_cerca_indietro(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    int    i;

    if (!self->find.needle[0]) { gf_az_cerca(self); return; }

    trova_tutte(self);
    if (self->find.n_match == 0) { gf_msg(self, "Nessuna occorrenza trovata"); return; }

    for (i = self->find.n_match - 1; i >= 0; i--) {
        int r = self->find.match_row[i];
        int c = self->find.match_col[i];
        int inizio_sel = t->sel_active ? t->sel_col : t->cursor_col;
        int riga_sel   = t->sel_active ? t->sel_row : t->cursor_row;

        if (r < riga_sel || (r == riga_sel && c < inizio_sel)) {
            vai_a_match(self, i);
            return;
        }
    }

    vai_a_match(self, self->find.n_match - 1);
    gf_msg(self, "Ricerca ripresa dalla fine del documento");
}

void gf_az_sostituisci(GfEdit *self)
{
    static const char *opzioni[] = { "Tutte", "Una", "Annulla" };
    GfTab *t = gf_corrente(self);
    char   testo[GF_COLS];
    int    scelta;

    if (!gf_dlg_stringa(self, "Sostituisci", "Testo da cercare:",
                        self->find.needle, GF_FIND_MAX)) return;
    if (!self->find.needle[0]) return;

    if (!gf_dlg_stringa(self, "Sostituisci", "Sostituire con:",
                        self->find.replacement, GF_FIND_MAX)) return;

    trova_tutte(self);
    if (self->find.n_match == 0) { gf_msg(self, "Nessuna occorrenza trovata"); return; }

    gf_fmt(testo, sizeof(testo), "Trovate %d occorrenze di '%s'.",
           self->find.n_match, self->find.needle);
    scelta = gf_dlg_conferma(self, "Sostituisci", testo, opzioni, 3);
    if (scelta < 0 || scelta == 2) return;

    if (scelta == 1) {
        /* Una sola: quella dopo il cursore. */
        gf_az_cerca_avanti(self);
        if (self->find.n_match == 0) return;

        gf_undo_apri(t);
        {
            int r = self->find.match_row[self->find.match_corrente];
            int c = self->find.match_col[self->find.match_corrente];
            const char *s = gf_riga(t, r);
            char nuova[GF_LINE_STRIDE];
            int n = 0, i;
            int lung_ago = (int)strlen(self->find.needle);

            for (i = 0; i < c && n < GF_MAX_COL; i++) nuova[n++] = s[i];
            for (i = 0; self->find.replacement[i] && n < GF_MAX_COL; i++)
                nuova[n++] = self->find.replacement[i];
            for (i = c + lung_ago; s[i] && n < GF_MAX_COL; i++) nuova[n++] = s[i];
            nuova[n] = '\0';

            gf_riga_imposta(t, r, nuova);
            t->cursor_row = r;
            t->cursor_col = c + (int)strlen(self->find.replacement);
            t->sel_active = 0;
        }
        dopo_modifica(self);
        gf_aggiusta_vista(self);
        gf_msg(self, "Sostituita 1 occorrenza");
        return;
    }

    /* Tutte. Si procede riga per riga e da SINISTRA a destra
     * ricominciando dopo il testo appena inserito: sostituire "a" con
     * "aa" altrimenti troverebbe di nuovo la propria sostituzione e non
     * finirebbe mai. */
    gf_undo_apri(t);
    {
        int lung_ago = (int)strlen(self->find.needle);
        int lung_new = (int)strlen(self->find.replacement);
        int fatte = 0;
        int r;

        for (r = 0; r < t->num_lines; r++) {
            char nuova[GF_LINE_STRIDE];
            const char *s = gf_riga(t, r);
            int i = 0, n = 0, cambiata = 0;

            while (s[i] && n < GF_MAX_COL) {
                if (confronta(self, s + i, self->find.needle)) {
                    int k;
                    for (k = 0; k < lung_new && n < GF_MAX_COL; k++)
                        nuova[n++] = self->find.replacement[k];
                    i += lung_ago;
                    cambiata = 1;
                    fatte++;
                } else {
                    nuova[n++] = s[i++];
                }
            }
            nuova[n] = '\0';

            if (cambiata) gf_riga_imposta(t, r, nuova);
        }

        t->sel_active = 0;
        dopo_modifica(self);
        gf_aggiusta_vista(self);

        gf_fmt(testo, sizeof(testo), "Sostituite %d occorrenze", fatte);
        gf_msg(self, testo);
    }
}

void gf_az_vai_a_riga(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    char   buf[16] = "";
    int    n;

    if (!gf_dlg_stringa(self, "Vai a riga", "Numero di riga:", buf, sizeof(buf))) return;

    n = atoi(buf);
    if (n < 1 || n > t->num_lines) {
        char m[GF_COLS];
        gf_fmt(m, sizeof(m), "Riga %d inesistente: il documento ne ha %d",
               n, t->num_lines);
        gf_msg(self, m);
        return;
    }

    t->cursor_row = n - 1;
    t->cursor_col = 0;
    t->sel_active = 0;
    gf_aggiusta_vista(self);
}

/* =============================================================================
 * Azioni: opzioni
 * ============================================================================= */
void gf_az_tab_width(GfEdit *self)
{
    char buf[8];
    int  n;

    gf_fmt(buf, sizeof(buf), "%d", self->opz.tab_width);
    if (!gf_dlg_stringa(self, "Tabulazione", "Larghezza (1-16):", buf, sizeof(buf)))
        return;

    n = atoi(buf);
    if (n < 1 || n > 16) { gf_msg(self, "Valore fuori intervallo (1-16)"); return; }

    self->opz.tab_width = n;
    gf_msg(self, "Larghezza della tabulazione aggiornata");
}

void gf_az_autosave(GfEdit *self)
{
    char buf[8];
    int  n;

    gf_fmt(buf, sizeof(buf), "%d", self->opz.autosave_sec);
    if (!gf_dlg_stringa(self, "Autosalvataggio",
                        "Intervallo in secondi (0 = disattivo):", buf, sizeof(buf)))
        return;

    n = atoi(buf);
    if (n < 0 || n > 3600) { gf_msg(self, "Valore fuori intervallo (0-3600)"); return; }

    self->opz.autosave_sec    = n;
    self->ultimo_autosave_ms  = uptime_ms();

    if (n == 0) gf_msg(self, "Autosalvataggio disattivato");
    else {
        char m[GF_COLS];
        gf_fmt(m, sizeof(m), "Autosalvataggio ogni %d secondi", n);
        gf_msg(self, m);
    }
}

void gf_az_developing(GfEdit *self)
{
    self->opz.developing = !self->opz.developing;
    gf_msg(self, self->opz.developing
                   ? "Evidenziazione sintattica attiva"
                   : "Evidenziazione sintattica disattivata");
}

void gf_az_lingua(GfEdit *self)
{
    static const char *opzioni[] = { "Nessuno", "C", "C++", "BASIC", "Asm" };
    GfTab *t = gf_corrente(self);
    int    scelta = gf_dlg_conferma(self, "Linguaggio",
                                    "Linguaggio per l'evidenziazione:", opzioni, 5);

    if (scelta < 0) return;

    t->lingua = (GfLingua)scelta;
    gf_ricalcola_commenti(t);
    gf_msg(self, "Linguaggio aggiornato");
}

/* =============================================================================
 * Azioni: aiuto
 * ============================================================================= */
void gf_az_istruzioni(GfEdit *self)
{
    static const char *righe[] = {
        "",
        "  MOVIMENTO",
        "    Frecce               sposta il cursore",
        "    Home / Fine          inizio / fine della riga",
        "    Ctrl+Home / Ctrl+Fine inizio / fine del documento",
        "    PagSu / PagGiu       una schermata per volta",
        "    Ctrl+G               vai a una riga per numero",
        "",
        "  SELEZIONE",
        "    Shift + movimento    estende la selezione",
        "    Ctrl+A               seleziona tutto",
        "    ESC                  abbandona la selezione",
        "",
        "  MODIFICA",
        "    Ins                  alterna inserimento e sovrascrittura",
        "    Ctrl+Z               annulla l'ultima operazione",
        "    Ctrl+X / Ctrl+C      taglia / copia (senza selezione: la riga)",
        "    Ctrl+V               incolla",
        "",
        "  FILE",
        "    Ctrl+N / Ctrl+O      nuovo / apri",
        "    F2 o Ctrl+S          salva",
        "    Ctrl+W               chiudi l'area",
        "    Alt+X o Ctrl+Q       esci",
        "",
        "  RICERCA",
        "    Ctrl+F               trova",
        "    F3 / Shift+F3        occorrenza successiva / precedente",
        "    Ctrl+H               sostituisci",
        "",
        "  AREE (fino a otto file aperti insieme)",
        "    F6 / Shift+F6        area successiva / precedente",
        "    Alt+1 ... Alt+8      vai direttamente a un'area",
        "",
        "  MENU",
        "    F10 o ESC            apre la barra dei menu",
        "    Alt+F M C O A        apre direttamente un menu",
        "",
        "  LIMITI",
        "    512 righe per file, 200 caratteri per riga, 8 aree.",
        "    Un file piu' grande viene caricato in parte e il",
        "    salvataggio resta disabilitato, per non cancellare",
        "    cio' che non e' stato letto.",
        ""
    };

    gf_dlg_testo_scorrevole(self, "Istruzioni", righe,
                            (int)(sizeof(righe) / sizeof(righe[0])));
}

void gf_az_info(GfEdit *self)
{
    static char r0[GF_COLS], r1[GF_COLS], r2[GF_COLS], r3[GF_COLS];
    static char r5[GF_COLS], r6[GF_COLS];
    const char *righe[9];
    char        os[128];

    gf_fmt(r0, sizeof(r0), "%s versione %s", GF_NAME, GF_VERSION);
    gf_fmt(r1, sizeof(r1), "Autore : %s", GF_AUTHOR);
    gf_fmt(r2, sizeof(r2), "Email  : %s", GF_EMAIL);
    gf_fmt(r3, sizeof(r3), "Licenza: %s (GNU General Public License)", GF_LICENSE);

    if (osversion(os, sizeof(os)) < 0) gf_strlcpy(os, "EX-OS", 6);
    gf_fmt(r5, sizeof(r5), "Sistema: %s", os);
    gf_fmt(r6, sizeof(r6), "Caratteri digitati in questa sessione: %d",
           (int)self->caratteri_digitati);

    righe[0] = r0;
    righe[1] = "";
    righe[2] = r1;
    righe[3] = r2;
    righe[4] = r3;
    righe[5] = "";
    righe[6] = r5;
    righe[7] = r6;
    righe[8] = "";

    gf_dlg_messaggio(self, "Informazioni", righe, 9);
}

void gf_az_licenza(GfEdit *self)
{
    static const char *righe[] = {
        "",
        "  GF Edit - editor di testo per EX-OS",
        "  Copyright (C) 2025 Graziano Falcone",
        "",
        "  Questo programma e' software libero: puoi ridistribuirlo",
        "  e/o modificarlo secondo i termini della GNU General Public",
        "  License, versione 2, come pubblicata dalla Free Software",
        "  Foundation.",
        "",
        "  Questo programma e' distribuito nella speranza che sia",
        "  utile, ma SENZA ALCUNA GARANZIA; senza neppure la garanzia",
        "  implicita di COMMERCIABILITA' o IDONEITA' PER UN PARTICOLARE",
        "  SCOPO. Vedi la GNU General Public License per maggiori",
        "  dettagli.",
        "",
        "  Dovresti aver ricevuto una copia della GNU General Public",
        "  License insieme a questo programma; il testo completo si",
        "  trova nel file LICENSE nella radice del progetto EX-OS e",
        "  in gftexteditor/COPYING.",
        ""
    };

    gf_dlg_testo_scorrevole(self, "Licenza", righe,
                            (int)(sizeof(righe) / sizeof(righe[0])));
}
