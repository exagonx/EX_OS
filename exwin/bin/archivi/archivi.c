/* =============================================================================
 * exwin/bin/archivi/archivi.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The graphical archiver — WinZip's half of the request
 *
 *     /exwin/bin/archivi [ARCHIVIO.ZIP]
 *
 * ! THERE IS NO ZIP IN THIS FILE, and that is the whole reason the library
 * exists. Everything about the format lives in lib/exzip, and this program
 * calls the same functions /bin/zip calls. Written the other way round, the
 * second program rewrites the format and from that day the two disagree about
 * what a ZIP is.
 *
 * ! SINCE 26 SEPTEMBER 2026 (@ARCHIVI-VISTA) THERE IS ONE STATE: an archive is
 * always a real archive. «Nuovo» writes an empty one at once; every file or
 * directory added reopens it (ex_zip_riapri), adds and writes the catalogue
 * again — the archive is built by itself, as asked, and File > Costruisci
 * only rewrites the catalogue on request. Before, there were two states
 * (reading, building) and an archive that was not one until «Finisci».
 *
 * ! THE LIST IS A TABLE THIS PROGRAM DRAWS, not the toolkit's «lista»: that
 * control has fixed-pitch rows of 63 characters with columns made of spaces,
 * and what was asked — columns to click for sorting, borders to drag, a name
 * column and a path column — needs pixels. It is kept inside this file on
 * purpose, the house rule for a piece with one user: the day the file
 * manager wants the same, it moves into lib/exwin with both users to test.
 *
 * ! THE OPTIONS LIVE IN /exwin/config/archivi.cfg, the directory of the
 * configurations of ExWin programs (decided with the request; SVILUPPO.md).
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exzip.h"
#include "exinfo.h"
#include "kbd_proto.h"

/* +0.001 a ogni modifica: `archivi -version` la stampa. Vedi EX_VERSIONE. */
#define VERSIONE_APP "0.004"
EX_VERSIONE("archivi", VERSIONE_APP);

#define FIN_W       680
#define FIN_H       420

#define MENU_H      20
#define AREA_X      4
#define TAB_Y       (MENU_H + 2)    /* the header row of the table */
#define TESTA_H     18
#define RIGA_H      16
#define SCORRI_W    16
#define BASSO       24

#define PERC_MAX    320

#define ID_APRI       1
#define ID_NUOVO      2
#define ID_COSTRUISCI 3
#define ID_CHIUDI     4
#define ID_ESCI       5

#define ID_ESTRAI     10
#define ID_ESTRAI_TUT 11
#define ID_AGGIUNGI   12
#define ID_AGG_CART   13

#define ID_ISTRUZIONI 20
#define ID_INFO       21

#define ID_COMPRESS   25
#define ID_ESTENSIONE 26

#define ID_SCORRI     30

#define CONFIG_DIR    "/exwin/config"
#define CONFIG_FILE   "/exwin/config/archivi.cfg"

static ExFinestra g_f, g_scorri, g_stato, g_menu;

static ExZip *g_z;                  /* the archive on screen, open for reading */
static char   g_perc[PERC_MAX] = "";
static char   g_avviso[160] = "";
static char   g_dove[PERC_MAX] = "";   /* where the last extraction went */

/* The options (see opzioni_leggi) */
static unsigned int g_livello = 2;              /* 0 none .. 3 best */
static char         g_ext[16] = ".zip";

static const char *const LIVELLI[4] = { "nessuna", "veloce", "normale", "avanzata" };

/* =============================================================================
 * THE TABLE
 *
 * One row per entry of the archive. «Nome» is the last piece of the name,
 * «Percorso» the directories in front of it — which is where the file will
 * go, under the chosen directory, when it is extracted. A directory entry
 * shows its own name in «Nome» with a slash.
 *
 * ! THE ROWS POINT INTO ONE POOL OF NAMES, copied once when the archive is
 * opened. Sorting then compares strings in memory instead of asking the
 * library sixty thousand times per click.
 * ============================================================================= */
enum { C_NOME, C_PERC, C_BYTE, C_COMP, C_DATA, C_MET, COLONNE };

static const char *const TITOLI[COLONNE] = {
    "Nome", "Percorso", "Byte", "Compresso", "Data", "Metodo"
};
static int g_col_w[COLONNE] = { 170, 170, 80, 80, 88, 64 };
#define COL_MIN  24

typedef struct {
    unsigned int  i;            /* index in the archive */
    const char   *nome;
    const char   *perc;         /* "" when at the top */
    unsigned long dim, dim_c;
    unsigned long data;         /* yyyymmdd, to sort by */
    unsigned int  metodo;
    int           dir;
} Riga;

static Riga        *g_r = 0;
static char        *g_pool = 0;
static unsigned int g_rn = 0;

static int          g_ord = C_NOME;     /* the column the table is sorted by */
static int          g_ord_giu = 0;      /* 1 = descending */
static int          g_sel = -1;         /* the chosen row, -1 = none */
static unsigned int g_primo = 0;        /* the first row shown */

/* A column border being dragged: its column, and where the drag started. */
static int g_tira = -1, g_tira_x0 = 0, g_tira_w0 = 0;

/* The client size now: set at birth, changed by EXM_MISURA. */
static int g_w = FIN_W, g_h = FIN_H;
static int fin_w(void) { return g_w; }
static int fin_h(void) { return g_h; }
static int tab_w(void) { return fin_w() - AREA_X * 2 - SCORRI_W; }
static int righe_y(void) { return TAB_Y + TESTA_H; }
static int righe_h(void) { return fin_h() - righe_y() - BASSO; }
static unsigned int righe_viste(void)
{
    int n = righe_h() / RIGA_H;
    return n > 0 ? (unsigned int)n : 1u;
}

static void righe_libera(void)
{
    if (g_r) free(g_r);
    if (g_pool) free(g_pool);
    g_r = 0; g_pool = 0; g_rn = 0;
    g_sel = -1; g_primo = 0;
}

static int confronta(const Riga *a, const Riga *b)
{
    int r = 0;

    switch (g_ord) {
    case C_NOME: r = strcmp(a->nome, b->nome); break;
    case C_PERC:
        r = strcmp(a->perc, b->perc);
        if (r == 0) r = strcmp(a->nome, b->nome);
        break;
    case C_BYTE: r = a->dim   < b->dim   ? -1 : a->dim   > b->dim;   break;
    case C_COMP: r = a->dim_c < b->dim_c ? -1 : a->dim_c > b->dim_c; break;
    case C_DATA: r = a->data  < b->data  ? -1 : a->data  > b->data;  break;
    case C_MET:  r = (int)a->metodo - (int)b->metodo; break;
    }
    /* ! THE SAME ORDER EVERY TIME FOR EQUAL KEYS: the archive's own order.
     * Without it two clicks on «Metodo» could shuffle equal rows, and a
     * table that moves by itself looks broken. */
    if (r == 0) r = a->i < b->i ? -1 : a->i > b->i;
    return g_ord_giu ? -r : r;
}

static int confronta_q(const void *a, const void *b)
{
    return confronta((const Riga *)a, (const Riga *)b);
}

static void ordina(void)
{
    unsigned int scelta = (g_sel >= 0) ? g_r[g_sel].i : 0xFFFFFFFFu;
    unsigned int k;

    if (g_rn > 1) qsort(g_r, g_rn, sizeof(Riga), confronta_q);

    /* The chosen row stays chosen, wherever it went. */
    g_sel = -1;
    for (k = 0; k < g_rn; k++) if (g_r[k].i == scelta) { g_sel = (int)k; break; }
}

/* Reads the archive into the table. */
static void righe_carica(void)
{
    ExZipVoce     v;
    unsigned int  n, i;
    unsigned long pool = 0, usato = 0;

    righe_libera();
    if (!g_z) return;

    n = ex_zip_quante(g_z);
    if (n == 0) return;

    for (i = 0; i < n; i++)
        if (ex_zip_voce(g_z, i, &v)) pool += strlen(v.nome) + 2;

    g_r = (Riga *)malloc(n * sizeof(Riga));
    g_pool = (char *)malloc(pool + 1);
    if (!g_r || !g_pool) {
        righe_libera();
        strcpy(g_avviso, "non c'e' memoria per l'elenco");
        return;
    }

    for (i = 0; i < n; i++) {
        Riga        *r = &g_r[g_rn];
        char        *p = g_pool + usato;
        unsigned int l;
        char        *barra;

        if (!ex_zip_voce(g_z, i, &v)) continue;

        l = (unsigned int)strlen(v.nome);
        memcpy(p, v.nome, l + 1);
        usato += l + 2;

        /* A directory ends with '/': its own name is the piece before. */
        if (l > 1 && p[l - 1] == '/') p[l - 1] = '\0';
        barra = strrchr(p, '/');
        if (barra) { *barra = '\0'; r->perc = p; r->nome = barra + 1; }
        else       { r->perc = p + l; r->nome = p; }   /* p + l is "" */

        r->i      = i;
        r->dim    = v.dim;
        r->dim_c  = v.dim_c;
        r->data   = (unsigned long)v.anno * 10000ul + v.mese * 100ul + v.giorno;
        r->metodo = v.metodo;
        r->dir    = v.directory;
        g_rn++;
    }
    ordina();
}

/* The scroll bar says what the table can show. */
static void scorri_aggiorna(void)
{
    unsigned int vis = righe_viste();
    unsigned int max = g_rn > vis ? g_rn - vis : 0;

    if (g_primo > max) g_primo = max;
    ex_scorri_limiti(g_scorri, max, vis);
    ex_scorri_vai(g_scorri, g_primo);
}

/* ! TEXT IS CUT TO THE COLUMN, with a «~» when something is missing: the
 * system font is eight pixels wide, so the count is exact. A cut name must
 * not pass for a short one. */
static void scrivi_in(int x, int y, int w, const char *s, int destra, unsigned int c)
{
    char t[EXZIP_NOME_MAX];
    int  n = (w - 8) / 8, l = (int)strlen(s);

    if (n <= 0) return;
    if (n > (int)sizeof(t) - 1) n = (int)sizeof(t) - 1;
    if (l > n) { memcpy(t, s, (size_t)n); t[n - 1] = '~'; t[n] = '\0'; l = n; }
    else       { memcpy(t, s, (size_t)l + 1); }
    ex_scrivi(g_f, destra ? x + w - 4 - l * 8 : x + 4, y, t, c);
}

static void tabella_disegna(void)
{
    int          x0 = AREA_X, tw = tab_w(), y, c, x;
    unsigned int k, vis = righe_viste();
    char         t[48];

    /* The header: one raised cell per column, the sorted one with ^ or v. */
    ex_riempi(g_f, x0, TAB_Y, tw, TESTA_H, EX_GRIGIO);
    for (c = 0, x = x0; c < COLONNE && x < x0 + tw; x += g_col_w[c], c++) {
        int w = g_col_w[c];

        if (x + w > x0 + tw) w = x0 + tw - x;
        ex_rilievo(g_f, x, TAB_Y, w, TESTA_H);
        if (c == g_ord) snprintf(t, sizeof(t), "%s %c", TITOLI[c], g_ord_giu ? 'v' : '^');
        else            snprintf(t, sizeof(t), "%s", TITOLI[c]);
        scrivi_in(x, TAB_Y + 1, w, t, 0, EX_NERO);
    }

    /* The rows: a white well, the chosen one in blue. */
    y = righe_y();
    ex_riempi(g_f, x0, y, tw, righe_h(), EX_BIANCO);
    for (k = 0; k < vis && g_primo + k < g_rn; k++) {
        const Riga  *r  = &g_r[g_primo + k];
        int          ry = y + (int)k * RIGA_H;
        int          scelta = (int)(g_primo + k) == g_sel;
        unsigned int fg = scelta ? EX_BIANCO : EX_NERO;

        if (scelta) ex_riempi(g_f, x0, ry, tw, RIGA_H, EX_BLU);

        for (c = 0, x = x0; c < COLONNE && x < x0 + tw; x += g_col_w[c], c++) {
            int w = g_col_w[c];

            if (x + w > x0 + tw) w = x0 + tw - x;
            switch (c) {
            case C_NOME:
                if (r->dir) {
                    char n[EXZIP_NOME_MAX];
                    snprintf(n, sizeof(n), "%s/", r->nome);
                    scrivi_in(x, ry, w, n, 0, fg);
                } else {
                    scrivi_in(x, ry, w, r->nome, 0, fg);
                }
                break;
            case C_PERC: scrivi_in(x, ry, w, r->perc, 0, fg); break;
            case C_BYTE: snprintf(t, sizeof(t), "%lu", r->dim);   scrivi_in(x, ry, w, t, 1, fg); break;
            case C_COMP: snprintf(t, sizeof(t), "%lu", r->dim_c); scrivi_in(x, ry, w, t, 1, fg); break;
            case C_DATA:
                snprintf(t, sizeof(t), "%04lu-%02lu-%02lu",
                         r->data / 10000, (r->data / 100) % 100, r->data % 100);
                scrivi_in(x, ry, w, t, 0, fg);
                break;
            case C_MET:
                scrivi_in(x, ry, w, r->dir ? "-" : r->metodo == EXZIP_DEFLATE ? "deflate" : "store",
                          0, fg);
                break;
            }
        }
    }
    ex_incavo(g_f, x0, y, tw, righe_h());
}

/* Which column border is under x (within three pixels), or -1. */
static int bordo_sotto(int x)
{
    int c, bx = AREA_X;

    for (c = 0; c < COLONNE; c++) {
        bx += g_col_w[c];
        if (x >= bx - 3 && x <= bx + 3) return c;
    }
    return -1;
}

static int colonna_sotto(int x)
{
    int c, bx = AREA_X;

    for (c = 0; c < COLONNE; c++) {
        if (x >= bx && x < bx + g_col_w[c]) return c;
        bx += g_col_w[c];
    }
    return -1;
}

/* Brings the chosen row into view. */
static void mostra_scelta(void)
{
    unsigned int vis = righe_viste();

    if (g_sel < 0) return;
    if ((unsigned int)g_sel < g_primo) g_primo = (unsigned int)g_sel;
    if ((unsigned int)g_sel >= g_primo + vis) g_primo = (unsigned int)g_sel - vis + 1;
    scorri_aggiorna();
}

/* -----------------------------------------------------------------------------
 * Status, title, painting
 * --------------------------------------------------------------------------- */
static void stato(void)
{
    char t[PERC_MAX + 200];

    if (!g_z) snprintf(t, sizeof(t), "nessun archivio aperto.  %s", g_avviso);
    else      snprintf(t, sizeof(t), "%s - %u voci, compressione %s.  %s",
                       g_perc, g_rn, LIVELLI[g_livello], g_avviso);
    ex_testo_metti(g_stato, t);
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp);

static void ridisegna(void)
{
    proc(g_f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(g_f);
}

static void titolo(void)
{
    char t[PERC_MAX + 32];

    if (g_perc[0]) snprintf(t, sizeof(t), "Archivi - %s", g_perc);
    else           strcpy(t, "Archivi");
    ex_titolo(g_f, t);
}

/* -----------------------------------------------------------------------------
 * The options, in /exwin/config/archivi.cfg
 *
 *     compressione = normale        nessuna, veloce, normale, avanzata
 *     estensione   = .zip
 *
 * ! A MISSING OR BROKEN FILE IS NOT AN ERROR: the defaults are what the
 * program did before there were options. A line that is not understood is
 * skipped, and written back correctly at the next save.
 * --------------------------------------------------------------------------- */
static void taglia(char *s)
{
    char *p = s, *e;

    while (*p == ' ' || *p == '\t') p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r')) *--e = '\0';
}

static void opzioni_leggi(void)
{
    char buf[512], *riga, *dopo;
    int  fd, n;

    fd = open(CONFIG_FILE, O_RDONLY, 0);
    if (fd < 0) return;
    n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';

    for (riga = buf; riga && *riga; riga = dopo) {
        char *uguale;

        dopo = strchr(riga, '\n');
        if (dopo) *dopo++ = '\0';
        if (riga[0] == '#') continue;
        uguale = strchr(riga, '=');
        if (!uguale) continue;
        *uguale = '\0';
        taglia(riga);
        taglia(uguale + 1);

        if (strcmp(riga, "compressione") == 0) {
            unsigned int l;
            for (l = 0; l < 4; l++) if (strcmp(uguale + 1, LIVELLI[l]) == 0) g_livello = l;
        } else if (strcmp(riga, "estensione") == 0 && uguale[1] == '.' &&
                   strlen(uguale + 1) < sizeof(g_ext)) {
            strcpy(g_ext, uguale + 1);
        }
    }
}

/* Returns 1 if saved. On a read-only system (the live CD) it says so once:
 * the options then last until the program closes. */
static int opzioni_scrivi(void)
{
    char t[256];
    int  fd, n;

    if (mkdir(CONFIG_DIR, 0755) != 0) {
        struct stat st;
        if (stat(CONFIG_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    }
    fd = open(CONFIG_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    n = snprintf(t, sizeof(t),
                 "# Archivi: le opzioni. Le riscrive il programma (menu Opzioni).\n"
                 "compressione = %s\n"
                 "estensione   = %s\n", LIVELLI[g_livello], g_ext);
    if (write(fd, t, (unsigned int)n) != n) { close(fd); return 0; }
    close(fd);
    return 1;
}

static void opzioni_salva(void)
{
    if (opzioni_scrivi()) { snprintf(g_avviso, sizeof(g_avviso), "opzioni salvate in %s", CONFIG_FILE); return; }
    ex_dlg_avviso("Opzioni non salvate",
                  "Non riesco a scrivere " CONFIG_FILE " (il sistema e' in sola "
                  "lettura?). La scelta vale fino alla chiusura di Archivi.");
}

/* The compression dialog: four radios, «Salva» and «Annulla». */
#define ID_LIV0    100
#define ID_LIV_OK  110
#define ID_LIV_NO  111
static int        g_liv_fatto;          /* 0 open, 1 saved, -1 cancelled */
static ExFinestra g_liv_r[4];

static long liv_proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    if (msg == EXM_COMANDO && wp == ID_LIV_OK) { g_liv_fatto = 1;  return 0; }
    if (msg == EXM_COMANDO && wp == ID_LIV_NO) { g_liv_fatto = -1; return 0; }
    if (msg == EXM_CHIUDI)                     { g_liv_fatto = -1; return 0; }
    if (msg == EXM_TASTO && (wp & KBD_KEY_MASK) == 27) { g_liv_fatto = -1; return 0; }
    return ex_procedura_base(f, msg, wp, lp);
}

static void compressione_scegli(void)
{
    static const char *const spiega[4] = {
        "Nessuna: i file entrano come sono (store)",
        "Veloce: cerca poco, comprime meno",
        "Normale: come zlib -6 (predefinita)",
        "Avanzata: cerca di piu', comprime di piu'"
    };
    ExFinestra   f;
    ExMsg        m;
    unsigned int sw = 0, sh = 0, l;
    int          w = 360, h = 200;

    ex_schermo(&sw, &sh);
    g_liv_fatto = 0;
    f = ex_crea("finestra", "Compressione",
                EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_SOPRA | EX_MODALE,
                ((int)sw - w) / 2, ((int)sh - h) / 2, w, h, 0, 0, liv_proc);
    if (!f) return;

    for (l = 0; l < 4; l++) {
        g_liv_r[l] = ex_crea("radio", spiega[l], EX_FIGLIO, 16, 16 + (int)l * 26,
                             w - 32, 22, f, ID_LIV0 + l, 0);
        if (l == g_livello) ex_accendi(g_liv_r[l], 1);
    }
    ex_crea("pulsante", "Salva",   EX_FIGLIO, w - 196, h - 40, 84, 26, f, ID_LIV_OK, 0);
    ex_crea("pulsante", "Annulla", EX_FIGLIO, w - 104, h - 40, 84, 26, f, ID_LIV_NO, 0);
    ex_fuoco(g_liv_r[g_livello]);

    ex_procedura_base(f, EXM_DISEGNA, 0, 0);
    ex_aggiorna(f);
    while (g_liv_fatto == 0 && ex_prendi_msg(&m)) ex_smista(&m);

    if (g_liv_fatto == 1) {
        for (l = 0; l < 4; l++) if (ex_acceso(g_liv_r[l])) g_livello = l;
        ex_zip_livello(g_livello);
        opzioni_salva();
    }
    ex_distruggi(f);
}

static void estensione_scegli(void)
{
    char v[sizeof(g_ext)], t[sizeof(g_ext) + 1];
    unsigned int i;

    strcpy(v, g_ext);
    if (!ex_dlg_chiedi("Estensione predefinita",
                       "L'estensione dei nuovi archivi (per esempio .zip):",
                       "Salva", v, sizeof(v)))
        return;

    /* A leading dot is added if missing; only letters and digits after it. */
    snprintf(t, sizeof(t), "%s%s", v[0] == '.' ? "" : ".", v);
    for (i = 1; t[i]; i++) {
        char c = t[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) {
            ex_dlg_avviso("Estensione non valida",
                          "Solo lettere e cifre dopo il punto, per esempio .zip");
            return;
        }
    }
    if (i < 2 || i >= sizeof(g_ext)) {
        ex_dlg_avviso("Estensione non valida", "Da una a quattordici lettere dopo il punto.");
        return;
    }
    strcpy(g_ext, t);
    opzioni_salva();
}

/* -----------------------------------------------------------------------------
 * Opening, creating, building
 * --------------------------------------------------------------------------- */
static void chiudi_archivio(void)
{
    if (g_z) { ex_zip_chiudi(g_z); g_z = 0; }
    g_perc[0] = '\0';
    righe_libera();
    scorri_aggiorna();
    strcpy(g_avviso, "chiuso");
    titolo();
    stato();
}

/* Opens `perc` for reading and shows it. `ricarica` = the same archive again,
 * after it was rebuilt: the sorting and the chosen row stay. */
static int apri_(const char *perc, int ricarica)
{
    ExZip *z = ex_zip_apri(perc);

    if (!z) {
        ex_dlg_avviso("Non si apre", ex_zip_errore());
        return 0;
    }
    if (g_z) ex_zip_chiudi(g_z);
    g_z = z;
    if (perc != g_perc) {
        strncpy(g_perc, perc, PERC_MAX - 1);
        g_perc[PERC_MAX - 1] = '\0';
    }
    if (!ricarica) g_avviso[0] = '\0';

    righe_carica();
    if (!ricarica || g_sel < 0) { g_primo = 0; if (g_rn) g_sel = 0; }
    scorri_aggiorna();
    titolo();
    stato();
    return 1;
}

static void apri(const char *perc) { apri_(perc, 0); }

static void apri_con_dialogo(void)
{
    char p[PERC_MAX];

    strncpy(p, g_perc[0] ? g_perc : "/", PERC_MAX - 1);
    p[PERC_MAX - 1] = '\0';
    if (!ex_dlg_apri(p, sizeof(p))) return;
    apri(p);
}

static const char *nome_corto(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static void nuovo(void)
{
    char   p[PERC_MAX];
    ExZip *z;
    const char *base, *punto;

    /* ! THE DIALOG STARTS FROM THE DIRECTORY, NOT FROM A NAME: what is typed
     * is added after what is proposed, and «/nuovo.zip» plus «/disk/c» would
     * be a path nobody meant. The extension is added below instead. */
    strcpy(p, "/");
    if (!ex_dlg_percorso("Archivio nuovo", "Archivio:", "Crea", p, sizeof(p)))
        return;

    /* ! THE DEFAULT EXTENSION GOES ON A NAME THAT HAS NONE, and only then:
     * «foto.tar» stays as it was typed. */
    base  = nome_corto(p);
    punto = strrchr(base, '.');
    if (!punto && strlen(p) + strlen(g_ext) < sizeof(p)) strcat(p, g_ext);

    z = ex_zip_crea(p);
    if (!z || !ex_zip_finisci(z)) {
        ex_dlg_avviso("Non si crea", ex_zip_errore());
        if (z) ex_zip_chiudi(z);
        return;
    }
    ex_zip_chiudi(z);

    if (apri_(p, 0))
        strcpy(g_avviso, "archivio creato: aggiungi file o cartelle dal menu Comandi");
    stato();
}

/* =============================================================================
 * ADDING = REOPEN, ADD, BUILD, SHOW
 *
 * ! THE CATALOGUE IS WRITTEN EVEN WHEN THE ADDING FAILED. ex_zip_riapri()
 * writes new entries over the old catalogue: giving up there would lose every
 * old entry. What failed is left out of the new catalogue, and said.
 * ============================================================================= */
static ExZip *riapri_per_aggiungere(void)
{
    ExZip *z;

    if (!g_z) {
        ex_dlg_avviso("Nessun archivio",
                      "Apri un archivio o creane uno nuovo (menu File), poi "
                      "aggiungi.");
        return 0;
    }
    ex_zip_chiudi(g_z);
    g_z = 0;

    ex_zip_livello(g_livello);
    z = ex_zip_riapri(g_perc);
    if (!z) {
        const char *e = ex_zip_errore();
        ex_dlg_avviso("Non ci posso aggiungere",
                      e && e[0] ? e : "La libreria degli archivi di questo sistema "
                                      "e' di prima del 26 settembre 2026 e non sa "
                                      "aggiungere: va aggiornata insieme ad Archivi.");
        apri_(g_perc, 1);
        return 0;
    }
    return z;
}

static int costruisci_e_mostra(ExZip *z)
{
    int ok = ex_zip_finisci(z);

    if (!ok) ex_dlg_avviso("L'archivio non si costruisce", ex_zip_errore());
    ex_zip_chiudi(z);
    apri_(g_perc, 1);
    return ok;
}

static void aggiungi(void)
{
    char   p[PERC_MAX];
    ExZip *z;
    int    ok;

    strcpy(p, "/");
    if (!g_z) { riapri_per_aggiungere(); return; }
    if (!ex_dlg_apri(p, sizeof(p))) return;
    if (!(z = riapri_per_aggiungere())) return;

    ok = ex_zip_aggiungi(z, p, nome_corto(p));
    if (!ok) ex_dlg_avviso("Non l'ho aggiunto", ex_zip_errore());
    if (costruisci_e_mostra(z) && ok)
        snprintf(g_avviso, sizeof(g_avviso), "aggiunto %s: archivio costruito", nome_corto(p));
    stato();
}

static void aggiungi_cartella(void)
{
    static char p[PERC_MAX] = "/";
    char        nome[PERC_MAX];
    int         n, saltati = 0;
    unsigned int l;
    ExZip      *z;

    if (!g_z) { riapri_per_aggiungere(); return; }
    if (!ex_dlg_percorso("Aggiungi una cartella", "Cartella:", "Aggiungi", p, sizeof(p)))
        return;

    strncpy(nome, p, sizeof(nome) - 1);
    nome[sizeof(nome) - 1] = '\0';
    l = (unsigned int)strlen(nome);
    while (l > 1 && nome[l - 1] == '/') nome[--l] = '\0';
    if (strcmp(nome, "/") == 0) {
        ex_dlg_avviso("Non l'ho aggiunta",
                      "La radice intera non entra in un archivio: scegli una "
                      "cartella dentro.");
        return;
    }
    if (!(z = riapri_per_aggiungere())) return;

    n = ex_zip_aggiungi_albero(z, nome, nome_corto(nome), &saltati);
    if (n == -2) ex_dlg_avviso("Non l'ho aggiunta",
                               "La libreria degli archivi e' di prima del 23 "
                               "settembre 2026 e non sa le cartelle.");
    else if (n < 0) ex_dlg_avviso("Non l'ho aggiunta", ex_zip_errore());
    else if (saltati) {
        char t[256];
        snprintf(t, sizeof(t), "Aggiunti %d file; %s", n, ex_zip_errore());
        ex_dlg_avviso("Aggiunta a meta'", t);
    }
    if (costruisci_e_mostra(z) && n >= 0)
        snprintf(g_avviso, sizeof(g_avviso), "aggiunta %s/ con %d file: archivio costruito",
                 nome_corto(nome), n);
    stato();
}

/* File > Costruisci: the catalogue written again. It happens by itself after
 * every addition; asked by hand, it also proves the archive reopens. */
static void costruisci(void)
{
    ExZip *z;

    if (!g_z) { strcpy(g_avviso, "nessun archivio da costruire"); stato(); return; }
    if (!(z = riapri_per_aggiungere())) return;
    if (costruisci_e_mostra(z))
        snprintf(g_avviso, sizeof(g_avviso), "archivio costruito: %u voci", g_rn);
    stato();
}

/* -----------------------------------------------------------------------------
 * Extracting
 * --------------------------------------------------------------------------- */

/* ! LA CARTELLA SI SCEGLIE, NON SI INDOVINA, e il dialogo che lo chiede e'
 * quello dei percorsi: dentro c'e' «Nuova cartella», che e' esattamente cio'
 * che serve a chi estrae un archivio in un posto nuovo. */
static int chiedi_dove(void)
{
    char p[PERC_MAX];
    unsigned int l;

    strncpy(p, g_dove[0] ? g_dove : "/", PERC_MAX - 1);
    p[PERC_MAX - 1] = '\0';

    l = (unsigned int)strlen(p);
    if (l && p[l - 1] != '/' && l + 2 < sizeof(p)) { p[l] = '/'; p[l + 1] = '\0'; }

    if (!ex_dlg_percorso("Estrai in", "Cartella:", "Estrai", p, sizeof(p)))
        return 0;

    strncpy(g_dove, p, PERC_MAX - 1);
    g_dove[PERC_MAX - 1] = '\0';
    return 1;
}

/* Refuses a name that climbs out of the destination. Same check as /bin/zip,
 * and it is here as well because the two programs are two front doors to the
 * same archive: a defence that lives in only one of them is a defence that
 * depends on which door was used. */
static int nome_pericoloso(const char *n)
{
    unsigned int i;

    if (n[0] == '/' || n[0] == '\\') return 1;

    for (i = 0; n[i]; i++)
        if (n[i] == '.' && n[i + 1] == '.' &&
            (n[i + 2] == '/' || n[i + 2] == '\0') &&
            (i == 0 || n[i - 1] == '/'))
            return 1;

    return 0;
}

static void cartelle_per(const char *perc)
{
    char p[PERC_MAX];
    unsigned int i;

    strncpy(p, perc, PERC_MAX - 1);
    p[PERC_MAX - 1] = '\0';

    for (i = 1; p[i]; i++) {
        if (p[i] != '/') continue;
        p[i] = '\0';
        mkdir(p, 0755);
        p[i] = '/';
    }
}

/* Extracts one entry into g_dove. Returns 1, or 0 having said why. */
static int estrai_una(unsigned int i, char *perche, unsigned int max)
{
    ExZipVoce v;
    char      perc[PERC_MAX];

    if (!ex_zip_voce(g_z, i, &v)) return 0;

    if (nome_pericoloso(v.nome)) {
        snprintf(perche, max, "%s: esce dalla cartella scelta", v.nome);
        return 0;
    }

    if (snprintf(perc, sizeof(perc), "%s/%s", g_dove, v.nome) >= (int)sizeof(perc)) {
        snprintf(perche, max, "%s: percorso troppo lungo", v.nome);
        return 0;
    }

    cartelle_per(perc);

    if (!ex_zip_estrai(g_z, i, perc)) {
        snprintf(perche, max, "%s: %s", v.nome, ex_zip_errore());
        return 0;
    }
    return 1;
}

static void estrai_scelto(void)
{
    char perche[192];

    if (!g_z) { strcpy(g_avviso, "apri prima un archivio"); stato(); return; }
    if (g_sel < 0 || (unsigned int)g_sel >= g_rn) { strcpy(g_avviso, "scegli una riga"); stato(); return; }

    if (!chiedi_dove()) return;

    if (estrai_una(g_r[g_sel].i, perche, sizeof(perche))) {
        snprintf(g_avviso, sizeof(g_avviso), "estratto in %s", g_dove);
    } else {
        ex_dlg_avviso("Non estratto", perche);
        strcpy(g_avviso, "non estratto");
    }
    stato();
}

static void estrai_tutto(void)
{
    unsigned int i, n, fatti = 0, saltati = 0;
    char         perche[192], primo[192];

    if (!g_z) { strcpy(g_avviso, "apri prima un archivio"); stato(); return; }
    if (!chiedi_dove()) return;

    n = ex_zip_quante(g_z);
    primo[0] = '\0';

    for (i = 0; i < n; i++) {
        if (estrai_una(i, perche, sizeof(perche))) { fatti++; continue; }
        saltati++;
        /* ! IL PRIMO ERRORE E' QUELLO CHE SI MOSTRA, non l'ultimo: gli altri
         * ne sono quasi sempre la conseguenza. */
        if (!primo[0]) strncpy(primo, perche, sizeof(primo) - 1);
    }

    if (saltati) {
        char t[320];

        snprintf(t, sizeof(t), "Estratti %u file su %u.\n\nIl primo che non e' andato:\n%s",
                 fatti, n, primo);
        ex_dlg_avviso("Qualcosa non e' uscito", t);
    }

    snprintf(g_avviso, sizeof(g_avviso), "%u estratti in %s%s", fatti, g_dove,
             saltati ? ", qualcuno no" : "");
    stato();
}

/* -----------------------------------------------------------------------------
 * Info
 * --------------------------------------------------------------------------- */
static void istruzioni(void)
{
    ex_dlg_avviso("Istruzioni",
        "File, Apri... mostra cosa c'e' dentro un archivio. Un clic\n"
        "sull'intestazione di una colonna ordina per quella (un altro\n"
        "clic rovescia); il bordo fra due intestazioni si trascina.\n\n"
        "Comandi, Estrai tutto in... sceglie (o crea) la cartella;\n"
        "Invio o doppio clic estraggono la riga scelta.\n\n"
        "File, Nuovo... crea un archivio vuoto; Comandi, Aggiungi\n"
        "file... o Aggiungi cartella... lo riempiono, e l'archivio si\n"
        "costruisce da solo a ogni aggiunta.\n\n"
        "Opzioni: la compressione e l'estensione dei nuovi archivi,\n"
        "salvate in " CONFIG_FILE ".");
}

static void informazioni(void)
{
    char t[512];

    exinfo_testo(t, sizeof(t), "Archivi", VERSIONE_APP,
                 "Apre e crea archivi ZIP. Il formato lo sa exzip.so, "
                 "la stessa libreria che usa il comando `zip`.");
    ex_dlg_avviso("Informazioni su", t);
}

/* -----------------------------------------------------------------------------
 * The window procedure
 * --------------------------------------------------------------------------- */
static void esci(void)
{
    if (g_z) ex_zip_chiudi(g_z);
    ex_esci(0);
}

/* A mouse button down in the client area: the header or a row. */
static void clic(int x, int y)
{
    if (y >= TAB_Y && y < TAB_Y + TESTA_H) {
        int c = bordo_sotto(x);

        if (c >= 0) {                   /* a border: start dragging it */
            g_tira = c; g_tira_x0 = x; g_tira_w0 = g_col_w[c];
            return;
        }
        c = colonna_sotto(x);
        if (c < 0) return;
        if (c == g_ord) g_ord_giu = !g_ord_giu;
        else            { g_ord = c; g_ord_giu = 0; }
        ordina();
        mostra_scelta();
        return;
    }

    if (y >= righe_y() && y < righe_y() + righe_h() &&
        x >= AREA_X && x < AREA_X + tab_w()) {
        unsigned int k = g_primo + (unsigned int)((y - righe_y()) / RIGA_H);
        if (k < g_rn) g_sel = (int)k;
    }
}

static void tasto(unsigned int c)
{
    unsigned int vis = righe_viste();
    int          s = g_sel;

    if (g_rn == 0) return;
    switch (c) {
    case KBD_K_UP:   s = s > 0 ? s - 1 : 0; break;
    case KBD_K_DOWN: s = s + 1 < (int)g_rn ? s + 1 : (int)g_rn - 1; break;
    case KBD_K_PGUP: s = s > (int)vis ? s - (int)vis : 0; break;
    case KBD_K_PGDN: s = s + (int)vis < (int)g_rn ? s + (int)vis : (int)g_rn - 1; break;
    case KBD_K_HOME: s = 0; break;
    case KBD_K_END:  s = (int)g_rn - 1; break;
    default: return;
    }
    g_sel = s;
    mostra_scelta();
}

static void disponi(int w, int h)
{
    g_w = w; g_h = h;
    ex_sposta(g_scorri, w - AREA_X - SCORRI_W, righe_y());
    ex_misura(g_scorri, SCORRI_W, h - righe_y() - BASSO);
    ex_sposta(g_stato, 6, h - 20);
    ex_misura(g_stato, w - 12, 16);
    scorri_aggiorna();
}

static long proc(ExFinestra f, unsigned int msg, unsigned int wp, long lp)
{
    unsigned int c;

    switch (msg) {
    case EXM_COMANDO:
        if (wp == ID_SCORRI) { g_primo = (unsigned int)lp; break; }

        g_avviso[0] = '\0';
        if (wp == ID_APRI)       { apri_con_dialogo();  break; }
        if (wp == ID_NUOVO)      { nuovo();             break; }
        if (wp == ID_COSTRUISCI) { costruisci();        break; }
        if (wp == ID_CHIUDI)     { chiudi_archivio();   break; }
        if (wp == ID_ESCI)       { esci();              return 0; }

        if (wp == ID_ESTRAI)     { estrai_scelto();     break; }
        if (wp == ID_ESTRAI_TUT) { estrai_tutto();      break; }
        if (wp == ID_AGGIUNGI)   { aggiungi();          break; }
        if (wp == ID_AGG_CART)   { aggiungi_cartella(); break; }

        if (wp == ID_COMPRESS)   { compressione_scegli(); stato(); break; }
        if (wp == ID_ESTENSIONE) { estensione_scegli();   stato(); break; }

        if (wp == ID_ISTRUZIONI) { istruzioni();        break; }
        if (wp == ID_INFO)       { informazioni();      break; }
        return 0;

    case EXM_MOUSE_GIU:
        clic(EX_X(lp), EX_Y(lp));
        break;

    case EXM_MOUSE_MOSSO:
        if (g_tira < 0) return 0;
        {
            int w = g_tira_w0 + EX_X(lp) - g_tira_x0;
            g_col_w[g_tira] = w < COL_MIN ? COL_MIN : w;
        }
        break;

    case EXM_MOUSE_SU:
        g_tira = -1;
        return 0;

    case EXM_DOPPIOCLIC:
        if (EX_Y(lp) >= righe_y()) { clic(EX_X(lp), EX_Y(lp)); estrai_scelto(); }
        break;

    case EXM_TASTO:
        g_avviso[0] = '\0';
        c = wp & KBD_KEY_MASK;

        if (wp & KBD_MOD_CTRL) {
            if (c == 'o' || c == 'O') { apri_con_dialogo(); break; }
            if (c == 'n' || c == 'N') { nuovo();            break; }
            if (c == 'q' || c == 'Q') { esci();             return 0; }
        }
        if (c == '\n' || c == '\r') { estrai_scelto(); break; }
        if (c >= KBD_K_UP && c <= KBD_K_PGDN) { tasto(c); break; }
        return ex_procedura_base(f, msg, wp, lp);

    case EXM_CHIUDI:
        esci();
        return 0;

    /* ! EVERYTHING THIS PROGRAM DRAWS OF ITS OWN IS REPAINTED HERE: the
     * server asks for a repaint every time a dialog on top closes, and
     * ex_procedura_base() wipes the client area. See the note about the
     * header row that was lost twice on 22 September 2026. */
    case EXM_DISEGNA:
        ex_procedura_base(f, msg, wp, lp);
        tabella_disegna();
        return 0;

    case EXM_MISURA:
        disponi(EX_X(lp), EX_Y(lp));
        break;

    default:
        return ex_procedura_base(f, msg, wp, lp);
    }

    ridisegna();
    return 0;
}

int main(int argc, char **argv)
{
    ExMsg m;

    opzioni_leggi();
    ex_zip_livello(g_livello);

    g_f = ex_crea("finestra", "Archivi",
                  EX_TITOLO | EX_BORDO | EX_CHIUDI | EX_RIDIM,
                  EX_AUTO, EX_AUTO, FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) {
        printf("archivi: il server a finestre non risponde.\n");
        printf("         Avvialo con:  exwin\n");
        return 1;
    }

    g_menu = ex_menu(g_f);
    ex_menu_voce(g_menu, "File", "Apri...\tCtrl+O",  ID_APRI);
    ex_menu_voce(g_menu, "File", "Nuovo...\tCtrl+N", ID_NUOVO);
    ex_menu_voce(g_menu, "File", "Costruisci",       ID_COSTRUISCI);
    ex_menu_voce(g_menu, "File", "-",                0);
    ex_menu_voce(g_menu, "File", "Chiudi",           ID_CHIUDI);
    ex_menu_voce(g_menu, "File", "Esci\tCtrl+Q",     ID_ESCI);

    ex_menu_voce(g_menu, "Comandi", "Estrai il file scelto\tInvio", ID_ESTRAI);
    ex_menu_voce(g_menu, "Comandi", "Estrai tutto in...",           ID_ESTRAI_TUT);
    ex_menu_voce(g_menu, "Comandi", "-",                            0);
    ex_menu_voce(g_menu, "Comandi", "Aggiungi file...",             ID_AGGIUNGI);
    ex_menu_voce(g_menu, "Comandi", "Aggiungi cartella...",         ID_AGG_CART);

    ex_menu_voce(g_menu, "Opzioni", "Compressione...",          ID_COMPRESS);
    ex_menu_voce(g_menu, "Opzioni", "Estensione predefinita...", ID_ESTENSIONE);

    ex_menu_voce(g_menu, "Info", "Istruzioni",      ID_ISTRUZIONI);
    ex_menu_voce(g_menu, "Info", "Informazioni su", ID_INFO);

    g_scorri = ex_crea("scorrimento", "", EX_FIGLIO,
                       FIN_W - AREA_X - SCORRI_W, TAB_Y + TESTA_H,
                       SCORRI_W, FIN_H - TAB_Y - TESTA_H - BASSO, g_f, ID_SCORRI, 0);
    g_stato = ex_crea("etichetta", "", EX_FIGLIO,
                      6, FIN_H - 20, FIN_W - 12, 16, g_f, 0, 0);
    if (!g_scorri || !g_stato) {
        printf("archivi: non riesco a creare i controlli\n");
        return 1;
    }

    if (argc >= 2) apri(argv[1]);
    else           stato();

    ridisegna();

    while (ex_prendi_msg(&m)) ex_smista(&m);

    if (g_z) ex_zip_chiudi(g_z);
    return 0;
}
