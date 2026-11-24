/* =============================================================================
 * exwin/bin/scegli/scegli.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * scegli — the file dialog of ExWin for programs that are not ExWin programs
 *
 *     scegli apri     [-t TITLE] [PATH]     a file to open
 *     scegli salva    [-t TITLE] [PATH]     where to save (PATH = the proposal)
 *     scegli cartella [-t TITLE] [PATH]     a folder
 *
 * The path chosen goes to stdout with a newline, exit 0; "Annulla" is exit 1,
 * a wrong command line exit 2. Firefox's file picker (widget/exos,
 * nsFilePickerExos) starts it and reads the answer (voce 118).
 *
 * ! A PROCESS OF ITS OWN, NOT exdlg.so INSIDE THE CALLER. exwin.so speaks to
 * the server through the mailbox of the process; inside Firefox it would read
 * the messages of Firefox's own windows, and Firefox would read its.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"

EX_VERSIONE("scegli", "0.001");

int main(int argc, char **argv)
{
    char        p[1024];
    const char *modo, *titolo = 0;
    int         i, ok;

    if (argc < 2) {
        printf("uso: scegli apri|salva|cartella [-t TITOLO] [PERCORSO]\n");
        return 2;
    }
    modo = argv[1];
    p[0] = '\0';
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-t") && i + 1 < argc) titolo = argv[++i];
        else snprintf(p, sizeof(p), "%s", argv[i]);
    }
    if (!strcmp(modo, "apri"))
        ok = ex_dlg_apri(p, sizeof(p));    /* the open dialog has no words to change */
    else if (!strcmp(modo, "salva"))
        ok = titolo ? ex_dlg_percorso(titolo, 0, 0, p, sizeof(p)) : ex_dlg_salva(p, sizeof(p));
    else if (!strcmp(modo, "cartella"))
        ok = ex_dlg_percorso(titolo ? titolo : "Scegli una cartella", "Cartella", "Scegli", p, sizeof(p));
    else {
        printf("scegli: '%s'? apri, salva o cartella\n", modo);
        return 2;
    }
    if (!ok || !p[0]) return 1;
    printf("%s\n", p);
    return 0;
}
