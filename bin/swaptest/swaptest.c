/* =============================================================================
 * bin/swaptest/swaptest.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * swaptest — chiede piu' memoria di quanta ce ne sia, e controlla che torni
 *
 *     swaptest            chiede il doppio della RAM libera
 *     swaptest 12         chiede 12 MB
 *
 * ! LA PROVA NON E' «NON E' ESPLOSO», E' «I BYTE SONO GLI STESSI». Uno sfratto
 * riuscito e uno sbagliato si assomigliano moltissimo: in tutti e due i casi il
 * programma continua a girare, e la differenza si vede solo RILEGGENDO quello
 * che si era scritto. Percio' qui ogni pagina viene riempita con un motivo che
 * dipende dal suo numero, e alla fine si ricontrolla pagina per pagina: una
 * pagina tornata dallo slot sbagliato, o non tornata affatto, non ha nessuna
 * probabilita' di superare il confronto.
 *
 * ! E SI SCRIVE TUTTO PRIMA E SI RILEGGE TUTTO DOPO, non pagina per pagina.
 * Scrivere e rileggere subito proverebbe soltanto che la memoria funziona:
 * quella pagina e' ancora li', nessuno ha avuto motivo di mandarla via. Lo
 * sfratto succede mentre si scrivono le pagine SUCCESSIVE, quindi il controllo
 * deve venire dopo l'ultima.
 *
 * ! IL MOTIVO NON E' COSTANTE, e non e' un dettaglio: riempire tutto con lo
 * stesso byte vorrebbe dire che due pagine scambiate fra loro superano il
 * controllo. Scambiare due pagine e' esattamente il modo in cui un elenco di
 * slot sbagliato si manifesta.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `swaptest -version` la stampa. Vedi EX_VERSIONE. */
EX_VERSIONE("swaptest", "0.001");

#define PAGINA 4096u

/* Il primo byte di ogni pagina non basta: un guasto che sposta i dati DENTRO
 * la pagina lo supererebbe. Si guardano il principio, la meta' e la fine. */
static void riempi(unsigned char *p, unsigned int n)
{
    p[0]              = (unsigned char)(n & 0xFF);
    p[1]              = (unsigned char)((n >> 8) & 0xFF);
    p[PAGINA / 2]     = (unsigned char)(n ^ 0x5A);
    p[PAGINA - 1]     = (unsigned char)(~n & 0xFF);
}

static int controlla(const unsigned char *p, unsigned int n)
{
    return p[0]          == (unsigned char)(n & 0xFF) &&
           p[1]          == (unsigned char)((n >> 8) & 0xFF) &&
           p[PAGINA / 2] == (unsigned char)(n ^ 0x5A) &&
           p[PAGINA - 1] == (unsigned char)(~n & 0xFF);
}

int main(int argc, char **argv)
{
    MemInfo       mi;
    unsigned char *blocco;
    unsigned int  mb = 0, pagine, i, sbagliate = 0;
    unsigned int  libere_prima = 0, libere_dopo = 0;

    if (argc > 1 && argv[1][0] == '-') {
        printf("uso: swaptest [MEGABYTE]\n");
        printf("  chiede memoria, ci scrive un motivo, la rilegge e la conta.\n");
        printf("  Senza argomenti chiede il doppio della RAM libera, cosi' la\n");
        printf("  differenza la deve fare l'area di scambio.\n");
        return 0;
    }
    if (argc > 1) mb = (unsigned int)atoi(argv[1]);

    if (meminfo(&mi) != 0) {
        printf("swaptest: meminfo non risponde\n");
        return 1;
    }
    libere_prima = mi.free_kb;

    /* ! IL DOPPIO DELLA MEMORIA LIBERA, e non un numero fisso: su una macchina
     * grande un numero fisso non farebbe sfrattare niente e la prova direbbe
     * «passata» senza aver provato nulla. Chiedere il doppio garantisce che
     * meta' delle pagine debba per forza passare dal disco. */
    if (mb == 0) mb = (libere_prima / 1024) * 2;
    if (mb < 2)  mb = 2;

    pagine = mb * (1024u * 1024u / PAGINA);

    printf("swaptest: RAM libera %u KB, chiedo %u MB (%u pagine)\n",
           libere_prima, mb, pagine);

    blocco = (unsigned char *)malloc(pagine * PAGINA);
    if (blocco == 0) {
        printf("swaptest: malloc di %u MB rifiutata.\n", mb);
        printf("          Senza area di scambio e' il comportamento atteso:\n");
        printf("          si prepara con  mkswap  e si dichiara in kernel.cfg.\n");
        return 1;
    }

    /* ! LE PAGINE SI TOCCANO UNA PER UNA. malloc rende un indirizzo, non della
     * memoria: le pagine arrivano quando le si scrive, ed e' scrivendole che
     * si mette il sistema sotto pressione. Un malloc grande e mai toccato non
     * fa sfrattare niente. */
    for (i = 0; i < pagine; i++)
        riempi(blocco + (unsigned int)i * PAGINA, i);

    if (meminfo(&mi) == 0) libere_dopo = mi.free_kb;

    for (i = 0; i < pagine; i++)
        if (!controlla(blocco + (unsigned int)i * PAGINA, i)) {
            if (sbagliate < 5)
                printf("swaptest: pagina %u tornata sbagliata\n", i);
            sbagliate++;
        }

    printf("swaptest: RAM libera prima %u KB, dopo %u KB\n",
           libere_prima, libere_dopo);

    if (sbagliate) {
        printf("swaptest: %u pagine su %u NON sono tornate come erano.\n",
               sbagliate, pagine);
        free(blocco);
        return 1;
    }

    printf("swaptest: tutte e %u le pagine sono tornate identiche.\n", pagine);
    free(blocco);
    return 0;
}
