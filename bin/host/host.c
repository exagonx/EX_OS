/* =============================================================================
 * bin/host/host.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Chiede al DNS l'indirizzo di un nome.
 *
 *   host www.esempio.it
 *
 * Tutta la risoluzione sta in lib/dns.c: questo file legge un argomento,
 * chiama una funzione e stampa. Serve a poter provare il risolutore da
 * solo — quando `ping nome` non funziona, la domanda è se sia rotto il
 * ping o il DNS, e senza questo comando bisogna indovinare.
 * ============================================================================= */

#include "libc.h"
#include "dns.h"
#include "rete.h"

/* +0.001 a ogni modifica: `host -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("host", "0.001");

static void stampa_ip(const unsigned char *p)
{
    printf("%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
}

int main(int argc, char **argv)
{
    unsigned char ip[4], da[4];
    int           rc;

    if (argc != 2) {
        printf("uso: host NOME\n\n");
        printf("Chiede al server DNS configurato l'indirizzo di NOME.\n");
        printf("Il server lo mette il DHCP: `ipcfg` mostra quale.\n");
        return 1;
    }

    rc = dns_risolvi_da(argv[1], ip, da);

    if (rc == 0) {
        printf("%s ha indirizzo ", argv[1]);
        stampa_ip(ip);
        if ((da[0] | da[1] | da[2] | da[3]) != 0) {
            printf("  (risposta da ");
            stampa_ip(da);
            printf(")");
        } else {
            printf("  (era gia' un indirizzo, nessuna domanda fatta)");
        }
        printf("\n");
        return 0;
    }

    /* Ogni errore dice cosa fare, non solo cosa non ha funzionato. */
    switch (rc) {
    case -ENOENT:
        printf("host: '%s' non esiste.\n", argv[1]);
        break;
    case -ENODEV:
        printf("host: lo stack IP non e' attivo.\n");
        rete_istruzioni();
        break;
    case -ENETDOWN:
        printf("host: nessun server DNS configurato.\n");
        rete_istruzioni();
        break;
    case -ETIMEDOUT:
        printf("host: il server DNS non risponde.\n");
        printf("      E' raggiungibile? Provalo:  ping <indirizzo del DNS>\n");
        break;
    case -EINVAL:
        printf("host: '%s' non e' un nome utilizzabile.\n", argv[1]);
        break;
    default:
        printf("host: risposta non comprensibile dal server (%d).\n", rc);
        break;
    }
    return 1;
}
