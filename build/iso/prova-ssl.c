/* =============================================================================
 * tools/iso/prova-ssl.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * La libreria crittografica di OpenSSL, dentro EX-OS.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ SI USA UN VETTORE DI PROVA PUBBLICATO, NON UN VALORE INVENTATO
 *
 * SHA-256("abc") e' il primo vettore di prova di FIPS 180-4, e vale
 *
 *     ba7816bf 8f01cfea 414140de 5dae2223
 *     b00361a3 96177a9c b410ff61 f20015ad
 *
 * Confrontare con un valore preso da un'altra esecuzione dello stesso
 * programma proverebbe soltanto che il programma e' ripetibile — cosa
 * che riesce benissimo anche a un'implementazione sbagliata. Un vettore
 * pubblicato prova che il risultato e' quello giusto.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ RAND_bytes PUO' LEGITTIMAMENTE FALLIRE, E VA DETTO INVECE CHE
 * NASCOSTO
 *
 * Sotto c'e' getentropy() della libc, e sotto ancora la raccolta di
 * entropia del kernel (kernel/arch/x86/entropia.c), che RIFIUTA di dare
 * byte quando non ne ha abbastanza. Su una macchina appena accesa, senza
 * RDRAND e senza nessuno che abbia toccato la tastiera, il rifiuto e' la
 * risposta CORRETTA.
 *
 * Un programma che in quel caso proseguisse con quello che c'e' e'
 * esattamente il programma che genera chiavi indovinabili. Qui si stampa
 * che non ce n'era e si dice cosa fare.
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

static const unsigned char atteso[32] = {
    0xba,0x78,0x16,0xbf, 0x8f,0x01,0xcf,0xea,
    0x41,0x41,0x40,0xde, 0x5d,0xae,0x22,0x23,
    0xb0,0x03,0x61,0xa3, 0x96,0x17,0x7a,0x9c,
    0xb4,0x10,0xff,0x61, 0xf2,0x00,0x15,0xad
};

static void stampa(const char *eti, const unsigned char *d, int n)
{
    int i;

    printf("  %s", eti);
    for (i = 0; i < n; i++) printf("%02x", d[i]);
    printf("\n");
}

int main(void)
{
    unsigned char digest[32], casuali[16];
    unsigned int  dlen = 0;
    int           i, uguale, tutti_zero;

    printf("OpenSSL dentro EX-OS\n\n");
    printf("  versione   %s\n\n", OpenSSL_version(OPENSSL_VERSION_STRING));

    /* ⚠️ EVP E NON SHA256_Init/Update/Final: quelle sono l'API di
     * OpenSSL 1.x, deprecata da tempo, e questa libreria e' configurata
     * con `no-deprecated` — non esistono proprio. EVP_Digest e' anche la
     * via che passa dai provider, cioe' quella che verrebbe usata da
     * qualunque codice vero. */
    if (EVP_Digest("abc", 3, digest, &dlen, EVP_sha256(), NULL) != 1 ||
        dlen != 32) {
        printf("  EVP_Digest ha fallito.\n");
        return 1;
    }

    stampa("sha256(abc) ", digest, 32);
    stampa("atteso      ", atteso, 32);

    uguale = (memcmp(digest, atteso, 32) == 0);
    printf("\n  %s\n\n", uguale
           ? "Corrisponde al vettore di FIPS 180-4."
           : "NON corrisponde: la libreria calcola male.");

    if (!uguale) return 1;

    /* --- il generatore, che passa dall'entropia del kernel --- */
    if (RAND_bytes(casuali, sizeof(casuali)) != 1) {
        printf("  RAND_bytes non ha potuto servire.\n");
        printf("  Non e' un difetto: il kernel non aveva abbastanza\n");
        printf("  entropia e lo ha detto invece di inventarla. Batti\n");
        printf("  qualche tasto e rilancia.\n");
        return 2;
    }

    stampa("RAND_bytes  ", casuali, 16);

    tutti_zero = 1;
    for (i = 0; i < (int)sizeof(casuali); i++) if (casuali[i]) tutti_zero = 0;
    printf("\n  %s\n", tutti_zero
           ? "Sono tutti zero: qualcosa non ha funzionato."
           : "Il generatore e' stato seminato dall'entropia del kernel.");

    return tutti_zero ? 3 : 0;
}
