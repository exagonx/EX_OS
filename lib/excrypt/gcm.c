/* =============================================================================
 * lib/excrypt/gcm.c — AES-GCM (NIST SP 800-38D)
 *
 * Written on 23 September 2026 for TLS 1.3: until then the client offered one
 * cipher only, ChaCha20-Poly1305, and servers that do not accept it — eBay,
 * measured with tools/prova_certificati.sh — answered with an alert. The
 * other cipher every TLS 1.3 server has is AES-128-GCM.
 *
 * ! THE AES WAS ALREADY HERE (aes.c, from the Wi-Fi: WPA2 is AES-CCM), and
 * GCM, like CCM, only ever ENCRYPTS with it — counter mode, both ways. So
 * this file is the counter and GHASH, nothing else.
 *
 * ! GHASH GOES A BIT AT A TIME, on four 32-bit words: the algorithm of the
 * standard, with no tables. The tables of the fast implementations are the
 * same ones that leak the key through the cache (the same reason aes.c has
 * none), and a page of a few hundred KB costs milliseconds this way.
 *
 * Checked on the host by tools/prova_gcm.sh against the test vectors of the
 * GCM paper and against Python's `cryptography` on thousands of random cases.
 * ============================================================================= */
#include "excrypt.h"

static unsigned int be32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] << 8) | p[3];
}

static void metti32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);  p[3] = (unsigned char)v;
}

int gcm_chiave(GcmChiave *g, const unsigned char *chiave, unsigned int byte)
{
    unsigned char zero[16], h[16];
    int i;

    if (aes_chiave(&g->aes, chiave, byte) != 0) return -1;
    for (i = 0; i < 16; i++) zero[i] = 0;
    aes_cifra(&g->aes, zero, h);
    for (i = 0; i < 4; i++) g->h[i] = be32(h + 4 * i);
    return 0;
}

/* x = x * H in GF(2^128), the bit order of GCM (the first bit is the
 * coefficient of x^0, which is the most significant bit of the first byte). */
static void gf_per(unsigned int x[4], const unsigned int h[4])
{
    unsigned int z[4] = { 0, 0, 0, 0 };
    unsigned int v[4];
    int i, b;

    v[0] = h[0]; v[1] = h[1]; v[2] = h[2]; v[3] = h[3];

    for (i = 0; i < 4; i++) {
        unsigned int w = x[i];

        for (b = 31; b >= 0; b--) {
            unsigned int basso;

            if ((w >> b) & 1u) { z[0] ^= v[0]; z[1] ^= v[1]; z[2] ^= v[2]; z[3] ^= v[3]; }

            basso = v[3] & 1u;
            v[3] = (v[3] >> 1) | (v[2] << 31);
            v[2] = (v[2] >> 1) | (v[1] << 31);
            v[1] = (v[1] >> 1) | (v[0] << 31);
            v[0] >>= 1;
            if (basso) v[0] ^= 0xE1000000u;
        }
    }
    x[0] = z[0]; x[1] = z[1]; x[2] = z[2]; x[3] = z[3];
}

static void ghash_blocchi(const GcmChiave *g, unsigned int y[4],
                          const unsigned char *d, unsigned int n)
{
    while (n > 0) {
        unsigned char b[16];
        unsigned int  k = n < 16 ? n : 16, i;

        for (i = 0; i < 16; i++) b[i] = i < k ? d[i] : 0;
        for (i = 0; i < 4; i++) y[i] ^= be32(b + 4 * i);
        gf_per(y, g->h);
        d += k;
        n -= k;
    }
}

/* The tag: GHASH of aad and ciphertext, XOR E(K, J0). */
static void etichetta(const GcmChiave *g, const unsigned char j0[16],
                      const unsigned char *aad, unsigned int aad_n,
                      const unsigned char *c, unsigned int n,
                      unsigned char tag[16])
{
    unsigned int  y[4] = { 0, 0, 0, 0 };
    unsigned char lung[16], ej[16];
    int i;

    ghash_blocchi(g, y, aad, aad_n);
    ghash_blocchi(g, y, c, n);

    /* The lengths in BITS, 64 bits each: the high word is zero below 512 MB. */
    metti32(lung,      aad_n >> 29); metti32(lung + 4,  aad_n << 3);
    metti32(lung + 8,  n >> 29);     metti32(lung + 12, n << 3);
    ghash_blocchi(g, y, lung, 16);

    aes_cifra(&g->aes, j0, ej);
    for (i = 0; i < 4; i++) metti32(tag + 4 * i, y[i] ^ be32(ej + 4 * i));
}

/* Counter mode from J0 + 1: the last 32 bits count, big endian. */
static void ctr(const GcmChiave *g, const unsigned char j0[16],
                const unsigned char *in, unsigned char *out, unsigned int n)
{
    unsigned char cb[16], ks[16];
    unsigned int  conta = be32(j0 + 12), i;

    for (i = 0; i < 12; i++) cb[i] = j0[i];
    while (n > 0) {
        unsigned int k = n < 16 ? n : 16;

        metti32(cb + 12, ++conta);
        aes_cifra(&g->aes, cb, ks);
        for (i = 0; i < k; i++) out[i] = in[i] ^ ks[i];
        in += k; out += k; n -= k;
    }
}

static void j0_di(const unsigned char iv[12], unsigned char j0[16])
{
    int i;

    for (i = 0; i < 12; i++) j0[i] = iv[i];
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
}

void aes_gcm_cifra(const GcmChiave *g, const unsigned char iv[12],
                   const unsigned char *aad, unsigned int aad_n,
                   const unsigned char *in, unsigned char *out, unsigned int n,
                   unsigned char tag[16])
{
    unsigned char j0[16];

    j0_di(iv, j0);
    ctr(g, j0, in, out, n);
    etichetta(g, j0, aad, aad_n, out, n, tag);
}

int aes_gcm_decifra(const GcmChiave *g, const unsigned char iv[12],
                    const unsigned char *aad, unsigned int aad_n,
                    const unsigned char *in, unsigned char *out, unsigned int n,
                    const unsigned char tag[16])
{
    unsigned char j0[16], mio[16], diff = 0;
    unsigned int  i;

    j0_di(iv, j0);
    /* ! THE TAG FIRST, ON THE CIPHERTEXT, AND COMPARED IN CONSTANT TIME: the
     * plaintext is only produced for an authentic record. `in` and `out` may
     * be the same buffer, so the check has to come before the decryption. */
    etichetta(g, j0, aad, aad_n, in, n, mio);
    for (i = 0; i < 16; i++) diff |= (unsigned char)(mio[i] ^ tag[i]);
    if (diff) return -1;

    ctr(g, j0, in, out, n);
    return 0;
}
