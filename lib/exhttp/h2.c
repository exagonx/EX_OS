/* =============================================================================
 * lib/exhttp/h2.c
 * EX-OS — Extensible Operating System
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 * HTTP/2 over nghttp2, opened with dlopen (what it is for: h2.h).
 *
 * ! nghttp2's TYPES ARE WRITTEN HERE, NOT INCLUDED, as zlib's in srotola.c:
 * exhttp.so is built with the project's headers, and five structures of
 * nghttp2.h (1.71.0) are used: nghttp2_nv, nghttp2_settings_entry, the frame
 * header at the start of every nghttp2_frame, and two pointers. The API is
 * the "2" one (mem_send2, mem_recv2, submit_request2), nghttp2_ssize being
 * ptrdiff_t.
 * ============================================================================= */
#include "libc.h"
#include "h2.h"

typedef ptrdiff_t ng_ssize;

typedef struct {
    unsigned char *name;
    unsigned char *value;
    size_t         namelen;
    size_t         valuelen;
    unsigned char  flags;
} NgNv;

typedef struct { int id; unsigned int value; } NgSettings;

typedef struct {                /* nghttp2_frame_hd, first in every frame */
    size_t        length;
    int           stream_id;
    unsigned char type;
    unsigned char flags;
    unsigned char reserved;
} NgTesta;

#define NG_HEADERS      1
#define NG_GOAWAY       7
#define NG_CANCEL       8
#define NG_NV_NO_COPY   0       /* nghttp2 copies: the strings are ours */

#define NG_SETTINGS_ENABLE_PUSH            2
#define NG_SETTINGS_MAX_CONCURRENT_STREAMS 3
#define NG_SETTINGS_INITIAL_WINDOW_SIZE    4

typedef int (*NgSuTesta)(void *, const void *, const unsigned char *, size_t,
                         const unsigned char *, size_t, unsigned char, void *);
typedef int (*NgSuDati)(void *, unsigned char, int, const unsigned char *, size_t, void *);
typedef int (*NgSuChiuso)(void *, int, unsigned int, void *);
typedef int (*NgSuFrame)(void *, const void *, void *);

static int      (*ng_cb_new)(void **);
static void     (*ng_cb_del)(void *);
static void     (*ng_cb_testa)(void *, NgSuTesta);
static void     (*ng_cb_dati)(void *, NgSuDati);
static void     (*ng_cb_chiuso)(void *, NgSuChiuso);
static void     (*ng_cb_frame)(void *, NgSuFrame);
static int      (*ng_client_new)(void **, const void *, void *);
static void     (*ng_session_del)(void *);
static int      (*ng_settings)(void *, unsigned char, const NgSettings *, size_t);
static int      (*ng_request)(void *, const void *, const NgNv *, size_t, const void *, void *);
static ng_ssize (*ng_recv)(void *, const unsigned char *, size_t);
static ng_ssize (*ng_send)(void *, const unsigned char **);
static int      (*ng_rst)(void *, unsigned char, int, unsigned int);
static void    *(*ng_get_ud)(void *, int);
static int      (*ng_set_ud)(void *, int, void *);

static int g_cercata = 0, g_ok = 0;

static void *simbolo(void *h, const char *nome, int *ok)
{
    void *p = h ? dlsym(h, nome) : 0;

    if (!p) *ok = 0;
    return p;
}

int h2_disponibile(void)
{
    void *h;
    int   ok;

    if (g_cercata) return g_ok;
    g_cercata = 1;
    h = dlopen("libnghttp2.so.14", RTLD_NOW);
    ok = h != 0;
    *(void **)&ng_cb_new      = simbolo(h, "nghttp2_session_callbacks_new", &ok);
    *(void **)&ng_cb_del      = simbolo(h, "nghttp2_session_callbacks_del", &ok);
    *(void **)&ng_cb_testa    = simbolo(h, "nghttp2_session_callbacks_set_on_header_callback", &ok);
    *(void **)&ng_cb_dati     = simbolo(h, "nghttp2_session_callbacks_set_on_data_chunk_recv_callback", &ok);
    *(void **)&ng_cb_chiuso   = simbolo(h, "nghttp2_session_callbacks_set_on_stream_close_callback", &ok);
    *(void **)&ng_cb_frame    = simbolo(h, "nghttp2_session_callbacks_set_on_frame_recv_callback", &ok);
    *(void **)&ng_client_new  = simbolo(h, "nghttp2_session_client_new", &ok);
    *(void **)&ng_session_del = simbolo(h, "nghttp2_session_del", &ok);
    *(void **)&ng_settings    = simbolo(h, "nghttp2_submit_settings", &ok);
    *(void **)&ng_request     = simbolo(h, "nghttp2_submit_request2", &ok);
    *(void **)&ng_recv        = simbolo(h, "nghttp2_session_mem_recv2", &ok);
    *(void **)&ng_send        = simbolo(h, "nghttp2_session_mem_send2", &ok);
    *(void **)&ng_rst         = simbolo(h, "nghttp2_submit_rst_stream", &ok);
    *(void **)&ng_get_ud      = simbolo(h, "nghttp2_session_get_stream_user_data", &ok);
    *(void **)&ng_set_ud      = simbolo(h, "nghttp2_session_set_stream_user_data", &ok);
    g_ok = ok;
    return g_ok;
}

struct H2 {
    void     *s;                /* nghttp2_session */
    H2Scrivi  scrivi;
    void     *tubo;
    H2Ganci   g;
    int       goaway;
    int       rotta;
};

static int su_testa(void *s, const void *frame, const unsigned char *nome, size_t nl,
                    const unsigned char *val, size_t vl, unsigned char flags, void *ud)
{
    const NgTesta *t = (const NgTesta *)frame;
    H2            *h = (H2 *)ud;
    void          *f;

    (void)flags;
    if (t->type != NG_HEADERS) return 0;
    f = ng_get_ud(s, t->stream_id);
    if (f && h->g.testa) h->g.testa(f, (const char *)nome, (unsigned int)nl,
                                    (const char *)val, (unsigned int)vl);
    return 0;
}

static int su_dati(void *s, unsigned char flags, int id, const unsigned char *d,
                   size_t n, void *ud)
{
    H2   *h = (H2 *)ud;
    void *f = ng_get_ud(s, id);

    (void)flags;
    if (f && h->g.dati) h->g.dati(f, d, (unsigned int)n);
    return 0;
}

static int su_chiuso(void *s, int id, unsigned int errore, void *ud)
{
    H2   *h = (H2 *)ud;
    void *f = ng_get_ud(s, id);

    if (f && h->g.chiuso) h->g.chiuso(f, errore);
    return 0;
}

static int su_frame(void *s, const void *frame, void *ud)
{
    (void)s;
    if (((const NgTesta *)frame)->type == NG_GOAWAY) ((H2 *)ud)->goaway = 1;
    return 0;
}

int h2_manda(H2 *h)
{
    for (;;) {
        const unsigned char *d;
        ng_ssize n = ng_send(h->s, &d);

        if (n < 0) { h->rotta = 1; return -1; }
        if (n == 0) return 0;
        if (h->scrivi(h->tubo, d, (unsigned int)n) != (int)n) { h->rotta = 1; return -1; }
    }
}

H2 *h2_apri(H2Scrivi scrivi, void *tubo, const H2Ganci *g)
{
    H2         *h;
    void       *cb = 0;
    NgSettings  iv[3];

    if (!h2_disponibile()) return 0;
    h = (H2 *)calloc(1, sizeof(*h));
    if (!h) return 0;
    h->scrivi = scrivi;
    h->tubo   = tubo;
    h->g      = *g;
    if (ng_cb_new(&cb) != 0) { free(h); return 0; }
    ng_cb_testa(cb, su_testa);
    ng_cb_dati(cb, su_dati);
    ng_cb_chiuso(cb, su_chiuso);
    ng_cb_frame(cb, su_frame);
    if (ng_client_new(&h->s, cb, h) != 0) { ng_cb_del(cb); free(h); return 0; }
    ng_cb_del(cb);

    /* ! NO PUSH (nobody would take it), and a stream window of 256 KB: the
     * default 64 KB makes a big script wait for a WINDOW_UPDATE every 64 KB,
     * which on a far server is a round trip each time */
    iv[0].id = NG_SETTINGS_ENABLE_PUSH;            iv[0].value = 0;
    iv[1].id = NG_SETTINGS_MAX_CONCURRENT_STREAMS; iv[1].value = 100;
    iv[2].id = NG_SETTINGS_INITIAL_WINDOW_SIZE;    iv[2].value = 256 * 1024;
    if (ng_settings(h->s, 0, iv, 3) != 0 || h2_manda(h) < 0) { h2_chiudi(h); return 0; }
    return h;
}

static void nv(NgNv *v, const char *nome, const char *valore)
{
    v->name     = (unsigned char *)nome;
    v->value    = (unsigned char *)valore;
    v->namelen  = strlen(nome);
    v->valuelen = strlen(valore);
    v->flags    = NG_NV_NO_COPY;
}

int h2_richiesta(H2 *h, const char *schema, const char *autorita,
                 const char *percorso, const char *const *nomi,
                 const char *const *valori, int n, void *flusso)
{
    NgNv v[24];
    int  k, id;

    if (!h || h->rotta || h->goaway || n < 0 || n > 20) return -1;
    nv(&v[0], ":method", "GET");
    nv(&v[1], ":scheme", schema);
    nv(&v[2], ":authority", autorita);
    nv(&v[3], ":path", percorso);
    for (k = 0; k < n; k++) nv(&v[4 + k], nomi[k], valori[k]);
    id = ng_request(h->s, 0, v, (size_t)(4 + n), 0, flusso);
    if (id <= 0) return -1;
    if (h2_manda(h) < 0) return -1;
    return id;
}

int h2_dai(H2 *h, const unsigned char *d, unsigned int n)
{
    ng_ssize r;

    if (!h || h->rotta) return -1;
    r = ng_recv(h->s, d, n);
    if (r < 0) { h->rotta = 1; return -1; }
    return h2_manda(h);
}

void h2_lascia(H2 *h, int id)
{
    if (!h || id <= 0) return;
    ng_set_ud(h->s, id, 0);
    ng_rst(h->s, 0, id, NG_CANCEL);
    h2_manda(h);
}

int h2_aperta(const H2 *h)
{
    return h && !h->rotta && !h->goaway;
}

void h2_chiudi(H2 *h)
{
    if (!h) return;
    if (h->s) ng_session_del(h->s);
    free(h);
}
