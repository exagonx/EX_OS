/* =============================================================================
 * lib/excrypt/p256.c — ECDH on P-256, in constant time
 *
 * Written on 23 September 2026 for the TLS client: www.poste.it does not
 * accept x25519 and wants the key exchange on secp256r1 (P-256).
 *
 * ! WHY NOT lib/excurva, WHICH ALREADY DOES P-256: excurva says in its header
 * that it is NOT constant-time, on purpose — only PUBLIC numbers go through
 * it (a key, a signature, a digest), and constant-time code there would be
 * complication paid for nothing. An ECDH multiplies by OUR SECRET scalar:
 * that is exactly the case excurva was not written for, and the project's
 * own rule says so. So this is a separate, small piece, written for secrets.
 *
 * ! HOW IT STAYS CONSTANT-TIME:
 *   - the field is eight fixed 32-bit words, Montgomery multiplication with
 *     no early exits; additions and subtractions finish with a masked
 *     correction, never an `if` on the value;
 *   - the point formulas are the COMPLETE ones of Renes, Costello and Batina
 *     (2016, Algorithm 4, a = -3): one formula for every pair of points,
 *     infinity and doubling included — no special case to branch on;
 *   - the scalar goes through a Montgomery ladder: the same two operations
 *     at every bit, and the bit only chooses, by mask, which point is which;
 *   - the inversion at the end is Fermat, a fixed public exponent.
 * What does depend on data is only public: rejecting a peer point that is
 * not on the curve, and a private key of zero (a broken random source).
 *
 * Checked on the host by tools/prova_p256.sh against Python's
 * `cryptography`: public keys and shared secrets.
 * ============================================================================= */
#include "excrypt.h"

typedef unsigned int       u32;
typedef unsigned long long u64;
typedef struct { u32 x[8], y[8], z[8]; } Proj;

static const u32 P[8]   = { 0xffffffffu, 0xffffffffu, 0xffffffffu, 0x00000000u,
                            0x00000000u, 0x00000000u, 0x00000001u, 0xffffffffu };
static const u32 R2[8]  = { 0x00000003u, 0x00000000u, 0xffffffffu, 0xfffffffbu,
                            0xfffffffeu, 0xffffffffu, 0xfffffffdu, 0x00000004u };
static const u32 UNO[8] = { 0x00000001u, 0x00000000u, 0x00000000u, 0xffffffffu,
                            0xffffffffu, 0xffffffffu, 0xfffffffeu, 0x00000000u };
static const u32 BM[8]  = { 0x29c4bddfu, 0xd89cdf62u, 0x78843090u, 0xacf005cdu,
                            0xf7212ed6u, 0xe5a220abu, 0x04874834u, 0xdc30061du };
static const unsigned char N_BE[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51 };
static const unsigned char GX[32] = {
    0x6b,0x17,0xd1,0xf2,0xe1,0x2c,0x42,0x47,0xf8,0xbc,0xe6,0xe5,0x63,0xa4,0x40,0xf2,
    0x77,0x03,0x7d,0x81,0x2d,0xeb,0x33,0xa0,0xf4,0xa1,0x39,0x45,0xd8,0x98,0xc2,0x96 };
static const unsigned char GY[32] = {
    0x4f,0xe3,0x42,0xe2,0xfe,0x1a,0x7f,0x9b,0x8e,0xe7,0xeb,0x4a,0x7c,0x0f,0x9e,0x16,
    0x2b,0xce,0x33,0x57,0x6b,0x31,0x5e,0xce,0xcb,0xb6,0x40,0x68,0x37,0xbf,0x51,0xf5 };

/* --- the field, mod p, in Montgomery form (R = 2^256) ---------------------- */

/* r = t - p if t >= p, else t; t has nine words. By mask, not by branch. */
static void riduci(u32 r[8], const u32 t[9])
{
    u32 s[8], prestito = 0, maschera;
    int i;

    for (i = 0; i < 8; i++) {
        u64 d = (u64)t[i] - P[i] - prestito;
        s[i] = (u32)d;
        prestito = (u32)(d >> 63);
    }
    /* t - p is negative exactly when the ninth word cannot pay the borrow. */
    prestito = (u32)(((u64)t[8] - prestito) >> 63);
    maschera = 0u - prestito;              /* all ones: keep t */
    for (i = 0; i < 8; i++) r[i] = (t[i] & maschera) | (s[i] & ~maschera);
}

static void fmul(u32 r[8], const u32 a[8], const u32 b[8])
{
    u32 t[10] = { 0 };
    int i, j;

    for (i = 0; i < 8; i++) {
        u64 c = 0;
        u32 m;

        for (j = 0; j < 8; j++) {
            c += (u64)t[j] + (u64)a[j] * b[i];
            t[j] = (u32)c;
            c >>= 32;
        }
        c += t[8];
        t[8] = (u32)c;
        t[9] = (u32)(c >> 32);

        m = t[0];                          /* -p^-1 mod 2^32 is 1 for P-256 */
        c = (u64)t[0] + (u64)m * P[0];
        c >>= 32;
        for (j = 1; j < 8; j++) {
            c += (u64)t[j] + (u64)m * P[j];
            t[j - 1] = (u32)c;
            c >>= 32;
        }
        c += t[8];
        t[7] = (u32)c;
        t[8] = t[9] + (u32)(c >> 32);
        t[9] = 0;
    }
    riduci(r, t);
}

static void fadd(u32 r[8], const u32 a[8], const u32 b[8])
{
    u32 t[9];
    u64 c = 0;
    int i;

    for (i = 0; i < 8; i++) { c += (u64)a[i] + b[i]; t[i] = (u32)c; c >>= 32; }
    t[8] = (u32)c;
    riduci(r, t);
}

static void fsub(u32 r[8], const u32 a[8], const u32 b[8])
{
    u32 t[8], maschera, prestito = 0;
    u64 c = 0;
    int i;

    for (i = 0; i < 8; i++) {
        u64 d = (u64)a[i] - b[i] - prestito;
        t[i] = (u32)d;
        prestito = (u32)(d >> 63);
    }
    maschera = 0u - prestito;              /* went negative: add p back */
    for (i = 0; i < 8; i++) {
        c += (u64)t[i] + (P[i] & maschera);
        r[i] = (u32)c;
        c >>= 32;
    }
}

static void da_byte(u32 r[8], const unsigned char b[32])
{
    int i;

    for (i = 0; i < 8; i++)
        r[i] = ((u32)b[31 - 4 * i]) | ((u32)b[30 - 4 * i] << 8) |
               ((u32)b[29 - 4 * i] << 16) | ((u32)b[28 - 4 * i] << 24);
}

static void a_byte(unsigned char b[32], const u32 r[8])
{
    int i;

    for (i = 0; i < 8; i++) {
        b[31 - 4 * i] = (unsigned char)r[i];
        b[30 - 4 * i] = (unsigned char)(r[i] >> 8);
        b[29 - 4 * i] = (unsigned char)(r[i] >> 16);
        b[28 - 4 * i] = (unsigned char)(r[i] >> 24);
    }
}

/* Is the number (not in Montgomery form) below p? Public data only. */
static int sotto_p(const u32 a[8])
{
    int i;

    for (i = 7; i >= 0; i--) {
        if (a[i] < P[i]) return 1;
        if (a[i] > P[i]) return 0;
    }
    return 0;
}

/* a^(p-2): Fermat. The exponent is public and fixed. */
static void finverso(u32 r[8], const u32 a[8])
{
    u32 e[8], acc[8];
    int i, b;

    for (i = 0; i < 8; i++) { e[i] = P[i]; acc[i] = UNO[i]; }
    e[0] -= 2;                             /* p - 2: no borrow, p ends in ...ff */
    for (i = 7; i >= 0; i--)
        for (b = 31; b >= 0; b--) {
            fmul(acc, acc, acc);
            if ((e[i] >> b) & 1u) fmul(acc, acc, a);
        }
    for (i = 0; i < 8; i++) r[i] = acc[i];
}

/* --- points: the complete formulas ----------------------------------------- */

/* Renes, Costello, Batina 2016, Algorithm 4 (a = -3): R = P + Q for ANY two
 * points, the same point and infinity included. */
static void somma(Proj *r, const Proj *p, const Proj *q)
{
    u32 t0[8], t1[8], t2[8], t3[8], t4[8], x3[8], y3[8], z3[8];

    fmul(t0, p->x, q->x);   fmul(t1, p->y, q->y);   fmul(t2, p->z, q->z);
    fadd(t3, p->x, p->y);   fadd(t4, q->x, q->y);   fmul(t3, t3, t4);
    fadd(t4, t0, t1);       fsub(t3, t3, t4);       fadd(t4, p->y, p->z);
    fadd(x3, q->y, q->z);   fmul(t4, t4, x3);       fadd(x3, t1, t2);
    fsub(t4, t4, x3);       fadd(x3, p->x, p->z);   fadd(y3, q->x, q->z);
    fmul(x3, x3, y3);       fadd(y3, t0, t2);       fsub(y3, x3, y3);
    fmul(z3, BM, t2);       fsub(x3, y3, z3);       fadd(z3, x3, x3);
    fadd(x3, x3, z3);       fsub(z3, t1, x3);       fadd(x3, t1, x3);
    fmul(y3, BM, y3);       fadd(t1, t2, t2);       fadd(t2, t1, t2);
    fsub(y3, y3, t2);       fsub(y3, y3, t0);       fadd(t1, y3, y3);
    fadd(y3, t1, y3);       fadd(t1, t0, t0);       fadd(t0, t1, t0);
    fsub(t0, t0, t2);       fmul(t1, t4, y3);       fmul(t2, t0, y3);
    fmul(y3, x3, z3);       fadd(y3, y3, t2);       fmul(x3, t3, x3);
    fsub(x3, x3, t1);       fmul(z3, t4, z3);       fmul(t1, t3, t0);
    fadd(z3, z3, t1);

    {
        int i;
        for (i = 0; i < 8; i++) { r->x[i] = x3[i]; r->y[i] = y3[i]; r->z[i] = z3[i]; }
    }
}

static void scambia(Proj *a, Proj *b, u32 bit)
{
    u32 m = 0u - bit, t;
    int i;

    for (i = 0; i < 8; i++) {
        t = m & (a->x[i] ^ b->x[i]); a->x[i] ^= t; b->x[i] ^= t;
        t = m & (a->y[i] ^ b->y[i]); a->y[i] ^= t; b->y[i] ^= t;
        t = m & (a->z[i] ^ b->z[i]); a->z[i] ^= t; b->z[i] ^= t;
    }
}

/* k * (x, y), x and y already in Montgomery form. Returns 0, or -1 if the
 * result is the point at infinity. The affine x goes to `x_out`, 32 bytes;
 * `y_out` may be 0. */
static int moltiplica(const unsigned char k[32], const u32 x[8], const u32 y[8],
                      unsigned char x_out[32], unsigned char *y_out)
{
    Proj r0, r1;
    u32  zi[8], ax[8], ay[8], zero = 0, uno_n[8] = { 1, 0, 0, 0, 0, 0, 0, 0 };
    int  i, b;

    /* r0 = infinity (0 : 1 : 0), r1 = the point. */
    for (i = 0; i < 8; i++) {
        r0.x[i] = 0; r0.y[i] = UNO[i]; r0.z[i] = 0;
        r1.x[i] = x[i]; r1.y[i] = y[i]; r1.z[i] = UNO[i];
    }

    for (i = 0; i < 32; i++)
        for (b = 7; b >= 0; b--) {
            u32 bit = (k[i] >> b) & 1u;

            scambia(&r0, &r1, bit);
            somma(&r1, &r0, &r1);
            somma(&r0, &r0, &r0);
            scambia(&r0, &r1, bit);
        }

    for (i = 0; i < 8; i++) zero |= r0.z[i];
    if (zero == 0) return -1;

    finverso(zi, r0.z);
    fmul(ax, r0.x, zi);
    fmul(ax, ax, uno_n);                   /* out of Montgomery form */
    a_byte(x_out, ax);
    if (y_out) {
        fmul(ay, r0.y, zi);
        fmul(ay, ay, uno_n);
        a_byte(y_out, ay);
    }
    return 0;
}

/* 0 < k < n, compared in bytes: public outcome, the key is rejected. */
static int scalare_buono(const unsigned char k[32])
{
    int i, zero = 1;

    for (i = 0; i < 32; i++) if (k[i]) zero = 0;
    if (zero) return 0;
    for (i = 0; i < 32; i++) {
        if (k[i] < N_BE[i]) return 1;
        if (k[i] > N_BE[i]) return 0;
    }
    return 0;
}

int p256_pubblica(const unsigned char privata[32], unsigned char pubblica[65])
{
    u32 x[8], y[8];

    if (!scalare_buono(privata)) return -1;
    da_byte(x, GX); fmul(x, x, R2);
    da_byte(y, GY); fmul(y, y, R2);
    pubblica[0] = 4;
    return moltiplica(privata, x, y, pubblica + 1, pubblica + 33);
}

int p256_condiviso(const unsigned char privata[32], const unsigned char *punto,
                   unsigned int punto_n, unsigned char segreto[32])
{
    u32 x[8], y[8], s[8], d[8], t[8], tre[8];

    if (!scalare_buono(privata)) return -1;

    /* ! THE PEER'S POINT IS CHECKED: uncompressed, coordinates below p, and
     * ON THE CURVE. A point off the curve is the classic «invalid curve»
     * attack — multiplied by our secret, it leaks it piece by piece. */
    if (punto_n != 65 || punto[0] != 4) return -1;
    da_byte(x, punto + 1);
    da_byte(y, punto + 33);
    if (!sotto_p(x) || !sotto_p(y)) return -1;
    fmul(x, x, R2);
    fmul(y, y, R2);

    /* y^2 == x^3 - 3x + b */
    fmul(s, y, y);
    fmul(d, x, x); fmul(d, d, x);
    fadd(tre, x, x); fadd(tre, tre, x);
    fsub(d, d, tre);
    fadd(d, d, BM);
    fsub(t, s, d);
    {
        u32 o = 0;
        int i;
        for (i = 0; i < 8; i++) o |= t[i];
        if (o != 0) return -1;
    }

    return moltiplica(privata, x, y, segreto, 0);
}
