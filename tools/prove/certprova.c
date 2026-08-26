/* =============================================================================
 * tools/prove/certprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il banco di prova della catena, che gira SULL'HOST.
 *
 *     certprova <AAAAMMGGhhmmssZ|-> <radici...> -- <catena...>
 *
 * e stampa il verdetto: 0 se la catena e' buona, altrimenti il motivo.
 *
 * ! SHA-256 LO METTE OPENSSL, e non e' una scorciatoia: dentro EX-OS lo mette
 * la libc, che ce l'ha gia'. Qui si sta provando la CATENA — chi firma chi,
 * chi e' una CA, cosa e' scaduto — non l'impronta, che e' provata altrove e da
 * altri. Prendere quella di riferimento toglie di mezzo una variabile invece
 * di aggiungerne una copia.
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include <openssl/sha.h>
#include "excert.h"

void sha256(const void *dati, unsigned int len, unsigned char out[32])
{
    SHA256((const unsigned char *)dati, len, out);
}

/* ! E SHA-384 PER LO STESSO MOTIVO, da quando excert verifica ECDSA su P-384:
 * quella curva firma con SHA-384, e senza questo ponte il banco non si
 * collegava nemmeno — cioe' la prova della catena non girava piu' affatto. */
void sha384(const void *dati, unsigned int len, unsigned char out[48])
{
    SHA384((const unsigned char *)dati, len, out);
}

/* ! E SHA-512, da quando excert verifica anche sha512WithRSAEncryption. Il
 * banco deve avere TUTTE le impronte che il codice provato puo' chiamare, o
 * non compila — ed e' successo di nuovo: l'ultima volta era excurva. */
void sha512(const void *dati, unsigned int len, unsigned char out[64]);
void sha512(const void *dati, unsigned int len, unsigned char out[64])
{
    SHA512((const unsigned char *)dati, len, out);
}

static unsigned char buf[64][65536];
static unsigned int  misura[64];
static unsigned int  quanti_file;

static int leggi(const char *nome, unsigned char **p, unsigned int *n)
{
    FILE *f = fopen(nome, "rb");
    size_t letti;

    if (!f) return -1;
    if (quanti_file >= 64) { fclose(f); return -1; }
    letti = fread(buf[quanti_file], 1, sizeof(buf[0]), f);
    fclose(f);
    misura[quanti_file] = (unsigned int)letti;
    *p = buf[quanti_file];
    *n = (unsigned int)letti;
    quanti_file++;
    return 0;
}

int main(int argc, char **argv)
{
    static ExMagazzino m;
    static ExCert      catena[10];
    unsigned int       nc = 0;
    int                i, dopo = 0;
    const char        *adesso;

    if (argc < 3) { printf("uso: certprova <data|-> <radici...> -- <catena...>\n");
                    return 2; }

    adesso = (argv[1][0] == '-') ? 0 : argv[1];

    for (i = 2; i < argc; i++) {
        unsigned char *p;
        unsigned int   n;

        if (strcmp(argv[i], "--") == 0) { dopo = 1; continue; }
        if (leggi(argv[i], &p, &n) != 0) { printf("999\n"); return 1; }

        if (!dopo) {
            excert_magazzino_aggiungi(&m, p, n);
        } else {
            if (nc >= 10) { printf("999\n"); return 1; }
            if (excert_analizza(p, n, &catena[nc]) != 0) { printf("-1\n"); return 0; }
            nc++;
        }
    }

    {
        unsigned int anello = 0;
        int esito = excert_catena_valida(catena, nc, &m, adesso, &anello);

        if (esito != EXCERT_OK)
            fprintf(stderr, "anello %u: %s\n", anello, excert_perche(esito));
        printf("%d\n", esito);
    }
    return 0;
}
