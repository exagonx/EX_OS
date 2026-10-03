/* =============================================================================
 * exwin/bin/exmajong/exmajong.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * EXMajong, the mahjong solitaire (@GIOCHI, asked on 3 October 2026)
 *
 *     /exwin/bin/exmajong
 *
 * 144 tiles in the classic «turtle», five layers high. Two equal tiles that
 * are both free go away together; a tile is free when nothing lies on it and
 * its left or its right side is open. Flowers match any flower, seasons any
 * season. The board is empty: the game is won.
 *
 * ! EVERY DEAL CAN BE SOLVED. The tiles are placed backwards: from the full
 * turtle, two positions free at the same moment get a matching pair and are
 * taken away, and so on until none is left. Played in that order the game
 * comes out; the player just has to find an order that works. The same is
 * done for «Mescola» when no pair is left, with the tiles still on the table.
 *
 * The faces are drawn by program, once per kind (lib/exgioco): no Chinese
 * font is needed. The characters suit shows its number in red; the winds
 * say EST, SUD, OVEST, NORD.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"
#include "kbd_proto.h"
#include "exgioco.h"

#define VERSIONE_APP "0.001"
EX_VERSIONE("exmajong", VERSIONE_APP);

#define NOME_CFG "exmajong"

#define FW       36             /* the face of a tile */
#define FH       48
#define D        5              /* its thickness, and the shift of a layer */
#define MX       44
#define MY       36
#define TAV_W    600
#define TAV_H    460
#define STATO_H  20
#define N_TESS   144
#define N_TIPI   42

enum {
    ID_NUOVA = 1, ID_RICOMINCIA, ID_ANNULLA, ID_SUGGERISCI, ID_MESCOLA, ID_LIBERE,
    ID_STAT, ID_ESCI, ID_ISTR, ID_INFO
};

/* --- The turtle ------------------------------------------------------------ */
typedef struct { signed char x, y, z; } Posto;      /* x, y in half tiles */

static Posto  g_p[N_TESS];
static int    g_ordine[N_TESS];                     /* drawing order */
/* Who lies on each tile, and who touches it on the left and on the right. */
static unsigned char g_sopra[N_TESS][4], g_nsopra[N_TESS];
static unsigned char g_sx[N_TESS][2], g_nsx[N_TESS];
static unsigned char g_dx[N_TESS][2], g_ndx[N_TESS];

static int g_np;

static void posto(int x2, int y2, int z)
{
    g_p[g_np].x = (signed char)x2;
    g_p[g_np].y = (signed char)y2;
    g_p[g_np].z = (signed char)z;
    g_np++;
}

static void riquadro(int x0, int x1, int y0, int y1, int z)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) posto(2 * x, 2 * y, z);
}

static int vicino(int a, int b) { return a - b < 2 && b - a < 2; }

static int confronta_ordine(const void *a, const void *b)
{
    const Posto *p = &g_p[*(const int *)a], *q = &g_p[*(const int *)b];

    if (p->z != q->z) return p->z - q->z;
    if (p->x + p->y != q->x + q->y) return (p->x + p->y) - (q->x + q->y);
    return p->x - q->x;
}

static void tartaruga(void)
{
    static const signed char RIGHE[8][2] = {
        {1, 12}, {3, 10}, {2, 11}, {1, 12}, {1, 12}, {2, 11}, {3, 10}, {1, 12}
    };
    int i, j, y;

    g_np = 0;
    for (y = 0; y < 8; y++) riquadro(RIGHE[y][0], RIGHE[y][1], y, y, 0);
    posto(0, 7, 0);
    posto(26, 7, 0);
    posto(28, 7, 0);
    riquadro(4, 9, 1, 6, 1);
    riquadro(5, 8, 2, 5, 2);
    riquadro(6, 7, 3, 4, 3);
    posto(13, 7, 4);

    for (i = 0; i < N_TESS; i++) {
        g_nsopra[i] = g_nsx[i] = g_ndx[i] = 0;
        g_ordine[i] = i;
    }
    for (i = 0; i < N_TESS; i++)
        for (j = 0; j < N_TESS; j++) {
            const Posto *a = &g_p[i], *b = &g_p[j];
            if (i == j) continue;
            if (b->z == a->z + 1 && vicino(a->x, b->x) && vicino(a->y, b->y) && g_nsopra[i] < 4)
                g_sopra[i][g_nsopra[i]++] = (unsigned char)j;
            if (b->z == a->z && vicino(a->y, b->y)) {
                if (b->x == a->x - 2 && g_nsx[i] < 2) g_sx[i][g_nsx[i]++] = (unsigned char)j;
                if (b->x == a->x + 2 && g_ndx[i] < 2) g_dx[i][g_ndx[i]++] = (unsigned char)j;
            }
        }
    qsort(g_ordine, N_TESS, sizeof(int), confronta_ordine);
}

/* --- The game -------------------------------------------------------------- */
typedef struct {
    signed char   tipo[N_TESS];
    unsigned char c[N_TESS];        /* still on the table */
    unsigned int  mosse;
} Stato;

static ExWindow g_f, g_menu;
static Tela     g_t;
static Tela     g_facce[N_TIPI];
static ExFont   g_font, g_f_grande, g_f_nota;
static Stato    g_s;
static signed char g_iniziale[N_TESS];
static int      g_scelta = -1;
static int      g_sugg_a = -1, g_sugg_b = -1;
static int      g_mostra_libere;

#define STORIA_MAX 80
static Stato    g_storia[STORIA_MAX];
static int      g_nstoria;

static int      g_partite, g_vinte, g_record;    /* record: best time, seconds */
static unsigned int g_inizio_ms, g_secondi;
static int      g_finita;

static int classe(int tipo) { return tipo < 34 ? tipo : tipo < 38 ? 34 : 38; }
static int uguali(int a, int b) { return classe(a) == classe(b); }

static int libera_in(const unsigned char *c, int i)
{
    int k, sx = 1, dx = 1;

    for (k = 0; k < g_nsopra[i]; k++) if (c[g_sopra[i][k]]) return 0;
    for (k = 0; k < g_nsx[i]; k++) if (c[g_sx[i][k]]) sx = 0;
    for (k = 0; k < g_ndx[i]; k++) if (c[g_dx[i][k]]) dx = 0;
    return sx || dx;
}

static int libera(int i) { return g_s.c[i] && libera_in(g_s.c, i); }

/* Gives the tiles in `tipi` (n of them, n even, made of matching pairs) to the
 * positions marked in `dove`, so that taking them away pair by pair works.
 * Returns 0 if a hundred tries all got stuck. */
static int disponi(const unsigned char *dove, signed char *tipi, int n, signed char *uscita)
{
    signed char coppie[N_TESS];
    unsigned char virt[N_TESS];
    int libere[N_TESS];
    int prova, i, k, np;

    /* Pair them: the same class, two by two, after a shuffle. */
    for (i = n - 1; i > 0; i--) {
        int j = (int)caso_fino((unsigned int)i + 1);
        signed char x = tipi[i]; tipi[i] = tipi[j]; tipi[j] = x;
    }
    np = 0;
    for (i = 0; i < n; i++) {
        if (tipi[i] < 0) continue;
        for (k = i + 1; k < n; k++)
            if (tipi[k] >= 0 && uguali(tipi[i], tipi[k])) break;
        if (k == n) return 0;
        coppie[np++] = tipi[i];
        coppie[np++] = tipi[k];
        tipi[i] = tipi[k] = -1;
    }

    for (prova = 0; prova < 100; prova++) {
        for (i = 0; i < N_TESS; i++) virt[i] = dove[i];
        for (k = 0; k < np; k += 2) {
            int nl = 0, a, b;
            for (i = 0; i < N_TESS; i++) if (virt[i] && libera_in(virt, i)) libere[nl++] = i;
            if (nl < 2) break;
            a = (int)caso_fino((unsigned int)nl);
            b = (int)caso_fino((unsigned int)nl - 1);
            if (b >= a) b++;
            uscita[libere[a]] = coppie[k];
            uscita[libere[b]] = coppie[k + 1];
            virt[libere[a]] = virt[libere[b]] = 0;
        }
        if (k >= np) return 1;
    }
    return 0;
}

static int coppie_libere(int *a, int *b)
{
    int i, j, n = 0;

    *a = *b = -1;
    for (i = 0; i < N_TESS; i++) {
        if (!libera(i)) continue;
        for (j = i + 1; j < N_TESS; j++)
            if (libera(j) && uguali(g_s.tipo[i], g_s.tipo[j])) {
                if (*a < 0) { *a = i; *b = j; }
                n++;
            }
    }
    return n;
}

static int rimaste(void)
{
    int i, n = 0;
    for (i = 0; i < N_TESS; i++) n += g_s.c[i];
    return n;
}

/* --- The faces -------------------------------------------------------------- */
static const unsigned int COLORI_PALLINI[3] = { 0x2A5DB0, 0x2E8B3E, 0xC0282D };
static const unsigned char SCHEMA[10][9] = {
    {0},
    {4},
    {1, 7},
    {0, 4, 8},
    {0, 2, 6, 8},
    {0, 2, 4, 6, 8},
    {0, 2, 3, 5, 6, 8},
    {0, 2, 3, 4, 5, 6, 8},
    {0, 1, 2, 3, 5, 6, 7, 8},
    {0, 1, 2, 3, 4, 5, 6, 7, 8},
};

static void pallini(Tela *t, int n)
{
    int k;

    for (k = 0; k < n; k++) {
        int pos = SCHEMA[n][k], cx = FW * (24 + 26 * (pos % 3)) / 100;
        int cy = FH * (20 + 30 * (pos / 3)) / 100, r = n == 1 ? 11 : n <= 4 ? 6 : 5;
        unsigned int c = COLORI_PALLINI[n == 1 ? 2 : k % 3];

        tela_disco(t, cx, cy, r, c, 1);
        tela_disco(t, cx, cy, r / 2, 0xF5EED8, 0);
        tela_disco(t, cx, cy, r / 4 + 1, c, 0);
    }
}

static void canne(Tela *t, int n)
{
    int k;

    if (n == 1) {                       /* the one: a tall cane with a red top */
        tela_tondo(t, FW / 2 - 4, 8, 8, FH - 16, 3, 0x2E8B3E);
        tela_disco(t, FW / 2, 10, 5, 0xC0282D, 1);
        tela_rett(t, FW / 2 - 4, FH / 2, 8, 2, 0x1C5C28);
        return;
    }
    for (k = 0; k < n; k++) {
        int pos = SCHEMA[n][k], cx = FW * (24 + 26 * (pos % 3)) / 100;
        int cy = FH * (20 + 30 * (pos / 3)) / 100, h = n <= 4 ? 13 : 11;
        unsigned int c = (n >= 5 && pos == 4) ? 0xC0282D : 0x2E8B3E;

        tela_tondo(t, cx - 3, cy - h / 2, 6, h, 2, c);
        tela_rett(t, cx - 3, cy, 6, 1, colore_scuro(c, 90));
    }
}

static void caratteri(Tela *t, int n)
{
    char s[4];

    sprintf(s, "%d", n);
    tela_testo_c(t, g_f_grande, FW / 2, 4, s, 0x1C2A6E);
    /* A red mark under the number, the same on every tile of the suit. */
    tela_rett(t, 9, 30, FW - 18, 2, 0xC0282D);
    tela_rett(t, FW / 2 - 1, 27, 2, 15, 0xC0282D);
    tela_rett(t, 11, 36, FW - 22, 2, 0xC0282D);
    tela_rett(t, 9, 41, FW - 18, 2, 0xC0282D);
}

static void scritta(Tela *t, const char *s, unsigned int c)
{
    tela_testo_c(t, g_f_nota, FW / 2, FH - ex_font_height(g_f_nota) - 3, s, c);
}

static void fiore(Tela *t, int n, unsigned int c, int stagione)
{
    static const int DX[5] = { 0, 7, 4, -4, -7 }, DY[5] = { -7, -2, 6, 6, -2 };
    char s[4];
    int k;

    if (stagione) {                     /* a sun with eight rays */
        for (k = 0; k < 8; k++) {
            static const int RX[8] = { 0, 7, 10, 7, 0, -7, -10, -7 };
            static const int RY[8] = { -10, -7, 0, 7, 10, 7, 0, -7 };
            tela_disco(t, FW / 2 + RX[k], 19 + RY[k], 2, c, 0);
        }
        tela_disco(t, FW / 2, 19, 6, c, 1);
    } else {
        for (k = 0; k < 5; k++) tela_disco(t, FW / 2 + DX[k], 19 + DY[k], 5, c, 1);
        tela_disco(t, FW / 2, 19, 3, 0xE8B020, 0);
    }
    sprintf(s, "%d", n);
    tela_testo(t, g_f_nota, 3, 2, s, 0x1C2A6E);
    scritta(t, stagione ? "STAG." : "FIORE", 0x1C2A6E);
}

static void faccia(Tela *t, int tipo)
{
    static const char *VENTI[4] = { "E", "S", "O", "N" };
    static const char *NOMI_VENTI[4] = { "EST", "SUD", "OVEST", "NORD" };
    static const unsigned int FIORI[4] = { 0xD0507A, 0xC07020, 0x8050B0, 0x3070C0 };
    static const unsigned int STAGIONI[4] = { 0x3C9A3C, 0xD05020, 0xA06A20, 0x4A7AC0 };

    memset(t->px, 0, (unsigned int)(t->w * t->h) * 4u);
    tela_tondo(t, D, D, FW, FH, 5, 0x9C8456);        /* the side */
    tela_tondo(t, D - 1, D - 1, FW, FH, 5, 0xC2A970);
    tela_tondo(t, 0, 0, FW, FH, 5, 0x8C7A50);        /* the face */
    tela_tondo(t, 1, 1, FW - 2, FH - 2, 4, 0xFBF6E6);
    tela_sfuma(t, 3, FH - 10, FW - 6, 7, 0xFBF6E6, 0xEDE3C6);

    if (tipo < 9)        pallini(t, tipo + 1);
    else if (tipo < 18)  canne(t, tipo - 8);
    else if (tipo < 27)  caratteri(t, tipo - 17);
    else if (tipo < 31) {
        tela_testo_c(t, g_f_grande, FW / 2, 5, VENTI[tipo - 27], 0x1C2A6E);
        scritta(t, NOMI_VENTI[tipo - 27], 0x1C2A6E);
    } else if (tipo == 31) {
        tela_tondo(t, 8, 8, FW - 16, 22, 4, 0xC0282D);
        tela_rett(t, FW / 2 - 1, 5, 2, 28, 0xC0282D);
        scritta(t, "DRAGO", 0xC0282D);
    } else if (tipo == 32) {
        tela_tondo(t, 8, 8, FW - 16, 22, 4, 0x2E8B3E);
        tela_rett(t, 12, 18, FW - 24, 2, 0xFBF6E6);
        scritta(t, "DRAGO", 0x2E8B3E);
    } else if (tipo == 33) {
        tela_bordo(t, 8, 7, FW - 16, 24, 0x2A5DB0);
        tela_bordo(t, 11, 10, FW - 22, 18, 0x2A5DB0);
        scritta(t, "DRAGO", 0x2A5DB0);
    } else if (tipo < 38) fiore(t, tipo - 33, FIORI[tipo - 34], 0);
    else                  fiore(t, tipo - 37, STAGIONI[tipo - 38], 1);
}

static Tela *tessera(int tipo)
{
    Tela *t = &g_facce[tipo];

    if (!t->px) {
        if (!tela_crea(t, FW + D, FH + D)) return 0;
        faccia(t, tipo);
    }
    return t;
}

/* --- Drawing ---------------------------------------------------------------- */
static int posto_x(int i) { return MX + g_p[i].x * FW / 2 - g_p[i].z * D; }
static int posto_y(int i) { return MY + g_p[i].y * FH / 2 - g_p[i].z * D; }

static void disegna_stato(void)
{
    char t[160], tempo[16];
    int  y = TAV_H - STATO_H, a, b;

    tela_sfuma(&g_t, 0, y, TAV_W, STATO_H, 0x3A2A1A, 0x2A1E12);
    gioco_tempo_testo(tempo, g_secondi);
    snprintf(t, sizeof(t), "Tessere: %d    Coppie libere: %d    Tempo: %s",
             rimaste(), coppie_libere(&a, &b), tempo);
    tela_testo(&g_t, g_font, 8, y + 3, t, 0xF0E6D2);
    snprintf(t, sizeof(t), "Vinte %d su %d", g_vinte, g_partite);
    tela_testo(&g_t, g_font, TAV_W - 8 - ex_text_width(g_font, t), y + 3, t, 0xF0E6D2);
}

static void disegna(void)
{
    int k;

    tela_sfuma(&g_t, 0, 0, TAV_W, TAV_H - STATO_H, 0x5C3B22, 0x3E2716);
    for (k = 0; k < N_TESS; k++) {
        int i = g_ordine[k], x, y;
        Tela *f;

        if (!g_s.c[i]) continue;
        x = posto_x(i);
        y = posto_y(i);
        f = tessera(g_s.tipo[i]);
        if (f) tela_copia(&g_t, x, y, f, 0, 0, FW + D, FH + D, 1);
        if (i == g_scelta)
            tela_velo(&g_t, x + 1, y + 1, FW - 2, FH - 2, 0xF0C020, 110);
        else if (i == g_sugg_a || i == g_sugg_b)
            tela_velo(&g_t, x + 1, y + 1, FW - 2, FH - 2, 0x40C0F0, 100);
        else if (g_mostra_libere && !libera(i))
            tela_velo(&g_t, x + 1, y + 1, FW - 2, FH - 2, 0x000000, 70);
    }
    disegna_stato();
}

static void ridisegna(void)
{
    disegna();
    tela_mostra(g_f, &g_t, 0, 0, TAV_W, TAV_H);
    gioco_annuncia(g_f, 1);
}

/* --- Games ------------------------------------------------------------------ */
static void statistiche_salva(void)
{
    char t[240];

    snprintf(t, sizeof(t),
             "# EXMajong: le scelte e le partite. Le riscrive il programma.\n"
             "libere  = %d\npartite = %d\nvinte   = %d\nrecord  = %d\n",
             g_mostra_libere, g_partite, g_vinte, g_record);
    gioco_cfg_scrivi(NOME_CFG, t);
}

static void nuova(int stessa)
{
    int i;

    for (i = 0; i < N_TESS; i++) g_s.c[i] = 1;
    if (!stessa) {
        signed char tipi[N_TESS];
        int n = 0, k;

        for (k = 0; k < 34; k++) for (i = 0; i < 4; i++) tipi[n++] = (signed char)k;
        for (k = 34; k < 42; k++) tipi[n++] = (signed char)k;
        while (!disponi(g_s.c, tipi, N_TESS, g_iniziale)) {
            n = 0;
            for (k = 0; k < 34; k++) for (i = 0; i < 4; i++) tipi[n++] = (signed char)k;
            for (k = 34; k < 42; k++) tipi[n++] = (signed char)k;
        }
    }
    memcpy(g_s.tipo, g_iniziale, sizeof(g_s.tipo));
    g_s.mosse = 0;
    g_nstoria = 0;
    g_scelta = g_sugg_a = g_sugg_b = -1;
    g_inizio_ms = 0;
    g_secondi = 0;
    g_finita = 0;
    g_partite++;
    statistiche_salva();
    ridisegna();
}

static void ricorda(void)
{
    if (g_nstoria == STORIA_MAX) {
        memmove(g_storia, g_storia + 1, sizeof(Stato) * (STORIA_MAX - 1));
        g_nstoria--;
    }
    g_storia[g_nstoria++] = g_s;
}

static void annulla(void)
{
    if (!g_nstoria || g_finita) return;
    g_s = g_storia[--g_nstoria];
    g_scelta = g_sugg_a = g_sugg_b = -1;
    ridisegna();
}

static void mescola(void)
{
    signed char tipi[N_TESS], uscita[N_TESS];
    int i, n = 0;

    for (i = 0; i < N_TESS; i++) if (g_s.c[i]) tipi[n++] = g_s.tipo[i];
    if (n < 2) return;
    memcpy(uscita, g_s.tipo, sizeof(uscita));
    ricorda();
    for (i = 0; i < 20; i++) {
        signed char copia[N_TESS];
        memcpy(copia, tipi, (unsigned int)n);
        if (disponi(g_s.c, copia, n, uscita)) break;
    }
    if (i == 20) {
        g_nstoria--;
        ex_dlg_avviso("Mescola", "Le tessere rimaste non si possono disporre in modo "
                      "che la partita si risolva. Annulla qualche mossa (Ctrl+Z).");
        ridisegna();
        return;
    }
    memcpy(g_s.tipo, uscita, sizeof(g_s.tipo));
    g_s.mosse++;
    g_scelta = g_sugg_a = g_sugg_b = -1;
    ridisegna();
}

static void controlla_fine(void)
{
    char t[240], tempo[16];
    int a, b;

    if (g_finita) return;
    if (!rimaste()) {
        g_finita = 1;
        g_vinte++;
        if (!g_record || g_secondi < (unsigned int)g_record) g_record = (int)g_secondi;
        statistiche_salva();
        ridisegna();
        gioco_tempo_testo(tempo, g_secondi);
        snprintf(t, sizeof(t), "Hai liberato il tavolo in %s.\n\nUn'altra partita?", tempo);
        if (ex_dlg_conferma("EXMajong", t, "Nuova partita", "Basta cosi'")) nuova(0);
        else ridisegna();
        return;
    }
    if (!coppie_libere(&a, &b)) {
        static const char *const VOCI[3] = { "Mescola", "Annulla la mossa", "Lascia stare" };
        int r = ex_dlg_scegli("EXMajong", "Nessuna coppia libera: non si puo' piu' "
                              "togliere niente.\n\nMescolare le tessere rimaste?", VOCI, 3);
        if (r == 0) mescola();
        else if (r == 1) annulla();
        else ridisegna();
    }
}

static void suggerisci(void)
{
    int a, b;

    if (g_finita) return;
    if (coppie_libere(&a, &b)) { g_sugg_a = a; g_sugg_b = b; ridisegna(); return; }
    controlla_fine();
}

static int trova(int x, int y)
{
    int k;

    for (k = N_TESS - 1; k >= 0; k--) {
        int i = g_ordine[k], px, py;
        if (!g_s.c[i]) continue;
        px = posto_x(i);
        py = posto_y(i);
        if (x >= px && y >= py && x < px + FW && y < py + FH) return i;
    }
    return -1;
}

static void clic(int x, int y)
{
    int i = trova(x, y - GIOCO_MENU_H);

    if (g_finita) return;
    g_sugg_a = g_sugg_b = -1;
    if (i < 0 || !libera(i)) { g_scelta = -1; ridisegna(); return; }
    if (g_scelta < 0 || g_scelta == i) {
        g_scelta = g_scelta == i ? -1 : i;
        ridisegna();
        return;
    }
    if (!uguali(g_s.tipo[i], g_s.tipo[g_scelta])) { g_scelta = i; ridisegna(); return; }

    ricorda();
    g_s.c[i] = g_s.c[g_scelta] = 0;
    g_s.mosse++;
    g_scelta = -1;
    if (!g_inizio_ms) g_inizio_ms = uptime_ms();
    ridisegna();
    controlla_fine();
}

static void statistiche(void)
{
    char t[240], tempo[16];

    gioco_tempo_testo(tempo, (unsigned int)g_record);
    snprintf(t, sizeof(t), "Partite giocate: %d\nPartite vinte: %d\nTempo migliore: %s",
             g_partite, g_vinte, g_record ? tempo : "-");
    ex_dlg_avviso("Statistiche", t);
}

static void informazioni(void)
{
    char t[640];

    exinfo_testo(t, sizeof(t), "EXMajong", VERSIONE_APP,
                 "Il solitario con le tessere del mahjong: 144 tessere a tartaruga, "
                 "da togliere a coppie.");
    ex_dlg_avviso("Informazioni su", t);
}

static void istruzioni(void)
{
    ex_dlg_testo("Istruzioni di EXMajong",
        "LO SCOPO\n"
        "Togliere tutte le 144 tessere dal tavolo, due alla volta.\n"
        "\n"
        "LE COPPIE\n"
        "Si tolgono due tessere uguali, tutt'e due libere: un clic sulla prima, "
        "un clic sulla seconda. I quattro fiori vanno bene fra loro anche se sono "
        "diversi, e cosi' le quattro stagioni.\n"
        "\n"
        "QUANDO UNA TESSERA E' LIBERA\n"
        "Quando non ha niente sopra, e almeno uno dei due lati, il destro o il "
        "sinistro, e' scoperto. Partita > Mostra le tessere libere scurisce le "
        "altre.\n"
        "\n"
        "LE TESSERE\n"
        "Cerchi, canne e caratteri (con il numero), da 1 a 9; i quattro venti "
        "(EST, SUD, OVEST, NORD); i tre draghi (rosso, verde, bianco); quattro "
        "fiori e quattro stagioni.\n"
        "\n"
        "OGNI PARTITA SI PUO' VINCERE: le tessere sono disposte apposta. Se non "
        "resta nessuna coppia, si puo' mescolare quel che resta o annullare.\n"
        "\n"
        "I TASTI\n"
        "F2 o Ctrl+N: nuova partita.  Ctrl+Z: annulla.  H: suggerimento.  "
        "M: mescola.  Ctrl+Q: esci.");
}

static void esci(void) { statistiche_salva(); ex_quit(0); }

static void libere_scegli(int si)
{
    g_mostra_libere = si;
    ex_menu_check(g_menu, ID_LIBERE, si);
    statistiche_salva();
    ridisegna();
}

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
    if ((c | 32) == 'm') { mescola(); return 1; }
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
        disegna();
        tela_mostra(f, &g_t, 0, 0, TAV_W, TAV_H);
        return 0;

    case EXM_TIMER:
        gioco_tempo(f);
        if (g_inizio_ms && !g_finita) {
            unsigned int s = (uptime_ms() - g_inizio_ms) / 1000u;
            if (s != g_secondi) {
                g_secondi = s;
                disegna_stato();
                tela_mostra(f, &g_t, 0, TAV_H - STATO_H, TAV_W, STATO_H);
                gioco_annuncia(f, 0);
            }
        }
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
        case ID_NUOVA:      nuova(0); break;
        case ID_RICOMINCIA: g_partite--; nuova(1); break;
        case ID_ANNULLA:    annulla(); break;
        case ID_SUGGERISCI: suggerisci(); break;
        case ID_MESCOLA:    mescola(); break;
        case ID_LIBERE:     libere_scegli(!g_mostra_libere); break;
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
    g_mostra_libere = gioco_cfg_intero(NOME_CFG, "libere", 0) ? 1 : 0;
    g_partite = gioco_cfg_intero(NOME_CFG, "partite", 0);
    g_vinte   = gioco_cfg_intero(NOME_CFG, "vinte", 0);
    g_record  = gioco_cfg_intero(NOME_CFG, "record", 0);

    if (!tela_crea(&g_t, TAV_W, TAV_H)) {
        printf("exmajong: memoria insufficiente.\n");
        return 1;
    }
    g_font    = ex_font_find(EX_FAMILY_SANS, 13, 0, 0);
    g_f_grande = ex_font_find(EX_FAMILY_SERIF, 22, 1, 0);
    g_f_nota  = ex_font_find(EX_FAMILY_SANS, 9, 1, 0);
    tartaruga();

    g_f = ex_create("window", "EXMajong", EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                    EX_AUTO, EX_AUTO, TAV_W, GIOCO_MENU_H + TAV_H, 0, 0, proc);
    if (!g_f) {
        printf("exmajong: il server a finestre non risponde.\n");
        printf("          Avvialo con:  exwin\n");
        return 1;
    }

    g_menu = ex_menu_bar(g_f);
    ex_menu_add_item(g_menu, "Partita", "Nuova\tF2", ID_NUOVA);
    ex_menu_add_item(g_menu, "Partita", "Ricomincia la stessa", ID_RICOMINCIA);
    ex_menu_add_item(g_menu, "Partita", "Annulla la mossa\tCtrl+Z", ID_ANNULLA);
    ex_menu_add_item(g_menu, "Partita", "Suggerimento\tH", ID_SUGGERISCI);
    ex_menu_add_item(g_menu, "Partita", "Mescola\tM", ID_MESCOLA);
    ex_menu_add_item(g_menu, "Partita", "-", 0);
    ex_menu_add_item(g_menu, "Partita", "Mostra le tessere libere", ID_LIBERE);
    ex_menu_add_item(g_menu, "Partita", "Statistiche", ID_STAT);
    ex_menu_add_item(g_menu, "Partita", "Esci\tCtrl+Q", ID_ESCI);
    ex_menu_add_item(g_menu, "Info", "Istruzioni", ID_ISTR);
    ex_menu_add_item(g_menu, "Info", "Informazioni su", ID_INFO);
    ex_menu_check(g_menu, ID_LIBERE, g_mostra_libere);

    /* ! THE MENU BAR AND THE BUTTONS ARE CONTROLS: the base draws them on
     * EXM_PAINT, and no EXM_PAINT comes by itself for a window just made. */
    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    ex_set_timer(g_f, 200);
    nuova(0);

    while (ex_get_message(&m)) ex_dispatch(&m);
    return 0;
}
