/* =============================================================================
 * exwin/bin/exbrowser/browser_idb.c — IndexedDB's storage: SQLite (P11)
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * One native, __exos_sql(sql, ...params) -> JSON {"r":[[...]],"n":changes} or
 * {"e":"message"}, on one SQLite file per origin: $HOME/.app/exbrowser/idb/
 * <origin in hex>.sqlite (dati_file, exbrowser.c), or in memory when there is
 * no place on disk. libsqlite3.so.0 (tools/sqlite-exos) is opened with
 * dlopen; without it there is no indexedDB, as before P11. IndexedDB itself is
 * browser_idb.js, run here after the prelude.
 *
 * ! THE PAGE NEVER NAMES THE FILE: the origin is the page's address, so a
 * page reaches its own databases and nobody else's. The SQL is the
 * prelude's own (browser_idb.js), but __exos_sql is a global a page script
 * can call too: it can only reach the same file, which is the page's anyway.
 * ========================================================================== */
#include "browser_priv.h"
#include "browser_idb_js.h"

int dati_file(const char *cartella, const char *est, char *out, unsigned int max);   /* exbrowser.c */

#define SQL_OUT_MAX (8u * 1024u * 1024u)

static int   (*sq_apri)(const char *, void **, int, const char *);
static int   (*sq_prepara)(void *, const char *, int, void **, const char **);
static int   (*sq_testo)(void *, int, const char *, int, void *);
static int   (*sq_double)(void *, int, double);
static int   (*sq_null)(void *, int);
static int   (*sq_passo)(void *);
static int   (*sq_colonne)(void *);
static int   (*sq_tipo)(void *, int);
static const unsigned char *(*sq_col_testo)(void *, int);
static double (*sq_col_double)(void *, int);
static long long (*sq_col_int)(void *, int);
static int   (*sq_fine)(void *);
static const char *(*sq_errore)(void *);
static int   (*sq_cambi)(void *);
static int   (*sq_chiudi)(void *);
static int   (*sq_attesa)(void *, int);

static int   g_cercata, g_pronta;
static void *g_db;
static const void *g_db_vista;        /* the view (g_vi) whose file is open */
static char *g_out;
static unsigned int g_out_n;

static void *sim(void *l, const char *n, int *ok)
{
    void *p = dlsym(l, n);

    if (!p) *ok = 0;
    return p;
}

static int sqlite_pronta(void)
{
    void *l;
    int   ok = 1;

    if (g_cercata) return g_pronta;
    g_cercata = 1;
    l = dlopen("libsqlite3.so.0", RTLD_NOW);
    if (!l) return 0;
    *(void **)&sq_apri       = sim(l, "sqlite3_open_v2", &ok);
    *(void **)&sq_prepara    = sim(l, "sqlite3_prepare_v2", &ok);
    *(void **)&sq_testo      = sim(l, "sqlite3_bind_text", &ok);
    *(void **)&sq_double     = sim(l, "sqlite3_bind_double", &ok);
    *(void **)&sq_null       = sim(l, "sqlite3_bind_null", &ok);
    *(void **)&sq_passo      = sim(l, "sqlite3_step", &ok);
    *(void **)&sq_colonne    = sim(l, "sqlite3_column_count", &ok);
    *(void **)&sq_tipo       = sim(l, "sqlite3_column_type", &ok);
    *(void **)&sq_col_testo  = sim(l, "sqlite3_column_text", &ok);
    *(void **)&sq_col_double = sim(l, "sqlite3_column_double", &ok);
    *(void **)&sq_col_int    = sim(l, "sqlite3_column_int64", &ok);
    *(void **)&sq_fine       = sim(l, "sqlite3_finalize", &ok);
    *(void **)&sq_errore     = sim(l, "sqlite3_errmsg", &ok);
    *(void **)&sq_cambi      = sim(l, "sqlite3_changes", &ok);
    *(void **)&sq_chiudi     = sim(l, "sqlite3_close", &ok);
    *(void **)&sq_attesa     = sim(l, "sqlite3_busy_timeout", &ok);
    g_pronta = ok;
    return ok;
}

/* ---------------------------------------------------------------- JSON out */
static int o_metti(const char *s, unsigned int n)
{
    if (g_out_n + n + 2 >= SQL_OUT_MAX) return 0;
    memcpy(g_out + g_out_n, s, n);
    g_out_n += n;
    return 1;
}

static int o_stringa(const unsigned char *s)
{
    char e[8];

    if (!o_metti("\"", 1)) return 0;
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { if (!o_metti("\\", 1) || !o_metti((const char *)s, 1)) return 0; }
        else if (*s < 0x20) { snprintf(e, sizeof(e), "\\u%04x", *s); if (!o_metti(e, 6)) return 0; }
        else if (!o_metti((const char *)s, 1)) return 0;
    }
    return o_metti("\"", 1);
}

static ExJsVal errore(ExJsCtx *c, const char *m)
{
    g_out_n = 0;
    o_metti("{\"e\":", 5);
    o_stringa((const unsigned char *)(m ? m : "errore"));
    o_metti("}", 1);
    g_out[g_out_n] = 0;
    return exjs_stringa(c, g_out, (int)g_out_n);
}

static int db_apri(void)
{
    char p[1400];

    if (g_db && g_db_vista == g_vi) return 1;
    if (g_db) { sq_chiudi(g_db); g_db = 0; }
    /* SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX */
    if (!dati_file("idb", ".sqlite", p, sizeof(p)) || sq_apri(p, &g_db, 0x06 | 0x10000, 0) != 0) {
        if (g_db) { sq_chiudi(g_db); g_db = 0; }
        if (sq_apri(":memory:", &g_db, 0x06, 0) != 0) { g_db = 0; return 0; }
    }
    sq_attesa(g_db, 2000);
    g_db_vista = g_vi;
    return 1;
}

static ExJsVal nat_sql(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    const char *sql, *coda;
    void       *st = 0;
    int         i, rc, prime = 1, nc;
    char        num[40];

    (void)questo; (void)dato;
    if (!g_out) g_out = (char *)malloc(SQL_OUT_MAX);
    if (!g_out) return exjs_stringa(c, "{\"e\":\"memoria\"}", -1);
    if (n_arg < 1 || !(sql = exjs_a_stringa(c, a[0]))) return errore(c, "sql mancante");
    if (!db_apri()) return errore(c, "il database non si apre");
    {
        /* ! THE TEXT OF THE SQL IS COPIED: binding a string parameter may
         * reuse exjs_a_stringa's buffer */
        static char *testo;
        size_t       n = strlen(sql);

        free(testo);
        testo = (char *)malloc(n + 1);
        if (!testo) return errore(c, "memoria");
        memcpy(testo, sql, n + 1);
        sql = testo;
    }
    if (sq_prepara(g_db, sql, -1, &st, &coda) != 0 || !st) return errore(c, sq_errore(g_db));
    for (i = 1; i < n_arg; i++) {
        int t = exjs_tipo(c, a[i]);

        if (t == EXJS_NUMERO || t == EXJS_BOOLEANO) sq_double(st, i, exjs_a_numero(c, a[i]));
        else if (t == EXJS_STRINGA) sq_testo(st, i, exjs_a_stringa(c, a[i]), -1, (void *)(long)-1);   /* SQLITE_TRANSIENT */
        else sq_null(st, i);
    }
    g_out_n = 0;
    o_metti("{\"r\":[", 6);
    nc = sq_colonne(st);
    while ((rc = sq_passo(st)) == 100) {            /* SQLITE_ROW */
        if (!prime && !o_metti(",", 1)) break;
        prime = 0;
        o_metti("[", 1);
        for (i = 0; i < nc; i++) {
            int t = sq_tipo(st, i);

            if (i) o_metti(",", 1);
            if (t == 1) { snprintf(num, sizeof(num), "%lld", sq_col_int(st, i)); o_metti(num, (unsigned int)strlen(num)); }
            else if (t == 2) { snprintf(num, sizeof(num), "%.17g", sq_col_double(st, i)); o_metti(num, (unsigned int)strlen(num)); }
            else if (t == 5) o_metti("null", 4);
            else if (!o_stringa(sq_col_testo(st, i))) break;
        }
        if (!o_metti("]", 1)) break;
    }
    if (rc != 101) {                                /* SQLITE_DONE */
        const char *m = rc == 100 ? "risultato troppo grande" : sq_errore(g_db);
        char        copia[256];

        snprintf(copia, sizeof(copia), "%s", m);
        sq_fine(st);
        return errore(c, copia);
    }
    sq_fine(st);
    snprintf(num, sizeof(num), "],\"n\":%d}", sq_cambi(g_db));
    o_metti(num, (unsigned int)strlen(num));
    g_out[g_out_n] = 0;
    return exjs_stringa(c, g_out, (int)g_out_n);
}

/* the page is going: its file closes (the next page may be of another origin) */
void idb_via(void)
{
    if (g_db && g_db_vista == g_vi) { sq_chiudi(g_db); g_db = 0; g_db_vista = 0; }
}

void idb_registra(ExJsCtx *js)
{
    ExJsErrore err;
    ExJsVal    r;

    if (!sqlite_pronta()) return;
    exjs_metti(js, exjs_globale(js), "__exos_sql", exjs_nativa(js, nat_sql, 0, "__exos_sql"));
    memset(&err, 0, sizeof(err));
    if (!exjs_esegui(js, IDB_JS, (unsigned int)(sizeof(IDB_JS) - 1), &r, &err))
        printf("exbrowser: indexedDB, riga %d: %s\n", err.riga, err.messaggio);
}
