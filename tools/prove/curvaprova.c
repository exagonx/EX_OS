/* =============================================================================
 * tools/prove/curvaprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il banco di prova di ECDSA: le firme le FA OpenSSL, noi le guardiamo.
 *
 * ! UN VERIFICATORE CHE DICE SEMPRE «SI'» PASSA TUTTE LE FIRME BUONE, e questa
 * e' l'unica cosa che conta saperne. Per questo il driver legge anche firme
 * ROVINATE — un bit girato in r, in s, nell'impronta, nella chiave — e si
 * aspetta un no.
 *
 *     curvaprova <curva> <chiave.hex> <impronta.hex> <r.hex> <s.hex>
 *
 * `curva` e' 256 o 384, ed e' quella della CHIAVE: l'impronta puo' essere di
 * misura diversa, che e' proprio il caso interessante.
 *
 * Rende 0 se la firma e' buona, 1 se non lo e', 2 se gli argomenti sono
 * sbagliati.
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include "excurva.h"

static unsigned int da_hex(const char *s, unsigned char *b, unsigned int max)
{
    unsigned int n = 0;

    while (*s && n < max) {
        int a = -1, c = -1, i;

        for (i = 0; i < 2; i++) {
            char h = s[i];
            int  v = -1;

            if (h >= '0' && h <= '9') v = h - '0';
            else if (h >= 'a' && h <= 'f') v = h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') v = h - 'A' + 10;
            if (v < 0) return n;
            if (i == 0) a = v; else c = v;
        }
        b[n++] = (unsigned char)((a << 4) | c);
        s += 2;
    }
    return n;
}

int main(int argc, char **argv)
{
    unsigned char q[128], e[64], r[64], s[64];
    unsigned int  qn, en, rn, sn;
    int           curva = EXCURVA_P256;

    if (argc < 6) {
        fprintf(stderr, "uso: curvaprova 256|384 PUNTO IMPRONTA R S\n");
        return 2;
    }

    if (argv[1][0] == '3') curva = EXCURVA_P384;

    qn = da_hex(argv[2], q, sizeof(q));
    en = da_hex(argv[3], e, sizeof(e));
    rn = da_hex(argv[4], r, sizeof(r));
    sn = da_hex(argv[5], s, sizeof(s));

    return excurva_verifica(curva, q, qn, e, en, r, rn, s, sn) == 0 ? 0 : 1;
}
