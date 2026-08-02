/* =============================================================================
 * bin/mkdir/mkdir.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * mkdir — crea una directory.
 *
 *   mkdir <nome> [nome2 ...]
 *
 * Accetta percorsi assoluti ("/dati") o relativi alla directory corrente
 * ("dati"): la risoluzione la fa il kernel, vedi resolve_path() in
 * kernel/syscall/syscall_impl.c.
 *
 * LIMITE DA CONOSCERE: il driver FAT12 di EX-OS risolve i percorsi a un
 * solo livello, quindi si possono creare directory solo nella root. Un
 * "mkdir /boot/sub" viene rifiutato con un messaggio esplicito invece di
 * creare una directory che poi nessuna altra funzione saprebbe
 * raggiungere. Vedi fat12_mkdir() in kernel/fs/fat12.c.
 * ============================================================================= */

#include "libc.h"

/* Codici errno restituiti dal kernel, in negativo. Duplicati qui con la
 * stessa convenzione usata altrove nel progetto: userspace e kernel non
 * condividono header. */
#define E_EXIST     17
#define E_NOSYS     38
#define E_NOSPC     28
#define E_NOENT      2
#define E_INVAL     22

static void uso(void)
{
    printf("Uso: mkdir <nome> [nome2 ...]\n");
    printf("Crea una o piu' directory.\n");
    printf("Nota: EX-OS supporta directory solo nella root (un livello).\n");
}

/* Traduce l'errore del kernel in una spiegazione utile. Un numero e basta
 * non direbbe niente a chi lo legge sullo schermo. */
static void spiega_errore(const char *nome, int err)
{
    switch (-err) {
        case E_EXIST:
            printf("mkdir: '%s' esiste gia'\n", nome);
            break;
        case E_NOSYS:
            printf("mkdir: '%s' — EX-OS supporta directory solo nella root; "
                   "un percorso annidato non sarebbe raggiungibile\n", nome);
            break;
        case E_NOSPC:
            printf("mkdir: '%s' — spazio esaurito (disco pieno o root directory "
                   "piena)\n", nome);
            break;
        case E_NOENT:
            printf("mkdir: percorso '%s' non valido\n", nome);
            break;
        case E_INVAL:
            printf("mkdir: nome '%s' non valido\n", nome);
            break;
        default:
            printf("mkdir: '%s' non creata (errore %d)\n", nome, err);
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
        int r = mkdir(argv[i], 0755);

        if (r == 0) {
            printf("mkdir: creata '%s'\n", argv[i]);
        } else {
            spiega_errore(argv[i], r);
            falliti++;
        }
    }

    /* Come i mkdir tradizionali: esito diverso da zero se almeno una
     * directory non e' stata creata, cosi' uno script puo' accorgersene. */
    return falliti ? 1 : 0;
}
