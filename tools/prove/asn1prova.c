/* =============================================================================
 * tools/prove/asn1prova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * Il banco di prova del lettore DER/X.509, che gira SULL'HOST.
 *
 * ! SI PROVA QUI PERCHE' IL RIFERIMENTO E' QUI: `openssl`, e i certificati
 * radice veri che stanno in /etc/ssl/certs. Dentro EX-OS non ci sarebbe niente
 * con cui confrontarsi, e ogni riga cambiata costerebbe un giro di costruzione
 * e un avvio.
 *
 * Questo programma non decide niente: legge UN certificato DER da stdin e
 * stampa i campi che ha capito. A confrontare e' asn1prova.py.
 *
 *     cc -o /tmp/asn1prova tools/prove/asn1prova.c lib/exasn1/exasn1.c \
 *        -I lib/exasn1
 * ============================================================================= */

#include <stdio.h>
#include "exasn1.h"

static void stampa(const char *nome, const ExDer *d)
{
    unsigned int i;

    printf("%s ", nome);
    if (d->p == 0) { printf("-\n"); return; }
    for (i = 0; i < d->n; i++) printf("%02x", d->p[i]);
    printf("\n");
}

int main(void)
{
    static unsigned char der[65536];
    unsigned int n = 0;
    int          c;
    ExCert       cert;

    while ((c = getchar()) != EOF && n < sizeof(der)) der[n++] = (unsigned char)c;

    if (excert_analizza(der, n, &cert) != 0) { printf("RIFIUTATO\n"); return 0; }

    stampa("tbs",       &cert.tbs);
    stampa("emittente", &cert.emittente);
    stampa("soggetto",  &cert.soggetto);
    stampa("serie",     &cert.numero_serie);
    stampa("modulo",    &cert.chiave_modulo);
    stampa("esponente", &cert.chiave_esponente);
    stampa("punto",     &cert.chiave_punto);
    stampa("firma",     &cert.firma);
    printf("tipo %u\n",      cert.tipo_chiave);
    printf("alg %u\n",       cert.alg_firma);
    printf("nonprima %s\n",  cert.non_prima);
    printf("nondopo %s\n",   cert.non_dopo);
    printf("ca %d\n",        cert.e_ca);
    printf("habasic %d\n",   cert.ha_basic);
    printf("stessonome %d\n",
           excert_stesso_nome(&cert.emittente, &cert.soggetto));
    return 0;
}
