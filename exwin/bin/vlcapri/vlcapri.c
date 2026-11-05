/* =============================================================================
 * exwin/bin/vlcapri/vlcapri.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * vlcapri — VLC from the desktop: choose a file, and VLC plays it
 *
 *     vlcapri              the "Apri" dialog, then VLC on the file chosen
 *     vlcapri <file>       VLC on that file, no dialog
 *
 * (voce 79, 4 November 2026: "VLC installato da ExPackage non compare nel
 * Program Manager".) THE MENU ENTRY OF THE vlc PACKAGE STARTS THIS, NOT vlc:
 * the program manager starts an entry with no arguments, and VLC without a
 * file and without its Qt interface (not ported) opens its line interface on
 * a console that a program started from the menu does not have - a process
 * running, invisible, waiting for commands nobody can type.
 *
 * So: the dialog every ExWin program uses to open a file (ex_dlg_apri), then
 *     /exos/bin/vlc -I dummy --play-and-exit <file>
 * -I dummy: no line interface; --play-and-exit: VLC ends with the file, and
 * the window of the video (vout_exos) closes with it. The keys of that window
 * go to VLC as always (space pauses).
 *
 * ! IT IS PART OF THE vlc PACKAGE, like exilla is of exilla: it is built
 * here (build/vlcapri, build-64/exwin/bin) and tools/librerie-exos/su-cd.sh
 * puts it in /exos/bin with the icons (tools/vlc-exos/icone) when VLC is
 * built. It is not on the system CD: without VLC it has nothing to start.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"

EX_VERSIONE("vlcapri", "0.001");

#define VLC      "/exos/bin/vlc"
#define PERC_MAX 512

static void dillo(const char *testo)
{
    unsigned int sw = 0, sh = 0;

    printf("vlcapri: %s\n", testo);
    ex_screen_size(&sw, &sh);
    if (sw != 0) ex_dlg_avviso("VLC", testo);       /* from the menu: no console */
}

int main(int argc, char **argv)
{
    static char file[PERC_MAX];
    char *av[6];
    unsigned int sw = 0, sh = 0;

    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("uso: vlcapri          sceglie un file e lo suona con VLC (dalla scrivania)\n");
        printf("     vlcapri <file>   lo suona con VLC, senza chiedere\n");
        printf("Dalla console:  vlc -I dummy --play-and-exit <file>\n");
        return 0;
    }
    if (access(VLC, F_OK) != 0) {
        dillo("VLC non e' installato. Da ExPackage, o da una console con la rete:   "
              "netupdate -install:vlc");
        return 1;
    }
    ex_screen_size(&sw, &sh);
    if (sw == 0) {
        printf("vlcapri: la scrivania non e' accesa. Dalla console:  vlc -I dummy --play-and-exit <file>\n");
        return 1;
    }

    if (argc >= 2) {
        snprintf(file, sizeof(file), "%s", argv[1]);
    } else {
        const char *casa = getenv("HOME");

        /* The dialog starts where the user's files are. */
        snprintf(file, sizeof(file), "%s/", casa && casa[0] ? casa : "/");
        if (!ex_dlg_apri(file, sizeof(file))) return 0;     /* "Annulla" */
    }
    if (access(file, F_OK) != 0) {
        char t[PERC_MAX + 40];

        snprintf(t, sizeof(t), "%s non c'e'.", file);
        dillo(t);
        return 1;
    }

    /* ! THE SOUND IS SAID BEFORE, NOT DISCOVERED: without the "audio" service
     * VLC plays the video in silence and says why only on a console that is
     * not there. It still starts: a video without sound is still a video. */
    if (ipc_lookup("audio") <= 0)
        dillo("Il suono non e' acceso: VLC mostra il video senza audio. "
              "Per accenderlo, da root:   audio -i");

    av[0] = (char *)VLC;
    av[1] = (char *)"-I";
    av[2] = (char *)"dummy";
    av[3] = (char *)"--play-and-exit";
    av[4] = file;
    av[5] = 0;
    if (ex_spawn_program(VLC, av, "VLC") < 0) {
        dillo("VLC non parte (memoria insufficiente, o il file e' rovinato: netupdate -check).");
        return 1;
    }
    return 0;
}
