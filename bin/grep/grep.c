/* =============================================================================
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     grep [-EFGivcnlLoqsHhrwxa] [-e MODELLO]... [MODELLO] [FILE...]
 *
 * With no FILE it reads standard input (with -r the current folder). Exit
 * code as POSIX: 0 found, 1 not found, 2 error.
 *
 * The libc has no regcomp, so the regular expressions are here: a parser to a
 * small tree, the tree to a program, and the program run as a set of states
 * (Thompson's way). ! NOT BACKTRACKING: a(a|a)*b on a long line would take
 * years, and a log of Firefox is tens of megabytes. Time is lines times
 * pattern, whatever the pattern. {m,n} copies the tree, as a loop has no
 * memory in a set of states.
 *
 * ! A MODEL WITHOUT METACHARACTERS DOES NOT GO THROUGH THE AUTOMATON: most
 * searches are plain words, and looking for the first byte is far faster.
 * ============================================================================= */
#include "libc.h"

EX_VERSIONE("grep", "0.001");

/* --- the tree ------------------------------------------------------------ */
enum { N_VUOTO, N_BYTE, N_SET, N_QUALUNQUE, N_INIZIO, N_FINE, N_CAT, N_ALT, N_RIP };

typedef struct Nodo {
    int            tipo;
    int            byte;
    unsigned char *set;         /* 32 bytes */
    struct Nodo   *a, *b;
    int            min, max;    /* N_RIP: max -1 = no limit */
} Nodo;

/* --- the program --------------------------------------------------------- */
enum { I_BYTE, I_SET, I_QUALUNQUE, I_INIZIO, I_FINE, I_SPLIT, I_JMP, I_FATTO };

typedef struct {
    int            op;
    int            byte;
    unsigned char *set;
    int            x, y;
} Istr;

typedef struct {
    Istr          *p;
    int            n, posti;
    unsigned char *lett;        /* -F or no metacharacters: the word itself */
    unsigned int   lett_n;
} Modello;

static int g_i, g_v, g_c, g_n, g_l, g_ll, g_o, g_q, g_s, g_r, g_w, g_x, g_a;
static int g_nomi = -1;         /* -H 1, -h 0, -1 by the number of files */
static int g_ere, g_fisso;
static int g_errori;

static Modello *g_mod;
static int      g_nmod;

static const char *g_p;         /* where the parser is */

/* On the error stream: a pipe must carry only the lines found */
static void errore(const char *nome, const char *m)
{
    char b[320];
    int  n = nome ? snprintf(b, sizeof(b), "grep: %s: %s\n", nome, m)
                  : snprintf(b, sizeof(b), "grep: %s\n", m);

    if (n > (int)sizeof(b)) n = (int)sizeof(b);
    (void)write(2, b, (unsigned int)n);
}

static void muori(const char *m)
{
    errore(0, m);
    exit(2);
}

static void *prendi(size_t n)
{
    void *p = malloc(n);

    if (!p) muori("memoria esaurita");
    memset(p, 0, n);
    return p;
}

static int minusc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static int maiusc(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static int parola(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') || c == '_';
}

static Nodo *nodo(int t, Nodo *a, Nodo *b)
{
    Nodo *n = prendi(sizeof(Nodo));

    n->tipo = t; n->a = a; n->b = b;
    return n;
}

static void set_metti(unsigned char *s, int c)
{
    s[c >> 3] |= (unsigned char)(1u << (c & 7));
    if (g_i) {
        c = minusc(c); s[c >> 3] |= (unsigned char)(1u << (c & 7));
        c = maiusc(c); s[c >> 3] |= (unsigned char)(1u << (c & 7));
    }
}

static int set_ha(const unsigned char *s, int c) { return (s[c >> 3] >> (c & 7)) & 1; }

static void set_inverti(unsigned char *s)
{
    int k;

    for (k = 0; k < 32; k++) s[k] = (unsigned char)~s[k];
    s['\n' >> 3] &= (unsigned char)~(1u << ('\n' & 7));
}

static int e_classe(const char *nome, int l, int c)
{
#define CL(s) (l == (int)sizeof(s) - 1 && memcmp(nome, s, (size_t)l) == 0)
    if (CL("alpha")) return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    if (CL("digit")) return c >= '0' && c <= '9';
    if (CL("alnum")) return parola(c) && c != '_';
    if (CL("upper")) return c >= 'A' && c <= 'Z';
    if (CL("lower")) return c >= 'a' && c <= 'z';
    if (CL("space")) return c == ' ' || (c >= 9 && c <= 13);
    if (CL("blank")) return c == ' ' || c == '\t';
    if (CL("punct")) return c > 32 && c < 127 && !(parola(c) && c != '_');
    if (CL("print")) return c >= 32 && c < 127;
    if (CL("graph")) return c > 32 && c < 127;
    if (CL("cntrl")) return c < 32 || c == 127;
    if (CL("xdigit")) return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                             (c >= 'A' && c <= 'F');
#undef CL
    muori("classe di caratteri sconosciuta");
    return 0;
}

/* [...]: g_p is after the '[' */
static Nodo *leggi_set(void)
{
    Nodo *n = nodo(N_SET, 0, 0);
    int   neg = 0, primo = 1, c, k;

    n->set = prendi(32);
    if (*g_p == '^') { neg = 1; g_p++; }
    for (;;) {
        if (!*g_p) muori("manca la ] di chiusura");
        if (*g_p == ']' && !primo) { g_p++; break; }
        primo = 0;
        if (g_p[0] == '[' && g_p[1] == ':') {
            const char *f = strstr(g_p + 2, ":]");

            if (!f) muori("manca :] nella classe");
            for (k = 0; k < 256; k++)
                if (e_classe(g_p + 2, (int)(f - g_p - 2), k)) set_metti(n->set, k);
            g_p = f + 2;
            continue;
        }
        c = (unsigned char)*g_p++;
        if (g_p[0] == '-' && g_p[1] && g_p[1] != ']') {
            int fine = (unsigned char)g_p[1];

            if (fine < c) muori("intervallo rovesciato in [ ]");
            for (k = c; k <= fine; k++) set_metti(n->set, k);
            g_p += 2;
        } else set_metti(n->set, c);
    }
    if (neg) set_inverti(n->set);
    return n;
}

static Nodo *set_di(const char *quali, int neg)
{
    Nodo *n = nodo(N_SET, 0, 0);
    int   k, l = (int)strlen(quali);

    n->set = prendi(32);
    for (k = 0; k < 256; k++)
        if (quali[0] == 'w' ? parola(k) : e_classe(quali, l, k)) set_metti(n->set, k);
    if (neg) set_inverti(n->set);
    return n;
}

/* ! THE SAME TOKENS IN THE TWO DIALECTS: in a basic expression \( \) \| \+ \?
 * \{ \} are the operators and the plain characters are themselves, in an
 * extended one it is the other way round. */
static int e_op(const char *p, char c, int *lung)
{
    if (g_ere) {
        *lung = 1;
        return p[0] == c;
    }
    *lung = 2;
    return p[0] == '\\' && p[1] == c;
}

static Nodo *leggi_alt(void);

static Nodo *leggi_atomo(void)
{
    int   l, c;
    Nodo *n;

    if (e_op(g_p, '(', &l)) {
        g_p += l;
        n = leggi_alt();
        if (!e_op(g_p, ')', &l)) muori("manca la ) di chiusura");
        g_p += l;
        return n;
    }
    c = (unsigned char)*g_p;
    if (c == '.') { g_p++; return nodo(N_QUALUNQUE, 0, 0); }
    if (c == '[') { g_p++; return leggi_set(); }
    if (c == '^') { g_p++; return nodo(N_INIZIO, 0, 0); }
    if (c == '$') { g_p++; return nodo(N_FINE, 0, 0); }
    if (c == '\\') {
        g_p++;
        c = (unsigned char)*g_p;
        if (!c) muori("\\ alla fine del modello");
        g_p++;
        switch (c) {
        case 'w': return set_di("w", 0);
        case 'W': return set_di("w", 1);
        case 'd': return set_di("digit", 0);
        case 'D': return set_di("digit", 1);
        case 's': return set_di("space", 0);
        case 'S': return set_di("space", 1);
        }
    } else g_p++;
    n = nodo(N_BYTE, 0, 0);
    n->byte = g_i ? minusc(c) : c;
    if (g_i && maiusc(c) != minusc(c)) {
        n->tipo = N_SET;
        n->set = prendi(32);
        set_metti(n->set, c);
    }
    return n;
}

static Nodo *leggi_rip(void)
{
    Nodo *n = leggi_atomo();
    int   l;

    for (;;) {
        Nodo *r;
        int   mn, mx;

        if (*g_p == '*') { g_p++; mn = 0; mx = -1; }
        else if (e_op(g_p, '+', &l)) { g_p += l; mn = 1; mx = -1; }
        else if (e_op(g_p, '?', &l)) { g_p += l; mn = 0; mx = 1; }
        else if (e_op(g_p, '{', &l)) {
            g_p += l;
            mn = 0;
            while (*g_p >= '0' && *g_p <= '9') mn = mn * 10 + (*g_p++ - '0');
            mx = mn;
            if (*g_p == ',') {
                g_p++;
                mx = -1;
                if (*g_p >= '0' && *g_p <= '9') {
                    mx = 0;
                    while (*g_p >= '0' && *g_p <= '9') mx = mx * 10 + (*g_p++ - '0');
                }
            }
            if (!e_op(g_p, '}', &l)) muori("manca la } di chiusura");
            g_p += l;
            if (mx >= 0 && mx < mn) muori("ripetizione rovesciata in { }");
        } else return n;
        r = nodo(N_RIP, n, 0);
        r->min = mn; r->max = mx;
        n = r;
    }
}

static Nodo *leggi_cat(void)
{
    Nodo *n = nodo(N_VUOTO, 0, 0);
    int   l, primo = 1;

    while (*g_p && !e_op(g_p, '|', &l) && !e_op(g_p, ')', &l)) {
        Nodo *d = leggi_rip();

        n = primo ? d : nodo(N_CAT, n, d);
        primo = 0;
    }
    return n;
}

static Nodo *leggi_alt(void)
{
    Nodo *n = leggi_cat();
    int   l;

    while (e_op(g_p, '|', &l)) {
        g_p += l;
        n = nodo(N_ALT, n, leggi_cat());
    }
    return n;
}

static int emetti(Modello *m, int op)
{
    if (m->n == m->posti) {
        Istr *q;

        m->posti = m->posti ? m->posti * 2 : 64;
        q = realloc(m->p, (size_t)m->posti * sizeof(Istr));
        if (!q) muori("memoria esaurita");
        m->p = q;
    }
    memset(&m->p[m->n], 0, sizeof(Istr));
    m->p[m->n].op = op;
    return m->n++;
}

static void genera(Modello *m, const Nodo *n)
{
    int a, b, k;

    switch (n->tipo) {
    case N_VUOTO: break;
    case N_BYTE: a = emetti(m, I_BYTE); m->p[a].byte = n->byte; break;
    case N_SET:  a = emetti(m, I_SET);  m->p[a].set  = n->set;  break;
    case N_QUALUNQUE: emetti(m, I_QUALUNQUE); break;
    case N_INIZIO: emetti(m, I_INIZIO); break;
    case N_FINE:   emetti(m, I_FINE);   break;
    case N_CAT: genera(m, n->a); genera(m, n->b); break;
    case N_ALT:
        a = emetti(m, I_SPLIT);
        m->p[a].x = m->n;
        genera(m, n->a);
        b = emetti(m, I_JMP);
        m->p[a].y = m->n;
        genera(m, n->b);
        m->p[b].x = m->n;
        break;
    case N_RIP:
        for (k = 0; k < n->min; k++) genera(m, n->a);
        if (n->max < 0) {
            a = emetti(m, I_SPLIT);
            m->p[a].x = m->n;
            genera(m, n->a);
            b = emetti(m, I_JMP);
            m->p[b].x = a;
            m->p[a].y = m->n;
        } else for (k = n->min; k < n->max; k++) {
            a = emetti(m, I_SPLIT);
            m->p[a].x = m->n;
            genera(m, n->a);
            m->p[a].y = -1;     /* filled below: past all the optional copies */
        }
        if (n->max >= 0)
            for (k = m->n - 1; k >= 0 && n->max > n->min; k--)
                if (m->p[k].op == I_SPLIT && m->p[k].y == -1) m->p[k].y = m->n;
        break;
    }
}

static void compila(const char *testo)
{
    Modello *m;
    Nodo    *n;
    int      l;

    g_mod = realloc(g_mod, (size_t)(g_nmod + 1) * sizeof(Modello));
    if (!g_mod) muori("memoria esaurita");
    m = &g_mod[g_nmod++];
    memset(m, 0, sizeof(*m));

    /* a plain word, whatever the dialect */
    if (g_fisso || !strpbrk(testo, g_ere ? ".[]()*+?{}|^$\\" : ".[*^$\\")) {
        l = (int)strlen(testo);
        if (!g_x && !g_o && !g_w) {
            int k;

            m->lett = prendi((size_t)l + 1);
            for (k = 0; k < l; k++)
                m->lett[k] = (unsigned char)(g_i ? minusc((unsigned char)testo[k]) : testo[k]);
            m->lett_n = (unsigned int)l;
            return;
        }
        if (g_fisso) {          /* -F with -x, -o, -w: letter by letter */
            int k;

            n = nodo(N_VUOTO, 0, 0);
            for (k = 0; k < l; k++) {
                Nodo *d = nodo(N_BYTE, 0, 0);
                int   c = (unsigned char)testo[k];

                d->byte = g_i ? minusc(c) : c;
                if (g_i && maiusc(c) != minusc(c)) {
                    d->tipo = N_SET;
                    d->set = prendi(32);
                    set_metti(d->set, c);
                }
                n = k ? nodo(N_CAT, n, d) : d;
            }
            goto albero;
        }
    }
    g_p = testo;
    n = leggi_alt();
    if (*g_p) muori("modello non valido: ) senza (");
albero:
    if (g_x) n = nodo(N_CAT, nodo(N_CAT, nodo(N_INIZIO, 0, 0), n), nodo(N_FINE, 0, 0));
    genera(m, n);
    emetti(m, I_FATTO);
}

/* --- running it ---------------------------------------------------------- */
static int *g_ora, *g_dopo, *g_visto;
static int  g_giro, g_stati;

static void pronti(int n)
{
    if (n <= g_stati) return;
    free(g_ora); free(g_dopo); free(g_visto);
    g_stati = n;
    g_ora   = prendi((size_t)n * sizeof(int));
    g_dopo  = prendi((size_t)n * sizeof(int));
    g_visto = prendi((size_t)n * sizeof(int));
}

/* Follows splits and anchors; returns 1 if I_FATTO was reached. The stack is
 * the explicit list: a recursion here would be as deep as the pattern. */
static int aggiungi(const Modello *m, int *lista, int *n, int pc, int pos, int len)
{
    static int *pila;
    static int  posti;
    int sp = 0, fatto = 0;

    if (posti < m->n * 2 + 2) {
        posti = m->n * 2 + 2;
        free(pila);
        pila = prendi((size_t)posti * sizeof(int));
    }
    pila[sp++] = pc;
    while (sp > 0) {
        const Istr *i;

        pc = pila[--sp];
        if (g_visto[pc] == g_giro) continue;
        g_visto[pc] = g_giro;
        i = &m->p[pc];
        switch (i->op) {
        case I_JMP:    pila[sp++] = i->x; break;
        case I_SPLIT:  pila[sp++] = i->y; pila[sp++] = i->x; break;
        case I_INIZIO: if (pos == 0)   pila[sp++] = pc + 1; break;
        case I_FINE:   if (pos == len) pila[sp++] = pc + 1; break;
        case I_FATTO:  fatto = 1; break;
        default:       lista[(*n)++] = pc; break;
        }
    }
    return fatto;
}

/* Is there a match in the line? from = -1 anywhere, stopping at the first;
 * otherwise only one starting at `from`, and *fine says where the longest ends. */
static int esegui(const Modello *m, const unsigned char *r, int len, int from, int *fine)
{
    int pos = from < 0 ? 0 : from, nora = 0, ndopo, k, trovato = 0;
    int *a = g_ora, *b = g_dopo;

    g_giro++;
    if (aggiungi(m, a, &nora, 0, pos, len)) {
        if (from < 0) return 1;
        trovato = 1; *fine = pos;
    }
    for (; pos < len; pos++) {
        int c = r[pos], *t;

        if (g_i && m->p) c = minusc(c);
        ndopo = 0;
        g_giro++;
        for (k = 0; k < nora; k++) {
            const Istr *i = &m->p[a[k]];
            int ok;

            if (i->op == I_BYTE)     ok = i->byte == c;
            else if (i->op == I_SET) ok = set_ha(i->set, r[pos]);
            else                     ok = 1;
            if (ok && aggiungi(m, b, &ndopo, a[k] + 1, pos + 1, len)) {
                if (from < 0) return 1;
                trovato = 1; *fine = pos + 1;
            }
        }
        /* anywhere: a match may also begin at the next byte */
        if (from < 0 && aggiungi(m, b, &ndopo, 0, pos + 1, len)) return 1;
        t = a; a = b; b = t;
        nora = ndopo;
        if (!nora && from >= 0) break;
    }
    return trovato;
}

static int lett_cerca(const Modello *m, const unsigned char *r, int len)
{
    unsigned int n = m->lett_n;
    int          k, f;

    if (n == 0) return 1;
    f = m->lett[0];
    for (k = 0; k + (int)n <= len; k++) {
        const unsigned char *p;
        unsigned int j;

        if (g_i) {
            if (minusc(r[k]) != f) continue;
            for (j = 1; j < n && minusc(r[k + (int)j]) == m->lett[j]; j++) ;
            if (j == n) return 1;
            continue;
        }
        p = memchr(r + k, f, (size_t)(len - k - (int)n + 1));
        if (!p) return 0;
        k = (int)(p - r);
        if (memcmp(p, m->lett, n) == 0) return 1;
    }
    return 0;
}

/* The leftmost-longest match from `da`, for -o and -w; 1 and the two ends */
static int cerca_da(const unsigned char *r, int len, int da, int *ini, int *fin)
{
    int s, q;

    for (s = da; s <= len; s++) {
        int meglio = -1;

        for (q = 0; q < g_nmod; q++) {
            int f = -1;

            if (g_mod[q].lett) continue;
            if (esegui(&g_mod[q], r, len, s, &f) && f > meglio) meglio = f;
        }
        if (meglio < 0) continue;
        if (g_w && ((s > 0 && parola(r[s - 1])) ||
                    (meglio < len && parola(r[meglio])))) continue;
        *ini = s; *fin = meglio;
        return 1;
    }
    return 0;
}

static int riga_buona(const unsigned char *r, int len)
{
    int q, a, b;

    if (g_w) return cerca_da(r, len, 0, &a, &b);
    for (q = 0; q < g_nmod; q++) {
        const Modello *m = &g_mod[q];

        if (m->lett ? lett_cerca(m, r, len) : esegui(m, r, len, -1, &a)) return 1;
    }
    return 0;
}

/* --- out ----------------------------------------------------------------- */
static char         g_fuori[65536];
static unsigned int g_nf;

static void svuota(void)
{
    unsigned int k = 0;

    while (k < g_nf) {
        int s = write(1, g_fuori + k, g_nf - k);

        if (s <= 0) break;
        k += (unsigned int)s;
    }
    g_nf = 0;
}

static void scrivi(const void *p, unsigned int n)
{
    const char *c = p;

    while (n > 0) {
        unsigned int t = sizeof(g_fuori) - g_nf;

        if (t > n) t = n;
        memcpy(g_fuori + g_nf, c, t);
        g_nf += t; c += t; n -= t;
        if (g_nf == sizeof(g_fuori)) svuota();
    }
}

static void scrivis(const char *s) { scrivi(s, (unsigned int)strlen(s)); }

static void scrivin(unsigned long v)
{
    char b[24];
    int  n = 0;

    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n > 0) scrivi(&b[--n], 1);
}

static void testa(const char *nome, unsigned long riga)
{
    if (g_nomi > 0) { scrivis(nome); scrivis(":"); }
    if (g_n) { scrivin(riga); scrivis(":"); }
}

/* --- one file ------------------------------------------------------------ */
static unsigned char *g_buf;
static unsigned int   g_posti = 1u << 16;

static int cerca_fd(int fd, const char *nome)
{
    unsigned int  pieni = 0, inizio = 0;
    unsigned long riga = 0, conta = 0;
    int           fine_file = 0, binario = 0, primo = 1, smesso = 0;

    if (!g_buf) g_buf = prendi(g_posti);

    while (!smesso) {
        unsigned char *nl;
        unsigned int   l;
        int            buona;

        nl = pieni > inizio ? memchr(g_buf + inizio, '\n', pieni - inizio) : 0;
        if (!nl && !fine_file) {
            int s;

            if (inizio > 0) {
                memmove(g_buf, g_buf + inizio, pieni - inizio);
                pieni -= inizio;
                inizio = 0;
            }
            /* ! THE LINE GROWS THE BUFFER: no limit on how long one may be */
            if (pieni == g_posti) {
                unsigned char *q = realloc(g_buf, (size_t)g_posti * 2);

                if (!q) muori("memoria esaurita");
                g_buf = q;
                g_posti *= 2;
            }
            s = read(fd, g_buf + pieni, g_posti - pieni);
            if (s < 0) {
                if (!g_s) { svuota(); errore(nome, "errore di lettura"); }
                g_errori = 1;
                fine_file = 1;
            } else if (s == 0) fine_file = 1;
            else {
                if (primo && !g_a && memchr(g_buf + pieni, 0, (size_t)s)) binario = 1;
                primo = 0;
                pieni += (unsigned int)s;
            }
            continue;
        }
        if (!nl && inizio == pieni) break;
        l = nl ? (unsigned int)(nl - (g_buf + inizio)) : pieni - inizio;
        riga++;
        buona = riga_buona(g_buf + inizio, (int)l);
        if (g_v) buona = !buona;
        if (buona) {
            conta++;
            if (g_q) return 1;
            if (g_l || g_ll) smesso = 1;
            else if (g_c) ;
            else if (binario) smesso = 1;
            else if (g_o && !g_v) {
                int da = 0, a, b;

                while (cerca_da(g_buf + inizio, (int)l, da, &a, &b)) {
                    if (b > a) {
                        testa(nome, riga);
                        scrivi(g_buf + inizio + a, (unsigned int)(b - a));
                        scrivis("\n");
                    }
                    da = b > a ? b : b + 1;
                    if (da > (int)l) break;
                }
            } else if (!g_o) {
                testa(nome, riga);
                scrivi(g_buf + inizio, l);
                scrivis("\n");
            }
        }
        inizio += l + (nl ? 1u : 0u);
    }
    if (g_c) {
        if (g_nomi > 0) { scrivis(nome); scrivis(":"); }
        scrivin(conta);
        scrivis("\n");
    }
    if ((g_l && conta) || (g_ll && !conta)) { scrivis(nome); scrivis("\n"); }
    if (binario && conta && !g_c && !g_l && !g_ll) {
        scrivis("Il file binario "); scrivis(nome); scrivis(" corrisponde\n");
    }
    return conta != 0;
}

static int cerca_nome(const char *nome, int esplicito);

static int cerca_dir(const char *dir)
{
    DIR           *d = opendir(dir);
    struct dirent *e;
    int            trovato = 0;

    if (!d) {
        if (!g_s) { svuota(); errore(dir, "cartella non leggibile"); }
        g_errori = 1;
        return 0;
    }
    while ((e = readdir(d)) != 0) {
        char  *p;
        size_t l;

        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        l = strlen(dir) + strlen(e->d_name) + 2;
        p = prendi(l);
        snprintf(p, l, "%s/%s", dir, e->d_name);
        if (cerca_nome(p, 0)) trovato = 1;
        free(p);
        if (trovato && g_q) break;
    }
    closedir(d);
    return trovato;
}

static int cerca_nome(const char *nome, int esplicito)
{
    struct stat st;
    int         fd, r;

    if (stat(nome, &st) != 0) {
        if (!g_s) { svuota(); errore(nome, "file non trovato"); }
        g_errori = 1;
        return 0;
    }
    if (S_ISDIR(st.st_mode)) {
        if (g_r) return cerca_dir(nome);
        if (esplicito && !g_s) { svuota(); errore(nome, "e' una cartella"); }
        return 0;
    }
    fd = open(nome, O_RDONLY, 0);
    if (fd < 0) {
        if (!g_s) { svuota(); errore(nome, "non si apre"); }
        g_errori = 1;
        return 0;
    }
    r = cerca_fd(fd, nome);
    close(fd);
    return r;
}

int main(int argc, char **argv)
{
    const char **modelli = prendi((size_t)argc * sizeof(char *));
    const char **file    = prendi((size_t)argc * sizeof(char *));
    int nm = 0, nf = 0, k, trovato = 0, opzioni = 1;

    for (k = 1; k < argc; k++) {
        const char *a = argv[k];

        if (opzioni && strcmp(a, "--") == 0) { opzioni = 0; continue; }
        if (opzioni && a[0] == '-' && a[1]) {
            const char *o;

            for (o = a + 1; *o; o++) switch (*o) {
            case 'E': g_ere = 1; g_fisso = 0; break;
            case 'F': g_fisso = 1; break;
            case 'G': g_ere = 0; g_fisso = 0; break;
            case 'i': g_i = 1; break;
            case 'v': g_v = 1; break;
            case 'c': g_c = 1; break;
            case 'n': g_n = 1; break;
            case 'l': g_l = 1; break;
            case 'L': g_ll = 1; break;
            case 'o': g_o = 1; break;
            case 'q': g_q = 1; break;
            case 's': g_s = 1; break;
            case 'H': g_nomi = 1; break;
            case 'h': g_nomi = 0; break;
            case 'r': case 'R': g_r = 1; break;
            case 'w': g_w = 1; break;
            case 'x': g_x = 1; break;
            case 'a': g_a = 1; break;
            case 'e':
                if (o[1]) modelli[nm++] = o + 1;
                else if (++k < argc) modelli[nm++] = argv[k];
                else muori("-e vuole un modello");
                o += strlen(o) - 1;
                break;
            default:
                printf("uso: grep [-EFGivcnlLoqsHhrwxa] [-e MODELLO]... [MODELLO] [FILE...]\n");
                return 2;
            }
            continue;
        }
        file[nf++] = a;
    }
    /* without -e the first free word is the model */
    if (nm == 0) {
        if (nf == 0) {
            printf("uso: grep [-EFGivcnlLoqsHhrwxa] [-e MODELLO]... [MODELLO] [FILE...]\n");
            return 2;
        }
        modelli[nm++] = file[0];
        file++;
        nf--;
    }
    for (k = 0; k < nm; k++) compila(modelli[k]);
    {
        int stati = 1;

        for (k = 0; k < g_nmod; k++) if (g_mod[k].n > stati) stati = g_mod[k].n;
        pronti(stati + 1);
    }
    if (g_nomi < 0) g_nomi = (nf > 1 || g_r) ? 1 : 0;

    if (nf == 0 && g_r) trovato = cerca_nome(".", 1);
    else if (nf == 0) trovato = cerca_fd(0, "(standard input)");
    for (k = 0; k < nf && !(trovato && g_q); k++)
        if (cerca_nome(file[k], 1)) trovato = 1;

    svuota();
    if (trovato && g_q) return 0;
    return g_errori ? 2 : (trovato ? 0 : 1);
}
