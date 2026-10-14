/* =============================================================================
 * lib/exwifi/exwifi.c — WiFi security: keys, EAPOL-Key, CCMP, TKIP, WEP (@WIFI W6)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * See exwifi.h for what each part is. References: IEEE 802.11-2012, 11.4
 * (TKIP, CCMP), 11.6 (keys and EAPOL-Key); RFC 1321 (MD5), 2104 (HMAC),
 * 3394 (AES key wrap).
 * ============================================================================= */
#include <string.h>
#include "excrypt.h"
#include "exwifi.h"

/* =============================================================================
 * MD5 (RFC 1321) and HMAC-MD5 - only for WPA's descriptor version 1
 * ============================================================================= */
static unsigned int rotl32(unsigned int x, unsigned int n) { return (x << n) | (x >> (32 - n)); }

static void md5_blocco(unsigned int h[4], const unsigned char *b)
{
    static const unsigned int K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
    static const unsigned char R[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
    unsigned int m[16], a = h[0], b2 = h[1], c = h[2], d = h[3], f, t;
    unsigned int i, g;

    for (i = 0; i < 16; i++)
        m[i] = (unsigned int)b[i*4] | ((unsigned int)b[i*4+1] << 8) |
               ((unsigned int)b[i*4+2] << 16) | ((unsigned int)b[i*4+3] << 24);
    for (i = 0; i < 64; i++) {
        if (i < 16)      { f = (b2 & c) | (~b2 & d); g = i; }
        else if (i < 32) { f = (d & b2) | (~d & c);  g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b2 ^ c ^ d;           g = (3 * i + 5) & 15; }
        else             { f = c ^ (b2 | ~d);        g = (7 * i) & 15; }
        t = d; d = c; c = b2;
        b2 = b2 + rotl32(a + f + K[i] + m[g], R[i]);
        a = t;
    }
    h[0] += a; h[1] += b2; h[2] += c; h[3] += d;
}

/* md5 over the concatenation of two pieces: HMAC needs (key pad || message)
 * and this spares a copy */
static void md5_due(const unsigned char *p1, unsigned int n1,
                    const unsigned char *p2, unsigned int n2, unsigned char out[16])
{
    unsigned int  h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
    unsigned char b[64];
    unsigned int  tot = n1 + n2, fatti = 0, k = 0, i;

    while (fatti < tot) {
        b[k++] = fatti < n1 ? p1[fatti] : p2[fatti - n1];
        fatti++;
        if (k == 64) { md5_blocco(h, b); k = 0; }
    }
    b[k++] = 0x80;
    if (k > 56) { while (k < 64) b[k++] = 0; md5_blocco(h, b); k = 0; }
    while (k < 56) b[k++] = 0;
    {
        unsigned long long bit = (unsigned long long)tot * 8;
        for (i = 0; i < 8; i++) b[56 + i] = (unsigned char)(bit >> (8 * i));
    }
    md5_blocco(h, b);
    for (i = 0; i < 16; i++) out[i] = (unsigned char)(h[i / 4] >> (8 * (i % 4)));
}

void md5(const unsigned char *m, unsigned int n, unsigned char out[16])
{
    md5_due(m, n, 0, 0, out);
}

void hmac_md5(const unsigned char *k, unsigned int k_n,
              const unsigned char *m, unsigned int n, unsigned char out[16])
{
    unsigned char kk[64], pad[64], dentro[16];
    unsigned int  i;

    memset(kk, 0, sizeof(kk));
    if (k_n > 64) md5(k, k_n, kk); else memcpy(kk, k, k_n);
    for (i = 0; i < 64; i++) pad[i] = (unsigned char)(kk[i] ^ 0x36);
    md5_due(pad, 64, m, n, dentro);
    for (i = 0; i < 64; i++) pad[i] = (unsigned char)(kk[i] ^ 0x5c);
    md5_due(pad, 64, dentro, 16, out);
}

/* =============================================================================
 * RC4 and CRC-32
 * ============================================================================= */
void rc4(const unsigned char *chiave, unsigned int chiave_n, unsigned int salta,
         const unsigned char *in, unsigned char *out, unsigned int n)
{
    unsigned char S[256], t;
    unsigned int  i, j = 0, k;

    for (i = 0; i < 256; i++) S[i] = (unsigned char)i;
    for (i = 0; i < 256; i++) {
        j = (j + S[i] + chiave[i % chiave_n]) & 0xFF;
        t = S[i]; S[i] = S[j]; S[j] = t;
    }
    i = j = 0;
    for (k = 0; k < salta + n; k++) {
        i = (i + 1) & 0xFF;
        j = (j + S[i]) & 0xFF;
        t = S[i]; S[i] = S[j]; S[j] = t;
        if (k >= salta) out[k - salta] = (unsigned char)(in[k - salta] ^ S[(S[i] + S[j]) & 0xFF]);
    }
}

unsigned int crc32_ieee(const unsigned char *m, unsigned int n)
{
    unsigned int c = 0xFFFFFFFFu, i, k;

    for (i = 0; i < n; i++) {
        c ^= m[i];
        for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}

/* =============================================================================
 * Keys
 * ============================================================================= */
void wpa_pmk(const char *parola, const unsigned char *ssid, unsigned int ssid_n,
             unsigned char pmk[32])
{
    pbkdf2_sha1((const unsigned char *)parola, (unsigned int)strlen(parola),
                ssid, ssid_n, 4096, pmk, 32);
}

void wpa_prf(const unsigned char *k, unsigned int k_n, const char *etichetta,
             const unsigned char *dati, unsigned int dati_n,
             unsigned char *out, unsigned int out_n)
{
    unsigned char m[256], h[20];
    unsigned int  e = (unsigned int)strlen(etichetta), n, fatti = 0, i = 0, c;

    if (e + 1 + dati_n + 1 > sizeof(m)) { memset(out, 0, out_n); return; }
    memcpy(m, etichetta, e);
    m[e] = 0;
    memcpy(m + e + 1, dati, dati_n);
    n = e + 1 + dati_n + 1;
    while (fatti < out_n) {
        m[n - 1] = (unsigned char)i++;
        hmac_sha1(k, k_n, m, n, h);
        c = out_n - fatti < 20 ? out_n - fatti : 20;
        memcpy(out + fatti, h, c);
        fatti += c;
    }
}

void wpa_ptk(const unsigned char pmk[32], const unsigned char aa[6],
             const unsigned char spa[6], const unsigned char anonce[32],
             const unsigned char snonce[32], int cifrario, WpaPtk *p)
{
    unsigned char d[76], k[64];
    int           a_prima = memcmp(aa, spa, 6) < 0, n_prima = memcmp(anonce, snonce, 32) < 0;
    unsigned int  n = cifrario == WPA_CIFRARIO_TKIP ? 64 : 48;

    memcpy(d,      a_prima ? aa : spa, 6);
    memcpy(d + 6,  a_prima ? spa : aa, 6);
    memcpy(d + 12, n_prima ? anonce : snonce, 32);
    memcpy(d + 44, n_prima ? snonce : anonce, 32);
    wpa_prf(pmk, 32, "Pairwise key expansion", d, sizeof(d), k, n);
    memset(p, 0, sizeof(*p));
    memcpy(p->kck, k, 16);
    memcpy(p->kek, k + 16, 16);
    p->tk_n = n - 32;
    memcpy(p->tk, k + 32, p->tk_n);
    memset(k, 0, sizeof(k));
}

/* =============================================================================
 * AES key wrap (RFC 3394)
 * ============================================================================= */
void aes_wrap(const unsigned char kek[16], const unsigned char *in, unsigned int n,
              unsigned char *out)
{
    AesChiave    c;
    unsigned char a[8], b[16];
    unsigned int  blocchi = n / 8, j, i, t;

    aes_chiave(&c, kek, 16);
    memset(a, 0xA6, 8);
    memcpy(out + 8, in, n);
    for (j = 0; j <= 5; j++)
        for (i = 1; i <= blocchi; i++) {
            memcpy(b, a, 8);
            memcpy(b + 8, out + 8 * i, 8);
            aes_cifra(&c, b, b);
            t = blocchi * j + i;
            memcpy(a, b, 8);
            a[7] ^= (unsigned char)t; a[6] ^= (unsigned char)(t >> 8);
            a[5] ^= (unsigned char)(t >> 16); a[4] ^= (unsigned char)(t >> 24);
            memcpy(out + 8 * i, b + 8, 8);
        }
    memcpy(out, a, 8);
    memset(&c, 0, sizeof(c));
}

int aes_unwrap(const unsigned char kek[16], const unsigned char *in, unsigned int n,
               unsigned char *out)
{
    AesChiave     c;
    unsigned char a[8], b[16];
    unsigned int  blocchi, j, i, t, diverso = 0;
    int           jj;

    if (n < 24 || n % 8) return -1;
    blocchi = n / 8 - 1;
    aes_chiave(&c, kek, 16);
    memcpy(a, in, 8);
    memcpy(out, in + 8, n - 8);
    for (jj = 5; jj >= 0; jj--)
        for (i = blocchi; i >= 1; i--) {
            j = (unsigned int)jj;
            t = blocchi * j + i;
            memcpy(b, a, 8);
            b[7] ^= (unsigned char)t; b[6] ^= (unsigned char)(t >> 8);
            b[5] ^= (unsigned char)(t >> 16); b[4] ^= (unsigned char)(t >> 24);
            memcpy(b + 8, out + 8 * (i - 1), 8);
            aes_decifra(&c, b, b);
            memcpy(a, b, 8);
            memcpy(out + 8 * (i - 1), b + 8, 8);
        }
    memset(&c, 0, sizeof(c));
    for (i = 0; i < 8; i++) diverso |= a[i] ^ 0xA6u;
    if (diverso) { memset(out, 0, n - 8); return -1; }
    return 0;
}

/* =============================================================================
 * EAPOL-Key
 *
 * The frame, from the 802.1X header:
 *   0 version  1 type (3 = Key)  2-3 body length
 *   4 descriptor (254 WPA, 2 RSN)  5-6 key info  7-8 key length
 *   9-16 replay counter  17-48 nonce  49-64 key IV  65-72 RSC  73-80 reserved
 *   81-96 MIC  97-98 key data length  99... key data
 * ============================================================================= */
#define K_DESCR   4
#define K_INFO    5
#define K_LUNG    7
#define K_REPLAY  9
#define K_NONCE   17
#define K_IV      49
#define K_RSC     65
#define K_MIC     81
#define K_DLUNG   97
#define K_DATI    99

static unsigned int be16(const unsigned char *p) { return ((unsigned int)p[0] << 8) | p[1]; }
static void metti16(unsigned char *p, unsigned int v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }

void eapol_mic(int versione, const unsigned char kck[16],
               const unsigned char *frame, unsigned int n, unsigned char mic[16])
{
    unsigned char h[20];

    if (versione == 1) { hmac_md5(kck, 16, frame, n, mic); return; }
    hmac_sha1(kck, 16, frame, n, h);
    memcpy(mic, h, 16);
}

static int mic_giusto(const WpaSupplicante *s, int versione, const unsigned char *in, unsigned int n)
{
    unsigned char copia[EAPOL_MAX], mic[16];
    unsigned int  i, diverso = 0;

    if (n > sizeof(copia)) return 0;
    memcpy(copia, in, n);
    memset(copia + K_MIC, 0, 16);
    eapol_mic(versione, s->ptk.kck, copia, n, mic);
    for (i = 0; i < 16; i++) diverso |= mic[i] ^ in[K_MIC + i];
    return diverso == 0;
}

/* an answer: same version and descriptor, the replay counter echoed */
static unsigned int risposta(const WpaSupplicante *s, const unsigned char *in, unsigned int info,
                             const unsigned char *nonce, const unsigned char *dati,
                             unsigned int dati_n, unsigned char *out)
{
    unsigned int n = K_DATI + dati_n, versione = info & EAPOL_KI_VERSIONE;

    memset(out, 0, n);
    out[0] = in[0] >= 2 ? in[0] : 1;
    out[1] = 3;
    metti16(out + 2, n - EAPOL_TESTA);
    out[K_DESCR] = in[K_DESCR];
    metti16(out + K_INFO, info);
    if (!s->rsn) memcpy(out + K_LUNG, in + K_LUNG, 2);    /* WPA echoes it; RSN leaves 0 */
    memcpy(out + K_REPLAY, in + K_REPLAY, 8);
    if (nonce) memcpy(out + K_NONCE, nonce, 32);
    metti16(out + K_DLUNG, dati_n);
    if (dati_n) memcpy(out + K_DATI, dati, dati_n);
    eapol_mic((int)versione, s->ptk.kck, out, n, out + K_MIC);
    return n;
}

/* key data in clear: RC4 (version 1) or AES unwrap (version 2) */
static int dati_in_chiaro(const WpaSupplicante *s, unsigned int versione, const unsigned char *in,
                          const unsigned char *dati, unsigned int n, unsigned char *out, unsigned int *out_n)
{
    if (versione == 1) {
        unsigned char k[32];

        memcpy(k, in + K_IV, 16);
        memcpy(k + 16, s->ptk.kek, 16);
        rc4(k, 32, 256, dati, out, n);
        memset(k, 0, sizeof(k));
        *out_n = n;
        return 0;
    }
    if (aes_unwrap(s->ptk.kek, dati, n, out) != 0) return -1;
    *out_n = n - 8;
    return 0;
}

/* the GTK KDE in RSN key data: dd len 00-0f-ac 01 [id] [0] GTK */
static int cerca_gtk(WpaSupplicante *s, const unsigned char *d, unsigned int n)
{
    unsigned int i = 0;

    while (i + 2 <= n) {
        unsigned int t = d[i], l = d[i + 1];

        if (t == 0xdd && l == 0) break;                    /* padding */
        if (i + 2 + l > n) break;
        if (t == 0xdd && l >= 6 + 16 && d[i+2] == 0x00 && d[i+3] == 0x0f && d[i+4] == 0xac && d[i+5] == 1) {
            s->gtk_n = l - 6;
            if (s->gtk_n > sizeof(s->gtk)) return -1;
            s->gtk_indice = d[i + 6] & 3;
            memcpy(s->gtk, d + i + 8, s->gtk_n);
            s->gtk_pronta = 1;
            return 0;
        }
        i += 2 + l;
    }
    return -1;
}

void wpa_inizia(WpaSupplicante *s, const unsigned char pmk[32],
                const unsigned char aa[6], const unsigned char spa[6],
                int rsn, int cifrario, const unsigned char *ie, unsigned int ie_n,
                const unsigned char snonce[32])
{
    memset(s, 0, sizeof(*s));
    memcpy(s->pmk, pmk, 32);
    memcpy(s->aa, aa, 6);
    memcpy(s->spa, spa, 6);
    s->rsn = rsn;
    s->cifrario = cifrario;
    if (ie_n > sizeof(s->ie)) ie_n = sizeof(s->ie);
    memcpy(s->ie, ie, ie_n);
    s->ie_n = ie_n;
    memcpy(s->snonce, snonce, 32);
}

int wpa_ricevi(WpaSupplicante *s, const unsigned char *in, unsigned int in_n,
               unsigned char *out, unsigned int *out_n)
{
    unsigned int  info, versione, dlung;
    unsigned char chiaro[EAPOL_MAX];
    unsigned int  chiaro_n = 0;

    *out_n = 0;
    if (in_n < K_DATI || in_n > EAPOL_MAX || in[1] != 3) return WPA_ERR_FORMA;
    if (EAPOL_TESTA + be16(in + 2) > in_n) return WPA_ERR_FORMA;
    in_n = EAPOL_TESTA + be16(in + 2);                     /* what follows is padding */
    if (in[K_DESCR] != (s->rsn ? 2 : 254)) return WPA_NIENTE;
    info = be16(in + K_INFO);
    versione = info & EAPOL_KI_VERSIONE;
    dlung = be16(in + K_DLUNG);
    if (versione != 1 && versione != 2) return WPA_ERR_FORMA;
    if (K_DATI + dlung > in_n) return WPA_ERR_FORMA;
    if (!(info & EAPOL_KI_ACK)) return WPA_NIENTE;          /* not from the authenticator */

    /* every frame with a MIC: the MIC, then the replay counter */
    if (info & EAPOL_KI_MIC) {
        if (!s->ptk_ok) return WPA_ERR_FORMA;
        if (!mic_giusto(s, (int)versione, in, in_n)) return WPA_ERR_MIC;
        if (s->replay_ok && memcmp(in + K_REPLAY, s->replay, 8) <= 0) return WPA_ERR_REPLAY;
    }

    if ((info & EAPOL_KI_COPPIA) && !(info & EAPOL_KI_MIC)) {
        /* --- message 1 of 4: derive the PTK, answer with SNonce and our element */
        memcpy(s->anonce, in + K_NONCE, 32);
        wpa_ptk(s->pmk, s->aa, s->spa, s->anonce, s->snonce, s->cifrario, &s->ptk);
        s->ptk_ok = 1;
        *out_n = risposta(s, in, versione | EAPOL_KI_COPPIA | EAPOL_KI_MIC,
                          s->snonce, s->ie, s->ie_n, out);
        return WPA_RISPONDI;
    }

    if (info & EAPOL_KI_COPPIA) {
        /* --- message 3 of 4 */
        if (memcmp(in + K_NONCE, s->anonce, 32) != 0) return WPA_ERR_NONCE;
        memcpy(s->replay, in + K_REPLAY, 8);
        s->replay_ok = 1;
        if (s->rsn && (info & EAPOL_KI_CIFRATI) && dlung) {
            if (dati_in_chiaro(s, versione, in, in + K_DATI, dlung, chiaro, &chiaro_n) != 0)
                return WPA_ERR_CHIAVE;
            if (cerca_gtk(s, chiaro, chiaro_n) == 0) memcpy(s->gtk_rsc, in + K_RSC, 8);
            memset(chiaro, 0, sizeof(chiaro));
        }
        s->coppia_pronta = 1;
        *out_n = risposta(s, in, versione | EAPOL_KI_COPPIA | EAPOL_KI_MIC |
                          (s->rsn ? EAPOL_KI_SICURO : 0), 0, 0, 0, out);
        return WPA_COPPIA;
    }

    /* --- group message 1 of 2 */
    if (!(info & EAPOL_KI_MIC) || !dlung) return WPA_ERR_FORMA;
    memcpy(s->replay, in + K_REPLAY, 8);
    s->replay_ok = 1;
    if (dati_in_chiaro(s, versione, in, in + K_DATI, dlung, chiaro, &chiaro_n) != 0)
        return WPA_ERR_CHIAVE;
    if (s->rsn) {
        if (cerca_gtk(s, chiaro, chiaro_n) != 0) { memset(chiaro, 0, sizeof(chiaro)); return WPA_ERR_CHIAVE; }
    } else {
        unsigned int l = be16(in + K_LUNG);                 /* WPA: the key data IS the key */

        if (l == 0 || l > sizeof(s->gtk) || l > chiaro_n) { memset(chiaro, 0, sizeof(chiaro)); return WPA_ERR_CHIAVE; }
        memcpy(s->gtk, chiaro, l);
        s->gtk_n = l;
        s->gtk_indice = (int)((info & EAPOL_KI_INDICE) >> 4);
        s->gtk_pronta = 1;
    }
    memset(chiaro, 0, sizeof(chiaro));
    memcpy(s->gtk_rsc, in + K_RSC, 8);
    *out_n = risposta(s, in, versione | EAPOL_KI_MIC | EAPOL_KI_SICURO |
                      (s->rsn ? 0 : (info & EAPOL_KI_INDICE)), 0, 0, 0, out);
    return WPA_GRUPPO;
}

/* =============================================================================
 * CCMP
 * ============================================================================= */
/* the parts of a MAC header CCMP cares about; 0 if the header is too short */
static unsigned int testa_misura(const unsigned char *hdr, unsigned int hdr_n, int *qos, int *a4)
{
    unsigned int n = 24, tipo = (hdr[0] >> 2) & 3, sotto = hdr[0] >> 4;

    *a4 = (hdr[1] & 3) == 3;
    *qos = tipo == 2 && (sotto & 8);
    if (*a4) n += 6;
    if (*qos) n += 2;
    return hdr_n >= n ? n : 0;
}

static int ccmp_preparo(const unsigned char *hdr, unsigned int hdr_n, const unsigned char pn[6],
                        unsigned char aad[30], unsigned int *aad_n, unsigned char nonce[13])
{
    int          qos, a4;
    unsigned int n = testa_misura(hdr, hdr_n, &qos, &a4), k = 0;

    if (!n) return -1;
    aad[k++] = ((hdr[0] >> 2) & 3) == 2 ? (unsigned char)(hdr[0] & 0x8F) : hdr[0];
    aad[k] = (unsigned char)((hdr[1] & 0xC7) | 0x40);
    if (qos) aad[k] &= 0x7F;
    k++;
    memcpy(aad + k, hdr + 4, 18); k += 18;                  /* A1 A2 A3 */
    aad[k++] = hdr[22] & 0x0F;                              /* fragment number only */
    aad[k++] = 0;
    if (a4) { memcpy(aad + k, hdr + 24, 6); k += 6; }
    if (qos) {
        aad[k++] = hdr[a4 ? 30 : 24] & 0x0F;
        aad[k++] = 0;
    }
    *aad_n = k;
    nonce[0] = qos ? (unsigned char)(hdr[a4 ? 30 : 24] & 0x0F) : 0;
    memcpy(nonce + 1, hdr + 10, 6);                         /* A2 */
    nonce[7] = pn[5]; nonce[8] = pn[4]; nonce[9] = pn[3];
    nonce[10] = pn[2]; nonce[11] = pn[1]; nonce[12] = pn[0];
    return 0;
}

int ccmp_cifra(const unsigned char tk[16], const unsigned char *hdr, unsigned int hdr_n,
               unsigned long long pn, int indice,
               const unsigned char *in, unsigned int n, unsigned char *out)
{
    AesChiave     c;
    unsigned char p[6], aad[30], nonce[13];
    unsigned int  aad_n, i;

    for (i = 0; i < 6; i++) p[i] = (unsigned char)(pn >> (8 * i));
    if (ccmp_preparo(hdr, hdr_n, p, aad, &aad_n, nonce) != 0) return -1;
    out[0] = p[0]; out[1] = p[1]; out[2] = 0;
    out[3] = (unsigned char)(0x20 | ((indice & 3) << 6));
    out[4] = p[2]; out[5] = p[3]; out[6] = p[4]; out[7] = p[5];
    aes_chiave(&c, tk, 16);
    aes_ccm_cifra(&c, nonce, 13, aad, aad_n, in, out + 8, n, out + 8 + n, 8);
    memset(&c, 0, sizeof(c));
    return (int)n + 16;
}

int ccmp_decifra(const unsigned char tk[16], const unsigned char *hdr, unsigned int hdr_n,
                 const unsigned char *in, unsigned int in_n, unsigned char *out,
                 unsigned long long *pn)
{
    AesChiave     c;
    unsigned char p[6], aad[30], nonce[13];
    unsigned int  aad_n, n, i;
    int           r;

    if (in_n < 16 || !(in[3] & 0x20)) return -1;
    p[0] = in[0]; p[1] = in[1]; p[2] = in[4]; p[3] = in[5]; p[4] = in[6]; p[5] = in[7];
    if (ccmp_preparo(hdr, hdr_n, p, aad, &aad_n, nonce) != 0) return -1;
    n = in_n - 16;
    aes_chiave(&c, tk, 16);
    r = aes_ccm_decifra(&c, nonce, 13, aad, aad_n, in + 8, out, n, in + 8 + n, 8);
    memset(&c, 0, sizeof(c));
    if (r != 0) return -1;
    if (pn) { *pn = 0; for (i = 0; i < 6; i++) *pn |= (unsigned long long)p[i] << (8 * i); }
    return (int)n;
}

/* =============================================================================
 * TKIP: key mixing (11.4.2.5), Michael (11.4.2.3)
 *
 * The 16-bit S-box of the key mixing is the AES S-box seen through
 * MixColumns: entry i = (2*S[i]) << 8 | 3*S[i]. It is computed here from the
 * AES S-box, itself computed (inverse in GF(2^8), then the affine map), so
 * that no 512-entry table has to be copied by hand.
 * ============================================================================= */
static unsigned short g_tkip_s[256];
static int            g_tkip_s_ok = 0;

static unsigned char xtime(unsigned char x) { return (unsigned char)((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }

static void tkip_sbox(void)
{
    unsigned char p = 1, q = 1, s;
    unsigned char sb[256];
    int           i;

    if (g_tkip_s_ok) return;
    /* p runs over the powers of 3, q over those of 3^-1: q = p^-1 */
    do {
        p = (unsigned char)(p ^ xtime(p));
        q ^= (unsigned char)(q << 1); q ^= (unsigned char)(q << 2); q ^= (unsigned char)(q << 4);
        if (q & 0x80) q ^= 0x09;
        s = (unsigned char)(q ^ ((q << 1) | (q >> 7)) ^ ((q << 2) | (q >> 6)) ^
                            ((q << 3) | (q >> 5)) ^ ((q << 4) | (q >> 4)));
        sb[p] = (unsigned char)(s ^ 0x63);
    } while (p != 1);
    sb[0] = 0x63;
    for (i = 0; i < 256; i++) {
        unsigned char d = xtime(sb[i]);
        g_tkip_s[i] = (unsigned short)((d << 8) | (unsigned char)(d ^ sb[i]));
    }
    g_tkip_s_ok = 1;
}

static unsigned short tS(unsigned short v)
{
    unsigned short a = g_tkip_s[v & 0xFF], b = g_tkip_s[v >> 8];

    return (unsigned short)(a ^ (unsigned short)((b >> 8) | (b << 8)));
}

#define MK16(hi, lo) ((unsigned short)(((hi) << 8) | (lo)))
#define ROTR1(v)     ((unsigned short)(((v) >> 1) | ((v) << 15)))

void tkip_chiave_rc4(const unsigned char tk[16], const unsigned char ta[6],
                     unsigned int iv32, unsigned int iv16, unsigned char out[16])
{
    unsigned short t[5], p[6];
    int            i, j;

    tkip_sbox();
    /* phase 1: the transmitter and the upper 32 bits of the counter */
    t[0] = (unsigned short)iv32;
    t[1] = (unsigned short)(iv32 >> 16);
    t[2] = MK16(ta[1], ta[0]);
    t[3] = MK16(ta[3], ta[2]);
    t[4] = MK16(ta[5], ta[4]);
    for (i = 0; i < 8; i++) {
        j = 2 * (i & 1);
        t[0] = (unsigned short)(t[0] + tS((unsigned short)(t[4] ^ MK16(tk[1 + j], tk[0 + j]))));
        t[1] = (unsigned short)(t[1] + tS((unsigned short)(t[0] ^ MK16(tk[5 + j], tk[4 + j]))));
        t[2] = (unsigned short)(t[2] + tS((unsigned short)(t[1] ^ MK16(tk[9 + j], tk[8 + j]))));
        t[3] = (unsigned short)(t[3] + tS((unsigned short)(t[2] ^ MK16(tk[13 + j], tk[12 + j]))));
        t[4] = (unsigned short)(t[4] + tS((unsigned short)(t[3] ^ MK16(tk[1 + j], tk[0 + j]))) + i);
    }
    /* phase 2: the lower 16 bits */
    for (i = 0; i < 5; i++) p[i] = t[i];
    p[5] = (unsigned short)(t[4] + iv16);
    p[0] = (unsigned short)(p[0] + tS((unsigned short)(p[5] ^ MK16(tk[1], tk[0]))));
    p[1] = (unsigned short)(p[1] + tS((unsigned short)(p[0] ^ MK16(tk[3], tk[2]))));
    p[2] = (unsigned short)(p[2] + tS((unsigned short)(p[1] ^ MK16(tk[5], tk[4]))));
    p[3] = (unsigned short)(p[3] + tS((unsigned short)(p[2] ^ MK16(tk[7], tk[6]))));
    p[4] = (unsigned short)(p[4] + tS((unsigned short)(p[3] ^ MK16(tk[9], tk[8]))));
    p[5] = (unsigned short)(p[5] + tS((unsigned short)(p[4] ^ MK16(tk[11], tk[10]))));
    p[0] = (unsigned short)(p[0] + ROTR1((unsigned short)(p[5] ^ MK16(tk[13], tk[12]))));
    p[1] = (unsigned short)(p[1] + ROTR1((unsigned short)(p[0] ^ MK16(tk[15], tk[14]))));
    p[2] = (unsigned short)(p[2] + ROTR1(p[1]));
    p[3] = (unsigned short)(p[3] + ROTR1(p[2]));
    p[4] = (unsigned short)(p[4] + ROTR1(p[3]));
    p[5] = (unsigned short)(p[5] + ROTR1(p[4]));
    out[0] = (unsigned char)(iv16 >> 8);
    out[1] = (unsigned char)(((iv16 >> 8) | 0x20) & 0x7F);
    out[2] = (unsigned char)iv16;
    out[3] = (unsigned char)((unsigned short)(p[5] ^ MK16(tk[1], tk[0])) >> 1);
    for (i = 0; i < 6; i++) {
        out[4 + 2 * i] = (unsigned char)p[i];
        out[5 + 2 * i] = (unsigned char)(p[i] >> 8);
    }
}

typedef struct { unsigned int l, r, m; unsigned int k; } Michael;

static void michael_blocco(Michael *x, unsigned int m)
{
    unsigned int l = x->l ^ m, r = x->r;

    r ^= rotl32(l, 17); l += r;
    r ^= ((l & 0xFF00FF00u) >> 8) | ((l & 0x00FF00FFu) << 8); l += r;
    r ^= rotl32(l, 3); l += r;
    r ^= (l >> 2) | (l << 30); l += r;
    x->l = l; x->r = r;
}

static void michael_byte(Michael *x, const unsigned char *b, unsigned int n)
{
    unsigned int i;

    for (i = 0; i < n; i++) {
        x->m |= (unsigned int)b[i] << (8 * x->k);
        if (++x->k == 4) { michael_blocco(x, x->m); x->m = 0; x->k = 0; }
    }
}

static void michael_avvio(Michael *x, const unsigned char c[8])
{
    x->l = (unsigned int)c[0] | ((unsigned int)c[1] << 8) | ((unsigned int)c[2] << 16) | ((unsigned int)c[3] << 24);
    x->r = (unsigned int)c[4] | ((unsigned int)c[5] << 8) | ((unsigned int)c[6] << 16) | ((unsigned int)c[7] << 24);
    x->m = 0; x->k = 0;
}

static void michael_fine(Michael *x, unsigned char mic[8])
{
    static const unsigned char coda[8] = { 0x5a, 0, 0, 0, 0, 0, 0, 0 };
    int i;

    michael_byte(x, coda, 5);                   /* 0x5a and at least four zeros */
    while (x->k) michael_byte(x, coda + 1, 1);
    for (i = 0; i < 4; i++) { mic[i] = (unsigned char)(x->l >> (8 * i)); mic[4 + i] = (unsigned char)(x->r >> (8 * i)); }
}

void michael(const unsigned char chiave[8], const unsigned char *m, unsigned int n, unsigned char mic[8])
{
    Michael x;

    michael_avvio(&x, chiave);
    michael_byte(&x, m, n);
    michael_fine(&x, mic);
}

static void michael_msdu(const unsigned char chiave[8], const unsigned char da[6], const unsigned char sa[6],
                         int priorita, const unsigned char *m, unsigned int n, unsigned char mic[8])
{
    Michael       x;
    unsigned char p[4] = { 0, 0, 0, 0 };

    p[0] = (unsigned char)(priorita & 0x0F);
    michael_avvio(&x, chiave);
    michael_byte(&x, da, 6);
    michael_byte(&x, sa, 6);
    michael_byte(&x, p, 4);
    michael_byte(&x, m, n);
    michael_fine(&x, mic);
}

static void icv_metti(unsigned char *p, unsigned int n)
{
    unsigned int c = crc32_ieee(p, n);

    p[n] = (unsigned char)c; p[n + 1] = (unsigned char)(c >> 8);
    p[n + 2] = (unsigned char)(c >> 16); p[n + 3] = (unsigned char)(c >> 24);
}

static int icv_giusto(const unsigned char *p, unsigned int n)
{
    unsigned int c = crc32_ieee(p, n);

    return p[n] == (unsigned char)c && p[n + 1] == (unsigned char)(c >> 8) &&
           p[n + 2] == (unsigned char)(c >> 16) && p[n + 3] == (unsigned char)(c >> 24);
}

int tkip_cifra(const unsigned char tk[16], const unsigned char mic[8],
               const unsigned char ta[6], const unsigned char da[6],
               const unsigned char sa[6], int priorita, unsigned long long tsc,
               int indice, const unsigned char *in, unsigned int n, unsigned char *out)
{
    unsigned char k[16];
    unsigned int  iv16 = (unsigned int)(tsc & 0xFFFF), iv32 = (unsigned int)(tsc >> 16);

    tkip_chiave_rc4(tk, ta, iv32, iv16, k);
    out[0] = k[0]; out[1] = k[1]; out[2] = k[2];
    out[3] = (unsigned char)(0x20 | ((indice & 3) << 6));
    out[4] = (unsigned char)iv32; out[5] = (unsigned char)(iv32 >> 8);
    out[6] = (unsigned char)(iv32 >> 16); out[7] = (unsigned char)(iv32 >> 24);
    memcpy(out + 8, in, n);
    michael_msdu(mic, da, sa, priorita, in, n, out + 8 + n);
    icv_metti(out + 8, n + 8);
    rc4(k, 16, 0, out + 8, out + 8, n + 12);
    memset(k, 0, sizeof(k));
    return (int)n + 20;
}

int tkip_decifra(const unsigned char tk[16], const unsigned char mic[8],
                 const unsigned char ta[6], const unsigned char da[6],
                 const unsigned char sa[6], int priorita,
                 const unsigned char *in, unsigned int in_n, unsigned char *out,
                 unsigned long long *tsc)
{
    unsigned char k[16], m[8];
    unsigned int  iv16, iv32, n, i, diverso = 0;

    if (in_n < 20 || !(in[3] & 0x20)) return -1;
    iv16 = ((unsigned int)in[0] << 8) | in[2];
    iv32 = (unsigned int)in[4] | ((unsigned int)in[5] << 8) | ((unsigned int)in[6] << 16) | ((unsigned int)in[7] << 24);
    n = in_n - 20;
    tkip_chiave_rc4(tk, ta, iv32, iv16, k);
    rc4(k, 16, 0, in + 8, out, n + 12);
    memset(k, 0, sizeof(k));
    if (!icv_giusto(out, n + 8)) { memset(out, 0, n + 12); return -1; }
    michael_msdu(mic, da, sa, priorita, out, n, m);
    for (i = 0; i < 8; i++) diverso |= m[i] ^ out[n + i];
    if (diverso) { memset(out, 0, n + 12); return -2; }
    if (tsc) *tsc = ((unsigned long long)iv32 << 16) | iv16;
    return (int)n;
}

/* =============================================================================
 * WEP
 * ============================================================================= */
int wep_cifra(const unsigned char *chiave, unsigned int chiave_n,
              const unsigned char iv[3], int indice,
              const unsigned char *in, unsigned int n, unsigned char *out)
{
    unsigned char k[16];

    if (chiave_n != 5 && chiave_n != 13) return -1;
    memcpy(k, iv, 3);
    memcpy(k + 3, chiave, chiave_n);
    memcpy(out, iv, 3);
    out[3] = (unsigned char)((indice & 3) << 6);
    memcpy(out + 4, in, n);
    icv_metti(out + 4, n);
    rc4(k, 3 + chiave_n, 0, out + 4, out + 4, n + 4);
    memset(k, 0, sizeof(k));
    return (int)n + 8;
}

int wep_decifra(const unsigned char *chiave, unsigned int chiave_n,
                const unsigned char *in, unsigned int in_n, unsigned char *out)
{
    unsigned char k[16];
    unsigned int  n;

    if ((chiave_n != 5 && chiave_n != 13) || in_n < 8) return -1;
    n = in_n - 8;
    memcpy(k, in, 3);
    memcpy(k + 3, chiave, chiave_n);
    rc4(k, 3 + chiave_n, 0, in + 4, out, n + 4);
    memset(k, 0, sizeof(k));
    if (!icv_giusto(out, n)) { memset(out, 0, n + 4); return -1; }
    return (int)n;
}
