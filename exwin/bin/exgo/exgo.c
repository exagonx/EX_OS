/* =============================================================================
 * exwin/bin/exgo/exgo.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * EXGO, the game of go (@GIOCHI, asked on 3 October 2026)
 *
 *     /exwin/bin/exgo
 *
 * On a board of 9, 13 or 19 lines, against the computer (which plays white)
 * or between two people at the same keyboard. Captures, the ko rule and no
 * suicide. Two passes in a row end the game: the dead stones are marked with
 * a click and the score is counted by area, as in the Chinese rules — stones
 * on the board plus surrounded empty points — with 6.5 points of komi to
 * white.
 *
 * ! THE COMPUTER IS A BEGINNER, AND IT SAYS SO. A strong go program reads
 * thousands of positions per move; this one looks one move ahead: it takes
 * what can be taken, saves a group in atari, avoids putting itself in atari,
 * does not fill its own eyes and prefers the third and fourth lines at the
 * start. Enough to learn the rules against; not a teacher.
 *
 * The board and the stones are drawn into a picture (lib/exgioco); the panel
 * on the right is part of it, and the three buttons under it are controls of
 * the toolkit, outside the picture.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"
#include "kbd_proto.h"
#include "exgioco.h"

#define VERSIONE_APP "0.001"
EX_VERSIONE("exgo", VERSIONE_APP);

#define NOME_CFG "exgo"

#define TAV_W      500             /* the board */
#define TAV_H      500
#define PAN_W      170             /* the panel on its right */
#define PAN_H      330
#define CLIENT_W   (TAV_W + PAN_W)
#define MAXN       19
#define KOMI_X10   65
#define VUOTO      0
#define NERO       1
#define BIANCO     2

enum {
    ID_NUOVA = 1, ID_ANNULLA, ID_PASSA, ID_ABBANDONA, ID_N9, ID_N13, ID_N19,
    ID_COMPUTER, ID_DUE, ID_CONTA, ID_ESCI, ID_ISTR, ID_INFO,
    ID_B_PASSA = 100, ID_B_ANNULLA, ID_B_ABBANDONA
};

typedef struct {
    unsigned char b[MAXN * MAXN];
    short         ko;               /* the point the player to move may not take */
    unsigned char turno;
    short         prig[3];          /* stones captured BY each colour */
    unsigned char passaggi;         /* passes in a row */
    short         ultima;           /* last move, -1 a pass */
    short         mosse;
} Pos;

static ExWindow g_f, g_menu, g_bpassa, g_bannulla, g_babbandona;
static Tela     g_t, g_pan;
static ExFont   g_font, g_font_g, g_font_c;
static Pos      g_s;
static int      g_n = 9;
static int      g_computer = 1;     /* the computer plays white */
static int      g_pensa;            /* the computer has to move at the next tick */
static int      g_conta;            /* counting the score */
static int      g_finita;
static unsigned char g_morta[MAXN * MAXN];
static char     g_msg[96];
static int      g_vinte, g_perse;

#define STORIA_MAX 600
static Pos     *g_storia;
static int      g_nstoria;

/* --- The board -------------------------------------------------------------- */
static int vicini(int p, int *v)
{
    int x = p % g_n, y = p / g_n, n = 0;

    if (x > 0)       v[n++] = p - 1;
    if (x < g_n - 1) v[n++] = p + 1;
    if (y > 0)       v[n++] = p - g_n;
    if (y < g_n - 1) v[n++] = p + g_n;
    return n;
}

/* The group of the stone at p: its stones in `pietre` (if not 0), how many,
 * and its liberties in *libere. */
static int gruppo(const Pos *s, int p, short *pietre, int *libere)
{
    static unsigned short pila[MAXN * MAXN];
    static unsigned char  visto[MAXN * MAXN], vista_lib[MAXN * MAXN];
    int colore = s->b[p], n = 0, sp = 0, nl = 0, i;

    memset(visto, 0, (unsigned int)(g_n * g_n));
    memset(vista_lib, 0, (unsigned int)(g_n * g_n));
    pila[sp++] = (unsigned short)p;
    visto[p] = 1;
    while (sp) {
        int q = pila[--sp], v[4], k, nv = vicini(q, v);
        if (pietre) pietre[n] = (short)q;
        n++;
        for (k = 0; k < nv; k++) {
            int r = v[k];
            if (s->b[r] == VUOTO) {
                if (!vista_lib[r]) { vista_lib[r] = 1; nl++; }
            } else if (s->b[r] == colore && !visto[r]) {
                visto[r] = 1;
                pila[sp++] = (unsigned short)r;
            }
        }
    }
    (void)i;
    if (libere) *libere = nl;
    return n;
}

static int libere_di(const Pos *s, int p)
{
    int l;
    gruppo(s, p, 0, &l);
    return l;
}

/* Plays `colore` at p. Returns 0 if the move is not allowed (occupied, ko,
 * suicide), and then s is untouched. */
static int gioca(Pos *s, int p, int colore)
{
    static short pietre[MAXN * MAXN];
    int v[4], nv, k, prese = 0, ultima_presa = -1, l, nostre;
    int altro = 3 - colore;
    Pos prima;

    if (s->b[p] != VUOTO || p == s->ko) return 0;
    prima = *s;
    s->b[p] = (unsigned char)colore;
    nv = vicini(p, v);
    for (k = 0; k < nv; k++) {
        if (s->b[v[k]] != altro) continue;
        if (libere_di(s, v[k]) == 0) {
            int n = gruppo(s, v[k], pietre, 0), i;
            for (i = 0; i < n; i++) s->b[pietre[i]] = VUOTO;
            prese += n;
            ultima_presa = v[k];
        }
    }
    nostre = gruppo(s, p, 0, &l);
    if (l == 0) { *s = prima; return 0; }              /* suicide */

    s->ko = (short)((prese == 1 && nostre == 1 && l == 1) ? ultima_presa : -1);
    s->prig[colore] = (short)(s->prig[colore] + prese);
    s->turno = (unsigned char)altro;
    s->passaggi = 0;
    s->ultima = (short)p;
    s->mosse++;
    return 1;
}

static void passa_turno(Pos *s)
{
    s->ko = -1;
    s->turno = (unsigned char)(3 - s->turno);
    s->passaggi++;
    s->ultima = -1;
    s->mosse++;
}

/* --- The computer ----------------------------------------------------------- */
#define NON_SI -100000

static int valuta(const Pos *s, int p, int io, int pietre_sul)
{
    Pos t = *s;
    int altro = 3 - io, v[4], nv = vicini(p, v), k, sc, prese, l, dim, linea, x, y;
    int mie = 0, vicina = 0, dx, dy;

    /* ! NOT IN ONE'S OWN EYE: a point all of whose neighbours are ours is
     * an eye, and filling it is how a living group dies. Unless one of those
     * neighbours is in atari, and then it is a connection. */
    for (k = 0; k < nv; k++) if (s->b[v[k]] == io) mie++;
    if (mie == nv) {
        int salva = 0;
        for (k = 0; k < nv; k++) if (libere_di(s, v[k]) == 1) salva = 1;
        if (!salva) return -1000;
    }
    if (!gioca(&t, p, io)) return NON_SI;

    prese = t.prig[io] - s->prig[io];
    sc = prese * 60;
    dim = gruppo(&t, p, 0, &l);
    if (l == 1 && !prese) sc -= 150 + dim * 10;
    else if (l == 1)      sc -= 30;
    else if (l == 2)      sc -= 8;

    for (k = 0; k < nv; k++) {
        int q = v[k], lq;
        if (s->b[q] == io && libere_di(s, q) == 1 && l >= 2) {
            sc += 50 + 10 * gruppo(s, q, 0, 0);        /* saved from atari */
        }
        if (t.b[q] == altro) {
            int nq = gruppo(&t, q, 0, &lq);
            if (lq == 1) sc += 25 + 5 * nq;
            else if (lq == 2) sc += 6;
        }
    }

    x = p % g_n; y = p / g_n;
    linea = x;
    if (y < linea) linea = y;
    if (g_n - 1 - x < linea) linea = g_n - 1 - x;
    if (g_n - 1 - y < linea) linea = g_n - 1 - y;
    if (pietre_sul < g_n * g_n / 6) {
        if (linea == 2 || linea == 3) sc += 10;
        if (linea == 0) sc -= 25;
        if (linea == 1) sc -= 8;
    } else if (linea == 0) sc -= 10;

    for (dy = -2; dy <= 2 && !vicina; dy++)
        for (dx = -2; dx <= 2; dx++) {
            int xx = x + dx, yy = y + dy;
            if (xx < 0 || yy < 0 || xx >= g_n || yy >= g_n) continue;
            if (s->b[yy * g_n + xx] != VUOTO) { vicina = 1; break; }
        }
    if (vicina) sc += 5;
    else if (pietre_sul) sc -= 2;

    return sc + (int)caso_fino(8);
}

/* The computer's move: a point, or -1 to pass. */
static int scegli_mossa(const Pos *s)
{
    int p, migliore = -1, punti = NON_SI, pietre = 0;

    for (p = 0; p < g_n * g_n; p++) if (s->b[p] != VUOTO) pietre++;
    for (p = 0; p < g_n * g_n; p++) {
        int v;
        if (s->b[p] != VUOTO) continue;
        v = valuta(s, p, s->turno, pietre);
        if (v > punti) { punti = v; migliore = p; }
    }
    if (punti < -40) return -1;
    /* The other one has passed: only a move that gains something is worth it. */
    if (s->passaggi && punti < 15) return -1;
    return migliore;
}

/* --- The score -------------------------------------------------------------- */
/* Area score, dead stones taken off: black and white in tenths of a point
 * (komi included); `terr` gets the owner of each empty point (0 = nobody). */
static void conta(int *nero, int *bianco, unsigned char *terr)
{
    static unsigned short pila[MAXN * MAXN];
    unsigned char b[MAXN * MAXN], visto[MAXN * MAXN];
    int p, punti[3] = { 0, 0, 0 };

    for (p = 0; p < g_n * g_n; p++) {
        b[p] = g_morta[p] ? VUOTO : g_s.b[p];
        visto[p] = 0;
        terr[p] = 0;
        if (b[p]) punti[b[p]]++;
    }
    for (p = 0; p < g_n * g_n; p++) {
        int sp = 0, n = 0, bordo = 0, k, i;
        static short area[MAXN * MAXN];

        if (b[p] != VUOTO || visto[p]) continue;
        pila[sp++] = (unsigned short)p;
        visto[p] = 1;
        while (sp) {
            int q = pila[--sp], v[4], nv = vicini(q, v);
            area[n++] = (short)q;
            for (k = 0; k < nv; k++) {
                int r = v[k];
                if (b[r] == VUOTO) {
                    if (!visto[r]) { visto[r] = 1; pila[sp++] = (unsigned short)r; }
                } else bordo |= b[r];
            }
        }
        if (bordo == NERO || bordo == BIANCO) {
            punti[bordo] += n;
            for (i = 0; i < n; i++) terr[area[i]] = (unsigned char)bordo;
        }
    }
    *nero = punti[NERO] * 10;
    *bianco = punti[BIANCO] * 10 + KOMI_X10;
}

/* --- Drawing ---------------------------------------------------------------- */
static int passo(void) { return (TAV_W - 60) / (g_n - 1); }
static int origine(void) { return (TAV_W - passo() * (g_n - 1)) / 2; }
static int punto_x(int p) { return origine() + (p % g_n) * passo(); }
static int punto_y(int p) { return origine() + (p / g_n) * passo(); }

static const char LETTERE[] = "ABCDEFGHJKLMNOPQRST";

static int stella(int x, int y)
{
    int a = g_n == 9 ? 2 : 3, c = g_n / 2, b = g_n - 1 - a;

    if (g_n == 9) return (x == 4 && y == 4) || ((x == a || x == b) && (y == a || y == b));
    return (x == a || x == c || x == b) && (y == a || y == c || y == b) &&
           (g_n == 19 || ((x == c) == (y == c)));
}

static void disegna_tavola(void)
{
    int k, o = origine(), d = passo(), fine = o + d * (g_n - 1), r = d * 47 / 100, p;
    unsigned char terr[MAXN * MAXN];
    int nero, bianco;

    tela_sfuma(&g_t, 0, 0, TAV_W, TAV_H, 0xE6BC6C, 0xD2A04E);
    /* A little grain: thin lines a shade darker, always in the same places. */
    for (k = 3; k < TAV_H; k += 7 + (k * 7) % 5)
        tela_velo(&g_t, 0, k, TAV_W, 1, 0x8A5A20, 18);

    for (k = 0; k < g_n; k++) {
        char s[4];
        tela_linea(&g_t, o, o + k * d, fine, o + k * d, 0x2A1A08);
        tela_linea(&g_t, o + k * d, o, o + k * d, fine, 0x2A1A08);
        s[0] = LETTERE[k]; s[1] = '\0';
        tela_testo_c(&g_t, g_font_c, o + k * d, o - 24, s, 0x4A3010);
        tela_testo_c(&g_t, g_font_c, o + k * d, fine + 10, s, 0x4A3010);
        sprintf(s, "%d", g_n - k);
        tela_testo_c(&g_t, g_font_c, o - 18, o + k * d - ex_font_height(g_font_c) / 2, s, 0x4A3010);
        tela_testo_c(&g_t, g_font_c, fine + 18, o + k * d - ex_font_height(g_font_c) / 2, s, 0x4A3010);
    }
    tela_bordo(&g_t, o - 1, o - 1, fine - o + 3, fine - o + 3, 0x2A1A08);
    for (p = 0; p < g_n * g_n; p++)
        if (stella(p % g_n, p / g_n)) tela_disco(&g_t, punto_x(p), punto_y(p), d > 30 ? 4 : 3, 0x2A1A08, 0);

    if (g_conta) conta(&nero, &bianco, terr);

    for (p = 0; p < g_n * g_n; p++) {
        int x = punto_x(p), y = punto_y(p), c = g_s.b[p];

        if (c) {
            tela_disco(&g_t, x + 2, y + 2, r, 0x5A3A10, 0);       /* the shadow */
            tela_disco(&g_t, x, y, r, c == NERO ? 0x1E1E1E : 0xF2F2EE, 1);
            if (g_conta && g_morta[p]) {
                tela_velo(&g_t, x - r, y - r, 2 * r, 2 * r, 0xD2A04E, 120);
                tela_linea(&g_t, x - r / 2, y - r / 2, x + r / 2, y + r / 2, 0xC02020);
                tela_linea(&g_t, x - r / 2, y + r / 2, x + r / 2, y - r / 2, 0xC02020);
            }
        }
        if (g_conta && (terr[p] == NERO || terr[p] == BIANCO)) {
            int q = d / 5 + 2;
            tela_rett(&g_t, x - q / 2, y - q / 2, q, q, terr[p] == NERO ? 0x101010 : 0xFAFAFA);
        }
    }
    /* The last move: a ring in the other colour. */
    if (!g_conta && g_s.ultima >= 0 && g_s.b[g_s.ultima]) {
        int x = punto_x(g_s.ultima), y = punto_y(g_s.ultima);
        unsigned int c = g_s.b[g_s.ultima] == NERO ? 0xF0F0F0 : 0x202020;
        tela_disco(&g_t, x, y, r / 3 + 1, c, 0);
        tela_disco(&g_t, x, y, r / 3 - 1, g_s.b[g_s.ultima] == NERO ? 0x1E1E1E : 0xF2F2EE, 0);
    }
}

static void disegna_pannello(void)
{
    char t[96];
    int  y = 14;

    tela_sfuma(&g_pan, 0, 0, PAN_W, PAN_H, 0xD8D8D8, 0xC0C0C0);
    tela_rett(&g_pan, 0, 0, 1, PAN_H, 0x808080);

    tela_testo(&g_pan, g_font_g, 14, y, "EXGO", 0x2A1A08);
    y += 34;
    snprintf(t, sizeof(t), "Scacchiera %dx%d", g_n, g_n);
    tela_testo(&g_pan, g_font, 14, y, t, 0x202020); y += 18;
    tela_testo(&g_pan, g_font, 14, y, g_computer ? "Contro il computer" : "Due giocatori", 0x202020);
    y += 18;
    tela_testo(&g_pan, g_font, 14, y, "Komi al bianco: 6,5", 0x202020);
    y += 30;

    tela_disco(&g_pan, 24, y + 8, 8, 0x1E1E1E, 1);
    snprintf(t, sizeof(t), "Nero%s: %d prese", g_computer ? " (tu)" : "", g_s.prig[NERO]);
    tela_testo(&g_pan, g_font, 40, y + 1, t, 0x202020);
    y += 26;
    tela_disco(&g_pan, 24, y + 8, 8, 0xF2F2EE, 1);
    snprintf(t, sizeof(t), "Bianco%s: %d prese", g_computer ? " (PC)" : "", g_s.prig[BIANCO]);
    tela_testo(&g_pan, g_font, 40, y + 1, t, 0x202020);
    y += 36;

    if (g_conta) {
        unsigned char terr[MAXN * MAXN];
        int nero, bianco;

        conta(&nero, &bianco, terr);
        tela_testo(&g_pan, g_font, 14, y, "CONTEGGIO", 0x8A1A1A); y += 18;
        snprintf(t, sizeof(t), "Nero: %d", nero / 10);
        tela_testo(&g_pan, g_font, 14, y, t, 0x202020); y += 18;
        snprintf(t, sizeof(t), "Bianco: %d,%d", bianco / 10, bianco % 10);
        tela_testo(&g_pan, g_font, 14, y, t, 0x202020); y += 22;
        tela_testo(&g_pan, g_font, 14, y, "Clic sui gruppi morti,", 0x404040); y += 16;
        tela_testo(&g_pan, g_font, 14, y, "poi Conta i punti.", 0x404040); y += 16;
    } else if (!g_finita) {
        snprintf(t, sizeof(t), "Tocca al %s", g_s.turno == NERO ? "nero" : "bianco");
        tela_testo(&g_pan, g_font, 14, y, t, 0x202020); y += 18;
        if (g_pensa) { tela_testo(&g_pan, g_font, 14, y, "Il computer pensa...", 0x404040); y += 18; }
    }
    if (g_msg[0]) {
        /* The message may be long: two lines of what fits. */
        char a[48];
        unsigned int k = 0, n = (unsigned int)strlen(g_msg);
        while (k < n && k < sizeof(a) - 1) { a[k] = g_msg[k]; k++; }
        a[k] = '\0';
        if (ex_text_width(g_font, a) > PAN_W - 20) {
            char *spazio = 0, *c;
            for (c = a; *c; c++) {
                char salva = *c;
                *c = '\0';
                if (ex_text_width(g_font, a) > PAN_W - 20) { *c = salva; break; }
                *c = salva;
                if (*c == ' ') spazio = c;
            }
            if (spazio) {
                *spazio = '\0';
                tela_testo(&g_pan, g_font, 14, y, a, 0x8A1A1A); y += 16;
                tela_testo(&g_pan, g_font, 14, y, g_msg + (spazio - a) + 1, 0x8A1A1A);
            } else tela_testo(&g_pan, g_font, 14, y, a, 0x8A1A1A);
        } else tela_testo(&g_pan, g_font, 14, y, a, 0x8A1A1A);
    }
}

static void mostra(int forza)
{
    tela_mostra(g_f, &g_t, 0, 0, TAV_W, TAV_H);
    ex_pixmap(g_f, TAV_W, GIOCO_MENU_H, PAN_W, PAN_H, g_pan.px, PAN_W);
    gioco_annuncia(g_f, forza);
}

static void ridisegna(void)
{
    disegna_tavola();
    disegna_pannello();
    mostra(1);
}

/* --- Games ------------------------------------------------------------------ */
static void scelte_salva(void)
{
    char t[200];

    snprintf(t, sizeof(t),
             "# EXGO: le scelte e le partite. Le riscrive il programma.\n"
             "lato     = %d\ncomputer = %d\nvinte    = %d\nperse    = %d\n",
             g_n, g_computer, g_vinte, g_perse);
    gioco_cfg_scrivi(NOME_CFG, t);
}

static void bottoni(void)
{
    ex_enable(g_babbandona, !g_conta && !g_finita && !g_pensa);
    ex_set_text(g_bpassa, g_conta ? "Conta i punti" : "Passa");
    ex_enable(g_bpassa, !g_finita && !g_pensa);
    ex_enable(g_bannulla, g_nstoria > 0 && !g_pensa);
}

static void nuova(void)
{
    memset(&g_s, 0, sizeof(g_s));
    g_s.ko = -1;
    g_s.turno = NERO;
    g_s.ultima = -1;
    g_nstoria = 0;
    g_conta = g_finita = g_pensa = 0;
    g_msg[0] = '\0';
    bottoni();
    ridisegna();
}

static void ricorda(void)
{
    if (!g_storia) return;
    if (g_nstoria == STORIA_MAX) {
        memmove(g_storia, g_storia + 1, sizeof(Pos) * (STORIA_MAX - 1));
        g_nstoria--;
    }
    g_storia[g_nstoria++] = g_s;
}

static void fine_partita(int nero, int bianco, const char *come)
{
    char t[200];
    int  vince_nero = nero > bianco;

    g_finita = 1;
    g_conta = 0;
    if (come) {
        snprintf(t, sizeof(t), "%s", come);
    } else {
        int diff = vince_nero ? nero - bianco : bianco - nero;
        snprintf(t, sizeof(t), "Nero %d,%d - Bianco %d,%d.\n\nVince il %s di %d,%d punti.",
                 nero / 10, nero % 10, bianco / 10, bianco % 10,
                 vince_nero ? "nero" : "bianco", diff / 10, diff % 10);
        snprintf(g_msg, sizeof(g_msg), "Vince il %s di %d,%d", vince_nero ? "nero" : "bianco",
                 diff / 10, diff % 10);
    }
    if (g_computer) { if (vince_nero) g_vinte++; else g_perse++; scelte_salva(); }
    bottoni();
    ridisegna();
    ex_dlg_avviso("Fine della partita", t);
    ridisegna();
}

static void inizia_conteggio(void)
{
    int p;

    g_conta = 1;
    /* A first guess of the dead: groups with one liberty left. The player
     * corrects it with a click. */
    for (p = 0; p < g_n * g_n; p++)
        g_morta[p] = (unsigned char)(g_s.b[p] && libere_di(&g_s, p) == 1);
    snprintf(g_msg, sizeof(g_msg), "Due passaggi: la partita e' finita.");
    bottoni();
    ridisegna();
}

static void dopo_mossa(void)
{
    if (g_s.passaggi >= 2) { inizia_conteggio(); return; }
    if (g_computer && g_s.turno == BIANCO) g_pensa = 1;
    bottoni();
    ridisegna();
}

static void passa(void)
{
    if (g_finita || g_pensa) return;
    if (g_conta) {
        int nero, bianco;
        unsigned char terr[MAXN * MAXN];
        conta(&nero, &bianco, terr);
        fine_partita(nero, bianco, 0);
        return;
    }
    ricorda();
    snprintf(g_msg, sizeof(g_msg), "Il %s passa.", g_s.turno == NERO ? "nero" : "bianco");
    passa_turno(&g_s);
    dopo_mossa();
}

static void mossa_computer(void)
{
    int p = scegli_mossa(&g_s);

    g_pensa = 0;
    ricorda();
    if (p < 0 || !gioca(&g_s, p, BIANCO)) {
        passa_turno(&g_s);
        snprintf(g_msg, sizeof(g_msg), "Il computer passa.");
    } else {
        snprintf(g_msg, sizeof(g_msg), "Bianco in %c%d.", LETTERE[p % g_n], g_n - p / g_n);
    }
    dopo_mossa();
}

static void annulla(void)
{
    if (g_pensa || !g_nstoria) return;
    if (g_conta || g_finita) {
        /* Back to the board, before the last pass. */
        g_conta = g_finita = 0;
    }
    g_s = g_storia[--g_nstoria];
    /* Against the computer, back to a position where it is our turn. */
    if (g_computer && g_s.turno == BIANCO && g_nstoria) g_s = g_storia[--g_nstoria];
    g_msg[0] = '\0';
    bottoni();
    ridisegna();
}

static void abbandona(void)
{
    char t[120];

    if (g_finita || g_conta || g_pensa) return;
    snprintf(t, sizeof(t), "Il %s abbandona la partita?", g_s.turno == NERO ? "nero" : "bianco");
    if (!ex_dlg_conferma("Abbandona", t, "Abbandona", "Continua")) { ridisegna(); return; }
    snprintf(g_msg, sizeof(g_msg), "Il %s ha abbandonato.", g_s.turno == NERO ? "nero" : "bianco");
    snprintf(t, sizeof(t), "Il %s ha abbandonato: vince il %s.",
             g_s.turno == NERO ? "nero" : "bianco", g_s.turno == NERO ? "bianco" : "nero");
    fine_partita(g_s.turno == NERO ? 0 : 10, g_s.turno == NERO ? 10 : 0, t);
}

/* The intersection nearest to (x, y) of the board, or -1. */
static int punto_a(int x, int y)
{
    int d = passo(), o = origine(), cx, cy;

    cx = (x - o + d / 2) / d;
    cy = (y - o + d / 2) / d;
    if (x < o - d / 2 || y < o - d / 2 || cx < 0 || cy < 0 || cx >= g_n || cy >= g_n) return -1;
    return cy * g_n + cx;
}

static void clic(int x, int y)
{
    int p;

    y -= GIOCO_MENU_H;
    if (x >= TAV_W || g_finita || g_pensa) return;
    p = punto_a(x, y);
    if (p < 0) return;

    if (g_conta) {
        /* A click on a stone marks its whole group dead, or alive again. */
        static short pietre[MAXN * MAXN];
        int n, i, m;
        if (!g_s.b[p]) return;
        n = gruppo(&g_s, p, pietre, 0);
        m = !g_morta[p];
        for (i = 0; i < n; i++) g_morta[pietre[i]] = (unsigned char)m;
        ridisegna();
        return;
    }
    if (g_computer && g_s.turno == BIANCO) return;
    {
        Pos prova = g_s;
        if (!gioca(&prova, p, g_s.turno)) {
            snprintf(g_msg, sizeof(g_msg), g_s.b[p] ? "Il punto e' occupato." :
                     p == g_s.ko ? "Ko: qui non si puo' riprendere subito." :
                     "Suicidio: la pietra non avrebbe liberta'.");
            ridisegna();
            return;
        }
        ricorda();
        g_s = prova;
    }
    g_msg[0] = '\0';
    dopo_mossa();
}

static void scegli_lato(int n)
{
    g_n = n;
    ex_menu_check(g_menu, ID_N9, n == 9);
    ex_menu_check(g_menu, ID_N13, n == 13);
    ex_menu_check(g_menu, ID_N19, n == 19);
    scelte_salva();
    nuova();
}

static void scegli_avversario(int computer)
{
    g_computer = computer;
    ex_menu_check(g_menu, ID_COMPUTER, computer);
    ex_menu_check(g_menu, ID_DUE, !computer);
    scelte_salva();
    nuova();
}

static void informazioni(void)
{
    char t[640];

    exinfo_testo(t, sizeof(t), "EXGO", VERSIONE_APP,
                 "Il gioco del go su scacchiera 9x9, 13x13 o 19x19, contro il "
                 "computer o in due.");
    ex_dlg_avviso("Informazioni su", t);
}

static void istruzioni(void)
{
    ex_dlg_testo("Istruzioni di EXGO",
        "LO SCOPO\n"
        "Circondare piu' territorio dell'avversario. Si gioca a turno, una pietra "
        "per volta sugli incroci; il nero comincia.\n"
        "\n"
        "LE CATTURE\n"
        "Le pietre dello stesso colore che si toccano lungo le linee sono un "
        "gruppo. Gli incroci vuoti accanto al gruppo sono le sue liberta'. Un "
        "gruppo senza liberta' e' catturato e lascia la scacchiera.\n"
        "\n"
        "LE DUE REGOLE\n"
        "Non si puo' mettere una pietra dove resterebbe senza liberta' (suicidio), "
        "a meno che catturi qualcosa. E non si puo' riprendere subito una sola "
        "pietra appena presa (ko): si gioca prima altrove.\n"
        "\n"
        "LA FINE\n"
        "Quando non c'e' piu' niente di utile da fare si passa. Dopo due passaggi "
        "di fila si contano i punti: un clic sui gruppi che sono comunque morti li "
        "segna (il gioco ne propone alcuni), poi Conta i punti. Ogni pietra sulla "
        "scacchiera e ogni incrocio vuoto circondato da un solo colore vale un "
        "punto; il bianco ha 6,5 punti di compenso (komi) perche' gioca per "
        "secondo.\n"
        "\n"
        "IL COMPUTER\n"
        "Gioca col bianco ed e' un principiante: guarda una mossa avanti. Va bene "
        "per imparare le regole. Partita > Due giocatori per giocare in due.\n"
        "\n"
        "I TASTI\n"
        "F2 o Ctrl+N: nuova partita.  Ctrl+Z: annulla.  P: passa.  Ctrl+Q: esci.");
}

static void esci(void) { scelte_salva(); ex_quit(0); }

static int tasto(unsigned int wp)
{
    unsigned int c = wp & KBD_KEY_MASK;

    if (wp & KBD_MOD_CTRL) {
        switch (c | 32) {
        case 'n': nuova(); return 1;
        case 'z': annulla(); return 1;
        case 'q': esci(); return 1;
        }
        return 0;
    }
    if (c == KBD_K_F(2)) { nuova(); return 1; }
    if ((c | 32) == 'p') { passa(); return 1; }
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
        disegna_tavola();
        disegna_pannello();
        mostra(0);
        return 0;

    case EXM_TIMER:
        gioco_tempo(f);
        if (g_pensa) mossa_computer();
        return EX_NO_REDRAW;

    case EXM_MOUSE_DOWN:
    case EXM_DOUBLE_CLICK:
        clic(EX_X(lp), EX_Y(lp));
        return EX_NO_REDRAW;

    case EXM_MOUSE_MOVE:
    case EXM_MOUSE_UP:
        return EX_NO_REDRAW;

    case EXM_COMMAND:
        switch (wp) {
        case ID_NUOVA:      nuova(); break;
        case ID_ANNULLA:
        case ID_B_ANNULLA:  annulla(); break;
        case ID_PASSA:
        case ID_CONTA:
        case ID_B_PASSA:    passa(); break;
        case ID_ABBANDONA:
        case ID_B_ABBANDONA: abbandona(); break;
        case ID_N9:         scegli_lato(9); break;
        case ID_N13:        scegli_lato(13); break;
        case ID_N19:        scegli_lato(19); break;
        case ID_COMPUTER:   scegli_avversario(1); break;
        case ID_DUE:        scegli_avversario(0); break;
        case ID_ESCI:       esci(); break;
        case ID_ISTR:       istruzioni(); break;
        case ID_INFO:       informazioni(); break;
        }
        /* The focus back to the window: P and the arrows are ours. */
        ex_clear_focus(g_f);
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
    int   bx = TAV_W + 15, by = GIOCO_MENU_H + PAN_H + 14;

    (void)argc; (void)argv;
    caso_semina();
    g_n = gioco_cfg_intero(NOME_CFG, "lato", 9);
    if (g_n != 13 && g_n != 19) g_n = 9;
    g_computer = gioco_cfg_intero(NOME_CFG, "computer", 1) ? 1 : 0;
    g_vinte = gioco_cfg_intero(NOME_CFG, "vinte", 0);
    g_perse = gioco_cfg_intero(NOME_CFG, "perse", 0);

    if (!tela_crea(&g_t, TAV_W, TAV_H) || !tela_crea(&g_pan, PAN_W, PAN_H)) {
        printf("exgo: memoria insufficiente.\n");
        return 1;
    }
    g_storia = (Pos *)malloc(sizeof(Pos) * STORIA_MAX);
    g_font   = ex_font_find(EX_FAMILY_SANS, 13, 0, 0);
    g_font_g = ex_font_find(EX_FAMILY_SERIF, 24, 1, 0);
    g_font_c = ex_font_find(EX_FAMILY_SANS, 11, 1, 0);

    g_f = ex_create("window", "EXGO", EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                    EX_AUTO, EX_AUTO, CLIENT_W, GIOCO_MENU_H + TAV_H, 0, 0, proc);
    if (!g_f) {
        printf("exgo: il server a finestre non risponde.\n");
        printf("      Avvialo con:  exwin\n");
        return 1;
    }

    g_menu = ex_menu_bar(g_f);
    ex_menu_add_item(g_menu, "Partita", "Nuova\tF2", ID_NUOVA);
    ex_menu_add_item(g_menu, "Partita", "Annulla la mossa\tCtrl+Z", ID_ANNULLA);
    ex_menu_add_item(g_menu, "Partita", "Passa\tP", ID_PASSA);
    ex_menu_add_item(g_menu, "Partita", "Abbandona", ID_ABBANDONA);
    ex_menu_add_item(g_menu, "Partita", "-", 0);
    ex_menu_add_item(g_menu, "Partita", "Scacchiera 9x9", ID_N9);
    ex_menu_add_item(g_menu, "Partita", "Scacchiera 13x13", ID_N13);
    ex_menu_add_item(g_menu, "Partita", "Scacchiera 19x19", ID_N19);
    ex_menu_add_item(g_menu, "Partita", "-", 0);
    ex_menu_add_item(g_menu, "Partita", "Contro il computer", ID_COMPUTER);
    ex_menu_add_item(g_menu, "Partita", "Due giocatori", ID_DUE);
    ex_menu_add_item(g_menu, "Partita", "-", 0);
    ex_menu_add_item(g_menu, "Partita", "Esci\tCtrl+Q", ID_ESCI);
    ex_menu_add_item(g_menu, "Info", "Istruzioni", ID_ISTR);
    ex_menu_add_item(g_menu, "Info", "Informazioni su", ID_INFO);
    ex_menu_check(g_menu, ID_N9, g_n == 9);
    ex_menu_check(g_menu, ID_N13, g_n == 13);
    ex_menu_check(g_menu, ID_N19, g_n == 19);
    ex_menu_check(g_menu, ID_COMPUTER, g_computer);
    ex_menu_check(g_menu, ID_DUE, !g_computer);

    g_bpassa     = ex_create("button", "Passa", EX_CHILD, bx, by, PAN_W - 30, 28, g_f, ID_B_PASSA, 0);
    g_bannulla   = ex_create("button", "Annulla", EX_CHILD, bx, by + 38, PAN_W - 30, 28, g_f, ID_B_ANNULLA, 0);
    g_babbandona = ex_create("button", "Abbandona", EX_CHILD, bx, by + 76, PAN_W - 30, 28, g_f, ID_B_ABBANDONA, 0);

    /* ! THE MENU BAR AND THE BUTTONS ARE CONTROLS: the base draws them on
     * EXM_PAINT, and no EXM_PAINT comes by itself for a window just made. */
    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    ex_set_timer(g_f, 200);
    nuova();

    while (ex_get_message(&m)) ex_dispatch(&m);
    return 0;
}
