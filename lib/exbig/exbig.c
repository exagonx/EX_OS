/* =============================================================================
 * lib/exbig/exbig.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Interi lunghi: il come. Il perche' — e il confine del lavoro — sta in
 * exbig.h.
 *
 * ! QUESTO FILE NON INCLUDE LA LIBC, ED E' APPOSTA. E' la stessa scelta di
 * ttf.c e raster.c: senza libc si compila anche PER L'HOST, e allora la prova
 * puo' confrontarlo con un'aritmetica di cui ci si fida — Python, che gli
 * interi lunghi ce li ha esatti. Una libreria di numeri provata contro se
 * stessa non e' provata: e' solo coerente.
 *
 * Serve un tipo a 64 bit per i prodotti, e `unsigned long long` va bene su
 * tutt'e due: su i386 GCC lo fa con `mull`, due istruzioni. E' la DIVISIONE a
 * 64 bit che non esiste, non la moltiplicazione.
 * ============================================================================= */

#include "exbig.h"

typedef unsigned int       u32;
typedef unsigned long long u64;

/* =============================================================================
 * Le cose elementari
 * ============================================================================= */
static void normalizza(ExBig *a)
{
    while (a->n > 0 && a->p[a->n - 1] == 0) a->n--;
}

void exbig_zero(ExBig *r)
{
    unsigned int i;

    for (i = 0; i < EXBIG_PAROLE; i++) r->p[i] = 0;
    r->n = 0;
}

void exbig_da_parola(ExBig *r, unsigned int v)
{
    exbig_zero(r);
    if (v) { r->p[0] = v; r->n = 1; }
}

int exbig_e_zero(const ExBig *a) { return a->n == 0; }

int exbig_cmp(const ExBig *a, const ExBig *b)
{
    unsigned int i;

    if (a->n != b->n) return a->n > b->n ? 1 : -1;
    for (i = a->n; i > 0; i--) {
        if (a->p[i - 1] != b->p[i - 1]) return a->p[i - 1] > b->p[i - 1] ? 1 : -1;
    }
    return 0;
}

unsigned int exbig_bit(const ExBig *a)
{
    u32 alta;
    unsigned int b;

    if (a->n == 0) return 0;
    alta = a->p[a->n - 1];
    b = 0;
    while (alta) { b++; alta >>= 1; }
    return (a->n - 1) * 32 + b;
}

int exbig_da_byte(ExBig *r, const unsigned char *b, unsigned int len)
{
    unsigned int i;

    /* Gli zeri davanti non contano e non occupano: un DER positivo ne porta
     * uno per dire «non e' negativo», e rifiutarlo per la misura sarebbe
     * rifiutare meta' dei certificati. */
    while (len > 0 && b[0] == 0) { b++; len--; }
    if (len > EXBIG_PAROLE * 4) return -1;

    exbig_zero(r);
    for (i = 0; i < len; i++) {
        unsigned int pos = len - 1 - i;         /* distanza dal fondo */
        r->p[pos / 4] |= (u32)b[i] << ((pos % 4) * 8);
    }
    r->n = (len + 3) / 4;
    normalizza(r);
    return 0;
}

int exbig_a_byte(const ExBig *a, unsigned char *b, unsigned int len)
{
    unsigned int i;

    if ((exbig_bit(a) + 7) / 8 > len) return -1;

    for (i = 0; i < len; i++) {
        unsigned int pos = len - 1 - i;
        b[i] = (pos / 4 < a->n) ? (unsigned char)(a->p[pos / 4] >> ((pos % 4) * 8))
                                : 0;
    }
    return 0;
}

/* =============================================================================
 * Somma e sottrazione, con il riporto tenuto a 64 bit
 *
 * ! IL RIPORTO NON SI INDOVINA DAL RISULTATO. Scrivere `if (s < a)` per capire
 * se una somma a 32 bit ha traboccato funziona finche' non c'e' anche il
 * riporto precedente da aggiungere: allora ci sono due sorgenti di trabocco e
 * un confronto solo non le distingue. Tenendo la somma in 64 bit il riporto e'
 * semplicemente il bit 32, e non c'e' niente da dedurre.
 * ============================================================================= */

/* r = a + b (mod 2^(32*EXBIG_PAROLE)). Rende il riporto uscente. */
static u32 somma(ExBig *r, const ExBig *a, const ExBig *b)
{
    unsigned int i, n = a->n > b->n ? a->n : b->n;
    u64 t = 0;

    for (i = 0; i < n; i++) {
        t += (u64)(i < a->n ? a->p[i] : 0) + (u64)(i < b->n ? b->p[i] : 0);
        r->p[i] = (u32)t;
        t >>= 32;
    }
    if (t && n < EXBIG_PAROLE) { r->p[n++] = (u32)t; t = 0; }
    for (i = n; i < EXBIG_PAROLE; i++) r->p[i] = 0;
    r->n = n;
    normalizza(r);
    return (u32)t;
}

/* r = a - b, con a >= b. */
static void sottrai(ExBig *r, const ExBig *a, const ExBig *b)
{
    unsigned int i;
    u64 prestito = 0;

    for (i = 0; i < a->n; i++) {
        u64 t = (u64)a->p[i] - (u64)(i < b->n ? b->p[i] : 0) - prestito;
        r->p[i] = (u32)t;
        prestito = (t >> 32) & 1;               /* 1 se ha preso in prestito */
    }
    for (i = a->n; i < EXBIG_PAROLE; i++) r->p[i] = 0;
    r->n = a->n;
    normalizza(r);
}

static void copia(ExBig *r, const ExBig *a)
{
    unsigned int i;

    for (i = 0; i < EXBIG_PAROLE; i++) r->p[i] = a->p[i];
    r->n = a->n;
}

/* =============================================================================
 * ! TOGLIERE m A UN NUMERO CHE HA UNA PAROLA IN PIU'
 *
 * Succede in due posti — la coda del prodotto di Montgomery e il raddoppio di
 * R^2 — e tutt'e due le volte il numero vero e' 2^(32n) + r, con r che da solo
 * puo' essere PIU' PICCOLO di m. Sottrarre m dal solo r andrebbe sotto zero:
 * e' il difetto che ha reso (m-1)^2 mod m uguale a zero.
 *
 * ! IL PRESTITO FINALE CANCELLA LA PAROLA IN PIU', PER COSTRUZIONE. In tutt'e
 * due i casi il valore e' minore di 2m, quindi tolto m sta in n parole. Non si
 * controlla a runtime: si dimostra qui, ed e' il genere di riga che senza
 * questa spiegazione accanto sembrerebbe una svista.
 * ============================================================================= */
static void togli_m(ExBig *r, const ExBig *m, unsigned int n)
{
    u64 prestito = 0;
    unsigned int i;

    for (i = 0; i < n; i++) {
        u64 s = (u64)r->p[i] - (u64)(i < m->n ? m->p[i] : 0) - prestito;
        r->p[i] = (u32)s;
        prestito = (s >> 32) & 1;
    }
    r->n = n;
    normalizza(r);
}

/* =============================================================================
 * MONTGOMERY
 *
 * La forma di Montgomery di `a` e' `aR mod m`, con R = 2^(32*n) e n il numero
 * di parole di m. Il prodotto di Montgomery
 *
 *     mont(x, y) = x*y*R^-1 mod m
 *
 * si calcola senza dividere: a ogni parola si aggiunge un multiplo di m
 * scelto perche' la parola piu' bassa venga ZERO, e allora dividere per 2^32
 * e' uno scorrimento di una parola. E' tutto qui.
 *
 * ! IL NUMERO MAGICO E' -m^-1 mod 2^32, E SI CALCOLA SENZA DIVIDERE. Newton su
 * interi: x_{k+1} = x_k * (2 - m * x_k) raddoppia i bit giusti a ogni giro,
 * quindi da 3 bit (x0 = m, esatto sui primi 3) a 32 bastano cinque giri. Anche
 * qui: solo moltiplicazioni.
 * ============================================================================= */
static u32 n0_inverso(u32 m)
{
    u32 x = m;                  /* m*m == 1 (mod 2^3) per m dispari */
    int i;

    for (i = 0; i < 5; i++) x *= 2u - m * x;    /* 3 -> 6 -> 12 -> 24 -> 48 bit */
    return (u32)(0u - x);                       /* serve -m^-1, non m^-1 */
}

/* r = a*b*R^-1 mod m, con n parole. `a` e `b` devono essere < m.
 *
 * ! IL RISULTATO PUO' SUPERARE m DI UN SOLO m, mai di piu', ed e' il motivo
 * per cui basta UNA sottrazione condizionata alla fine. Vale finche' a e b
 * sono minori di m: chi chiama lo garantisce, e la funzione non lo ricontrolla
 * perche' sta dentro un ciclo che gira migliaia di volte. */
static void mont_mul(ExBig *r, const ExBig *a, const ExBig *b,
                     const ExBig *m, u32 n0, unsigned int n)
{
    /* Una parola in piu' per il riporto che sborda dalla n-esima. */
    u32 t[EXBIG_PAROLE + 2];
    unsigned int i, j;

    for (i = 0; i <= n + 1; i++) t[i] = 0;

    for (i = 0; i < n; i++) {
        u64 riporto;
        u32 u;

        /* t += a * b[i] */
        riporto = 0;
        for (j = 0; j < n; j++) {
            u64 s = (u64)t[j] + (u64)a->p[j] * (u64)(i < b->n ? b->p[i] : 0)
                  + riporto;
            t[j] = (u32)s;
            riporto = s >> 32;
        }
        {   u64 s = (u64)t[n] + riporto;
            t[n] = (u32)s;
            t[n + 1] += (u32)(s >> 32);
        }

        /* u = t[0] * n0 mod 2^32, poi t += m * u: la parola bassa va a zero */
        u = (u32)((u64)t[0] * (u64)n0);
        riporto = 0;
        for (j = 0; j < n; j++) {
            u64 s = (u64)t[j] + (u64)m->p[j] * (u64)u + riporto;
            t[j] = (u32)s;
            riporto = s >> 32;
        }
        {   u64 s = (u64)t[n] + riporto;
            t[n] = (u32)s;
            t[n + 1] += (u32)(s >> 32);
        }

        /* dividere per 2^32 e' scorrere di una parola */
        for (j = 0; j <= n; j++) t[j] = t[j + 1];
        t[n + 1] = 0;
    }

    exbig_zero(r);
    for (i = 0; i < n; i++) r->p[i] = t[i];
    r->n = n;
    normalizza(r);

    /* =========================================================================
     * ! LA SOTTRAZIONE FINALE DEVE GUARDARE ANCHE t[n], E QUI C'ERA IL DIFETTO
     *
     * Il risultato di Montgomery sta in n parole PIU' un bit: puo' valere fino
     * a 2m-1, e 2m non ci sta in n parole. Quel bit e' t[n].
     *
     * La prima versione faceva `if (t[n] || r >= m) r = r - m`, che e'
     * sbagliato in modo sottile: quando t[n] vale 1 il numero vero e'
     * 2^(32n) + r, con r PIU' PICCOLO di m — e sottrarre m da r solo va sotto
     * zero. Il sintomo: (m-1)^2 mod m rendeva 0 invece di 1, ma solo per basi
     * vicine a m, cioe' proprio dove il bordo si tocca. I numeri a caso non ci
     * arrivano quasi mai: l'ha trovato il caso scritto a mano `a = m-1`.
     *
     * ! E IL PRESTITO FINALE CANCELLA t[n] PER COSTRUZIONE: il valore e'
     * minore di 2m, quindi tolto m sta in n parole. Non si controlla a
     * runtime, si dimostra qui — ed e' il genere di riga che senza la prova
     * accanto sembrerebbe una svista.
     * ========================================================================= */
    if (t[n]) {
        togli_m(r, m, n);
    } else if (exbig_cmp(r, m) >= 0) {
        ExBig q;
        sottrai(&q, r, m);
        copia(r, &q);
    }
}

/* =============================================================================
 * R^2 mod m — l'unico posto dove servirebbe una divisione, e non si usa
 *
 * Si parte da 1 e si raddoppia 64*n volte, sottraendo m ogni volta che si
 * arriva o si supera. Sono 64*n confronti su n parole: per un modulo da 2048
 * bit, quattromila giri da sessantaquattro parole — qualche millisecondo, UNA
 * VOLTA per verifica. Il ciclo caldo e' mont_mul, non questo.
 *
 * ! E RADDOPPIARE E' SOMMARE A SE STESSO, quindi anche qui zero divisioni.
 * ============================================================================= */
static void erre_quadro(ExBig *rr, const ExBig *m, unsigned int n)
{
    unsigned int i, giri = 64 * n;

    exbig_da_parola(rr, 1);
    for (i = 0; i < giri; i++) {
        ExBig d;
        u32   riporto = somma(&d, rr, rr);      /* d = 2*rr */

        /* ! IL RIPORTO USCENTE NON SI PUO' BUTTARE, e buttarlo e' costato la
         * meta' delle prove. Con un modulo di 4096 bit — il tetto dichiarato —
         * `d` occupa gia' tutte le parole che ci sono: il bit che esce in cima
         * non ha dove stare, e senza guardarlo il numero diventa un altro
         * numero. Si vedeva solo alla misura massima, cioe' nel caso che si
         * prova per ultimo e si spedisce per primo. */
        if (riporto) {
            copia(rr, &d);
            togli_m(rr, m, n);                  /* 2^(32n) + d, meno m */
        } else if (exbig_cmp(&d, m) >= 0) {
            ExBig q;
            sottrai(&q, &d, m);
            copia(rr, &q);
        } else {
            copia(rr, &d);
        }
    }
}

/* =============================================================================
 * L'esponenziazione modulare
 * ============================================================================= */
int exbig_modexp(ExBig *r, const ExBig *base, const ExBig *e, const ExBig *m)
{
    ExBig rr, x, acc, uno;
    unsigned int n, i;
    u32 n0;

    if (m->n == 0 || (m->p[0] & 1u) == 0) return -1;   /* zero o pari */
    if (m->n > EXBIG_PAROLE) return -1;
    if (exbig_cmp(base, m) >= 0) return -1;            /* firma malformata */

    n  = m->n;
    n0 = n0_inverso(m->p[0]);

    /* Se l'esponente e' zero il risultato e' 1 mod m, e con m = 1 e' zero.
     * Sono due righe e tolgono due casi particolari dal ciclo. */
    if (e->n == 0) {
        exbig_da_parola(r, 1);
        if (exbig_cmp(r, m) >= 0) exbig_zero(r);
        return 0;
    }

    erre_quadro(&rr, m, n);

    /* x = base in forma di Montgomery = base*R mod m = mont(base, R^2) */
    mont_mul(&x, base, &rr, m, n0, n);

    /* acc = 1 in forma di Montgomery = mont(1, R^2) */
    exbig_da_parola(&uno, 1);
    mont_mul(&acc, &uno, &rr, m, n0, n);

    /* Da sinistra a destra sui bit dell'esponente: quadrato sempre,
     * moltiplicazione dove il bit e' acceso. */
    for (i = exbig_bit(e); i > 0; i--) {
        ExBig t;

        mont_mul(&t, &acc, &acc, m, n0, n);
        copia(&acc, &t);

        if ((e->p[(i - 1) / 32] >> ((i - 1) % 32)) & 1u) {
            mont_mul(&t, &acc, &x, m, n0, n);
            copia(&acc, &t);
        }
    }

    /* Fuori dalla forma di Montgomery: mont(acc, 1) = acc*R^-1 */
    mont_mul(r, &acc, &uno, m, n0, n);
    return 0;
}
