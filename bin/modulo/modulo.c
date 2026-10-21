/* =============================================================================
 * bin/modulo/modulo.c — kernel modules: list them, load one
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 *     modulo                       the modules the kernel has loaded
 *     modulo carica FILE           load one (version and SHA-256 checked)
 *     modulo carica -forza FILE    load it even if they do not match
 *
 * The kernel loads /dev/ext4.mod by itself at boot (kernel/loader/modulo.c);
 * this is for the cases where that failed and the file has been put right
 * since, without a reboot - and to see what is loaded.
 *
 * ! -forza IS NEVER AUTOMATIC: it asks, and wants "SI" typed in capitals. A
 * module runs INSIDE the kernel and writes to disks; one that does not
 * match the kernel can damage data or stop the system.
 * ============================================================================= */
#include "libc.h"

EX_VERSIONE("modulo", "0.001");

static void uso(void)
{
    printf("uso: modulo                       i moduli caricati dal kernel\n"
           "     modulo carica FILE           carica un modulo (versione e SHA-256 controllati)\n"
           "     modulo carica -forza FILE    caricalo anche se non corrispondono\n");
}

static int elenco(void)
{
    char buf[512];
    int  n = modulo_kernel(0, (uintptr_t)buf, sizeof(buf));

    if (n < 0) {
        printf("modulo: il kernel non risponde (%s): e' piu' vecchio della 0.263?\n",
               strerror(errno));
        return 1;
    }
    if (n == 0) {
        printf("nessun modulo caricato: i volumi ext2/ext3/ext4 sono in sola lettura\n");
        return 0;
    }
    printf("%s", buf);
    return 0;
}

/* One line from the keyboard, read whole: the console hands a line to one
 * read(), and reading byte by byte loses the rest of it. */
static int conferma_si(void)
{
    char r[32];
    int  n;

    printf("Scrivi SI (maiuscolo) per caricarlo lo stesso: ");
    fflush(stdout);
    n = (int)read(0, r, sizeof(r) - 1);
    if (n <= 0) return 0;
    r[n] = '\0';
    while (n > 0 && (r[n - 1] == '\n' || r[n - 1] == '\r' || r[n - 1] == ' ')) r[--n] = '\0';
    return strcmp(r, "SI") == 0;
}

static int carica(const char *file, int forza)
{
    if (getuid() != 0) {
        printf("modulo: solo root puo' caricare moduli nel kernel\n");
        return 1;
    }

    if (modulo_kernel(1, (uintptr_t)file, 0) == 0) {
        printf("%s caricato\n", file);
        return 0;
    }

    if (errno == EBADMSG && forza) {
        printf("\n!!! ATTENZIONE !!!\n"
               "%s non corrisponde a questo kernel: e' stato costruito per un'altra\n"
               "versione, oppure e' danneggiato (il kernel ha scritto sopra che cosa\n"
               "si aspettava e che cosa ha trovato). Un modulo cosi' gira DENTRO il\n"
               "kernel e scrive sui dischi: se e' sbagliato puo' rovinare i dati o\n"
               "bloccare il sistema. La responsabilita' e' tua.\n\n", file);
        if (!conferma_si()) {
            printf("non caricato\n");
            return 1;
        }
        if (modulo_kernel(1, (uintptr_t)file, 1) == 0) {
            printf("%s caricato SENZA i controlli, come chiesto\n", file);
            return 0;
        }
    }

    switch (errno) {
    case EBADMSG:
        printf("modulo: %s non corrisponde a questo kernel (versione o SHA-256): non\n"
               "        caricato. `modulo carica -forza %s` lo carica lo stesso.\n",
               file, file);
        break;
    case EEXIST:
        printf("modulo: un modulo con questo nome e' gia' caricato\n");
        break;
    case ENOEXEC:
        printf("modulo: %s non e' un modulo per questo kernel, o si e' rifiutato di partire\n"
               "        (il motivo e' nel registro del kernel: dmesg)\n", file);
        break;
    default:
        printf("modulo: %s: %s\n", file, strerror(errno));
        break;
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc == 1) return elenco();

    if (strcmp(argv[1], "carica") == 0) {
        if (argc == 3 && argv[2][0] != '-') return carica(argv[2], 0);
        if (argc == 4 && strcmp(argv[2], "-forza") == 0) return carica(argv[3], 1);
    }

    uso();
    return 2;
}
