/* =============================================================================
 * lib/exhttp/srotola.c
 * EX-OS — Extensible Operating System
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 * Content-Encoding gzip, deflate, br, zstd — decoded a piece at a time
 * (debug/implementazione_BROWSER002.txt, P2; what it is for in srotola.h).
 *
 * ! A PIECE AT A TIME, NOT THE WHOLE BODY: the bytes arrive in blocks of
 * four kilobytes and leave in blocks of sixteen, so a 30 MB answer never
 * needs 30 MB, and the receiver (exhttp's buffer and its verso) sees the
 * page as if it had come uncompressed.
 *
 * ! THE THREE LIBRARIES' TYPES ARE WRITTEN HERE, NOT INCLUDED. exhttp.so is
 * built with the project's own headers (lib/include), not with a sysroot's,
 * and only a few names of each library are used. They are the ones of
 * zlib 1.3.2 (zlib.h), zstd 1.5.7 (zstd.h) and brotli 1.2.1 (decode.h), and
 * a mistake in z_stream would not pass silently: inflateInit2_ checks
 * sizeof(z_stream) against its own and refuses (Z_VERSION_ERROR).
 * ============================================================================= */
#include "libc.h"
#include "srotola.h"

/* -----------------------------------------------------------------------------
 * zlib (zlib.h of 1.3.2: uInt is unsigned int, uLong unsigned long)
 * --------------------------------------------------------------------------- */
typedef struct {
    const unsigned char *next_in;
    unsigned int         avail_in;
    unsigned long        total_in;
    unsigned char       *next_out;
    unsigned int         avail_out;
    unsigned long        total_out;
    const char          *msg;
    void                *state;
    void                *zalloc;
    void                *zfree;
    void                *opaque;
    int                  data_type;
    unsigned long        adler;
    unsigned long        reserved;
} ZFlusso;

#define Z_OK            0
#define Z_STREAM_END    1
#define Z_NEED_DICT     2
#define Z_BUF_ERROR   (-5)
#define Z_NO_FLUSH      0

static int (*z_init2)(ZFlusso *, int, const char *, int);
static int (*z_inflate)(ZFlusso *, int);
static int (*z_reset)(ZFlusso *);
static int (*z_end)(ZFlusso *);

/* -----------------------------------------------------------------------------
 * zstd (zstd.h of 1.5.7)
 * --------------------------------------------------------------------------- */
typedef struct { const void *src; size_t size; size_t pos; } ZsEntra;
typedef struct { void *dst; size_t size; size_t pos; } ZsEsce;
#define ZSTD_d_windowLogMax 100

static void  *(*zs_crea)(void);
static size_t (*zs_libera)(void *);
static size_t (*zs_param)(void *, int, int);
static size_t (*zs_srotola)(void *, ZsEsce *, ZsEntra *);
static unsigned (*zs_errore)(size_t);

/* -----------------------------------------------------------------------------
 * brotli (decode.h of 1.2.1)
 * --------------------------------------------------------------------------- */
#define BR_ERRORE        0
#define BR_FATTO         1
#define BR_VUOLE_DATI    2
#define BR_VUOLE_POSTO   3

static void *(*br_crea)(void *, void *, void *);
static void  (*br_libera)(void *);
static int   (*br_srotola)(void *, size_t *, const unsigned char **,
                           size_t *, unsigned char **, size_t *);

/* -----------------------------------------------------------------------------
 * Opening the libraries, once
 * --------------------------------------------------------------------------- */
static int  g_cercate = 0;
static int  g_z = 0, g_zs = 0, g_br = 0;
static char g_codifiche[32];

static void *simbolo(void *h, const char *nome, int *ok)
{
    void *p = h ? dlsym(h, nome) : 0;

    if (!p) *ok = 0;
    return p;
}

/* ! RTLD_NOW AND NEVER CLOSED: a library half-found (a symbol missing, an
 * older version) is a coding not offered, decided here once, not a call
 * through a null pointer in the middle of a page. */
static void cerca(void)
{
    void *h;
    int   ok;
    unsigned int n = 0;

    if (g_cercate) return;
    g_cercate = 1;

    h = dlopen("libz.so.1", RTLD_NOW);
    ok = h != 0;
    *(void **)&z_init2   = simbolo(h, "inflateInit2_", &ok);
    *(void **)&z_inflate = simbolo(h, "inflate", &ok);
    *(void **)&z_reset   = simbolo(h, "inflateReset", &ok);
    *(void **)&z_end     = simbolo(h, "inflateEnd", &ok);
    g_z = ok;

    h = dlopen("libzstd.so.1", RTLD_NOW);
    ok = h != 0;
    *(void **)&zs_crea    = simbolo(h, "ZSTD_createDStream", &ok);
    *(void **)&zs_libera  = simbolo(h, "ZSTD_freeDStream", &ok);
    *(void **)&zs_param   = simbolo(h, "ZSTD_DCtx_setParameter", &ok);
    *(void **)&zs_srotola = simbolo(h, "ZSTD_decompressStream", &ok);
    *(void **)&zs_errore  = simbolo(h, "ZSTD_isError", &ok);
    g_zs = ok;

    h = dlopen("libbrotlidec.so.1", RTLD_NOW);
    ok = h != 0;
    *(void **)&br_crea    = simbolo(h, "BrotliDecoderCreateInstance", &ok);
    *(void **)&br_libera  = simbolo(h, "BrotliDecoderDestroyInstance", &ok);
    *(void **)&br_srotola = simbolo(h, "BrotliDecoderDecompressStream", &ok);
    g_br = ok;

    /* the order is the one of the big browsers; the server picks anyway */
    g_codifiche[0] = '\0';
    if (g_z)  { strcpy(g_codifiche + n, "gzip, deflate"); n = (unsigned int)strlen(g_codifiche); }
    if (g_br) { strcpy(g_codifiche + n, n ? ", br" : "br");     n = (unsigned int)strlen(g_codifiche); }
    if (g_zs) { strcpy(g_codifiche + n, n ? ", zstd" : "zstd"); }
}

const char *srotola_codifiche(void)
{
    cerca();
    return g_codifiche;
}

/* -----------------------------------------------------------------------------
 * The decoder
 * --------------------------------------------------------------------------- */
typedef enum { SR_GZIP, SR_DEFLATE, SR_BR, SR_ZSTD } SrTipo;

struct Srotola {
    SrTipo   tipo;
    int      finito;
    int      partito;       /* zlib: inflateInit2_ done (deflate waits for
                             * its first two bytes to know its kind)       */
    ZFlusso  z;
    void    *zs;
    void    *br;
    unsigned char primo[2]; /* deflate: the header bytes seen so far       */
    unsigned int  primo_n;
};

#define FINESTRA 16384

/* the same word, case aside, with spaces around allowed */
static int e_lei(const char *v, const char *parola)
{
    unsigned int l = (unsigned int)strlen(parola);

    while (*v == ' ' || *v == '\t') v++;
    if (strncasecmp(v, parola, l) != 0) return 0;
    v += l;
    while (*v == ' ' || *v == '\t') v++;
    return *v == '\0';
}

Srotola *srotola_apri(const char *codifica, int *rotto)
{
    Srotola *s;
    SrTipo   t;

    *rotto = 0;
    if (!codifica || e_lei(codifica, "") || e_lei(codifica, "identity")) return 0;

    cerca();
    *rotto = 1;
    if ((e_lei(codifica, "gzip") || e_lei(codifica, "x-gzip")) && g_z) t = SR_GZIP;
    else if (e_lei(codifica, "deflate") && g_z) t = SR_DEFLATE;
    else if (e_lei(codifica, "br") && g_br)     t = SR_BR;
    else if (e_lei(codifica, "zstd") && g_zs)   t = SR_ZSTD;
    else return 0;

    s = (Srotola *)calloc(1, sizeof(*s));
    if (!s) return 0;
    s->tipo = t;

    if (t == SR_GZIP) {
        /* 15 + 16: a gzip member, its header and its CRC-32 checked */
        if (z_init2(&s->z, 15 + 16, "1.3.2", (int)sizeof(ZFlusso)) != Z_OK) { free(s); return 0; }
        s->partito = 1;
    } else if (t == SR_ZSTD) {
        s->zs = zs_crea();
        if (!s->zs) { free(s); return 0; }
        /* ! RFC 9659: an HTTP decoder needs a window of at most 8 MB, and
         * refusing more stops a server from making us allocate 2 GB */
        if (zs_errore(zs_param(s->zs, ZSTD_d_windowLogMax, 23))) { zs_libera(s->zs); free(s); return 0; }
    } else if (t == SR_BR) {
        s->br = br_crea(0, 0, 0);
        if (!s->br) { free(s); return 0; }
    }
    *rotto = 0;
    return s;
}

const char *srotola_nome(const Srotola *s)
{
    static const char *const N[] = { "gzip", "deflate", "br", "zstd" };
    return s ? N[s->tipo] : "";
}

int srotola_finito(const Srotola *s)
{
    return s && s->finito;
}

/* ! "deflate" IN HTTP IS THE zlib FORMAT (RFC 9110 8.4.1.2), and some old
 * servers send the bare deflate stream instead. The first two bytes tell:
 * a zlib header is CM = 8 in the low nibble and a multiple of 31. */
static int z_parti_deflate(Srotola *s)
{
    int zlib = (s->primo[0] & 0x0f) == 8 &&
               (((unsigned int)s->primo[0] << 8) | s->primo[1]) % 31 == 0;

    if (z_init2(&s->z, zlib ? 15 : -15, "1.3.2", (int)sizeof(ZFlusso)) != Z_OK) return SROTOLA_ROTTO;
    s->partito = 1;
    return SROTOLA_OK;
}

static int z_dai(Srotola *s, const unsigned char *in, unsigned int n,
                 SrotolaScrivi f, void *dato, unsigned char *fuori)
{
    s->z.next_in  = in;
    s->z.avail_in = n;
    while (s->z.avail_in > 0) {
        int r;
        unsigned int fatti;

        if (s->finito) {
            /* ! gzip MAY BE SEVERAL MEMBERS ONE AFTER THE OTHER (RFC 1952
             * 2.2): what follows the end of one is the next. deflate is one
             * stream: bytes after its end are ignored. */
            if (s->tipo != SR_GZIP) return SROTOLA_OK;
            if (z_reset(&s->z) != Z_OK) return SROTOLA_ROTTO;
            s->finito = 0;
        }
        s->z.next_out  = fuori;
        s->z.avail_out = FINESTRA;
        r = z_inflate(&s->z, Z_NO_FLUSH);
        fatti = FINESTRA - s->z.avail_out;
        if (fatti && f(dato, fuori, fatti)) return SROTOLA_FERMATO;
        if (r == Z_STREAM_END) { s->finito = 1; continue; }
        if (r == Z_BUF_ERROR && fatti == 0) return SROTOLA_ROTTO;  /* no progress */
        if (r != Z_OK) return SROTOLA_ROTTO;                       /* Z_NEED_DICT too */
    }
    /* the input is used up but the output may still hold some: ask again
     * until zlib stops filling the window */
    while (!s->finito) {
        int r;
        unsigned int fatti;

        s->z.next_out  = fuori;
        s->z.avail_out = FINESTRA;
        r = z_inflate(&s->z, Z_NO_FLUSH);
        fatti = FINESTRA - s->z.avail_out;
        if (fatti && f(dato, fuori, fatti)) return SROTOLA_FERMATO;
        if (r == Z_STREAM_END) s->finito = 1;
        else if (r != Z_OK && r != Z_BUF_ERROR) return SROTOLA_ROTTO;
        if (fatti < FINESTRA) break;
    }
    return SROTOLA_OK;
}

int srotola_dai(Srotola *s, const unsigned char *in, unsigned int n,
                SrotolaScrivi f, void *dato)
{
    static unsigned char fuori[FINESTRA];

    if (!s) return n ? (f(dato, in, n) ? SROTOLA_FERMATO : SROTOLA_OK) : SROTOLA_OK;
    if (n == 0) return SROTOLA_OK;

    switch (s->tipo) {
    case SR_DEFLATE:
        if (!s->partito) {
            while (s->primo_n < 2 && n > 0) {
                /* kept aside, then given to zlib with the rest */
                s->primo[s->primo_n++] = *in++;
                n--;
            }
            if (s->primo_n < 2) return SROTOLA_OK;
            if (z_parti_deflate(s) != SROTOLA_OK) return SROTOLA_ROTTO;
            {
                int r = z_dai(s, s->primo, 2, f, dato, fuori);
                if (r != SROTOLA_OK) return r;
            }
            if (n == 0) return SROTOLA_OK;
        }
        return z_dai(s, in, n, f, dato, fuori);

    case SR_GZIP:
        return z_dai(s, in, n, f, dato, fuori);

    case SR_ZSTD: {
        ZsEntra e;

        e.src = in; e.size = n; e.pos = 0;
        while (e.pos < e.size) {
            ZsEsce o;
            size_t r;

            o.dst = fuori; o.size = FINESTRA; o.pos = 0;
            r = zs_srotola(s->zs, &o, &e);
            if (zs_errore(r)) return SROTOLA_ROTTO;
            if (o.pos && f(dato, fuori, (unsigned int)o.pos)) return SROTOLA_FERMATO;
            /* 0 = a frame has ended and its output is all out; another may
             * follow, and then this goes back to 0 */
            s->finito = (r == 0);
            if (o.pos == 0 && e.pos == e.size) break;
        }
        /* what is left in its window comes out without more input */
        while (!s->finito) {
            ZsEsce o;
            size_t r;

            e.src = in; e.size = 0; e.pos = 0;
            o.dst = fuori; o.size = FINESTRA; o.pos = 0;
            r = zs_srotola(s->zs, &o, &e);
            if (zs_errore(r)) return SROTOLA_ROTTO;
            if (o.pos && f(dato, fuori, (unsigned int)o.pos)) return SROTOLA_FERMATO;
            s->finito = (r == 0);
            if (o.pos < FINESTRA) break;
        }
        return SROTOLA_OK;
    }

    case SR_BR: {
        const unsigned char *pi = in;
        size_t ai = n;

        if (s->finito) return SROTOLA_OK;        /* bytes after the end */
        for (;;) {
            unsigned char *po = fuori;
            size_t ao = FINESTRA;
            int r = br_srotola(s->br, &ai, &pi, &ao, &po, 0);

            if (po > fuori && f(dato, fuori, (unsigned int)(po - fuori))) return SROTOLA_FERMATO;
            if (r == BR_ERRORE) return SROTOLA_ROTTO;
            if (r == BR_FATTO) { s->finito = 1; return SROTOLA_OK; }
            if (r == BR_VUOLE_DATI) return SROTOLA_OK;
            /* BR_VUOLE_POSTO: the window is full, empty it and go on */
        }
    }
    }
    return SROTOLA_ROTTO;
}

void srotola_chiudi(Srotola *s)
{
    if (!s) return;
    if ((s->tipo == SR_GZIP || s->tipo == SR_DEFLATE) && s->partito) z_end(&s->z);
    if (s->zs) zs_libera(s->zs);
    if (s->br) br_libera(s->br);
    free(s);
}
