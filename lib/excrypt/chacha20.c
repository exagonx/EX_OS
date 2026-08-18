/* =============================================================================
 * lib/excrypt/chacha20.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ChaCha20 — RFC 8439
 *
 * ! E' STATO SCELTO AL POSTO DI AES PER UNA RAGIONE CHE RIGUARDA QUESTA
 * MACCHINA: AES veloce vuole le istruzioni AES-NI, che un Pentium non ha; AES
 * in software vuole tabelle in memoria, e le tabelle in memoria sono la strada
 * per cui si e' scoperto che la cache puo' raccontare la chiave. ChaCha20 e'
 * somme, XOR e rotazioni su venti parole a 32 bit: nessuna tabella, nessun
 * ramo che dipenda dalla chiave, e va bene su un processore senza niente.
 *
 * ! IL TEMPO NON DIPENDE DAI DATI, ED E' UNA PROPRIETA', NON UN CASO. Non ci
 * sono `if` che guardino la chiave o il testo: due messaggi diversi impiegano
 * lo stesso numero di istruzioni. E' cio' che rende inutile misurare quanto ci
 * mette.
 * ============================================================================= */

#include "excrypt.h"

static unsigned int rotl(unsigned int v, int n)
{
    return (v << n) | (v >> (32 - n));
}

/* Il quarto di giro: quattro somme, quattro XOR, quattro rotazioni. E' tutto
 * l'algoritmo — il resto e' ripeterlo. */
#define QR(a, b, c, d)                       \
    a += b; d ^= a; d = rotl(d, 16);         \
    c += d; b ^= c; b = rotl(b, 12);         \
    a += b; d ^= a; d = rotl(d,  8);         \
    c += d; b ^= c; b = rotl(b,  7)

static unsigned int le32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static void metti32(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* Un blocco di 64 byte di flusso, dato chiave, contatore e nonce. */
void chacha20_blocco(const unsigned char chiave[32], unsigned int contatore,
                     const unsigned char nonce[12], unsigned char fuori[64])
{
    /* ! LA COSTANTE E' «expand 32-byte k», e non e' decorativa: fa si' che lo
     * stato di partenza non sia mai tutto sotto il controllo di chi attacca. */
    static const unsigned int C[4] = {
        0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u
    };
    unsigned int s[16], x[16];
    int i;

    for (i = 0; i < 4; i++)  s[i]      = C[i];
    for (i = 0; i < 8; i++)  s[4 + i]  = le32(chiave + i * 4);
    s[12] = contatore;
    for (i = 0; i < 3; i++)  s[13 + i] = le32(nonce + i * 4);

    for (i = 0; i < 16; i++) x[i] = s[i];

    /* Venti giri: dieci di colonne e dieci di diagonali, alternati. */
    for (i = 0; i < 10; i++) {
        QR(x[0], x[4], x[ 8], x[12]);
        QR(x[1], x[5], x[ 9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);

        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[ 8], x[13]);
        QR(x[3], x[4], x[ 9], x[14]);
    }

    /* ! LO STATO DI PARTENZA SI RISOMMA ALLA FINE, e senza quella somma i venti
     * giri sarebbero invertibili: chi conosce l'uscita risalirebbe alla chiave
     * ripercorrendoli al contrario. E' la riga che trasforma una permutazione
     * in una funzione a senso unico. */
    for (i = 0; i < 16; i++) metti32(fuori + i * 4, x[i] + s[i]);
}

void chacha20(const unsigned char chiave[32], unsigned int contatore,
              const unsigned char nonce[12],
              const unsigned char *in, unsigned char *out, unsigned int n)
{
    unsigned char blocco[64];
    unsigned int  i, fatti = 0;

    while (fatti < n) {
        unsigned int q = n - fatti;

        if (q > 64) q = 64;
        chacha20_blocco(chiave, contatore, nonce, blocco);

        for (i = 0; i < q; i++) out[fatti + i] = in[fatti + i] ^ blocco[i];

        fatti += q;
        contatore++;
    }
}
