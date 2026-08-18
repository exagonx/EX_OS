/* =============================================================================
 * lib/excrypt/fe25519.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * L'aritmetica sul campo dei numeri modulo 2^255 - 19
 *
 * ! STA IN UN FILE SUO PERCHE' LA USANO IN DUE, e sono due cose diverse:
 * X25519 (lo scambio di chiavi, su curva di Montgomery) ed Ed25519 (la firma,
 * su curva di Edwards). Le curve sono diverse, i numeri sono gli stessi — e due
 * copie della stessa aritmetica divergerebbero al primo aggiustamento, con una
 * meta' del sistema che verifica firme che l'altra meta' non produce.
 *
 * ! I NUMERI HANNO 255 BIT E SI TENGONO IN SEDICI PEZZI DA SEDICI. Non esiste
 * un tipo da 255 bit e non serve: cosi' un prodotto di due cifre sta in 32 bit,
 * e i trentadue prodotti parziali si sommano senza traboccare dentro un intero
 * a 64. E' la ragione per cui i pezzi sono sedici e non quattro da 64.
 *
 * ! IL 19 CHE TORNA OVUNQUE E' IL MODULO. Cio' che esce dalla cima rientra dal
 * fondo moltiplicato per 38, che e' 2 per 19: la cifra che esce vale 2^256,
 * cioe' due volte 2^255.
 *
 * ! E NON C'E' NESSUN RAMO CHE GUARDI I VALORI. Lo scambio condizionale si fa
 * con una maschera: un `if` renderebbe il tempo dipendente dalla chiave, ed e'
 * da li' che si risale a una chiave privata un bit per volta.
 * ============================================================================= */

#include "fe25519.h"

const fe FE_121665 = { 0xDB41, 1 };

void fe_zero(fe a)      { int i; for (i = 0; i < 16; i++) a[i] = 0; }
void fe_uno(fe a)       { int i; fe_zero(a); a[0] = 1; (void)i; }
void fe_copia(fe a, const fe b) { int i; for (i = 0; i < 16; i++) a[i] = b[i]; }

void fe_somma(fe o, const fe a, const fe b)
{
    int i; for (i = 0; i < 16; i++) o[i] = a[i] + b[i];
}

void fe_sottrai(fe o, const fe a, const fe b)
{
    int i; for (i = 0; i < 16; i++) o[i] = a[i] - b[i];
}

/* Rimette le cifre dentro i sedici bit, e riporta in cima cio' che avanza.
 * L'ultima cifra rientra dal fondo moltiplicata per 38: vedi il modulo. */
void fe_riporta(fe o)
{
    i64 c;
    int i;

    for (i = 0; i < 16; i++) {
        o[i] += (i64)1 << 16;
        c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

void fe_moltiplica(fe o, const fe a, const fe b)
{
    i64 t[31];
    int i, j;

    for (i = 0; i < 31; i++) t[i] = 0;
    for (i = 0; i < 16; i++)
        for (j = 0; j < 16; j++) t[i + j] += a[i] * b[j];

    /* La meta' alta rientra: 2^256 vale 38 modulo 2^255-19. */
    for (i = 0; i < 15; i++) t[i] += 38 * t[i + 16];

    for (i = 0; i < 16; i++) o[i] = t[i];
    fe_riporta(o);
    fe_riporta(o);
}

void fe_quadrato(fe o, const fe a) { fe_moltiplica(o, a, a); }

/* ! LO SCAMBIO E' SENZA RAMI: `b` vale 0 o 1, la maschera diventa tutti zeri o
 * tutti uni, e le due cifre si scambiano o restano dove sono facendo sempre lo
 * stesso numero di operazioni. E' il cuore della resistenza al cronometro. */
void fe_scambia(fe p, fe q, i64 b)
{
    i64 t, c = ~(b - 1);
    int i;

    for (i = 0; i < 16; i++) {
        t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

/* L'inverso moltiplicativo, per elevamento a p-2. E' lento e va bene: si fa
 * una volta sola alla fine, e non dipende da nessun segreto in modo visibile. */
void fe_inverti(fe o, const fe i)
{
    fe c;
    int a;

    fe_copia(c, i);
    for (a = 253; a >= 0; a--) {
        fe_quadrato(c, c);
        if (a != 2 && a != 4) fe_moltiplica(c, c, i);
    }
    fe_copia(o, c);
}

/* Da cifre a byte, con la riduzione finale: il numero puo' ancora essere
 * maggiore del modulo, e va portato dentro. */
void fe_in_byte(unsigned char *fuori, const fe n)
{
    fe   m, t;
    i64  b;
    int  i, j;

    fe_copia(t, n);
    fe_riporta(t); fe_riporta(t); fe_riporta(t);

    /* Due sottrazioni del modulo, scegliendo senza guardare. */
    for (j = 0; j < 2; j++) {
        m[0] = t[0] - 0xffed;
        for (i = 1; i < 15; i++) {
            m[i] = t[i] - 0xffff - ((m[i-1] >> 16) & 1);
            m[i-1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        b = (m[15] >> 16) & 1;
        m[14] &= 0xffff;
        fe_scambia(t, m, 1 - b);
    }

    for (i = 0; i < 16; i++) {
        fuori[2 * i]     = (unsigned char)(t[i] & 0xff);
        fuori[2 * i + 1] = (unsigned char)(t[i] >> 8);
    }
}

void fe_da_byte(fe o, const unsigned char *n)
{
    int i;

    for (i = 0; i < 16; i++)
        o[i] = (i64)n[2 * i] + ((i64)n[2 * i + 1] << 8);

    /* ! IL BIT PIU' ALTO SI BUTTA, e lo dice l'RFC: un punto arriva da fuori,
     * e quel bit non fa parte della coordinata. Tenerlo vorrebbe dire accettare
     * come punti validi dei numeri che non lo sono. */
    o[15] &= 0x7fff;
}

