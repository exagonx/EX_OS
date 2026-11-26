/* =============================================================================
 * exwin/bin/exbrowser/browser_cripto.c — crypto.subtle's hashes (P11)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Two natives for the prelude's crypto.subtle (debug/implementazione_
 * BROWSER002.txt, 10.7):
 *   __exos_digest(alg, hex)          -> hex, or "" for an algorithm we lack
 *   __exos_hmac(alg, chiave, hex)    -> hex
 * with SHA-1, SHA-256, SHA-384 and SHA-512: SHA-1 and SHA-384/512 from
 * lib/excrypt (compiled in, as netupdate does with ed25519), SHA-256 from
 * the libc (the one TLS uses).
 *
 * ! THE BYTES TRAVEL AS HEX: ExJs has no ArrayBuffer to hand a native, and a
 * JavaScript string of bytes 128-255 would reach C as UTF-8. Hex is twice the
 * size and exact; the inputs of digest and sign on real pages are small.
 *
 * ! AN ALGORITHM WE DO NOT HAVE IS AN EMPTY ANSWER, and the prelude rejects
 * the Promise with NotSupportedError: never a result that looks right.
 * ========================================================================== */
#include "browser_priv.h"
#include "excrypt.h"

void sha256(const void *dati, __SIZE_TYPE__ len, unsigned char out[32]);   /* libc */

#define CRIPTO_MAX (1024 * 1024)

static int esa(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* hex into bytes: the count, or -1 */
static int da_esa(const char *s, unsigned char *out, int max)
{
    int n = 0;

    while (s[0] && s[1]) {
        int a = esa((unsigned char)s[0]), b = esa((unsigned char)s[1]);

        if (a < 0 || b < 0 || n >= max) return -1;
        out[n++] = (unsigned char)(a << 4 | b);
        s += 2;
    }
    return s[0] ? -1 : n;
}

static ExJsVal in_esa(ExJsCtx *c, const unsigned char *b, int n)
{
    static const char C[] = "0123456789abcdef";
    char s[129];
    int  i;

    for (i = 0; i < n && i < 64; i++) { s[i * 2] = C[b[i] >> 4]; s[i * 2 + 1] = C[b[i] & 15]; }
    s[i * 2] = 0;
    return exjs_stringa(c, s, -1);
}

/* the length of the hash, its block, or 0 for one we lack */
static int algoritmo(const char *a, int *blocco)
{
    if (!strcmp(a, "SHA-1"))   { *blocco = 64;  return 20; }
    if (!strcmp(a, "SHA-256")) { *blocco = 64;  return 32; }
    if (!strcmp(a, "SHA-384")) { *blocco = 128; return 48; }
    if (!strcmp(a, "SHA-512")) { *blocco = 128; return 64; }
    return 0;
}

static void hash(const char *a, const unsigned char *m, unsigned int n, unsigned char *out)
{
    if (!strcmp(a, "SHA-1")) sha1(m, n, out);
    else if (!strcmp(a, "SHA-256")) sha256(m, n, out);
    else if (!strcmp(a, "SHA-384")) sha384(m, n, out);
    else sha512(m, n, out);
}

static unsigned char *g_buf;

static int buf_pronto(void)
{
    if (!g_buf) g_buf = (unsigned char *)malloc(CRIPTO_MAX + 256);
    return g_buf != 0;
}

static ExJsVal nat_digest(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    const char   *alg, *h;
    char          nome[16];
    unsigned char out[64];
    int           l, bl, n;

    (void)questo; (void)dato;
    if (n_arg < 2 || !buf_pronto()) return exjs_stringa(c, "", -1);
    alg = exjs_a_stringa(c, a[0]);
    if (!alg) return exjs_stringa(c, "", -1);
    strncpy(nome, alg, sizeof(nome) - 1); nome[sizeof(nome) - 1] = 0;
    l = algoritmo(nome, &bl);
    h = exjs_a_stringa(c, a[1]);
    if (!l || !h || (n = da_esa(h, g_buf, CRIPTO_MAX)) < 0) return exjs_stringa(c, "", -1);
    hash(nome, g_buf, (unsigned int)n, out);
    return in_esa(c, out, l);
}

/* HMAC (RFC 2104) over any of the four */
static ExJsVal nat_hmac(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n_arg, void *dato)
{
    const char   *s;
    char          nome[16];
    unsigned char k[128], ip[128], op[128 + 64], out[64];
    int           l, bl, kn, n, i;

    (void)questo; (void)dato;
    if (n_arg < 3 || !buf_pronto()) return exjs_stringa(c, "", -1);
    s = exjs_a_stringa(c, a[0]);
    if (!s) return exjs_stringa(c, "", -1);
    strncpy(nome, s, sizeof(nome) - 1); nome[sizeof(nome) - 1] = 0;
    if (!(l = algoritmo(nome, &bl))) return exjs_stringa(c, "", -1);
    s = exjs_a_stringa(c, a[1]);
    if (!s || (kn = da_esa(s, g_buf, CRIPTO_MAX)) < 0) return exjs_stringa(c, "", -1);
    memset(k, 0, sizeof(k));
    if (kn > bl) { hash(nome, g_buf, (unsigned int)kn, k); kn = l; }
    else memcpy(k, g_buf, (unsigned int)kn);
    s = exjs_a_stringa(c, a[2]);
    if (!s || (n = da_esa(s, g_buf + bl, CRIPTO_MAX - bl)) < 0) return exjs_stringa(c, "", -1);
    for (i = 0; i < bl; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    memcpy(g_buf, ip, (unsigned int)bl);
    hash(nome, g_buf, (unsigned int)(bl + n), out);
    memcpy(op + bl, out, (unsigned int)l);
    hash(nome, op, (unsigned int)(bl + l), out);
    return in_esa(c, out, l);
}

void cripto_registra(ExJsCtx *js)
{
    exjs_metti(js, exjs_globale(js), "__exos_digest", exjs_nativa(js, nat_digest, 0, "__exos_digest"));
    exjs_metti(js, exjs_globale(js), "__exos_hmac", exjs_nativa(js, nat_hmac, 0, "__exos_hmac"));
}
