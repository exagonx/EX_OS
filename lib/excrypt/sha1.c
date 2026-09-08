/* =============================================================================
 * lib/excrypt/sha1.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * SHA-1 (RFC 3174), HMAC-SHA1 e PBKDF2.
 *
 * ! SHA-1 E' ROTTO, E LO SI SCRIVE LO STESSO. Le collisioni si comprano dal
 * 2017 e lib/excert lo RIFIUTA nelle firme dei certificati — sta scritto li'
 * col perche'. Qui serve per un uso diverso: WPA e WPA2 derivano le chiavi con
 * PBKDF2-HMAC-SHA1 e con un PRF costruito su HMAC-SHA1, e quella scelta l'ha
 * fatta lo standard nel 2004. Non si puo' cambiare da un lato solo: o si parla
 * la lingua dell'access point, o non ci si connette.
 *
 * ! E LA DIFFERENZA FRA I DUE USI E' TUTTA. Una collisione serve a far passare
 * un documento per un altro — ed e' il caso della firma di un certificato.
 * Dentro HMAC, SHA-1 e' usato come funzione a chiave, e li' le collisioni non
 * aiutano: HMAC-SHA1 non e' rotto. Percio' rifiutarlo nei certificati e
 * usarlo qui non e' un'incoerenza, sono due domande diverse.
 * ============================================================================= */

#include "excrypt.h"

static unsigned int ruota(unsigned int x, unsigned int n)
{
    return (x << n) | (x >> (32 - n));
}

static void sha1_blocco(unsigned int h[5], const unsigned char b[64])
{
    unsigned int w[80], a, bb, c, d, e, i;

    for (i = 0; i < 16; i++)
        w[i] = ((unsigned int)b[i*4] << 24) | ((unsigned int)b[i*4+1] << 16) |
               ((unsigned int)b[i*4+2] << 8) | (unsigned int)b[i*4+3];
    for (i = 16; i < 80; i++)
        w[i] = ruota(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    a = h[0]; bb = h[1]; c = h[2]; d = h[3]; e = h[4];

    for (i = 0; i < 80; i++) {
        unsigned int f, k, t;

        if (i < 20)      { f = (bb & c) | ((~bb) & d);        k = 0x5A827999u; }
        else if (i < 40) { f = bb ^ c ^ d;                    k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = bb ^ c ^ d;                    k = 0xCA62C1D6u; }

        t = ruota(a, 5) + f + e + k + w[i];
        e = d; d = c; c = ruota(bb, 30); bb = a; a = t;
    }

    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
}

void sha1(const void *dati, unsigned int len, unsigned char out[20])
{
    const unsigned char *p = (const unsigned char *)dati;
    unsigned int h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu,
                          0x10325476u, 0xC3D2E1F0u };
    unsigned char coda[128];
    unsigned int  i, resto, n_coda;
    unsigned long long bit = (unsigned long long)len * 8ull;

    for (i = 0; i + 64 <= len; i += 64) sha1_blocco(h, p + i);

    resto = len - i;
    for (n_coda = 0; n_coda < resto; n_coda++) coda[n_coda] = p[i + n_coda];
    coda[n_coda++] = 0x80;

    /* ! IL RIEMPIMENTO ARRIVA A 56 MOD 64, NON A 64: gli ultimi otto byte sono
     * la lunghezza in bit. Fermarsi a 64 da' un'impronta che sembra giusta e
     * non lo e' per nessuno tranne che per se stessi. */
    while ((n_coda % 64) != 56) coda[n_coda++] = 0;

    for (i = 0; i < 8; i++)
        coda[n_coda++] = (unsigned char)((bit >> (56 - 8 * i)) & 0xFF);

    for (i = 0; i < n_coda; i += 64) sha1_blocco(h, coda + i);

    for (i = 0; i < 5; i++) {
        out[i*4]     = (unsigned char)(h[i] >> 24);
        out[i*4 + 1] = (unsigned char)(h[i] >> 16);
        out[i*4 + 2] = (unsigned char)(h[i] >> 8);
        out[i*4 + 3] = (unsigned char)(h[i]);
    }
}

/* HMAC-SHA1 (RFC 2104).
 *
 * ! UNA CHIAVE PIU' LUNGA DEL BLOCCO SI ACCORCIA CON L'IMPRONTA, non si
 * tronca. Troncarla darebbe un HMAC diverso da quello di tutti gli altri, e la
 * differenza si vede solo con una chiave lunga — cioe' quasi mai in prova e
 * subito in produzione. */
void hmac_sha1(const unsigned char *chiave, unsigned int chiave_n,
               const unsigned char *m, unsigned int m_n,
               unsigned char out[20])
{
    unsigned char k[64], dentro[64], fuori[64], imp[20];
    unsigned char buf[64 + 8192];
    unsigned int  i, n;

    for (i = 0; i < 64; i++) k[i] = 0;
    if (chiave_n > 64) {
        sha1(chiave, chiave_n, imp);
        for (i = 0; i < 20; i++) k[i] = imp[i];
    } else {
        for (i = 0; i < chiave_n; i++) k[i] = chiave[i];
    }

    for (i = 0; i < 64; i++) {
        dentro[i] = (unsigned char)(k[i] ^ 0x36);
        fuori[i]  = (unsigned char)(k[i] ^ 0x5C);
    }

    /* ! IL MESSAGGIO SI COPIA IN UN BUFFER, ED E' IL LIMITE DI QUESTA
     * VERSIONE: 8 KB. Serve a PBKDF2 e al PRF di WPA, dove il messaggio e'
     * lungo decine di byte. Il giorno che servisse per un flusso, questa
     * funzione va rifatta a stati (init/update/final) invece di allargare il
     * buffer — e la riga qui sotto lo dice invece di troncare in silenzio. */
    if (m_n > 8192) { for (i = 0; i < 20; i++) out[i] = 0; return; }

    for (i = 0; i < 64; i++) buf[i] = dentro[i];
    for (i = 0; i < m_n; i++) buf[64 + i] = m[i];
    sha1(buf, 64 + m_n, imp);

    for (i = 0; i < 64; i++) buf[i] = fuori[i];
    for (i = 0; i < 20; i++) buf[64 + i] = imp[i];
    sha1(buf, 64 + 20, out);
    n = 0; (void)n;
}

/* PBKDF2-HMAC-SHA1 (RFC 2898).
 *
 * ! LE ITERAZIONI SONO IL PUNTO, NON UN PARAMETRO QUALUNQUE. WPA ne chiede
 * 4096 con l'SSID come sale: e' cio' che rende costoso provare le password a
 * tappeto. Abbassarle «per fare prima» non da' un errore: da' una rete che si
 * apre in un pomeriggio invece che in un anno.
 *
 * ! E IL SALE E' L'SSID, non un numero casuale, ed e' una debolezza nota dello
 * standard: due reti che si chiamano uguale hanno lo stesso sale, e per i nomi
 * piu' comuni le tabelle precalcolate esistono da vent'anni. Non si puo'
 * rimediare da questo lato — si puo' solo non chiamare la propria rete
 * «linksys». */
void pbkdf2_sha1(const unsigned char *parola, unsigned int parola_n,
                 const unsigned char *sale, unsigned int sale_n,
                 unsigned int giri, unsigned char *out, unsigned int out_n)
{
    unsigned char blocco[64 + 4];
    unsigned char u[20], t[20];
    unsigned int  i, k, g, fatti = 0, indice = 1;

    if (sale_n > 64) sale_n = 64;

    while (fatti < out_n) {
        unsigned int q = (out_n - fatti < 20) ? (out_n - fatti) : 20;

        for (i = 0; i < sale_n; i++) blocco[i] = sale[i];
        blocco[sale_n]     = (unsigned char)(indice >> 24);
        blocco[sale_n + 1] = (unsigned char)(indice >> 16);
        blocco[sale_n + 2] = (unsigned char)(indice >> 8);
        blocco[sale_n + 3] = (unsigned char)(indice);

        hmac_sha1(parola, parola_n, blocco, sale_n + 4, u);
        for (i = 0; i < 20; i++) t[i] = u[i];

        for (g = 1; g < giri; g++) {
            hmac_sha1(parola, parola_n, u, 20, u);
            for (k = 0; k < 20; k++) t[k] ^= u[k];
        }

        for (i = 0; i < q; i++) out[fatti + i] = t[i];
        fatti += q;
        indice++;
    }
}
