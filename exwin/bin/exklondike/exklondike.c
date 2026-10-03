/* =============================================================================
 * exwin/bin/exklondike/exklondike.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * EXKlondike, the classic solitaire (@GIOCHI, asked on 3 October 2026)
 *
 *     /exwin/bin/exklondike
 *
 * Seven columns, the stock, the waste and four foundations from ace to king.
 * Cards are dragged with the mouse; a click on a card sends it where it can
 * go, the foundations first. Drawing one card or three
 * is a choice in the Partita menu, kept in $HOME/.exwin/config/exklondike.cfg
 * with the statistics.
 *
 * ! WHEN ONLY FACE-UP CARDS ARE LEFT THE GAME IS WON, and the program says
 * so and finishes it: moving forty cards one by one to the foundations is not
 * a game any more.
 *
 * Drawing: lib/exgioco (the table is a picture laid with ex_pixmap; a dragged
 * card is redrawn over a copy of the table without it).
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"
#include "kbd_proto.h"
#include "exgioco.h"

#define VERSIONE_APP "0.001"
EX_VERSIONE("exklondike", VERSIONE_APP);

#define NOME_CFG  "exklondike"

#define CW        71
#define CH        96
#define GAP       12
#define TAV_W     (7 * CW + 8 * GAP)
#define TAV_H     480
#define STATO_H   20
#define RIGA_Y    10
#define COL_Y     (RIGA_Y + CH + 16)
#define PASSO_GIU 6                 /* offset of a face-down card in a column */
#define PASSO_SU  20                /* offset of a face-up card */
#define VENTAGLIO 16                /* the three cards of the waste */
#define SOGLIA    4                 /* pixels before a press becomes a drag */

enum { MAZZO = 0, SCARTI = 1, BASE0 = 2, COL0 = 6, N_PILE = 13 };

enum {
    ID_NUOVA = 1, ID_RICOMINCIA, ID_ANNULLA, ID_PESCA1, ID_PESCA3, ID_STAT,
    ID_ESCI, ID_ISTR, ID_INFO
};

typedef struct {
    signed char   c[N_PILE][52];
    unsigned char n[N_PILE];
    unsigned char coperte[N_PILE];  /* face-down cards at the bottom of a column */
    unsigned int  mosse;
} Stato;

static ExWindow g_f, g_menu;
static Tela     g_t, g_base;
static ExFont   g_font;
static Stato    g_s;
static signed char g_mazzo_iniziale[52];

#define STORIA_MAX 400
static Stato   *g_storia;
static int      g_nstoria;

static int      g_pesca = 1;        /* 1 or 3 */
static int      g_partite, g_vinte;
static unsigned int g_inizio_ms;    /* 0 = the clock has not started */
static unsigned int g_secondi;
static int      g_finita;

/* The press and the drag. */
static int g_premuto, g_tira;
static int g_da, g_da_i;            /* pile and index of the first card taken */
static int g_px, g_py;              /* where the button went down */
static int g_ox, g_oy;              /* the mouse inside the first card */
static int g_tx, g_ty;              /* the first dragged card, now */
static int g_rx0, g_ry0, g_rx1, g_ry1;  /* what the drag last covered */

/* --- Places on the table --------------------------------------------------- */
static int pila_x(int p)
{
    if (p == MAZZO)  return GAP;
    if (p == SCARTI) return GAP + (CW + GAP);
    if (p < COL0)    return GAP + (3 + p - BASE0) * (CW + GAP);
    return GAP + (p - COL0) * (CW + GAP);
}

static int pila_y(int p) { return p < COL0 ? RIGA_Y : COL_Y; }

/* The offset of the face-up cards of a column: smaller when it is long, so the
 * column never goes past the status bar. */
static int passo_su(int p)
{
    int giu = g_s.coperte[p], su = g_s.n[p] - giu, spazio, passo = PASSO_SU;

    if (su <= 1) return PASSO_SU;
    spazio = TAV_H - STATO_H - 6 - COL_Y - giu * PASSO_GIU - CH;
    if ((su - 1) * passo > spazio) passo = spazio / (su - 1);
    if (passo < 8) passo = 8;
    return passo;
}

static int carta_y(int p, int i)
{
    int giu = g_s.coperte[p];

    if (p < COL0) return RIGA_Y;
    if (i < giu) return COL_Y + i * PASSO_GIU;
    return COL_Y + giu * PASSO_GIU + (i - giu) * passo_su(p);
}

/* The waste shows up to three cards fanned (with Pesca 3): where card i is. */
static int scarti_x(int i)
{
    int n = g_s.n[SCARTI], primo = n - (g_pesca == 3 ? 3 : 1);

    if (primo < 0) primo = 0;
    if (i < primo) return pila_x(SCARTI);
    return pila_x(SCARTI) + (i - primo) * VENTAGLIO;
}

/* --- Rules ----------------------------------------------------------------- */
static int puo_andare(int carta, int dest, int quante)
{
    int n = g_s.n[dest], cima = n ? g_s.c[dest][n - 1] : -1;

    if (dest >= BASE0 && dest < COL0) {
        if (quante != 1) return 0;
        if (!n) return VALORE(carta) == 0;
        return SEME(cima) == SEME(carta) && VALORE(cima) + 1 == VALORE(carta);
    }
    if (dest >= COL0) {
        if (!n) return VALORE(carta) == 12;
        return ROSSA(cima) != ROSSA(carta) && VALORE(cima) == VALORE(carta) + 1;
    }
    return 0;
}

static int si_prende(int p, int i)
{
    if (i < 0 || i >= g_s.n[p]) return 0;
    if (p == MAZZO) return 0;
    if (p == SCARTI || p < COL0) return i == g_s.n[p] - 1;
    return i >= g_s.coperte[p];
}

/* --- History ---------------------------------------------------------------- */
static void ricorda(void)
{
    if (!g_storia) return;
    if (g_nstoria == STORIA_MAX) {
        memmove(g_storia, g_storia + 1, sizeof(Stato) * (STORIA_MAX - 1));
        g_nstoria--;
    }
    g_storia[g_nstoria++] = g_s;
}

static void avvia_orologio(void)
{
    if (!g_inizio_ms) g_inizio_ms = uptime_ms() - g_secondi * 1000u;
}

/* --- Drawing --------------------------------------------------------------- */
static void disegna_stato(void)
{
    char t[160], tempo[16];
    int  y = TAV_H - STATO_H;

    tela_sfuma(&g_t, 0, y, TAV_W, STATO_H, 0x1B5E31, 0x134A26);
    gioco_tempo_testo(tempo, g_secondi);
    snprintf(t, sizeof(t), "Mosse: %u    Tempo: %s    Pesca %d",
             g_s.mosse, tempo, g_pesca);
    tela_testo(&g_t, g_font, 8, y + 3, t, 0xE6F2E8);
    snprintf(t, sizeof(t), "Vinte %d su %d", g_vinte, g_partite);
    tela_testo(&g_t, g_font, TAV_W - 8 - ex_text_width(g_font, t), y + 3, t, 0xE6F2E8);
}

/* The whole table, without the cards being dragged (from pile `via`, index
 * `via_i` up; via < 0 hides nothing). */
static void disegna_tavolo(int via, int via_i)
{
    static const char *SEGNI_BASE = "A";
    int p, i;

    tela_sfuma(&g_t, 0, 0, TAV_W, TAV_H - STATO_H, 0x2E8B4A, 0x1D6B36);

    /* The stock: the back, or an empty place with a circle to start over. */
    if (g_s.n[MAZZO] && !(via == MAZZO)) carta_disegna(&g_t, pila_x(MAZZO), RIGA_Y, CARTA_DORSO);
    else carta_posto(&g_t, pila_x(MAZZO), RIGA_Y, g_s.n[SCARTI] ? "O" : "");

    /* The waste. */
    if (!g_s.n[SCARTI]) carta_posto(&g_t, pila_x(SCARTI), RIGA_Y, "");
    {
        int n = g_s.n[SCARTI], primo = n - (g_pesca == 3 ? 3 : 1);
        if (primo < 0) primo = 0;
        if (primo > 0 && !(via == SCARTI && via_i == primo - 1))
            carta_disegna(&g_t, pila_x(SCARTI), RIGA_Y, g_s.c[SCARTI][primo - 1]);
        for (i = primo; i < n; i++) {
            if (via == SCARTI && i >= via_i) break;
            carta_disegna(&g_t, scarti_x(i), RIGA_Y, g_s.c[SCARTI][i]);
        }
    }

    for (p = BASE0; p < COL0; p++) {
        int n = g_s.n[p];
        if (via == p) n = via_i;
        if (n) carta_disegna(&g_t, pila_x(p), RIGA_Y, g_s.c[p][n - 1]);
        else   carta_posto(&g_t, pila_x(p), RIGA_Y, SEGNI_BASE);
    }

    for (p = COL0; p < N_PILE; p++) {
        int n = g_s.n[p];
        if (via == p) n = via_i;
        if (!g_s.n[p]) carta_posto(&g_t, pila_x(p), COL_Y, "K");
        for (i = 0; i < n; i++)
            carta_disegna(&g_t, pila_x(p), carta_y(p, i),
                          i < g_s.coperte[p] ? CARTA_DORSO : g_s.c[p][i]);
    }
    disegna_stato();
}

static void ridisegna(void)
{
    disegna_tavolo(-1, 0);
    tela_mostra(g_f, &g_t, 0, 0, TAV_W, TAV_H);
    gioco_annuncia(g_f, 1);
}

/* --- Moves ------------------------------------------------------------------ */
static void scopri(int p)
{
    if (p >= COL0 && g_s.n[p] && g_s.coperte[p] >= g_s.n[p]) g_s.coperte[p] = (unsigned char)(g_s.n[p] - 1);
}

static void sposta(int da, int i, int a)
{
    int k, quante = g_s.n[da] - i;

    for (k = 0; k < quante; k++) g_s.c[a][g_s.n[a] + k] = g_s.c[da][i + k];
    g_s.n[a] = (unsigned char)(g_s.n[a] + quante);
    g_s.n[da] = (unsigned char)i;
    scopri(da);
    g_s.mosse++;
    avvia_orologio();
}

static void pesca(void)
{
    int k;

    ricorda();
    if (!g_s.n[MAZZO]) {
        if (!g_s.n[SCARTI]) { g_nstoria--; return; }
        for (k = 0; k < g_s.n[SCARTI]; k++)
            g_s.c[MAZZO][k] = g_s.c[SCARTI][g_s.n[SCARTI] - 1 - k];
        g_s.n[MAZZO] = g_s.n[SCARTI];
        g_s.n[SCARTI] = 0;
    } else {
        for (k = 0; k < g_pesca && g_s.n[MAZZO]; k++)
            g_s.c[SCARTI][g_s.n[SCARTI]++] = g_s.c[MAZZO][--g_s.n[MAZZO]];
    }
    g_s.mosse++;
    avvia_orologio();
}

static int tutte_scoperte(void)
{
    int p;

    if (g_s.n[MAZZO] || g_s.n[SCARTI]) return 0;
    for (p = COL0; p < N_PILE; p++) if (g_s.coperte[p]) return 0;
    return 1;
}

static int vinta(void)
{
    int p;
    for (p = BASE0; p < COL0; p++) if (g_s.n[p] != 13) return 0;
    return 1;
}

static void statistiche_salva(void)
{
    char t[200];

    snprintf(t, sizeof(t),
             "# EXKlondike: le scelte e le partite. Le riscrive il programma.\n"
             "pesca   = %d\npartite = %d\nvinte   = %d\n", g_pesca, g_partite, g_vinte);
    gioco_cfg_scrivi(NOME_CFG, t);
}

static void nuova(int stessa);

static void controlla_fine(void)
{
    char t[200], tempo[16];

    if (g_finita) return;
    if (tutte_scoperte() && !vinta()) {
        /* Finish it: each card that can go to a foundation goes, until none. */
        int mosso = 1;
        while (mosso) {
            int p;
            mosso = 0;
            for (p = COL0; p < N_PILE; p++) {
                int b, n = g_s.n[p];
                if (!n) continue;
                for (b = BASE0; b < COL0; b++)
                    if (puo_andare(g_s.c[p][n - 1], b, 1)) { sposta(p, n - 1, b); mosso = 1; break; }
            }
        }
    }
    if (!vinta()) return;

    g_finita = 1;
    g_vinte++;
    statistiche_salva();
    ridisegna();
    gioco_tempo_testo(tempo, g_secondi);
    snprintf(t, sizeof(t), "Hai vinto in %u mosse e %s.\n\nUn'altra partita?",
             g_s.mosse, tempo);
    if (ex_dlg_conferma("EXKlondike", t, "Nuova partita", "Basta cosi'")) nuova(0);
    else ridisegna();
}

/* A click on a card: to a foundation if it can, otherwise to the first column
 * that takes it. Returns 1 if it moved. */
static int manda(int p, int i)
{
    int a, carta = g_s.c[p][i], quante = g_s.n[p] - i;

    if (quante == 1)
        for (a = BASE0; a < COL0; a++)
            if (a != p && puo_andare(carta, a, 1)) { ricorda(); sposta(p, i, a); return 1; }
    if (p >= COL0 && i == g_s.coperte[p] && VALORE(carta) == 12 && i == 0) return 0;
    for (a = COL0; a < N_PILE; a++)
        if (a != p && puo_andare(carta, a, quante)) { ricorda(); sposta(p, i, a); return 1; }
    return 0;
}

/* --- Hit testing ------------------------------------------------------------ */
static int dentro(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static int trova(int x, int y, int *pila, int *ind)
{
    int p, i;

    if (dentro(x, y, pila_x(MAZZO), RIGA_Y, CW, CH)) { *pila = MAZZO; *ind = g_s.n[MAZZO] - 1; return 1; }
    if (g_s.n[SCARTI]) {
        int n = g_s.n[SCARTI] - 1;
        if (dentro(x, y, scarti_x(n), RIGA_Y, CW, CH)) { *pila = SCARTI; *ind = n; return 1; }
    }
    for (p = BASE0; p < COL0; p++)
        if (dentro(x, y, pila_x(p), RIGA_Y, CW, CH)) { *pila = p; *ind = g_s.n[p] - 1; return 1; }
    for (p = COL0; p < N_PILE; p++) {
        if (x < pila_x(p) || x >= pila_x(p) + CW) continue;
        for (i = g_s.n[p] - 1; i >= 0; i--)
            if (dentro(x, y, pila_x(p), carta_y(p, i), CW, CH)) { *pila = p; *ind = i; return 1; }
        if (!g_s.n[p] && dentro(x, y, pila_x(p), COL_Y, CW, CH)) { *pila = p; *ind = -1; return 1; }
    }
    return 0;
}

/* Where a dragged card is dropped: the pile its middle is over, or the one
 * under the mouse. */
static int destinazione(int mx, int my)
{
    int p, cx = g_tx + CW / 2, cy = g_ty + CH / 3;

    for (p = BASE0; p < N_PILE; p++) {
        int x = pila_x(p), y = pila_y(p), h = CH;
        if (p >= COL0 && g_s.n[p]) h = carta_y(p, g_s.n[p] - 1) - y + CH;
        if (dentro(cx, cy, x, y, CW, h) || dentro(mx, my, x, y, CW, h)) return p;
    }
    return -1;
}

/* --- The drag -------------------------------------------------------------- */
static void tira_disegna(void)
{
    int k, n = g_s.n[g_da] - g_da_i, passo = g_da >= COL0 ? passo_su(g_da) : 0;
    int x0 = g_tx, y0 = g_ty, x1 = g_tx + CW, y1 = g_ty + CH + (n - 1) * passo;
    int ux0, uy0, ux1, uy1;

    /* What was under the cards comes back from the copy, then the cards. */
    tela_copia(&g_t, g_rx0, g_ry0, &g_base, g_rx0, g_ry0, g_rx1 - g_rx0, g_ry1 - g_ry0, 0);
    for (k = 0; k < n; k++) carta_disegna(&g_t, g_tx, g_ty + k * passo, g_s.c[g_da][g_da_i + k]);

    ux0 = x0 < g_rx0 ? x0 : g_rx0;  uy0 = y0 < g_ry0 ? y0 : g_ry0;
    ux1 = x1 > g_rx1 ? x1 : g_rx1;  uy1 = y1 > g_ry1 ? y1 : g_ry1;
    tela_mostra(g_f, &g_t, ux0, uy0, ux1 - ux0, uy1 - uy0);
    gioco_annuncia(g_f, 0);
    g_rx0 = x0; g_ry0 = y0; g_rx1 = x1; g_ry1 = y1;
}

static void tira_inizia(void)
{
    g_tira = 1;
    disegna_tavolo(g_da, g_da_i);
    tela_copia(&g_base, 0, 0, &g_t, 0, 0, TAV_W, TAV_H, 0);
    g_rx0 = g_rx1 = g_tx;
    g_ry0 = g_ry1 = g_ty;
    tira_disegna();
}

static void mouse_giu(int x, int y, int doppio)
{
    int p, i;

    y -= GIOCO_MENU_H;
    if (g_finita || !trova(x, y, &p, &i)) return;
    if (p == MAZZO) { pesca(); ridisegna(); return; }
    /* ! THE FIRST CLICK OF A DOUBLE CLICK HAS ALREADY SENT THE CARD: the second
     * one would send the card that was under it, which nobody asked for. */
    if (doppio || !si_prende(p, i)) return;
    g_premuto = 1;
    g_tira = 0;
    g_da = p;
    g_da_i = i;
    g_px = x; g_py = y;
    g_tx = p == SCARTI ? scarti_x(i) : pila_x(p);
    g_ty = carta_y(p, i);
    g_ox = x - g_tx;
    g_oy = y - g_ty;
}

static void mouse_mosso(int x, int y)
{
    y -= GIOCO_MENU_H;
    if (!g_premuto) return;
    if (!g_tira) {
        if ((x - g_px) * (x - g_px) + (y - g_py) * (y - g_py) < SOGLIA * SOGLIA) return;
        tira_inizia();
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
    if (!g_tira) {                          /* a click, not a drag */
        if (manda(g_da, g_da_i)) { ridisegna(); controlla_fine(); }
        return;
    }
    g_tira = 0;
    dest = destinazione(x, y);
    if (dest >= 0 && dest != g_da && puo_andare(g_s.c[g_da][g_da_i], dest, g_s.n[g_da] - g_da_i)) {
        ricorda();
        sposta(g_da, g_da_i, dest);
    }
    ridisegna();
    controlla_fine();
}

/* --- Games ------------------------------------------------------------------ */
static void distribuisci(void)
{
    int p, k = 0, i;

    memset(&g_s, 0, sizeof(g_s));
    for (p = 0; p < 7; p++) {
        for (i = 0; i <= p; i++) g_s.c[COL0 + p][i] = g_mazzo_iniziale[k++];
        g_s.n[COL0 + p] = (unsigned char)(p + 1);
        g_s.coperte[COL0 + p] = (unsigned char)p;
    }
    for (i = 0; k < 52; i++) g_s.c[MAZZO][i] = g_mazzo_iniziale[k++];
    g_s.n[MAZZO] = (unsigned char)i;
}

static void nuova(int stessa)
{
    int i;

    if (!stessa) {
        for (i = 0; i < 52; i++) g_mazzo_iniziale[i] = (signed char)i;
        for (i = 51; i > 0; i--) {
            int j = (int)caso_fino((unsigned int)i + 1);
            signed char x = g_mazzo_iniziale[i];
            g_mazzo_iniziale[i] = g_mazzo_iniziale[j];
            g_mazzo_iniziale[j] = x;
        }
    }
    distribuisci();
    g_nstoria = 0;
    g_inizio_ms = 0;
    g_secondi = 0;
    g_finita = 0;
    g_premuto = g_tira = 0;
    g_partite++;
    statistiche_salva();
    ridisegna();
}

static void annulla(void)
{
    if (!g_nstoria || g_finita) return;
    g_s = g_storia[--g_nstoria];
    g_s.mosse++;
    ridisegna();
}

static void scegli_pesca(int n)
{
    g_pesca = n;
    ex_menu_check(g_menu, ID_PESCA1, n == 1);
    ex_menu_check(g_menu, ID_PESCA3, n == 3);
    statistiche_salva();
    ridisegna();
}

static void statistiche(void)
{
    char t[200];

    snprintf(t, sizeof(t), "Partite giocate: %d\nPartite vinte: %d\nPercentuale: %d%%",
             g_partite, g_vinte, g_partite ? g_vinte * 100 / g_partite : 0);
    ex_dlg_avviso("Statistiche", t);
}

static void informazioni(void)
{
    char t[640];

    exinfo_testo(t, sizeof(t), "EXKlondike", VERSIONE_APP,
                 "Il solitario classico: sette colonne, il mazzo e quattro basi "
                 "dall'asso al re.");
    ex_dlg_avviso("Informazioni su", t);
}

static void istruzioni(void)
{
    ex_dlg_testo("Istruzioni di EXKlondike",
        "LO SCOPO\n"
        "Portare tutte le carte sulle quattro basi in alto a destra, una per seme, "
        "dall'asso al re.\n"
        "\n"
        "LE COLONNE\n"
        "Su una colonna una carta va sopra una di valore piu' alto di uno e di "
        "colore diverso: un 6 rosso su un 7 nero. Si sposta anche una fila intera "
        "di carte scoperte. Una colonna vuota accetta solo un re. Quando una carta "
        "coperta resta in cima, si gira da sola.\n"
        "\n"
        "IL MAZZO\n"
        "Un clic sul mazzo gira una carta (o tre, con Partita > Pesca tre carte) "
        "sugli scarti. Quando e' finito, un clic lo rimette insieme.\n"
        "\n"
        "IL MOUSE\n"
        "Si trascina una carta (con quelle sopra) dove deve andare. Un "
        "clic la manda da sola: prima su una base, se puo', altrimenti sulla prima "
        "colonna che la prende.\n"
        "\n"
        "I TASTI\n"
        "F2 o Ctrl+N: nuova partita.  Ctrl+Z: annulla l'ultima mossa.  "
        "Spazio: gira il mazzo.  Ctrl+Q: esci.\n"
        "\n"
        "Quando restano solo carte scoperte la partita e' vinta, e il gioco la "
        "finisce da se'.");
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
    if (c == ' ' && !g_finita) { pesca(); ridisegna(); return 1; }
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

        /* The moves already queued are taken now: only the last one counts. */
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
        case ID_PESCA1:     scegli_pesca(1); break;
        case ID_PESCA3:     scegli_pesca(3); break;
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
    g_pesca   = gioco_cfg_intero(NOME_CFG, "pesca", 1) == 3 ? 3 : 1;
    g_partite = gioco_cfg_intero(NOME_CFG, "partite", 0);
    g_vinte   = gioco_cfg_intero(NOME_CFG, "vinte", 0);

    if (!tela_crea(&g_t, TAV_W, TAV_H) || !tela_crea(&g_base, TAV_W, TAV_H)) {
        printf("exklondike: memoria insufficiente.\n");
        return 1;
    }
    g_storia = (Stato *)malloc(sizeof(Stato) * STORIA_MAX);
    carte_misura(CW, CH);
    g_font = ex_font_find(EX_FAMILY_SANS, 13, 0, 0);

    g_f = ex_create("window", "EXKlondike", EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                    EX_AUTO, EX_AUTO, TAV_W, GIOCO_MENU_H + TAV_H, 0, 0, proc);
    if (!g_f) {
        printf("exklondike: il server a finestre non risponde.\n");
        printf("            Avvialo con:  exwin\n");
        return 1;
    }

    g_menu = ex_menu_bar(g_f);
    ex_menu_add_item(g_menu, "Partita", "Nuova\tF2", ID_NUOVA);
    ex_menu_add_item(g_menu, "Partita", "Ricomincia la stessa", ID_RICOMINCIA);
    ex_menu_add_item(g_menu, "Partita", "Annulla la mossa\tCtrl+Z", ID_ANNULLA);
    ex_menu_add_item(g_menu, "Partita", "-", 0);
    ex_menu_add_item(g_menu, "Partita", "Pesca una carta", ID_PESCA1);
    ex_menu_add_item(g_menu, "Partita", "Pesca tre carte", ID_PESCA3);
    ex_menu_add_item(g_menu, "Partita", "-", 0);
    ex_menu_add_item(g_menu, "Partita", "Statistiche", ID_STAT);
    ex_menu_add_item(g_menu, "Partita", "Esci\tCtrl+Q", ID_ESCI);
    ex_menu_add_item(g_menu, "Info", "Istruzioni", ID_ISTR);
    ex_menu_add_item(g_menu, "Info", "Informazioni su", ID_INFO);
    ex_menu_check(g_menu, ID_PESCA1, g_pesca == 1);
    ex_menu_check(g_menu, ID_PESCA3, g_pesca == 3);

    /* ! THE MENU BAR AND THE BUTTONS ARE CONTROLS: the base draws them on
     * EXM_PAINT, and no EXM_PAINT comes by itself for a window just made. */
    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    ex_set_timer(g_f, 200);
    nuova(0);

    while (ex_get_message(&m)) ex_dispatch(&m);
    return 0;
}
