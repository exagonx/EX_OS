/* =============================================================================
 * lib/excurva/excurva.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Le curve P-256 e P-384, e la verifica di una firma
 *
 * ! I PUNTI STANNO IN COORDINATE JACOBIANE, e la ragione e' una divisione. In
 * coordinate normali ogni somma di due punti finisce con un'inversione modulare
 * — che costa quanto duecento moltiplicazioni. In Jacobiane (X, Y, Z), dove il
 * punto vero e' (X/Z^2, Y/Z^3), somma e raddoppio sono SOLO moltiplicazioni: si
 * paga una sola inversione alla fine, per tornare al mondo normale.
 *
 * ! I CASI PARTICOLARI DELLA SOMMA NON SI POSSONO SALTARE. Sommare un punto a
 * SE STESSO con le formule della somma da' zero fratto zero: bisogna
 * accorgersene e chiamare il raddoppio. E sommare un punto al suo opposto da'
 * il punto all'infinito, che nelle Jacobiane e' «Z uguale a zero». Sono i due
 * casi che una prova con numeri a caso non incontra quasi mai e che un
 * avversario puo' cercare apposta.
 *
 * ! LA VERIFICA CONTROLLA ANCHE CHE IL PUNTO STIA SULLA CURVA. Una chiave
 * pubblica che non ci sta non e' una chiave: e' un invito a fare i conti in un
 * gruppo piu' piccolo, dove la matematica dice altro. Costa una moltiplicazione
 * e chiude una famiglia intera di attacchi.
 * ============================================================================= */

#include "excurva.h"
#include "exbig.h"

/* I numeri delle due curve, come stanno in FIPS 186-4. Tutt'e due hanno
 * a = -3, ed e' il motivo per cui le formule del raddoppio valgono per
 * entrambe senza un solo `if`.
 *
 *   P-256:  p = 2^256 - 2^224 + 2^192 + 2^96 - 1
 *   P-384:  p = 2^384 - 2^128 - 2^96 + 2^32 - 1
 */
static const unsigned char P256_P[32] = {
    0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF
};
static const unsigned char P256_N[32] = {
    0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xBC,0xE6,0xFA,0xAD,0xA7,0x17,0x9E,0x84,0xF3,0xB9,0xCA,0xC2,0xFC,0x63,0x25,0x51
};
static const unsigned char P256_B[32] = {
    0x5A,0xC6,0x35,0xD8,0xAA,0x3A,0x93,0xE7,0xB3,0xEB,0xBD,0x55,0x76,0x98,0x86,0xBC,
    0x65,0x1D,0x06,0xB0,0xCC,0x53,0xB0,0xF6,0x3B,0xCE,0x3C,0x3E,0x27,0xD2,0x60,0x4B
};
static const unsigned char P256_GX[32] = {
    0x6B,0x17,0xD1,0xF2,0xE1,0x2C,0x42,0x47,0xF8,0xBC,0xE6,0xE5,0x63,0xA4,0x40,0xF2,
    0x77,0x03,0x7D,0x81,0x2D,0xEB,0x33,0xA0,0xF4,0xA1,0x39,0x45,0xD8,0x98,0xC2,0x96
};
static const unsigned char P256_GY[32] = {
    0x4F,0xE3,0x42,0xE2,0xFE,0x1A,0x7F,0x9B,0x8E,0xE7,0xEB,0x4A,0x7C,0x0F,0x9E,0x16,
    0x2B,0xCE,0x33,0x57,0x6B,0x31,0x5E,0xCE,0xCB,0xB6,0x40,0x68,0x37,0xBF,0x51,0xF5
};

static const unsigned char P384_P[48] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
    0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF
};
static const unsigned char P384_N[48] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xC7,0x63,0x4D,0x81,0xF4,0x37,0x2D,0xDF,
    0x58,0x1A,0x0D,0xB2,0x48,0xB0,0xA7,0x7A,0xEC,0xEC,0x19,0x6A,0xCC,0xC5,0x29,0x73
};
static const unsigned char P384_B[48] = {
    0xB3,0x31,0x2F,0xA7,0xE2,0x3E,0xE7,0xE4,0x98,0x8E,0x05,0x6B,0xE3,0xF8,0x2D,0x19,
    0x18,0x1D,0x9C,0x6E,0xFE,0x81,0x41,0x12,0x03,0x14,0x08,0x8F,0x50,0x13,0x87,0x5A,
    0xC6,0x56,0x39,0x8D,0x8A,0x2E,0xD1,0x9D,0x2A,0x85,0xC8,0xED,0xD3,0xEC,0x2A,0xEF
};
static const unsigned char P384_GX[48] = {
    0xAA,0x87,0xCA,0x22,0xBE,0x8B,0x05,0x37,0x8E,0xB1,0xC7,0x1E,0xF3,0x20,0xAD,0x74,
    0x6E,0x1D,0x3B,0x62,0x8B,0xA7,0x9B,0x98,0x59,0xF7,0x41,0xE0,0x82,0x54,0x2A,0x38,
    0x55,0x02,0xF2,0x5D,0xBF,0x55,0x29,0x6C,0x3A,0x54,0x5E,0x38,0x72,0x76,0x0A,0xB7
};
static const unsigned char P384_GY[48] = {
    0x36,0x17,0xDE,0x4A,0x96,0x26,0x2C,0x6F,0x5D,0x9E,0x98,0xBF,0x92,0x92,0xDC,0x29,
    0xF8,0xF4,0x1D,0xBD,0x28,0x9A,0x14,0x7C,0xE9,0xDA,0x31,0x13,0xB5,0xF0,0xB8,0xC0,
    0x0A,0x60,0xB1,0xCE,0x1D,0x7E,0x81,0x9D,0x7A,0x43,0x1D,0x7C,0x90,0xEA,0x0E,0x5F
};

typedef struct {
    const unsigned char *p, *n, *b, *gx, *gy;
    unsigned int         byte;      /* 32 o 48 */
} Curva;

static const Curva CURVE[2] = {
    { P256_P, P256_N, P256_B, P256_GX, P256_GY, 32 },
    { P384_P, P384_N, P384_B, P384_GX, P384_GY, 48 }
};

typedef struct {
    ExBig x, y, z;      /* Jacobiane: il punto vero e' (x/z^2, y/z^3) */
} Punto;

/* Il campo, preparato una volta per verifica. */
typedef struct {
    ExBigMod     fp;        /* modulo p: le coordinate  */
    ExBig        b;
    ExBig        tre;
    const Curva *c;
} Campo;

static int e_infinito(const Punto *p) { return exbig_e_zero(&p->z); }

static void infinito(Punto *p)
{
    exbig_da_parola(&p->x, 1);
    exbig_da_parola(&p->y, 1);
    exbig_zero(&p->z);
}

/* =============================================================================
 * Raddoppio: 2P, con a = -3
 *
 * Le formule sono quelle classiche per a = -3, che risparmiano due
 * moltiplicazioni sfruttando  3(X - Z^2)(X + Z^2) = 3X^2 - 3Z^4.
 * ========================================================================== */
static void raddoppia(Punto *r, const Punto *p, const Campo *c)
{
    ExBig zz, m, s, t, u, y2;

    if (e_infinito(p) || exbig_e_zero(&p->y)) { infinito(r); return; }

    exbig_mod_mul(&zz, &p->z, &p->z, &c->fp);          /* zz = Z^2      */

    exbig_mod_sub(&t, &p->x, &zz, &c->fp);             /* X - Z^2       */
    exbig_mod_add(&u, &p->x, &zz, &c->fp);             /* X + Z^2       */
    exbig_mod_mul(&m, &t, &u, &c->fp);
    exbig_mod_mul(&m, &m, &c->tre, &c->fp);            /* m = 3(X^2-Z^4) */

    exbig_mod_mul(&y2, &p->y, &p->y, &c->fp);          /* Y^2           */
    exbig_mod_mul(&s, &p->x, &y2, &c->fp);
    exbig_mod_add(&s, &s, &s, &c->fp);
    exbig_mod_add(&s, &s, &s, &c->fp);                 /* s = 4*X*Y^2   */

    /* Z' = 2*Y*Z — si calcola PRIMA di toccare x e y, perche' r puo' essere p */
    exbig_mod_mul(&t, &p->y, &p->z, &c->fp);
    exbig_mod_add(&t, &t, &t, &c->fp);

    exbig_mod_mul(&u, &m, &m, &c->fp);                 /* m^2           */
    exbig_mod_sub(&u, &u, &s, &c->fp);
    exbig_mod_sub(&u, &u, &s, &c->fp);                 /* X' = m^2 - 2s */

    /* Y' = m(s - X') - 8*Y^4 */
    {
        ExBig y4;

        exbig_mod_mul(&y4, &y2, &y2, &c->fp);
        exbig_mod_add(&y4, &y4, &y4, &c->fp);
        exbig_mod_add(&y4, &y4, &y4, &c->fp);
        exbig_mod_add(&y4, &y4, &y4, &c->fp);          /* 8*Y^4         */

        exbig_mod_sub(&zz, &s, &u, &c->fp);
        exbig_mod_mul(&zz, &m, &zz, &c->fp);
        exbig_mod_sub(&zz, &zz, &y4, &c->fp);
    }

    r->x = u;
    r->y = zz;
    r->z = t;
}

/* =============================================================================
 * Somma: P + Q, con Q in coordinate NORMALI (z = 1)
 *
 * ! LA SECONDA E' SEMPRE AFFINE, e non e' una limitazione: i due punti che si
 * sommano in una verifica sono il generatore e la chiave pubblica, che
 * arrivano tutt'e due con z = 1. Le formule miste costano meno di quelle
 * generali, e sono meno da sbagliare.
 * ========================================================================== */
static void somma_affine(Punto *r, const Punto *p, const ExBig *qx,
                         const ExBig *qy, const Campo *c)
{
    ExBig z2, z3, u2, s2, h, ri, h2, h3, t, x3, y3, z3n;

    if (e_infinito(p)) {
        r->x = *qx; r->y = *qy;
        exbig_da_parola(&r->z, 1);
        return;
    }

    exbig_mod_mul(&z2, &p->z, &p->z, &c->fp);
    exbig_mod_mul(&z3, &z2, &p->z, &c->fp);
    exbig_mod_mul(&u2, qx, &z2, &c->fp);               /* U2 = Qx*Z^2  */
    exbig_mod_mul(&s2, qy, &z3, &c->fp);               /* S2 = Qy*Z^3  */

    exbig_mod_sub(&h, &u2, &p->x, &c->fp);
    exbig_mod_sub(&ri, &s2, &p->y, &c->fp);

    if (exbig_e_zero(&h)) {
        /* ! STESSA X: o sono lo stesso punto, o sono opposti. Le formule della
         * somma darebbero 0/0 in tutt'e due i casi, e proseguire vorrebbe dire
         * un punto inventato. */
        if (exbig_e_zero(&ri)) raddoppia(r, p, c);
        else                   infinito(r);
        return;
    }

    exbig_mod_mul(&h2, &h, &h, &c->fp);
    exbig_mod_mul(&h3, &h2, &h, &c->fp);
    exbig_mod_mul(&t, &p->x, &h2, &c->fp);

    exbig_mod_mul(&x3, &ri, &ri, &c->fp);
    exbig_mod_sub(&x3, &x3, &h3, &c->fp);
    exbig_mod_sub(&x3, &x3, &t, &c->fp);
    exbig_mod_sub(&x3, &x3, &t, &c->fp);               /* X3 = r^2-h^3-2t */

    exbig_mod_sub(&y3, &t, &x3, &c->fp);
    exbig_mod_mul(&y3, &ri, &y3, &c->fp);
    {
        ExBig w;

        exbig_mod_mul(&w, &p->y, &h3, &c->fp);
        exbig_mod_sub(&y3, &y3, &w, &c->fp);
    }

    exbig_mod_mul(&z3n, &p->z, &h, &c->fp);

    r->x = x3;
    r->y = y3;
    r->z = z3n;
}

/* r = k*P, con P affine. Doppia-e-somma dal bit piu' alto. */
static void moltiplica(Punto *r, const ExBig *k, const ExBig *px,
                       const ExBig *py, const Campo *c)
{
    unsigned int i = exbig_bit(k);

    infinito(r);
    for (; i > 0; i--) {
        Punto t;

        raddoppia(&t, r, c);
        *r = t;
        if ((k->p[(i - 1) / 32] >> ((i - 1) % 32)) & 1u)
            somma_affine(r, r, px, py, c);
    }
}

/* La somma di due multipli: u1*G + u2*Q. Si fanno separati e si sommano —
 * il trucco di Shamir farebbe meta' dei raddoppi, e non vale la
 * complicazione per una verifica ogni tanto. */
static void due_multipli(Punto *r, const ExBig *u1, const ExBig *u2,
                         const ExBig *qx, const ExBig *qy, const Campo *c)
{
    Punto a, b;
    ExBig gx, gy;

    exbig_da_byte(&gx, c->c->gx, c->c->byte);
    exbig_da_byte(&gy, c->c->gy, c->c->byte);

    moltiplica(&a, u1, &gx, &gy, c);
    moltiplica(&b, u2, qx, qy, c);

    if (e_infinito(&b)) { *r = a; return; }
    if (e_infinito(&a)) { *r = b; return; }

    /* b si porta in forma affine per usare la somma mista: una inversione,
     * l'unica di tutto il conto. z^-1 = z^(p-2) mod p, per il piccolo teorema
     * di Fermat — p e' primo, quindi non serve l'algoritmo di Euclide. */
    {
        ExBig zi, z2, z3, bx, by, pp, esp;

        exbig_da_byte(&pp, c->c->p, c->c->byte);
        exbig_da_byte(&esp, c->c->p, c->c->byte);
        esp.p[0] -= 2u;                     /* p e' dispari: nessun prestito */

        if (exbig_modexp(&zi, &b.z, &esp, &pp) != 0) { infinito(r); return; }

        exbig_mod_mul(&z2, &zi, &zi, &c->fp);
        exbig_mod_mul(&z3, &z2, &zi, &c->fp);
        exbig_mod_mul(&bx, &b.x, &z2, &c->fp);
        exbig_mod_mul(&by, &b.y, &z3, &c->fp);

        somma_affine(r, &a, &bx, &by, c);
    }
}

/* Il punto sta sulla curva?  y^2 == x^3 - 3x + b  (mod p) */
static int sulla_curva(const ExBig *x, const ExBig *y, const Campo *c)
{
    ExBig s, d, t;

    exbig_mod_mul(&s, y, y, &c->fp);            /* y^2       */
    exbig_mod_mul(&d, x, x, &c->fp);
    exbig_mod_mul(&d, &d, x, &c->fp);           /* x^3       */
    exbig_mod_mul(&t, x, &c->tre, &c->fp);      /* 3x        */
    exbig_mod_sub(&d, &d, &t, &c->fp);
    exbig_mod_add(&d, &d, &c->b, &c->fp);

    return exbig_cmp(&s, &d) == 0;
}

int excurva_verifica(int curva,
                     const unsigned char *punto, unsigned int punto_n,
                     const unsigned char *impronta, unsigned int impronta_n,
                     const unsigned char *r_b, unsigned int r_n,
                     const unsigned char *s_b, unsigned int s_n)
{
    const Curva *cv;
    Campo  c;
    ExBig  p, n, r, s, e, w, u1, u2, qx, qy, esp;
    Punto  R;
    unsigned int quanti;

    if (curva != EXCURVA_P256 && curva != EXCURVA_P384) return -1;
    cv = &CURVE[curva];

    if (!punto || !impronta || !r_b || !s_b) return -1;
    if (punto_n != 1 + 2 * cv->byte || punto[0] != 0x04) return -1;
    if (impronta_n == 0) return -1;

    /* ! GLI ZERI IN TESTA SI TOLGONO, e non e' pedanteria: un intero DER porta
     * uno 0x00 davanti quando il primo bit e' acceso — cioe' meta' delle volte
     * — e quel byte in piu' faceva rifiutare come «malformata» una firma
     * perfettamente valida. Il numero e' lo stesso; la sua scrittura no. */
    while (r_n > 1 && r_b[0] == 0) { r_b++; r_n--; }
    while (s_n > 1 && s_b[0] == 0) { s_b++; s_n--; }

    if (r_n == 0 || r_n > cv->byte || s_n == 0 || s_n > cv->byte) return -1;

    if (exbig_da_byte(&p, cv->p, cv->byte) != 0) return -1;
    if (exbig_da_byte(&n, cv->n, cv->byte) != 0) return -1;
    if (exbig_mod_prepara(&c.fp, &p) != 0) return -1;
    if (exbig_da_byte(&c.b, cv->b, cv->byte) != 0) return -1;
    exbig_da_parola(&c.tre, 3);
    c.c = cv;

    if (exbig_da_byte(&qx, punto + 1, cv->byte) != 0) return -1;
    if (exbig_da_byte(&qy, punto + 1 + cv->byte, cv->byte) != 0) return -1;
    if (exbig_cmp(&qx, &p) >= 0 || exbig_cmp(&qy, &p) >= 0) return -1;
    if (!sulla_curva(&qx, &qy, &c)) return -1;

    if (exbig_da_byte(&r, r_b, r_n) != 0) return -1;
    if (exbig_da_byte(&s, s_b, s_n) != 0) return -1;

    /* ! r E s DEVONO STARE FRA 1 E n-1, e non e' una formalita': con s = 0 la
     * verifica dividerebbe per zero, e con r = 0 una firma qualunque
     * passerebbe. Sono i primi due controlli che una firma falsa prova. */
    if (exbig_e_zero(&r) || exbig_e_zero(&s)) return -1;
    if (exbig_cmp(&r, &n) >= 0 || exbig_cmp(&s, &n) >= 0) return -1;

    /* ! DELL'IMPRONTA SI PRENDONO I BIT PIU' A SINISTRA, TANTI QUANTI n. E'
     * quello che dice lo standard, e serve davvero: una chiave P-256 firmata
     * con SHA-384 e' normalissima sul web — la catena di example.com e' fatta
     * cosi' — e prendere tutti i 48 byte darebbe un numero diverso da quello
     * che il firmatario ha usato. Nell'altro verso (P-384 con SHA-256) non si
     * aggiunge niente: l'impronta corta vale per quello che e'. */
    quanti = (impronta_n < cv->byte) ? impronta_n : cv->byte;
    if (exbig_da_byte(&e, impronta, quanti) != 0) return -1;

    if (exbig_cmp(&e, &n) >= 0) {
        ExBigMod fn;

        if (exbig_mod_prepara(&fn, &n) != 0) return -1;
        exbig_mod_sub(&e, &e, &n, &fn);
    }

    /* w = s^-1 mod n, per Fermat: n e' primo. */
    if (exbig_da_byte(&esp, cv->n, cv->byte) != 0) return -1;
    esp.p[0] -= 2u;
    if (exbig_modexp(&w, &s, &esp, &n) != 0) return -1;

    {
        ExBigMod fn;

        if (exbig_mod_prepara(&fn, &n) != 0) return -1;
        exbig_mod_mul(&u1, &e, &w, &fn);
        exbig_mod_mul(&u2, &r, &w, &fn);
    }

    due_multipli(&R, &u1, &u2, &qx, &qy, &c);
    if (e_infinito(&R)) return -1;

    /* Si torna in coordinate normali per confrontare la X con r. */
    {
        ExBig zi, z2, x, e2;

        if (exbig_da_byte(&e2, cv->p, cv->byte) != 0) return -1;
        e2.p[0] -= 2u;
        if (exbig_modexp(&zi, &R.z, &e2, &p) != 0) return -1;

        exbig_mod_mul(&z2, &zi, &zi, &c.fp);
        exbig_mod_mul(&x, &R.x, &z2, &c.fp);

        /* ! IL CONFRONTO E' CON x MOD n, NON CON x. Le due misure sono vicine
         * ma diverse: una x fra n e p e' legittima, e senza la riduzione
         * quelle firme — rarissime, ma esistono — verrebbero rifiutate.
         * x < p < 2n, quindi una sola sottrazione basta. */
        if (exbig_cmp(&x, &n) >= 0) {
            ExBigMod fn;

            if (exbig_mod_prepara(&fn, &n) != 0) return -1;
            exbig_mod_sub(&x, &x, &n, &fn);
        }

        return (exbig_cmp(&x, &r) == 0) ? 0 : -1;
    }
}
