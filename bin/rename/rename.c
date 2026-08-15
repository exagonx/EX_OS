/* =============================================================================
 * bin/rename/rename.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Cambia il nome di un file.
 *
 *   rename <vecchio> <nuovo>
 *
 * -----------------------------------------------------------------------------
 * ! SI CHIAMA `rename` E NON `mv`, E LA DIFFERENZA E' REALE
 *
 * `mv` su Unix fa due cose: rinomina, e SPOSTA — e quando sposta fra
 * filesystem diversi copia e cancella, cioe' esegue un'operazione
 * completamente diversa sotto lo stesso nome. Qui non si sposta niente:
 * si riscrive la voce di directory, i dati non toccano il bus.
 *
 * Dare a questo comando il nome `mv` significherebbe promettere lo
 * spostamento e non farlo. Il nome dice cosa fa.
 *
 * -----------------------------------------------------------------------------
 * ! DUE LIMITI, ED E' MEGLIO SPIEGARLI CHE STAMPARE UN NUMERO
 *
 *   - SOLO NELLA STESSA DIRECTORY. Attraversare directory sarebbe una
 *     copia piu' una cancellazione: un'altra operazione, con un altro
 *     costo e un altro modo di fallire a meta'.
 *   - NON SOSTITUISCE una destinazione che esiste gia'. Sostituire vuol
 *     dire cancellare un file che l'utente non ha nominato come vittima;
 *     chi vuole sostituire cancella prima, e cosi' la perdita e' una
 *     scelta.
 *
 * Il kernel risponde ENOSYS al primo caso e EEXIST al secondo. Qui si
 * traducono in due frasi che dicono anche COSA FARE, perche' «errore 38»
 * non ha mai aiutato nessuno.
 *
 * Il perche' esteso di entrambi i limiti sta in kernel/fs/vfs.c.
 * ============================================================================= */
#include "libc.h"

/* Vero se i due percorsi stanno nella stessa directory. Serve solo a dare
 * il messaggio giusto: la decisione la prende il kernel, qui si indovina
 * la ragione per spiegarla. */
static int stessa_directory(const char *a, const char *b)
{
    int ia = -1, ib = -1, i;

    for (i = 0; a[i]; i++) if (a[i] == '/') ia = i;
    for (i = 0; b[i]; i++) if (b[i] == '/') ib = i;

    if (ia != ib) return 0;
    for (i = 0; i < ia; i++) if (a[i] != b[i]) return 0;
    return 1;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        printf("uso: rename <vecchio> <nuovo>\n\n");
        printf("Cambia il NOME di un file senza spostarne i dati.\n\n");
        printf("Due limiti, entrambi voluti:\n");
        printf("  - solo nella STESSA directory: attraversarle sarebbe una\n");
        printf("    copia piu' una cancellazione, cioe' un'altra operazione;\n");
        printf("  - NON sostituisce un file che esiste gia': chi vuole\n");
        printf("    sostituire lo cancella prima, cosi' la perdita e' una\n");
        printf("    scelta e non un effetto collaterale.\n\n");
        printf("  rename appunti.txt vecchi.txt\n");
        return 1;
    }

    if (rename(argv[1], argv[2]) == 0) {
        printf("%s -> %s\n", argv[1], argv[2]);
        return 0;
    }

    /* I due casi che hanno una spiegazione, e che vale la pena dare. */
    if (errno == ENOSYS) {
        if (!stessa_directory(argv[1], argv[2])) {
            printf("rename: i due nomi stanno in directory diverse.\n");
            printf("        Questo comando rinomina, non sposta: per\n");
            printf("        spostare, copia con `cp` e poi cancella.\n");
        } else {
            printf("rename: il filesystem di '%s' non sa rinominare.\n", argv[1]);
        }
        return 1;
    }

    if (errno == EEXIST) {
        printf("rename: '%s' esiste gia'.\n", argv[2]);
        printf("        Cancellalo prima, se e' quello che vuoi:\n");
        printf("        delete %s\n", argv[2]);
        return 1;
    }

    if (errno == ENOENT) {
        printf("rename: '%s' non esiste.\n", argv[1]);
        return 1;
    }

    if (errno == EROFS) {
        printf("rename: il volume e' montato in sola lettura.\n");
        return 1;
    }

    printf("rename: %s (errore %d)\n", strerror(errno), errno);
    return 1;
}
