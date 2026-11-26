/* =============================================================================
 * exwin/bin/exbrowser/browser_lavoratori.c — Web Workers (P11)
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * A worker is a JavaScript context of its own (exjs_apri: with QuickJS, a
 * runtime of its own), with the worker's globals (self, postMessage,
 * onmessage, importScripts, close) and no document. The prelude's Worker
 * class talks to four natives:
 *   __exos_lav_nuovo(url, codice)  -> id or -1; codice is the source of a
 *                                     blob: URL, else the script is fetched
 *   __exos_lav_manda(id, messaggio) a message for the worker (serialized)
 *   __exos_lav_giro(id)            -> the worker's messages and errors since
 *                                     the last call, as JSON; and the worker
 *                                     runs its timers and its inbox here
 *   __exos_lav_fine(id)            terminate()
 *
 * ! IN THE SAME THREAD, NOT IN ONE OF ITS OWN (debug/implementazione_
 * BROWSER002.txt 4.7 e asked for a pthread): the network, the clock and the
 * engine's hooks of the browser are not safe from a second thread, and what
 * pages ask of a worker is mostly that it be a separate world with messages
 * in between - which this is: no shared object, every message a copy. A
 * worker that computes for a long time slows the page, as it would on one
 * core. A thread per worker is a later step, when the hooks are.
 *
 * ! THE SCRIPT RUNS AT THE FIRST giro, after the constructor has returned:
 * the page sets onmessage after new Worker, and the first messages it posts
 * wait in the inbox until the worker's script has run, as the standard says.
 * ========================================================================== */
#include "browser_priv.h"

#define LAV_MAX     4
#define LAV_CODA    (512 * 1024)        /* each queue, in bytes of JSON */
#define LAV_CODICE  (2 * 1024 * 1024)

/* exbrowser.c */
int  lav_scarica(const char *url, char *buf, unsigned int max, unsigned int *n, char *finale, unsigned int fmax,
                 int da_dove_vuole);   /* 1: importScripts, any origin (P14) */
void lav_ganci(ExJsCtx *c);
unsigned int lav_arena(void);

typedef struct {
    int          usato, partito;
    ExJsCtx     *js;
    void        *mem;
    char         url[1024];
    char        *codice;              /* until it runs */
    char        *in, *out;            /* JSON lines, '\n' between messages */
    unsigned int in_n, out_n;
} Lav;

static Lav  g_lav[LAV_MAX];
static Lav *g_ora;                    /* the worker whose code is running */

static void coda_metti(char *q, unsigned int *n, const char *tipo, const char *s)
{
    unsigned int l = (unsigned int)strlen(s), t = (unsigned int)strlen(tipo);

    if (*n + l + t + 2 >= LAV_CODA) return;     /* full: dropped, as a dead port */
    memcpy(q + *n, tipo, t); *n += t;
    memcpy(q + *n, s, l); *n += l;
    q[(*n)++] = '\n';
    q[*n] = 0;
}

/* ------------------------------------------------------- inside the worker */
static ExJsVal nat_al_padre(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    const char *s;

    (void)questo; (void)dato;
    if (!g_ora || n_arg < 1 || !(s = exjs_a_stringa(c, a[0]))) return exjs_indefinito();
    if (!strcmp(s, "\x01" "close")) { coda_metti(g_ora->out, &g_ora->out_n, "C", ""); return exjs_indefinito(); }
    coda_metti(g_ora->out, &g_ora->out_n, "M", s);
    return exjs_indefinito();
}

static void errore_dal_lavoro(const char *m)
{
    char r[400];
    unsigned int i, k = 0;

    /* one line: the queue is line by line */
    for (i = 0; m[i] && k < sizeof(r) - 1; i++) r[k++] = (m[i] == '\n' || m[i] == '\r') ? ' ' : m[i];
    r[k] = 0;
    coda_metti(g_ora->out, &g_ora->out_n, "E", r);
}

static int esegui(Lav *w, const char *src, unsigned int n)
{
    ExJsErrore err;
    ExJsVal    r;
    Lav       *era = g_ora;
    int        ok;

    g_ora = w;
    memset(&err, 0, sizeof(err));
    ok = exjs_esegui(w->js, src, n, &r, &err);
    if (!ok) errore_dal_lavoro(err.messaggio);
    g_ora = era;
    return ok;
}

/* importScripts(url): fetched and run now, in the worker */
static ExJsVal nat_importa(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    static char *buf;
    const char  *u;
    char         fin[1024];
    unsigned int n;
    Lav         *w = g_ora;
    int          i;

    (void)questo; (void)dato;
    if (!w) return exjs_indefinito();
    if (!buf) buf = (char *)malloc(LAV_CODICE + 1);
    if (!buf) return exjs_indefinito();
    for (i = 0; i < n_arg; i++) {
        if (!(u = exjs_a_stringa(c, a[i]))) continue;
        if (!lav_scarica(u, buf, LAV_CODICE, &n, fin, sizeof(fin), 1)) { errore_dal_lavoro("importScripts: non si carica"); continue; }
        buf[n] = 0;
        esegui(w, buf, n);
    }
    return exjs_indefinito();
}

static ExJsVal nat_lav_log(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    const char *s;

    (void)questo; (void)dato;
    if (n_arg > 0 && (s = exjs_a_stringa(c, a[0]))) printf("exbrowser: worker: %s\n", s);
    return exjs_indefinito();
}

static const char PRELUDIO_LAV[] =
"(function (G) {\n"
"  G.self = G; G.globalThis = G;\n"
"  var A = {};\n"
"  G.addEventListener = function (n, f) { (A[n] = A[n] || []).push(f); };\n"
"  G.removeEventListener = function (n, f) { var l = A[n] || [], i = l.indexOf(f); if (i >= 0) l.splice(i, 1); };\n"
"  G.postMessage = function (m) { __exos_al_padre(__exos_ser(m)); };\n"
"  G.close = function () { __exos_al_padre('\\u0001close'); };\n"
"  G.importScripts = function () { __exos_importa.apply(null, arguments); };\n"
"  G.__exos_consegna = function (s) {\n"
"    var e = { type: 'message', data: __exos_deser(s), target: G, currentTarget: G, ports: [] }, l = (A.message || []).slice(0), i;\n"
"    if (typeof G.onmessage === 'function') G.onmessage(e);\n"
"    for (i = 0; i < l.length; i++) l[i].call(G, e);\n"
"  };\n"
"  G.navigator = { language: 'it', languages: ['it'], hardwareConcurrency: 1, onLine: true };\n"
"  if (typeof G.console === 'undefined' || !G.console.log) G.console = {};\n"
"  ['log', 'info', 'warn', 'error', 'debug'].forEach(function (k) { G.console[k] = function () { __exos_lav_log(Array.prototype.join.call(arguments, ' ')); }; });\n"
"  if (typeof G.queueMicrotask !== 'function' && typeof Promise !== 'undefined') G.queueMicrotask = function (f) { Promise.resolve().then(f); };\n"
"})(this);\n";

/* the copy of a message (structured clone, as far as JSON goes): the same
 * text in the page and in the worker */
const char PRELUDIO_CLONE[] =
"(function (G) {\n"
"  var has = function (o, k) { return Object.prototype.hasOwnProperty.call(o, k); };\n"
"  var enc = function (v) {\n"
"    var o, k;\n"
"    if (v === undefined) return { __u: 1 };\n"
"    if (typeof v === 'number' && !isFinite(v)) return { __n: String(v) };\n"
"    if (typeof v === 'bigint') return { __b: String(v) };\n"
"    if (v === null || typeof v !== 'object') { if (typeof v === 'function') throw new Error('DataCloneError: una funzione'); return v; }\n"
"    if (v instanceof Date) return { __d: v.getTime() };\n"
"    if (v instanceof RegExp) return { __r: v.source, f: v.flags };\n"
"    if (v instanceof ArrayBuffer) return { __ab: Array.prototype.slice.call(new Uint8Array(v)) };\n"
"    if (ArrayBuffer.isView(v)) return { __ta: v.constructor.name, d: Array.prototype.slice.call(v) };\n"
"    if (typeof Map !== 'undefined' && v instanceof Map) { o = []; v.forEach(function (x, y) { o.push([enc(y), enc(x)]); }); return { __m: o }; }\n"
"    if (typeof Set !== 'undefined' && v instanceof Set) { o = []; v.forEach(function (x) { o.push(enc(x)); }); return { __s: o }; }\n"
"    if (v instanceof Error) return { __e: v.name, m: v.message };\n"
"    if (Array.isArray(v)) return v.map(enc);\n"
"    o = {}; for (k in v) if (has(v, k)) o[k] = enc(v[k]);\n"
"    return o;\n"
"  };\n"
"  var dec = function (v) {\n"
"    var o, k;\n"
"    if (v === null || typeof v !== 'object') return v;\n"
"    if (Array.isArray(v)) return v.map(dec);\n"
"    if (has(v, '__u')) return undefined;\n"
"    if (has(v, '__n')) return Number(v.__n);\n"
"    if (has(v, '__b')) return BigInt(v.__b);\n"
"    if (has(v, '__d')) return new Date(v.__d);\n"
"    if (has(v, '__r')) return new RegExp(v.__r, v.f);\n"
"    if (has(v, '__ab')) return new Uint8Array(v.__ab).buffer;\n"
"    if (has(v, '__ta')) return new (G[v.__ta] || Uint8Array)(v.d);\n"
"    if (has(v, '__m')) { o = new Map(); v.__m.forEach(function (x) { o.set(dec(x[0]), dec(x[1])); }); return o; }\n"
"    if (has(v, '__s')) { o = new Set(); v.__s.forEach(function (x) { o.add(dec(x)); }); return o; }\n"
"    if (has(v, '__e')) { o = new Error(v.m); o.name = v.__e; return o; }\n"
"    o = {}; for (k in v) if (has(v, k)) o[k] = dec(v[k]);\n"
"    return o;\n"
"  };\n"
"  G.__exos_ser = function (v) { return JSON.stringify(enc(v)); };\n"
"  G.__exos_deser = function (s) { return dec(JSON.parse(s)); };\n"
"})(this);\n";

static void lav_via(Lav *w)
{
    if (w->js) exjs_chiudi(w->js);
    free(w->mem); free(w->codice); free(w->in); free(w->out);
    memset(w, 0, sizeof(*w));
}

static int parti(Lav *w)
{
    unsigned int quanto, arena = lav_arena();
    char         base[1100];

    w->partito = 1;
    quanto = exjs_quanto_serve(2000u, arena);
    w->mem = malloc(quanto);
    if (!w->mem) return 0;
    w->js = exjs_apri(w->mem, quanto, 2000u, arena);
    if (!w->js) return 0;
    lav_ganci(w->js);
    exjs_metti(w->js, exjs_globale(w->js), "__exos_al_padre", exjs_nativa(w->js, nat_al_padre, 0, "__exos_al_padre"));
    exjs_metti(w->js, exjs_globale(w->js), "__exos_importa", exjs_nativa(w->js, nat_importa, 0, "__exos_importa"));
    exjs_metti(w->js, exjs_globale(w->js), "__exos_lav_log", exjs_nativa(w->js, nat_lav_log, 0, "__exos_lav_log"));
    esegui(w, PRELUDIO_CLONE, (unsigned int)strlen(PRELUDIO_CLONE));
    esegui(w, PRELUDIO_LAV, (unsigned int)strlen(PRELUDIO_LAV));
    snprintf(base, sizeof(base), "self.navigator.userAgent = '%s';", agente_browser());
    esegui(w, base, (unsigned int)strlen(base));
    {
        /* location: the worker's own address */
        char *q = base;
        unsigned int i;

        q += snprintf(base, sizeof(base) - 4, "self.location = { href: '");
        for (i = 0; w->url[i] && q < base + sizeof(base) - 8; i++)
            if (w->url[i] != '\'' && w->url[i] != '\\' && w->url[i] != '\n') *q++ = w->url[i];
        memcpy(q, "' };", 5);
        esegui(w, base, (unsigned int)strlen(base));
    }
    if (w->codice) {
        esegui(w, w->codice, (unsigned int)strlen(w->codice));
        free(w->codice); w->codice = 0;
    }
    return 1;
}

/* ------------------------------------------------------------- natives */
static ExJsVal nat_nuovo(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    const char  *u, *cod;
    unsigned int n;
    int          i;

    (void)questo; (void)dato;
    if (n_arg < 1 || !(u = exjs_a_stringa(c, a[0]))) return exjs_numero(c, -1);
    for (i = 0; i < LAV_MAX && g_lav[i].usato; i++) {}
    if (i == LAV_MAX) return exjs_numero(c, -1);
    memset(&g_lav[i], 0, sizeof(g_lav[i]));
    snprintf(g_lav[i].url, sizeof(g_lav[i].url), "%s", u);
    g_lav[i].codice = (char *)malloc(LAV_CODICE + 1);
    g_lav[i].in = (char *)malloc(LAV_CODA);
    g_lav[i].out = (char *)malloc(LAV_CODA);
    if (!g_lav[i].codice || !g_lav[i].in || !g_lav[i].out) { lav_via(&g_lav[i]); return exjs_numero(c, -1); }
    g_lav[i].usato = 1;
    cod = n_arg > 1 ? exjs_a_stringa(c, a[1]) : 0;
    if (cod && cod[0]) {
        snprintf(g_lav[i].codice, LAV_CODICE + 1, "%s", cod);
    } else {
        /* ! THE SCRIPT OF THE PAGE'S OWN ORIGIN only (lav_scarica checks) */
        char fin[1024];

        if (!lav_scarica(u, g_lav[i].codice, LAV_CODICE, &n, fin, sizeof(fin), 0)) {
            lav_via(&g_lav[i]);
            return exjs_numero(c, -1);
        }
        g_lav[i].codice[n] = 0;
    }
    return exjs_numero(c, i);
}

static Lav *lav_di(ExJsCtx *c, ExJsVal v)
{
    int i = (int)exjs_a_numero(c, v);

    return (i >= 0 && i < LAV_MAX && g_lav[i].usato) ? &g_lav[i] : 0;
}

static ExJsVal nat_manda(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    Lav        *w;
    const char *s;

    (void)questo; (void)dato;
    if (n_arg < 2 || !(w = lav_di(c, a[0])) || !(s = exjs_a_stringa(c, a[1]))) return exjs_booleano(0);
    coda_metti(w->in, &w->in_n, "", s);
    return exjs_booleano(1);
}

static ExJsVal nat_giro(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    static char *risp;
    Lav         *w;
    unsigned int k = 0, i, p;

    (void)questo; (void)dato;
    if (!risp) risp = (char *)malloc(2 * LAV_CODA + 16);
    if (!risp || n_arg < 1 || !(w = lav_di(c, a[0]))) return exjs_stringa(c, "", -1);
    if (!w->partito && !parti(w)) {
        lav_via(w);
        return exjs_stringa(c, "Eil worker non parte\n", -1);
    }
    /* the inbox: each message delivered as a task of the worker */
    if (w->in_n) {
        char *coda = (char *)malloc(w->in_n + 1);

        if (coda) {
            unsigned int n = w->in_n;

            memcpy(coda, w->in, n + 1);
            w->in_n = 0;
            for (p = 0, i = 0; i < n; i++)
                if (coda[i] == '\n') {
                    /* ! THE MESSAGE GOES AS A STRING LITERAL, never as code:
                     * the worker parses it (JSON.parse, __exos_deser) */
                    static const char T[] = "__exos_consegna('";
                    char *src = (char *)malloc((i - p) * 6 + sizeof(T) + 4);

                    if (src) {
                        unsigned int q = sizeof(T) - 1, j;

                        memcpy(src, T, q);
                        for (j = p; j < i; j++) {
                            unsigned char ch = (unsigned char)coda[j];

                            if (ch == '\\' || ch == '\'') { src[q++] = '\\'; src[q++] = (char)ch; }
                            else if (ch < 0x20) q += (unsigned int)sprintf(src + q, "\\x%02x", ch);
                            else if (ch == 0xE2 && j + 2 < i && (unsigned char)coda[j + 1] == 0x80 &&
                                     ((unsigned char)coda[j + 2] == 0xA8 || (unsigned char)coda[j + 2] == 0xA9)) {
                                q += (unsigned int)sprintf(src + q, "\\u%s", (unsigned char)coda[j + 2] == 0xA8 ? "2028" : "2029");
                                j += 2;
                            } else src[q++] = (char)ch;
                        }
                        memcpy(src + q, "');", 4);
                        esegui(w, src, q + 3);
                        free(src);
                    }
                    p = i + 1;
                }
            free(coda);
        }
    }
    {
        Lav *era = g_ora;

        g_ora = w;
        exjs_pompa(w->js, uptime_ms());   /* its timers and microtasks */
        g_ora = era;
    }
    memcpy(risp, w->out, w->out_n);
    k = w->out_n;
    w->out_n = 0;
    risp[k] = 0;
    return exjs_stringa(c, risp, (int)k);
}

static ExJsVal nat_fine(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    Lav *w;

    (void)questo; (void)dato;
    if (n_arg >= 1 && (w = lav_di(c, a[0]))) lav_via(w);
    return exjs_indefinito();
}

void lav_tutti_via(void)
{
    int i;

    for (i = 0; i < LAV_MAX; i++) if (g_lav[i].usato) lav_via(&g_lav[i]);
}

void lav_registra(ExJsCtx *js)
{
    ExJsErrore err;
    ExJsVal    r;

    exjs_metti(js, exjs_globale(js), "__exos_lav_nuovo", exjs_nativa(js, nat_nuovo, 0, "__exos_lav_nuovo"));
    exjs_metti(js, exjs_globale(js), "__exos_lav_manda", exjs_nativa(js, nat_manda, 0, "__exos_lav_manda"));
    exjs_metti(js, exjs_globale(js), "__exos_lav_giro", exjs_nativa(js, nat_giro, 0, "__exos_lav_giro"));
    exjs_metti(js, exjs_globale(js), "__exos_lav_fine", exjs_nativa(js, nat_fine, 0, "__exos_lav_fine"));
    memset(&err, 0, sizeof(err));
    exjs_esegui(js, PRELUDIO_CLONE, (unsigned int)strlen(PRELUDIO_CLONE), &r, &err);
}
