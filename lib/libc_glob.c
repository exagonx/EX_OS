/* =============================================================================
 * lib/libc_glob.c -- fnmatch and glob (5 November 2026, Valkey)
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
 * like getopt and iconv). Valkey's config.c expands "include /dir/<star>.conf"
 * with glob(); fnmatch is the matcher glob is made of, and has its own users.
 *
 * fnmatch: '*', '?', '[...]' with ranges, '!' or '^' to negate, a ']' first
 * in the set taken as itself, '\' to quote (not with FNM_NOESCAPE);
 * FNM_PATHNAME ('/' only matches '/'), FNM_PERIOD (a leading '.', or one
 * after '/' with FNM_PATHNAME, only matches a '.' written in the pattern),
 * FNM_CASEFOLD (GNU). No character classes ([:alpha:]): the locale is C and
 * nobody asked yet; such a set matches nothing rather than something wrong.
 *
 * glob: the pattern is expanded one path component at a time (a directory
 * read with readdir for each component that has a wildcard, the name taken
 * as it is for the others), so "a/<star>" "/b/c?d/<star>.conf" (<star> = the asterisk) work at any depth.
 * Flags: GLOB_ERR, GLOB_MARK, GLOB_NOSORT, GLOB_DOOFFS, GLOB_NOCHECK,
 * GLOB_APPEND, GLOB_NOESCAPE; errfunc is called on a directory that cannot
 * be read. Results sorted with strcmp, as in the C locale.
 * ============================================================================= */

#define FNM_NOMATCH     1
#define FNM_NOESCAPE    0x01
#define FNM_PATHNAME    0x02
#define FNM_PERIOD      0x04
#define FNM_CASEFOLD    0x10

typedef struct {
    size_t gl_pathc;
    char **gl_pathv;
    size_t gl_offs;
} glob_t;

#define GLOB_ERR        0x01
#define GLOB_MARK       0x02
#define GLOB_NOSORT     0x04
#define GLOB_DOOFFS     0x08
#define GLOB_NOCHECK    0x10
#define GLOB_APPEND     0x20
#define GLOB_NOESCAPE   0x40
#define GLOB_NOSPACE    1
#define GLOB_ABORTED    2
#define GLOB_NOMATCH    3

static int fnm_minuscola(int c, int flags)
{
    if ((flags & FNM_CASEFOLD) && c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

/* One set "[...]" against c. *p points after '['; on return, after ']'.
 * Returns 1 match, 0 no match, -1 not a set (no closing ']': '[' is itself). */
static int fnm_insieme(const char **pp, int c, int flags)
{
    const char *p = *pp;
    int nega = 0, preso = 0, primo = 1;

    if (*p == '!' || *p == '^') { nega = 1; p++; }
    c = fnm_minuscola(c, flags);
    for (;;) {
        int a, b;

        if (*p == '\0') return -1;
        if (*p == ']' && !primo) { p++; break; }
        primo = 0;
        if (*p == '[' && p[1] == ':') {
            /* a class: skipped, matches nothing (see the top) */
            const char *f = strstr(p + 2, ":]");
            if (!f) return -1;
            p = f + 2;
            continue;
        }
        if (*p == '\\' && !(flags & FNM_NOESCAPE) && p[1]) p++;
        a = (unsigned char)*p++;
        b = a;
        if (*p == '-' && p[1] != ']' && p[1] != '\0') {
            p++;
            if (*p == '\\' && !(flags & FNM_NOESCAPE) && p[1]) p++;
            b = (unsigned char)*p++;
        }
        a = fnm_minuscola(a, flags);
        b = fnm_minuscola(b, flags);
        if ((flags & FNM_PATHNAME) && c == '/') continue;
        if (c >= a && c <= b) preso = 1;
    }
    *pp = p;
    return preso != nega;
}

static int fnm_da(const char *p, const char *s, const char *inizio, int flags)
{
    for (;;) {
        int c = (unsigned char)*s;
        int periodo = (flags & FNM_PERIOD) && c == '.' &&
                      (s == inizio || ((flags & FNM_PATHNAME) && s[-1] == '/'));

        switch (*p) {
        case '\0':
            return c == '\0' ? 0 : FNM_NOMATCH;
        case '?':
            if (c == '\0' || periodo) return FNM_NOMATCH;
            if ((flags & FNM_PATHNAME) && c == '/') return FNM_NOMATCH;
            p++; s++;
            continue;
        case '*':
            while (*p == '*') p++;
            if (periodo) return FNM_NOMATCH;
            if (*p == '\0') {
                if (flags & FNM_PATHNAME) return strchr(s, '/') ? FNM_NOMATCH : 0;
                return 0;
            }
            for (;;) {
                if (fnm_da(p, s, inizio, flags & ~FNM_PERIOD) == 0) return 0;
                if (*s == '\0') return FNM_NOMATCH;
                if ((flags & FNM_PATHNAME) && *s == '/') return FNM_NOMATCH;
                s++;
            }
        case '[': {
            const char *q = p + 1;
            int r;

            if (c == '\0' || periodo) return FNM_NOMATCH;
            r = fnm_insieme(&q, c, flags);
            if (r < 0) {                        /* a lone '[' */
                if (c != '[') return FNM_NOMATCH;
                p++; s++;
                continue;
            }
            if (!r) return FNM_NOMATCH;
            p = q; s++;
            continue;
        }
        case '\\':
            if (!(flags & FNM_NOESCAPE) && p[1]) p++;
            /* fall through */
        default:
            if (fnm_minuscola((unsigned char)*p, flags) != fnm_minuscola(c, flags))
                return FNM_NOMATCH;
            p++; s++;
            continue;
        }
    }
}

int fnmatch(const char *modello, const char *nome, int flags)
{
    if (!modello || !nome) return FNM_NOMATCH;
    return fnm_da(modello, nome, nome, flags);
}

/* --- glob ------------------------------------------------------------------- */

static int glob_ha_jolly(const char *s, size_t n, int noescape)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (s[i] == '\\' && !noescape) { i++; continue; }
        if (s[i] == '*' || s[i] == '?' || s[i] == '[') return 1;
    }
    return 0;
}

typedef struct {
    char  **v;
    size_t  n, posti;
} GlobLista;

static int glob_metti(GlobLista *l, const char *percorso, int mark)
{
    struct stat st;
    size_t n = strlen(percorso);
    char *c;

    if (l->n == l->posti) {
        size_t np = l->posti ? l->posti * 2 : 16;
        char **nv = (char **)realloc(l->v, np * sizeof(char *));
        if (!nv) return GLOB_NOSPACE;
        l->v = nv; l->posti = np;
    }
    mark = mark && n > 0 && percorso[n - 1] != '/' &&
           stat(percorso, &st) == 0 && S_ISDIR(st.st_mode);
    c = (char *)malloc(n + 2);
    if (!c) return GLOB_NOSPACE;
    memcpy(c, percorso, n);
    if (mark) c[n++] = '/';
    c[n] = '\0';
    l->v[l->n++] = c;
    return 0;
}

/* The name without the quoting backslashes, for a component with no wildcard */
static void glob_senza_escape(char *d, const char *s, size_t n, int noescape)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (s[i] == '\\' && !noescape && i + 1 < n) i++;
        *d++ = s[i];
    }
    *d = '\0';
}

/* base: what is expanded so far ("" or ending in '/'); resto: the pattern left */
static int glob_scendi(const char *base, const char *resto, int flags,
                       int (*errfunc)(const char *, int), GlobLista *l)
{
    const char *fine;
    size_t lb = strlen(base), lc;
    char *percorso;
    int noescape = (flags & GLOB_NOESCAPE) != 0, r = 0;

    while (*resto == '/') resto++;
    if (*resto == '\0') {
        struct stat st;

        if (lb == 0 || stat(base, &st) != 0) return 0;
        return glob_metti(l, base, flags & GLOB_MARK);
    }
    fine = strchr(resto, '/');
    lc = fine ? (size_t)(fine - resto) : strlen(resto);

    if (!glob_ha_jolly(resto, lc, noescape)) {
        percorso = (char *)malloc(lb + lc + 2);
        if (!percorso) return GLOB_NOSPACE;
        memcpy(percorso, base, lb);
        glob_senza_escape(percorso + lb, resto, lc, noescape);
        if (fine) strcat(percorso, "/");
        r = glob_scendi(percorso, fine ? fine : "", flags, errfunc, l);
        free(percorso);
        return r;
    }

    {
        char  *comp = (char *)malloc(lc + 1);
        DIR   *d;
        struct dirent *e;
        int    fl = FNM_PERIOD | (noescape ? FNM_NOESCAPE : 0);

        if (!comp) return GLOB_NOSPACE;
        memcpy(comp, resto, lc);
        comp[lc] = '\0';
        d = opendir(lb ? base : ".");
        if (!d) {
            int e2 = errno;

            free(comp);
            if ((errfunc && errfunc(lb ? base : ".", e2)) || (flags & GLOB_ERR))
                return GLOB_ABORTED;
            return 0;
        }
        while (r == 0 && (e = readdir(d)) != NULL) {
            size_t ln;

            if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
                if (comp[0] != '.') continue;
            }
            if (fnmatch(comp, e->d_name, fl) != 0) continue;
            ln = strlen(e->d_name);
            percorso = (char *)malloc(lb + ln + 2);
            if (!percorso) { r = GLOB_NOSPACE; break; }
            memcpy(percorso, base, lb);
            memcpy(percorso + lb, e->d_name, ln + 1);
            if (fine) strcat(percorso, "/");
            r = glob_scendi(percorso, fine ? fine : "", flags, errfunc, l);
            free(percorso);
        }
        closedir(d);
        free(comp);
    }
    return r;
}

static int glob_confronta(const void *a, const void *b)
{
    return strcmp(*(char * const *)a, *(char * const *)b);
}

void globfree(glob_t *g)
{
    size_t i;

    if (!g || !g->gl_pathv) return;
    for (i = 0; i < g->gl_pathc; i++) free(g->gl_pathv[g->gl_offs + i]);
    free(g->gl_pathv);
    g->gl_pathv = NULL;
    g->gl_pathc = 0;
}

int glob(const char *modello, int flags, int (*errfunc)(const char *, int), glob_t *g)
{
    GlobLista l = { NULL, 0, 0 };
    size_t offs, prima, i;
    char **v;
    int r;

    if (!modello || !g) return GLOB_NOSPACE;
    if (!(flags & GLOB_APPEND)) {
        g->gl_pathc = 0;
        g->gl_pathv = NULL;
        if (!(flags & GLOB_DOOFFS)) g->gl_offs = 0;
    }
    offs = (flags & GLOB_DOOFFS) ? g->gl_offs : 0;

    r = glob_scendi(modello[0] == '/' ? "/" : "", modello, flags, errfunc, &l);
    if (r == 0 && l.n == 0) {
        if (flags & GLOB_NOCHECK) r = glob_metti(&l, modello, 0);
        else r = GLOB_NOMATCH;
    }
    if (r != 0 && r != GLOB_NOMATCH && l.n == 0) {
        free(l.v);
        return r;
    }
    if (r == GLOB_NOMATCH) { free(l.v); return GLOB_NOMATCH; }
    if (!(flags & GLOB_NOSORT) && l.n > 1) qsort(l.v, l.n, sizeof(char *), glob_confronta);

    prima = (flags & GLOB_APPEND) ? g->gl_pathc : 0;
    v = (char **)realloc((flags & GLOB_APPEND) ? g->gl_pathv : NULL,
                         (offs + prima + l.n + 1) * sizeof(char *));
    if (!v) {
        for (i = 0; i < l.n; i++) free(l.v[i]);
        free(l.v);
        return GLOB_NOSPACE;
    }
    if (!(flags & GLOB_APPEND)) for (i = 0; i < offs; i++) v[i] = NULL;
    for (i = 0; i < l.n; i++) v[offs + prima + i] = l.v[i];
    v[offs + prima + l.n] = NULL;
    free(l.v);
    g->gl_pathv = v;
    g->gl_pathc = prima + l.n;
    return r;
}
