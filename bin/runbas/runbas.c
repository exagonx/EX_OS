/* =============================================================================
 * bin/runbas/runbas.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * runbas — runs a QBASIC program with the gfbasic interpreter (@RUNBAS)
 *
 *     runbas mioprog.bas
 *
 * The language is gfbasic/gf_basic.c's: DIM, PRINT, INPUT, IF, FOR, WHILE,
 * DO...LOOP, GOTO/GOSUB, and the usual string and number functions. See the
 * head of that file for what is and is not there.
 *
 * ! IT IS A PROGRAM OF ITS OWN, AND gfedit LAUNCHES IT (F5): the request was
 * that gfedit keeps working without the interpreter. A library linked into
 * the editor would make the editor refuse to start without it; a program
 * that is missing is a message. And the interpreted program dies on its own,
 * without taking the editor with it.
 *
 * Exit status: 0 when the program ran to the end, 1 on a syntax or runtime
 * error (said on the screen with its line), 2 when the file cannot be read.
 * SYSRETURN_VALUE(n) sets the exit status to n, which is what it is for.
 * ============================================================================= */
#include "libc.h"
#include "gf_basic.h"

EX_VERSIONE("runbas", "0.002");

#define SORGENTE_MAX (256u * 1024u)     /* a BASIC program, not a data file */

int main(int argc, char **argv)
{
    struct stat st;
    GF_BASIC   *gf;
    char       *testo;
    long        letti = 0;
    int         fd, rc;

    if (argc < 2 || argv[1][0] == '-') {
        printf("uso: runbas programma.bas\n\n"
               "  Esegue un programma QBASIC con l'interprete gfbasic.\n"
               "  Da gfedit: F5, o Opzioni > Esegui.\n");
        return argc < 2 ? 2 : 0;
    }

    if (stat(argv[1], &st) != 0) { printf("runbas: %s: non c'e'\n", argv[1]); return 2; }
    if ((unsigned long)st.st_size > SORGENTE_MAX) {
        printf("runbas: %s: piu' di %u KB, non sembra un programma\n",
               argv[1], SORGENTE_MAX / 1024);
        return 2;
    }
    fd = open(argv[1], O_RDONLY, 0);
    testo = (char *)malloc((size_t)st.st_size + 1);
    if (fd < 0 || !testo) { printf("runbas: %s: non si legge\n", argv[1]); return 2; }
    while (letti < (long)st.st_size) {
        int r = (int)read(fd, testo + letti, (unsigned int)(st.st_size - letti));
        if (r <= 0) break;
        letti += r;
    }
    close(fd);
    testo[letti] = '\0';

    gf = gf_basic_create();
    if (!gf) { printf("runbas: non c'e' memoria per l'interprete\n"); return 2; }

    if (gf_basic_set_program(gf, testo) != 0) {
        printf("runbas: %s: %s\n", argv[1], gf_basic_get_error(gf));
        return 1;
    }
    rc = gf_basic_run(gf);
    if (rc != 0) {
        printf("\nrunbas: %s: %s\n", argv[1], gf_basic_get_error(gf));
        return 1;
    }
    return (int)gf_basic_get_return_value(gf);
}
