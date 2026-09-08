/* =============================================================================
 * lib/excrypt/aes.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * AES (FIPS-197) — solo la cifratura, e CCM sopra.
 *
 * ! SOLO LA CIFRATURA, E NON E' UN LAVORO A META'. CCM — il modo che usa
 * WPA2 — e' fatto di CTR e CBC-MAC, e tutt'e due chiamano AES SOLO in avanti,
 * anche per decifrare: in CTR si cifra il contatore e si mette in XOR. La
 * tabella inversa e le colonne inverse sono duecento righe che nessuno
 * chiamerebbe mai, e duecento righe che nessuno chiama sono duecento righe che
 * nessuno prova.
 *
 * ! E' LA SCELTA OPPOSTA A QUELLA DI extls.h, e le due si tengono insieme.
 * Li' si e' evitato RSA per non scrivere una libreria di grandi numeri; qui si
 * SCRIVE AES perche' senza non esiste WPA2: il Wi-Fi protetto e' CCMP, cioe'
 * AES-CCM, e non c'e' una strada alternativa come ChaCha20 lo era per TLS.
 * Quando una cosa non ha alternative si fa; quando ne ha, si sceglie la piu'
 * piccola.
 *
 * ! NIENTE TABELLE T. Le implementazioni veloci precalcolano quattro tabelle
 * da un kilobyte che fondono SubBytes, ShiftRows e MixColumns; sono anche
 * quelle che perdono la chiave attraverso la cache, ed e' il motivo per cui
 * le librerie serie oggi le evitano. Qui si fa il conto per colonne: piu'
 * lento e piu' corto, e su una scheda a 54 Mbit/s la differenza non si vede.
 * ============================================================================= */

#include "excrypt.h"

/* La S-box di FIPS-197. E' l'unica tabella: 256 byte. */
static const unsigned char SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

/* xtime: moltiplicazione per 2 nel campo di Rijndael. Il polinomio di
 * riduzione e' 0x11b, e il byte 0x1b e' quello che ne resta a 8 bit. */
static unsigned char xtime(unsigned char a)
{
    return (unsigned char)((a << 1) ^ ((a & 0x80) ? 0x1b : 0x00));
}

/* L'espansione della chiave: da 16, 24 o 32 byte alle (giri+1) sottochiavi.
 *
 * ! I GIRI DIPENDONO DALLA LUNGHEZZA, non sono una costante: 10 per 128 bit,
 * 12 per 192, 14 per 256. Sbagliarli non da' un errore, da' un cifrario
 * diverso — e nei vettori si vede subito, che e' il motivo per cui i vettori
 * ci sono. */
int aes_chiave(AesChiave *c, const unsigned char *chiave, unsigned int byte)
{
    unsigned int nk, i, j;
    unsigned char rcon = 1;

    if (c == 0 || chiave == 0) return -1;
    if (byte != 16 && byte != 24 && byte != 32) return -1;

    nk = byte / 4;
    c->giri = nk + 6;

    for (i = 0; i < byte; i++) c->rk[i] = chiave[i];

    for (i = nk; i < 4u * (c->giri + 1); i++) {
        unsigned char t[4];

        for (j = 0; j < 4; j++) t[j] = c->rk[(i - 1) * 4 + j];

        if (i % nk == 0) {
            unsigned char g = t[0];

            t[0] = (unsigned char)(SBOX[t[1]] ^ rcon);
            t[1] = SBOX[t[2]];
            t[2] = SBOX[t[3]];
            t[3] = SBOX[g];
            rcon = xtime(rcon);
        } else if (nk > 6 && i % nk == 4) {
            /* ! QUESTO RAMO ESISTE SOLO PER AES-256, e si dimentica sempre.
             * Senza, la chiave a 256 bit si espande in modo sbagliato dal
             * quinto blocco in poi: 128 e 192 continuano a funzionare, e il
             * difetto si vede solo nel vettore da 256. */
            for (j = 0; j < 4; j++) t[j] = SBOX[t[j]];
        }

        for (j = 0; j < 4; j++)
            c->rk[i * 4 + j] = (unsigned char)(c->rk[(i - nk) * 4 + j] ^ t[j]);
    }
    return 0;
}

/* Un blocco da 16 byte, in avanti. Lo stato e' la colonna-per-colonna di
 * FIPS-197: s[r][c] sta in b[c*4+r]. */
void aes_cifra(const AesChiave *c, const unsigned char in[16],
               unsigned char out[16])
{
    unsigned char s[16];
    unsigned int  giro, i;

    for (i = 0; i < 16; i++) s[i] = (unsigned char)(in[i] ^ c->rk[i]);

    for (giro = 1; giro <= c->giri; giro++) {
        unsigned char t[16];

        /* SubBytes e ShiftRows insieme: la riga r scorre di r posti. */
        for (i = 0; i < 16; i++) {
            unsigned int r = i % 4, col = i / 4;
            unsigned int da = ((col + r) % 4) * 4 + r;

            t[i] = SBOX[s[da]];
        }

        if (giro != c->giri) {
            /* MixColumns, una colonna per volta. */
            for (i = 0; i < 4; i++) {
                unsigned char *p = t + i * 4;
                unsigned char a0 = p[0], a1 = p[1], a2 = p[2], a3 = p[3];
                unsigned char x = (unsigned char)(a0 ^ a1 ^ a2 ^ a3);

                p[0] = (unsigned char)(a0 ^ x ^ xtime((unsigned char)(a0 ^ a1)));
                p[1] = (unsigned char)(a1 ^ x ^ xtime((unsigned char)(a1 ^ a2)));
                p[2] = (unsigned char)(a2 ^ x ^ xtime((unsigned char)(a2 ^ a3)));
                p[3] = (unsigned char)(a3 ^ x ^ xtime((unsigned char)(a3 ^ a0)));
            }
        }

        for (i = 0; i < 16; i++)
            s[i] = (unsigned char)(t[i] ^ c->rk[giro * 16 + i]);
    }

    for (i = 0; i < 16; i++) out[i] = s[i];
}

/* =============================================================================
 * CCM (RFC 3610) — il modo di WPA2
 *
 * ! E' DUE COSE IN UNA, E VANNO FATTE NELL'ORDINE GIUSTO: un CBC-MAC che
 * copre intestazione e testo in chiaro, e un CTR che cifra il testo e il MAC.
 * Il MAC si calcola sul CHIARO e si cifra dopo: farlo sul cifrato sarebbe un
 * altro schema, e non interoperabile.
 *
 * ! IL BLOCCO ZERO DEL CTR E' RISERVATO AL MAC. Il testo comincia dal blocco
 * uno. Usare il blocco zero per i dati vuol dire un pacchetto che si decifra
 * da solo ma che nessun altro accetta.
 * ========================================================================== */

static void ccm_b0(unsigned char b[16], const unsigned char *nonce,
                   unsigned int nonce_n, unsigned int aad_n,
                   unsigned int testo_n, unsigned int tag_n)
{
    unsigned int q = 15 - nonce_n;   /* byte della lunghezza */
    unsigned int i;
    unsigned int len = testo_n;

    b[0] = (unsigned char)(((aad_n > 0) ? 0x40 : 0x00) |
                           (((tag_n - 2) / 2) << 3) | (q - 1));
    for (i = 0; i < nonce_n; i++) b[1 + i] = nonce[i];
    for (i = 0; i < q; i++) {
        b[15 - i] = (unsigned char)(len & 0xFF);
        len >>= 8;
    }
}

static void ccm_a_i(unsigned char a[16], const unsigned char *nonce,
                    unsigned int nonce_n, unsigned int i)
{
    unsigned int q = 15 - nonce_n, k;

    a[0] = (unsigned char)(q - 1);
    for (k = 0; k < nonce_n; k++) a[1 + k] = nonce[k];
    for (k = 0; k < q; k++) {
        a[15 - k] = (unsigned char)(i & 0xFF);
        i >>= 8;
    }
}

static void xor_blocco(unsigned char *x, const unsigned char *b, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) x[i] ^= b[i];
}

/* Il CBC-MAC di CCM: B0, l'AAD con la sua lunghezza davanti, il testo in
 * chiaro. Rende in `x` il MAC ancora da cifrare.
 *
 * ! STA IN UNA FUNZIONE SUA PERCHE' LO USANO IN DUE, e la prima versione di
 * questo file non lo faceva: cifratura e decifratura calcolavano lo stesso MAC
 * con due pezzi di codice diversi, e due copie dello stesso conto sono due
 * occasioni di sbagliarlo in un modo solo. */
static void ccm_mac(const AesChiave *c, unsigned char x[16],
                    const unsigned char *nonce, unsigned int nonce_n,
                    const unsigned char *aad, unsigned int aad_n,
                    const unsigned char *chiaro, unsigned int n,
                    unsigned int tag_n)
{
    unsigned int i, resto;

    ccm_b0(x, nonce, nonce_n, aad_n, n, tag_n);
    aes_cifra(c, x, x);

    if (aad_n > 0) {
        unsigned char b[16];
        unsigned int  off = 0, primo;

        for (i = 0; i < 16; i++) b[i] = 0;

        /* ! LA LUNGHEZZA DELL'AAD SI CODIFICA, E LA FORMA DIPENDE DA QUANTO
         * E' LUNGA: due byte sotto 65280, sei sopra. In WPA2 l'AAD e' sempre
         * corta, ma scrivere solo il caso corto vuol dire un pezzo che
         * funziona finche' non serve. */
        if (aad_n < 0xFF00u) {
            b[0] = (unsigned char)(aad_n >> 8);
            b[1] = (unsigned char)(aad_n & 0xFF);
            primo = 2;
        } else {
            b[0] = 0xFF; b[1] = 0xFE;
            b[2] = (unsigned char)(aad_n >> 24);
            b[3] = (unsigned char)(aad_n >> 16);
            b[4] = (unsigned char)(aad_n >> 8);
            b[5] = (unsigned char)(aad_n & 0xFF);
            primo = 6;
        }

        while (off < aad_n) {
            unsigned int spazio = 16 - primo;
            unsigned int q = (aad_n - off < spazio) ? (aad_n - off) : spazio;

            for (i = 0; i < q; i++) b[primo + i] = aad[off + i];
            for (i = primo + q; i < 16; i++) b[i] = 0;

            xor_blocco(x, b, 16);
            aes_cifra(c, x, x);
            off += q;
            primo = 0;
            for (i = 0; i < 16; i++) b[i] = 0;
        }
    }

    for (i = 0; i + 16 <= n; i += 16) {
        xor_blocco(x, chiaro + i, 16);
        aes_cifra(c, x, x);
    }
    resto = n - i;
    if (resto > 0) {
        unsigned char b[16];
        unsigned int  k;

        for (k = 0; k < 16; k++) b[k] = (k < resto) ? chiaro[i + k] : 0;
        xor_blocco(x, b, 16);
        aes_cifra(c, x, x);
    }
}

/* Il CTR di CCM: cifra o decifra, che e' la stessa operazione.
 *
 * ! IL BLOCCO ZERO NON SI USA QUI: e' riservato al tag. Il testo comincia dal
 * blocco uno, e usare lo zero darebbe un pacchetto che si decifra da solo e
 * che nessun altro accetta. */
static void ccm_ctr(const AesChiave *c, const unsigned char *nonce,
                    unsigned int nonce_n, const unsigned char *in,
                    unsigned char *out, unsigned int n)
{
    unsigned char a[16], s[16];
    unsigned int  i;

    for (i = 0; i < n; i += 16) {
        unsigned int q = (n - i < 16) ? (n - i) : 16;
        unsigned int k;

        ccm_a_i(a, nonce, nonce_n, (i / 16) + 1);
        aes_cifra(c, a, s);
        for (k = 0; k < q; k++) out[i + k] = (unsigned char)(in[i + k] ^ s[k]);
    }
}

/* I parametri che i due sensi controllano allo stesso modo. */
static int ccm_misure_ok(unsigned int nonce_n, unsigned int tag_n)
{
    if (nonce_n < 7 || nonce_n > 13) return 0;
    if (tag_n < 4 || tag_n > 16 || (tag_n % 2) != 0) return 0;
    return 1;
}

int aes_ccm_cifra(const AesChiave *c,
                  const unsigned char *nonce, unsigned int nonce_n,
                  const unsigned char *aad, unsigned int aad_n,
                  const unsigned char *in, unsigned char *out, unsigned int n,
                  unsigned char *tag, unsigned int tag_n)
{
    unsigned char x[16], a[16], s[16];
    unsigned int  i;

    if (c == 0 || nonce == 0 || tag == 0) return -1;
    if (!ccm_misure_ok(nonce_n, tag_n)) return -1;

    /* Il MAC si calcola sul CHIARO, e si cifra dopo: farlo sul cifrato
     * sarebbe un altro schema, e non interoperabile. */
    ccm_mac(c, x, nonce, nonce_n, aad, aad_n, in, n, tag_n);

    ccm_a_i(a, nonce, nonce_n, 0);
    aes_cifra(c, a, s);
    for (i = 0; i < tag_n; i++) tag[i] = (unsigned char)(x[i] ^ s[i]);

    ccm_ctr(c, nonce, nonce_n, in, out, n);
    return 0;
}

int aes_ccm_decifra(const AesChiave *c,
                    const unsigned char *nonce, unsigned int nonce_n,
                    const unsigned char *aad, unsigned int aad_n,
                    const unsigned char *in, unsigned char *out, unsigned int n,
                    const unsigned char *tag, unsigned int tag_n)
{
    unsigned char x[16], a[16], s[16];
    unsigned int  i, diverso = 0;

    if (c == 0 || nonce == 0 || tag == 0) return -1;
    if (!ccm_misure_ok(nonce_n, tag_n)) return -1;

    /* Prima si mette in chiaro, poi si rifa' il MAC sul chiaro: e' l'ordine
     * che impone lo schema, perche' il MAC copre il chiaro e non il cifrato. */
    ccm_ctr(c, nonce, nonce_n, in, out, n);
    ccm_mac(c, x, nonce, nonce_n, aad, aad_n, out, n, tag_n);

    ccm_a_i(a, nonce, nonce_n, 0);
    aes_cifra(c, a, s);

    /* ! IL CONFRONTO E' A TEMPO COSTANTE, e non e' pedanteria: uscire al primo
     * byte diverso dice a chi prova quanti byte ha gia' indovinato, e un tag
     * si indovina un byte per volta se il confronto glielo permette. */
    for (i = 0; i < tag_n; i++)
        diverso |= (unsigned int)((x[i] ^ s[i]) ^ tag[i]);

    /* ! E SE IL TAG NON TORNA, IL CHIARO NON SI CONSEGNA. Restituire i byte
     * decifrati «tanto poi chi chiama controlla il codice» e' il modo in cui
     * nascono gli oracoli: chi attacca manda pacchetti storti e guarda cosa
     * succede a valle. Qui si azzera. */
    if (diverso != 0) {
        for (i = 0; i < n; i++) out[i] = 0;
        return -1;
    }
    return 0;
}
