/* =============================================================================
 * bin/gfedit/gf_ui.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * GF Edit — disegno dello schermo, dialoghi e menu a tendina.
 *
 * Tutto passa dal frame ombra di gf_term.c: qui dentro non c'è una sola
 * write(). I dialoghi hanno un ciclo di tasti proprio e ridisegnano
 * l'editor sotto di sé prima di aprirsi, così quando si chiudono non
 * devono ricordare cosa coprivano — il frame successivo lo riscopre da
 * solo confrontandosi con quello a video.
 * ============================================================================= */

#include "gfedit.h"

/* =============================================================================
 * Barra dei menu
 * ============================================================================= */
typedef struct {
    const char *etichetta;
    const char *scorciatoia;
    void      (*azione)(GfEdit *);
} GfVoce;

typedef struct {
    const char   *titolo;
    char          acceleratore;      /* lettera di Alt+<lettera> */
    const GfVoce *voci;
    int           n_voci;
} GfMenu;

/* Una voce con etichetta "-" è un separatore: non si seleziona. */
static const GfVoce voci_file[] = {
    { "Nuovo",             "",         gf_az_nuovo      },
    { "Apri...",           "",         gf_az_apri       },
    { "Salva",             "F2",       gf_az_salva      },
    { "Salva con nome...", "",         gf_az_salva_come },
    { "Chiudi",            "",         gf_az_chiudi     },
    { "-",                 "",         0                },
    { "Esci",              "Alt+X",    gf_az_esci       },
};

static const GfVoce voci_modifica[] = {
    { "Annulla",           "Ctrl+Z",   gf_az_annulla          },
    { "-",                 "",         0                      },
    { "Taglia",            "Ctrl+X",   gf_az_taglia           },
    { "Copia",             "Ctrl+C",   gf_az_copia            },
    { "Incolla",           "Ctrl+V",   gf_az_incolla          },
    { "-",                 "",         0                      },
    { "Seleziona tutto",   "Ctrl+A",   gf_az_seleziona_tutto  },
};

static const GfVoce voci_cerca[] = {
    { "Trova...",          "Ctrl+F",   gf_az_cerca            },
    { "Trova successivo",  "F3",       gf_az_cerca_avanti     },
    { "Trova precedente",  "Shift+F3", gf_az_cerca_indietro   },
    { "Sostituisci...",    "Ctrl+H",   gf_az_sostituisci      },
    { "-",                 "",         0                      },
    { "Vai a riga...",     "Ctrl+G",   gf_az_vai_a_riga       },
};

static const GfVoce voci_opzioni[] = {
    { "Larghezza tabulazione...", "",  gf_az_tab_width  },
    { "Autosalvataggio...",       "",  gf_az_autosave   },
    { "Evidenziazione sintattica","",  gf_az_developing },
    { "Linguaggio...",            "",  gf_az_lingua     },
};

static const GfVoce voci_aiuto[] = {
    { "Istruzioni",    "F1",  gf_az_istruzioni },
    { "Informazioni",  "",    gf_az_info       },
    { "Licenza",       "",    gf_az_licenza    },
};

#define NVOCI(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const GfMenu menus[] = {
    { "File",     'F', voci_file,     NVOCI(voci_file)     },
    { "Modifica", 'M', voci_modifica, NVOCI(voci_modifica) },
    { "Cerca",    'C', voci_cerca,    NVOCI(voci_cerca)    },
    { "Opzioni",  'O', voci_opzioni,  NVOCI(voci_opzioni)  },
    { "Aiuto",    'A', voci_aiuto,    NVOCI(voci_aiuto)    },
};
#define N_MENU NVOCI(menus)

/* Colonna di partenza di ogni titolo nella barra. Calcolata una volta:
 * serve sia a disegnare la barra sia ad allineare la tendina sotto al
 * titolo giusto. */
static int menu_colonna(int idx)
{
    int c = 1, i;

    for (i = 0; i < idx && i < N_MENU; i++) {
        c += (int)strlen(menus[i].titolo) + 3;
    }
    return c;
}

static void disegna_barra_menu(GfEdit *self, int aperto)
{
    int i;

    (void)self;
    gf_term_riempi(GF_ROW_MENU, 0, GF_COLS, ' ', CP_MENU);

    for (i = 0; i < N_MENU; i++) {
        int c = menu_colonna(i);
        unsigned char at = (i == aperto) ? CP_MENU_SEL : CP_MENU;

        gf_term_cella(GF_ROW_MENU, c - 1, ' ', at);
        gf_term_scrivi(GF_ROW_MENU, c, menus[i].titolo, at);
        gf_term_cella(GF_ROW_MENU, c + (int)strlen(menus[i].titolo), ' ', at);

        /* La prima lettera è l'acceleratore: evidenziata come nei menu
         * DOS, dove il colore diverso È la documentazione della
         * combinazione Alt+lettera. */
        gf_term_cella(GF_ROW_MENU, c, menus[i].titolo[0],
                      (i == aperto) ? CP_MENU_SEL : CP_MENU_TASTO);
    }
}

/* =============================================================================
 * Barra delle aree aperte
 * ============================================================================= */
static void disegna_barra_tab(GfEdit *self)
{
    int i, c = 0;

    gf_term_riempi(GF_ROW_TABS, 0, GF_COLS, ' ', CP_TAB_INATTIVA);

    for (i = 0; i < GF_MAX_TABS; i++) {
        GfTab *t = &self->tabs[i];
        char   etichetta[GF_MAX_NAME + 8];
        int    len;

        if (!t->in_use) continue;

        gf_fmt(etichetta, sizeof(etichetta), " %d:%s%s ",
               i + 1,
               t->filename[0] ? t->filename : "(nuovo)",
               t->modified ? "*" : "");

        len = (int)strlen(etichetta);
        if (c + len >= GF_COLS) break;

        gf_term_scrivi(GF_ROW_TABS, c, etichetta,
                       (i == self->tab_corrente) ? CP_TAB_ATTIVA : CP_TAB_INATTIVA);
        c += len;
    }
}

/* =============================================================================
 * Area di testo
 * ============================================================================= */
static const unsigned char colore_token[] = {
    CP_TESTO, CP_KEYWORD, CP_STRINGA, CP_COMMENTO,
    CP_NUMERO, CP_FUNZIONE, CP_PREPROC
};

static void disegna_testo(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    unsigned char hl[GF_MAX_COL + 1];
    int r1 = 0, c1 = 0, r2 = 0, c2 = 0;
    int r;

    if (t->sel_active) gf_sel_normalizza(t, &r1, &c1, &r2, &c2);

    for (r = 0; r < GF_TEXT_ROWS; r++) {
        int   idx = t->view_top + r;
        int   riga_schermo = GF_TEXT_TOP + r;
        const char *s;
        int   i, dc = 0;

        gf_term_riempi(riga_schermo, 0, GF_COLS, ' ', CP_TESTO);

        if (idx >= t->num_lines) {
            /* Oltre la fine del documento: una tilde come in vi, che
             * distingue "riga vuota" da "riga che non esiste". */
            gf_term_cella(riga_schermo, 0, '~', CP_TAB_INATTIVA);
            continue;
        }

        s = gf_riga(t, idx);

        if (self->opz.developing && t->lingua != GF_LANG_NONE) {
            gf_evidenzia_riga(t, idx, hl, GF_MAX_COL);
        } else {
            memset(hl, GF_TOK_NORMALE, sizeof(hl));
        }

        for (i = 0; s[i]; i++) {
            int larghezza = (s[i] == '\t')
                          ? self->opz.tab_width - (dc % self->opz.tab_width)
                          : 1;
            unsigned char at = colore_token[hl[i] < 7 ? hl[i] : 0];
            int k;

            /* Selezione: sovrascrive il colore sintattico. Il confronto
             * è sui byte del documento, non sulle colonne di schermo —
             * la selezione appartiene al testo, non alla sua resa. */
            if (t->sel_active) {
                int dentro = (idx > r1 || (idx == r1 && i >= c1)) &&
                             (idx < r2 || (idx == r2 && i <  c2));
                if (dentro) at = CP_SELEZIONE;
            }

            for (k = 0; k < larghezza; k++) {
                int col = dc - t->view_left;
                if (col >= 0 && col < GF_COLS) {
                    gf_term_cella(riga_schermo, col,
                                  (s[i] == '\t') ? ' ' : s[i], at);
                }
                dc++;
            }
        }
    }
}

/* =============================================================================
 * Barra di stato e riga dei messaggi
 * ============================================================================= */
static void disegna_stato(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    char   buf[GF_COLS + 1];
    int    secondi = (int)(uptime_ms() / 1000u);

    gf_term_riempi(GF_ROW_STATUS, 0, GF_COLS, ' ', CP_STATO);

    gf_fmt(buf, sizeof(buf), " %s%s",
           t->has_path ? t->filepath : "(senza nome)",
           t->modified ? " *" : "");
    gf_term_scrivi(GF_ROW_STATUS, 0, buf, CP_STATO);

    gf_fmt(buf, sizeof(buf), "Ri %d/%d  Col %d  %s  %s  %d:%02d ",
           t->cursor_row + 1, t->num_lines, t->cursor_col + 1,
           t->insert_mode ? "INS" : "SOV",
           (self->opz.developing && t->lingua != GF_LANG_NONE) ? "SYN" : "   ",
           secondi / 60, secondi % 60);
    {
        /* Allineata a destra. La colonna di partenza va agganciata a
         * zero: con una stringa più lunga dello schermo sarebbe
         * negativa, e gf_term_cella scarterebbe le prime celle per poi
         * accettare le successive — cioè scriverebbe la coda della
         * stringa a sinistra, sfalsata. */
        int c0 = GF_COLS - (int)strlen(buf);
        if (c0 < 0) c0 = 0;
        gf_term_scrivi(GF_ROW_STATUS, c0, buf, CP_STATO);
    }

    /* Riga dei messaggi: l'avviso di troncamento ha la precedenza su
     * qualunque messaggio, perché segnala che il file su disco contiene
     * più di quanto si veda ed è la cosa più importante da sapere prima
     * di salvare. */
    gf_term_riempi(GF_ROW_MSG, 0, GF_COLS, ' ', CP_MSG);

    if (t->troncato) {
        gf_term_scrivi(GF_ROW_MSG, 0,
            " ATTENZIONE: file troncato al caricamento: il salvataggio e'"
            " disabilitato su questo file", CP_MSG);
    } else if (self->messaggio[0]) {
        gf_term_scrivi(GF_ROW_MSG, 1, self->messaggio, CP_MSG);
    } else {
        gf_term_scrivi(GF_ROW_MSG, 1,
            "F1 Aiuto  F2 Salva  F3 Trova  F10 Menu  Alt+X Esci", CP_MSG);
    }
}

/* =============================================================================
 * gf_componi / gf_disegna — un fotogramma dell'editor
 *
 * Sono due funzioni e non una perché i dialoghi si disegnano SOPRA
 * l'editor: se ognuno di loro chiamasse una gf_disegna che riversa
 * subito a video, ogni tasto premuto dentro un dialogo mostrerebbe per
 * un istante l'editor senza il riquadro, e poi il riquadro. Con due
 * flush per fotogramma lo sfarfallio si vede, perché fra i due c'è una
 * syscall e un ridisegno reale della memoria video.
 *
 * gf_componi riempie il frame ombra e basta; il flush lo fa chi ha
 * finito di comporre — l'editor quando disegna sé stesso, il dialogo
 * dopo aver messo giù il proprio riquadro.
 * ============================================================================= */
void gf_componi(GfEdit *self)
{
    GfTab *t = gf_corrente(self);
    int    cs;

    disegna_barra_menu(self, -1);
    disegna_barra_tab(self);
    disegna_testo(self);
    disegna_stato(self);

    cs = gf_col_schermo(self, gf_riga(t, t->cursor_row), t->cursor_col) - t->view_left;
    if (cs < 0) cs = 0;
    if (cs >= GF_COLS) cs = GF_COLS - 1;

    gf_term_cursore(GF_TEXT_TOP + (t->cursor_row - t->view_top), cs, 1);
}

void gf_disegna(GfEdit *self)
{
    gf_componi(self);
    gf_term_flush();
}

void gf_msg(GfEdit *self, const char *s)
{
    gf_strlcpy(self->messaggio, s, GF_COLS);
}

/* =============================================================================
 * Riquadro centrato — base comune di tutti i dialoghi
 * ============================================================================= */
static void riquadro_titolato(int r, int c, int h, int w, const char *titolo)
{
    int len = (int)strlen(titolo);
    int tc  = c + (w - len - 2) / 2;

    gf_term_riquadro(r, c, h, w, CP_DIALOGO);

    if (len + 4 < w) {
        gf_term_cella(r, tc - 1, ' ', CP_DLG_TITOLO);
        gf_term_scrivi(r, tc, titolo, CP_DLG_TITOLO);
        gf_term_cella(r, tc + len, ' ', CP_DLG_TITOLO);
    }
}

/* Larghezza del riquadro necessaria per contenere n righe di testo. */
static int larghezza_per(const char **righe, int n, int minimo)
{
    int w = minimo, i;

    for (i = 0; i < n; i++) {
        int len = (int)strlen(righe[i]) + 4;
        if (len > w) w = len;
    }
    if (w > GF_COLS - 2) w = GF_COLS - 2;
    return w;
}

/* =============================================================================
 * gf_dlg_messaggio — testo e un tasto per chiudere
 * ============================================================================= */
void gf_dlg_messaggio(GfEdit *self, const char *titolo, const char **righe, int n)
{
    int w = larghezza_per(righe, n, (int)strlen(titolo) + 8);
    int h = n + 4;
    int r, c, i;

    if (h > GF_ROWS - 2) h = GF_ROWS - 2;
    r = (GF_ROWS - h) / 2;
    c = (GF_COLS - w) / 2;

    gf_componi(self);
    riquadro_titolato(r, c, h, w, titolo);

    for (i = 0; i < n && i < h - 4; i++) {
        gf_term_scrivi_n(r + 1 + i, c + 2, righe[i], w - 4, CP_DIALOGO);
    }
    gf_term_scrivi(r + h - 2, c + 2, "[ Premi un tasto ]", CP_DIALOGO);

    gf_term_cursore(r + h - 2, c + 2, 0);
    gf_term_flush();
    gf_getkey();
}

/* =============================================================================
 * gf_dlg_conferma — scelta fra più opzioni
 *
 * Ritorna l'indice scelto, o -1 se l'utente annulla con ESC. Le opzioni
 * si scorrono con le frecce ORIZZONTALI: sono bottoni in fila, e le
 * frecce verticali non avrebbero un verso naturale.
 * ============================================================================= */
int gf_dlg_conferma(GfEdit *self, const char *titolo, const char *testo,
                    const char **opzioni, int n_opzioni)
{
    const char *righe[1];
    int  w, h = 7, r, c;
    int  scelta = 0;

    righe[0] = testo;
    w = larghezza_per(righe, 1, (int)strlen(titolo) + 8);
    {
        int larghezza_bottoni = 2, i;
        for (i = 0; i < n_opzioni; i++) larghezza_bottoni += (int)strlen(opzioni[i]) + 6;
        if (larghezza_bottoni > w) w = larghezza_bottoni;
        if (w > GF_COLS - 2) w = GF_COLS - 2;
    }

    r = (GF_ROWS - h) / 2;
    c = (GF_COLS - w) / 2;

    for (;;) {
        unsigned key;
        int i, bc;

        gf_componi(self);
        riquadro_titolato(r, c, h, w, titolo);
        gf_term_scrivi_n(r + 2, c + 2, testo, w - 4, CP_DIALOGO);

        bc = c + 2;
        for (i = 0; i < n_opzioni; i++) {
            unsigned char at = (i == scelta) ? CP_MENU_SEL : CP_DIALOGO;
            gf_term_cella(r + 4, bc, i == scelta ? '>' : ' ', at);
            gf_term_cella(r + 4, bc + 1, ' ', at);
            gf_term_scrivi(r + 4, bc + 2, opzioni[i], at);
            gf_term_cella(r + 4, bc + 2 + (int)strlen(opzioni[i]), ' ', at);
            bc += (int)strlen(opzioni[i]) + 6;
        }

        gf_term_cursore(r + 4, c + 2, 0);
        gf_term_flush();

        key = gf_getkey();
        switch (key & KBD_KEY_MASK) {
            case KBD_K_LEFT:  if (scelta > 0) scelta--; break;
            case KBD_K_RIGHT: if (scelta < n_opzioni - 1) scelta++; break;
            case KBD_K_HOME:  scelta = 0; break;
            case KBD_K_END:   scelta = n_opzioni - 1; break;
            case '\n':        return scelta;
            case 27:          return -1;
            default: {
                /* Iniziale dell'opzione: 'S' su "Si", 'N' su "No". È il
                 * modo in cui si risponde a un dialogo DOS senza
                 * spostare le mani dalle lettere. Ctrl e Alt esclusi:
                 * una scorciatoia premuta per abitudine non deve
                 * rispondere al posto dell'utente a un dialogo che gli
                 * sta chiedendo se buttare via il lavoro. */
                char ch;
                int  i2;

                if (key & (KBD_MOD_CTRL | KBD_MOD_ALT)) break;

                ch = gf_minuscolo((char)(key & 0xFF));
                for (i2 = 0; i2 < n_opzioni; i2++) {
                    if (gf_minuscolo(opzioni[i2][0]) == ch) return i2;
                }
                break;
            }
        }
    }
}

/* =============================================================================
 * gf_dlg_stringa — un campo di testo su una riga
 *
 * Ritorna 1 se confermato, 0 se annullato. Il buffer entra con il
 * valore predefinito e ne esce modificato solo in caso di conferma.
 * ============================================================================= */
int gf_dlg_stringa(GfEdit *self, const char *titolo, const char *etichetta,
                   char *buf, int size)
{
    char lavoro[GF_MAX_PATH];
    int  w = GF_COLS - 20;
    int  h = 7, r, c;
    int  len, pos;

    if (w < (int)strlen(titolo) + 8) w = (int)strlen(titolo) + 8;
    if (w > GF_COLS - 2) w = GF_COLS - 2;
    r = (GF_ROWS - h) / 2;
    c = (GF_COLS - w) / 2;

    gf_strlcpy(lavoro, buf, (int)sizeof(lavoro));
    len = (int)strlen(lavoro);
    pos = len;

    for (;;) {
        unsigned key;
        int campo_w = w - 4;
        int primo   = 0;

        /* Se il testo è più lungo del campo, si mostra la finestra che
         * contiene il cursore. */
        if (pos >= campo_w) primo = pos - campo_w + 1;

        gf_componi(self);
        riquadro_titolato(r, c, h, w, titolo);
        gf_term_scrivi_n(r + 1, c + 2, etichetta, w - 4, CP_DIALOGO);
        gf_term_riempi(r + 3, c + 2, campo_w, ' ', CP_DLG_CAMPO);
        gf_term_scrivi_n(r + 3, c + 2, lavoro + primo, campo_w, CP_DLG_CAMPO);
        gf_term_scrivi(r + 5, c + 2, "Invio = conferma   ESC = annulla", CP_DIALOGO);

        gf_term_cursore(r + 3, c + 2 + (pos - primo), 1);
        gf_term_flush();

        key = gf_getkey();
        switch (key & KBD_KEY_MASK) {
            case 27: return 0;

            case '\n':
                gf_strlcpy(buf, lavoro, size);
                return 1;

            case '\b':
                if (pos > 0) {
                    memmove(lavoro + pos - 1, lavoro + pos, (size_t)(len - pos + 1));
                    pos--; len--;
                }
                break;

            case KBD_K_DEL:
                if (pos < len) {
                    memmove(lavoro + pos, lavoro + pos + 1, (size_t)(len - pos));
                    len--;
                }
                break;

            case KBD_K_LEFT:  if (pos > 0)   pos--; break;
            case KBD_K_RIGHT: if (pos < len) pos++; break;
            case KBD_K_HOME:  pos = 0;   break;
            case KBD_K_END:   pos = len; break;

            default: {
                char ch = (char)(key & 0xFF);

                /* Ctrl e Alt NON producono testo. Senza questo controllo
                 * un Ctrl+A premuto per abitudine dentro un campo di
                 * ricerca ci scriverebbe una 'a' — e la ricerca
                 * fallirebbe cercando qualcosa che l'utente non ha mai
                 * digitato, senza che niente lo faccia sospettare. In
                 * modalità raw il modificatore viaggia a parte proprio
                 * per poterlo distinguere (vedi kbd_proto.h). */
                if (key & (KBD_MOD_CTRL | KBD_MOD_ALT)) break;

                if ((key & KBD_KEY_MASK) < 0x100 && (unsigned char)ch >= 32 &&
                    len < (int)sizeof(lavoro) - 1 && len < size - 1) {
                    memmove(lavoro + pos + 1, lavoro + pos, (size_t)(len - pos + 1));
                    lavoro[pos++] = ch;
                    len++;
                }
                break;
            }
        }
    }
}

/* =============================================================================
 * gf_dlg_testo_scorrevole — visualizzatore per aiuto e licenza
 * ============================================================================= */
int gf_dlg_testo_scorrevole(GfEdit *self, const char *titolo,
                            const char **righe, int n)
{
    int w = GF_COLS - 6;
    int h = GF_ROWS - 4;
    int r = 2, c = 3;
    int visibili = h - 4;
    int top = 0;

    for (;;) {
        unsigned key;
        int i;

        gf_componi(self);
        riquadro_titolato(r, c, h, w, titolo);

        for (i = 0; i < visibili && top + i < n; i++) {
            gf_term_scrivi_n(r + 1 + i, c + 2, righe[top + i], w - 4, CP_DIALOGO);
        }

        {
            char stato[64];
            gf_fmt(stato, sizeof(stato), "Riga %d/%d  Frecce, PagSu/PagGiu  ESC = chiudi",
                   top + 1, n);
            gf_term_scrivi(r + h - 2, c + 2, stato, CP_DIALOGO);
        }

        gf_term_cursore(r + h - 2, c + 2, 0);
        gf_term_flush();

        key = gf_getkey();
        switch (key & KBD_KEY_MASK) {
            case 27:
            case '\n':        return 0;
            case KBD_K_UP:    if (top > 0) top--; break;
            case KBD_K_DOWN:  if (top < n - visibili) top++; break;
            case KBD_K_PGUP:  top -= visibili; break;
            case KBD_K_PGDN:  top += visibili; break;
            case KBD_K_HOME:  top = 0; break;
            case KBD_K_END:   top = n - visibili; break;
            default: break;
        }
        if (top > n - visibili) top = n - visibili;
        if (top < 0) top = 0;
    }
}

/* =============================================================================
 * gf_dlg_sfoglia — navigazione fra directory e file
 *
 * listdir_from() consegna al massimo LISTDIR_MAX_BATCH voci per
 * chiamata: chiederne di più non è un errore ma non serve, e chi usa
 * l'idioma "ne ho ricevute meno di quante ne ho chieste, quindi sono
 * finite" si fermerebbe a metà directory. Vedi il commento su
 * listdir_from in lib/include/libc.h.
 * ============================================================================= */
#define SFOGLIA_MAX 128

typedef struct {
    char name[64];
    unsigned char is_dir;
} VoceSfoglia;

static VoceSfoglia sfoglia_voci[SFOGLIA_MAX];

static int leggi_directory(const char *path)
{
    DirEntry blocco[LISTDIR_MAX_BATCH];
    int n = 0, start = 0, letti;

    /* Prima voce sempre presente: risalire di un livello deve essere
     * possibile anche da una directory vuota. */
    gf_strlcpy(sfoglia_voci[0].name, "..", 64);
    sfoglia_voci[0].is_dir = 1;
    n = 1;

    while (n < SFOGLIA_MAX &&
           (letti = listdir_from(path, blocco, LISTDIR_MAX_BATCH, start)) > 0) {
        int i;

        for (i = 0; i < letti && n < SFOGLIA_MAX; i++) {
            if (strcmp(blocco[i].name, ".") == 0 ||
                strcmp(blocco[i].name, "..") == 0) continue;
            gf_strlcpy(sfoglia_voci[n].name, blocco[i].name, 64);
            sfoglia_voci[n].is_dir = blocco[i].is_dir;
            n++;
        }

        start += letti;
        if (letti < LISTDIR_MAX_BATCH) break;
    }

    return n;
}

int gf_dlg_sfoglia(GfEdit *self, const char *titolo, char *out, int size,
                   int permetti_nuovo)
{
    char corrente[GF_MAX_PATH];
    int  w = GF_COLS - 20;
    int  h = GF_ROWS - 6;
    int  r = 3, c = 10;
    int  visibili = h - 5;
    int  n, sel = 0, top = 0;

    gf_strlcpy(corrente, self->directory, (int)sizeof(corrente));
    n = leggi_directory(corrente);

    for (;;) {
        unsigned key;
        int i;

        gf_componi(self);
        riquadro_titolato(r, c, h, w, titolo);
        gf_term_scrivi_n(r + 1, c + 2, corrente, w - 4, CP_DIALOGO);

        for (i = 0; i < visibili && top + i < n; i++) {
            VoceSfoglia *v = &sfoglia_voci[top + i];
            char riga[GF_COLS];
            unsigned char at = (top + i == sel) ? CP_MENU_SEL : CP_DIALOGO;

            gf_fmt(riga, sizeof(riga), "%s%s", v->name, v->is_dir ? "/" : "");
            gf_term_riempi(r + 3 + i, c + 2, w - 4, ' ', at);
            gf_term_scrivi_n(r + 3 + i, c + 2, riga, w - 4, at);
        }
        for (; i < visibili; i++) gf_term_riempi(r + 3 + i, c + 2, w - 4, ' ', CP_DIALOGO);

        gf_term_scrivi(r + h - 2, c + 2,
                       permetti_nuovo
                         ? "Invio = scegli   N = nuovo nome   ESC = annulla"
                         : "Invio = apri   ESC = annulla", CP_DIALOGO);

        gf_term_cursore(r + h - 2, c + 2, 0);
        gf_term_flush();

        key = gf_getkey();
        switch (key & KBD_KEY_MASK) {
            case 27: return 0;

            case KBD_K_UP:   if (sel > 0) sel--; break;
            case KBD_K_DOWN: if (sel < n - 1) sel++; break;
            case KBD_K_PGUP: sel -= visibili; break;
            case KBD_K_PGDN: sel += visibili; break;
            case KBD_K_HOME: sel = 0; break;
            case KBD_K_END:  sel = n - 1; break;

            case '\n':
                if (sel < 0 || sel >= n) break;

                if (sfoglia_voci[sel].is_dir) {
                    if (strcmp(sfoglia_voci[sel].name, "..") == 0) {
                        gf_path_padre(corrente);
                    } else {
                        char nuovo[GF_MAX_PATH];
                        gf_path_unisci(nuovo, sizeof(nuovo), corrente,
                                       sfoglia_voci[sel].name);
                        gf_strlcpy(corrente, nuovo, (int)sizeof(corrente));
                    }
                    gf_strlcpy(self->directory, corrente, GF_MAX_PATH);
                    n   = leggi_directory(corrente);
                    sel = 0;
                    top = 0;
                    break;
                }

                gf_path_unisci(out, size, corrente, sfoglia_voci[sel].name);
                gf_strlcpy(self->directory, corrente, GF_MAX_PATH);
                return 1;

            default:
                if (key & (KBD_MOD_CTRL | KBD_MOD_ALT)) break;
                if (permetti_nuovo && gf_minuscolo((char)(key & 0xFF)) == 'n') {
                    char nome[GF_MAX_NAME] = "";
                    if (gf_dlg_stringa(self, "Nuovo nome",
                                       "Nome del file da creare:",
                                       nome, sizeof(nome)) && nome[0]) {
                        gf_path_unisci(out, size, corrente, nome);
                        gf_strlcpy(self->directory, corrente, GF_MAX_PATH);
                        return 1;
                    }
                }
                break;
        }

        if (sel < 0) sel = 0;
        if (sel >= n) sel = n - 1;
        if (sel < top) top = sel;
        if (sel >= top + visibili) top = sel - visibili + 1;
    }
}

/* =============================================================================
 * gf_menu — la barra dei menu con le tendine
 *
 * 'menu_iniziale' è il menu da aprire subito (0..N-1), oppure -1 per
 * cominciare dal primo. Il ciclo è tutto qui dentro: si esce con ESC,
 * eseguendo una voce, oppure — come nei menu DOS — premendo di nuovo
 * F10.
 * ============================================================================= */
void gf_menu(GfEdit *self, int menu_iniziale)
{
    int m = (menu_iniziale >= 0 && menu_iniziale < N_MENU) ? menu_iniziale : 0;
    int v = 0;

    /* La prima voce non deve essere un separatore. Non lo è in nessuno
     * dei menu attuali, ma il ciclo di navigazione dà per scontato che
     * la selezione stia sempre su una voce eseguibile. */
    while (v < menus[m].n_voci && menus[m].voci[v].etichetta[0] == '-') v++;

    for (;;) {
        unsigned key;
        int c0 = menu_colonna(m);
        int w  = 0, i;
        int h;

        for (i = 0; i < menus[m].n_voci; i++) {
            int len = (int)strlen(menus[m].voci[i].etichetta) +
                      (int)strlen(menus[m].voci[i].scorciatoia) + 6;
            if (len > w) w = len;
        }
        h = menus[m].n_voci + 2;
        if (c0 + w > GF_COLS) c0 = GF_COLS - w;

        gf_componi(self);
        disegna_barra_menu(self, m);
        gf_term_riquadro(1, c0, h, w, CP_DIALOGO);

        for (i = 0; i < menus[m].n_voci; i++) {
            const GfVoce *voce = &menus[m].voci[i];
            unsigned char at = (i == v) ? CP_MENU_SEL : CP_DIALOGO;

            if (voce->etichetta[0] == '-') {
                gf_term_riempi(2 + i, c0 + 1, w - 2, (char)0xC4, CP_DIALOGO);
                continue;
            }

            gf_term_riempi(2 + i, c0 + 1, w - 2, ' ', at);
            gf_term_scrivi(2 + i, c0 + 2, voce->etichetta, at);
            if (voce->scorciatoia[0]) {
                gf_term_scrivi(2 + i,
                               c0 + w - 2 - (int)strlen(voce->scorciatoia),
                               voce->scorciatoia, at);
            }
        }

        gf_term_cursore(2 + v, c0 + 2, 0);
        gf_term_flush();

        key = gf_getkey();

        switch (key & KBD_KEY_MASK) {
            case 27:
                return;

            case KBD_K_LEFT:
                m = (m + N_MENU - 1) % N_MENU;
                v = 0;
                while (v < menus[m].n_voci && menus[m].voci[v].etichetta[0] == '-') v++;
                break;

            case KBD_K_RIGHT:
                m = (m + 1) % N_MENU;
                v = 0;
                while (v < menus[m].n_voci && menus[m].voci[v].etichetta[0] == '-') v++;
                break;

            case KBD_K_UP:
                do {
                    v = (v + menus[m].n_voci - 1) % menus[m].n_voci;
                } while (menus[m].voci[v].etichetta[0] == '-');
                break;

            case KBD_K_DOWN:
                do {
                    v = (v + 1) % menus[m].n_voci;
                } while (menus[m].voci[v].etichetta[0] == '-');
                break;

            case '\n':
                if (menus[m].voci[v].azione) {
                    menus[m].voci[v].azione(self);
                }
                return;

            default:
                if ((key & KBD_KEY_MASK) == KBD_K_F(10)) return;

                /* Iniziale della voce: esegue direttamente, come nei
                 * menu DOS. Ctrl e Alt esclusi, come negli altri
                 * dialoghi. */
                if (key & (KBD_MOD_CTRL | KBD_MOD_ALT)) break;
                {
                    char ch = gf_minuscolo((char)(key & 0xFF));
                    int  i2;
                    for (i2 = 0; i2 < menus[m].n_voci; i2++) {
                        if (menus[m].voci[i2].etichetta[0] == '-') continue;
                        if (gf_minuscolo(menus[m].voci[i2].etichetta[0]) == ch) {
                            if (menus[m].voci[i2].azione) menus[m].voci[i2].azione(self);
                            return;
                        }
                    }
                }
                break;
        }
    }
}

/* =============================================================================
 * Indice del menu associato a una lettera (per Alt+F, Alt+M, ...).
 * Ritorna -1 se la lettera non apre nessun menu.
 * ============================================================================= */
int gf_menu_da_lettera(char ch);

int gf_menu_da_lettera(char ch)
{
    int i;

    for (i = 0; i < N_MENU; i++) {
        if (gf_minuscolo(menus[i].acceleratore) == gf_minuscolo(ch)) return i;
    }
    return -1;
}
