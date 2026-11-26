/* =============================================================================
 * exwin/bin/exbrowser/browser_intl.c — Intl over ICU4C (P11)
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QuickJS has no Intl. ICU4C 79.1 (tools/icu-exos: libicuuc.so.79,
 * libicui18n.so.79, the data in /lib/icu/icudt79l.dat) is opened with dlopen
 * the first time a page asks, and one native, __exos_icu(op, ...), answers
 * the Intl objects of browser_intl.js:
 *   "loc", tag            -> the canonical tag, or "" if ICU does not take it
 *   "max"/"min", tag      -> with/without the likely subtags (Intl.Locale)
 *   "num", loc, skel, x   -> a number formatted with an ICU number skeleton
 *   "dat", loc, skel, tz, ms   -> a date; skel "S<d><t>" are the styles
 *                                 (0 none, 1 full .. 4 short), else a
 *                                 skeleton through the pattern generator
 *   "pat", loc, skel      -> the pattern the generator chooses (hourCycle)
 *   "plu", loc, tipo, x   -> the plural category (cardinal or ordinal)
 *   "rel", loc, opz, v, u -> a relative time ("ieri", "tra 3 giorni")
 *   "col", loc, opz, a, b -> -1, 0, 1 with a collator
 *   "seg", loc, gran, s   -> the segments as JSON [[start, end, word], ...]
 *   "lis", loc, opz, json -> a list ("a, b e c")
 *   "nom", loc, opz, code -> a display name (language, region, script, currency)
 *   "tz"                  -> the system's zone as an IANA name
 * Indexes are UTF-16 units, as JavaScript's.
 *
 * ! ICU'S NAMES CARRY ITS VERSION (u_strlen_79): the symbols are looked up
 * with the suffix. ! THE DATA FILE IS SET BEFORE ANYTHING ELSE
 * (u_setDataDirectory): the libraries carry only a stub.
 * ========================================================================== */
#include "browser_priv.h"
#include "browser_intl_js.h"

#define ICU_V "_79"
typedef unsigned short UC;

static int   g_cercata, g_pronta;
static void *g_uc, *g_i18n;

/* common */
static void  (*u_dir)(const char *);
static void  (*u_da8)(UC *, int, int *, const char *, int, int *);
static void  (*u_a8)(char *, int, int *, const UC *, int, int *);
static int   (*loc_da_tag)(const char *, char *, int, int *, int *);
static int   (*loc_a_tag)(const char *, char *, int, int, int *);
static int   (*loc_max)(const char *, char *, int, int *);
static int   (*loc_min)(const char *, char *, int, int *);
static void *(*brk_apri)(int, const char *, const UC *, int, int *);
static int   (*brk_primo)(void *);
static int   (*brk_dopo)(void *);
static int   (*brk_stato)(const void *);
static void  (*brk_chiudi)(void *);
static void *(*ldn_apri)(const char *, int, int *);
static int   (*ldn_lingua)(const void *, const char *, UC *, int, int *);
static int   (*ldn_regione)(const void *, const char *, UC *, int, int *);
static int   (*ldn_script)(const void *, const char *, UC *, int, int *);
static void  (*ldn_chiudi)(void *);
static const UC *(*cur_nome)(const UC *, const char *, int, signed char *, int *, int *);
/* i18n */
static void *(*nf_apri)(const UC *, int, const char *, int *);
static void *(*nf_ris)(int *);
static void  (*nf_double)(const void *, double, void *, int *);
static int   (*nf_a_stringa)(const void *, UC *, int, int *);
static void  (*nf_chiudi)(void *);
static void  (*nf_ris_chiudi)(void *);
static void *(*dtpg_apri)(const char *, int *);
static int   (*dtpg_migliore)(void *, const UC *, int, UC *, int, int *);
static void  (*dtpg_chiudi)(void *);
static void *(*df_apri)(int, int, const char *, const UC *, int, const UC *, int, int *);
static int   (*df_formatta)(const void *, double, UC *, int, void *, int *);
static void  (*df_chiudi)(void *);
static void *(*pl_apri)(const char *, int, int *);
static int   (*pl_scegli)(const void *, double, UC *, int, int *);
static void  (*pl_chiudi)(void *);
static void *(*rel_apri)(const char *, void *, int, int, int *);
static int   (*rel_num)(const void *, double, int, UC *, int, int *);
static int   (*rel_parole)(const void *, double, int, UC *, int, int *);
static void  (*rel_chiudi)(void *);
static void *(*col_apri)(const char *, int *);
static void  (*col_attr)(void *, int, int, int *);
static int   (*col_conf)(const void *, const UC *, int, const UC *, int);
static void  (*col_chiudi)(void *);
static void *(*lf_apri)(const char *, int, int, int *);
static int   (*lf_formatta)(const void *, const UC *const *, const int *, int, UC *, int, int *);
static void  (*lf_chiudi)(void *);

static void *sim(void *l, const char *n, int *ok)
{
    char  b[96];
    void *p;

    snprintf(b, sizeof(b), "%s" ICU_V, n);
    p = dlsym(l, b);
    if (!p) { *ok = 0; printf("exbrowser: Intl: manca %s\n", b); }
    return p;
}

static int icu_pronta(void)
{
    int ok = 1;

    if (g_cercata) return g_pronta;
    g_cercata = 1;
    g_uc = dlopen("libicuuc.so.79", RTLD_NOW);
    g_i18n = g_uc ? dlopen("libicui18n.so.79", RTLD_NOW) : 0;
    if (!g_i18n) return 0;
    *(void **)&u_dir       = sim(g_uc, "u_setDataDirectory", &ok);
    *(void **)&u_da8       = sim(g_uc, "u_strFromUTF8", &ok);
    *(void **)&u_a8        = sim(g_uc, "u_strToUTF8", &ok);
    *(void **)&loc_da_tag  = sim(g_uc, "uloc_forLanguageTag", &ok);
    *(void **)&loc_a_tag   = sim(g_uc, "uloc_toLanguageTag", &ok);
    *(void **)&loc_max     = sim(g_uc, "uloc_addLikelySubtags", &ok);
    *(void **)&loc_min     = sim(g_uc, "uloc_minimizeSubtags", &ok);
    *(void **)&brk_apri    = sim(g_uc, "ubrk_open", &ok);
    *(void **)&brk_primo   = sim(g_uc, "ubrk_first", &ok);
    *(void **)&brk_dopo    = sim(g_uc, "ubrk_next", &ok);
    *(void **)&brk_stato   = sim(g_uc, "ubrk_getRuleStatus", &ok);
    *(void **)&brk_chiudi  = sim(g_uc, "ubrk_close", &ok);
    *(void **)&ldn_apri    = sim(g_uc, "uldn_open", &ok);
    *(void **)&ldn_lingua  = sim(g_uc, "uldn_localeDisplayName", &ok);
    *(void **)&ldn_regione = sim(g_uc, "uldn_regionDisplayName", &ok);
    *(void **)&ldn_script  = sim(g_uc, "uldn_scriptDisplayName", &ok);
    *(void **)&ldn_chiudi  = sim(g_uc, "uldn_close", &ok);
    *(void **)&cur_nome    = sim(g_uc, "ucurr_getName", &ok);
    *(void **)&nf_apri     = sim(g_i18n, "unumf_openForSkeletonAndLocale", &ok);
    *(void **)&nf_ris      = sim(g_i18n, "unumf_openResult", &ok);
    *(void **)&nf_double   = sim(g_i18n, "unumf_formatDouble", &ok);
    *(void **)&nf_a_stringa = sim(g_i18n, "unumf_resultToString", &ok);
    *(void **)&nf_chiudi   = sim(g_i18n, "unumf_close", &ok);
    *(void **)&nf_ris_chiudi = sim(g_i18n, "unumf_closeResult", &ok);
    *(void **)&dtpg_apri   = sim(g_i18n, "udatpg_open", &ok);
    *(void **)&dtpg_migliore = sim(g_i18n, "udatpg_getBestPattern", &ok);
    *(void **)&dtpg_chiudi = sim(g_i18n, "udatpg_close", &ok);
    *(void **)&df_apri     = sim(g_i18n, "udat_open", &ok);
    *(void **)&df_formatta = sim(g_i18n, "udat_format", &ok);
    *(void **)&df_chiudi   = sim(g_i18n, "udat_close", &ok);
    *(void **)&pl_apri     = sim(g_i18n, "uplrules_openForType", &ok);
    *(void **)&pl_scegli   = sim(g_i18n, "uplrules_select", &ok);
    *(void **)&pl_chiudi   = sim(g_i18n, "uplrules_close", &ok);
    *(void **)&rel_apri    = sim(g_i18n, "ureldatefmt_open", &ok);
    *(void **)&rel_num     = sim(g_i18n, "ureldatefmt_formatNumeric", &ok);
    *(void **)&rel_parole  = sim(g_i18n, "ureldatefmt_format", &ok);
    *(void **)&rel_chiudi  = sim(g_i18n, "ureldatefmt_close", &ok);
    *(void **)&col_apri    = sim(g_i18n, "ucol_open", &ok);
    *(void **)&col_attr    = sim(g_i18n, "ucol_setAttribute", &ok);
    *(void **)&col_conf    = sim(g_i18n, "ucol_strcoll", &ok);
    *(void **)&col_chiudi  = sim(g_i18n, "ucol_close", &ok);
    *(void **)&lf_apri     = sim(g_i18n, "ulistfmt_openForType", &ok);
    *(void **)&lf_formatta = sim(g_i18n, "ulistfmt_format", &ok);
    *(void **)&lf_chiudi   = sim(g_i18n, "ulistfmt_close", &ok);
    if (ok) u_dir("/lib/icu");
    g_pronta = ok;
    return ok;
}

/* ---------------------------------------------------------- UTF-8 <-> 16 */
#define U_MAX 8192
static UC   g_u1[U_MAX], g_u2[U_MAX], g_uo[U_MAX];
static char g_o[4 * U_MAX];

static int a16(const char *s, UC *o)
{
    int n = 0, e = 0;

    u_da8(o, U_MAX - 1, &n, s ? s : "", -1, &e);
    if (e > 0) return -1;
    if (n >= U_MAX) n = U_MAX - 1;
    o[n] = 0;
    return n;
}

static ExJsVal da16(ExJsCtx *c, const UC *s, int n)
{
    int k = 0, e = 0;

    if (n < 0) return exjs_stringa(c, "", -1);
    u_a8(g_o, (int)sizeof(g_o) - 1, &k, s, n, &e);
    if (e > 0) return exjs_stringa(c, "", -1);
    g_o[k] = 0;
    return exjs_stringa(c, g_o, k);
}

/* a locale id of ICU from a BCP 47 tag */
static void loc_id(const char *tag, char *o, int max)
{
    int e = 0, p = 0;

    loc_da_tag(tag && tag[0] ? tag : "it", o, max, &p, &e);
    if (e > 0 || !o[0]) snprintf(o, (size_t)max, "it");
}

/* ------------------------------------------------- small caches of objects */
#define CACHE 24
typedef struct { char chiave[160]; void *o; int tipo; } Voce;
static Voce g_cache[CACHE];
static int  g_cache_n;

static void *cache_cerca(int tipo, const char *chiave)
{
    int i;

    for (i = 0; i < g_cache_n; i++)
        if (g_cache[i].tipo == tipo && !strcmp(g_cache[i].chiave, chiave)) return g_cache[i].o;
    return 0;
}

static void cache_chiudi(Voce *v)
{
    switch (v->tipo) {
    case 1: nf_chiudi(v->o); break;
    case 2: df_chiudi(v->o); break;
    case 3: pl_chiudi(v->o); break;
    case 4: rel_chiudi(v->o); break;
    case 5: col_chiudi(v->o); break;
    case 6: lf_chiudi(v->o); break;
    case 7: dtpg_chiudi(v->o); break;
    case 8: ldn_chiudi(v->o); break;
    default: break;
    }
}

static void *cache_metti(int tipo, const char *chiave, void *o)
{
    Voce *v;

    if (!o) return 0;
    if (g_cache_n == CACHE) { cache_chiudi(&g_cache[0]); memmove(g_cache, g_cache + 1, sizeof(Voce) * (CACHE - 1)); g_cache_n--; }
    v = &g_cache[g_cache_n++];
    v->tipo = tipo; v->o = o;
    snprintf(v->chiave, sizeof(v->chiave), "%s", chiave);
    return o;
}

/* ---------------------------------------------------------------- the ops */
static ExJsVal fuso(ExJsCtx *c)
{
    int m = 60, ue = 1;
    char b[32];

    if (fuso_get(&m, &ue) != 0) { m = 60; ue = 1; }
    if (ue && m == 0)   return exjs_stringa(c, "Europe/London", -1);
    if (ue && m == 60)  return exjs_stringa(c, "Europe/Rome", -1);
    if (ue && m == 120) return exjs_stringa(c, "Europe/Athens", -1);
    if (m == 0) return exjs_stringa(c, "UTC", -1);
    if (m % 60) return exjs_stringa(c, "UTC", -1);
    /* ! Etc/GMT-1 IS ONE HOUR EAST: the sign is POSIX's, the other way */
    snprintf(b, sizeof(b), "Etc/GMT%+d", -m / 60);
    return exjs_stringa(c, b, -1);
}

static ExJsVal op_num(ExJsCtx *c, const char *loc, const char *skel, double x)
{
    char  ch[160], id[64];
    void *f, *r;
    int   e = 0, n, k;

    loc_id(loc, id, sizeof(id));
    snprintf(ch, sizeof(ch), "%s|%s", id, skel);
    f = cache_cerca(1, ch);
    if (!f) {
        if ((k = a16(skel, g_u1)) < 0) return exjs_stringa(c, "", -1);
        f = nf_apri(g_u1, k, id, &e);
        if (e > 0) { if (f) nf_chiudi(f); return exjs_stringa(c, "\001", -1); }   /* a bad skeleton */
        cache_metti(1, ch, f);
    }
    e = 0;
    r = nf_ris(&e);
    if (e > 0) return exjs_stringa(c, "", -1);
    nf_double(f, x, r, &e);
    n = nf_a_stringa(r, g_uo, U_MAX, &e);
    nf_ris_chiudi(r);
    return da16(c, g_uo, e > 0 ? -1 : n);
}

static void *generatore(const char *id)
{
    void *g = cache_cerca(7, id);
    int   e = 0;

    if (g) return g;
    g = dtpg_apri(id, &e);
    if (e > 0) return 0;
    return cache_metti(7, id, g);
}

static ExJsVal op_dat(ExJsCtx *c, const char *loc, const char *skel, const char *tz, double ms, int solo_schema)
{
    char  ch[200], id[64];
    void *f;
    int   e = 0, n, kt;

    loc_id(loc, id, sizeof(id));
    snprintf(ch, sizeof(ch), "%s|%s|%s", id, skel, tz);
    f = solo_schema ? 0 : cache_cerca(2, ch);
    if (!f) {
        if ((kt = a16(tz && tz[0] ? tz : "UTC", g_u2)) < 0) return exjs_stringa(c, "", -1);
        if (skel[0] == 'S') {
            /* UDAT_FULL 0 .. UDAT_SHORT 3, UDAT_NONE -1 */
            int d = skel[1] - '1', t = skel[2] - '1';

            f = df_apri(t < 0 ? -1 : t, d < 0 ? -1 : d, id, g_u2, kt, 0, 0, &e);
        } else {
            void *g = generatore(id);
            int   k = a16(skel, g_u1), p;

            if (!g || k < 0) return exjs_stringa(c, "", -1);
            p = dtpg_migliore(g, g_u1, k, g_uo, U_MAX, &e);
            if (e > 0) return exjs_stringa(c, "", -1);
            if (solo_schema) return da16(c, g_uo, p);
            memcpy(g_u1, g_uo, (size_t)(p + 1) * sizeof(UC));
            f = df_apri(-2, -2, id, g_u2, kt, g_u1, p, &e);        /* UDAT_PATTERN */
        }
        if (e > 0) { if (f) df_chiudi(f); return exjs_stringa(c, "", -1); }
        cache_metti(2, ch, f);
    }
    e = 0;
    n = df_formatta(f, ms, g_uo, U_MAX, 0, &e);
    return da16(c, g_uo, e > 0 ? -1 : n);
}

static ExJsVal op_plu(ExJsCtx *c, const char *loc, const char *tipo, double x)
{
    char  ch[100], id[64];
    void *p;
    int   e = 0, n, t = !strcmp(tipo, "ordinal");

    loc_id(loc, id, sizeof(id));
    snprintf(ch, sizeof(ch), "%s|%d", id, t);
    p = cache_cerca(3, ch);
    if (!p) { p = pl_apri(id, t, &e); if (e > 0) return exjs_stringa(c, "other", -1); cache_metti(3, ch, p); }
    n = pl_scegli(p, x, g_uo, U_MAX, &e);
    return da16(c, g_uo, e > 0 ? -1 : n);
}

/* opz: "<style>|<numeric>" style long/short/narrow, numeric always/auto;
 * unit: second..year as UDAT_REL_UNIT_* */
static ExJsVal op_rel(ExJsCtx *c, const char *loc, const char *opz, double v, const char *unit)
{
    static const char *U[] = { "year", "quarter", "month", "week", "day", "hour", "minute", "second", 0 };
    char  ch[100], id[64];
    void *f;
    int   e = 0, n, u, stile = 0;

    for (u = 0; U[u] && strncmp(unit, U[u], strlen(U[u])); u++) {}
    if (!U[u]) return exjs_stringa(c, "\001", -1);
    loc_id(loc, id, sizeof(id));
    if (!strncmp(opz, "short", 5)) stile = 1;
    else if (!strncmp(opz, "narrow", 6)) stile = 2;
    snprintf(ch, sizeof(ch), "%s|%d", id, stile);
    f = cache_cerca(4, ch);
    if (!f) { f = rel_apri(id, 0, stile, 256, &e);      /* UDISPCTX_CAPITALIZATION_NONE */ if (e > 0) return exjs_stringa(c, "", -1); cache_metti(4, ch, f); }
    /* UDAT_REL_UNIT_YEAR 0, QUARTER 1, MONTH 2, WEEK 3, DAY 4, HOUR 5, MINUTE 6, SECOND 7 */
    n = strstr(opz, "auto") ? rel_parole(f, v, u, g_uo, U_MAX, &e) : rel_num(f, v, u, g_uo, U_MAX, &e);
    return da16(c, g_uo, e > 0 ? -1 : n);
}

/* opz: sensitivity base/accent/case/variant, "n" numeric, "p" ignorePunctuation */
static ExJsVal op_col(ExJsCtx *c, const char *loc, const char *opz, const char *a, const char *b)
{
    char  ch[100], id[64];
    void *k;
    int   e = 0, na, nb, r;

    loc_id(loc, id, sizeof(id));
    snprintf(ch, sizeof(ch), "%s|%s", id, opz);
    k = cache_cerca(5, ch);
    if (!k) {
        k = col_apri(id, &e);
        if (e > 0) return exjs_numero(c, strcmp(a, b) < 0 ? -1 : strcmp(a, b) > 0);
        /* UCOL_STRENGTH 5: PRIMARY 0, SECONDARY 1, TERTIARY 2; UCOL_CASE_LEVEL 3 ON 17 */
        e = 0;
        if (!strncmp(opz, "base", 4)) col_attr(k, 5, 0, &e);
        else if (!strncmp(opz, "accent", 6)) col_attr(k, 5, 1, &e);
        else if (!strncmp(opz, "case", 4)) { col_attr(k, 5, 0, &e); col_attr(k, 3, 17, &e); }
        else col_attr(k, 5, 2, &e);
        if (strchr(opz, 'n')) col_attr(k, 7, 17, &e);        /* UCOL_NUMERIC_COLLATION */
        if (strchr(opz, 'p')) col_attr(k, 1, 20, &e);        /* UCOL_ALTERNATE_HANDLING SHIFTED */
        cache_metti(5, ch, k);
    }
    na = a16(a, g_u1); nb = a16(b, g_u2);
    if (na < 0 || nb < 0) return exjs_numero(c, 0);
    r = col_conf(k, g_u1, na, g_u2, nb);
    return exjs_numero(c, r < 0 ? -1 : r > 0 ? 1 : 0);
}

static ExJsVal op_seg(ExJsCtx *c, const char *loc, const char *gran, const char *s)
{
    char  id[64], *o = g_o;
    void *b;
    int   e = 0, n, tipo, p, q, k = 0;

    loc_id(loc, id, sizeof(id));
    n = a16(s, g_u1);
    if (n < 0) return exjs_stringa(c, "[]", -1);
    /* UBRK_CHARACTER 0, WORD 1, LINE 2, SENTENCE 3 */
    tipo = !strcmp(gran, "word") ? 1 : !strcmp(gran, "sentence") ? 3 : 0;
    b = brk_apri(tipo, id, g_u1, n, &e);
    if (e > 0) return exjs_stringa(c, "[]", -1);
    k += snprintf(o + k, sizeof(g_o) - (size_t)k, "[");
    for (p = brk_primo(b), q = brk_dopo(b); q != -1 && k < (int)sizeof(g_o) - 40; p = q, q = brk_dopo(b)) {
        /* a word's rule status 200..499 are letters, numbers, kana, ideographs */
        int parola = tipo == 1 && brk_stato(b) >= 100;

        k += snprintf(o + k, sizeof(g_o) - (size_t)k, "%s[%d,%d,%d]", p ? "," : "", p, q, parola);
    }
    k += snprintf(o + k, sizeof(g_o) - (size_t)k, "]");
    brk_chiudi(b);
    return exjs_stringa(c, o, k);
}

/* opz "<type>|<style>": conjunction/disjunction/unit, long/short/narrow;
 * the items in the JSON-ish list "a\001b\001c" */
static ExJsVal op_lis(ExJsCtx *c, const char *loc, const char *opz, const char *voci)
{
    static UC   buf[U_MAX];
    const UC   *pv[64];
    int         lv[64], nv = 0, e = 0, n, tipo = 0, larg = 0, pos = 0;
    char        ch[100], id[64];
    const char *p = voci;
    void       *f;

    loc_id(loc, id, sizeof(id));
    if (!strncmp(opz, "disjunction", 11)) tipo = 1;
    else if (!strncmp(opz, "unit", 4)) tipo = 2;
    if (strstr(opz, "|short")) larg = 1;
    else if (strstr(opz, "|narrow")) larg = 2;
    snprintf(ch, sizeof(ch), "%s|%d|%d", id, tipo, larg);
    f = cache_cerca(6, ch);
    if (!f) { f = lf_apri(id, tipo, larg, &e); if (e > 0) return exjs_stringa(c, "", -1); cache_metti(6, ch, f); }
    while (p && *p && nv < 64) {
        const char *q = strchr(p, '\001');
        char        t[1024];
        size_t      l = q ? (size_t)(q - p) : strlen(p);
        int         k = 0;

        if (l >= sizeof(t)) l = sizeof(t) - 1;
        memcpy(t, p, l); t[l] = 0;
        e = 0;
        u_da8(buf + pos, U_MAX - pos - 1, &k, t, -1, &e);
        if (e > 0 || pos + k >= U_MAX - 1) break;
        pv[nv] = buf + pos; lv[nv] = k; nv++;
        pos += k + 1;
        p = q ? q + 1 : 0;
    }
    e = 0;
    n = lf_formatta(f, pv, lv, nv, g_uo, U_MAX, &e);
    return da16(c, g_uo, e > 0 ? -1 : n);
}

static ExJsVal op_nom(ExJsCtx *c, const char *loc, const char *opz, const char *code)
{
    char  id[64], ch[100];
    void *d;
    int   e = 0, n;

    loc_id(loc, id, sizeof(id));
    if (!strncmp(opz, "currency", 8)) {
        signed char plur = 0;
        int         l = 0;
        const UC   *r;

        if (a16(code, g_u1) < 0) return exjs_stringa(c, "", -1);
        r = cur_nome(g_u1, id, strstr(opz, "narrow") ? 2 : strstr(opz, "short") ? 0 : 1, &plur, &l, &e);  /* UCURR_SYMBOL_NAME 0, LONG 1, NARROW 2 */
        return da16(c, r, e > 0 || !r ? -1 : l);
    }
    snprintf(ch, sizeof(ch), "%s|nom", id);
    d = cache_cerca(8, ch);
    if (!d) { d = ldn_apri(id, 0, &e); if (e > 0) return exjs_stringa(c, "", -1); cache_metti(8, ch, d); }
    e = 0;
    if (!strncmp(opz, "region", 6)) n = ldn_regione(d, code, g_uo, U_MAX, &e);
    else if (!strncmp(opz, "script", 6)) n = ldn_script(d, code, g_uo, U_MAX, &e);
    else {
        char l[64];

        loc_id(code, l, sizeof(l));
        n = ldn_lingua(d, l, g_uo, U_MAX, &e);
    }
    return da16(c, g_uo, e > 0 ? -1 : n);
}

static ExJsVal nat_icu(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    static char s[4][16384];
    const char *t;
    int         i;

    (void)questo; (void)dato;
    if (!icu_pronta() || n_arg < 1) return exjs_stringa(c, "", -1);
    for (i = 0; i < 4; i++) {
        s[i][0] = 0;
        if (i + 1 < n_arg && exjs_tipo(c, a[i + 1]) == EXJS_STRINGA && (t = exjs_a_stringa(c, a[i + 1])))
            snprintf(s[i], sizeof(s[i]), "%s", t);
    }
    t = exjs_a_stringa(c, a[0]);
    if (!t) return exjs_stringa(c, "", -1);
    if (!strcmp(t, "tz")) return fuso(c);
    if (!strcmp(t, "loc") || !strcmp(t, "max") || !strcmp(t, "min")) {
        char id[96], id2[96], tag[96];
        int  e = 0, p = 0;

        loc_da_tag(s[0], id, sizeof(id), &p, &e);
        if (e > 0 || !id[0] || p < (int)strlen(s[0])) return exjs_stringa(c, "", -1);
        if (t[0] == 'm') {
            e = 0;
            if (t[1] == 'a') loc_max(id, id2, sizeof(id2), &e); else loc_min(id, id2, sizeof(id2), &e);
            if (e > 0) return exjs_stringa(c, "", -1);
            memcpy(id, id2, sizeof(id));
        }
        e = 0;
        loc_a_tag(id, tag, sizeof(tag), 1, &e);
        return exjs_stringa(c, e > 0 ? "" : tag, -1);
    }
    if (!strcmp(t, "num")) return op_num(c, s[0], s[1], n_arg > 3 ? exjs_a_numero(c, a[3]) : 0);
    if (!strcmp(t, "dat")) return op_dat(c, s[0], s[1], s[2], n_arg > 4 ? exjs_a_numero(c, a[4]) : 0, 0);
    if (!strcmp(t, "pat")) return op_dat(c, s[0], s[1], "UTC", 0, 1);
    if (!strcmp(t, "plu")) return op_plu(c, s[0], s[1], n_arg > 3 ? exjs_a_numero(c, a[3]) : 0);
    if (!strcmp(t, "rel")) return op_rel(c, s[0], s[1], n_arg > 3 ? exjs_a_numero(c, a[3]) : 0, s[3]);
    if (!strcmp(t, "col")) return op_col(c, s[0], s[1], s[2], s[3]);
    if (!strcmp(t, "seg")) return op_seg(c, s[0], s[1], s[2]);
    if (!strcmp(t, "lis")) return op_lis(c, s[0], s[1], s[2]);
    if (!strcmp(t, "nom")) return op_nom(c, s[0], s[1], s[2]);
    return exjs_stringa(c, "", -1);
}

void intl_registra(ExJsCtx *js)
{
    ExJsErrore err;
    ExJsVal    r;

    /* ! ICU IS OPENED HERE, the first time a page runs scripts: 7 MB of
     * libraries and 4 MB of data, read once for the whole session */
    if (!icu_pronta()) return;
    exjs_metti(js, exjs_globale(js), "__exos_icu", exjs_nativa(js, nat_icu, 0, "__exos_icu"));
    memset(&err, 0, sizeof(err));
    if (!exjs_esegui(js, INTL_JS, (unsigned int)(sizeof(INTL_JS) - 1), &r, &err))
        printf("exbrowser: Intl, riga %d: %s\n", err.riga, err.messaggio);
}
