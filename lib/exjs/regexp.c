/* =============================================================================
 * lib/exjs/regexp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Regular expressions (@EXJS-REGEXP, 29 September 2026)
 *
 * A backtracking matcher, the kind every JavaScript engine has: the pattern
 * becomes a small tree (RNodo), and prova() walks it against the text with a
 * continuation - what is left to match once the current piece is done. It
 * has classes, anchors, word boundaries, groups (capturing, non-capturing,
 * named), alternation, greedy and lazy quantifiers, backreferences and
 * lookahead; the flags g, i, m, s and y.
 *
 * ! TWO GUARDS, AS IN THE INTERPRETER. A pattern like (a*)*b on a long text
 * backtracks for ever: steps are counted, and past RE_PASSI_MAX the match
 * throws instead of freezing the page. And every step of a sequence is a C
 * call: past RE_PROF_MAX nested calls it throws instead of eating the stack.
 *
 * ! THE COMMON QUANTIFIERS DO NOT RECURSE PER CHARACTER. `.*`, `\s+`,
 * `[a-z]*` - a repeat of one single-character node - count how far they can
 * go in a loop and then back off one by one: one C call per attempt, not one
 * per character. Without it `.*` on a 100 KB page would be 100000 frames.
 *
 * ! BYTES, NOT CHARACTERS: ExJs strings are UTF-8 bytes, and here too a `.`
 * matches one byte. /./ against "è" matches half of it, as "è".length is 2.
 * The i flag folds ASCII only.
 * ============================================================================= */

#include "exjs_int.h"

/* ! NIENTE LIBC QUI DENTRO, come nel resto di ExJs: le poche funzioni che
 * servono sono queste. */
static int r_lung(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int r_ha(const char *s, char x) { for (; *s; s++) if (*s == x) return 1; return 0; }
static int r_ugu(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int r_ugu_n(const char *a, const char *b, int n) { int i; for (i = 0; i < n; i++) if (a[i] != b[i]) return 0; return 1; }
static void r_copia(char *d, const char *s, int max) { int i = 0; while (s[i] && i + 1 < max) { d[i] = s[i]; i++; } d[i] = '\0'; }
static void r_zero(void *p, int n) { char *c = (char *)p; while (n--) *c++ = 0; }
static void r_int_copia(int *d, const int *s, int n) { while (n--) *d++ = *s++; }

#define RE_NODI_MAX    512
#define RE_CLASSI_MAX  64
#define RE_GRUPPI_MAX  32
#define RE_PASSI_MAX   2000000L
/* ! IL TETTO DELLA PROFONDITA' ESCE DALLA PILA, MISURATA: un programma ha
 * 256 KB di pila al massimo (USER_STACK_MAX), e l'interprete ne usa la sua
 * parte. Un livello qui costa fino a ~250 byte sull'i386 (prova_dentro 112,
 * ripeti 112, segui; -fstack-usage), quindi 400 livelli sono ~100 KB. Oltre
 * si lancia un RangeError: meglio dell'uscita per pila finita. */
#define RE_PROF_MAX    400
#define RE_ANNIDATE    4           /* regexp in uso insieme (replace con funzione) */

enum { R_CAR = 1, R_QUALUNQUE, R_CLASSE, R_INIZIO, R_FINE, R_CONFINE, R_NONCONFINE,
       R_GRUPPO, R_ALT, R_RIPETI, R_RIF, R_AVANTI, R_NONAVANTI };

typedef struct {
    unsigned char tipo;
    unsigned char avido;
    short         gruppo;          /* R_GRUPPO: -1 = (?:...); R_RIF: quale */
    int           a, b;            /* figli: il corpo, l'altro ramo */
    int           prossimo;        /* il nodo dopo, nella sequenza */
    int           min, max;        /* R_RIPETI; max -1 = senza tetto */
    int           c;               /* R_CAR: il byte; R_CLASSE: quale classe */
} RNodo;

typedef struct { unsigned char bit[32]; } RClasse;

typedef struct {
    RNodo        n[RE_NODI_MAX];
    int          nn;
    RClasse      cl[RE_CLASSI_MAX];
    int          ncl;
    int          ngruppi;          /* compreso lo 0, il match intero */
    char         nome[RE_GRUPPI_MAX][24];
    int          radice;
    int          flag;
    const char  *err;
    /* il testo che si prova, e dove erano i gruppi */
    const unsigned char *s;
    int          len;
    int          ini[RE_GRUPPI_MAX], fin[RE_GRUPPI_MAX];
    long         passi;
    int          prof;
    int          troppo;           /* 1 passi, 2 profondita' */
    int          fine;             /* dove e' finito il match */
    /* la compilazione */
    const char  *p;
} Re;

static Re  g_re[RE_ANNIDATE];
static int g_re_n = 0;

/* =============================================================================
 * COMPILARE
 * ========================================================================== */
static int nodo_nuovo(Re *R, int tipo)
{
    RNodo *N;

    if (R->nn >= RE_NODI_MAX) { R->err = "espressione regolare troppo lunga"; return -1; }
    N = &R->n[R->nn];
    r_zero(N, (int)sizeof(*N));
    N->tipo = (unsigned char)tipo;
    N->a = N->b = N->prossimo = -1;
    N->gruppo = -1;
    N->max = -1;
    N->avido = 1;
    return R->nn++;
}

static int classe_nuova(Re *R)
{
    if (R->ncl >= RE_CLASSI_MAX) { R->err = "troppe classi [..] nell'espressione regolare"; return -1; }
    r_zero(&R->cl[R->ncl], (int)sizeof(RClasse));
    return R->ncl++;
}

static void cl_metti(RClasse *C, int x) { C->bit[(x >> 3) & 31] |= (unsigned char)(1 << (x & 7)); }
static int  cl_ha(const RClasse *C, int x) { return (C->bit[(x >> 3) & 31] >> (x & 7)) & 1; }

static int e_cifra(int x)  { return x >= '0' && x <= '9'; }
static int e_parola(int x) { return (x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') || e_cifra(x) || x == '_'; }
static int e_spazio(int x) { return x == ' ' || x == '\t' || x == '\n' || x == '\r' || x == '\v' || x == '\f'; }
static int basso(int x)    { return (x >= 'A' && x <= 'Z') ? x + 32 : x; }
static int alto(int x)     { return (x >= 'a' && x <= 'z') ? x - 32 : x; }

/* \d \w \s e le maiuscole, dentro una classe */
static void cl_predefinita(RClasse *C, char k)
{
    int x;
    for (x = 0; x < 256; x++) {
        int si = (k == 'd' || k == 'D') ? e_cifra(x)
               : (k == 'w' || k == 'W') ? e_parola(x) : e_spazio(x);
        if (k >= 'A' && k <= 'Z') si = !si;
        if (si) cl_metti(C, x);
    }
}

static int esa(char x)
{
    if (x >= '0' && x <= '9') return x - '0';
    if (x >= 'a' && x <= 'f') return x - 'a' + 10;
    if (x >= 'A' && x <= 'F') return x - 'A' + 10;
    return -1;
}

/* Un carattere scappato dopo '\': rende il byte, o -1 se non e' un byte
 * semplice (per \d e compagni, che chi chiama tratta a parte). */
static int scappato(Re *R)
{
    char k = *R->p++;

    switch (k) {
    case 'n': return '\n';
    case 'r': return '\r';
    case 't': return '\t';
    case 'v': return '\v';
    case 'f': return '\f';
    case '0': return 0;
    case 'x':
        if (esa(R->p[0]) >= 0 && esa(R->p[1]) >= 0) {
            int v = esa(R->p[0]) * 16 + esa(R->p[1]);
            R->p += 2;
            return v;
        }
        return 'x';
    case 'c':
        if ((*R->p >= 'a' && *R->p <= 'z') || (*R->p >= 'A' && *R->p <= 'Z')) return *R->p++ & 31;
        return 'c';
    default: return (unsigned char)k;
    }
}

static int alternativa(Re *R);

/* [...] */
static int classe(Re *R)
{
    int      k = classe_nuova(R), neg = 0, prima = -1, x, n;
    RClasse *C;

    if (k < 0) return -1;
    C = &R->cl[k];
    if (*R->p == '^') { neg = 1; R->p++; }
    if (*R->p == ']') { cl_metti(C, ']'); R->p++; }
    while (*R->p && *R->p != ']') {
        int ch;

        if (*R->p == '\\') {
            R->p++;
            if (*R->p && r_ha("dDwWsS", *R->p)) { cl_predefinita(C, *R->p++); prima = -1; continue; }
            if (*R->p == 'b') { R->p++; ch = '\b'; }
            else ch = scappato(R);
        } else ch = (unsigned char)*R->p++;

        /* a-z: un intervallo, se il '-' non e' l'ultimo */
        if (R->p[0] == '-' && R->p[1] && R->p[1] != ']') {
            int fino;
            R->p++;
            if (*R->p == '\\') { R->p++; fino = scappato(R); }
            else fino = (unsigned char)*R->p++;
            if (fino < ch) { R->err = "intervallo al contrario in [..]"; return -1; }
            for (x = ch; x <= fino; x++) cl_metti(C, x);
            prima = -1;
            continue;
        }
        cl_metti(C, ch);
        prima = ch;
    }
    (void)prima;
    if (*R->p != ']') { R->err = "manca ] nell'espressione regolare"; return -1; }
    R->p++;
    if (R->flag & RE_I)
        for (x = 0; x < 256; x++) if (cl_ha(C, x)) { cl_metti(C, basso(x)); cl_metti(C, alto(x)); }
    if (neg) for (x = 0; x < 32; x++) C->bit[x] = (unsigned char)~C->bit[x];
    n = nodo_nuovo(R, R_CLASSE);
    if (n >= 0) R->n[n].c = k;
    return n;
}

static int car(Re *R, int x)
{
    int n = nodo_nuovo(R, R_CAR);
    if (n >= 0) R->n[n].c = x;
    return n;
}

/* Un pezzo: un carattere, una classe, un gruppo, un'ancora. */
static int atomo(Re *R)
{
    char k = *R->p;
    int  n;

    if (k == '(') {
        int gruppo = -1, tipo = R_GRUPPO, corpo;

        R->p++;
        if (R->p[0] == '?') {
            if (R->p[1] == ':') R->p += 2;
            else if (R->p[1] == '=') { R->p += 2; tipo = R_AVANTI; }
            else if (R->p[1] == '!') { R->p += 2; tipo = R_NONAVANTI; }
            else if (R->p[1] == '<' && R->p[2] != '=' && R->p[2] != '!') {
                unsigned int i = 0;
                R->p += 2;
                if (R->ngruppi >= RE_GRUPPI_MAX) { R->err = "troppi gruppi"; return -1; }
                gruppo = R->ngruppi++;
                while (*R->p && *R->p != '>' && i + 1 < sizeof(R->nome[0])) R->nome[gruppo][i++] = *R->p++;
                R->nome[gruppo][i] = '\0';
                if (*R->p != '>') { R->err = "nome di gruppo non chiuso"; return -1; }
                R->p++;
            } else { R->err = "(? che ExJs non conosce (il guardare indietro non c'e')"; return -1; }
        } else {
            if (R->ngruppi >= RE_GRUPPI_MAX) { R->err = "troppi gruppi"; return -1; }
            gruppo = R->ngruppi++;
        }
        corpo = alternativa(R);
        if (R->err) return -1;
        if (*R->p != ')') { R->err = "manca ) nell'espressione regolare"; return -1; }
        R->p++;
        n = nodo_nuovo(R, tipo);
        if (n < 0) return -1;
        R->n[n].a = corpo;
        R->n[n].gruppo = (short)gruppo;
        return n;
    }
    if (k == '[') { R->p++; return classe(R); }
    if (k == '.') { R->p++; return nodo_nuovo(R, R_QUALUNQUE); }
    if (k == '^') { R->p++; return nodo_nuovo(R, R_INIZIO); }
    if (k == '$') { R->p++; return nodo_nuovo(R, R_FINE); }
    if (k == '\\') {
        char e = R->p[1];

        R->p++;
        if (e == 'b') { R->p++; return nodo_nuovo(R, R_CONFINE); }
        if (e == 'B') { R->p++; return nodo_nuovo(R, R_NONCONFINE); }
        if (e && r_ha("dDwWsS", e)) {
            int kk = classe_nuova(R);
            if (kk < 0) return -1;
            cl_predefinita(&R->cl[kk], e);
            R->p++;
            n = nodo_nuovo(R, R_CLASSE);
            if (n >= 0) R->n[n].c = kk;
            return n;
        }
        if (e >= '1' && e <= '9') {                 /* \1: un riferimento */
            int g = 0;
            while (e_cifra(*R->p)) g = g * 10 + (*R->p++ - '0');
            n = nodo_nuovo(R, R_RIF);
            if (n >= 0) R->n[n].gruppo = (short)g;
            return n;
        }
        if (e == 'k' && R->p[1] == '<') {           /* \k<nome> */
            char nome[24];
            unsigned int i = 0;
            int g;
            R->p += 2;
            while (*R->p && *R->p != '>' && i + 1 < sizeof(nome)) nome[i++] = *R->p++;
            nome[i] = '\0';
            if (*R->p == '>') R->p++;
            for (g = 1; g < R->ngruppi; g++) if (r_ugu(R->nome[g], nome)) break;
            n = nodo_nuovo(R, R_RIF);
            if (n >= 0) R->n[n].gruppo = (short)g;
            return n;
        }
        if (e == 'u' && esa(R->p[1]) >= 0 && esa(R->p[2]) >= 0 && esa(R->p[3]) >= 0 && esa(R->p[4]) >= 0) {
            /* \uXXXX: i byte UTF-8 in fila, in un gruppo che non cattura
             * perche' un quantificatore dopo li prenda tutti */
            unsigned int u = (unsigned)(esa(R->p[1]) << 12 | esa(R->p[2]) << 8 | esa(R->p[3]) << 4 | esa(R->p[4]));
            int b[3], nb = 0, i, primo = -1, ultimo = -1;
            R->p += 5;
            if (u < 0x80) return car(R, (int)u);
            if (u < 0x800) { b[0] = 0xC0 | (int)(u >> 6); b[1] = 0x80 | (int)(u & 0x3F); nb = 2; }
            else { b[0] = 0xE0 | (int)(u >> 12); b[1] = 0x80 | (int)((u >> 6) & 0x3F); b[2] = 0x80 | (int)(u & 0x3F); nb = 3; }
            for (i = 0; i < nb; i++) {
                int x = car(R, b[i]);
                if (x < 0) return -1;
                if (primo < 0) primo = x; else R->n[ultimo].prossimo = x;
                ultimo = x;
            }
            n = nodo_nuovo(R, R_GRUPPO);
            if (n >= 0) R->n[n].a = primo;
            return n;
        }
        return car(R, scappato(R));
    }
    if (k == '*' || k == '+' || k == '?') { R->err = "niente da ripetere"; return -1; }
    R->p++;
    return car(R, (unsigned char)k);
}

/* {n}, {n,}, {n,m}: rende 1 se c'era, e lo legge */
static int graffa(Re *R, int *min, int *max)
{
    const char *p = R->p + 1;
    int a = 0, b;

    if (!e_cifra(*p)) return 0;
    while (e_cifra(*p)) a = a * 10 + (*p++ - '0');
    b = a;
    if (*p == ',') {
        p++;
        if (*p == '}') b = -1;
        else {
            if (!e_cifra(*p)) return 0;
            b = 0;
            while (e_cifra(*p)) b = b * 10 + (*p++ - '0');
        }
    }
    if (*p != '}') return 0;
    R->p = p + 1;
    *min = a;
    *max = b;
    return 1;
}

/* Una sequenza di pezzi, ciascuno col suo quantificatore. */
static int sequenza(Re *R)
{
    int primo = -1, ultimo = -1;

    while (*R->p && *R->p != '|' && *R->p != ')' && !R->err) {
        int x = atomo(R), min = -1, max = -1;

        if (x < 0) return -1;
        if (*R->p == '*')      { min = 0; max = -1; R->p++; }
        else if (*R->p == '+') { min = 1; max = -1; R->p++; }
        else if (*R->p == '?') { min = 0; max = 1;  R->p++; }
        else if (*R->p == '{' && graffa(R, &min, &max)) { }
        if (min >= 0) {
            int r = nodo_nuovo(R, R_RIPETI);
            if (r < 0) return -1;
            if (max >= 0 && max < min) { R->err = "{n,m} con m minore di n"; return -1; }
            R->n[r].a = x;
            R->n[r].min = min;
            R->n[r].max = max;
            if (*R->p == '?') { R->n[r].avido = 0; R->p++; }
            x = r;
        }
        if (primo < 0) primo = x; else R->n[ultimo].prossimo = x;
        ultimo = x;
    }
    return primo;
}

static int alternativa(Re *R)
{
    int s = sequenza(R);

    if (R->err) return -1;
    if (*R->p == '|') {
        int n = nodo_nuovo(R, R_ALT), altro;
        if (n < 0) return -1;
        R->p++;
        altro = alternativa(R);
        if (R->err) return -1;
        R->n[n].a = s;
        R->n[n].b = altro;
        return n;
    }
    return s;
}

static Re *compila(const char *corpo, int flag)
{
    Re *R;

    if (g_re_n >= RE_ANNIDATE) return 0;
    R = &g_re[g_re_n];
    R->nn = 0;
    R->ncl = 0;
    R->ngruppi = 1;
    r_zero(R->nome, (int)sizeof(R->nome));
    R->flag = flag;
    R->err = 0;
    R->p = corpo;
    R->radice = alternativa(R);
    if (!R->err && *R->p == ')') R->err = "una ) di troppo nell'espressione regolare";
    return R;
}

/* =============================================================================
 * PROVARE
 * ========================================================================== */
enum { C_NODO, C_GRUPPO, C_RIPETI, C_STOP };

typedef struct Cont {
    int                tipo, nodo, conta, inizio, gruppo;
    const struct Cont *dopo;
} Cont;

static int prova(Re *R, int n, int pos, const Cont *k);
static int ripeti(Re *R, int n, int conta, int pos, const Cont *k);

static int uguali(Re *R, int x, int y)
{
    if (x == y) return 1;
    return (R->flag & RE_I) && basso(x) == basso(y);
}

static int segui(Re *R, int pos, const Cont *k)
{
    if (!k) { R->fine = pos; return 1; }
    switch (k->tipo) {
    case C_NODO:
        return prova(R, k->nodo, pos, k->dopo);
    case C_GRUPPO: {
        int vi = R->ini[k->gruppo], vf = R->fin[k->gruppo];
        R->ini[k->gruppo] = k->inizio;
        R->fin[k->gruppo] = pos;
        if (prova(R, k->nodo, pos, k->dopo)) return 1;
        R->ini[k->gruppo] = vi;
        R->fin[k->gruppo] = vf;
        return 0;
    }
    case C_RIPETI:
        /* ! UN GIRO VUOTO DOPO IL MINIMO NON CONTA: (a*)* girerebbe per
         * sempre sulla stessa posizione. */
        if (pos == k->inizio && k->conta > R->n[k->nodo].min) return 0;
        return ripeti(R, k->nodo, k->conta, pos, k->dopo);
    default:
        R->fine = pos;
        return 1;
    }
}

/* Un nodo che prende esattamente un carattere: il percorso veloce di ripeti. */
static int un_carattere(Re *R, int n)
{
    return n >= 0 && R->n[n].prossimo < 0 &&
           (R->n[n].tipo == R_CAR || R->n[n].tipo == R_QUALUNQUE || R->n[n].tipo == R_CLASSE);
}

static int prende(Re *R, int n, int pos)
{
    const RNodo *N = &R->n[n];
    int          x;

    if (pos >= R->len) return 0;
    x = R->s[pos];
    if (N->tipo == R_CAR) return uguali(R, x, N->c);
    if (N->tipo == R_QUALUNQUE) return (R->flag & RE_S) || x != '\n';
    return cl_ha(&R->cl[N->c], x);
}

static int ripeti(Re *R, int n, int conta, int pos, const Cont *k)
{
    const RNodo *N = &R->n[n];
    Cont         c;

    if (un_carattere(R, N->a)) {
        int m = 0, lim = R->len - pos, i;

        if (N->max >= 0 && N->max < lim) lim = N->max;
        while (m < lim && prende(R, N->a, pos + m)) m++;
        if (m < N->min) return 0;
        if (N->avido) {
            for (i = m; i >= N->min; i--) if (prova(R, N->prossimo, pos + i, k)) return 1;
        } else {
            for (i = N->min; i <= m; i++) if (prova(R, N->prossimo, pos + i, k)) return 1;
        }
        return 0;
    }

    c.tipo = C_RIPETI; c.nodo = n; c.conta = conta + 1; c.inizio = pos; c.gruppo = 0; c.dopo = k;
    if (conta < N->min) return prova(R, N->a, pos, &c);
    if (N->avido) {
        if ((N->max < 0 || conta < N->max) && prova(R, N->a, pos, &c)) return 1;
        return prova(R, N->prossimo, pos, k);
    }
    if (prova(R, N->prossimo, pos, k)) return 1;
    if (N->max < 0 || conta < N->max) return prova(R, N->a, pos, &c);
    return 0;
}

/* ! A PARTE, E NON IN LINEA: le due tabelle dei gruppi che servono qui
 * pesano 256 byte, e dentro prova_dentro le pagava OGNI livello della
 * ricorsione (352 byte a livello, misurato con -fstack-usage). */
static __attribute__((noinline)) int avanti(Re *R, int n, int pos, const Cont *k)
{
    const RNodo *N = &R->n[n];
    Cont stop;
    int  ok, ini[RE_GRUPPI_MAX], fin[RE_GRUPPI_MAX], fine = R->fine;

    r_int_copia(ini, R->ini, RE_GRUPPI_MAX);
    r_int_copia(fin, R->fin, RE_GRUPPI_MAX);
    stop.tipo = C_STOP; stop.dopo = 0;
    ok = prova(R, N->a, pos, &stop);
    R->fine = fine;
    if (N->tipo == R_NONAVANTI) {
        r_int_copia(R->ini, ini, RE_GRUPPI_MAX);
        r_int_copia(R->fin, fin, RE_GRUPPI_MAX);
        if (ok) return 0;
    } else if (!ok) return 0;
    if (prova(R, N->prossimo, pos, k)) return 1;
    r_int_copia(R->ini, ini, RE_GRUPPI_MAX);
    r_int_copia(R->fin, fin, RE_GRUPPI_MAX);
    return 0;
}

static int prova_dentro(Re *R, int n, int pos, const Cont *k)
{
    const RNodo *N;

    if (n < 0) return segui(R, pos, k);
    N = &R->n[n];

    switch (N->tipo) {
    case R_CAR: case R_QUALUNQUE: case R_CLASSE:
        return prende(R, n, pos) && prova(R, N->prossimo, pos + 1, k);

    case R_INIZIO:
        if (pos == 0 || ((R->flag & RE_M) && R->s[pos - 1] == '\n')) return prova(R, N->prossimo, pos, k);
        return 0;

    case R_FINE:
        if (pos == R->len || ((R->flag & RE_M) && R->s[pos] == '\n')) return prova(R, N->prossimo, pos, k);
        return 0;

    case R_CONFINE: case R_NONCONFINE: {
        int a = pos > 0 && e_parola(R->s[pos - 1]);
        int b = pos < R->len && e_parola(R->s[pos]);
        if ((a != b) == (N->tipo == R_CONFINE)) return prova(R, N->prossimo, pos, k);
        return 0;
    }

    case R_GRUPPO: {
        Cont c;
        c.tipo = (N->gruppo >= 0) ? C_GRUPPO : C_NODO;
        c.nodo = N->prossimo; c.conta = 0; c.inizio = pos; c.gruppo = N->gruppo; c.dopo = k;
        return prova(R, N->a, pos, &c);
    }

    case R_ALT: {
        Cont c;
        c.tipo = C_NODO; c.nodo = N->prossimo; c.conta = 0; c.inizio = pos; c.gruppo = 0; c.dopo = k;
        if (prova(R, N->a, pos, &c)) return 1;
        return prova(R, N->b, pos, &c);
    }

    case R_RIPETI:
        return ripeti(R, n, 0, pos, k);

    case R_RIF: {
        int g = N->gruppo, l, i;
        if (g >= R->ngruppi || R->fin[g] < 0) return prova(R, N->prossimo, pos, k);
        l = R->fin[g] - R->ini[g];
        if (pos + l > R->len) return 0;
        for (i = 0; i < l; i++) if (!uguali(R, R->s[pos + i], R->s[R->ini[g] + i])) return 0;
        return prova(R, N->prossimo, pos + l, k);
    }

    case R_AVANTI: case R_NONAVANTI:
        return avanti(R, n, pos, k);
    }
    return 0;
}

/* ! UN CICLO PER TUTTO QUEL CHE STA IN CODA: un carattere, un'ancora, il
 * passare al nodo dopo o alla continuazione semplice non chiamano se
 * stessi, vanno avanti qui. Ricorrono solo gruppi, alternative e
 * ripetizioni, che hanno qualcosa da ricordare per tornare indietro. Cosi'
 * la profondita' segue la struttura del modello e non la lunghezza del
 * testo. */
static int prova(Re *R, int n, int pos, const Cont *k)
{
    int r = 0;

    if (R->troppo) return 0;
    if (++R->prof > RE_PROF_MAX) { R->prof--; R->troppo = 2; return 0; }
    for (;;) {
        const RNodo *N;

        if (++R->passi > RE_PASSI_MAX) { R->troppo = 1; r = 0; break; }
        if (n < 0) {
            if (!k) { R->fine = pos; r = 1; break; }
            if (k->tipo == C_NODO) { n = k->nodo; k = k->dopo; continue; }
            r = segui(R, pos, k);
            break;
        }
        N = &R->n[n];
        if (N->tipo == R_CAR || N->tipo == R_QUALUNQUE || N->tipo == R_CLASSE) {
            if (!prende(R, n, pos)) { r = 0; break; }
            pos++;
            n = N->prossimo;
            continue;
        }
        if (N->tipo == R_INIZIO) {
            if (!(pos == 0 || ((R->flag & RE_M) && R->s[pos - 1] == '\n'))) { r = 0; break; }
            n = N->prossimo;
            continue;
        }
        if (N->tipo == R_FINE) {
            if (!(pos == R->len || ((R->flag & RE_M) && R->s[pos] == '\n'))) { r = 0; break; }
            n = N->prossimo;
            continue;
        }
        r = prova_dentro(R, n, pos, k);
        break;
    }
    R->prof--;
    return r;
}

/* Cerca da `da` in poi (solo a `da`, con y). Rende l'inizio, o -1. */
static int cerca(Re *R, const unsigned char *s, int len, int da)
{
    int i, g;

    R->s = s;
    R->len = len;
    R->passi = 0;
    R->prof = 0;
    R->troppo = 0;
    for (i = da; i <= len; i++) {
        for (g = 0; g < R->ngruppi; g++) { R->ini[g] = -1; R->fin[g] = -1; }
        if (prova(R, R->radice, i, 0)) {
            R->ini[0] = i;
            R->fin[0] = R->fine;
            return i;
        }
        if (R->troppo || (R->flag & RE_Y)) break;
    }
    return -1;
}

/* =============================================================================
 * GLI OGGETTI RegExp
 * ========================================================================== */
static int g_proto_re = -1;

static int e_regexp(ExJsCtx *c, ExJsVal v)
{
    ExJsOggetto *O = exjs_ogg(c, exjs_a_oggetto(v));
    return O && g_proto_re >= 0 && O->proto == g_proto_re;
}

static void testo_flag(int f, char *out)
{
    int i = 0;
    if (f & RE_G) out[i++] = 'g';
    if (f & RE_I) out[i++] = 'i';
    if (f & RE_M) out[i++] = 'm';
    if (f & RE_S) out[i++] = 's';
    if (f & RE_U) out[i++] = 'u';
    if (f & RE_Y) out[i++] = 'y';
    out[i] = '\0';
}

static int flag_da_testo(const char *s)
{
    int f = 0;
    for (; *s; s++) {
        if      (*s == 'g') f |= RE_G;
        else if (*s == 'i') f |= RE_I;
        else if (*s == 'm') f |= RE_M;
        else if (*s == 's') f |= RE_S;
        else if (*s == 'y') f |= RE_Y;
        else if (*s == 'u' || *s == 'd') f |= RE_U;
        else return -1;
    }
    return f;
}

ExJsVal exjs_regexp_nuova(ExJsCtx *c, const char *corpo, int flag)
{
    ExJsVal o = exjs_oggetto(c);
    char    fl[8];
    Re     *R = compila(corpo, flag);

    if (!R) { exjs_lancia_errore(c, "RangeError", "troppe espressioni regolari annidate"); return exjs_indefinito(); }
    if (R->err) {
        exjs_lancia_errore(c, "SyntaxError", R->err);
        return exjs_indefinito();
    }
    if (exjs_a_oggetto(o) < 0) return o;
    exjs_ogg(c, exjs_a_oggetto(o))->proto = g_proto_re;
    testo_flag(flag, fl);
    exjs_metti(c, o, "source", exjs_stringa(c, corpo[0] ? corpo : "(?:)", -1));
    exjs_metti(c, o, "flags", exjs_stringa(c, fl, -1));
    exjs_metti(c, o, "global", exjs_booleano(flag & RE_G));
    exjs_metti(c, o, "ignoreCase", exjs_booleano(flag & RE_I));
    exjs_metti(c, o, "multiline", exjs_booleano(flag & RE_M));
    exjs_metti(c, o, "sticky", exjs_booleano(flag & RE_Y));
    exjs_metti(c, o, "lastIndex", exjs_numero(c, 0));
    return o;
}

/* La regexp di un oggetto, compilata (in cima alla pila: chi la prende la
 * rende con rilascia()). 0 se non si compila: l'eccezione e' gia' lanciata. */
static Re *prendi(ExJsCtx *c, ExJsVal re)
{
    ExJsVal      src = exjs_prendi(c, re, "source"), fl = exjs_prendi(c, re, "flags");
    const char  *s = exjs_a_stringa(c, src);
    char         corpo[1024];
    int          f;
    Re          *R;

    r_copia(corpo, r_ugu(s, "(?:)") ? "" : s, (int)sizeof(corpo));
    f = flag_da_testo(exjs_a_stringa(c, fl));
    R = compila(corpo, f < 0 ? 0 : f);
    if (!R) { exjs_lancia_errore(c, "RangeError", "troppe espressioni regolari annidate"); return 0; }
    if (R->err) { exjs_lancia_errore(c, "SyntaxError", R->err); return 0; }
    g_re_n++;
    return R;
}

static void rilascia(void) { if (g_re_n > 0) g_re_n--; }

static int troppo(ExJsCtx *c, Re *R)
{
    if (!R->troppo) return 0;
    exjs_lancia_errore(c, "RangeError", R->troppo == 1
                       ? "espressione regolare troppo lenta: fermata"
                       : "espressione regolare troppo annidata: fermata");
    return 1;
}

static ExJsVal arg(const ExJsVal *a, int n, int i) { return i < n ? a[i] : exjs_indefinito(); }

/* Una stringa che resta ferma (vedi stabile in libreria.c). */
static const char *stabile(ExJsCtx *c, ExJsVal v)
{
    if (exjs_tipo(c, v) == EXJS_STRINGA) return exjs_a_stringa(c, v);
    return exjs_a_stringa(c, exjs_stringa(c, exjs_a_stringa(c, v), -1));
}

/* Il vettore di un match: [intero, gruppi...] con index, input e groups. */
static ExJsVal risultato(ExJsCtx *c, Re *R, const char *s, ExJsVal input)
{
    ExJsVal v = exjs_vettore(c), gr = exjs_indefinito();
    int     g, nomi = 0;

    for (g = 0; g < R->ngruppi; g++) {
        ExJsVal x = (R->fin[g] >= 0) ? exjs_stringa(c, s + R->ini[g], R->fin[g] - R->ini[g])
                                     : exjs_indefinito();
        exjs_indice_metti(c, v, (unsigned int)g, x);
        if (R->nome[g][0]) {
            if (!nomi) { gr = exjs_oggetto(c); nomi = 1; }
            exjs_metti(c, gr, R->nome[g], x);
        }
    }
    exjs_metti(c, v, "index", exjs_numero(c, R->ini[0]));
    exjs_metti(c, v, "input", input);
    exjs_metti(c, v, "groups", gr);
    return v;
}

/* exec, con lastIndex per g e y. ! test() passa solo = 1: non serve il
 * vettore del risultato, e costruirlo in un ciclo consumerebbe arena e
 * oggetti per niente (ExJs non li recupera). Rende true al posto suo. */
static ExJsVal esegui_come(ExJsCtx *c, ExJsVal re, ExJsVal str, int solo);

static ExJsVal esegui(ExJsCtx *c, ExJsVal re, ExJsVal str)
{
    return esegui_come(c, re, str, 0);
}

static ExJsVal esegui_come(ExJsCtx *c, ExJsVal re, ExJsVal str, int solo)
{
    const char  *s = stabile(c, str);
    int          len = r_lung(s), da = 0, at;
    Re          *R = prendi(c, re);
    ExJsVal      r;

    if (!R) return exjs_nullo();
    if (R->flag & (RE_G | RE_Y)) {
        double li = exjs_a_numero(c, exjs_prendi(c, re, "lastIndex"));
        da = (li > 0 && li == li) ? (int)li : 0;
        if (da > len) {
            exjs_metti(c, re, "lastIndex", exjs_numero(c, 0));
            rilascia();
            return exjs_nullo();
        }
    }
    at = cerca(R, (const unsigned char *)s, len, da);
    if (troppo(c, R)) { rilascia(); return exjs_nullo(); }
    if (at < 0) {
        if (R->flag & (RE_G | RE_Y)) exjs_metti(c, re, "lastIndex", exjs_numero(c, 0));
        rilascia();
        return exjs_nullo();
    }
    if (R->flag & (RE_G | RE_Y)) exjs_metti(c, re, "lastIndex", exjs_numero(c, R->fin[0]));
    if (solo) { rilascia(); return exjs_booleano(1); }
    r = risultato(c, R, s, exjs_tipo(c, str) == EXJS_STRINGA ? str : exjs_stringa(c, s, -1));
    rilascia();
    return r;
}

static ExJsVal nat_RegExp(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal p = arg(a, n, 0), f = arg(a, n, 1);
    int     fl = 0;
    char    corpo[1024];

    (void)q; (void)d;
    if (e_regexp(c, p)) {
        r_copia(corpo, exjs_a_stringa(c, exjs_prendi(c, p, "source")), (int)sizeof(corpo));
        if (r_ugu(corpo, "(?:)")) corpo[0] = '\0';
        if (exjs_tipo(c, f) == EXJS_INDEFINITO) f = exjs_prendi(c, p, "flags");
    } else {
        r_copia(corpo, exjs_tipo(c, p) == EXJS_INDEFINITO ? "" : exjs_a_stringa(c, p), (int)sizeof(corpo));
    }
    if (exjs_tipo(c, f) != EXJS_INDEFINITO) {
        fl = flag_da_testo(exjs_a_stringa(c, f));
        if (fl < 0) { exjs_lancia_errore(c, "SyntaxError", "flag sconosciuto in RegExp"); return exjs_indefinito(); }
    }
    return exjs_regexp_nuova(c, corpo, fl);
}

static ExJsVal nat_exec(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)d;
    return esegui(c, q, arg(a, n, 0));
}

static ExJsVal nat_test(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)d;
    return exjs_booleano(exjs_tipo(c, esegui_come(c, q, arg(a, n, 0), 1)) != EXJS_NULLO);
}

static ExJsVal nat_re_testo(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    const char  *src = stabile(c, exjs_prendi(c, q, "source"));
    const char  *fl  = stabile(c, exjs_prendi(c, q, "flags"));
    unsigned int f;

    (void)a; (void)n; (void)d;
    f = exjs_arena_apri(c);
    if (f == EXJS_FILO_NO) return exjs_stringa(c, "", -1);
    exjs_arena_aggiungi(c, "/", 1);
    exjs_arena_aggiungi(c, src, (unsigned)r_lung(src));
    exjs_arena_aggiungi(c, "/", 1);
    exjs_arena_aggiungi(c, fl, (unsigned)r_lung(fl));
    return exjs_arena_chiudi(c, f);
}

/* =============================================================================
 * I METODI DELLE STRINGHE CHE PRENDONO UNA REGEXP
 * ========================================================================== */
/* ! IL TESTO NUOVO SI RACCOGLIE A PEZZI, in un vettore di stringhe, e si
 * unisce alla fine: dentro replace si chiama JavaScript (la funzione di
 * rimpiazzo), e mentre un filo dell'arena e' aperto non si puo'. */
typedef struct { ExJsCtx *c; ExJsVal v; unsigned int n; } Buf;

static void b_metti(Buf *B, const char *s, unsigned int n)
{
    if (n == 0) return;
    exjs_indice_metti(B->c, B->v, B->n++, exjs_stringa(B->c, s, (int)n));
}

static ExJsVal b_chiudi(ExJsCtx *c, Buf *B)
{
    return exjs_stringa(c, exjs_vettore_testo_sep(c, B->v, ""), -1);
}

/* $&, $1, $<nome>, $`, $', $$ nel testo di ricambio */
static void ricambio(Buf *B, const char *t, const char *s, int len, Re *R)
{
    while (*t) {
        if (*t != '$' || !t[1]) { b_metti(B, t++, 1); continue; }
        if (t[1] == '$') { b_metti(B, "$", 1); t += 2; continue; }
        if (t[1] == '&') { b_metti(B, s + R->ini[0], (unsigned)(R->fin[0] - R->ini[0])); t += 2; continue; }
        if (t[1] == '`') { b_metti(B, s, (unsigned)R->ini[0]); t += 2; continue; }
        if (t[1] == '\'') { b_metti(B, s + R->fin[0], (unsigned)(len - R->fin[0])); t += 2; continue; }
        if (e_cifra(t[1])) {
            int g = t[1] - '0', l = 2;
            if (e_cifra(t[2]) && g * 10 + (t[2] - '0') < R->ngruppi) { g = g * 10 + (t[2] - '0'); l = 3; }
            if (g >= 1 && g < R->ngruppi) {
                if (R->fin[g] >= 0) b_metti(B, s + R->ini[g], (unsigned)(R->fin[g] - R->ini[g]));
                t += l;
                continue;
            }
        }
        if (t[1] == '<') {
            const char *e = t + 2;
            while (*e && *e != '>') e++;
            if (*e) {
                int g;
                for (g = 1; g < R->ngruppi; g++)
                    if (R->nome[g][0] && r_lung(R->nome[g]) == (int)(e - t - 2) &&
                        r_ugu_n(R->nome[g], t + 2, (int)(e - t - 2))) break;
                if (g < R->ngruppi && R->fin[g] >= 0) b_metti(B, s + R->ini[g], (unsigned)(R->fin[g] - R->ini[g]));
                t = e + 1;
                continue;
            }
        }
        b_metti(B, t++, 1);
    }
}

/* replace (d = 0) e replaceAll (d = 1): con una regexp o con una stringa,
 * con un testo (e i suoi $) o con una funzione. */
static ExJsVal nat_replace(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    const char  *s = stabile(c, q);
    int          len = r_lung(s), tutti = (int)(long)d, pos = 0, ultimo = 0;
    ExJsVal      pat = arg(a, n, 0), rep = arg(a, n, 1);
    int          fun = exjs_tipo(c, rep) == EXJS_FUNZIONE;
    const char  *testo = fun ? "" : stabile(c, rep);
    Buf          B;
    Re          *R;
    char         ago[1024];

    B.c = c; B.v = exjs_vettore(c); B.n = 0;
    if (e_regexp(c, pat)) {
        R = prendi(c, pat);
        if (!R) return exjs_indefinito();
        if (R->flag & RE_G) tutti = 1;
        else if (tutti) { rilascia(); exjs_lancia_errore(c, "TypeError", "replaceAll vuole una regexp con g"); return exjs_indefinito(); }
    } else {
        /* una stringa: diventa una regexp «letterale», scappando tutto */
        const char *p = stabile(c, pat);
        unsigned int i = 0;
        for (; *p && i + 2 < sizeof(ago); p++) {
            if (r_ha("\\^$.|?*+()[]{}", *p)) ago[i++] = '\\';
            ago[i++] = *p;
        }
        ago[i] = '\0';
        R = compila(ago, 0);
        if (!R || R->err) return exjs_stringa(c, s, -1);
        g_re_n++;
    }

    while (pos <= len) {
        int at = cerca(R, (const unsigned char *)s, len, pos);
        if (troppo(c, R)) { rilascia(); return exjs_indefinito(); }
        if (at < 0) break;
        b_metti(&B, s + ultimo, (unsigned)(at - ultimo));
        if (fun) {
            ExJsVal x[RE_GRUPPI_MAX + 3], r;
            int     g, k = 0, ini0 = R->ini[0], fin0 = R->fin[0];
            for (g = 0; g < R->ngruppi; g++)
                x[k++] = (R->fin[g] >= 0) ? exjs_stringa(c, s + R->ini[g], R->fin[g] - R->ini[g]) : exjs_indefinito();
            x[k++] = exjs_numero(c, ini0);
            x[k++] = exjs_tipo(c, q) == EXJS_STRINGA ? q : exjs_stringa(c, s, -1);
            r = exjs_chiama(c, rep, exjs_indefinito(), x, k, 0);
            if (exjs_interrotto(c)) { rilascia(); return exjs_indefinito(); }
            {
                const char *t = stabile(c, r);
                b_metti(&B, t, (unsigned)r_lung(t));
            }
            R->ini[0] = ini0;
            R->fin[0] = fin0;
        } else ricambio(&B, testo, s, len, R);
        ultimo = R->fin[0];
        pos = (R->fin[0] > at) ? R->fin[0] : at + 1;    /* un match vuoto avanza */
        if (!tutti) break;
    }
    b_metti(&B, s + ultimo, (unsigned)(len - ultimo));
    rilascia();
    if (e_regexp(c, pat) && (R->flag & RE_G)) exjs_metti(c, pat, "lastIndex", exjs_numero(c, 0));
    return b_chiudi(c, &B);
}

static ExJsVal g_split_vecchio = 0;

/* split con una regexp: i pezzi fra un match e l'altro, e dentro i gruppi. */
static ExJsVal nat_split(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    const char  *s;
    int          len, pos = 0, ultimo = 0;
    unsigned int k = 0, lim = 0xFFFFFFFFu;
    ExJsVal      v;
    Re          *R;

    (void)d;
    if (!e_regexp(c, arg(a, n, 0))) return exjs_chiama(c, g_split_vecchio, q, a, n, 0);
    s = stabile(c, q);
    len = r_lung(s);
    if (n > 1 && exjs_tipo(c, a[1]) != EXJS_INDEFINITO) lim = (unsigned int)exjs_a_numero(c, a[1]);
    v = exjs_vettore(c);
    if (lim == 0) return v;
    R = prendi(c, a[0]);
    if (!R) return exjs_indefinito();
    R->flag &= ~RE_Y;
    if (len == 0) {
        if (cerca(R, (const unsigned char *)s, 0, 0) < 0) exjs_indice_metti(c, v, 0, exjs_stringa(c, "", -1));
        rilascia();
        return v;
    }
    while (pos < len) {
        int at = cerca(R, (const unsigned char *)s, len, pos), g;
        if (troppo(c, R)) { rilascia(); return exjs_indefinito(); }
        if (at < 0 || at >= len) break;
        if (R->fin[0] == at && at == ultimo) { pos = at + 1; continue; }   /* vuoto dove siamo */
        if (R->fin[0] == at && at >= len) break;
        exjs_indice_metti(c, v, k++, exjs_stringa(c, s + ultimo, at - ultimo));
        if (k >= lim) { rilascia(); return v; }
        for (g = 1; g < R->ngruppi; g++) {
            exjs_indice_metti(c, v, k++, R->fin[g] >= 0
                              ? exjs_stringa(c, s + R->ini[g], R->fin[g] - R->ini[g]) : exjs_indefinito());
            if (k >= lim) { rilascia(); return v; }
        }
        ultimo = R->fin[0];
        pos = (R->fin[0] > at) ? R->fin[0] : at + 1;
    }
    exjs_indice_metti(c, v, k++, exjs_stringa(c, s + ultimo, len - ultimo));
    rilascia();
    return v;
}

/* match (d = 0), matchAll (d = 1), search (d = 2) */
static ExJsVal nat_match(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal      re = arg(a, n, 0), v;
    const char  *s = stabile(c, q);
    int          len = r_lung(s), modo = (int)(long)d, pos = 0, g;
    unsigned int k = 0;
    Re          *R;

    if (!e_regexp(c, re)) {
        ExJsVal x = exjs_tipo(c, re) == EXJS_INDEFINITO ? exjs_stringa(c, "", -1) : re;
        re = nat_RegExp(c, exjs_indefinito(), &x, 1, 0);
        if (exjs_interrotto(c)) return exjs_indefinito();
        if (modo == 1) exjs_metti(c, re, "flags", exjs_stringa(c, "g", -1));
    }
    if (modo == 2) {
        R = prendi(c, re);
        if (!R) return exjs_indefinito();
        g = cerca(R, (const unsigned char *)s, len, 0);
        rilascia();
        if (troppo(c, R)) return exjs_indefinito();
        return exjs_numero(c, g);
    }
    if (modo == 0 && !(flag_da_testo(exjs_a_stringa(c, exjs_prendi(c, re, "flags"))) & RE_G))
        return esegui(c, re, q);

    R = prendi(c, re);
    if (!R) return exjs_indefinito();
    v = exjs_vettore(c);
    while (pos <= len) {
        int at = cerca(R, (const unsigned char *)s, len, pos);
        if (troppo(c, R)) { rilascia(); return exjs_indefinito(); }
        if (at < 0) break;
        if (modo == 0) exjs_indice_metti(c, v, k++, exjs_stringa(c, s + at, R->fin[0] - at));
        else exjs_indice_metti(c, v, k++, risultato(c, R, s, q));
        pos = (R->fin[0] > at) ? R->fin[0] : at + 1;
    }
    rilascia();
    exjs_metti(c, re, "lastIndex", exjs_numero(c, 0));
    if (modo == 0 && k == 0) return exjs_nullo();
    return v;
}

void exjs_regexp_registra(ExJsCtx *c)
{
    ExJsVal g = exjs_globale(c), pr = exjs_oggetto(c), F;
    ExJsVal ps = exjs_da_oggetto(exjs_proto_str(c));

    g_re_n = 0;
    g_proto_re = exjs_a_oggetto(pr);
    F = exjs_nativa(c, nat_RegExp, 0, "RegExp");
    exjs_metti(c, g, "RegExp", F);
    exjs_metti(c, F, "prototype", pr);
    exjs_metti(c, pr, "constructor", F);
    exjs_metti(c, pr, "exec",     exjs_nativa(c, nat_exec, 0, "exec"));
    exjs_metti(c, pr, "test",     exjs_nativa(c, nat_test, 0, "test"));
    exjs_metti(c, pr, "toString", exjs_nativa(c, nat_re_testo, 0, "toString"));

    g_split_vecchio = exjs_prendi(c, ps, "split");
    exjs_metti(c, ps, "replace",    exjs_nativa(c, nat_replace, (void *)0, "replace"));
    exjs_metti(c, ps, "replaceAll", exjs_nativa(c, nat_replace, (void *)1, "replaceAll"));
    exjs_metti(c, ps, "split",      exjs_nativa(c, nat_split, 0, "split"));
    exjs_metti(c, ps, "match",      exjs_nativa(c, nat_match, (void *)0, "match"));
    exjs_metti(c, ps, "matchAll",   exjs_nativa(c, nat_match, (void *)1, "matchAll"));
    exjs_metti(c, ps, "search",     exjs_nativa(c, nat_match, (void *)2, "search"));
}
