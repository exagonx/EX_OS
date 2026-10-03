/* =============================================================================
 * exwin/bin/calctor/calctor.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Calctor, the calculator of ExWin (@CALCTOR, asked on 27 September 2026)
 *
 *     /exwin/bin/calctor
 *
 * Three modes, chosen with the radios under the menu bar or from
 * Opzioni > Modalita (a side menu):
 *   - Normale: the four operations, percent, square root, 1/x, x^2, memory;
 *   - Scientifica: adds trigonometry (degrees or radians), logarithms,
 *     powers and roots, factorial, parentheses;
 *   - Programmatore: whole numbers of 32 bits in base 2, 8, 10 or 16, with
 *     AND, OR, XOR, NOT and the shifts. Changing base converts what is shown.
 *
 * The display has four lines: the last three calculations, and the one being
 * typed. The «Cronologia» box opens the whole tape in a window of its own —
 * the paper roll of an office calculator — and File > Salva writes it as text.
 *
 * ! WHAT IS TYPED IS AN EXPRESSION, NOT A RUNNING TOTAL: 2 + 3 * 4 = gives 14,
 * as written, and parentheses exist. An office calculator would say 20; but a
 * scientific one that ignores precedence is simply wrong, and one program
 * with two rules would be worse. The functions (sin, x^2, 1/x...) act at once
 * on the number just typed, as on every pocket calculator.
 *
 * ! WITH THE TAPE ON IT IS AN ADDING MACHINE (28 September 2026, asked by
 * the user): «=» goes away, «+» and «-» become two tall keys, and each one
 * both adds (or subtracts) the number typed to a running total AND shows
 * that total — the «+=» and «-=» of office printing calculators. «12 x 3 +»
 * finishes the product and adds 36; «+» again with no new number prints
 * the subtotal. Enter and «=» from the keyboard work as «+». See nastro().
 *
 * ! THE CHOICES ARE THE USER'S, SO THEY LIVE IN THE PROFILE:
 * $HOME/.exwin/config/calctor.cfg — mode, base, degrees or radians, whether
 * the tape window is open (the rule in SVILUPPO.md).
 *
 * ! THE MATH IS lib/exmat (the x87), compiled in: the libc has no sin.
 * ============================================================================= */

#include "libc.h"
#include "math.h"
#include "exwin.h"
#include "exdlg.h"
#include "exinfo.h"
#include "kbd_proto.h"

/* +0.001 a ogni modifica: `calctor -version` la stampa. Vedi EX_VERSIONE. */
#define VERSIONE_APP "0.003"
EX_VERSIONE("calctor", VERSIONE_APP);

/* --- Geometry -------------------------------------------------------------- */
#define BW          60          /* a button */
#define BH          28
#define PASSO_X     64
#define PASSO_Y     32
#define MARGINE     8
#define Y_RADIO     26
#define Y_DISPLAY   52
#define H_DISPLAY   84
#define Y_SOTTO     142         /* the row under the display */
#define Y_TASTI     184
#define X_EXTRA     (MARGINE + 5 * PASSO_X)     /* the scientific / programmer block */
#define W_NORMALE   (X_EXTRA + MARGINE - 4)
#define W_LARGA     (X_EXTRA + 4 * PASSO_X + MARGINE - 4)
#define H_FIN       (Y_TASTI + 6 * PASSO_Y + 4)

/* --- Ids --------------------------------------------------------------------- */
enum {
    ID_NUOVO = 1, ID_SALVA, ID_ESCI,
    ID_M_NORM, ID_M_SCI, ID_M_PROG,
    ID_INFO, ID_ISTR,
    ID_R_NORM = 10, ID_R_SCI, ID_R_PROG,
    ID_CRON = 13,
    ID_B_BIN = 14, ID_B_OTT, ID_B_DEC, ID_B_ESA,
    ID_TASTO = 100
};

enum { MODO_NORM, MODO_SCI, MODO_PROG };
enum { BASE_BIN, BASE_OTT, BASE_DEC, BASE_ESA };

static const char *const MODI_CFG[3]  = { "normale", "scientifica", "programmatore" };
static const char *const BASI_CFG[4]  = { "bin", "ott", "dec", "esa" };
static const char *const BASI_NOME[4] = { "BIN", "OTT", "DEC", "ESA" };
static const int         BASI_N[4]    = { 2, 8, 10, 16 };
static const int         BASI_CIFRE[4] = { 32, 11, 10, 8 };

/* =============================================================================
 * THE BUTTONS
 *
 * One table: the caption, the code the engine understands, the block (0 the
 * main one, always there; 1 scientific; 2 programmer), the cell, and how many
 * rows tall. The keyboard sends the same codes.
 * ============================================================================= */
typedef struct {
    const char *testo;
    const char *cod;
    int         blocco, col, riga, alto;
    ExWindow  c;
} Tasto;

static Tasto g_t[] = {
    /* the main block, 5 x 6 */
    { "MC", "MC", 0, 0, 0, 1, 0 }, { "MR", "MR", 0, 1, 0, 1, 0 },
    { "M+", "M+", 0, 2, 0, 1, 0 }, { "M-", "M-", 0, 3, 0, 1, 0 },
    { "C",  "C",  0, 4, 0, 1, 0 },
    { "sqrt", "sqrt", 0, 0, 1, 1, 0 }, { "1/x", "1/x", 0, 1, 1, 1, 0 },
    { "x^2", "x^2", 0, 2, 1, 1, 0 },   { "%", "%", 0, 3, 1, 1, 0 },
    { "CE", "CE", 0, 4, 1, 1, 0 },
    { "7", "7", 0, 0, 2, 1, 0 }, { "8", "8", 0, 1, 2, 1, 0 }, { "9", "9", 0, 2, 2, 1, 0 },
    { "/", "/", 0, 3, 2, 1, 0 }, { "<-", "<-", 0, 4, 2, 1, 0 },
    { "4", "4", 0, 0, 3, 1, 0 }, { "5", "5", 0, 1, 3, 1, 0 }, { "6", "6", 0, 2, 3, 1, 0 },
    { "*", "*", 0, 3, 3, 1, 0 }, { "+/-", "+/-", 0, 4, 3, 1, 0 },
    { "1", "1", 0, 0, 4, 1, 0 }, { "2", "2", 0, 1, 4, 1, 0 }, { "3", "3", 0, 2, 4, 1, 0 },
    { "-", "-", 0, 3, 4, 1, 0 }, { "=", "=", 0, 4, 4, 2, 0 },
    { "0", "0", 0, 0, 5, 1, 0 }, { "00", "00", 0, 1, 5, 1, 0 }, { ".", ".", 0, 2, 5, 1, 0 },
    { "+", "+", 0, 3, 5, 1, 0 },

    /* the tape (adding machine) keys: in the main block, two rows tall,
     * shown instead of - + = when the tape is on */
    { "+", "+", 3, 3, 4, 2, 0 }, { "-", "-", 3, 4, 4, 2, 0 },

    /* scientific, 4 x 6 */
    { "DEG", "ang", 1, 0, 0, 1, 0 }, { "pi", "pi", 1, 1, 0, 1, 0 },
    { "e", "e", 1, 2, 0, 1, 0 },     { "n!", "n!", 1, 3, 0, 1, 0 },
    { "sin", "sin", 1, 0, 1, 1, 0 }, { "cos", "cos", 1, 1, 1, 1, 0 },
    { "tan", "tan", 1, 2, 1, 1, 0 }, { "x^y", "^", 1, 3, 1, 1, 0 },
    { "asin", "asin", 1, 0, 2, 1, 0 }, { "acos", "acos", 1, 1, 2, 1, 0 },
    { "atan", "atan", 1, 2, 2, 1, 0 }, { "x^3", "x^3", 1, 3, 2, 1, 0 },
    { "sinh", "sinh", 1, 0, 3, 1, 0 }, { "cosh", "cosh", 1, 1, 3, 1, 0 },
    { "tanh", "tanh", 1, 2, 3, 1, 0 }, { "cbrt", "cbrt", 1, 3, 3, 1, 0 },
    { "ln", "ln", 1, 0, 4, 1, 0 },   { "log", "log", 1, 1, 4, 1, 0 },
    { "e^x", "e^x", 1, 2, 4, 1, 0 }, { "10^x", "10^x", 1, 3, 4, 1, 0 },
    { "(", "(", 1, 0, 5, 1, 0 },     { ")", ")", 1, 1, 5, 1, 0 },
    { "|x|", "abs", 1, 2, 5, 1, 0 }, { "mod", "mod", 1, 3, 5, 1, 0 },

    /* programmer, 4 x 5 */
    { "A", "A", 2, 0, 0, 1, 0 }, { "B", "B", 2, 1, 0, 1, 0 },
    { "AND", "and", 2, 2, 0, 1, 0 }, { "OR", "or", 2, 3, 0, 1, 0 },
    { "C", "Cx", 2, 0, 1, 1, 0 }, { "D", "D", 2, 1, 1, 1, 0 },
    { "XOR", "xor", 2, 2, 1, 1, 0 }, { "NOT", "not", 2, 3, 1, 1, 0 },
    { "E", "E", 2, 0, 2, 1, 0 }, { "F", "F", 2, 1, 2, 1, 0 },
    { "<<", "shl", 2, 2, 2, 1, 0 }, { ">>", "shr", 2, 3, 2, 1, 0 },
    { "(", "(", 2, 0, 3, 1, 0 }, { ")", ")", 2, 1, 3, 1, 0 },
    { "mod", "mod", 2, 2, 3, 1, 0 },
};
#define N_TASTI ((int)(sizeof(g_t) / sizeof(g_t[0])))

/* =============================================================================
 * THE STATE
 * ============================================================================= */
static ExWindow g_f, g_menu, g_r_modo[3], g_cron, g_riq_base, g_r_base[4];
static ExWindow g_fcron = 0, g_lcron = 0;     /* the tape window */
static int        g_w = W_NORMALE, g_h = H_FIN;
static ExFont     g_font_grande, g_font_piccolo;

static int    g_modo = MODO_NORM, g_base = BASE_DEC, g_rad = 0;

/* The expression typed so far: numbers, operators, parentheses. */
enum { T_NUM, T_OP, T_AP, T_CH };
typedef struct {
    int    t;
    double v;
    char   op;              /* + - * / ^ m(mod) & | x(xor) < (shl) > (shr) */
    char   lab[48];         /* how a number is shown when it came from a function */
} Tok;

#define TOK_MAX 64
static Tok    g_tok[TOK_MAX];
static int    g_ntok = 0;

/* The number being typed, or the last result. */
static char   g_ent[48] = "0";
static int    g_scrive = 0;      /* digits are being typed into g_ent */
static int    g_ha = 0;          /* there is a current operand */
static double g_val = 0.0;
static char   g_lab[48] = "";    /* its label, when a function made it */
static double g_mem = 0.0;
static char   g_err[64] = "";
static int    g_dopo_uguale = 0; /* the value shown is the result of «=» */

/* The adding machine, when the tape is on: see nastro(). */
static int    g_nastro = 0;       /* the tape box is on */
static double g_tot = 0.0;        /* the running total */
static int    g_tot_mostrato = 0; /* the value shown IS the total, not an operand */
static int    g_da_totale = 0;    /* the expression began from the total shown */

/* The tape: every calculation finished with «=». */
#define CRON_MAX 1000
static char  *g_cr[CRON_MAX];
static int    g_ncr = 0;

/* =============================================================================
 * NUMBERS AS TEXT
 * ============================================================================= */
static long long a32(long long v) { return (long long)(int)(unsigned int)(v & 0xFFFFFFFFll); }

static void in_base(long long v, int base, char *out, unsigned int max)
{
    unsigned int u = (unsigned int)(v & 0xFFFFFFFFll);
    char         t[40];
    int          n = 0, b = BASI_N[base];

    if (base == BASE_DEC) { snprintf(out, max, "%d", (int)u); return; }
    if (u == 0) t[n++] = '0';
    while (u) { t[n++] = "0123456789ABCDEF"[u % (unsigned int)b]; u /= (unsigned int)b; }
    if ((unsigned int)n >= max) n = (int)max - 1;
    {
        int i;
        for (i = 0; i < n; i++) out[i] = t[n - 1 - i];
        out[n] = '\0';
    }
}

static void formatta(double v, char *out, unsigned int max)
{
    if (g_modo == MODO_PROG) { in_base((long long)v, g_base, out, max); return; }
    if (v != v)                     { snprintf(out, max, "Errore"); return; }
    if (v > 1e308 || v < -1e308)    { snprintf(out, max, "Infinito"); return; }
    if (v == floor(v) && fabs(v) < 1e15) snprintf(out, max, "%.0f", v);
    else                                 snprintf(out, max, "%.12g", v);
}

/* The digits typed, read in the current base. */
static double leggi_ent(void)
{
    if (g_modo != MODO_PROG) return strtod(g_ent, 0);
    {
        const char *p = g_ent;
        int         neg = 0;
        unsigned long long u = 0;

        if (*p == '-') { neg = 1; p++; }
        for (; *p; p++) {
            int d = (*p >= '0' && *p <= '9') ? *p - '0' : (*p >= 'A' && *p <= 'F') ? *p - 'A' + 10 : 0;
            u = u * (unsigned long long)BASI_N[g_base] + (unsigned long long)d;
        }
        return (double)a32(neg ? -(long long)u : (long long)u);
    }
}

static void tok_testo(const Tok *k, char *out, unsigned int max)
{
    switch (k->t) {
    case T_NUM:
        if (k->lab[0]) snprintf(out, max, "%s", k->lab);
        else           formatta(k->v, out, max);
        break;
    case T_AP: snprintf(out, max, "("); break;
    case T_CH: snprintf(out, max, ")"); break;
    default: {
        const char *s = "?";
        switch (k->op) {
        case '+': s = " + "; break;   case '-': s = " - "; break;
        case '*': s = " * "; break;   case '/': s = " / "; break;
        case '^': s = " ^ "; break;   case 'm': s = " mod "; break;
        case '&': s = " AND "; break; case '|': s = " OR "; break;
        case 'x': s = " XOR "; break; case '<': s = " << "; break;
        case '>': s = " >> "; break;
        }
        snprintf(out, max, "%s", s);
    }
    }
}

/* The whole line being worked on: the expression and the current number. */
static void riga_corrente(char *out, unsigned int max)
{
    char t[64];
    int  i;

    out[0] = '\0';
    for (i = 0; i < g_ntok; i++) {
        tok_testo(&g_tok[i], t, sizeof(t));
        if (strlen(out) + strlen(t) + 1 < max) strcat(out, t);
    }
    if (g_scrive)      snprintf(t, sizeof(t), "%s", g_ent);
    else if (g_ha)     { if (g_lab[0]) snprintf(t, sizeof(t), "%s", g_lab); else formatta(g_val, t, sizeof(t)); }
    else if (!g_ntok)  snprintf(t, sizeof(t), "0");
    else               t[0] = '\0';
    if (strlen(out) + strlen(t) + 1 < max) strcat(out, t);
}

/* =============================================================================
 * EVALUATING: the shunting yard, then the stack
 * ============================================================================= */
static int prec(char op)
{
    if (g_modo == MODO_PROG) {
        switch (op) {
        case '|': return 1; case 'x': return 2; case '&': return 3;
        case '<': case '>': return 4;
        case '+': case '-': return 5;
        default: return 6;               /* * / mod */
        }
    }
    switch (op) {
    case '+': case '-': return 1;
    case '^': return 3;
    default: return 2;                   /* * / mod */
    }
}

static int applica(double a, double b, char op, double *r)
{
    if (g_modo == MODO_PROG) {
        long long x = a32((long long)a), y = a32((long long)b), z = 0;

        switch (op) {
        case '+': z = x + y; break;
        case '-': z = x - y; break;
        case '*': z = x * y; break;
        /* ! IN 32 BIT, NOT 64: __divdi3 is not among the libc functions a
         * program can reach. And -2147483648 / -1 is done by hand: idiv
         * would raise an exception on it instead of wrapping. */
        case '/': if (!y) { strcpy(g_err, "Divisione per zero"); return 0; }
                  z = (y == -1) ? -x : (long long)((int)x / (int)y); break;
        case 'm': if (!y) { strcpy(g_err, "Divisione per zero"); return 0; }
                  z = (y == -1) ? 0 : (long long)((int)x % (int)y); break;
        case '&': z = x & y; break;
        case '|': z = x | y; break;
        case 'x': z = x ^ y; break;
        case '<': z = (long long)((unsigned int)x << (y & 31)); break;
        case '>': z = (long long)((int)x >> (y & 31)); break;
        }
        *r = (double)a32(z);
        return 1;
    }
    switch (op) {
    case '+': *r = a + b; break;
    case '-': *r = a - b; break;
    case '*': *r = a * b; break;
    case '/': if (b == 0.0) { strcpy(g_err, "Divisione per zero"); return 0; } *r = a / b; break;
    case 'm': if (b == 0.0) { strcpy(g_err, "Divisione per zero"); return 0; } *r = fmod(a, b); break;
    case '^': *r = pow(a, b); break;
    default:  *r = 0.0;
    }
    if (*r != *r) { strcpy(g_err, "Risultato non definito"); return 0; }
    return 1;
}

static int valuta(const Tok *in, int n, double *ris)
{
    Tok    out[TOK_MAX], pila[TOK_MAX];
    double st[TOK_MAX];
    int    no = 0, np = 0, ns = 0, i;

    for (i = 0; i < n; i++) {
        const Tok *k = &in[i];

        if (k->t == T_NUM) out[no++] = *k;
        else if (k->t == T_AP) pila[np++] = *k;
        else if (k->t == T_CH) {
            while (np && pila[np - 1].t != T_AP) out[no++] = pila[--np];
            if (np) np--;
        } else {
            while (np && pila[np - 1].t == T_OP &&
                   (prec(pila[np - 1].op) > prec(k->op) ||
                    (prec(pila[np - 1].op) == prec(k->op) && k->op != '^')))
                out[no++] = pila[--np];
            pila[np++] = *k;
        }
    }
    while (np) { if (pila[np - 1].t == T_OP) out[no++] = pila[np - 1]; np--; }

    for (i = 0; i < no; i++) {
        if (out[i].t == T_NUM) { st[ns++] = out[i].v; continue; }
        if (ns < 2) { strcpy(g_err, "Espressione incompleta"); return 0; }
        if (!applica(st[ns - 2], st[ns - 1], out[i].op, &st[ns - 2])) return 0;
        ns--;
    }
    if (ns != 1) { strcpy(g_err, "Espressione incompleta"); return 0; }
    *ris = st[0];
    return 1;
}

/* =============================================================================
 * THE TAPE
 * ============================================================================= */
static void cron_aggiungi(const char *s)
{
    if (g_ncr == CRON_MAX) {
        free(g_cr[0]);
        memmove(g_cr, g_cr + 1, (CRON_MAX - 1) * sizeof(char *));
        g_ncr--;
    }
    g_cr[g_ncr] = strdup(s);
    if (g_cr[g_ncr]) g_ncr++;
    if (g_lcron) {
        ex_list_add(g_lcron, s);
        ex_list_select(g_lcron, ex_list_count(g_lcron) - 1);
        ex_default_proc(g_fcron, EXM_PAINT, 0, 0);
        ex_update(g_fcron);
    }
}

static void cron_svuota(void)
{
    int i;

    for (i = 0; i < g_ncr; i++) free(g_cr[i]);
    g_ncr = 0;
    if (g_lcron) {
        ex_list_clear(g_lcron);
        ex_default_proc(g_fcron, EXM_PAINT, 0, 0);
        ex_update(g_fcron);
    }
}

/* =============================================================================
 * DRAWING THE DISPLAY
 * ============================================================================= */
static void scrivi_destra(ExFont fo, int x, int y, int w, const char *s, unsigned int c)
{
    const char *p = s;
    int         l = ex_text_width(fo, p);

    /* Too long: the end is what matters, the beginning goes. */
    while (*p && l > w) { p++; l = ex_text_width(fo, p); }
    ex_draw_text_font(g_f, fo, x + w - l, y, p, c);
}

static void display_disegna(void)
{
    int  x = MARGINE, y = Y_DISPLAY, w = g_w - 2 * MARGINE, i, hp, hg;
    char riga[256];

    hp = g_font_piccolo ? ex_font_height(g_font_piccolo) : 16;
    hg = g_font_grande  ? ex_font_height(g_font_grande)  : 16;
    if (hp > 18) hp = 18;

    ex_fill_rect(g_f, x, y, w, H_DISPLAY, 0x00E8F0E0);
    ex_draw_sunken(g_f, x, y, w, H_DISPLAY);

    /* Three lines of tape, oldest at the top. */
    for (i = 0; i < 3; i++) {
        int k = g_ncr - 3 + i;
        if (k >= 0) scrivi_destra(g_font_piccolo, x + 40, y + 4 + i * hp, w - 48, g_cr[k], 0x00505050);
    }

    /* What is on the left: memory, angles, base. */
    if (g_mem != 0.0) ex_draw_text(g_f, x + 6, y + H_DISPLAY - 22, "M", EX_BLACK);
    if (g_modo == MODO_SCI)  ex_draw_text(g_f, x + 6, y + 4, g_rad ? "RAD" : "DEG", 0x00505050);
    if (g_modo == MODO_PROG) ex_draw_text(g_f, x + 6, y + 4, BASI_NOME[g_base], 0x00505050);

    if (g_err[0]) snprintf(riga, sizeof(riga), "%s", g_err);
    else          riga_corrente(riga, sizeof(riga));
    scrivi_destra(g_font_grande, x + 24, y + H_DISPLAY - hg - 4, w - 32, riga,
                  g_err[0] ? EX_RED : EX_BLACK);
}

static void ridisegna_display(void)
{
    display_disegna();
    ex_update(g_f);
}

static void ridisegna(void)
{
    ex_default_proc(g_f, EXM_PAINT, 0, 0);
    display_disegna();
    ex_update(g_f);
}

/* =============================================================================
 * THE ENGINE: one code at a time, from a button or a key
 * ============================================================================= */
static void tutto_azzera(void)
{
    g_ntok = 0;
    g_scrive = 0; g_ha = 0; g_val = 0.0; g_lab[0] = '\0';
    strcpy(g_ent, "0");
    g_err[0] = '\0';
}

/* The current operand's value: what is typed, the result shown, or zero. */
static double operando(void)
{
    if (g_scrive) return leggi_ent();
    if (g_ha)     return g_val;
    return 0.0;
}

static void metti_valore(double v, const char *lab)
{
    if (g_modo == MODO_PROG) v = (double)a32((long long)v);
    g_val = v;
    g_ha = 1;
    g_scrive = 0;
    snprintf(g_lab, sizeof(g_lab), "%s", lab ? lab : "");
}

static int tok_push(int t, double v, char op, const char *lab)
{
    if (g_ntok >= TOK_MAX) { strcpy(g_err, "Espressione troppo lunga"); return 0; }
    g_tok[g_ntok].t = t;
    g_tok[g_ntok].v = v;
    g_tok[g_ntok].op = op;
    snprintf(g_tok[g_ntok].lab, sizeof(g_tok[g_ntok].lab), "%s", lab ? lab : "");
    g_ntok++;
    return 1;
}

/* The current number goes into the expression. */
static void spingi_operando(void)
{
    if (g_scrive) { g_val = leggi_ent(); g_lab[0] = '\0'; }
    tok_push(T_NUM, g_val, 0, g_lab);
    g_scrive = 0; g_ha = 0; g_lab[0] = '\0';
}

static int ultimo(void) { return g_ntok ? g_tok[g_ntok - 1].t : -1; }

static void cifra(const char *d)
{
    int max = g_modo == MODO_PROG ? BASI_CIFRE[g_base] : 16;

    if (g_err[0]) tutto_azzera();
    if (!g_scrive) {
        if (ultimo() == T_CH) return;           /* ")" then a number: no */
        strcpy(g_ent, "0");
        g_scrive = 1; g_ha = 1; g_lab[0] = '\0';
    }
    for (; *d; d++) {
        int l = (int)strlen(g_ent), cifre = l - (g_ent[0] == '-') - (strchr(g_ent, '.') != 0);

        if (*d == '.') {
            if (g_modo == MODO_PROG || strchr(g_ent, '.')) continue;
            if (l + 1 < (int)sizeof(g_ent)) { g_ent[l] = '.'; g_ent[l + 1] = '\0'; }
            continue;
        }
        if (strcmp(g_ent, "0") == 0) { g_ent[0] = *d; continue; }
        if (strcmp(g_ent, "-0") == 0) { g_ent[1] = *d; continue; }
        if (cifre >= max) continue;
        g_ent[l] = *d; g_ent[l + 1] = '\0';
        /* In decimal a 32-bit number stops at its range. */
        if (g_modo == MODO_PROG && g_base == BASE_DEC &&
            fabs(strtod(g_ent, 0)) > 2147483648.0) g_ent[l] = '\0';
    }
}

static void operatore(char op)
{
    if (g_err[0]) return;
    if (g_scrive || g_ha) spingi_operando();
    else if (ultimo() == T_OP) { g_tok[g_ntok - 1].op = op; return; }
    else if (ultimo() != T_CH) tok_push(T_NUM, 0.0, 0, 0);
    tok_push(T_OP, 0.0, op, 0);
}

static void uguale(void)
{
    char   espr[256], r[64], riga[340];
    double v;
    int    aperte = 0, i;

    if (g_err[0]) return;
    if (g_scrive || g_ha) spingi_operando();
    else if (ultimo() == T_OP) g_ntok--;
    if (g_ntok == 0) return;
    for (i = 0; i < g_ntok; i++) aperte += g_tok[i].t == T_AP ? 1 : g_tok[i].t == T_CH ? -1 : 0;
    while (aperte-- > 0) tok_push(T_CH, 0.0, 0, 0);

    riga_corrente(espr, sizeof(espr));
    if (!valuta(g_tok, g_ntok, &v)) { g_ntok = 0; return; }
    if (g_modo != MODO_PROG && fabs(v) < 1e-13) v = 0.0;

    formatta(v, r, sizeof(r));
    if (g_modo == MODO_PROG) snprintf(riga, sizeof(riga), "%s = %s  (%s)", espr, r, BASI_NOME[g_base]);
    else                     snprintf(riga, sizeof(riga), "%s = %s", espr, r);
    cron_aggiungi(riga);

    g_ntok = 0;
    metti_valore(v, 0);
    g_dopo_uguale = 1;
}

/* =============================================================================
 * THE ADDING MACHINE: «+» and «-» with the tape on
 *
 * A number followed by + or - goes into the total, and the total is shown at
 * once: the key is operator and «=» together. A product or a quotient typed
 * before is finished first (12 x 3 + adds 36). + with nothing new typed is
 * the subtotal. An expression started FROM the total shown (the total times
 * 2) REPLACES the total, or it would count twice.
 * ============================================================================= */
static void nastro(char op)
{
    char   espr[256], r[64], riga[340];
    double v;
    int    nuovo, aperte = 0, i;

    if (g_err[0]) return;
    nuovo = g_scrive || g_ntok > 0 || (g_ha && !g_tot_mostrato);

    if (!nuovo) {
        formatta(g_tot, r, sizeof(r));
        snprintf(riga, sizeof(riga), "%s  subtotale", r);
        cron_aggiungi(riga);
        metti_valore(g_tot, 0);
        g_tot_mostrato = 1;
        return;
    }

    if (g_ntok > 0) {
        if (g_scrive || g_ha) spingi_operando();
        else if (ultimo() == T_OP) g_ntok--;
        for (i = 0; i < g_ntok; i++) aperte += g_tok[i].t == T_AP ? 1 : g_tok[i].t == T_CH ? -1 : 0;
        while (aperte-- > 0) tok_push(T_CH, 0.0, 0, 0);
        riga_corrente(espr, sizeof(espr));
        if (!valuta(g_tok, g_ntok, &v)) { g_ntok = 0; return; }
        if (g_modo != MODO_PROG && fabs(v) < 1e-13) v = 0.0;
        formatta(v, r, sizeof(r));
        snprintf(riga, sizeof(riga), "%s = %s", espr, r);
        cron_aggiungi(riga);
        g_ntok = 0;
    } else {
        v = operando();
    }

    if (g_da_totale) g_tot = v;
    else             g_tot += op == '-' ? -v : v;
    if (g_modo == MODO_PROG) g_tot = (double)a32((long long)g_tot);
    if (g_modo != MODO_PROG && fabs(g_tot) < 1e-13) g_tot = 0.0;

    formatta(v, r, sizeof(r));
    if (g_da_totale) snprintf(riga, sizeof(riga), "%s  totale", r);
    else             snprintf(riga, sizeof(riga), "%s %c", r, op);
    cron_aggiungi(riga);

    metti_valore(g_tot, 0);
    g_tot_mostrato = 1;
    g_da_totale = 0;
}

static void nastro_azzera(void)
{
    g_tot = 0.0;
    g_tot_mostrato = 0;
    g_da_totale = 0;
}

static double fattoriale(double n)
{
    double r = 1.0;
    int    i;

    if (n < 0 || n != floor(n) || n > 170) { strcpy(g_err, "n! vuole un intero da 0 a 170"); return 0; }
    for (i = 2; i <= (int)n; i++) r *= i;
    return r;
}

/* A function of the current number: it acts at once. */
static void funzione(const char *f)
{
    double x, r = 0.0, ang;
    char   xs[48], lab[48];

    if (g_err[0]) return;
    if (!g_scrive && !g_ha && ultimo() == T_CH) return;     /* a function of a group: no */
    x = operando();
    if (g_scrive) snprintf(xs, sizeof(xs), "%s", g_ent);
    else if (g_ha && g_lab[0]) snprintf(xs, sizeof(xs), "%s", g_lab);
    else formatta(x, xs, sizeof(xs));

    ang = g_rad ? x : x * M_PI / 180.0;

    if      (!strcmp(f, "sqrt")) { if (x < 0) { strcpy(g_err, "Radice di un negativo"); return; } r = sqrt(x); }
    else if (!strcmp(f, "1/x"))  { if (x == 0) { strcpy(g_err, "Divisione per zero"); return; } r = 1.0 / x; }
    else if (!strcmp(f, "x^2"))  r = x * x;
    else if (!strcmp(f, "x^3"))  r = x * x * x;
    else if (!strcmp(f, "cbrt")) r = x < 0 ? -pow(-x, 1.0 / 3.0) : pow(x, 1.0 / 3.0);
    else if (!strcmp(f, "sin"))  r = sin(ang);
    else if (!strcmp(f, "cos"))  r = cos(ang);
    else if (!strcmp(f, "tan"))  {
        if (fabs(cos(ang)) < 1e-12) { strcpy(g_err, "Tangente non definita"); return; }
        r = tan(ang);
    }
    else if (!strcmp(f, "asin") || !strcmp(f, "acos")) {
        if (x < -1 || x > 1) { strcpy(g_err, "Fuori da -1..1"); return; }
        r = !strcmp(f, "asin") ? asin(x) : acos(x);
        if (!g_rad) r = r * 180.0 / M_PI;
    }
    else if (!strcmp(f, "atan")) { r = atan(x); if (!g_rad) r = r * 180.0 / M_PI; }
    else if (!strcmp(f, "sinh")) r = sinh(x);
    else if (!strcmp(f, "cosh")) r = cosh(x);
    else if (!strcmp(f, "tanh")) r = tanh(x);
    else if (!strcmp(f, "ln") || !strcmp(f, "log")) {
        if (x <= 0) { strcpy(g_err, "Logaritmo di un non positivo"); return; }
        r = !strcmp(f, "ln") ? log(x) : log10(x);
    }
    else if (!strcmp(f, "e^x"))  r = exp(x);
    else if (!strcmp(f, "10^x")) r = pow(10.0, x);
    else if (!strcmp(f, "abs"))  r = fabs(x);
    else if (!strcmp(f, "n!"))   { r = fattoriale(x); if (g_err[0]) return; }
    else if (!strcmp(f, "not"))  r = (double)a32(~a32((long long)x));
    else if (!strcmp(f, "%")) {
        /* a + b % = a + a*b/100, as on an office calculator; alone, b/100. */
        if (g_ntok >= 2 && g_tok[g_ntok - 1].t == T_OP &&
            (g_tok[g_ntok - 1].op == '+' || g_tok[g_ntok - 1].op == '-') &&
            g_tok[g_ntok - 2].t == T_NUM)
            r = g_tok[g_ntok - 2].v * x / 100.0;
        else
            r = x / 100.0;
        metti_valore(r, 0);
        return;
    }
    if (r != r) { strcpy(g_err, "Risultato non definito"); return; }
    if (g_modo != MODO_PROG && fabs(r) < 1e-13) r = 0.0;

    if (!strcmp(f, "x^2"))       snprintf(lab, sizeof(lab), "sqr(%s)", xs);
    else if (!strcmp(f, "x^3"))  snprintf(lab, sizeof(lab), "cube(%s)", xs);
    else if (!strcmp(f, "1/x"))  snprintf(lab, sizeof(lab), "1/(%s)", xs);
    else if (!strcmp(f, "e^x"))  snprintf(lab, sizeof(lab), "e^(%s)", xs);
    else if (!strcmp(f, "10^x")) snprintf(lab, sizeof(lab), "10^(%s)", xs);
    else if (!strcmp(f, "n!"))   snprintf(lab, sizeof(lab), "fact(%s)", xs);
    else if (!strcmp(f, "not"))  snprintf(lab, sizeof(lab), "NOT(%s)", xs);
    else                         snprintf(lab, sizeof(lab), "%s(%s)", f, xs);
    metti_valore(r, lab);
}

static void esegui(const char *c);

static void costante(double v, const char *nome)
{
    if (g_err[0]) tutto_azzera();
    if (ultimo() == T_CH) return;
    metti_valore(v, nome);
}

static void esegui(const char *c)
{
    /* ! DOPO «=» UNA PARENTESI COMINCIA UN CALCOLO NUOVO, come una cifra: il
     * risultato resta per chi lo usa con un operatore o una funzione, ma
     * «(1+2)*3» battuto dopo non deve diventare «1024 * (1+2) * 3». */
    int dopo = g_dopo_uguale;

    g_dopo_uguale = 0;

    /* The adding machine takes + - and = for itself (see nastro). */
    if (g_nastro) {
        if (!strcmp(c, "+") || !strcmp(c, "=")) { nastro('+'); return; }
        if (!strcmp(c, "-"))                     { nastro('-'); return; }
        if (!strcmp(c, "C")) nastro_azzera();
        else if (g_tot_mostrato && !g_scrive &&
                 (!strcmp(c, "*") || !strcmp(c, "/") || !strcmp(c, "^") || !strcmp(c, "mod") ||
                  !strcmp(c, "and") || !strcmp(c, "or") || !strcmp(c, "xor") ||
                  !strcmp(c, "shl") || !strcmp(c, "shr")))
            g_da_totale = 1;
        g_tot_mostrato = 0;
    }

    if (dopo && !strcmp(c, "(")) { g_ha = 0; g_scrive = 0; g_lab[0] = '\0'; }
    if (!strcmp(c, "C"))  { tutto_azzera(); return; }
    if (!strcmp(c, "CE")) { g_err[0] = '\0'; g_scrive = 0; g_ha = 0; g_lab[0] = '\0'; strcpy(g_ent, "0"); return; }

    if ((c[0] >= '0' && c[0] <= '9') || !strcmp(c, ".")) { cifra(c); return; }
    if (g_modo == MODO_PROG && c[1] == '\0' && c[0] >= 'A' && c[0] <= 'F') { cifra(c); return; }
    if (!strcmp(c, "Cx")) { cifra("C"); return; }       /* the hex digit C */

    if (!strcmp(c, "<-")) {
        if (g_err[0]) { tutto_azzera(); return; }
        if (g_scrive) {
            size_t l = strlen(g_ent);
            if (l > 1 && !(l == 2 && g_ent[0] == '-')) g_ent[l - 1] = '\0';
            else strcpy(g_ent, "0");
        }
        return;
    }
    if (!strcmp(c, "+/-")) {
        if (g_err[0]) return;
        if (g_scrive) {
            if (g_ent[0] == '-') memmove(g_ent, g_ent + 1, strlen(g_ent));
            else if (strlen(g_ent) + 1 < sizeof(g_ent)) { memmove(g_ent + 1, g_ent, strlen(g_ent) + 1); g_ent[0] = '-'; }
        } else if (g_ha) {
            char lab[48], xs[40];
            if (g_lab[0]) snprintf(xs, sizeof(xs), "%s", g_lab); else formatta(g_val, xs, sizeof(xs));
            snprintf(lab, sizeof(lab), "-(%s)", xs);
            metti_valore(-g_val, g_lab[0] ? lab : 0);
        }
        return;
    }

    if (!strcmp(c, "+") || !strcmp(c, "-") || !strcmp(c, "*") || !strcmp(c, "/")) { operatore(c[0]); return; }
    if (!strcmp(c, "^"))   { operatore('^'); return; }
    if (!strcmp(c, "mod")) { operatore('m'); return; }
    if (!strcmp(c, "and")) { operatore('&'); return; }
    if (!strcmp(c, "or"))  { operatore('|'); return; }
    if (!strcmp(c, "xor")) { operatore('x'); return; }
    if (!strcmp(c, "shl")) { operatore('<'); return; }
    if (!strcmp(c, "shr")) { operatore('>'); return; }
    if (!strcmp(c, "="))   { uguale(); return; }

    if (!strcmp(c, "(")) {
        if (g_err[0]) return;
        if (g_scrive || g_ha) { spingi_operando(); tok_push(T_OP, 0, '*', 0); }
        else if (ultimo() == T_CH) tok_push(T_OP, 0, '*', 0);
        tok_push(T_AP, 0, 0, 0);
        return;
    }
    if (!strcmp(c, ")")) {
        int aperte = 0, i;
        if (g_err[0]) return;
        for (i = 0; i < g_ntok; i++) aperte += g_tok[i].t == T_AP ? 1 : g_tok[i].t == T_CH ? -1 : 0;
        if (aperte <= 0) return;
        if (g_scrive || g_ha) spingi_operando();
        else if (ultimo() == T_OP || ultimo() == T_AP) return;
        tok_push(T_CH, 0, 0, 0);
        return;
    }

    if (!strcmp(c, "MC")) { g_mem = 0.0; return; }
    if (!strcmp(c, "MR")) { costante(g_mem, 0); return; }
    if (!strcmp(c, "M+") || !strcmp(c, "M-")) {
        double v = operando();
        if (g_err[0]) return;
        if (!g_scrive && !g_ha && ultimo() != T_CH && g_ntok == 0) v = g_val;
        g_mem += c[1] == '+' ? v : -v;
        if (g_modo == MODO_PROG) g_mem = (double)a32((long long)g_mem);
        if (g_scrive) metti_valore(v, 0);
        return;
    }
    if (!strcmp(c, "pi")) { costante(M_PI, "pi"); return; }
    if (!strcmp(c, "e"))  { costante(M_E, "e"); return; }
    if (!strcmp(c, "ang")) return;                        /* handled by the window */

    funzione(c);
}

/* =============================================================================
 * THE MODES
 * ============================================================================= */
static const char *config_file(int crea)
{
    static char f[200];
    const char *casa = getenv("HOME");
    char        d[200];

    if (!casa || !casa[0] || strcmp(casa, "/") == 0)
        casa = (getuid() == 0) ? "/root" : "";
    if (crea) {
        if (casa[0]) mkdir(casa, 0700);
        snprintf(d, sizeof(d), "%s/.exwin", casa);        mkdir(d, 0755);
        snprintf(d, sizeof(d), "%s/.exwin/config", casa); mkdir(d, 0755);
    }
    snprintf(f, sizeof(f), "%s/.exwin/config/calctor.cfg", casa);
    return f;
}

static void opzioni_salva(void)
{
    char t[256];
    int  fd = open(config_file(1), O_WRONLY | O_CREAT | O_TRUNC, 0644), n;

    if (fd < 0) return;         /* from the CD there is no profile to write */
    n = snprintf(t, sizeof(t),
                 "# Calctor: le scelte. Le riscrive il programma.\n"
                 "modo       = %s\nbase       = %s\nangoli     = %s\ncronologia = %s\n",
                 MODI_CFG[g_modo], BASI_CFG[g_base], g_rad ? "rad" : "deg",
                 g_fcron ? "si" : "no");
    write(fd, t, (unsigned int)n);
    close(fd);
}

static int g_cron_all_avvio = 0;

static void opzioni_leggi(void)
{
    char buf[512], *p;
    int  fd = open(config_file(0), O_RDONLY, 0), n, i;

    if (fd < 0) return;
    n = (int)read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;
    buf[n] = '\0';
    if ((p = strstr(buf, "modo")) && (p = strchr(p, '=')))
        for (i = 0; i < 3; i++) if (strstr(p, MODI_CFG[i]) == p + strspn(p, "= \t")) g_modo = i;
    if ((p = strstr(buf, "base")) && (p = strchr(p, '=')))
        for (i = 0; i < 4; i++) if (strstr(p, BASI_CFG[i]) == p + strspn(p, "= \t")) g_base = i;
    if ((p = strstr(buf, "angoli")) && (p = strchr(p, '=')))
        g_rad = strstr(p, "rad") == p + strspn(p, "= \t");
    if ((p = strstr(buf, "cronologia")) && (p = strchr(p, '=')))
        g_cron_all_avvio = strstr(p, "si") == p + strspn(p, "= \t");
}

/* Which buttons exist, and which are on, in this mode and base. */
static void tasti_aggiorna(void)
{
    int i;

    for (i = 0; i < N_TASTI; i++) {
        Tasto *t = &g_t[i];
        int    vis = t->blocco == 0 || (t->blocco == 1 && g_modo == MODO_SCI) ||
                     (t->blocco == 2 && g_modo == MODO_PROG) || (t->blocco == 3 && g_nastro);

        /* With the tape on, - + = give way to the two tall keys. */
        if (t->blocco == 0 && g_nastro &&
            (!strcmp(t->cod, "+") || !strcmp(t->cod, "-") || !strcmp(t->cod, "="))) vis = 0;
        int    si = 1;

        if (!t->c) continue;
        ex_show(t->c, vis);
        if (g_modo == MODO_PROG) {
            const char *c = t->cod;
            int d = -1;

            if (c[0] >= '0' && c[0] <= '9') d = c[0] - '0';
            else if (t->blocco == 2 && c[1] == '\0' && c[0] >= 'A' && c[0] <= 'F') d = c[0] - 'A' + 10;
            else if (!strcmp(c, "Cx")) d = 12;
            if (d >= 0) si = d < BASI_N[g_base];
            if (!strcmp(c, "00")) si = 1;
            if (!strcmp(c, ".") || !strcmp(c, "sqrt") || !strcmp(c, "1/x") || !strcmp(c, "%")) si = 0;
        }
        ex_enable(t->c, si);
    }
    if (g_riq_base) {
        ex_show(g_riq_base, g_modo == MODO_PROG);
        for (i = 0; i < 4; i++) ex_show(g_r_base[i], g_modo == MODO_PROG);
    }
    for (i = 0; i < N_TASTI; i++)
        if (!strcmp(g_t[i].cod, "ang") && g_t[i].c) ex_set_text(g_t[i].c, g_rad ? "RAD" : "DEG");
}

/* The number shown, carried into the new mode or base. */
static void porta_valore(void)
{
    double v = operando();

    /* The expression stays: its numbers are values, and they are shown in
     * the new base too. */
    if (g_scrive || g_ha) metti_valore(g_modo == MODO_PROG ? trunc(v) : v, 0);
    g_err[0] = '\0';
}

static void modo_imposta(int m)
{
    int w;

    if (m == g_modo) { ex_set_checked(g_r_modo[m], 1); ridisegna(); return; }
    {
        double v = operando();
        int    aveva = g_scrive || g_ha;
        g_modo = m;
        if (aveva) metti_valore(m == MODO_PROG ? trunc(v) : v, 0);
        g_ntok = 0;
        g_err[0] = '\0';
    }
    ex_set_checked(g_r_modo[m], 1);
    tasti_aggiorna();
    w = m == MODO_NORM ? W_NORMALE : W_LARGA;
    if (w != g_w) ex_resize(g_f, w, H_FIN);     /* EXM_SIZE redraws */
    else          ridisegna();
    opzioni_salva();
}

static void base_imposta(int b)
{
    porta_valore();
    g_base = b;
    ex_set_checked(g_r_base[b], 1);
    tasti_aggiorna();
    ridisegna();
    opzioni_salva();
}

/* =============================================================================
 * THE TAPE WINDOW
 * ============================================================================= */
static long cron_proc(ExWindow f, unsigned int msg, unsigned int wp, long lp);

static void cron_apri(void)
{
    unsigned int sw = 0, sh = 0;
    int          i;

    if (g_fcron) return;
    ex_screen_size(&sw, &sh);
    g_fcron = ex_create("window", "Cronologia di Calctor",
                      EX_CAPTION | EX_BORDER | EX_CLOSEBOX | EX_RESIZABLE,
                      (int)sw > 360 ? (int)sw - 340 : 0, 40, 320, 360, 0, 0, cron_proc);
    if (!g_fcron) return;
    g_lcron = ex_create("list", "", EX_CHILD, 4, 4, 312, 352, g_fcron, 1, 0);
    for (i = 0; i < g_ncr; i++) ex_list_add(g_lcron, g_cr[i]);
    if (g_ncr) ex_list_select(g_lcron, (unsigned int)g_ncr - 1);
    ex_default_proc(g_fcron, EXM_PAINT, 0, 0);
    ex_update(g_fcron);
    /* ! IL FUOCO TORNA ALLA CALCOLATRICE: il nastro nasce per ultimo e se lo
     * prendeva, e le cifre battute andavano a lui — da tastiera la
     * calcolatrice non rispondeva piu' (visto in QEMU, 28 settembre 2026). */
    ex_activate(g_f);
}

static void cron_chiudi(void)
{
    if (!g_fcron) return;
    ex_destroy(g_fcron);
    g_fcron = g_lcron = 0;
}

static long cron_proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CLOSE:
        cron_chiudi();
        ex_set_checked(g_cron, 0);
        ridisegna();
        opzioni_salva();
        return 0;
    case EXM_SIZE:
        if (g_lcron) ex_resize(g_lcron, EX_X(lp) - 8, EX_Y(lp) - 8);
        break;
    case EXM_COMMAND:
        return 0;               /* choosing a line of the tape does nothing */
    }
    return ex_default_proc(f, msg, wp, lp);
}

/* =============================================================================
 * FILE, INFO
 * ============================================================================= */
static void salva(void)
{
    char        p[256], t[64];
    const char *casa = getenv("HOME");
    int         fd, i;
    size_t      l;

    if (g_ncr == 0) { ex_dlg_avviso("Salva", "La cronologia e' vuota: non c'e' niente da salvare."); return; }
    snprintf(p, sizeof(p), "%s/cronologia.txt",
             (casa && casa[0] && strcmp(casa, "/")) ? casa : "");
    if (!ex_dlg_salva(p, sizeof(p))) return;
    l = strlen(p);
    if (l < 4 || strcmp(p + l - 4, ".txt") != 0) {
        if (l + 4 >= sizeof(p)) return;
        strcat(p, ".txt");
    }
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { ex_dlg_avviso("Salva", "Il file non si crea (sistema in sola lettura?)."); return; }
    for (i = 0; i < g_ncr; i++) {
        write(fd, g_cr[i], (unsigned int)strlen(g_cr[i]));
        write(fd, "\n", 1);
    }
    close(fd);
    snprintf(t, sizeof(t), "Salvate %d righe.", g_ncr);
    ex_dlg_avviso("Salva", t);
}

static void informazioni(void)
{
    char t[640];

    exinfo_testo(t, sizeof(t), "Calctor", VERSIONE_APP,
                 "La calcolatrice di ExWin: normale, scientifica e "
                 "programmatore, con la cronologia dei calcoli.");
    ex_dlg_avviso("Informazioni su", t);
}

static void istruzioni(void)
{
    ex_dlg_testo("Istruzioni di Calctor",
        "LE MODALITA'\n"
        "I tre cerchi sotto il menu (o Opzioni > Modalita) scelgono: "
        "Normale, Scientifica, Programmatore. La scelta si ricorda.\n"
        "\n"
        "IL DISPLAY\n"
        "Quattro righe: le ultime tre operazioni finite, e sotto quella "
        "che si sta scrivendo. A sinistra: M se la memoria non e' vuota; "
        "DEG o RAD in scientifica; la base in programmatore.\n"
        "\n"
        "COME SI CALCOLA\n"
        "Si scrive l'espressione e si preme =. Le precedenze valgono: "
        "2 + 3 * 4 = da' 14. Le funzioni (radice, 1/x, x^2, sin...) "
        "agiscono subito sul numero appena scritto. Il % dopo + o - "
        "calcola la percentuale del primo numero: 200 + 10 % = da' 220.\n"
        "C azzera tutto, CE solo il numero in corso, <- cancella l'ultima "
        "cifra, +/- cambia il segno.\n"
        "MC svuota la memoria, MR la richiama, M+ e M- ci sommano o "
        "sottraggono il numero mostrato.\n"
        "\n"
        "SCIENTIFICA\n"
        "DEG/RAD: gradi o radianti per sin, cos, tan e inverse. "
        "x^y e mod sono operatori; ( e ) aprono e chiudono i gruppi "
        "(quelli rimasti aperti li chiude =). n! vale fino a 170.\n"
        "\n"
        "PROGRAMMATORE\n"
        "Numeri interi a 32 bit con segno. BIN, OTT, DEC ed ESA scelgono "
        "la base: il numero mostrato si converte, e le cifre che nella "
        "base non esistono si spengono. AND, OR, XOR, NOT, << e >> "
        "lavorano sui bit; / e mod danno quoziente e resto interi.\n"
        "\n"
        "LA CRONOLOGIA\n"
        "La casella Cronologia apre una finestra che si sposta, con "
        "tutte le operazioni fatte, come il rotolo di carta di una "
        "calcolatrice da ufficio. File > Nuovo la svuota, File > Salva "
        "la scrive in un file di testo (.txt).\n"
        "Con la Cronologia accesa Calctor diventa una calcolatrice da "
        "ufficio: il tasto = sparisce, + e - diventano due tasti alti, e "
        "ognuno somma (o sottrae) il numero battuto al totale e mostra "
        "subito il totale. 12 * 3 + chiude il prodotto e somma 36. "
        "Premere + di nuovo senza un numero nuovo scrive il subtotale. "
        "Invio e = dalla tastiera valgono +. C azzera anche il totale.\n"
        "\n"
        "LA TASTIERA\n"
        "Cifre, + - * / ^ % ( ) e il punto (o la virgola). Invio o = "
        "calcola, Backspace cancella una cifra, Esc azzera tutto, Canc "
        "azzera il numero in corso. In programmatore anche A..F, & | ~. "
        "Ctrl+N nuovo, Ctrl+S salva, Ctrl+Q esce.");
}

/* =============================================================================
 * THE WINDOW
 * ============================================================================= */
static void premi(const char *cod)
{
    if (!strcmp(cod, "ang")) {
        g_rad = !g_rad;
        tasti_aggiorna();
        ridisegna();
        opzioni_salva();
        return;
    }
    esegui(cod);
    ridisegna_display();
}

static const char *da_tasto(unsigned int k)
{
    static char una[2];
    unsigned int c = k & KBD_KEY_MASK;

    if (c >= '0' && c <= '9') { una[0] = (char)c; una[1] = 0; return una; }
    if (g_modo == MODO_PROG) {
        if (c >= 'a' && c <= 'f') c -= 32;
        if (c >= 'A' && c <= 'F') { una[0] = (char)c; una[1] = 0; return c == 'C' ? "Cx" : una; }
        if (c == '&') return "and";
        if (c == '|') return "or";
        if (c == '~') return "not";
        if (c == '<') return "shl";
        if (c == '>') return "shr";
    }
    switch (c) {
    case '+': return "+";   case '-': return "-";
    case '*': return "*";   case '/': return "/";
    case '^': return g_modo == MODO_SCI ? "^" : 0;
    case '%': return g_modo == MODO_PROG ? "mod" : "%";
    case '(': return "(";   case ')': return ")";
    case '.': case ',': return ".";
    case '!': return g_modo == MODO_SCI ? "n!" : 0;
    case '=': case '\n': case '\r': return "=";
    case '\b': return "<-";
    case 27:  return "C";
    case KBD_K_DEL: return "CE";
    }
    return 0;
}

static long proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CLOSE:
        ex_quit(0);
        return 0;

    case EXM_PAINT:
        ex_default_proc(f, msg, wp, lp);
        display_disegna();
        ex_update(f);
        return 0;

    case EXM_SIZE:
        g_w = EX_X(lp);
        g_h = EX_Y(lp);
        ex_default_proc(f, msg, wp, lp);
        ridisegna();
        return 0;

    case EXM_COMMAND:
        switch (wp) {
        case ID_NUOVO:  cron_svuota(); tutto_azzera(); ridisegna(); return 0;
        /* After a dialog the window is drawn again: the toolkit repaints
         * only its own controls, and the display would stay blank. */
        case ID_SALVA:  salva(); ridisegna(); return 0;
        case ID_ESCI:   ex_quit(0); return 0;
        case ID_M_NORM: case ID_R_NORM: modo_imposta(MODO_NORM); break;
        case ID_M_SCI:  case ID_R_SCI:  modo_imposta(MODO_SCI);  break;
        case ID_M_PROG: case ID_R_PROG: modo_imposta(MODO_PROG); break;
        case ID_INFO:   informazioni(); ridisegna(); return 0;
        case ID_ISTR:   istruzioni(); ridisegna(); return 0;
        case ID_CRON:
            if (ex_is_checked(g_cron)) cron_apri(); else cron_chiudi();
            /* ! THE KEYS CHANGE WITH THE BOX, and so does the arithmetic:
             * what was being typed under the other rule starts over. */
            g_nastro = ex_is_checked(g_cron);
            tutto_azzera();
            nastro_azzera();
            tasti_aggiorna();
            opzioni_salva();
            ridisegna();
            break;
        case ID_B_BIN: case ID_B_OTT: case ID_B_DEC: case ID_B_ESA:
            base_imposta((int)(wp - ID_B_BIN));
            break;
        default:
            if (wp >= ID_TASTO && wp < ID_TASTO + (unsigned int)N_TASTI)
                premi(g_t[wp - ID_TASTO].cod);
            break;
        }
        /* ! THE FOCUS GOES BACK TO THE WINDOW after every button: a button
         * keeping it would take Enter for itself, and typing 2 + 3 Enter
         * would press «+» again instead of giving 5. */
        ex_clear_focus(g_f);
        return 0;

    case EXM_KEY:
        if (wp & KBD_MOD_CTRL) {
            unsigned int c = wp & KBD_KEY_MASK;
            if (c == 'n' || c == 'N') { cron_svuota(); tutto_azzera(); ridisegna(); return 0; }
            if (c == 's' || c == 'S') { salva(); ridisegna(); return 0; }
            if (c == 'q' || c == 'Q') { ex_quit(0); return 0; }
        } else {
            const char *cod = da_tasto(wp);
            if (cod) {
                int i;
                /* A switched-off digit stays off from the keyboard too. */
                for (i = 0; i < N_TASTI; i++)
                    if (!strcmp(g_t[i].cod, cod) && g_t[i].c && g_t[i].blocco == 0 &&
                        g_modo == MODO_PROG && (cod[0] >= '0' && cod[0] <= '9') &&
                        cod[0] - '0' >= BASI_N[g_base]) return 0;
                premi(cod);
                return 0;
            }
        }
        break;
    }
    return ex_default_proc(f, msg, wp, lp);
}

int main(int argc, char **argv)
{
    ExMsg m;
    int   i;

    (void)argc; (void)argv;
    opzioni_leggi();

    /* ! LARGA ALLA NASCITA, e stretta dopo se la modalita' e' la normale: i
     * controlli si creano dentro la finestra che c'e', e un pulsante nato
     * fuori dal bordo non si vede nemmeno quando la finestra si allarga. */
    g_w = W_LARGA;
    g_f = ex_create("window", "Calctor", EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                  EX_AUTO, EX_AUTO, g_w, H_FIN, 0, 0, proc);
    if (!g_f) {
        printf("calctor: il server a finestre non risponde.\n");
        printf("         Avvialo con:  exwin\n");
        return 1;
    }

    g_menu = ex_menu_bar(g_f);
    ex_menu_add_item(g_menu, "File", "Nuovo\tCtrl+N", ID_NUOVO);
    ex_menu_add_item(g_menu, "File", "Salva...\tCtrl+S", ID_SALVA);
    ex_menu_add_item(g_menu, "File", "-", 0);
    ex_menu_add_item(g_menu, "File", "Esci\tCtrl+Q", ID_ESCI);
    ex_menu_add_item(g_menu, "Opzioni/Modalita", "Normale", ID_M_NORM);
    ex_menu_add_item(g_menu, "Opzioni/Modalita", "Scientifica", ID_M_SCI);
    ex_menu_add_item(g_menu, "Opzioni/Modalita", "Programmatore", ID_M_PROG);
    ex_menu_add_item(g_menu, "Info", "Informazioni su", ID_INFO);
    ex_menu_add_item(g_menu, "Info", "Istruzioni", ID_ISTR);

    g_r_modo[0] = ex_create("radio", "Normale", EX_CHILD, MARGINE, Y_RADIO, 78, 20, g_f, ID_R_NORM, 0);
    g_r_modo[1] = ex_create("radio", "Scientifica", EX_CHILD, MARGINE + 80, Y_RADIO, 110, 20, g_f, ID_R_SCI, 0);
    g_r_modo[2] = ex_create("radio", "Programmatore", EX_CHILD, MARGINE + 196, Y_RADIO, 124, 20, g_f, ID_R_PROG, 0);
    ex_set_checked(g_r_modo[g_modo], 1);

    g_cron = ex_create("checkbox", "Cronologia", EX_CHILD, MARGINE, Y_SOTTO + 8, 120, 20, g_f, ID_CRON, 0);

    /* The bases: radios of their own frame, so they are a group apart. */
    g_riq_base = ex_create("frame", "Base", EX_CHILD, X_EXTRA, Y_SOTTO - 4,
                         4 * PASSO_X - 4, 38, g_f, 0, 0);
    for (i = 0; i < 4; i++)
        g_r_base[i] = ex_create("radio", BASI_NOME[i], EX_CHILD, 8 + i * 60, 14, 56, 20,
                              g_riq_base, (unsigned int)(ID_B_BIN + i), 0);
    ex_set_checked(g_r_base[g_base], 1);

    for (i = 0; i < N_TASTI; i++) {
        Tasto *t = &g_t[i];
        int    x = (t->blocco == 0 || t->blocco == 3 ? MARGINE : X_EXTRA) + t->col * PASSO_X;
        int    y = Y_TASTI + t->riga * PASSO_Y;

        t->c = ex_create("button", t->testo, EX_CHILD, x, y, BW,
                       BH + (t->alto - 1) * PASSO_Y, g_f, (unsigned int)(ID_TASTO + i), 0);
    }

    g_font_grande  = ex_font_find(EX_FAMILY_MONO, 22, 1, 0);
    g_font_piccolo = ex_font_find(EX_FAMILY_MONO, 14, 0, 0);

    if (g_cron_all_avvio) { ex_set_checked(g_cron, 1); cron_apri(); g_nastro = 1; }
    tasti_aggiorna();
    if (g_modo == MODO_NORM) ex_resize(g_f, W_NORMALE, H_FIN);
    ex_clear_focus(g_f);
    ridisegna();

    while (ex_get_message(&m)) ex_dispatch(&m);
    return 0;
}
