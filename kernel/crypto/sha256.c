/* =============================================================================
 * kernel/crypto/sha256.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * SHA-256 nel kernel — e perche' una copia, invece di riusare quella della libc
 *
 * ! IL KERNEL NON PUO' COLLEGARSI ALLA libc, e questa e' l'unica ragione della
 * copia: la libc gira in ring 3 e questa funzione serve dentro una syscall.
 * Non c'e' un modo di condividerla senza spostare mezzo mondo.
 *
 * ! E SERVE PERCHE' LA VERIFICA DELLA PASSWORD STA QUI. `su` deve poter
 * diventare root, e l'unico modo onesto e' che il kernel controlli la password
 * da se': /boot/ombra e' 0600, cioe' un processo di un utente normale NON PUO'
 * LEGGERLO — ed e' proprio quello il punto. Se la verifica avvenisse in spazio
 * utente bisognerebbe consegnargli qualcosa di quel file, e allora il file
 * potrebbe anche essere pubblico.
 *
 * ! SI E' SCARTATA LA VIA PIU' CORTA — dare a `su` il SALE e farsi rimandare
 * l'impronta gia' calcolata — perche' regala a chi attacca la possibilita' di
 * precalcolare un dizionario per QUEL sale prima ancora di provare. Il sale
 * non e' un segreto, ma darlo via non serve a niente e toglie qualcosa.
 *
 * ! IL CODICE E' LO STESSO DI lib/libc.c, COPIATO E NON RISCRITTO, e i due
 * devono restare identici: `crypttest` prova quello della libc contro i vettori
 * dell'RFC, e la prova vale per tutt'e due solo finche' sono la stessa cosa.
 * ============================================================================= */

#include "kernel.h"
#include "sha256.h"

/* La rotazione a destra, che il C non ha come operatore. */
static uint32_t sha_ruotad(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

void sha256(const void *dati, uint32_t len, uint8_t out[32])
{
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,
        0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
        0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,
        0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,
        0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
        0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,
        0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,
        0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
        0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                      0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    const uint8_t *p = (const uint8_t *)dati;
    uint8_t  blocco[64];
    size_t   resta = len;
    uint64_t bit = (uint64_t)len * 8;
    int      finito = 0;
    size_t   i;

    /* Si lavora un blocco per volta, componendo l'imbottitura sull'ultimo:
     * cosi' non serve una copia dell'intero messaggio. */
    while (!finito) {
        size_t n;

        if (resta >= 64) {
            for (i = 0; i < 64; i++) blocco[i] = *p++;
            resta -= 64;
        } else {
            n = resta;
            for (i = 0; i < n; i++) blocco[i] = *p++;
            blocco[n++] = 0x80;
            if (n > 56) {
                while (n < 64) blocco[n++] = 0;
                /* La lunghezza non ci sta: questo blocco va cosi', il
                 * prossimo e' tutto zeri piu' la lunghezza. */
                resta = (size_t)-1;      /* segnala "manca solo la coda" */
            } else {
                while (n < 56) blocco[n++] = 0;
                for (i = 0; i < 8; i++)
                    blocco[56 + i] = (uint8_t)(bit >> (56 - 8 * i));
                finito = 1;
            }
            if (resta == (size_t)-1) {
                /* niente: il giro successivo emette il blocco di coda */
            } else {
                finito = 1;
            }
        }

        {
            uint32_t w[64], a, b, c, d, e, f, g, hh, t1, t2;
            int      t;

            for (t = 0; t < 16; t++)
                w[t] = ((uint32_t)blocco[t*4] << 24) |
                       ((uint32_t)blocco[t*4+1] << 16) |
                       ((uint32_t)blocco[t*4+2] << 8) |
                       ((uint32_t)blocco[t*4+3]);
            for (t = 16; t < 64; t++) {
                uint32_t s0 = sha_ruotad(w[t-15],7) ^ sha_ruotad(w[t-15],18) ^ (w[t-15] >> 3);
                uint32_t s1 = sha_ruotad(w[t-2],17) ^ sha_ruotad(w[t-2],19) ^ (w[t-2] >> 10);
                w[t] = w[t-16] + s0 + w[t-7] + s1;
            }
            a=h[0]; b=h[1]; c=h[2]; d=h[3]; e=h[4]; f=h[5]; g=h[6]; hh=h[7];
            for (t = 0; t < 64; t++) {
                uint32_t S1 = sha_ruotad(e,6) ^ sha_ruotad(e,11) ^ sha_ruotad(e,25);
                uint32_t ch = (e & f) ^ ((~e) & g);
                uint32_t S0 = sha_ruotad(a,2) ^ sha_ruotad(a,13) ^ sha_ruotad(a,22);
                uint32_t mj = (a & b) ^ (a & c) ^ (b & c);

                t1 = hh + S1 + ch + K[t] + w[t];
                t2 = S0 + mj;
                hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
            }
            h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
            h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
        }

        if (!finito && resta == (size_t)-1) {
            for (i = 0; i < 56; i++) blocco[i] = 0;
            for (i = 0; i < 8; i++)
                blocco[56 + i] = (uint8_t)(bit >> (56 - 8 * i));
            resta = 0;
            finito = 1;
            /* e si rifa' un giro sul blocco appena composto */
            {
                uint32_t w[64], a, b, c, d, e, f, g, hh, t1, t2;
                int      t;

                for (t = 0; t < 16; t++)
                    w[t] = ((uint32_t)blocco[t*4] << 24) |
                           ((uint32_t)blocco[t*4+1] << 16) |
                           ((uint32_t)blocco[t*4+2] << 8) |
                           ((uint32_t)blocco[t*4+3]);
                for (t = 16; t < 64; t++) {
                    uint32_t s0 = sha_ruotad(w[t-15],7) ^ sha_ruotad(w[t-15],18) ^ (w[t-15] >> 3);
                    uint32_t s1 = sha_ruotad(w[t-2],17) ^ sha_ruotad(w[t-2],19) ^ (w[t-2] >> 10);
                    w[t] = w[t-16] + s0 + w[t-7] + s1;
                }
                a=h[0]; b=h[1]; c=h[2]; d=h[3]; e=h[4]; f=h[5]; g=h[6]; hh=h[7];
                for (t = 0; t < 64; t++) {
                    uint32_t S1 = sha_ruotad(e,6) ^ sha_ruotad(e,11) ^ sha_ruotad(e,25);
                    uint32_t ch = (e & f) ^ ((~e) & g);
                    uint32_t S0 = sha_ruotad(a,2) ^ sha_ruotad(a,13) ^ sha_ruotad(a,22);
                    uint32_t mj = (a & b) ^ (a & c) ^ (b & c);

                    t1 = hh + S1 + ch + K[t] + w[t];
                    t2 = S0 + mj;
                    hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
                }
                h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
                h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
            }
        }
    }

    for (i = 0; i < 8; i++) {
        out[i*4]   = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >> 8);
        out[i*4+3] = (uint8_t)(h[i]);
    }
}

/* L'impronta in esadecimale minuscolo, 64 caratteri piu' il terminatore. */
void sha256_esa(const void *dati, uint32_t len, char out[65])
{
    static const char cifre[] = "0123456789abcdef";
    uint8_t d[32];
    int i;

    sha256(dati, len, d);
    for (i = 0; i < 32; i++) {
        out[i*2]   = cifre[d[i] >> 4];
        out[i*2+1] = cifre[d[i] & 0x0F];
    }
    out[64] = '\0';
}