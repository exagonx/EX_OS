/* =============================================================================
 * lib/exjs/libreria.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The rest of the standard library (@EXJS-LACUNE, 29 September 2026)
 *
 * base.c has the first layer - Math, JSON, Date, the string and array methods
 * of ES3. This file has what a probe of common code found missing: Object and
 * Array as globals, call/apply/bind, hasOwnProperty, the ES5/ES2015 array and
 * string methods (reduce, some, find, includes, startsWith, padStart...),
 * toFixed and toString(base), and encodeURIComponent with its siblings.
 *
 * ! A SEPARATE FILE AND NOT MORE OF base.c: base.c is 2100 lines already, and
 * these functions only need the public interface plus a few internal calls.
 *
 * ! EVERY LOOP THAT CALLS BACK INTO JAVASCRIPT CHECKS exjs_interrotto: a
 * callback that throws must stop `reduce` at once, as it stops a for loop.
 * ============================================================================= */

#include "exjs_int.h"

static ExJsVal arg(const ExJsVal *a, int n, int i)
{
    return (i < n) ? a[i] : exjs_indefinito();
}

static int intero(ExJsCtx *c, const ExJsVal *a, int n, int i, int se_manca)
{
    double d;

    if (i >= n || exjs_tipo(c, a[i]) == EXJS_INDEFINITO) return se_manca;
    d = exjs_a_numero(c, a[i]);
    if (d != d) return 0;
    if (d > 2147483647.0) return 2147483647;
    if (d < -2147483647.0) return -2147483647;
    return (int)d;
}

static unsigned int lung(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static int strcmp_l(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static int strchr_l(const char *s, char x)
{
    for (; *s; s++) if (*s == x) return 1;
    return 0;
}

/* A string that stays put: a string value is already in the arena, anything
 * else is converted and stored there (numbers are converted into a scratch
 * buffer that the next conversion overwrites). */
static const char *stabile(ExJsCtx *c, ExJsVal v)
{
    if (exjs_tipo(c, v) == EXJS_STRINGA) return exjs_a_stringa(c, v);
    return exjs_a_stringa(c, exjs_stringa(c, exjs_a_stringa(c, v), -1));
}

static int e_funzione(ExJsCtx *c, ExJsVal f)
{
    return exjs_tipo(c, f) == EXJS_FUNZIONE;
}

static int e_vettore(ExJsCtx *c, ExJsVal v)
{
    ExJsOggetto *O = exjs_ogg(c, exjs_a_oggetto(v));
    return O && O->classe == EXJS_CL_VETTORE;
}

/* The length of anything that looks like an array: a vector, or an object
 * with a numeric `length` (NodeList, arguments of another engine). */
static unsigned int lunghezza_di(ExJsCtx *c, ExJsVal v)
{
    double d;

    if (e_vettore(c, v)) return exjs_lunghezza(c, v);
    if (exjs_a_oggetto(v) < 0) return 0;
    d = exjs_a_numero(c, exjs_prendi(c, v, "length"));
    return (d > 0 && d < 1e7) ? (unsigned int)d : 0;
}

static ExJsVal elemento(ExJsCtx *c, ExJsVal v, unsigned int i)
{
    char b[16];
    int  n = 0, k;
    char r[16];

    if (e_vettore(c, v)) return exjs_indice_prendi(c, v, i);
    do { r[n++] = (char)('0' + i % 10); i /= 10; } while (i);
    for (k = 0; k < n; k++) b[k] = r[n - 1 - k];
    b[n] = '\0';
    return exjs_prendi(c, v, b);
}

/* =============================================================================
 * Object
 * ========================================================================== */
/* modo 0 keys, 1 values, 2 entries */
static ExJsVal chiavi(ExJsCtx *c, ExJsVal o, int modo)
{
    ExJsVal      out = exjs_vettore(c);
    int          k = exjs_a_oggetto(o), p;
    unsigned int n = 0, i, l;

    if (k < 0) {
        /* A string: its indices. Anything else has no own keys. */
        if (exjs_tipo(c, o) == EXJS_STRINGA) {
            const char *s = stabile(c, o);
            for (i = 0, l = lung(s); i < l; i++) {
                ExJsVal kv = exjs_numero(c, (double)i);
                ExJsVal ks = exjs_stringa(c, exjs_a_stringa(c, kv), -1);
                ExJsVal vv = exjs_stringa(c, s + i, 1);
                if (modo == 0) exjs_indice_metti(c, out, n++, ks);
                else if (modo == 1) exjs_indice_metti(c, out, n++, vv);
                else {
                    ExJsVal cp = exjs_vettore(c);
                    exjs_indice_metti(c, cp, 0, ks);
                    exjs_indice_metti(c, cp, 1, vv);
                    exjs_indice_metti(c, out, n++, cp);
                }
            }
        }
        return out;
    }
    if (e_vettore(c, o)) {
        for (i = 0, l = exjs_lunghezza(c, o); i < l; i++) {
            ExJsVal ks = exjs_stringa(c, exjs_a_stringa(c, exjs_numero(c, (double)i)), -1);
            ExJsVal vv = exjs_indice_prendi(c, o, i);
            if (modo == 0) exjs_indice_metti(c, out, n++, ks);
            else if (modo == 1) exjs_indice_metti(c, out, n++, vv);
            else {
                ExJsVal cp = exjs_vettore(c);
                exjs_indice_metti(c, cp, 0, ks);
                exjs_indice_metti(c, cp, 1, vv);
                exjs_indice_metti(c, out, n++, cp);
            }
        }
    }
    for (p = exjs_prop_prima(c, k); p >= 0; p = exjs_prop_prossima(c, p)) {
        unsigned int nome = exjs_prop_nome(c, p);
        ExJsVal      ks, vv;

        if (exjs_arena_leggi(c, nome)[0] == '\001') continue;   /* the engine's */
        ks = exjs_stringa_off(c, nome);
        vv = exjs_prop_val(c, p);
        if (exjs_e_accessore(c, vv)) vv = exjs_prendi(c, o, exjs_arena_leggi(c, nome));
        if (modo == 0) exjs_indice_metti(c, out, n++, ks);
        else if (modo == 1) exjs_indice_metti(c, out, n++, vv);
        else {
            ExJsVal cp = exjs_vettore(c);
            exjs_indice_metti(c, cp, 0, ks);
            exjs_indice_metti(c, cp, 1, vv);
            exjs_indice_metti(c, out, n++, cp);
        }
    }
    return out;
}

static ExJsVal nat_chiavi(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q;
    return chiavi(c, arg(a, n, 0), (int)(long)d);
}

static ExJsVal nat_assign(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal dest = arg(a, n, 0);
    int     i;

    (void)q; (void)d;
    if (exjs_a_oggetto(dest) < 0) return dest;
    for (i = 1; i < n; i++) {
        int k = exjs_a_oggetto(a[i]), p;

        if (k < 0) continue;
        for (p = exjs_prop_prima(c, k); p >= 0; p = exjs_prop_prossima(c, p)) {
            unsigned int nome = exjs_prop_nome(c, p);
            if (exjs_arena_leggi(c, nome)[0] == '\001') continue;
            exjs_metti(c, dest, exjs_arena_leggi(c, nome), exjs_prop_val(c, p));
        }
    }
    return dest;
}

static ExJsVal nat_create(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal o = exjs_oggetto(c);

    (void)q; (void)d;
    if (exjs_a_oggetto(arg(a, n, 0)) >= 0) exjs_proto_metti(c, o, a[0]);
    if (n > 1 && exjs_a_oggetto(a[1]) >= 0) {
        /* Object.create(p, { x: { value: 1 } }): only `value`. */
        int k = exjs_a_oggetto(a[1]), p;
        for (p = exjs_prop_prima(c, k); p >= 0; p = exjs_prop_prossima(c, p))
            exjs_metti(c, o, exjs_arena_leggi(c, exjs_prop_nome(c, p)),
                       exjs_prendi(c, exjs_prop_val(c, p), "value"));
    }
    return o;
}

static ExJsVal nat_getPrototypeOf(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsOggetto *O = exjs_ogg(c, exjs_a_oggetto(arg(a, n, 0)));

    (void)q; (void)d;
    if (!O) return exjs_nullo();
    if (O->proto >= 0) return exjs_da_oggetto(O->proto);
    if (O->classe == EXJS_CL_VETTORE) return exjs_da_oggetto(exjs_proto_vet(c));
    return exjs_nullo();
}

/* defineProperty: `value`, or `get` and `set` (an accessor). writable,
 * enumerable and configurable are not kept: every property can be written
 * and is listed. */
static ExJsVal nat_defineProperty(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal o = arg(a, n, 0);

    (void)q; (void)d;
    if (exjs_a_oggetto(o) >= 0 && n > 2) {
        const char *nome = stabile(c, a[1]);
        ExJsVal     g = exjs_prendi(c, a[2], "get"), s2 = exjs_prendi(c, a[2], "set");
        if (e_funzione(c, g) || e_funzione(c, s2)) exjs_accessore_metti(c, o, nome, g, s2);
        else exjs_metti(c, o, nome, exjs_prendi(c, a[2], "value"));
    }
    return o;
}

static ExJsVal nat_defineProperties(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal o = arg(a, n, 0), dd = arg(a, n, 1);
    int     k = exjs_a_oggetto(dd), p;

    (void)q; (void)d;
    for (p = exjs_prop_prima(c, k); k >= 0 && p >= 0; p = exjs_prop_prossima(c, p)) {
        ExJsVal x[3];
        x[0] = o;
        x[1] = exjs_stringa_off(c, exjs_prop_nome(c, p));
        x[2] = exjs_prop_val(c, p);
        nat_defineProperty(c, q, x, 3, 0);
    }
    return o;
}

static ExJsVal nat_identita(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)c; (void)q; (void)d;
    return arg(a, n, 0);
}

static ExJsVal nat_Object(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q; (void)d;
    if (n > 0 && exjs_a_oggetto(a[0]) >= 0) return a[0];
    return exjs_oggetto(c);
}

static ExJsVal nat_is(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal x = arg(a, n, 0), y = arg(a, n, 1);

    (void)q; (void)d;
    if (exjs_tipo(c, x) == EXJS_NUMERO && exjs_tipo(c, y) == EXJS_NUMERO) {
        double u = exjs_a_numero(c, x), v = exjs_a_numero(c, y);
        if (u != u && v != v) return exjs_booleano(1);
        return exjs_booleano(u == v);
    }
    return exjs_booleano(exjs_identici_pub(c, x, y));
}

/* On every object: hasOwnProperty, toString, valueOf. */
static ExJsVal nat_hasOwn(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    const char *nome = stabile(c, arg(a, n, 0));
    int         k = exjs_a_oggetto(q);

    (void)d;
    if (k < 0) return exjs_booleano(0);
    if (e_vettore(c, q)) {
        const char *s = nome;
        unsigned int i = 0;
        if (*s >= '0' && *s <= '9') {
            while (*s >= '0' && *s <= '9') i = i * 10 + (unsigned int)(*s++ - '0');
            if (!*s) return exjs_booleano(i < exjs_lunghezza(c, q));
        }
        if (nome[0] == 'l' && !strcmp_l(nome, "length")) return exjs_booleano(1);
    }
    return exjs_booleano(exjs_prop_trova(c, k, nome, 0) >= 0);
}

static ExJsVal nat_ogg_testo(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q; (void)a; (void)n; (void)d;
    return exjs_stringa(c, "[object Object]", -1);
}

static ExJsVal nat_questo(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)c; (void)a; (void)n; (void)d;
    return q;
}

/* =============================================================================
 * Array
 * ========================================================================== */
static ExJsVal nat_isArray(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q; (void)d;
    return exjs_booleano(e_vettore(c, arg(a, n, 0)));
}

static ExJsVal nat_Array(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal v = exjs_vettore(c);
    int     i;

    (void)q; (void)d;
    /* new Array(3) is three holes; new Array(1, 2) is [1, 2]. */
    if (n == 1 && exjs_tipo(c, a[0]) == EXJS_NUMERO) {
        double l = exjs_a_numero(c, a[0]);
        if (l > 0 && l < 1e6) exjs_indice_metti(c, v, (unsigned int)l - 1, exjs_indefinito());
        return v;
    }
    for (i = 0; i < n; i++) exjs_indice_metti(c, v, (unsigned int)i, a[i]);
    return v;
}

static ExJsVal nat_from(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal      src = arg(a, n, 0), f = arg(a, n, 1), v = exjs_vettore(c);
    unsigned int i, l;

    (void)q; (void)d;
    if (exjs_tipo(c, src) == EXJS_STRINGA) {
        const char *s = stabile(c, src);
        for (i = 0, l = lung(s); i < l; i++)
            exjs_indice_metti(c, v, i, exjs_stringa(c, s + i, 1));
    } else {
        for (i = 0, l = lunghezza_di(c, src); i < l; i++)
            exjs_indice_metti(c, v, i, elemento(c, src, i));
    }
    if (e_funzione(c, f)) {
        for (i = 0, l = exjs_lunghezza(c, v); i < l && !exjs_interrotto(c); i++) {
            ExJsVal x[2];
            x[0] = exjs_indice_prendi(c, v, i);
            x[1] = exjs_numero(c, (double)i);
            exjs_indice_metti(c, v, i, exjs_chiama(c, f, exjs_indefinito(), x, 2, 0));
        }
    }
    return v;
}

static ExJsVal nat_of(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal v = exjs_vettore(c);
    int     i;

    (void)q; (void)d;
    for (i = 0; i < n; i++) exjs_indice_metti(c, v, (unsigned int)i, a[i]);
    return v;
}

static ExJsVal nat_includes_vet(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q), i;
    ExJsVal      x = arg(a, n, 0);
    int          nan = exjs_tipo(c, x) == EXJS_NUMERO && exjs_a_numero(c, x) != exjs_a_numero(c, x);

    (void)d;
    for (i = 0; i < l; i++) {
        ExJsVal y = exjs_indice_prendi(c, q, i);
        if (exjs_identici_pub(c, x, y)) return exjs_booleano(1);
        /* includes, unlike indexOf, finds NaN */
        if (nan && exjs_tipo(c, y) == EXJS_NUMERO && exjs_a_numero(c, y) != exjs_a_numero(c, y))
            return exjs_booleano(1);
    }
    return exjs_booleano(0);
}

/* modo 0 some, 1 every, 2 find, 3 findIndex */
static ExJsVal cerca(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, int modo)
{
    unsigned int l = exjs_lunghezza(c, q), i;
    ExJsVal      f = arg(a, n, 0), questo = arg(a, n, 1);

    if (!e_funzione(c, f)) {
        exjs_lancia_errore(c, "TypeError", "non e' una funzione");
        return exjs_indefinito();
    }
    for (i = 0; i < l && !exjs_interrotto(c); i++) {
        ExJsVal x[3];
        int     si;

        x[0] = exjs_indice_prendi(c, q, i);
        x[1] = exjs_numero(c, (double)i);
        x[2] = q;
        si = exjs_a_booleano(c, exjs_chiama(c, f, questo, x, 3, 0));
        if (exjs_interrotto(c)) break;
        if (modo == 0 && si)  return exjs_booleano(1);
        if (modo == 1 && !si) return exjs_booleano(0);
        if (modo == 2 && si)  return x[0];
        if (modo == 3 && si)  return exjs_numero(c, (double)i);
    }
    if (modo == 0) return exjs_booleano(0);
    if (modo == 1) return exjs_booleano(1);
    if (modo == 3) return exjs_numero(c, -1.0);
    return exjs_indefinito();
}

static ExJsVal nat_cerca(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    return cerca(c, q, a, n, (int)(long)d);
}

/* reduce (d = 0) and reduceRight (d = 1) */
static ExJsVal nat_reduce(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q), k;
    int          destra = (int)(long)d;
    ExJsVal      f = arg(a, n, 0), acc;

    if (!e_funzione(c, f)) {
        exjs_lancia_errore(c, "TypeError", "non e' una funzione");
        return exjs_indefinito();
    }
    k = 0;
    if (n > 1) acc = a[1];
    else {
        if (l == 0) {
            exjs_lancia_errore(c, "TypeError", "reduce di un vettore vuoto senza valore iniziale");
            return exjs_indefinito();
        }
        acc = exjs_indice_prendi(c, q, destra ? l - 1 : 0);
        k = 1;
    }
    for (; k < l && !exjs_interrotto(c); k++) {
        unsigned int i = destra ? l - 1 - k : k;
        ExJsVal      x[4];

        x[0] = acc;
        x[1] = exjs_indice_prendi(c, q, i);
        x[2] = exjs_numero(c, (double)i);
        x[3] = q;
        acc = exjs_chiama(c, f, exjs_indefinito(), x, 4, 0);
    }
    return acc;
}

static ExJsVal nat_splice(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q), i;
    int          da, quanti, nuovi = n > 2 ? n - 2 : 0;
    ExJsVal      tolti = exjs_vettore(c), resto = exjs_vettore(c);
    unsigned int nr = 0;

    (void)d;
    da = intero(c, a, n, 0, 0);
    if (da < 0) da += (int)l;
    if (da < 0) da = 0;
    if (da > (int)l) da = (int)l;
    quanti = (n > 1) ? intero(c, a, n, 1, 0) : (int)l - da;
    if (quanti < 0) quanti = 0;
    if (quanti > (int)l - da) quanti = (int)l - da;

    for (i = 0; i < (unsigned int)quanti; i++)
        exjs_indice_metti(c, tolti, i, exjs_indice_prendi(c, q, (unsigned int)da + i));
    for (i = (unsigned int)(da + quanti); i < l; i++)
        exjs_indice_metti(c, resto, nr++, exjs_indice_prendi(c, q, i));
    exjs_vettore_tronca(c, q, (unsigned int)da);
    for (i = 0; i < (unsigned int)nuovi; i++)
        exjs_indice_metti(c, q, (unsigned int)da + i, a[2 + i]);
    for (i = 0; i < nr; i++)
        exjs_indice_metti(c, q, (unsigned int)(da + nuovi) + i, exjs_indice_prendi(c, resto, i));
    return tolti;
}

static ExJsVal nat_fill(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q);
    int          da = intero(c, a, n, 1, 0), fino = intero(c, a, n, 2, (int)l), i;

    (void)d;
    if (da < 0) da += (int)l;
    if (fino < 0) fino += (int)l;
    if (da < 0) da = 0;
    if (fino > (int)l) fino = (int)l;
    for (i = da; i < fino; i++) exjs_indice_metti(c, q, (unsigned int)i, arg(a, n, 0));
    return q;
}

static ExJsVal nat_at(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    int i = intero(c, a, n, 0, 0);

    (void)d;
    if (exjs_tipo(c, q) == EXJS_STRINGA) {
        const char *s = stabile(c, q);
        int l = (int)lung(s);
        if (i < 0) i += l;
        return (i >= 0 && i < l) ? exjs_stringa(c, s + i, 1) : exjs_indefinito();
    }
    {
        int l = (int)exjs_lunghezza(c, q);
        if (i < 0) i += l;
        return (i >= 0 && i < l) ? exjs_indice_prendi(c, q, (unsigned int)i) : exjs_indefinito();
    }
}

static void appiattisci(ExJsCtx *c, ExJsVal src, ExJsVal out, unsigned int *k, int prof)
{
    unsigned int l = exjs_lunghezza(c, src), i;

    for (i = 0; i < l; i++) {
        ExJsVal x = exjs_indice_prendi(c, src, i);
        if (prof > 0 && e_vettore(c, x)) appiattisci(c, x, out, k, prof - 1);
        else exjs_indice_metti(c, out, (*k)++, x);
    }
}

static ExJsVal nat_flat(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal      out = exjs_vettore(c);
    unsigned int k = 0;
    int          prof = intero(c, a, n, 0, 1);

    (void)d;
    if (prof > 32) prof = 32;
    appiattisci(c, q, out, &k, prof);
    return out;
}

static ExJsVal nat_vet_testo(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)a; (void)n; (void)d;
    return exjs_stringa(c, exjs_a_stringa(c, q), -1);
}

/* =============================================================================
 * String
 * ========================================================================== */
static int trova(const char *s, const char *t, int da)
{
    int ls = (int)lung(s), lt = (int)lung(t), i, j;

    if (da < 0) da = 0;
    for (i = da; i + lt <= ls; i++) {
        for (j = 0; j < lt && s[i + j] == t[j]; j++) { }
        if (j == lt) return i;
    }
    return -1;
}

/* d: 0 includes, 1 startsWith, 2 endsWith */
static ExJsVal nat_str_prova(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    const char *s = stabile(c, q), *t = stabile(c, arg(a, n, 0));
    int         ls = (int)lung(s), lt = (int)lung(t), modo = (int)(long)d, p;

    if (modo == 0) return exjs_booleano(trova(s, t, intero(c, a, n, 1, 0)) >= 0);
    if (modo == 1) {
        p = intero(c, a, n, 1, 0);
        if (p < 0) p = 0;
        if (p + lt > ls) return exjs_booleano(0);
        return exjs_booleano(trova(s + p, t, 0) == 0);
    }
    p = intero(c, a, n, 1, ls);                     /* endsWith: la fine */
    if (p > ls) p = ls;
    if (p - lt < 0) return exjs_booleano(0);
    {
        int j;
        for (j = 0; j < lt; j++) if (s[p - lt + j] != t[j]) return exjs_booleano(0);
    }
    return exjs_booleano(1);
}

/* padStart (d = 0) and padEnd (d = 1) */
static ExJsVal nat_pad(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    const char  *s = stabile(c, q);
    const char  *r = (n > 1 && exjs_tipo(c, a[1]) != EXJS_INDEFINITO) ? stabile(c, a[1]) : " ";
    int          ls = (int)lung(s), lr = (int)lung(r), vuole = intero(c, a, n, 0, 0), i;
    unsigned int f;

    if (vuole <= ls || lr == 0) return exjs_stringa(c, s, -1);
    if (vuole > 1 << 20) vuole = 1 << 20;
    f = exjs_arena_apri(c);
    if (f == EXJS_FILO_NO) return exjs_stringa(c, "", -1);
    if ((int)(long)d == 1) exjs_arena_aggiungi(c, s, (unsigned int)ls);
    for (i = 0; i < vuole - ls; i++) exjs_arena_aggiungi(c, r + i % lr, 1);
    if ((int)(long)d == 0) exjs_arena_aggiungi(c, s, (unsigned int)ls);
    return exjs_arena_chiudi(c, f);
}

static ExJsVal nat_repeat(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    const char  *s = stabile(c, q);
    int          volte = intero(c, a, n, 0, 0), i;
    unsigned int l = lung(s), f;

    (void)d;
    if (volte < 0) { exjs_lancia_errore(c, "RangeError", "repeat con un numero negativo"); return exjs_indefinito(); }
    if ((unsigned long)volte * l > (1u << 20)) { exjs_lancia_errore(c, "RangeError", "stringa troppo lunga"); return exjs_indefinito(); }
    f = exjs_arena_apri(c);
    if (f == EXJS_FILO_NO) return exjs_stringa(c, "", -1);
    for (i = 0; i < volte; i++) exjs_arena_aggiungi(c, s, l);
    return exjs_arena_chiudi(c, f);
}

/* trimStart (d = 0) and trimEnd (d = 1) */
static ExJsVal nat_trim_lato(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    const char  *s = stabile(c, q);
    unsigned int i = 0, l = lung(s);

    (void)a; (void)n;
    if ((int)(long)d == 0) {
        while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
        return exjs_stringa(c, s + i, -1);
    }
    while (l > 0 && (s[l-1] == ' ' || s[l-1] == '\t' || s[l-1] == '\n' || s[l-1] == '\r')) l--;
    return exjs_stringa(c, s, (int)l);
}

static ExJsVal nat_str_concat(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int f;
    int          i;
    const char  *s0 = stabile(c, q);
    const char  *pezzi[16];

    (void)d;
    if (n > 16) n = 16;
    for (i = 0; i < n; i++) pezzi[i] = stabile(c, a[i]);   /* before the thread */
    f = exjs_arena_apri(c);
    if (f == EXJS_FILO_NO) return exjs_stringa(c, "", -1);
    exjs_arena_aggiungi(c, s0, lung(s0));
    for (i = 0; i < n; i++) exjs_arena_aggiungi(c, pezzi[i], lung(pezzi[i]));
    return exjs_arena_chiudi(c, f);
}

static ExJsVal nat_str_testo(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)a; (void)n; (void)d;
    return exjs_stringa(c, stabile(c, q), -1);
}

/* =============================================================================
 * Number
 * ========================================================================== */
static ExJsVal nat_toFixed(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char b[64];

    (void)d;
    exjs_numero_fisso(exjs_a_numero(c, q), intero(c, a, n, 0, 0), b, sizeof(b));
    return exjs_stringa(c, b, -1);
}

/* n / d with d < 65536, in 16-bit pieces: only 32-bit divisions. A 64-bit
 * division by a variable calls __udivmoddi4, which EX-OS programs do not
 * have (the same rule as in base.c, next to Date). */
static unsigned long long div_piccolo(unsigned long long n, unsigned int d, unsigned int *resto)
{
    unsigned long long q = 0;
    unsigned int       r = 0;
    int                k;

    for (k = 3; k >= 0; k--) {
        unsigned int cur = (r << 16) | (unsigned int)((n >> (k * 16)) & 0xFFFFu);
        q |= (unsigned long long)(cur / d) << (k * 16);
        r  = cur % d;
    }
    *resto = r;
    return q;
}

static ExJsVal nat_num_testo(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    int    base = intero(c, a, n, 0, 10);
    double x = exjs_a_numero(c, q);
    char   r[80], b[80];
    int    k = 0, i = 0, neg = 0;
    double ip;

    (void)d;
    if (base == 10 || x != x || x > 9e15 || x < -9e15)
        return exjs_stringa(c, exjs_a_stringa(c, q), -1);
    if (base < 2 || base > 36) { exjs_lancia_errore(c, "RangeError", "base fuori da 2..36"); return exjs_indefinito(); }
    if (x < 0) { neg = 1; x = -x; }
    ip = (double)(long long)x;
    {
        unsigned long long u = (unsigned long long)ip;
        do {
            unsigned int c2;
            u = div_piccolo(u, (unsigned int)base, &c2);
            r[k++] = (char)(c2 < 10 ? '0' + c2 : 'a' + c2 - 10);
        } while (u && k < 70);
    }
    if (neg) b[i++] = '-';
    while (k) b[i++] = r[--k];
    x -= ip;
    if (x > 0) {                                    /* a few digits after the point */
        int j;
        b[i++] = '.';
        for (j = 0; j < 20 && x > 0; j++) {
            int c2;
            x *= base;
            c2 = (int)x;
            b[i++] = (char)(c2 < 10 ? '0' + c2 : 'a' + c2 - 10);
            x -= c2;
        }
    }
    b[i] = '\0';
    return exjs_stringa(c, b, -1);
}

static ExJsVal nat_isInteger(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal x = arg(a, n, 0);
    double  v;

    (void)q;
    if (exjs_tipo(c, x) != EXJS_NUMERO) return exjs_booleano(0);
    v = exjs_a_numero(c, x);
    if (v != v || v > 1.7e308 || v < -1.7e308) return exjs_booleano(0);
    if ((int)(long)d == 1) return exjs_booleano(1);            /* isFinite */
    if (v > 9007199254740992.0 || v < -9007199254740992.0) return exjs_booleano(1);
    return exjs_booleano(v == (double)(long long)v);
}

static ExJsVal nat_numIsNaN(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal x = arg(a, n, 0);
    double  v;

    (void)q; (void)d;
    if (exjs_tipo(c, x) != EXJS_NUMERO) return exjs_booleano(0);
    v = exjs_a_numero(c, x);
    return exjs_booleano(v != v);
}

/* =============================================================================
 * Function: call, apply, bind
 * ========================================================================== */
#define ARG_MAX 32

static ExJsVal nat_call(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)d;
    if (!e_funzione(c, q)) { exjs_lancia_errore(c, "TypeError", "call su qualcosa che non e' una funzione"); return exjs_indefinito(); }
    return exjs_chiama(c, q, arg(a, n, 0), n > 1 ? a + 1 : 0, n > 1 ? n - 1 : 0, 0);
}

static ExJsVal nat_apply(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal      v[ARG_MAX], lista = arg(a, n, 1);
    unsigned int l = lunghezza_di(c, lista), i;

    (void)d;
    if (!e_funzione(c, q)) { exjs_lancia_errore(c, "TypeError", "apply su qualcosa che non e' una funzione"); return exjs_indefinito(); }
    if (l > ARG_MAX) l = ARG_MAX;
    for (i = 0; i < l; i++) v[i] = elemento(c, lista, i);
    return exjs_chiama(c, q, arg(a, n, 0), v, (int)l, 0);
}

/* A bound function is a native whose `dato` is the index of an object that
 * keeps the target, `this` and the arguments given to bind. The names start
 * with \001 so for..in and Object.keys do not see them. */
static ExJsVal nat_legata(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal      L = exjs_da_oggetto((int)(long)d);
    ExJsVal      f = exjs_prendi(c, L, "\001f"), pre = exjs_prendi(c, L, "\001a");
    ExJsVal      v[ARG_MAX];
    unsigned int k = 0, i, lp = exjs_lunghezza(c, pre);

    (void)q;
    for (i = 0; i < lp && k < ARG_MAX; i++) v[k++] = exjs_indice_prendi(c, pre, i);
    for (i = 0; i < (unsigned int)n && k < ARG_MAX; i++) v[k++] = a[i];
    return exjs_chiama(c, f, exjs_prendi(c, L, "\001t"), v, (int)k, 0);
}

static ExJsVal nat_bind(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal L = exjs_oggetto(c), pre = exjs_vettore(c);
    int     i;

    (void)d;
    if (!e_funzione(c, q)) { exjs_lancia_errore(c, "TypeError", "bind su qualcosa che non e' una funzione"); return exjs_indefinito(); }
    if (exjs_a_oggetto(L) < 0) return exjs_indefinito();
    for (i = 1; i < n; i++) exjs_indice_metti(c, pre, (unsigned int)(i - 1), a[i]);
    exjs_metti(c, L, "\001f", q);
    exjs_metti(c, L, "\001t", arg(a, n, 0));
    exjs_metti(c, L, "\001a", pre);
    return exjs_nativa(c, nat_legata, (void *)(long)exjs_a_oggetto(L), "bound");
}

static ExJsVal nat_fun_testo(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q; (void)a; (void)n; (void)d;
    return exjs_stringa(c, "function () { [codice] }", -1);
}

/* =============================================================================
 * encodeURIComponent and its siblings: bytes, which are already UTF-8
 * ========================================================================== */
static const char *URI_RISERVATI = ";,/?:@&=+$#";

static ExJsVal nat_codifica(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    const unsigned char *s = (const unsigned char *)stabile(c, arg(a, n, 0));
    int                  intera = (int)(long)d;       /* encodeURI: keeps the reserved */
    unsigned int         f;
    static const char    H[] = "0123456789ABCDEF";

    (void)q;
    f = exjs_arena_apri(c);
    if (f == EXJS_FILO_NO) return exjs_stringa(c, "", -1);
    for (; *s; s++) {
        int lascia = (*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') ||
                     (*s >= '0' && *s <= '9') || (*s && strchr_l("-_.!~*'()", (char)*s));
        if (!lascia && intera && strchr_l(URI_RISERVATI, (char)*s)) lascia = 1;
        if (lascia) exjs_arena_aggiungi(c, (const char *)s, 1);
        else {
            char t[3];
            t[0] = '%'; t[1] = H[*s >> 4]; t[2] = H[*s & 15];
            exjs_arena_aggiungi(c, t, 3);
        }
    }
    return exjs_arena_chiudi(c, f);
}

static int esa(char x)
{
    if (x >= '0' && x <= '9') return x - '0';
    if (x >= 'a' && x <= 'f') return x - 'a' + 10;
    if (x >= 'A' && x <= 'F') return x - 'A' + 10;
    return -1;
}

static ExJsVal nat_decodifica(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    const char  *s = stabile(c, arg(a, n, 0));
    unsigned int f;

    (void)q; (void)d;
    f = exjs_arena_apri(c);
    if (f == EXJS_FILO_NO) return exjs_stringa(c, "", -1);
    for (; *s; s++) {
        if (*s == '%' && esa(s[1]) >= 0 && esa(s[2]) >= 0) {
            char b = (char)(esa(s[1]) * 16 + esa(s[2]));
            exjs_arena_aggiungi(c, &b, 1);
            s += 2;
        } else exjs_arena_aggiungi(c, s, 1);
    }
    return exjs_arena_chiudi(c, f);
}

/* =============================================================================
 * Math, the rest: with the x87 coprocessor
 *
 * ! THE FPU INSTRUCTIONS AND NOT A SERIES: fsin, fpatan, fyl2x and f2xm1 are
 * on every processor EX-OS runs on (the i386 target) and on the host bench
 * (x86-64 still has the x87), they are exact to the last bit of a double, and
 * cost one line each. math.h is not available here: the engine is built
 * freestanding.
 * ========================================================================== */
static long double x87_log2(long double x)
{
    long double r;
    __asm__ ("fyl2x" : "=t"(r) : "0"(x), "u"(1.0L) : "st(1)");
    return r;
}

/* 2^x = 2^int * 2^frac, with f2xm1 on the fraction (|frac| < 1) */
static long double x87_exp2(long double x)
{
    long double i, f, r;

    if (x != x) return x;
    if (x > 16384.0L) return 1.0L / 0.0L;
    if (x < -16500.0L) return 0.0L;
    i = (long double)(long long)x;
    f = x - i;
    __asm__ ("f2xm1" : "=t"(r) : "0"(f));
    r += 1.0L;
    __asm__ ("fscale" : "=t"(r) : "0"(r), "u"(i));
    return r;
}

static long double x87_atan2(long double y, long double x)
{
    long double r;
    __asm__ ("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}

static long double x87_sin(long double x)  { __asm__ ("fsin" : "+t"(x)); return x; }
static long double x87_cos(long double x)  { __asm__ ("fcos" : "+t"(x)); return x; }
static long double x87_sqrt(long double x) { __asm__ ("fsqrt" : "+t"(x)); return x; }

#define LN2   0.693147180559945309417232121458L
#define LOG2E 1.442695040888963407359924681002L

double exjs_potenza(double b, double e)
{
    long long k;
    double    r = 1.0, x = b;
    int       neg;

    if (e != e) return e;
    if (e == 0.0) return 1.0;
    if (b != b) return b;
    /* an integer exponent: by squaring, exact where it can be */
    if (e == (double)(long long)e && e < 1e18 && e > -1e18) {
        k = (long long)e;
        neg = k < 0;
        if (neg) k = -k;
        while (k) { if (k & 1) r *= x; x *= x; k >>= 1; }
        return neg ? 1.0 / r : r;
    }
    if (b < 0) return 0.0 / 0.0;            /* (-8) ** (1/3) is NaN, as in JS */
    if (b == 0) return e > 0 ? 0.0 : 1.0 / 0.0;
    return (double)x87_exp2((long double)e * x87_log2((long double)b));
}

static double num(ExJsCtx *c, const ExJsVal *a, int n, int i)
{
    return i < n ? exjs_a_numero(c, a[i]) : 0.0 / 0.0;
}

/* d: which function */
static ExJsVal nat_math(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    long double x = num(c, a, n, 0), r;

    (void)q;
    switch ((int)(long)d) {
    case 0:  r = x87_sin(x); break;
    case 1:  r = x87_cos(x); break;
    case 2:  r = x87_sin(x) / x87_cos(x); break;                    /* tan */
    case 3:  r = x87_atan2(x, 1.0L); break;                         /* atan */
    case 4:  r = x87_atan2(x, num(c, a, n, 1)); break;               /* atan2 */
    case 5:  r = (x < -1 || x > 1) ? 0.0L / 0.0L : x87_atan2(x, x87_sqrt(1 - x * x)); break;  /* asin */
    case 6:  r = (x < -1 || x > 1) ? 0.0L / 0.0L : x87_atan2(x87_sqrt(1 - x * x), x); break;  /* acos */
    case 7:  r = x < 0 ? 0.0L / 0.0L : x == 0 ? -1.0L / 0.0L : x87_log2(x) * LN2; break;      /* log */
    case 8:  r = x < 0 ? 0.0L / 0.0L : x == 0 ? -1.0L / 0.0L : x87_log2(x); break;            /* log2 */
    case 9:  r = x < 0 ? 0.0L / 0.0L : x == 0 ? -1.0L / 0.0L :
                 x87_log2(x) / 3.321928094887362347870319429489L; break;                      /* log10 */
    case 10: r = x87_exp2(x * LOG2E); break;                                                  /* exp */
    case 11: r = (x > 0 ? 1 : x < 0 ? -1 : x); break;                                         /* sign */
    case 12: r = (long double)(long long)x; if (x != x || x > 9e18 || x < -9e18) r = x; break; /* trunc */
    case 13: r = x < 0 ? -x87_exp2(x87_log2(-x) / 3) : x == 0 ? x : x87_exp2(x87_log2(x) / 3); break; /* cbrt */
    case 14: {                                                                                 /* hypot */
        int i;
        long double s = 0;
        for (i = 0; i < n; i++) { long double y = exjs_a_numero(c, a[i]); s += y * y; }
        r = x87_sqrt(s);
        break;
    }
    case 15: r = x87_exp2(x * LOG2E) - 1; break;                                              /* expm1 */
    case 16: r = x87_log2(1 + x) * LN2; break;                                                /* log1p */
    case 17: { long double e = x87_exp2(x * LOG2E); r = (e - 1 / e) / 2; break; }             /* sinh */
    case 18: { long double e = x87_exp2(x * LOG2E); r = (e + 1 / e) / 2; break; }             /* cosh */
    case 19: { long double e = x87_exp2(2 * x * LOG2E); r = (e - 1) / (e + 1); break; }       /* tanh */
    default: r = (double)(float)x; break;                                                     /* fround */
    }
    return exjs_numero(c, (double)r);
}

static ExJsVal nat_math_pow(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q; (void)d;
    return exjs_numero(c, exjs_potenza(num(c, a, n, 0), num(c, a, n, 1)));
}

/* =============================================================================
 * Map and Set
 *
 * ! TWO VECTORS, NOT A HASH: keys in "\001k", values in "\001w" (for a Set
 * only "\001v"). Finding a key is a linear walk with SameValueZero - fine for
 * the dozens of entries of the code ExJs runs, and every other structure
 * would need a hash of an object, which the engine does not have. for..of and
 * ... read "\001v" (a Set) or "\001k" + "\001w" (a Map): see elementi() in
 * run.c.
 * ========================================================================== */
static int stesso_zero(ExJsCtx *c, ExJsVal x, ExJsVal y)
{
    if (exjs_tipo(c, x) == EXJS_NUMERO && exjs_tipo(c, y) == EXJS_NUMERO) {
        double u = exjs_a_numero(c, x), v = exjs_a_numero(c, y);
        if (u != u && v != v) return 1;
        return u == v;
    }
    return exjs_identici_pub(c, x, y);
}

static int cerca_in(ExJsCtx *c, ExJsVal vet, ExJsVal x)
{
    unsigned int i, l = exjs_lunghezza(c, vet);
    for (i = 0; i < l; i++) if (stesso_zero(c, exjs_indice_prendi(c, vet, i), x)) return (int)i;
    return -1;
}

static void togli_da(ExJsCtx *c, ExJsVal vet, unsigned int i)
{
    unsigned int l = exjs_lunghezza(c, vet), j;
    for (j = i; j + 1 < l; j++) exjs_indice_metti(c, vet, j, exjs_indice_prendi(c, vet, j + 1));
    exjs_vettore_tronca(c, vet, l - 1);
}

static int proto_map = -1, proto_set = -1;      /* see registration */

static ExJsVal nat_Map(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal o = exjs_oggetto(c), k = exjs_vettore(c), w = exjs_vettore(c);
    int     set = (int)(long)d;

    (void)q;
    if (exjs_a_oggetto(o) < 0) return o;
    exjs_ogg(c, exjs_a_oggetto(o))->proto = set ? proto_set : proto_map;
    if (set) exjs_metti(c, o, "\001v", k);
    else { exjs_metti(c, o, "\001k", k); exjs_metti(c, o, "\001w", w); }
    if (n > 0 && !(exjs_tipo(c, a[0]) == EXJS_INDEFINITO || exjs_tipo(c, a[0]) == EXJS_NULLO)) {
        unsigned int i, l = lunghezza_di(c, a[0]);
        for (i = 0; i < l; i++) {
            ExJsVal x = elemento(c, a[0], i);
            if (set) { if (cerca_in(c, k, x) < 0) exjs_indice_metti(c, k, exjs_lunghezza(c, k), x); }
            else {
                ExJsVal kk = elemento(c, x, 0), vv = elemento(c, x, 1);
                int     p = cerca_in(c, k, kk);
                if (p >= 0) exjs_indice_metti(c, w, (unsigned int)p, vv);
                else {
                    exjs_indice_metti(c, k, exjs_lunghezza(c, k), kk);
                    exjs_indice_metti(c, w, exjs_lunghezza(c, w), vv);
                }
            }
        }
    }
    return o;
}

/* d: 0 get, 1 set, 2 has, 3 delete, 4 clear, 5 size, 6 add (Set) */
static ExJsVal nat_map_op(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal k = exjs_prendi(c, q, "\001k"), w = exjs_prendi(c, q, "\001w");
    ExJsVal x = arg(a, n, 0);
    int     set = 0, p, op = (int)(long)d;

    if (exjs_a_oggetto(k) < 0) { k = exjs_prendi(c, q, "\001v"); set = 1; }
    if (exjs_a_oggetto(k) < 0) {
        exjs_lancia_errore(c, "TypeError", "non e' una Map ne' un Set");
        return exjs_indefinito();
    }
    p = cerca_in(c, k, x);
    switch (op) {
    case 0: return (p >= 0 && !set) ? exjs_indice_prendi(c, w, (unsigned int)p) : exjs_indefinito();
    case 1: case 6:
        if (p >= 0) { if (!set) exjs_indice_metti(c, w, (unsigned int)p, arg(a, n, 1)); }
        else {
            exjs_indice_metti(c, k, exjs_lunghezza(c, k), x);
            if (!set) exjs_indice_metti(c, w, exjs_lunghezza(c, w), arg(a, n, 1));
        }
        return q;
    case 2: return exjs_booleano(p >= 0);
    case 3:
        if (p < 0) return exjs_booleano(0);
        togli_da(c, k, (unsigned int)p);
        if (!set) togli_da(c, w, (unsigned int)p);
        return exjs_booleano(1);
    case 4:
        exjs_vettore_tronca(c, k, 0);
        if (!set) exjs_vettore_tronca(c, w, 0);
        return exjs_indefinito();
    default: return exjs_numero(c, (double)exjs_lunghezza(c, k));
    }
}

/* d: 0 keys, 1 values, 2 entries: a vector (for..of reads it the same) */
static ExJsVal nat_map_elenco(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal      k = exjs_prendi(c, q, "\001k"), w = exjs_prendi(c, q, "\001w"), out = exjs_vettore(c);
    unsigned int i, l;
    int          modo = (int)(long)d;

    (void)a; (void)n;
    if (exjs_a_oggetto(k) < 0) { k = exjs_prendi(c, q, "\001v"); w = k; }
    for (i = 0, l = exjs_lunghezza(c, k); i < l; i++) {
        if (modo == 0) exjs_indice_metti(c, out, i, exjs_indice_prendi(c, k, i));
        else if (modo == 1) exjs_indice_metti(c, out, i, exjs_indice_prendi(c, w, i));
        else {
            ExJsVal cp = exjs_vettore(c);
            exjs_indice_metti(c, cp, 0, exjs_indice_prendi(c, k, i));
            exjs_indice_metti(c, cp, 1, exjs_indice_prendi(c, w, i));
            exjs_indice_metti(c, out, i, cp);
        }
    }
    return out;
}

static ExJsVal nat_map_forEach(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal      k = exjs_prendi(c, q, "\001k"), w = exjs_prendi(c, q, "\001w"), f = arg(a, n, 0);
    unsigned int i;

    (void)d;
    if (exjs_a_oggetto(k) < 0) { k = exjs_prendi(c, q, "\001v"); w = k; }
    if (!e_funzione(c, f)) return exjs_indefinito();
    for (i = 0; i < exjs_lunghezza(c, k) && !exjs_interrotto(c); i++) {
        ExJsVal x[3];
        x[0] = exjs_indice_prendi(c, w, i);
        x[1] = exjs_indice_prendi(c, k, i);
        x[2] = q;
        exjs_chiama(c, f, arg(a, n, 1), x, 3, 0);
    }
    return exjs_indefinito();
}

/* =============================================================================
 * Registration
 * ========================================================================== */
static void nat(ExJsCtx *c, ExJsVal dove, const char *nome, ExJsNativa f, void *d)
{
    exjs_metti(c, dove, nome, exjs_nativa(c, f, d, nome));
}

void exjs_libreria_registra(ExJsCtx *c)
{
    ExJsVal g = exjs_globale(c), O, A, N;
    ExJsVal ps = exjs_da_oggetto(exjs_proto_str(c));
    ExJsVal pv = exjs_da_oggetto(exjs_proto_vet(c));
    ExJsVal pn = exjs_da_oggetto(exjs_proto_num(c));
    ExJsVal pf = exjs_da_oggetto(exjs_proto_fun(c));
    ExJsVal po = exjs_da_oggetto(exjs_proto_ogg(c));

    O = exjs_nativa(c, nat_Object, 0, "Object");
    exjs_metti(c, g, "Object", O);
    exjs_metti(c, O, "prototype", po);
    nat(c, O, "keys",    nat_chiavi, (void *)0);
    nat(c, O, "getOwnPropertyNames", nat_chiavi, (void *)0);
    nat(c, O, "values",  nat_chiavi, (void *)1);
    nat(c, O, "entries", nat_chiavi, (void *)2);
    nat(c, O, "assign",  nat_assign, 0);
    nat(c, O, "create",  nat_create, 0);
    nat(c, O, "getPrototypeOf", nat_getPrototypeOf, 0);
    nat(c, O, "defineProperty", nat_defineProperty, 0);
    nat(c, O, "defineProperties", nat_defineProperties, 0);
    nat(c, O, "freeze",  nat_identita, 0);
    nat(c, O, "seal",    nat_identita, 0);
    nat(c, O, "preventExtensions", nat_identita, 0);
    nat(c, O, "is",      nat_is, 0);

    nat(c, po, "hasOwnProperty", nat_hasOwn, 0);
    nat(c, po, "toString", nat_ogg_testo, 0);
    nat(c, po, "valueOf",  nat_questo, 0);

    A = exjs_nativa(c, nat_Array, 0, "Array");
    exjs_metti(c, g, "Array", A);
    exjs_metti(c, A, "prototype", pv);
    nat(c, A, "isArray", nat_isArray, 0);
    nat(c, A, "from",    nat_from, 0);
    nat(c, A, "of",      nat_of, 0);

    nat(c, pv, "includes",    nat_includes_vet, 0);
    nat(c, pv, "some",        nat_cerca, (void *)0);
    nat(c, pv, "every",       nat_cerca, (void *)1);
    nat(c, pv, "find",        nat_cerca, (void *)2);
    nat(c, pv, "findIndex",   nat_cerca, (void *)3);
    nat(c, pv, "reduce",      nat_reduce, (void *)0);
    nat(c, pv, "reduceRight", nat_reduce, (void *)1);
    nat(c, pv, "splice",      nat_splice, 0);
    nat(c, pv, "fill",        nat_fill, 0);
    nat(c, pv, "at",          nat_at, 0);
    nat(c, pv, "flat",        nat_flat, 0);
    nat(c, pv, "toString",    nat_vet_testo, 0);

    nat(c, ps, "includes",   nat_str_prova, (void *)0);
    nat(c, ps, "startsWith", nat_str_prova, (void *)1);
    nat(c, ps, "endsWith",   nat_str_prova, (void *)2);
    nat(c, ps, "padStart",   nat_pad, (void *)0);
    nat(c, ps, "padEnd",     nat_pad, (void *)1);
    nat(c, ps, "repeat",     nat_repeat, 0);
    nat(c, ps, "trimStart",  nat_trim_lato, (void *)0);
    nat(c, ps, "trimEnd",    nat_trim_lato, (void *)1);
    nat(c, ps, "concat",     nat_str_concat, 0);
    nat(c, ps, "at",         nat_at, 0);
    nat(c, ps, "toString",   nat_str_testo, 0);
    nat(c, ps, "valueOf",    nat_str_testo, 0);

    nat(c, pn, "toFixed",  nat_toFixed, 0);
    nat(c, pn, "toString", nat_num_testo, 0);
    nat(c, pn, "valueOf",  nat_questo, 0);
    N = exjs_prendi(c, g, "Number");
    if (exjs_a_oggetto(N) >= 0) {
        exjs_metti(c, N, "prototype", pn);
        nat(c, N, "isInteger", nat_isInteger, (void *)0);
        nat(c, N, "isSafeInteger", nat_isInteger, (void *)0);
        nat(c, N, "isFinite",  nat_isInteger, (void *)1);
        nat(c, N, "isNaN",     nat_numIsNaN, 0);
        exjs_metti(c, N, "parseFloat", exjs_prendi(c, g, "parseFloat"));
        exjs_metti(c, N, "parseInt",   exjs_prendi(c, g, "parseInt"));
        exjs_metti(c, N, "MAX_SAFE_INTEGER", exjs_numero(c, 9007199254740991.0));
        exjs_metti(c, N, "MIN_SAFE_INTEGER", exjs_numero(c, -9007199254740991.0));
        exjs_metti(c, N, "EPSILON",   exjs_numero(c, 2.220446049250313e-16));
        exjs_metti(c, N, "MAX_VALUE", exjs_numero(c, 1.7976931348623157e308));
        exjs_metti(c, N, "MIN_VALUE", exjs_numero(c, 5e-324));
    }
    {
        ExJsVal S = exjs_prendi(c, g, "String");
        if (exjs_a_oggetto(S) >= 0) exjs_metti(c, S, "prototype", ps);
    }

    nat(c, pf, "call",     nat_call, 0);
    nat(c, pf, "apply",    nat_apply, 0);
    nat(c, pf, "bind",     nat_bind, 0);
    nat(c, pf, "toString", nat_fun_testo, 0);

    nat(c, g, "encodeURIComponent", nat_codifica, (void *)0);
    nat(c, g, "encodeURI",          nat_codifica, (void *)1);
    nat(c, g, "decodeURIComponent", nat_decodifica, 0);
    nat(c, g, "decodeURI",          nat_decodifica, 0);

    exjs_metti(c, g, "globalThis", g);       /* NaN, Infinity, undefined: val.c */
    exjs_regexp_registra(c);

    {
        static const char *const NOMI[] = { "sin", "cos", "tan", "atan", "atan2", "asin",
            "acos", "log", "log2", "log10", "exp", "sign", "trunc", "cbrt", "hypot",
            "expm1", "log1p", "sinh", "cosh", "tanh", "fround" };
        ExJsVal      M = exjs_prendi(c, g, "Math");
        unsigned int i;

        for (i = 0; i < sizeof(NOMI) / sizeof(NOMI[0]); i++) nat(c, M, NOMI[i], nat_math, (void *)(long)i);
        nat(c, M, "pow", nat_math_pow, 0);                  /* esponenti non interi */
        exjs_metti(c, M, "LN2",     exjs_numero(c, 0.6931471805599453));
        exjs_metti(c, M, "LN10",    exjs_numero(c, 2.302585092994046));
        exjs_metti(c, M, "LOG2E",   exjs_numero(c, 1.4426950408889634));
        exjs_metti(c, M, "LOG10E",  exjs_numero(c, 0.4342944819032518));
        exjs_metti(c, M, "SQRT2",   exjs_numero(c, 1.4142135623730951));
        exjs_metti(c, M, "SQRT1_2", exjs_numero(c, 0.7071067811865476));
    }

    {
        ExJsVal pm = exjs_oggetto(c), pse = exjs_oggetto(c), Mp, Sp;

        proto_map = exjs_a_oggetto(pm);
        proto_set = exjs_a_oggetto(pse);
        Mp = exjs_nativa(c, nat_Map, (void *)0, "Map");
        Sp = exjs_nativa(c, nat_Map, (void *)1, "Set");
        exjs_metti(c, g, "Map", Mp);
        exjs_metti(c, g, "Set", Sp);
        exjs_metti(c, Mp, "prototype", pm);
        exjs_metti(c, Sp, "prototype", pse);
        exjs_metti(c, pm, "constructor", Mp);
        exjs_metti(c, pse, "constructor", Sp);
        nat(c, pm, "get",    nat_map_op, (void *)0);
        nat(c, pm, "set",    nat_map_op, (void *)1);
        nat(c, pm, "has",    nat_map_op, (void *)2);
        nat(c, pm, "delete", nat_map_op, (void *)3);
        nat(c, pm, "clear",  nat_map_op, (void *)4);
        nat(c, pm, "keys",    nat_map_elenco, (void *)0);
        nat(c, pm, "values",  nat_map_elenco, (void *)1);
        nat(c, pm, "entries", nat_map_elenco, (void *)2);
        nat(c, pm, "forEach", nat_map_forEach, 0);
        exjs_accessore_metti(c, pm, "size", exjs_nativa(c, nat_map_op, (void *)5, "size"), exjs_indefinito());
        nat(c, pse, "add",    nat_map_op, (void *)6);
        nat(c, pse, "has",    nat_map_op, (void *)2);
        nat(c, pse, "delete", nat_map_op, (void *)3);
        nat(c, pse, "clear",  nat_map_op, (void *)4);
        nat(c, pse, "keys",    nat_map_elenco, (void *)0);
        nat(c, pse, "values",  nat_map_elenco, (void *)0);
        nat(c, pse, "entries", nat_map_elenco, (void *)2);
        nat(c, pse, "forEach", nat_map_forEach, 0);
        exjs_accessore_metti(c, pse, "size", exjs_nativa(c, nat_map_op, (void *)5, "size"), exjs_indefinito());
    }
}
