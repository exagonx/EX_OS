/* =============================================================================
 * bin/rmdir/rmdir.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * rmdir — cancella una directory VUOTA.
 *
 *   rmdir <nome> [nome2 ...]
 *
 * Se la directory contiene qualcosa l'operazione viene rifiutata. Non è
 * una limitazione temporanea: senza una cancellazione ricorsiva i file
 * rimasti dentro diventerebbero irraggiungibili e i loro cluster
 * resterebbero occupati per sempre — spazio perso in silenzio, che solo
 * un controllo del filesystem potrebbe recuperare.
 *
 * Accetta percorsi assoluti ("/dati") o relativi alla directory corrente
 * ("dati"). Come mkdir, opera solo su directory nella root: vedi
 * fat12_rmdir() in kernel/fs/fat12.c.
 * ============================================================================= */

#include "libc.h"

/* Codici errno restituiti dal kernel, in negativo. Duplicati qui con la
 * stessa convenzione usata altrove nel progetto: userspace e kernel non
 * condividono header. */
#define E_NOENT      2
#define E_NOTDIR    20
#define E_INVAL     22
#define E_NOSYS     38
#define E_NOTEMPTY  39

static void uso(void)
{
    printf("Uso: rmdir <nome> [nome2 ...]\n");
    printf("Cancella una o piu' directory VUOTE.\n");
    printf("Una directory che contiene file non viene cancellata.\n");
}

/* Un numero da solo non direbbe niente a chi legge lo schermo. */
static void spiega_errore(const char *nome, int err)
{
    switch (-err) {
        case E_NOTEMPTY:
            printf("rmdir: '%s' non e' vuota — cancella prima il suo "
                   "contenuto\n", nome);
            break;
        case E_NOENT:
            printf("rmdir: '%s' non esiste\n", nome);
            break;
        case E_NOTDIR:
            printf("rmdir: '%s' non e' una directory\n", nome);
            break;
        case E_NOSYS:
            printf("rmdir: '%s' — EX-OS gestisce directory solo nella root "
                   "(un livello)\n", nome);
            break;
        case E_INVAL:
            printf("rmdir: '%s' non e' un nome valido da cancellare\n", nome);
            break;
        default:
            printf("rmdir: '%s' non cancellata (errore %d)\n", nome, err);
            break;
    }
}

int main(int argc, char **argv)
{
    int i;
    int falliti = 0;

    if (argc < 2) {
        uso();
        return 1;
    }

    for (i = 1; i < argc; i++) {
        int r = rmdir(argv[i]);

        if (r == 0) {
            printf("rmdir: cancellata '%s'\n", argv[i]);
        } else {
            spiega_errore(argv[i], r);
            falliti++;
        }
    }

    /* Esito diverso da zero se almeno una directory non e' stata
     * cancellata, cosi' uno script puo' accorgersene. */
    return falliti ? 1 : 0;
}
