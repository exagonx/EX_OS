/* =============================================================================
 * lib/excrypt/ed25519.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Ed25519 — la firma (RFC 8032)
 *
 * ! SERVE A UNA COSA SOLA, ED E' LA PIU' IMPORTANTE DI UNA SESSIONE CIFRATA:
 * dire a chi si collega CON CHI sta parlando. Lo scambio di chiavi da' un
 * segreto condiviso anche a chi si e' messo in mezzo — X25519 da solo protegge
 * dall'ascolto, non dalla sostituzione. La firma dell'host sullo scambio e'
 * cio' che rende quella sostituzione impossibile da nascondere.
 *
 * ! LA CURVA E' UN'ALTRA, I NUMERI SONO GLI STESSI. X25519 lavora sulla curva
 * di Montgomery, qui si e' su quella di Edwards: le formule sono diverse, ma
 * l'aritmetica modulo 2^255-19 e' la stessa, ed e' in fe25519.c.
 *
 * ! E LA FIRMA NON E' CASUALE, e' DETERMINISTICA. Il numero segreto di ogni
 * firma non si sorteggia: si calcola come SHA-512 di una parte della chiave
 * privata piu' il messaggio. E' la difesa contro l'errore che ha svelato le
 * chiavi di piu' di un sistema famoso — due firme fatte con lo stesso numero
 * casuale rivelano la chiave privata, e un generatore debole basta a produrre
 * quella coincidenza.
 * ============================================================================= */

#include "excrypt.h"
#include "fe25519.h"

typedef i64 gf[16];

/* d = -121665/121666, la costante della curva; e la radice quadrata di -1. */
static const gf D = {
    0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
    0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203
};
static const gf D2 = {
    0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
    0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406
};
static const gf X = {
    0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
    0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169
};
static const gf Y = {
    0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
    0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666
};
static const gf I = {
    0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
    0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83
};

/* L'ordine del sottogruppo, in byte: serve a ridurre gli scalari. */
static const i64 L[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58,
    0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0,    0,    0,    0,    0,    0,    0,    0,
    0,    0,    0,    0,    0,    0,    0,    0x10
};

/* -----------------------------------------------------------------------------
 * I punti, in coordinate estese (X, Y, Z, T)
 *
 * ! LE COORDINATE SONO QUATTRO E NON DUE PER NON DIVIDERE MAI. Una divisione
 * nel campo e' un'inversione, che costa quanto duecentocinquanta
 * moltiplicazioni: tenendo il denominatore da parte (Z) si divide una volta
 * sola, alla fine.
 * --------------------------------------------------------------------------- */
static void somma_punti(gf p[4], gf q[4])
{
    gf a, b, c, d, t, e, f, g, h;

    fe_sottrai(a, p[1], p[0]);
    fe_sottrai(t, q[1], q[0]);
    fe_moltiplica(a, a, t);
    fe_somma(b, p[0], p[1]);
    fe_somma(t, q[0], q[1]);
    fe_moltiplica(b, b, t);
    fe_moltiplica(c, p[3], q[3]);
    fe_moltiplica(c, c, D2);
    fe_moltiplica(d, p[2], q[2]);
    fe_somma(d, d, d);
    fe_sottrai(e, b, a);
    fe_sottrai(f, d, c);
    fe_somma(g, d, c);
    fe_somma(h, b, a);

    fe_moltiplica(p[0], e, f);
    fe_moltiplica(p[1], h, g);
    fe_moltiplica(p[2], g, f);
    fe_moltiplica(p[3], e, h);
}

static void scambia_punti(gf p[4], gf q[4], i64 b)
{
    int i;
    for (i = 0; i < 4; i++) fe_scambia(p[i], q[i], b);
}

/* La moltiplicazione scalare, a tempo costante: si guardano tutti i bit e si
 * fanno sempre le stesse operazioni. */
static void moltiplica_punto(gf p[4], gf q[4], const unsigned char *s)
{
    int i;

    fe_zero(p[0]); fe_uno(p[1]); fe_uno(p[2]); fe_zero(p[3]);

    for (i = 255; i >= 0; --i) {
        i64 b = (s[i / 8] >> (i & 7)) & 1;

        scambia_punti(p, q, b);
        somma_punti(q, p);
        somma_punti(p, p);
        scambia_punti(p, q, b);
    }
}

static void moltiplica_base(gf p[4], const unsigned char *s)
{
    gf q[4];

    fe_copia(q[0], X);
    fe_copia(q[1], Y);
    fe_uno(q[2]);
    fe_moltiplica(q[3], X, Y);
    moltiplica_punto(p, q, s);
}

/* Un punto si impacchetta in 32 byte: la Y, piu' UN BIT che dice quale delle
 * due X. E' meta' dello spazio, e la X si ricalcola a chi serve. */
static void impacchetta(unsigned char *r, gf p[4])
{
    gf tx, ty, zi;

    fe_inverti(zi, p[2]);
    fe_moltiplica(tx, p[0], zi);
    fe_moltiplica(ty, p[1], zi);
    fe_in_byte(r, ty);

    {
        unsigned char sx[32];
        fe_in_byte(sx, tx);
        r[31] ^= (unsigned char)((sx[0] & 1) << 7);
    }
}

static int uguali32(const unsigned char *a, const unsigned char *b)
{
    unsigned char d = 0;
    int i;
    for (i = 0; i < 32; i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}

/* Il bit basso della coordinata, che e' come si distingue una radice
 * dall'altra. */
static int par(const gf a)
{
    unsigned char d[32];

    fe_in_byte(d, a);
    return d[0] & 1;
}

/* Il contrario: da 32 byte al punto — e lo rende NEGATO.
 *
 * ! IL SEGNO ROVESCIATO NON E' UNA STRANEZZA, E' CIO' CHE SERVE ALLA VERIFICA.
 * Il controllo e' «S per la base MENO h per A fa R»: avendo qui gia' il punto
 * negato, quella sottrazione diventa una somma, e la somma di punti e' l'unica
 * operazione che si e' scritta. La prima stesura rendeva il punto normale, e
 * tutte le firme — comprese quelle GIUSTE, identiche ai vettori dell'RFC —
 * risultavano false: il difetto non era nella firma ma nel confronto.
 *
 * Rende 0, o -1 se quei byte non sono un punto della curva. */
static int spacchetta(gf r[4], const unsigned char *p)
{
    static const gf ZERO = {0};
    gf t, chk, num, den, den2, den4, den6;
    unsigned char a[32], b[32];
    int i;

    fe_uno(r[2]);
    fe_da_byte(r[1], p);
    fe_quadrato(num, r[1]);                 /* y^2            */
    fe_moltiplica(den, num, D);             /* d y^2          */
    fe_sottrai(num, num, r[2]);             /* y^2 - 1        */
    fe_somma(den, r[2], den);               /* 1 + d y^2      */

    fe_quadrato(den2, den);
    fe_quadrato(den4, den2);
    fe_moltiplica(den6, den4, den2);
    fe_moltiplica(t, den6, num);
    fe_moltiplica(t, t, den);               /* num den^7      */

    /* La radice: elevamento a (p-5)/8. */
    {
        gf c;
        fe_copia(c, t);
        for (i = 250; i >= 0; i--) {
            fe_quadrato(c, c);
            if (i != 1) fe_moltiplica(c, c, t);
        }
        fe_copia(t, c);
    }

    fe_moltiplica(t, t, num);
    fe_moltiplica(t, t, den);
    fe_moltiplica(t, t, den);
    fe_moltiplica(r[0], t, den);

    fe_quadrato(chk, r[0]);
    fe_moltiplica(chk, chk, den);
    fe_in_byte(a, chk);
    fe_in_byte(b, num);

    /* ! DUE RADICI E UNA SOLA GIUSTA: se il quadrato non torna, si moltiplica
     * per la radice di -1 e si riprova. Se non torna nemmeno allora, quei byte
     * non sono un punto — e un punto inventato non deve passare per buono. */
    if (!uguali32(a, b)) fe_moltiplica(r[0], r[0], I);

    fe_quadrato(chk, r[0]);
    fe_moltiplica(chk, chk, den);
    fe_in_byte(a, chk);
    if (!uguali32(a, b)) return -1;

    /* Il bit in cima ai 32 byte dice quale delle due X. Qui il confronto e'
     * rovesciato apposta: cosi' il punto esce negato. */
    if (par(r[0]) == (p[31] >> 7)) fe_sottrai(r[0], ZERO, r[0]);

    fe_moltiplica(r[3], r[0], r[1]);
    return 0;
}

/* -----------------------------------------------------------------------------
 * La riduzione modulo l — l'ordine del gruppo
 *
 * ! GLI SCALARI VANNO RIDOTTI, O LA FIRMA E' MALLEABILE. Senza, allo stesso
 * messaggio corrisponderebbero firme diverse tutte valide, e chi sta in mezzo
 * potrebbe cambiarne una senza invalidarla.
 * --------------------------------------------------------------------------- */
static void modl(unsigned char *r, i64 x[64])
{
    i64 carry;
    int i, j;

    for (i = 63; i >= 32; --i) {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; j++) {
        x[j] += carry - (x[31] >> 4) * L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; j++) x[j] -= carry * L[j];
    for (i = 0; i < 32; i++) {
        x[i + 1] += x[i] >> 8;
        r[i] = (unsigned char)(x[i] & 255);
    }
}

static void riduci(unsigned char *r)
{
    i64 x[64];
    int i;

    for (i = 0; i < 64; i++) x[i] = (i64)(unsigned char)r[i];
    for (i = 0; i < 64; i++) r[i] = 0;
    modl(r, x);
}

/* -----------------------------------------------------------------------------
 * Le tre funzioni pubbliche
 * --------------------------------------------------------------------------- */
void ed25519_pubblica(unsigned char pub[32], const unsigned char seme[32])
{
    unsigned char h[64];
    gf p[4];

    sha512(seme, 32, h);

    /* ! LA POTATURA E' LA STESSA DI X25519, e per la stessa ragione: fissa la
     * lunghezza dello scalare e lo tiene nel sottogruppo giusto. */
    h[0]  &= 248;
    h[31] &= 127;
    h[31] |= 64;

    moltiplica_base(p, h);
    impacchetta(pub, p);
}

void ed25519_firma(unsigned char firma[64], const unsigned char *m,
                   unsigned int n, const unsigned char seme[32],
                   const unsigned char pub[32])
{
    unsigned char h[64], r[64], hram[64], buf[512];
    i64  x[64];
    gf   p[4];
    unsigned int i;

    if (n > sizeof(buf) - 64) n = sizeof(buf) - 64;

    sha512(seme, 32, h);
    h[0] &= 248; h[31] &= 127; h[31] |= 64;

    /* r = SHA-512(meta' alta della chiave || messaggio): deterministico. */
    for (i = 0; i < 32; i++) buf[i] = h[32 + i];
    for (i = 0; i < n; i++)  buf[32 + i] = m[i];
    sha512(buf, 32 + n, r);
    riduci(r);

    moltiplica_base(p, r);
    impacchetta(firma, p);

    /* hram = SHA-512(R || A || messaggio) */
    for (i = 0; i < 32; i++) buf[i] = firma[i];
    for (i = 0; i < 32; i++) buf[32 + i] = pub[i];
    for (i = 0; i < n; i++)  buf[64 + i] = m[i];
    sha512(buf, 64 + n, hram);
    riduci(hram);

    /* S = r + hram * a  (mod l) */
    for (i = 0; i < 64; i++) x[i] = 0;
    for (i = 0; i < 32; i++) x[i] = (i64)r[i];
    for (i = 0; i < 32; i++) {
        unsigned int j;
        for (j = 0; j < 32; j++) x[i + j] += (i64)hram[i] * (i64)h[j];
    }
    modl(firma + 32, x);
}

int ed25519_verifica(const unsigned char firma[64], const unsigned char *m,
                     unsigned int n, const unsigned char pub[32])
{
    unsigned char hram[64], buf[512], t[32];
    gf  p[4], q[4];
    unsigned int i;

    if (n > sizeof(buf) - 64) return -1;

    /* ! LA META' ALTA DELLA FIRMA DEV'ESSERE GIA' RIDOTTA. Accettarne una piu'
     * grande vuol dire accettare due firme diverse per lo stesso messaggio. */
    if (firma[63] & 224) return -1;

    if (spacchetta(q, pub) != 0) return -1;

    for (i = 0; i < 32; i++) buf[i] = firma[i];
    for (i = 0; i < 32; i++) buf[32 + i] = pub[i];
    for (i = 0; i < n; i++)  buf[64 + i] = m[i];
    sha512(buf, 64 + n, hram);
    riduci(hram);

    moltiplica_punto(p, q, hram);
    {
        gf b[4];
        moltiplica_base(b, firma + 32);
        somma_punti(p, b);
    }

    impacchetta(t, p);
    return uguali32(firma, t) ? 0 : -1;
}
