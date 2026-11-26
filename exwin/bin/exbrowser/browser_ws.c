/* =============================================================================
 * exwin/bin/exbrowser/browser_ws.c — WebSocket (RFC 6455) for the page (P11)
 *
 * Copyright (C) 2026 Graziano Falcone
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The prelude's WebSocket class talks to four natives:
 *   __exos_ws_apri(url, protocolli, origine) -> id (>= 0) or -1
 *   __exos_ws_giro(id)     -> the events since the last call, as JSON:
 *                             [["open","proto"],["text","..."],["bin","hex"],
 *                              ["close",code,"reason",pulita],["error"]]
 *   __exos_ws_manda(id, testo, binario)  binario = 1: testo is hex
 *   __exos_ws_chiudi(id, codice, motivo)
 * The prelude calls giro on a timer while a socket is alive: the events reach
 * the page as tasks of the event loop, as the standard wants.
 *
 * ! THE CONNECTION IS MADE AT THE FIRST giro, not in apri: the constructor
 * must return before anything happens (a page sets onopen after new
 * WebSocket), and the handshake - TCP, TLS, the HTTP upgrade - blocks the
 * browser as a fetch does. The transport is exhttp's (exhttp_tcp,
 * exhttp_tls_sopra); the framing is here, small enough not to need a library.
 *
 * ! THE CLIENT MASKS EVERY FRAME (5.3) with four random bytes, a server
 * closes on an unmasked one; the server's frames are never masked. Pings
 * get their pong here, fragments are put together here, and a message over
 * WS_MSG_MAX closes the socket with 1009, as browsers do with their own cap.
 * ========================================================================== */
#include "browser_priv.h"
#include "exhttp.h"

void sha1(const void *dati, unsigned int len, unsigned char out[20]);   /* lib/excrypt */
void ws_biscotti(const char *host, const char *percorso, int cifrata, char *out, unsigned int max);

#define WS_MAX      8
#define WS_MSG_MAX  (1024 * 1024)
#define WS_EVENTI   (2 * WS_MSG_MAX + 4096)

typedef struct {
    int             usato, stato;      /* 0 to connect, 1 open, 2 closing (we sent close), 3 closed */
    char            url[EXHTTP_URL_MAX], prot[256], origine[256];
    ExHttpTrasporto tcp, t;
    int             tls;
    unsigned char  *in;                /* what came and is not yet a frame */
    unsigned int    in_n;
    unsigned char  *msg;               /* a message in fragments */
    unsigned int    msg_n;
    int             msg_tipo;          /* 1 text, 2 binary */
} Ws;

static Ws    g_ws[WS_MAX];
static char *g_ev;
static unsigned int g_ev_n;

/* ------------------------------------------------------------- events */
static void ev_metti(const char *s)
{
    unsigned int n = (unsigned int)strlen(s);

    if (g_ev_n + n + 2 >= WS_EVENTI) return;
    memcpy(g_ev + g_ev_n, s, n);
    g_ev_n += n;
}

static void ev_stringa(const unsigned char *s, unsigned int n)
{
    unsigned int i;
    char         e[8];

    ev_metti("\"");
    for (i = 0; i < n && g_ev_n + 8 < WS_EVENTI; i++) {
        unsigned char c = s[i];

        if (c == '"' || c == '\\') { g_ev[g_ev_n++] = '\\'; g_ev[g_ev_n++] = (char)c; }
        else if (c < 0x20) { snprintf(e, sizeof(e), "\\u%04x", c); ev_metti(e); }
        else g_ev[g_ev_n++] = (char)c;
    }
    ev_metti("\"");
}

static void ev_inizio(void) { if (g_ev_n > 1) ev_metti(","); ev_metti("["); }

/* ------------------------------------------------------------- base64 */
static void b64(const unsigned char *s, int n, char *o)
{
    static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i, k = 0;

    for (i = 0; i < n; i += 3) {
        unsigned int v = (unsigned int)s[i] << 16 | (i + 1 < n ? (unsigned int)s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);

        o[k++] = A[v >> 18 & 63];
        o[k++] = A[v >> 12 & 63];
        o[k++] = i + 1 < n ? A[v >> 6 & 63] : '=';
        o[k++] = i + 2 < n ? A[v & 63] : '=';
    }
    o[k] = 0;
}

/* ------------------------------------------------------------- frames */
static int scrivi_tutto(Ws *w, const unsigned char *b, unsigned int n)
{
    while (n) {
        int r = w->t.scrivi(w->t.stato, b, n);

        if (r <= 0) return 0;
        b += r; n -= (unsigned int)r;
    }
    return 1;
}

static int manda_frame(Ws *w, int op, const unsigned char *d, unsigned int n)
{
    unsigned char t[14], m[4], *b;
    unsigned int  k = 0, i;
    int           ok;

    if (getentropy(m, 4) != 0) return 0;
    t[k++] = (unsigned char)(0x80 | op);
    if (n < 126) t[k++] = (unsigned char)(0x80 | n);
    else if (n < 65536) { t[k++] = 0x80 | 126; t[k++] = (unsigned char)(n >> 8); t[k++] = (unsigned char)n; }
    else { t[k++] = 0x80 | 127; for (i = 0; i < 4; i++) t[k++] = 0; for (i = 0; i < 4; i++) t[k++] = (unsigned char)(n >> (24 - 8 * i)); }
    memcpy(t + k, m, 4); k += 4;
    b = (unsigned char *)malloc(k + n + 1);
    if (!b) return 0;
    memcpy(b, t, k);
    for (i = 0; i < n; i++) b[k + i] = d[i] ^ m[i & 3];
    ok = scrivi_tutto(w, b, k + n);
    free(b);
    return ok;
}

static void chiudi_tutto(Ws *w, int codice, const char *motivo, int pulita)
{
    char e[32];

    if (w->stato != 3) {
        ev_inizio();
        snprintf(e, sizeof(e), "\"close\",%d,", codice);
        ev_metti(e);
        ev_stringa((const unsigned char *)motivo, (unsigned int)strlen(motivo));
        ev_metti(pulita ? ",true]" : ",false]");
    }
    if (w->t.chiudi && w->stato != 0) w->t.chiudi(w->t.stato);
    w->stato = 3;
    free(w->in); w->in = 0; w->in_n = 0;
    free(w->msg); w->msg = 0; w->msg_n = 0;
    w->usato = 0;
}

/* the frames that are whole in w->in */
static void leggi_frame(Ws *w)
{
    for (;;) {
        unsigned char *b = w->in;
        unsigned int   k = 2, n, i;
        int            fin, op;

        if (w->in_n < 2) return;
        fin = b[0] & 0x80; op = b[0] & 15;
        n = b[1] & 127;
        if (b[1] & 0x80) { chiudi_tutto(w, 1002, "frame mascherato dal server", 0); return; }
        if (n == 126) { if (w->in_n < 4) return; n = (unsigned int)b[2] << 8 | b[3]; k = 4; }
        else if (n == 127) {
            if (w->in_n < 10) return;
            if (b[2] | b[3] | b[4] | b[5]) { chiudi_tutto(w, 1009, "messaggio troppo grande", 0); return; }
            n = (unsigned int)b[6] << 24 | (unsigned int)b[7] << 16 | (unsigned int)b[8] << 8 | b[9]; k = 10;
        }
        if (n > WS_MSG_MAX) { chiudi_tutto(w, 1009, "messaggio troppo grande", 0); return; }
        if (w->in_n < k + n) return;
        b += k;
        if (op == 9) manda_frame(w, 10, b, n);                  /* ping: pong */
        else if (op == 8) {                                      /* close */
            int  cod = n >= 2 ? (b[0] << 8 | b[1]) : 1005;
            char mot[128];
            unsigned int m = n > 2 ? n - 2 : 0;

            if (m > sizeof(mot) - 1) m = sizeof(mot) - 1;
            memcpy(mot, b + 2, m); mot[m] = 0;
            if (w->stato == 1) manda_frame(w, 8, b, n >= 2 ? 2 : 0);   /* the answer */
            chiudi_tutto(w, cod, mot, 1);
            return;
        } else if (op == 1 || op == 2 || op == 0) {
            if (op) { w->msg_n = 0; w->msg_tipo = op; }
            if (w->msg_n + n > WS_MSG_MAX) { chiudi_tutto(w, 1009, "messaggio troppo grande", 0); return; }
            if (!w->msg) w->msg = (unsigned char *)malloc(WS_MSG_MAX);
            if (!w->msg) { chiudi_tutto(w, 1011, "memoria", 0); return; }
            memcpy(w->msg + w->msg_n, b, n); w->msg_n += n;
            if (fin) {
                ev_inizio();
                if (w->msg_tipo == 1) { ev_metti("\"text\","); ev_stringa(w->msg, w->msg_n); }
                else {
                    static const char C[] = "0123456789abcdef";
                    ev_metti("\"bin\",\"");
                    for (i = 0; i < w->msg_n && g_ev_n + 4 < WS_EVENTI; i++) { g_ev[g_ev_n++] = C[w->msg[i] >> 4]; g_ev[g_ev_n++] = C[w->msg[i] & 15]; }
                    ev_metti("\"");
                }
                ev_metti("]");
                w->msg_n = 0;
            }
        }
        memmove(w->in, w->in + k + n, w->in_n - k - n);
        w->in_n -= k + n;
    }
}

/* ------------------------------------------------------ the handshake */
static int connetti(Ws *w)
{
    char           host[256], perc[EXHTTP_URL_MAX], chiave[32], richiesta[EXHTTP_URL_MAX + 6000], biscotti[4096];
    char           atteso[32], err[128], extra[4400];
    unsigned char  r16[16], h[20], *risp;
    const char    *u = w->url, *p;
    unsigned int   porta, n = 0, i;
    int            k;

    w->tls = !strncmp(u, "wss://", 6);
    u += w->tls ? 6 : 5;
    for (i = 0; *u && *u != '/' && *u != '?' && *u != ':' && i < sizeof(host) - 1; u++) host[i++] = *u;
    host[i] = 0;
    porta = w->tls ? 443 : 80;
    if (*u == ':') { porta = 0; for (u++; *u >= '0' && *u <= '9'; u++) porta = porta * 10 + (unsigned int)(*u - '0'); }
    snprintf(perc, sizeof(perc), "%s", *u ? u : "/");
    if (perc[0] == '?') { memmove(perc + 1, perc, strlen(perc) + 1); perc[0] = '/'; }
    if (!exhttp_tcp(&w->tcp, host, porta)) return 0;
    if (w->tls) {
        if (!exhttp_tls_sopra(&w->t, &w->tcp, host, err, sizeof(err))) { w->tcp.chiudi(w->tcp.stato); return 0; }
    } else w->t = w->tcp;
    if (getentropy(r16, 16) != 0) { w->t.chiudi(w->t.stato); return 0; }
    b64(r16, 16, chiave);
    biscotti[0] = 0;
    ws_biscotti(host, perc, w->tls, biscotti, sizeof(biscotti));
    extra[0] = 0;
    if (w->prot[0]) snprintf(extra, sizeof(extra), "Sec-WebSocket-Protocol: %s\r\n", w->prot);
    if (biscotti[0]) snprintf(extra + strlen(extra), sizeof(extra) - strlen(extra), "Cookie: %s\r\n", biscotti);
    k = snprintf(richiesta, sizeof(richiesta),
                 "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                 "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nOrigin: %s\r\n%s\r\n",
                 perc, host, chiave, w->origine, extra);
    if (k <= 0 || k >= (int)sizeof(richiesta) || !scrivi_tutto(w, (const unsigned char *)richiesta, (unsigned int)k)) {
        w->t.chiudi(w->t.stato); return 0;
    }
    /* the answer's head; what comes after it is already frames */
    risp = (unsigned char *)malloc(16384);
    if (!risp) { w->t.chiudi(w->t.stato); return 0; }
    for (;;) {
        int r = w->t.leggi(w->t.stato, risp + n, 16383 - n, 10000);

        if (r <= 0) { free(risp); w->t.chiudi(w->t.stato); return 0; }
        n += (unsigned int)r;
        risp[n] = 0;
        if ((p = strstr((char *)risp, "\r\n\r\n")) != 0) break;
        if (n >= 16383) { free(risp); w->t.chiudi(w->t.stato); return 0; }
    }
    /* 101 and the right Sec-WebSocket-Accept (4.1), or it is not ours */
    {
        char base[64];

        snprintf(base, sizeof(base), "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", chiave);
        sha1(base, (unsigned int)strlen(base), h);
        b64(h, 20, atteso);
    }
    if (strncmp((char *)risp, "HTTP/1.1 101", 12) || !strstr((char *)risp, atteso)) {
        free(risp); w->t.chiudi(w->t.stato); return 0;
    }
    {
        const char *pr = strstr((char *)risp, "Sec-WebSocket-Protocol:");
        char        scelto[128];

        if (!pr) pr = strstr((char *)risp, "sec-websocket-protocol:");
        scelto[0] = 0;
        if (pr && pr < p) {
            for (pr += 23; *pr == ' '; pr++) {}
            for (i = 0; pr[i] && pr[i] != '\r' && i < sizeof(scelto) - 1; i++) scelto[i] = pr[i];
            scelto[i] = 0;
        }
        ev_inizio(); ev_metti("\"open\","); ev_stringa((const unsigned char *)scelto, (unsigned int)strlen(scelto)); ev_metti("]");
    }
    w->in = (unsigned char *)malloc(WS_MSG_MAX + 16);
    if (!w->in) { free(risp); w->t.chiudi(w->t.stato); return 0; }
    w->in_n = n - (unsigned int)((const unsigned char *)p + 4 - risp);
    memcpy(w->in, p + 4, w->in_n);
    free(risp);
    w->stato = 1;
    leggi_frame(w);         /* frames that came with the head */
    return 1;
}

/* ------------------------------------------------------------- natives */
static ExJsVal nat_apri(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    const char *s;
    int         i;

    (void)questo; (void)dato;
    if (n_arg < 1 || !(s = exjs_a_stringa(c, a[0]))) return exjs_numero(c, -1);
    if (strncmp(s, "ws://", 5) && strncmp(s, "wss://", 6)) return exjs_numero(c, -1);
    for (i = 0; i < WS_MAX && g_ws[i].usato; i++) {}
    if (i == WS_MAX) return exjs_numero(c, -1);
    memset(&g_ws[i], 0, sizeof(g_ws[i]));
    g_ws[i].usato = 1;
    snprintf(g_ws[i].url, sizeof(g_ws[i].url), "%s", s);
    if (n_arg > 1 && (s = exjs_a_stringa(c, a[1]))) snprintf(g_ws[i].prot, sizeof(g_ws[i].prot), "%s", s);
    if (n_arg > 2 && (s = exjs_a_stringa(c, a[2]))) snprintf(g_ws[i].origine, sizeof(g_ws[i].origine), "%s", s);
    return exjs_numero(c, i);
}

static Ws *ws_di(ExJsCtx *c, ExJsVal v)
{
    int i = (int)exjs_a_numero(c, v);

    return (i >= 0 && i < WS_MAX && g_ws[i].usato) ? &g_ws[i] : 0;
}

static ExJsVal nat_giro(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    Ws *w;

    (void)questo; (void)dato;
    if (!g_ev) g_ev = (char *)malloc(WS_EVENTI);
    if (!g_ev || n_arg < 1) return exjs_stringa(c, "[]", -1);
    g_ev_n = 0;
    ev_metti("[");
    w = ws_di(c, a[0]);
    if (w && w->stato == 0 && !connetti(w)) {
        ev_inizio(); ev_metti("\"error\"]");
        chiudi_tutto(w, 1006, "", 0);
        w = 0;
    }
    while (w && w->usato && w->stato >= 1 && w->stato <= 2) {
        int q = w->t.quanti ? w->t.quanti(w->t.stato) : 1, r;

        if (q == 0) break;
        if (q < 0) { chiudi_tutto(w, 1006, "", 0); break; }
        if (w->in_n >= WS_MSG_MAX + 16) { chiudi_tutto(w, 1009, "messaggio troppo grande", 0); break; }
        r = w->t.leggi(w->t.stato, w->in + w->in_n, WS_MSG_MAX + 16 - w->in_n, 1);
        if (r == 0) { chiudi_tutto(w, 1006, "", 0); break; }
        if (r < 0) break;
        w->in_n += (unsigned int)r;
        leggi_frame(w);
    }
    ev_metti("]");
    g_ev[g_ev_n] = 0;
    return exjs_stringa(c, g_ev, (int)g_ev_n);
}

static ExJsVal nat_manda(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    Ws         *w;
    const char *s;
    int         bin;

    (void)questo; (void)dato;
    if (n_arg < 2 || !(w = ws_di(c, a[0])) || w->stato != 1 || !(s = exjs_a_stringa(c, a[1]))) return exjs_booleano(0);
    bin = n_arg > 2 && exjs_a_numero(c, a[2]) != 0;
    if (!bin) return exjs_booleano(manda_frame(w, 1, (const unsigned char *)s, (unsigned int)strlen(s)));
    {
        unsigned int   n = (unsigned int)strlen(s) / 2, i;
        unsigned char *b = (unsigned char *)malloc(n + 1);
        int            ok;

        if (!b) return exjs_booleano(0);
        for (i = 0; i < n; i++) {
            int h = s[i * 2], l = s[i * 2 + 1];

            h = h <= '9' ? h - '0' : h - 'a' + 10;
            l = l <= '9' ? l - '0' : l - 'a' + 10;
            b[i] = (unsigned char)(h << 4 | l);
        }
        ok = manda_frame(w, 2, b, n);
        free(b);
        return exjs_booleano(ok);
    }
}

static ExJsVal nat_chiudi(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    Ws           *w;
    unsigned char b[125];
    int           cod = 1000;
    unsigned int  n = 2;
    const char   *m;

    (void)questo; (void)dato;
    if (n_arg < 1 || !(w = ws_di(c, a[0]))) return exjs_booleano(0);
    if (w->stato == 0) {                     /* never connected: just gone */
        w->usato = 0; w->stato = 3;
        return exjs_booleano(1);
    }
    if (w->stato != 1) return exjs_booleano(0);
    if (n_arg > 1 && exjs_a_numero(c, a[1]) > 0) cod = (int)exjs_a_numero(c, a[1]);
    b[0] = (unsigned char)(cod >> 8); b[1] = (unsigned char)cod;
    if (n_arg > 2 && (m = exjs_a_stringa(c, a[2]))) {
        unsigned int l = (unsigned int)strlen(m);

        if (l > 123) l = 123;
        memcpy(b + 2, m, l); n += l;
    }
    manda_frame(w, 8, b, n);
    w->stato = 2;           /* the server's close comes in giro */
    return exjs_booleano(1);
}

/* the page is going: every socket with it, without events */
void ws_tutti_via(void)
{
    int i;

    for (i = 0; i < WS_MAX; i++)
        if (g_ws[i].usato) {
            if (g_ws[i].stato == 1 || g_ws[i].stato == 2) g_ws[i].t.chiudi(g_ws[i].t.stato);
            free(g_ws[i].in); free(g_ws[i].msg);
            memset(&g_ws[i], 0, sizeof(g_ws[i]));
        }
}

void ws_registra(ExJsCtx *js)
{
    exjs_metti(js, exjs_globale(js), "__exos_ws_apri", exjs_nativa(js, nat_apri, 0, "__exos_ws_apri"));
    exjs_metti(js, exjs_globale(js), "__exos_ws_giro", exjs_nativa(js, nat_giro, 0, "__exos_ws_giro"));
    exjs_metti(js, exjs_globale(js), "__exos_ws_manda", exjs_nativa(js, nat_manda, 0, "__exos_ws_manda"));
    exjs_metti(js, exjs_globale(js), "__exos_ws_chiudi", exjs_nativa(js, nat_chiudi, 0, "__exos_ws_chiudi"));
}
