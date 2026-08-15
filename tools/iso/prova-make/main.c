/* =============================================================================
 * tools/iso/prova-make/main.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * ! QUESTO FILE NON ENTRA NELL'ARCHIVIO, e il makefile lo toglie
 * esplicitamente dal carattere jolly. Se ci finisse, `ld` troverebbe `main`
 * due volte — nell'oggetto e nella libreria — e il messaggio parlerebbe di
 * una definizione multipla senza dire che a metterla li' e' stata
 * un'espansione.
 * ============================================================================= */

#include <stdio.h>
#include "prova.h"

int main(void)
{
    long q = somma_quadrati(10);
    int  n = conta("EX-OS costruisce con make", 'c');
    int  ok = 1;

    printf("prova-make — costruito DENTRO EX-OS\n");

    /* ! IL VALORE ATTESO STA SCRITTO ACCANTO A QUELLO OTTENUTO. Stampare
     * solo il risultato vorrebbe dire che a giudicare e' chi legge, e chi
     * legge non ha in testa la somma dei quadrati da 1 a 10. */
    printf("  somma dei quadrati : %ld   (atteso 385)\n", q);
    if (q != 385) ok = 0;

    printf("  occorrenze di 'c'  : %d     (attese 3)\n", n);
    if (n != 3) ok = 0;

    printf("  esito              : %s\n", ok ? "tutto a posto" : "SBAGLIATO");

    /* Il codice d'uscita e' cio' che un makefile guarda. Stampare «tutto a
     * posto» e uscire con 0 comunque renderebbe la prova inutile dentro uno
     * script. */
    return ok ? 0 : 1;
}
