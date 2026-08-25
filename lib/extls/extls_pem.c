/* =============================================================================
 * lib/extls/extls_pem.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il magazzino delle CA, da un file PEM
 *
 * ! IL FORMATO E' BASE64 FRA DUE RIGHE DI TRATTINI, e si legge in venti righe:
 * non vale la pena inventarne uno nostro «piu' efficiente». Il magazzino si
 * aggiorna copiandoci sopra il file di un altro sistema — e' l'unico modo
 * pratico di tenerlo fresco, e un formato nostro lo renderebbe impossibile.
 *
 * ! I BYTE DECIFRATI RESTANO DI CHI CHIAMA, e non e' un dettaglio: ExCert non
 * copia niente, tiene fette che puntano dentro il DER. Se il buffer del DER
 * sparisce, il magazzino punta nel vuoto — e lo fa in silenzio, perche' quei
 * byte quasi sempre ci sono ancora e sono solo diventati qualcos'altro.
 *
 * ! UN CERTIFICATO CHE NON SI CAPISCE SI SALTA, NON FERMA IL GIRO. Un file di
 * CA di un sistema vero ne ha un centinaio e mezzo, e ne basta uno con
 * un'estensione che non sappiamo leggere per restare senza magazzino — cioe'
 * senza https. Si tiene quello che si capisce e si dice quanti sono.
 * ============================================================================= */

#include "extls.h"

/* -1 = non e' un carattere di base64. */
static int valore(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int comincia_con(const char *p, const char *fine, const char *cosa)
{
    while (*cosa) {
        if (p >= fine || *p != *cosa) return 0;
        p++; cosa++;
    }
    return 1;
}

int extls_magazzino_pem(ExMagazzino *m, const char *pem, unsigned int pem_n,
                        unsigned char *der, unsigned int der_max,
                        unsigned int *der_usati)
{
    const char *p = pem, *fine = pem + pem_n;
    unsigned int usati = 0;
    int          quanti = 0;

    if (!m || !pem || !der) return -1;
    m->n = 0;

    while (p < fine) {
        unsigned int inizio;
        unsigned int accumulo = 0, bit = 0;

        /* Si cerca l'apertura, riga per riga. */
        if (!comincia_con(p, fine, "-----BEGIN CERTIFICATE-----")) {
            while (p < fine && *p != '\n') p++;
            if (p < fine) p++;
            continue;
        }
        while (p < fine && *p != '\n') p++;
        if (p < fine) p++;

        inizio = usati;

        /* I byte, fino alla riga di chiusura. */
        while (p < fine && !comincia_con(p, fine, "-----END")) {
            int v = valore(*p);

            if (v >= 0) {
                accumulo = (accumulo << 6) | (unsigned int)v;
                bit += 6;
                if (bit >= 8) {
                    bit -= 8;
                    if (usati >= der_max) return -2;
                    der[usati++] = (unsigned char)((accumulo >> bit) & 0xFF);
                }
            }
            /* '=' e a capo non aggiungono bit: si passa oltre. */
            p++;
        }
        while (p < fine && *p != '\n') p++;
        if (p < fine) p++;

        if (usati > inizio && m->n < EXCERT_MAGAZZINO_MAX) {
            if (excert_magazzino_aggiungi(m, der + inizio, usati - inizio) == 0)
                quanti++;
            else
                usati = inizio;      /* non si capisce: si riprende il posto */
        }
    }

    if (der_usati) *der_usati = usati;
    return quanti;
}
