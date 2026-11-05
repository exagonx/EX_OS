/* =============================================================================
 * lib/libc_regex.c -- POSIX regular expressions: regcomp, regexec, regerror,
 * regfree (5 November 2026, Valkey)
 * EX-OS -- Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Included by lib/libc.c in the part for ported software (static libc only,
 * like glob). Written here: there was no POSIX regex in the libc, and no
 * engine in the project to port (grep's gnulib is not checked out).
 *
 * What it takes:
 *   ERE (REG_EXTENDED): ( ) | * + ? {m} {m,} {m,n} . [ ] ^ $ and \ before a
 *       special character;
 *   BRE: \( \) \{ \} * . [ ] ^ (at the start) $ (at the end), \1..\9, and
 *       GNU's \+ \? \| ; a '*' first in the expression or a group is itself;
 *   sets: ranges, [^...], a ']' first taken as itself, the classes
 *       [:alnum:] [:alpha:] [:blank:] [:cntrl:] [:digit:] [:graph:]
 *       [:lower:] [:print:] [:punct:] [:space:] [:upper:] [:xdigit:];
 *   REG_ICASE, REG_NEWLINE ('.' and [^...] do not take '\n', ^ and $ also
 *       at the line ends), REG_NOSUB; regexec REG_NOTBOL, REG_NOTEOL.
 * Not taken: collating elements [. .] and equivalence classes [= =]
 * (REG_ECOLLATE): the locale is C.
 *
 * ! THE MATCH IS POSIX'S: LEFTMOST, THEN LONGEST. The engine backtracks over
 * a tree, and at each starting point it tries every way and keeps the
 * longest end (with the submatches of the first way that reached it). The
 * cost of trying every way is bounded: past REGEX_PASSI steps for one
 * regexec the search stops with what it has found, or REG_ESPACE if it has
 * found nothing -- a pathological pattern costs an error, not a hang.
 * Submatches: those of the first way found for the longest match, which is
 * what most engines give and what POSIX leaves loose enough for code that
 * reads \0 and the groups.
 * ============================================================================= */

typedef int regoff_t;

typedef struct {
    size_t re_nsub;
    void  *__exos_rx;
} regex_t;

typedef struct {
    regoff_t rm_so;
    regoff_t rm_eo;
} regmatch_t;

#define REG_EXTENDED    1
#define REG_ICASE       2
#define REG_NEWLINE     4
#define REG_NOSUB       8
#define REG_NOTBOL      1
#define REG_NOTEOL      2

#define REG_NOMATCH     1
#define REG_BADPAT      2
#define REG_ECOLLATE    3
#define REG_ECTYPE      4
#define REG_EESCAPE     5
#define REG_ESUBREG     6
#define REG_EBRACK      7
#define REG_EPAREN      8
#define REG_EBRACE      9
#define REG_BADBR       10
#define REG_ERANGE      11
#define REG_ESPACE      12
#define REG_BADRPT      13

#define REGEX_PASSI     20000000L
#define REGEX_RIP_MAX   32767           /* RE_DUP_MAX of glibc */

enum { RX_CAR, RX_QUALSIASI, RX_INSIEME, RX_INIZIO, RX_FINE, RX_GRUPPO,
       RX_SCELTA, RX_RIPETI, RX_RIF, RX_VUOTO };

typedef struct RxNodo {
    int            tipo;
    int            c;               /* RX_CAR: the byte; RX_GRUPPO/RX_RIF: the number */
    int            min, max;        /* RX_RIPETI; max -1 = no limit */
    unsigned char *insieme;         /* RX_INSIEME: 256 flags */
    struct RxNodo *figlio;          /* RX_GRUPPO, RX_RIPETI: the body; RX_SCELTA: first branch */
    struct RxNodo *altro;           /* RX_SCELTA: the next branch (a chain of RX_SCELTA) */
    struct RxNodo *dopo;            /* the next in the sequence */
} RxNodo;

typedef struct {
    RxNodo  *radice;
    RxNodo **nodi;                  /* every node, to free them */
    size_t   n, posti;
    int      flags;
    size_t   ngruppi;
} RxProg;

/* --- compiling ------------------------------------------------------------------ */

typedef struct {
    const char *p;
    RxProg     *prog;
    int         ere, icase, err;
    size_t      gruppi;
    int         aperti;             /* open groups: ')' closes only these in ERE */
} RxParser;

static RxNodo *rx_nuovo(RxParser *ps, int tipo)
{
    RxProg *pr = ps->prog;
    RxNodo *n;

    if (pr->n == pr->posti) {
        size_t   np = pr->posti ? pr->posti * 2 : 32;
        RxNodo **v = (RxNodo **)realloc(pr->nodi, np * sizeof(RxNodo *));
        if (!v) { ps->err = REG_ESPACE; return NULL; }
        pr->nodi = v; pr->posti = np;
    }
    n = (RxNodo *)calloc(1, sizeof(RxNodo));
    if (!n) { ps->err = REG_ESPACE; return NULL; }
    n->tipo = tipo;
    pr->nodi[pr->n++] = n;
    return n;
}

static int rx_classe(const char *nome, size_t l, int c)
{
#define RX_E(s) (l == sizeof(s) - 1 && memcmp(nome, s, l) == 0)
    if (RX_E("alnum"))  return isalnum(c);
    if (RX_E("alpha"))  return isalpha(c);
    if (RX_E("blank"))  return c == ' ' || c == '\t';
    if (RX_E("cntrl"))  return iscntrl(c);
    if (RX_E("digit"))  return isdigit(c);
    if (RX_E("graph"))  return isgraph(c);
    if (RX_E("lower"))  return islower(c);
    if (RX_E("print"))  return isprint(c);
    if (RX_E("punct"))  return ispunct(c);
    if (RX_E("space"))  return isspace(c);
    if (RX_E("upper"))  return isupper(c);
    if (RX_E("xdigit")) return isxdigit(c);
#undef RX_E
    return -1;
}

static void rx_metti(unsigned char *ins, int c, int icase)
{
    ins[c & 255] = 1;
    if (icase) { ins[tolower(c) & 255] = 1; ins[toupper(c) & 255] = 1; }
}

/* after '[' */
static RxNodo *rx_insieme(RxParser *ps)
{
    RxNodo *n = rx_nuovo(ps, RX_INSIEME);
    int     nega = 0, primo = 1, i;

    if (!n) return NULL;
    n->insieme = (unsigned char *)calloc(256, 1);
    if (!n->insieme) { ps->err = REG_ESPACE; return NULL; }
    if (*ps->p == '^') { nega = 1; ps->p++; }
    for (;;) {
        int a, b;

        if (*ps->p == '\0') { ps->err = REG_EBRACK; return NULL; }
        if (*ps->p == ']' && !primo) { ps->p++; break; }
        primo = 0;
        if (ps->p[0] == '[' && (ps->p[1] == ':' || ps->p[1] == '.' || ps->p[1] == '=')) {
            char        k = ps->p[1];
            const char *f = ps->p + 2;

            while (*f && !(f[0] == k && f[1] == ']')) f++;
            if (!*f) { ps->err = REG_EBRACK; return NULL; }
            if (k != ':') { ps->err = REG_ECOLLATE; return NULL; }
            if (rx_classe(ps->p + 2, (size_t)(f - ps->p - 2), 'a') < 0) { ps->err = REG_ECTYPE; return NULL; }
            for (i = 0; i < 256; i++)
                if (rx_classe(ps->p + 2, (size_t)(f - ps->p - 2), i) > 0) rx_metti(n->insieme, i, ps->icase);
            ps->p = f + 2;
            continue;
        }
        a = (unsigned char)*ps->p++;
        b = a;
        if (ps->p[0] == '-' && ps->p[1] != ']' && ps->p[1] != '\0') {
            ps->p++;
            b = (unsigned char)*ps->p++;
            if (b < a) { ps->err = REG_ERANGE; return NULL; }
        }
        for (i = a; i <= b; i++) rx_metti(n->insieme, i, ps->icase);
    }
    if (nega) {
        for (i = 0; i < 256; i++) n->insieme[i] = !n->insieme[i];
        if (ps->prog->flags & REG_NEWLINE) n->insieme['\n'] = 0;
    }
    n->insieme[0] = 0;
    return n;
}

static RxNodo *rx_scelta(RxParser *ps);

/* {m}, {m,}, {m,n}: after '{' (or '\{'); 0 = done, the numbers in *mn, *mx */
static int rx_graffe(RxParser *ps, int *mn, int *mx)
{
    long a = -1, b;

    if (!isdigit((unsigned char)*ps->p)) { ps->err = REG_BADBR; return -1; }
    a = 0;
    while (isdigit((unsigned char)*ps->p)) { a = a * 10 + (*ps->p++ - '0'); if (a > REGEX_RIP_MAX) { ps->err = REG_BADBR; return -1; } }
    b = a;
    if (*ps->p == ',') {
        ps->p++;
        if (isdigit((unsigned char)*ps->p)) {
            b = 0;
            while (isdigit((unsigned char)*ps->p)) { b = b * 10 + (*ps->p++ - '0'); if (b > REGEX_RIP_MAX) { ps->err = REG_BADBR; return -1; } }
            if (b < a) { ps->err = REG_BADBR; return -1; }
        } else b = -1;
    }
    if (ps->ere) {
        if (*ps->p != '}') { ps->err = REG_EBRACE; return -1; }
        ps->p++;
    } else {
        if (ps->p[0] != '\\' || ps->p[1] != '}') { ps->err = REG_EBRACE; return -1; }
        ps->p += 2;
    }
    *mn = (int)a; *mx = (int)b;
    return 0;
}

/* one atom, or NULL with ps->err (or NULL without error: the end of a sequence) */
static RxNodo *rx_atomo(RxParser *ps, int inizio)
{
    RxNodo *n;
    char    c = *ps->p;

    if (c == '\0') return NULL;
    if (ps->ere) {
        if (c == '|' || (c == ')' && ps->aperti)) return NULL;
        if (c == '*' || c == '+' || c == '?' || c == '{') {
            if (c == '{' && !isdigit((unsigned char)ps->p[1])) goto letterale;
            ps->err = REG_BADRPT; return NULL;
        }
        if (c == '(') {
            size_t k = ++ps->gruppi;

            ps->p++;
            ps->aperti++;
            n = rx_nuovo(ps, RX_GRUPPO);
            if (!n) return NULL;
            n->c = (int)k;
            if (*ps->p == ')') n->figlio = rx_nuovo(ps, RX_VUOTO);
            else n->figlio = rx_scelta(ps);
            if (ps->err) return NULL;
            if (*ps->p != ')') { ps->err = REG_EPAREN; return NULL; }
            ps->p++;
            ps->aperti--;
            return n;
        }
        if (c == '^') { ps->p++; return rx_nuovo(ps, RX_INIZIO); }
        if (c == '$') { ps->p++; return rx_nuovo(ps, RX_FINE); }
    } else {
        if (c == '\\' && (ps->p[1] == ')' || ps->p[1] == '|')) return NULL;
        if (c == '\\' && ps->p[1] == '(') {
            size_t k = ++ps->gruppi;

            ps->p += 2;
            ps->aperti++;
            n = rx_nuovo(ps, RX_GRUPPO);
            if (!n) return NULL;
            n->c = (int)k;
            if (ps->p[0] == '\\' && ps->p[1] == ')') n->figlio = rx_nuovo(ps, RX_VUOTO);
            else n->figlio = rx_scelta(ps);
            if (ps->err) return NULL;
            if (ps->p[0] != '\\' || ps->p[1] != ')') { ps->err = REG_EPAREN; return NULL; }
            ps->p += 2;
            ps->aperti--;
            return n;
        }
        if (c == '^' && inizio) { ps->p++; return rx_nuovo(ps, RX_INIZIO); }
        if (c == '$' && (ps->p[1] == '\0' || (ps->p[1] == '\\' && (ps->p[2] == ')' || ps->p[2] == '|')))) {
            ps->p++; return rx_nuovo(ps, RX_FINE);
        }
        if (c == '*' && inizio) goto letterale;
        if (c == '\\' && ps->p[1] >= '1' && ps->p[1] <= '9') {
            int k = ps->p[1] - '0';

            if ((size_t)k > ps->gruppi) { ps->err = REG_ESUBREG; return NULL; }
            ps->p += 2;
            n = rx_nuovo(ps, RX_RIF);
            if (n) n->c = k;
            return n;
        }
    }
    if (c == '.') {
        ps->p++;
        return rx_nuovo(ps, RX_QUALSIASI);
    }
    if (c == '[') {
        ps->p++;
        return rx_insieme(ps);
    }
    if (c == '\\') {
        if (ps->p[1] == '\0') { ps->err = REG_EESCAPE; return NULL; }
        ps->p++;
        c = *ps->p;
    }
letterale:
    ps->p++;
    n = rx_nuovo(ps, RX_CAR);
    if (n) n->c = ps->icase ? tolower((unsigned char)c) : (unsigned char)c;
    return n;
}

/* the quantifiers after an atom */
static RxNodo *rx_quantifica(RxParser *ps, RxNodo *a)
{
    for (;;) {
        int mn, mx;
        RxNodo *r;
        const char *q = ps->p;

        if (*q == '*') { mn = 0; mx = -1; ps->p++; }
        else if (ps->ere && *q == '+') { mn = 1; mx = -1; ps->p++; }
        else if (ps->ere && *q == '?') { mn = 0; mx = 1; ps->p++; }
        else if (!ps->ere && q[0] == '\\' && q[1] == '+') { mn = 1; mx = -1; ps->p += 2; }
        else if (!ps->ere && q[0] == '\\' && q[1] == '?') { mn = 0; mx = 1; ps->p += 2; }
        else if (ps->ere && *q == '{' && isdigit((unsigned char)q[1])) {
            ps->p++;
            if (rx_graffe(ps, &mn, &mx) != 0) return NULL;
        } else if (!ps->ere && q[0] == '\\' && q[1] == '{') {
            ps->p += 2;
            if (rx_graffe(ps, &mn, &mx) != 0) return NULL;
        } else return a;
        if (a->tipo == RX_INIZIO || a->tipo == RX_FINE) {
            if (ps->ere) { ps->err = REG_BADRPT; return NULL; }
        }
        r = rx_nuovo(ps, RX_RIPETI);
        if (!r) return NULL;
        r->figlio = a; r->min = mn; r->max = mx;
        a = r;
    }
}

static RxNodo *rx_sequenza(RxParser *ps)
{
    RxNodo *testa = NULL, *coda = NULL;
    int     inizio = 1;

    for (;;) {
        RxNodo *a = rx_atomo(ps, inizio);

        if (ps->err) return NULL;
        if (!a) break;
        inizio = a->tipo == RX_INIZIO && !ps->ere;
        a = rx_quantifica(ps, a);
        if (!a) return NULL;
        if (coda) coda->dopo = a; else testa = a;
        coda = a;
    }
    if (!testa) testa = rx_nuovo(ps, RX_VUOTO);
    return testa;
}

static RxNodo *rx_scelta(RxParser *ps)
{
    RxNodo *prima = rx_sequenza(ps), *s, *ultimo;

    if (!prima) return NULL;
    if (!(ps->ere ? *ps->p == '|' : (ps->p[0] == '\\' && ps->p[1] == '|'))) return prima;
    s = rx_nuovo(ps, RX_SCELTA);
    if (!s) return NULL;
    s->figlio = prima;
    ultimo = s;
    while (ps->ere ? *ps->p == '|' : (ps->p[0] == '\\' && ps->p[1] == '|')) {
        RxNodo *b, *t;

        ps->p += ps->ere ? 1 : 2;
        b = rx_sequenza(ps);
        if (!b) return NULL;
        t = rx_nuovo(ps, RX_SCELTA);
        if (!t) return NULL;
        t->figlio = b;
        ultimo->altro = t;
        ultimo = t;
    }
    return s;
}

static void rx_libera(RxProg *pr)
{
    size_t i;

    if (!pr) return;
    for (i = 0; i < pr->n; i++) { free(pr->nodi[i]->insieme); free(pr->nodi[i]); }
    free(pr->nodi);
    free(pr);
}

int regcomp(regex_t *re, const char *modello, int flags)
{
    RxParser ps;
    RxProg  *pr;

    if (!re || !modello) return REG_BADPAT;
    pr = (RxProg *)calloc(1, sizeof(RxProg));
    if (!pr) return REG_ESPACE;
    pr->flags = flags;
    memset(&ps, 0, sizeof ps);
    ps.p = modello; ps.prog = pr;
    ps.ere = (flags & REG_EXTENDED) != 0;
    ps.icase = (flags & REG_ICASE) != 0;
    pr->radice = rx_scelta(&ps);
    if (!ps.err && *ps.p != '\0') ps.err = ps.ere ? REG_EPAREN : REG_EPAREN;
    if (ps.err) { rx_libera(pr); return ps.err; }
    pr->ngruppi = ps.gruppi;
    re->re_nsub = ps.gruppi;
    re->__exos_rx = pr;
    return 0;
}

void regfree(regex_t *re)
{
    if (!re) return;
    rx_libera((RxProg *)re->__exos_rx);
    re->__exos_rx = NULL;
}

/* --- matching ------------------------------------------------------------------- */

/* what comes after a node: the next in its sequence, then the continuation */
typedef struct RxSeguito {
    int                      tipo;      /* 0 = a node, 1 = close a group, 2 = one more turn of a repeat */
    const RxNodo            *n;
    int                      volte;     /* tipo 2: how many turns done */
    const char              *da;        /* tipo 2: where this turn started */
    const struct RxSeguito  *poi;
} RxSeguito;

typedef struct {
    const RxProg *pr;
    const char   *testo, *fine;
    int           eflags;
    long          passi;
    regoff_t     *so, *eo;              /* the groups now (0 = the whole match) */
    regoff_t     *best_so, *best_eo;
    const char   *best;                 /* the longest end found, or NULL */
    size_t        ng;
    int           troppo;
} RxStato;

static int rx_prova(RxStato *st, const RxNodo *n, const char *s, const RxSeguito *k);

static int rx_avanti(RxStato *st, const char *s, const RxSeguito *k)
{
    if (!k) {
        /* a way to the end: keep it if it is the longest so far */
        if (!st->best || s > st->best) {
            st->best = s;
            memcpy(st->best_so, st->so, (st->ng + 1) * sizeof(regoff_t));
            memcpy(st->best_eo, st->eo, (st->ng + 1) * sizeof(regoff_t));
            st->best_eo[0] = (regoff_t)(s - st->testo);
        }
        return s == st->fine;               /* nothing can be longer: stop */
    }
    if (k->tipo == 0) return rx_prova(st, k->n, s, k->poi);
    if (k->tipo == 1) {
        int      g = k->n->c, r;
        regoff_t vecchio = st->eo[g];

        st->eo[g] = (regoff_t)(s - st->testo);
        r = rx_avanti(st, s, k->poi);
        st->eo[g] = vecchio;
        return r;
    }
    /* tipo 2: one turn of a repeat is over */
    {
        const RxNodo *r = k->n;
        int           volte = k->volte + 1;

        if (s == k->da && volte > r->min) return 0;    /* an empty turn: no loop */
        if (r->max < 0 || volte < r->max) {
            RxSeguito ancora = { 2, r, volte, s, k->poi };
            if (rx_prova(st, r->figlio, s, &ancora)) return 1;
            if (st->troppo) return 0;
        }
        if (volte >= r->min) {
            if (r->dopo) { RxSeguito d = { 0, r->dopo, 0, 0, k->poi }; return rx_avanti(st, s, &d); }
            return rx_avanti(st, s, k->poi);
        }
        return 0;
    }
}

static int rx_car_uguale(const RxStato *st, int a, int b)
{
    if (st->pr->flags & REG_ICASE) return tolower(a) == tolower(b);
    return a == b;
}

static int rx_prova(RxStato *st, const RxNodo *n, const char *s, const RxSeguito *k)
{
    RxSeguito d;
    const RxSeguito *poi;

    if (++st->passi > REGEX_PASSI) { st->troppo = 1; return 0; }
    if (n->dopo) { d.tipo = 0; d.n = n->dopo; d.volte = 0; d.da = 0; d.poi = k; poi = &d; }
    else poi = k;

    switch (n->tipo) {
    case RX_VUOTO:
        return rx_avanti(st, s, poi);
    case RX_CAR:
        if (s >= st->fine || !rx_car_uguale(st, (unsigned char)*s, n->c)) return 0;
        return rx_avanti(st, s + 1, poi);
    case RX_QUALSIASI:
        if (s >= st->fine) return 0;
        if (*s == '\n' && (st->pr->flags & REG_NEWLINE)) return 0;
        return rx_avanti(st, s + 1, poi);
    case RX_INSIEME:
        if (s >= st->fine || !n->insieme[(unsigned char)*s]) return 0;
        return rx_avanti(st, s + 1, poi);
    case RX_INIZIO:
        if (s == st->testo ? (st->eflags & REG_NOTBOL)
                           : !((st->pr->flags & REG_NEWLINE) && s[-1] == '\n'))
            return 0;
        return rx_avanti(st, s, poi);
    case RX_FINE:
        if (s == st->fine ? (st->eflags & REG_NOTEOL)
                          : !((st->pr->flags & REG_NEWLINE) && *s == '\n'))
            return 0;
        return rx_avanti(st, s, poi);
    case RX_GRUPPO: {
        int       g = n->c, r;
        regoff_t  vs = st->so[g], ve = st->eo[g];
        RxSeguito chiudi = { 1, n, 0, 0, poi };

        st->so[g] = (regoff_t)(s - st->testo);
        st->eo[g] = -1;
        r = rx_prova(st, n->figlio, s, &chiudi);
        st->so[g] = vs; st->eo[g] = ve;
        return r;
    }
    case RX_SCELTA: {
        const RxNodo *b;

        for (b = n; b; b = b->altro) {
            if (rx_prova(st, b->figlio, s, poi)) return 1;
            if (st->troppo) return 0;
        }
        return 0;
    }
    case RX_RIPETI: {
        /* zero turns first counts as done when min is 0; then the turns */
        RxSeguito turno = { 2, n, 0, s, k };

        if (n->max != 0 && rx_prova(st, n->figlio, s, &turno)) return 1;
        if (st->troppo) return 0;
        if (n->min == 0) return rx_avanti(st, s, poi);
        return 0;
    }
    case RX_RIF: {
        int    g = n->c;
        size_t l;

        if (st->so[g] < 0 || st->eo[g] < 0) return 0;
        l = (size_t)(st->eo[g] - st->so[g]);
        if ((size_t)(st->fine - s) < l) return 0;
        if (st->pr->flags & REG_ICASE) {
            size_t i;
            for (i = 0; i < l; i++)
                if (tolower((unsigned char)s[i]) != tolower((unsigned char)st->testo[st->so[g] + i])) return 0;
        } else if (memcmp(s, st->testo + st->so[g], l) != 0) return 0;
        return rx_avanti(st, s + l, poi);
    }
    }
    return 0;
}

int regexec(const regex_t *re, const char *testo, size_t nmatch, regmatch_t *pm, int eflags)
{
    const RxProg *pr;
    RxStato       st;
    regoff_t     *v;
    const char   *s;
    size_t        i, ng;
    int           esito = REG_NOMATCH;

    if (!re || !re->__exos_rx || !testo) return REG_BADPAT;
    pr = (const RxProg *)re->__exos_rx;
    ng = pr->ngruppi;
    v = (regoff_t *)malloc(4 * (ng + 1) * sizeof(regoff_t));
    if (!v) return REG_ESPACE;
    memset(&st, 0, sizeof st);
    st.pr = pr; st.testo = testo; st.fine = testo + strlen(testo);
    st.eflags = eflags; st.ng = ng;
    st.so = v; st.eo = v + ng + 1; st.best_so = v + 2 * (ng + 1); st.best_eo = v + 3 * (ng + 1);

    for (s = testo; ; s++) {
        for (i = 0; i <= ng; i++) st.so[i] = st.eo[i] = -1;
        st.so[0] = (regoff_t)(s - testo);
        st.best = NULL;
        rx_prova(&st, pr->radice, s, NULL);
        if (st.best) {
            esito = 0;
            st.best_so[0] = (regoff_t)(s - testo);
            break;
        }
        if (st.troppo) { esito = REG_ESPACE; break; }
        if (s >= st.fine) break;
    }
    if (esito == 0 && !(pr->flags & REG_NOSUB) && pm) {
        for (i = 0; i < nmatch; i++) {
            if (i <= ng && st.best_so[i] >= 0 && st.best_eo[i] >= 0) {
                pm[i].rm_so = st.best_so[i]; pm[i].rm_eo = st.best_eo[i];
            } else {
                pm[i].rm_so = pm[i].rm_eo = -1;
            }
        }
    }
    free(v);
    return esito;
}

size_t regerror(int err, const regex_t *re, char *buf, size_t n)
{
    static const char *const testi[] = {
        "Success", "No match", "Invalid regular expression",
        "Invalid collation character", "Invalid character class name",
        "Trailing backslash", "Invalid back reference", "Unmatched [ or [^",
        "Unmatched ( or \\(", "Unmatched \\{", "Invalid content of \\{\\}",
        "Invalid range end", "Memory exhausted", "Invalid preceding regular expression"
    };
    const char *t = (err >= 0 && err < (int)(sizeof testi / sizeof testi[0])) ? testi[err] : "Unknown error";
    size_t      l = strlen(t) + 1;

    (void)re;
    if (buf && n) {
        size_t c = l < n ? l : n;
        memcpy(buf, t, c - 1);
        buf[c - 1] = '\0';
    }
    return l;
}
