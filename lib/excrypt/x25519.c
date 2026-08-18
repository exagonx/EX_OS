/* =============================================================================
 * lib/excrypt/x25519.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * X25519 — lo scambio di chiavi (RFC 7748)
 *
 * Due macchine che non si sono mai parlate arrivano allo stesso segreto senza
 * mandarlo sul cavo: ognuna manda il proprio punto pubblico, e moltiplica
 * quello dell'altra per il proprio scalare segreto. Il risultato e' lo stesso
 * da tutt'e due le parti — e chi ascolta ha visto solo i due punti pubblici.
 *
 * -----------------------------------------------------------------------------
 * ! I NUMERI HANNO 255 BIT E SI TENGONO IN SEDICI PEZZI DA SEDICI
 *
 * Non esiste un tipo da 255 bit, e non serve: si tiene il numero come sedici
 * cifre in base 2^16, dentro interi con segno a 64 bit. Cosi' un prodotto di
 * due cifre sta comodo (32 bit) e ci si possono sommare i trentadue prodotti
 * parziali senza traboccare — che e' la ragione per cui i pezzi sono sedici e
 * non quattro da 64.
 *
 * ! IL RIPORTO SI FA DOPO, NON DURANTE. Le cifre crescono oltre i 16 bit e si
 * rimettono in riga con una sola passata (`riporta`): farlo a ogni operazione
 * costerebbe di piu' e non servirebbe a niente.
 *
 * ! E IL 19 CHE TORNA OVUNQUE E' IL MODULO: 2^255 - 19. Cio' che esce dalla
 * cima rientra dal fondo moltiplicato per 38, che e' 2 per 19 — perche' la
 * cifra che esce vale 2^256, cioe' due volte 2^255.
 *
 * -----------------------------------------------------------------------------
 * ! NON C'E' NESSUN «if» CHE GUARDI IL SEGRETO, E NON E' PIGNOLERIA
 *
 * La scala di Montgomery esegue le STESSE operazioni per ogni bit dello
 * scalare: cambia solo quale delle due coppie di punti viene scambiata, e lo
 * scambio si fa con una maschera, non con un ramo. Un `if (bit)` renderebbe il
 * tempo — o il consumo, o la cache — dipendente dalla chiave privata, e da li'
 * si risale alla chiave un bit per volta. E' l'attacco che ha reso famose le
 * implementazioni scritte «in modo naturale».
 * ============================================================================= */

#include "excrypt.h"
#include "fe25519.h"

int x25519(unsigned char fuori[32], const unsigned char scalare[32],
           const unsigned char punto[32])
{
    unsigned char z[32];
    fe   x, a, b, c, d, e, f;
    i64  r;
    int  i, k;

    for (i = 0; i < 32; i++) z[i] = scalare[i];

    /* ! LO SCALARE SI POTA, e sono tre righe che valgono molto: i tre bit bassi
     * a zero tengono il risultato nel sottogruppo giusto (e rendono innocui i
     * punti di ordine piccolo che qualcuno potrebbe mandare apposta), il bit
     * alto azzerato e quello sotto acceso fissano la lunghezza — cosi' la scala
     * fa sempre lo stesso numero di giri, qualunque sia la chiave. */
    z[0]  &= 248;
    z[31] &= 127;
    z[31] |= 64;

    fe_da_byte(x, punto);

    fe_copia(b, x);
    fe_zero(c);
    fe_uno(a);
    fe_uno(d);

    for (i = 254; i >= 0; --i) {
        r = (z[i >> 3] >> (i & 7)) & 1;
        fe_scambia(a, b, r);
        fe_scambia(c, d, r);

        fe_somma(e, a, c);
        fe_sottrai(a, a, c);
        fe_somma(c, b, d);
        fe_sottrai(b, b, d);
        fe_quadrato(d, e);
        fe_quadrato(f, a);
        fe_moltiplica(a, c, a);
        fe_moltiplica(c, b, e);
        fe_somma(e, a, c);
        fe_sottrai(a, a, c);
        fe_quadrato(b, a);
        fe_sottrai(c, d, f);
        fe_moltiplica(a, c, FE_121665);
        fe_somma(a, a, d);
        fe_moltiplica(c, c, a);
        fe_moltiplica(a, d, f);
        fe_moltiplica(d, b, x);
        fe_quadrato(b, e);

        fe_scambia(a, b, r);
        fe_scambia(c, d, r);
    }

    fe_inverti(c, c);
    fe_moltiplica(a, a, c);
    fe_in_byte(fuori, a);

    /* ! UN SEGRETO TUTTO ZERI VUOL DIRE CHE IL PUNTO RICEVUTO ERA DI ORDINE
     * PICCOLO, e non e' un caso da ignorare: e' il modo in cui qualcuno prova a
     * forzare un segreto condiviso che conosce gia'. Chi chiama deve
     * rifiutare la connessione, e per questo si rende un valore. */
    {
        unsigned char acc = 0;

        for (i = 0; i < 32; i++) acc |= fuori[i];
        k = (acc == 0) ? -1 : 0;
    }
    return k;
}

/* La chiave pubblica e' lo scalare applicato al punto base, che e' 9. */
int x25519_pubblica(unsigned char fuori[32], const unsigned char privata[32])
{
    unsigned char base[32];
    int i;

    for (i = 0; i < 32; i++) base[i] = 0;
    base[0] = 9;

    /* Qui un risultato nullo non puo' capitare col punto base: si rende 0. */
    x25519(fuori, privata, base);
    return 0;
}
