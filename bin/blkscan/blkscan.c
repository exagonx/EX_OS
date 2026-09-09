/* =============================================================================
 * bin/blkscan/blkscan.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LE PARTIZIONI DI UN DISCO SERVITO DA UN DRIVER
 *
 *     blkscan            rilegge tutti i dispositivi serviti da un driver
 *     blkscan usb0       rilegge quello
 *
 * ! PERCHE' NON LO FA IL DRIVER DA SE'. Leggere il settore 0 vuol dire
 * mandare una richiesta al servente e aspettarne la risposta. Ma chi offre il
 * dispositivo E' il servente: si aspetterebbe da solo, e il kernel lo rifiuta
 * con -EDEADLK (il perche' per esteso sta in kernel/include/blkr3.h). La
 * scansione la chiede qualcun altro, a cose fatte.
 *
 * ! E PERCHE' NON STA DENTRO `disk`. Quel programma dichiara in fondo alla sua
 * uscita «Sola lettura: questo programma non modifica nulla», e questo invece
 * cambia quali dispositivi esistono. Una riga che diventa falsa e' peggio di
 * un comando in piu'.
 *
 * ! UNA CHIAVETTA SENZA TABELLA NON E' UN ERRORE, ED E' IL CASO NORMALE.
 * Quasi tutte sono un filesystem che comincia al settore 0 — i floppy USB lo
 * sono sempre. «zero partizioni» vuol dire che si monta com'e':
 *
 *     mount usb0 /USB/DRIVE0
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `blkscan -version` la stampa. Vedi
 * EX_VERSIONE in libc.h. */
EX_VERSIONE("blkscan", "0.001");

#define TIPO_RING3   5   /* BlkInfo.tipo: servito da un processo */

static void aiuto(void)
{
    printf("blkscan - rilegge la tabella delle partizioni dei dispositivi\n");
    printf("          serviti da un driver (chiavette USB, dischi in RAM).\n\n");
    printf("  blkscan            tutti quelli che trova\n");
    printf("  blkscan <nome>     solo quello (usb0, ram0)\n\n");
    printf("Dopo, le partizioni si chiamano <nome>p1, <nome>p2, ... e si\n");
    printf("montano come qualunque altra. Zero partizioni non e' un errore:\n");
    printf("vuol dire che il supporto e' un volume solo, e si monta com'e'.\n");
}

/* Rende 0 se e' andata, 1 se no. */
static int uno(const char *nome)
{
    int n = blk_scansiona(nome);

    if (n < 0) {
        printf("blkscan: %s: ", nome);
        if      (n == -ENOENT) printf("non esiste\n");
        else if (n == -EINVAL) printf("non e' servito da un driver: le sue\n"
                                      "         partizioni le legge il kernel da se'\n");
        else if (n == -EBUSY)  printf("una sua partizione e' montata: smontala prima\n");
        else if (n == -EPERM)  printf("serve essere root\n");
        else if (n == -EIO)    printf("il driver non risponde\n");
        else                   printf("errore %d\n", n);
        return 1;
    }

    if (n == 0)
        printf("%s: nessuna tabella delle partizioni - e' un volume solo,\n"
               "     si monta cosi': mount %s <punto>\n", nome, nome);
    else
        printf("%s: %d partizioni - %sp1", nome, n, nome);

    if (n > 0) {
        int k;
        for (k = 2; k <= n; k++) printf(", %sp%d", nome, k);
        printf("\n");
    }
    return 0;
}

int main(int argc, char **argv)
{
    BlkInfo      b[8];
    unsigned int start = 0;
    int          n, k, quanti = 0, guasti = 0;

    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            aiuto();
            return 0;
        }
        return uno(argv[1]);
    }

    /* Senza argomenti: tutti quelli che ci sono. E' quello che fara' il
     * sorvegliante quando si infila una chiavetta. */
    for (;;) {
        n = blkinfo(b, 8, start);
        if (n <= 0) break;

        for (k = 0; k < n; k++) {
            if (b[k].tipo != TIPO_RING3) continue;
            quanti++;
            guasti += uno(b[k].nome);
        }

        start += (unsigned int)n;
        if (n < 8) break;
    }

    if (quanti == 0) {
        printf("blkscan: nessun dispositivo servito da un driver.\n");
        printf("         Ne offre uno /dev/ramdisk.drv, e un giorno il driver\n");
        printf("         delle chiavette USB.\n");
        return 1;
    }

    return guasti ? 1 : 0;
}
