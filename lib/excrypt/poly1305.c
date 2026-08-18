/* =============================================================================
 * lib/excrypt/poly1305.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Poly1305 — RFC 8439
 *
 * ! CIFRARE NON BASTA, E QUESTA E' LA META' CHE SI DIMENTICA. ChaCha20 nasconde
 * il contenuto ma non impedisce di CAMBIARLO: chi sta in mezzo puo' invertire
 * un bit del testo cifrato e invertire lo stesso bit del testo in chiaro, senza
 * conoscerlo. Poly1305 e' l'impronta che rende quel cambiamento visibile.
 *
 * ! E LA CHIAVE E' USA E GETTA, per costruzione. Poly1305 non e' un MAC che si
 * puo' usare due volte con la stessa chiave: due messaggi autenticati con lo
 * stesso `r` permettono di ricavarlo e di falsificare tutto. Per questo la
 * chiave la genera ChaCha20 dal contatore zero, una per pacchetto.
 *
 * ! IL NUMERO PRIMO E' 2^130 - 5, e i conti si fanno a pezzi da 26 bit su
 * numeri a 32 bit: cinque pezzi coprono i 130 bit, e i prodotti parziali stanno
 * in 64 bit senza traboccare.
 * ============================================================================= */

#include "excrypt.h"

typedef unsigned long long u64;

static unsigned int le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

void poly1305(const unsigned char chiave[32], const unsigned char *m,
              unsigned int n, unsigned char out[16])
{
    unsigned int r0, r1, r2, r3, r4;
    unsigned int s1, s2, s3, s4;
    unsigned int h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;
    unsigned int c;
    u64 d0, d1, d2, d3, d4;
    u64 f;
    unsigned int g0, g1, g2, g3, g4;
    unsigned int mask;

    /* ! `r` SI POTA: certi bit vengono azzerati prima dell'uso, ed e' quello
     * che tiene i prodotti dentro i 64 bit. Non e' una debolezza scelta per
     * comodita' — e' parte della definizione, e chi non lo fa ottiene numeri
     * giusti su alcuni messaggi e sbagliati su altri. */
    r0 = (le32(chiave     )     ) & 0x3ffffff;
    r1 = (le32(chiave +  3) >> 2) & 0x3ffff03;
    r2 = (le32(chiave +  6) >> 4) & 0x3ffc0ff;
    r3 = (le32(chiave +  9) >> 6) & 0x3f03fff;
    r4 = (le32(chiave + 12) >> 8) & 0x00fffff;

    /* I multipli di 5 servono alla riduzione modulo 2^130-5: un pezzo che esce
     * dall'alto rientra dal basso moltiplicato per 5. */
    s1 = r1 * 5; s2 = r2 * 5; s3 = r3 * 5; s4 = r4 * 5;

    while (n > 0) {
        unsigned char blocco[16];
        unsigned int  q = (n < 16) ? n : 16;
        unsigned int  i;

        for (i = 0; i < q; i++)  blocco[i] = m[i];

        /* ! IL BIT IN CIMA AL BLOCCO NON E' RIEMPIMENTO: distingue un messaggio
         * che finisce con degli zeri da uno piu' corto. Senza, «ab» e
         * «ab\0\0» darebbero la stessa impronta. Su un blocco parziale il bit
         * va subito dopo i byte veri, non in fondo ai sedici. */
        /* ! IL BIT VA SCRITTO SOLO SU UN BLOCCO PARZIALE, e la prima stesura
         * lo scriveva sempre: con q = 16 quel `blocco[q]` cade UN BYTE OLTRE il
         * vettore. A terra era passato — il banco di prova non girava sotto
         * sanitizzatore — e l'ha trovato il canarino dello stack di EX-OS alla
         * prima esecuzione sulla macchina vera. Su un blocco pieno il bit
         * arriva dall'altra strada, quella che somma 1<<24 a h4. */
        if (q < 16) {
            blocco[q] = 1;
            for (i = q + 1; i < 16; i++) blocco[i] = 0;
        }

        h0 += (le32(blocco     )     ) & 0x3ffffff;
        h1 += (le32(blocco +  3) >> 2) & 0x3ffffff;
        h2 += (le32(blocco +  6) >> 4) & 0x3ffffff;
        h3 += (le32(blocco +  9) >> 6) & 0x3ffffff;
        h4 += (le32(blocco + 12) >> 8) | ((q == 16) ? (1u << 24) : 0u);

        d0 = (u64)h0*r0 + (u64)h1*s4 + (u64)h2*s3 + (u64)h3*s2 + (u64)h4*s1;
        d1 = (u64)h0*r1 + (u64)h1*r0 + (u64)h2*s4 + (u64)h3*s3 + (u64)h4*s2;
        d2 = (u64)h0*r2 + (u64)h1*r1 + (u64)h2*r0 + (u64)h3*s4 + (u64)h4*s3;
        d3 = (u64)h0*r3 + (u64)h1*r2 + (u64)h2*r1 + (u64)h3*r0 + (u64)h4*s4;
        d4 = (u64)h0*r4 + (u64)h1*r3 + (u64)h2*r2 + (u64)h3*r1 + (u64)h4*r0;

        c = (unsigned int)(d0 >> 26); h0 = (unsigned int)d0 & 0x3ffffff;
        d1 += c; c = (unsigned int)(d1 >> 26); h1 = (unsigned int)d1 & 0x3ffffff;
        d2 += c; c = (unsigned int)(d2 >> 26); h2 = (unsigned int)d2 & 0x3ffffff;
        d3 += c; c = (unsigned int)(d3 >> 26); h3 = (unsigned int)d3 & 0x3ffffff;
        d4 += c; c = (unsigned int)(d4 >> 26); h4 = (unsigned int)d4 & 0x3ffffff;
        h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
        h1 += c;

        m += q;
        n -= q;
    }

    /* Riporto finale. */
    c = h1 >> 26; h1 &= 0x3ffffff;
    h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
    h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
    h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff;
    h1 += c;

    /* ! SI CALCOLA h - p E SI SCEGLIE SENZA GUARDARE, con una maschera invece
     * che con un `if`: un ramo che dipende dal valore dell'impronta e' un ramo
     * che si puo' cronometrare. Costa due righe in piu' e toglie un canale. */
    g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
    g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
    g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
    g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
    g4 = h4 + c - (1u << 26);

    mask = (g4 >> 31) - 1;      /* tutti uno se g4 non e' negativo */
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;
    h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    /* Da cinque pezzi da 26 bit a quattro parole da 32. */
    h0 = (h0      ) | (h1 << 26);
    h1 = (h1 >>  6) | (h2 << 20);
    h2 = (h2 >> 12) | (h3 << 14);
    h3 = (h3 >> 18) | (h4 <<  8);

    /* E infine si somma `s`, la seconda meta' della chiave. */
    f = (u64)h0 + le32(chiave + 16);            h0 = (unsigned int)f;
    f = (u64)h1 + le32(chiave + 20) + (f >> 32); h1 = (unsigned int)f;
    f = (u64)h2 + le32(chiave + 24) + (f >> 32); h2 = (unsigned int)f;
    f = (u64)h3 + le32(chiave + 28) + (f >> 32); h3 = (unsigned int)f;

    out[ 0] = (unsigned char)(h0      ); out[ 1] = (unsigned char)(h0 >>  8);
    out[ 2] = (unsigned char)(h0 >> 16); out[ 3] = (unsigned char)(h0 >> 24);
    out[ 4] = (unsigned char)(h1      ); out[ 5] = (unsigned char)(h1 >>  8);
    out[ 6] = (unsigned char)(h1 >> 16); out[ 7] = (unsigned char)(h1 >> 24);
    out[ 8] = (unsigned char)(h2      ); out[ 9] = (unsigned char)(h2 >>  8);
    out[10] = (unsigned char)(h2 >> 16); out[11] = (unsigned char)(h2 >> 24);
    out[12] = (unsigned char)(h3      ); out[13] = (unsigned char)(h3 >>  8);
    out[14] = (unsigned char)(h3 >> 16); out[15] = (unsigned char)(h3 >> 24);
}

/* ! IL CONFRONTO DELLE IMPRONTE NON PUO' FERMARSI AL PRIMO BYTE DIVERSO. Un
 * memcmp che esce presto dice, col tempo che impiega, quanti byte erano giusti:
 * chi prova puo' indovinare l'impronta un byte per volta invece che tutta
 * insieme — 256 tentativi per byte al posto di 2^128. Qui si guardano sempre
 * tutti e sedici. */
int poly1305_uguali(const unsigned char a[16], const unsigned char b[16])
{
    unsigned char d = 0;
    int i;

    for (i = 0; i < 16; i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}
