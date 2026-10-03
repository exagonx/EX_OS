/* =============================================================================
 * exwin/bin/exspider/exspider.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * EXSpider, the two-deck solitaire (@GIOCHI, asked on 3 October 2026)
 *
 *     /exwin/bin/exspider
 *
 * 104 cards in ten columns and a stock of fifty, dealt ten at a time. A card
 * goes on one a value higher of any suit; a run can be moved only if it is all
 * of one suit. A complete run from king to ace of one suit leaves the table;
 * eight runs win. One, two or four suits, chosen in the Partita menu and kept
 * in $HOME/.exwin/config/exspider.cfg with the statistics.
 *
 * ! THE STOCK IS DEALT ONLY WHEN NO COLUMN IS EMPTY, the rule of the classic
 * game: with it, an empty column is a resource to fill before dealing.
 *
 * The score is the classic one: 500, minus one per move, plus 100 per run.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"
#include "kbd_proto.h"
#include "exgioco.h"

#define VERSIONE_APP "0.001"
EX_VERSIONE("exspider", VERSIONE_APP);

#define NOME_CFG  "exspider"

#define CW        64
#define CH        87
#define GAP       8
#define N_COL     10
#define TAV_W     (N_COL * CW + (N_COL + 1) * GAP)
#define TAV_H     500
#define STATO_H   20
#define COL_Y     8
#define BASSO_Y   (TAV_H - STATO_H - CH - 8)
#define PASSO_GIU 5
#define PASSO_SU  20
#define SOGLIA    4
#define LUNGA     104

enum {
    ID_NUOVA = 1, ID_RICOMINCIA, ID_ANNULLA, ID_SUGGERISCI, ID_SEMI1, ID_SEMI2,
    ID_SEMI4, ID_STAT, ID_ESCI, ID_ISTR, ID_INFO
};

typedef struct {
    signed char   c[N_COL][LUNGA];
    unsigned char n[N_COL];
    unsigned char coperte[N_COL];
    signed char   mazzo[50];
    unsigned char nmazzo;
    unsigned char fatte;            /* runs gone to the foundation */
    signed char   semi_fatti[8];    /* the suit of each, to draw its king */
    int           punti;
    unsigned int  mosse;
} Stato;

static ExWindow g_f, g_menu;
static Tela     g_t, g_base;
static ExFont   g_font;
static Stato    g_s;
static signed char g_iniziale[104];

#define STORIA_MAX 300
static Stato   *g_storia;
static int      g_nstoria;

static int      g_semi = 1;
static int      g_partite, g_vinte, g_record;
static unsigned int g_inizio_ms, g_secondi;
static int      g_finita;

static int g_premuto, g_tira, g_da, g_da_i, g_px, g_py, g_ox, g_oy, g_tx, g_ty;
static int g_rx0, g_ry0, g_rx1, g_ry1;
static int g_sugg_da = -1, g_sugg_i, g_sugg_a;     /* the hint on show */

/* --- Places ----------------------------------------------------------------- */
static int col_x(int c) { return GAP + c * (CW + GAP); }

static int passo_su(int c)
{
    int giu = g_s.coperte[c], su = g_s.n[c] - giu, spazio, passo = PASSO_SU;

    if (su <= 1) return PASSO_SU;
    spazio = BASSO_Y - 10 - COL_Y - giu * PASSO_GIU - CH;
    if ((su - 1) * passo > spazio) passo = spazio / (su - 1);
    if (passo < 6) passo = 6;
    return passo;
}

static int carta_y(int c, int i)
{
    int giu = g_s.coperte[c];

    if (i < giu) return COL_Y + i * PASSO_GIU;
    return COL_Y + giu * PASSO_GIU + (i - giu) * passo_su(c);
}

/* The stock, bottom right: one back for each deal left, a little apart. */
static int mazzo_x(void) { return TAV_W - GAP - CW - 4 * 10; }

/* --- Rules ------------------------------------------------------------------ */
/* The cards from i to the top are one run of one suit, each a value lower. */
static int in_fila(int c, int i)
{
    int k;

    if (i < g_s.coperte[c] || i >= g_s.n[c]) return 0;
    for (k = i + 1; k < g_s.n[c]; k++) {
        int a = g_s.c[c][k - 1], b = g_s.c[c][k];
        if (SEME(a) != SEME(b) || VALORE(a) != VALORE(b) + 1) return 0;
    }
    return 1;
}

static int puo_andare(int carta, int dest)
{
    int n = g_s.n[dest];

    return !n || VALORE(g_s.c[dest][n - 1]) == VALORE(carta) + 1;
}

static void ricorda(void)
{
    if (!g_storia) return;
    if (g_nstoria == STORIA_MAX) {
        memmove(g_storia, g_storia + 1, sizeof(Stato) * (STORIA_MAX - 1));
        g_nstoria--;
    }
    g_storia[g_nstoria++] = g_s;
}

static void scopri(int c)
{
    if (g_s.n[c] && g_s.coperte[c] >= g_s.n[c]) g_s.coperte[c] = (unsigned char)(g_s.n[c] - 1);
}

/* A run from king to ace of one suit on top of the column leaves it. */
static void togli_complete(int c)
{
    int n = g_s.n[c], i = n - 13;

    if (i < 0 || VALORE(g_s.c[c][i]) != 12 || !in_fila(c, i)) return;
    g_s.semi_fatti[g_s.fatte++] = (signed char)SEME(g_s.c[c][i]);
    g_s.n[c] = (unsigned char)i;
    g_s.punti += 100;
    scopri(c);
}

static void avvia_orologio(void)
{
    if (!g_inizio_ms) g_inizio_ms = uptime_ms() - g_secondi * 1000u;
}

static void sposta(int da, int i, int a)
{
    int k, quante = g_s.n[da] - i;

    for (k = 0; k < quante; k++) g_s.c[a][g_s.n[a] + k] = g_s.c[da][i + k];
    g_s.n[a] = (unsigned char)(g_s.n[a] + quante);
    g_s.n[da] = (unsigned char)i;
    scopri(da);
    g_s.mosse++;
    g_s.punti--;
    togli_complete(a);
    avvia_orologio();
}

/* --- Drawing ---------------------------------------------------------------- */
static void disegna_stato(void)
{
    char t[160], tempo[16];
    int  y = TAV_H - STATO_H;

    tela_sfuma(&g_t, 0, y, TAV_W, STATO_H, 0x1B5E31, 0x134A26);
    gioco_tempo_testo(tempo, g_secondi);
    snprintf(t, sizeof(t), "Punti: %d    Mosse: %u    Tempo: %s    %d %s",
             g_s.punti, g_s.mosse, tempo, g_semi, g_semi == 1 ? "seme" : "semi");
    tela_testo(&g_t, g_font, 8, y + 3, t, 0xE6F2E8);
    snprintf(t, sizeof(t), "Vinte %d su %d", g_vinte, g_partite);
    tela_testo(&g_t, g_font, TAV_W - 8 - ex_text_width(g_font, t), y + 3, t, 0xE6F2E8);
}

static void disegna_tavolo(int via, int via_i)
{
    int c, i;

    tela_sfuma(&g_t, 0, 0, TAV_W, TAV_H - STATO_H, 0x2E8B4A, 0x1D6B36);

    for (c = 0; c < N_COL; c++) {
        int n = g_s.n[c];
        if (via == c) n = via_i;
        if (!g_s.n[c]) carta_posto(&g_t, col_x(c), COL_Y, "");
        for (i = 0; i < n; i++)
            carta_disegna(&g_t, col_x(c), carta_y(c, i),
                          i < g_s.coperte[c] ? CARTA_DORSO : g_s.c[c][i]);
    }

    /* The runs done, bottom left: the king of each, a little apart. */
    for (i = 0; i < 8; i++) {
        if (i < g_s.fatte) carta_disegna(&g_t, GAP + i * 22, BASSO_Y, g_s.semi_fatti[i] * 13 + 12);
    }
    if (!g_s.fatte) carta_posto(&g_t, GAP, BASSO_Y, "K");

    for (i = 0; i < g_s.nmazzo / 10; i++)
        carta_disegna(&g_t, mazzo_x() + i * 10, BASSO_Y, CARTA_DORSO);

    if (g_sugg_da >= 0 && via < 0) {
        carta_evidenzia(&g_t, col_x(g_sugg_da), carta_y(g_sugg_da, g_sugg_i), 0xFFE040);
        if (g_s.n[g_sugg_a])
            carta_evidenzia(&g_t, col_x(g_sugg_a), carta_y(g_sugg_a, g_s.n[g_sugg_a] - 1), 0xFFE040);
        else
            carta_evidenzia(&g_t, col_x(g_sugg_a), COL_Y, 0xFFE040);
    }
    disegna_stato();
}

static void ridisegna(void)
{
    disegna_tavolo(-1, 0);
    tela_mostra(g_f, &g_t, 0, 0, TAV_W, TAV_H);
    gioco_annuncia(g_f, 1);
}

/* --- Games ------------------------------------------------------------------ */
static void statistiche_salva(void)
{
    char t[240];

    snprintf(t, sizeof(t),
             "# EXSpider: le scelte e le partite. Le riscrive il programma.\n"
             "semi    = %d\npartite = %d\nvinte   = %d\nrecord  = %d\n",
             g_semi, g_partite, g_vinte, g_record);
    gioco_cfg_scrivi(NOME_CFG, t);
}

static void distribuisci(void)
{
    int c, k = 0, i;

    memset(&g_s, 0, sizeof(g_s));
    for (c = 0; c < N_COL; c++) {
        int quante = c < 4 ? 6 : 5;
        for (i = 0; i < quante; i++) g_s.c[c][i] = g_iniziale[k++];
        g_s.n[c] = (unsigned char)quante;
        g_s.coperte[c] = (unsigned char)(quante - 1);
    }
    for (i = 0; i < 50; i++) g_s.mazzo[i] = g_iniziale[k++];
    g_s.nmazzo = 50;
    g_s.punti = 500;
}

static void nuova(int stessa)
{
    int i;

    if (!stessa) {
        /* Eight runs of thirteen: with one suit all spades, with two spades
         * and hearts, with four all of them. */
        for (i = 0; i < 104; i++) {
            g_iniziale[i] = (signed char)(((i / 13) % g_semi) * 13 + i % 13);
        }
        for (i = 103; i > 0; i--) {
            int j = (int)caso_fino((unsigned int)i + 1);
            signed char x = g_iniziale[i];
            g_iniziale[i] = g_iniziale[j];
            g_iniziale[j] = x;
        }
    }
    distribuisci();
    g_nstoria = 0;
    g_inizio_ms = 0;
    g_secondi = 0;
    g_finita = 0;
    g_premuto = g_tira = 0;
    g_sugg_da = -1;
    g_partite++;
    statistiche_salva();
    ridisegna();
}

static void controlla_fine(void)
{
    char t[240], tempo[16];

    if (g_finita || g_s.fatte < 8) return;
    g_finita = 1;
    g_vinte++;
    if (g_s.punti > g_record) g_record = g_s.punti;
    statistiche_salva();
    ridisegna();
    gioco_tempo_testo(tempo, g_secondi);
    snprintf(t, sizeof(t), "Hai vinto con %d punti, in %u mosse e %s.\n\nUn'altra partita?",
             g_s.punti, g_s.mosse, tempo);
    if (ex_dlg_conferma("EXSpider", t, "Nuova partita", "Basta cosi'")) nuova(0);
    else ridisegna();
}

static void distribuisci_fila(void)
{
    int c;

    if (!g_s.nmazzo || g_finita) return;
    for (c = 0; c < N_COL; c++)
        if (!g_s.n[c]) {
            ex_dlg_avviso("EXSpider", "Prima di distribuire, nessuna colonna deve "
                          "restare vuota: mettici una carta.");
            ridisegna();
            return;
        }
    ricorda();
    for (c = 0; c < N_COL; c++) {
        g_s.c[c][g_s.n[c]++] = g_s.mazzo[--g_s.nmazzo];
        togli_complete(c);
    }
    g_s.mosse++;
    g_s.punti--;
    g_sugg_da = -1;
    avvia_orologio();
    ridisegna();
    controlla_fine();
}

/* The best place for the run from i: a column that continues its suit, then
 * any column that takes it, then an empty one. -1 if none. */
static int dove_va(int c, int i)
{
    int a, carta = g_s.c[c][i], vuota = -1, qualunque = -1;

    for (a = 0; a < N_COL; a++) {
        int n = g_s.n[a];
        if (a == c) continue;
        if (!n) { if (vuota < 0 && i > 0) vuota = a; continue; }
        if (VALORE(g_s.c[a][n - 1]) != VALORE(carta) + 1) continue;
        if (SEME(g_s.c[a][n - 1]) == SEME(carta)) return a;
        if (qualunque < 0) qualunque = a;
    }
    return qualunque >= 0 ? qualunque : vuota;
}

/* A move worth suggesting: the longest run that can go somewhere useful (not
 * from a column to an empty one when it is already at the bottom). */
static int suggerisci_calcola(int *da, int *i_out, int *a_out)
{
    int c, best = -1, punteggio = -1;

    for (c = 0; c < N_COL; c++) {
        int i;
        for (i = g_s.coperte[c]; i < g_s.n[c]; i++) {
            int a, p;
            if (!in_fila(c, i)) continue;
            a = dove_va(c, i);
            if (a < 0) continue;
            p = (g_s.n[c] - i) * 2;
            if (g_s.n[a] && SEME(g_s.c[a][g_s.n[a] - 1]) == SEME(g_s.c[c][i])) p += 30;
            if (i == g_s.coperte[c] && g_s.coperte[c]) p += 20;   /* turns a card */
            if (!g_s.n[a]) p -= 10;
            /* Moving to another column of the same value, same suit gained
             * nothing: skip it. */
            if (i > 0 && i > g_s.coperte[c] && g_s.n[a] &&
                VALORE(g_s.c[c][i - 1]) == VALORE(g_s.c[c][i]) + 1 &&
                SEME(g_s.c[c][i - 1]) == SEME(g_s.c[c][i])) continue;
            if (i > 0 && i > g_s.coperte[c] && g_s.n[a] &&
                VALORE(g_s.c[c][i - 1]) == VALORE(g_s.c[c][i]) + 1 &&
                SEME(g_s.c[a][g_s.n[a] - 1]) != SEME(g_s.c[c][i])) continue;
            if (p > punteggio) { punteggio = p; best = c; *i_out = i; *a_out = a; }
            break;              /* the longest run of this column is enough */
        }
    }
    *da = best;
    return best >= 0;
}

static void suggerisci(void)
{
    int da, i, a;

    if (g_finita) return;
    if (suggerisci_calcola(&da, &i, &a)) {
        g_sugg_da = da; g_sugg_i = i; g_sugg_a = a;
        ridisegna();
        return;
    }
    ex_dlg_avviso("Suggerimento", g_s.nmazzo ? "Nessuna mossa utile: distribuisci "
                  "una fila dal mazzo (un clic sul mazzo in basso a destra)."
                  : "Nessuna mossa utile. Prova ad annullare qualche mossa (Ctrl+Z).");
    ridisegna();
}

/* --- Mouse ------------------------------------------------------------------ */
static int dentro(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static int trova(int x, int y, int *col, int *ind)
{
    int c, i;

    for (c = 0; c < N_COL; c++) {
        if (x < col_x(c) || x >= col_x(c) + CW) continue;
        for (i = g_s.n[c] - 1; i >= 0; i--)
            if (dentro(x, y, col_x(c), carta_y(c, i), CW, CH)) { *col = c; *ind = i; return 1; }
    }
    return 0;
}

static int destinazione(int mx, int my)
{
    int c, cx = g_tx + CW / 2, cy = g_ty + CH / 3;

    for (c = 0; c < N_COL; c++) {
        int x = col_x(c), h = CH;
        if (g_s.n[c]) h = carta_y(c, g_s.n[c] - 1) - COL_Y + CH;
        if (dentro(cx, cy, x, COL_Y, CW, h) || dentro(mx, my, x, COL_Y, CW, h)) return c;
    }
    return -1;
}

static void tira_disegna(void)
{
    int k, n = g_s.n[g_da] - g_da_i, passo = passo_su(g_da);
    int x0 = g_tx, y0 = g_ty, x1 = g_tx + CW, y1 = g_ty + CH + (n - 1) * passo;
    int ux0, uy0, ux1, uy1;

    tela_copia(&g_t, g_rx0, g_ry0, &g_base, g_rx0, g_ry0, g_rx1 - g_rx0, g_ry1 - g_ry0, 0);
    for (k = 0; k < n; k++) carta_disegna(&g_t, g_tx, g_ty + k * passo, g_s.c[g_da][g_da_i + k]);
    ux0 = x0 < g_rx0 ? x0 : g_rx0;  uy0 = y0 < g_ry0 ? y0 : g_ry0;
    ux1 = x1 > g_rx1 ? x1 : g_rx1;  uy1 = y1 > g_ry1 ? y1 : g_ry1;
    tela_mostra(g_f, &g_t, ux0, uy0, ux1 - ux0, uy1 - uy0);
    gioco_annuncia(g_f, 0);
    g_rx0 = x0; g_ry0 = y0; g_rx1 = x1; g_ry1 = y1;
}

static void mouse_giu(int x, int y, int doppio)
{
    int c, i;

    y -= GIOCO_MENU_H;
    if (g_finita) return;
    if (g_sugg_da >= 0) { g_sugg_da = -1; ridisegna(); }
    if (g_s.nmazzo && dentro(x, y, mazzo_x(), BASSO_Y, CW + (g_s.nmazzo / 10 - 1) * 10, CH)) {
        distribuisci_fila();
        return;
    }
    if (doppio || !trova(x, y, &c, &i) || !in_fila(c, i)) return;
    g_premuto = 1;
    g_tira = 0;
    g_da = c;
    g_da_i = i;
    g_px = x; g_py = y;
    g_tx = col_x(c);
    g_ty = carta_y(c, i);
    g_ox = x - g_tx;
    g_oy = y - g_ty;
}

static void mouse_mosso(int x, int y)
{
    y -= GIOCO_MENU_H;
    if (!g_premuto) return;
    if (!g_tira) {
        if ((x - g_px) * (x - g_px) + (y - g_py) * (y - g_py) < SOGLIA * SOGLIA) return;
        g_tira = 1;
        disegna_tavolo(g_da, g_da_i);
        tela_copia(&g_base, 0, 0, &g_t, 0, 0, TAV_W, TAV_H, 0);
        g_rx0 = g_rx1 = g_tx;
        g_ry0 = g_ry1 = g_ty;
    }
    g_tx = x - g_ox;
    g_ty = y - g_oy;
    tira_disegna();
}

static void mouse_su(int x, int y)
{
    int dest;

    y -= GIOCO_MENU_H;
    if (!g_premuto) return;
    g_premuto = 0;
    if (!g_tira) {
        dest = dove_va(g_da, g_da_i);
        if (dest >= 0) { ricorda(); sposta(g_da, g_da_i, dest); }
        ridisegna();
        controlla_fine();
        return;
    }
    g_tira = 0;
    dest = destinazione(x, y);
    if (dest >= 0 && dest != g_da && puo_andare(g_s.c[g_da][g_da_i], dest)) {
        ricorda();
        sposta(g_da, g_da_i, dest);
    }
    ridisegna();
    controlla_fine();
}

static void annulla(void)
{
    if (!g_nstoria || g_finita) return;
    g_s = g_storia[--g_nstoria];
    g_s.mosse++;
    g_s.punti--;
    g_sugg_da = -1;
    ridisegna();
}

static void scegli_semi(int n)
{
    g_semi = n;
    ex_menu_check(g_menu, ID_SEMI1, n == 1);
    ex_menu_check(g_menu, ID_SEMI2, n == 2);
    ex_menu_check(g_menu, ID_SEMI4, n == 4);
    nuova(0);
}

static void statistiche(void)
{
    char t[240];

    snprintf(t, sizeof(t), "Partite giocate: %d\nPartite vinte: %d\nPercentuale: %d%%\n"
             "Punteggio migliore: %d", g_partite, g_vinte,
             g_partite ? g_vinte * 100 / g_partite : 0, g_record);
    ex_dlg_avviso("Statistiche", t);
}

static void informazioni(void)
{
    char t[640];

    exinfo_testo(t, sizeof(t), "EXSpider", VERSIONE_APP,
                 "Il solitario a due mazzi: dieci colonne, otto scale dal re "
                 "all'asso da completare.");
    ex_dlg_avviso("Informazioni su", t);
}

static void istruzioni(void)
{
    ex_dlg_testo("Istruzioni di EXSpider",
        "LO SCOPO\n"
        "Formare otto scale complete, dal re all'asso dello stesso seme. Una scala "
        "completa lascia il tavolo da sola e va in basso a sinistra.\n"
        "\n"
        "LE MOSSE\n"
        "Una carta va sopra una di valore piu' alto di uno, di qualunque seme. Una "
        "fila di carte si sposta tutta insieme solo se e' dello stesso seme e in "
        "ordine. In una colonna vuota va qualunque carta o fila. Quando una carta "
        "coperta resta in cima, si gira da sola.\n"
        "\n"
        "IL MAZZO\n"
        "Un clic sul mazzo in basso a destra distribuisce una carta su ogni "
        "colonna. Si puo' solo quando nessuna colonna e' vuota.\n"
        "\n"
        "I SEMI\n"
        "Con un seme (picche) e' il gioco piu' facile, con quattro il piu' "
        "difficile: si sceglie da Partita, e si comincia una partita nuova.\n"
        "\n"
        "IL MOUSE\n"
        "Si trascina una carta (con quelle sopra) dove deve andare. Un clic la "
        "manda da sola nel posto migliore.\n"
        "\n"
        "I PUNTI\n"
        "Si parte da 500: ogni mossa toglie un punto, ogni scala completa ne "
        "aggiunge 100.\n"
        "\n"
        "I TASTI\n"
        "F2 o Ctrl+N: nuova partita.  Ctrl+Z: annulla.  H: suggerimento.  "
        "D: distribuisci dal mazzo.  Ctrl+Q: esci.");
}

static void esci(void) { statistiche_salva(); ex_quit(0); }

static int tasto(unsigned int wp)
{
    unsigned int c = wp & KBD_KEY_MASK;

    if (wp & KBD_MOD_CTRL) {
        switch (c | 32) {
        case 'n': nuova(0); return 1;
        case 'z': annulla(); return 1;
        case 'q': esci(); return 1;
        }
        return 0;
    }
    if (c == KBD_K_F(2)) { nuova(0); return 1; }
    if ((c | 32) == 'h') { suggerisci(); return 1; }
    if ((c | 32) == 'd') { distribuisci_fila(); return 1; }
    return 0;
}

static long proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CLOSE:
        esci();
        return 0;

    case EXM_PAINT:
        ex_default_proc(f, msg, wp, lp);
        if (g_tira) disegna_tavolo(g_da, g_da_i);
        else        disegna_tavolo(-1, 0);
        tela_mostra(f, &g_t, 0, 0, TAV_W, TAV_H);
        return 0;

    case EXM_TIMER:
        gioco_tempo(f);
        if (g_inizio_ms && !g_finita) {
            unsigned int s = (uptime_ms() - g_inizio_ms) / 1000u;
            if (s != g_secondi && !g_tira) {
                g_secondi = s;
                disegna_stato();
                tela_mostra(f, &g_t, 0, TAV_H - STATO_H, TAV_W, STATO_H);
                gioco_annuncia(f, 0);
            }
        }
        return EX_NO_REDRAW;

    case EXM_MOUSE_DOWN:
        mouse_giu(EX_X(lp), EX_Y(lp), 0);
        return EX_NO_REDRAW;
    case EXM_DOUBLE_CLICK:
        mouse_giu(EX_X(lp), EX_Y(lp), 1);
        return EX_NO_REDRAW;

    case EXM_MOUSE_MOVE: {
        ExMsg m2;
        int   x = EX_X(lp), y = EX_Y(lp), altro = 0;

        while (ex_peek_message(&m2)) {
            if (m2.msg == EXM_MOUSE_MOVE && m2.window == f) { x = EX_X(m2.lp); y = EX_Y(m2.lp); continue; }
            altro = 1;
            break;
        }
        mouse_mosso(x, y);
        if (altro) ex_dispatch(&m2);
        return EX_NO_REDRAW;
    }

    case EXM_MOUSE_UP:
        mouse_su(EX_X(lp), EX_Y(lp));
        return EX_NO_REDRAW;

    case EXM_COMMAND:
        switch (wp) {
        case ID_NUOVA:      nuova(0); break;
        case ID_RICOMINCIA: g_partite--; nuova(1); break;
        case ID_ANNULLA:    annulla(); break;
        case ID_SUGGERISCI: suggerisci(); break;
        case ID_SEMI1:      scegli_semi(1); break;
        case ID_SEMI2:      scegli_semi(2); break;
        case ID_SEMI4:      scegli_semi(4); break;
        case ID_STAT:       statistiche(); ridisegna(); break;
        case ID_ESCI:       esci(); break;
        case ID_ISTR:       istruzioni(); ridisegna(); break;
        case ID_INFO:       informazioni(); ridisegna(); break;
        }
        return 0;

    case EXM_KEY:
        if (tasto(wp)) return EX_NO_REDRAW;
        break;
    }
    return ex_default_proc(f, msg, wp, lp);
}

int main(int argc, char **argv)
{
    ExMsg m;

    (void)argc; (void)argv;
    caso_semina();
    g_semi    = gioco_cfg_intero(NOME_CFG, "semi", 1);
    if (g_semi != 2 && g_semi != 4) g_semi = 1;
    g_partite = gioco_cfg_intero(NOME_CFG, "partite", 0);
    g_vinte   = gioco_cfg_intero(NOME_CFG, "vinte", 0);
    g_record  = gioco_cfg_intero(NOME_CFG, "record", 0);

    if (!tela_crea(&g_t, TAV_W, TAV_H) || !tela_crea(&g_base, TAV_W, TAV_H)) {
        printf("exspider: memoria insufficiente.\n");
        return 1;
    }
    g_storia = (Stato *)malloc(sizeof(Stato) * STORIA_MAX);
    carte_misura(CW, CH);
    g_font = ex_font_find(EX_FAMILY_SANS, 13, 0, 0);

    g_f = ex_create("window", "EXSpider", EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                    EX_AUTO, EX_AUTO, TAV_W, GIOCO_MENU_H + TAV_H, 0, 0, proc);
    if (!g_f) {
        printf("exspider: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }

    g_menu = ex_menu_bar(g_f);
    ex_menu_add_item(g_menu, "Partita", "Nuova\tF2", ID_NUOVA);
    ex_menu_add_item(g_menu, "Partita", "Ricomincia la stessa", ID_RICOMINCIA);
    ex_menu_add_item(g_menu, "Partita", "Annulla la mossa\tCtrl+Z", ID_ANNULLA);
    ex_menu_add_item(g_menu, "Partita", "Suggerimento\tH", ID_SUGGERISCI);
    ex_menu_add_item(g_menu, "Partita", "-", 0);
    ex_menu_add_item(g_menu, "Partita", "Un seme (facile)", ID_SEMI1);
    ex_menu_add_item(g_menu, "Partita", "Due semi (medio)", ID_SEMI2);
    ex_menu_add_item(g_menu, "Partita", "Quattro semi (difficile)", ID_SEMI4);
    ex_menu_add_item(g_menu, "Partita", "-", 0);
    ex_menu_add_item(g_menu, "Partita", "Statistiche", ID_STAT);
    ex_menu_add_item(g_menu, "Partita", "Esci\tCtrl+Q", ID_ESCI);
    ex_menu_add_item(g_menu, "Info", "Istruzioni", ID_ISTR);
    ex_menu_add_item(g_menu, "Info", "Informazioni su", ID_INFO);
    ex_menu_check(g_menu, ID_SEMI1, g_semi == 1);
    ex_menu_check(g_menu, ID_SEMI2, g_semi == 2);
    ex_menu_check(g_menu, ID_SEMI4, g_semi == 4);

    /* ! THE MENU BAR AND THE BUTTONS ARE CONTROLS: the base draws them on
     * EXM_PAINT, and no EXM_PAINT comes by itself for a window just made. */
    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    ex_set_timer(g_f, 200);
    nuova(0);

    while (ex_get_message(&m)) ex_dispatch(&m);
    return 0;
}
