/* =============================================================================
 * tools/prove/wpaprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * I pezzi di crittografia che vuole il Wi-Fi, contro i vettori dei loro
 * documenti. Gira SULL'HOST.
 *
 * ! I VETTORI SONO L'UNICA PROVA CHE CONTA, e qui piu' che altrove: un AES
 * sbagliato non da' un errore, da' un pacchetto che l'access point scarta —
 * e da fuori sembra un problema di antenna. I numeri qui sotto vengono da
 * FIPS-197 (AES), RFC 3610 (CCM), RFC 3174 (SHA-1), RFC 2202 (HMAC-SHA1),
 * RFC 6070 (PBKDF2) e IEEE 802.11-2007 H.4 (la PSK di WPA).
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include "excrypt.h"

static int passate = 0, fallite = 0;

static void esito(const char *nome, int ok)
{
    if (ok) { passate++; printf("  [ok]      %s\n", nome); }
    else    { fallite++; printf("  [FALLITO] %s\n", nome); }
}

static void mostra(const char *eti, const unsigned char *b, unsigned int n)
{
    unsigned int i;
    printf("            %s", eti);
    for (i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

static int uguali(const unsigned char *a, const unsigned char *b, unsigned int n,
                  const char *nome)
{
    if (memcmp(a, b, n) == 0) return 1;
    mostra("atteso ", b, n);
    mostra("avuto  ", a, n);
    (void)nome;
    return 0;
}

int main(void)
{
    printf("wpaprova - i pezzi del Wi-Fi contro i vettori dei loro documenti\n\n");

    /* --- AES-128, FIPS-197 appendice B ------------------------------------ */
    {
        static const unsigned char k[16] = {
            0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
            0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c };
        static const unsigned char in[16] = {
            0x32,0x43,0xf6,0xa8,0x88,0x5a,0x30,0x8d,
            0x31,0x31,0x98,0xa2,0xe0,0x37,0x07,0x34 };
        static const unsigned char att[16] = {
            0x39,0x25,0x84,0x1d,0x02,0xdc,0x09,0xfb,
            0xdc,0x11,0x85,0x97,0x19,0x6a,0x0b,0x32 };
        AesChiave c; unsigned char out[16];

        aes_chiave(&c, k, 16);
        aes_cifra(&c, in, out);
        esito("AES-128 (FIPS-197 B)", uguali(out, att, 16, "aes128"));
    }

    /* --- AES-192 e AES-256, FIPS-197 appendice C -------------------------- */
    {
        static const unsigned char in[16] = {
            0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
            0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff };
        static const unsigned char k192[24] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,
            0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17 };
        static const unsigned char a192[16] = {
            0xdd,0xa9,0x7c,0xa4,0x86,0x4c,0xdf,0xe0,
            0x6e,0xaf,0x70,0xa0,0xec,0x0d,0x71,0x91 };
        static const unsigned char k256[32] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,
            0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
            0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f };
        static const unsigned char a256[16] = {
            0x8e,0xa2,0xb7,0xca,0x51,0x67,0x45,0xbf,
            0xea,0xfc,0x49,0x90,0x4b,0x49,0x60,0x89 };
        AesChiave c; unsigned char out[16];

        aes_chiave(&c, k192, 24);
        aes_cifra(&c, in, out);
        esito("AES-192 (FIPS-197 C.2)", uguali(out, a192, 16, "aes192"));

        /* ! IL VETTORE DA 256 E' QUELLO CHE TROVA IL RAMO DIMENTICATO
         * dell'espansione della chiave (i % nk == 4): con 128 e 192 quel ramo
         * non si esegue mai, e un'espansione rotta li' passa le altre due
         * prove senza una piega. */
        aes_chiave(&c, k256, 32);
        aes_cifra(&c, in, out);
        esito("AES-256 (FIPS-197 C.3)", uguali(out, a256, 16, "aes256"));
    }

    /* --- CCM, RFC 3610 pacchetto di prova 1 ------------------------------- */
    {
        static const unsigned char k[16] = {
            0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7,
            0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf };
        static const unsigned char nonce[13] = {
            0x00,0x00,0x00,0x03,0x02,0x01,0x00,0xa0,
            0xa1,0xa2,0xa3,0xa4,0xa5 };
        static const unsigned char aad[8] = {
            0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07 };
        static const unsigned char msg[23] = {
            0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
            0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
            0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e };
        static const unsigned char att[31] = {
            0x58,0x8c,0x97,0x9a,0x61,0xc6,0x63,0xd2,
            0xf0,0x66,0xd0,0xc2,0xc0,0xf9,0x89,0x80,
            0x6d,0x5f,0x6b,0x61,0xda,0xc3,0x84,
            0x17,0xe8,0xd1,0x2c,0xfd,0xf9,0x26,0xe0 };
        AesChiave c;
        unsigned char out[23], tag[8], chiaro[23];

        aes_chiave(&c, k, 16);
        aes_ccm_cifra(&c, nonce, 13, aad, 8, msg, out, 23, tag, 8);
        esito("CCM: il cifrato (RFC 3610 n.1)", uguali(out, att, 23, "ccm-c"));
        esito("CCM: il tag     (RFC 3610 n.1)", uguali(tag, att + 23, 8, "ccm-t"));

        /* E il giro completo torna indietro. */
        esito("CCM: e si decifra",
              aes_ccm_decifra(&c, nonce, 13, aad, 8, out, chiaro, 23, tag, 8) == 0 &&
              memcmp(chiaro, msg, 23) == 0);

        /* ! UN BIT CAMBIATO DEVE FAR FALLIRE, ed e' la prova che vale piu' di
         * tutte: un CCM che decifra sempre non protegge niente. */
        {
            unsigned char rovinato[23];
            unsigned char fuori[23];

            memcpy(rovinato, out, 23);
            rovinato[7] ^= 0x01;
            esito("CCM: un bit cambiato e il tag non torna",
                  aes_ccm_decifra(&c, nonce, 13, aad, 8, rovinato, fuori, 23,
                                  tag, 8) != 0);
        }
        {
            unsigned char tag_storto[8];
            unsigned char fuori[23];

            memcpy(tag_storto, tag, 8);
            tag_storto[0] ^= 0x80;
            esito("CCM: un tag storto si rifiuta",
                  aes_ccm_decifra(&c, nonce, 13, aad, 8, out, fuori, 23,
                                  tag_storto, 8) != 0);
        }
    }

    /* --- SHA-1, RFC 3174 -------------------------------------------------- */
    {
        static const unsigned char a1[20] = {
            0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,
            0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d };
        static const unsigned char a2[20] = {
            0x84,0x98,0x3e,0x44,0x1c,0x3b,0xd2,0x6e,0xba,0xae,
            0x4a,0xa1,0xf9,0x51,0x29,0xe5,0xe5,0x46,0x70,0xf1 };
        static const unsigned char vuoto[20] = {
            0xda,0x39,0xa3,0xee,0x5e,0x6b,0x4b,0x0d,0x32,0x55,
            0xbf,0xef,0x95,0x60,0x18,0x90,0xaf,0xd8,0x07,0x09 };
        unsigned char out[20];

        sha1("abc", 3, out);
        esito("SHA-1 di «abc»", uguali(out, a1, 20, "sha1a"));

        sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, out);
        esito("SHA-1 su 56 byte (due blocchi)", uguali(out, a2, 20, "sha1b"));

        /* ! IL MESSAGGIO VUOTO TROVA IL RIEMPIMENTO SBAGLIATO, e non costa
         * niente provarlo: e' il caso in cui il blocco e' fatto di solo
         * padding. */
        sha1("", 0, out);
        esito("SHA-1 del messaggio vuoto", uguali(out, vuoto, 20, "sha1v"));
    }

    /* --- HMAC-SHA1, RFC 2202 --------------------------------------------- */
    {
        unsigned char k[20], out[20];
        static const unsigned char a1[20] = {
            0xb6,0x17,0x31,0x86,0x55,0x05,0x72,0x64,0xe2,0x8b,
            0xc0,0xb6,0xfb,0x37,0x8c,0x8e,0xf1,0x46,0xbe,0x00 };
        static const unsigned char klungo[80] = {
            0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
            0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
            0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
            0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
            0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
            0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
            0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,
            0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa,0xaa };
        static const unsigned char a5[20] = {
            0xaa,0x4a,0xe5,0xe1,0x52,0x72,0xd0,0x0e,0x95,0x70,
            0x56,0x37,0xce,0x8a,0x3b,0x55,0xed,0x40,0x21,0x12 };
        int i;

        for (i = 0; i < 20; i++) k[i] = 0x0b;
        hmac_sha1(k, 20, (const unsigned char *)"Hi There", 8, out);
        esito("HMAC-SHA1 (RFC 2202 n.1)", uguali(out, a1, 20, "hmac1"));

        /* ! LA CHIAVE PIU' LUNGA DEL BLOCCO E' IL CASO CHE SI SBAGLIA: va
         * accorciata con l'impronta, non troncata. Con chiavi corte le due
         * strade danno lo stesso risultato, e il difetto non si vede. */
        hmac_sha1(klungo, 80,
                  (const unsigned char *)"Test Using Larger Than Block-Size Key - Hash Key First",
                  54, out);
        esito("HMAC-SHA1 con chiave da 80 byte (RFC 2202 n.6)",
              uguali(out, a5, 20, "hmac6"));
    }

    /* --- PBKDF2-HMAC-SHA1, RFC 6070 -------------------------------------- */
    {
        static const unsigned char a1[20] = {
            0x0c,0x60,0xc8,0x0f,0x96,0x1f,0x0e,0x71,0xf3,0xa9,
            0xb5,0x24,0xaf,0x60,0x12,0x06,0x2f,0xe0,0x37,0xa6 };
        /* ! QUESTE DUE ATTESE ERANO SCAMBIATE, e la prova ha accusato il
         * codice invece del vettore: con 4096 giri usciva la risposta di 2
         * giri, che e' esattamente cio' che si vede quando le due righe sono
         * al contrario. A dirlo e' stata la prova QUI SOTTO — la PSK di WPA,
         * che fa 4096 giri e passava — perche' due vettori indipendenti che
         * si contraddicono lasciano una sola conclusione possibile. Un
         * vettore trascritto male e' un difetto della prova, e costa quanto
         * un difetto del codice. */
        static const unsigned char a2[20] = {      /* 2 giri */
            0xea,0x6c,0x01,0x4d,0xc7,0x2d,0x6f,0x8c,0xcd,0x1e,
            0xd9,0x2a,0xce,0x1d,0x41,0xf0,0xd8,0xde,0x89,0x57 };
        static const unsigned char a4096[20] = {   /* 4096 giri */
            0x4b,0x00,0x79,0x01,0xb7,0x65,0x48,0x9a,0xbe,0xad,
            0x49,0xd9,0x26,0xf7,0x21,0xd0,0x65,0xa4,0x29,0xc1 };
        unsigned char out[32];

        pbkdf2_sha1((const unsigned char *)"password", 8,
                    (const unsigned char *)"salt", 4, 1, out, 20);
        esito("PBKDF2 1 giro (RFC 6070)", uguali(out, a1, 20, "pb1"));

        pbkdf2_sha1((const unsigned char *)"password", 8,
                    (const unsigned char *)"salt", 4, 2, out, 20);
        esito("PBKDF2 2 giri (RFC 6070)", uguali(out, a2, 20, "pb2"));

        pbkdf2_sha1((const unsigned char *)"password", 8,
                    (const unsigned char *)"salt", 4, 4096, out, 20);
        esito("PBKDF2 4096 giri (RFC 6070)", uguali(out, a4096, 20, "pb4096"));
    }

    /* --- La PSK di WPA, IEEE 802.11-2007 appendice H.4 -------------------- */
    {
        /* password «password», SSID «IEEE»: e' il vettore che dimostra che
         * l'intera catena — 4096 giri, SSID come sale, 32 byte — e' quella
         * che usa un access point vero. */
        static const unsigned char att[32] = {
            0xf4,0x2c,0x6f,0xc5,0x2d,0xf0,0xeb,0xef,0x9e,0xbb,0x4b,0x90,
            0xb3,0x8a,0x5f,0x90,0x2e,0x83,0xfe,0x1b,0x13,0x5a,0x70,0xe2,
            0x3a,0xed,0x76,0x2e,0x97,0x10,0xa1,0x2e };
        unsigned char psk[32];

        pbkdf2_sha1((const unsigned char *)"password", 8,
                    (const unsigned char *)"IEEE", 4, 4096, psk, 32);
        esito("la PSK di WPA (802.11-2007 H.4)", uguali(psk, att, 32, "psk"));
    }

    printf("\n%d passate, %d fallite\n", passate, fallite);
    return fallite == 0 ? 0 : 1;
}
