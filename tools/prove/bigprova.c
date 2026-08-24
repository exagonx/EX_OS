/* =============================================================================
 * tools/prove/bigprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * =============================================================================
 *
 * Il banco di prova degli interi lunghi, che gira SULL'HOST.
 *
 * ! SI PROVA FUORI DA EX-OS PERCHE' exbig.c NON SA COS'E' EX-OS: legge numeri
 * e rende numeri, non fa syscall, non alloca, non include la libc. Provarlo
 * dentro una macchina virtuale vorrebbe dire un giro di costruzione e un
 * avvio per ogni riga cambiata — la stessa ragione per cui il lettore
 * TrueType si prova qui accanto.
 *
 * ! E IL RIFERIMENTO E' DI QUALCUN ALTRO: gli interi di Python, che sono
 * esatti e che nessuno di noi ha scritto. `pow(a, e, m)` e' la risposta
 * giusta per definizione; confrontare exbig con exbig non proverebbe niente,
 * proverebbe solo che e' coerente con se stesso.
 *
 * Questo programma non decide niente: legge righe «a e m» in esadecimale,
 * stampa il risultato in esadecimale, e chi confronta e' bigprova.py.
 *
 *     cc -o /tmp/bigprova tools/prove/bigprova.c lib/exbig/exbig.c -I lib/exbig
 *     python3 tools/prove/bigprova.py
 * ============================================================================= */

#include <stdio.h>
#include <string.h>
#include "exbig.h"

/* Da esadecimale a byte, poi a ExBig: la stessa strada che fara' un DER. */
static int leggi_hex(const char *s, ExBig *r)
{
    unsigned char b[EXBIG_PAROLE * 4];
    unsigned int  n = 0, i;
    size_t        len = strlen(s);

    if (len % 2) return -1;                     /* mezzo byte non esiste */
    if (len / 2 > sizeof(b)) return -1;

    for (i = 0; i < len; i += 2) {
        unsigned int v;
        if (sscanf(s + i, "%2x", &v) != 1) return -1;
        b[n++] = (unsigned char)v;
    }
    return exbig_da_byte(r, b, n);
}

static void stampa_hex(const ExBig *a)
{
    unsigned char b[EXBIG_PAROLE * 4];
    unsigned int  len = (exbig_bit(a) + 7) / 8, i;

    if (len == 0) { printf("00\n"); return; }
    if (exbig_a_byte(a, b, len) != 0) { printf("ERRORE\n"); return; }
    for (i = 0; i < len; i++) printf("%02x", b[i]);
    printf("\n");
}

int main(void)
{
    char riga[9000];

    while (fgets(riga, sizeof(riga), stdin)) {
        char a_s[3000], e_s[3000], m_s[3000];
        ExBig a, e, m, r;

        if (sscanf(riga, "%2999s %2999s %2999s", a_s, e_s, m_s) != 3) continue;

        if (leggi_hex(a_s, &a) != 0 || leggi_hex(e_s, &e) != 0 ||
            leggi_hex(m_s, &m) != 0) { printf("LETTURA\n"); continue; }

        if (exbig_modexp(&r, &a, &e, &m) != 0) { printf("RIFIUTATO\n"); continue; }
        stampa_hex(&r);
    }
    return 0;
}
