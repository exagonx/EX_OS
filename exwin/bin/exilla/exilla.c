/* =============================================================================
 * exwin/bin/exilla/exilla.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * exilla — avvia Exilla (Firefox per EX-OS)
 *
 *     exilla [indirizzo]          apre il navigatore, se c'e' con quella pagina
 *     exilla -dove                dice dove sta, e quanto occupa
 *
 * ! EXILLA E' UN PACCHETTO DEL REPOSITORY, E QUESTO PROGRAMMA NE FA PARTE.
 * Si installa come il compilatore:   netupdate -install:exilla   e finisce
 * tutta in /exwin/app/exilla - il navigatore, questo lanciatore, le icone.
 * La voce nel menu di avvio (categoria Internet) la scrive netupdate, che la
 * toglie con -remove:exilla. Fino al 9 ottobre 2026 questo programma stava nel
 * sistema di base e copiava Exilla da una chiavetta: due strade per installare
 * la stessa cosa, e una sola delle due la conosceva il registro.
 *
 * ! FIREFOX NON SI LANCIA DA SOLO, e sono quattro cose da ricordare:
 *   - MOZ_FORCE_DISABLE_E10S=1: tutto in un processo. I processi figli di
 *     Gecko vogliono una comunicazione fra processi che qui non e' portata;
 *   - un PROFILO scrivibile, che e' di chi usa la macchina: $HOME/.exilla;
 *   - EXILLA_FONTS: dove stanno i caratteri (quelli di ExWin);
 *   - -no-remote: non cercare un Firefox gia' aperto a cui passare la pagina.
 * Sbagliarne una da' una finestra che non si apre e nessun messaggio.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"
#include "exdlg.h"

#define VERSIONE_APP "0.002"
EX_VERSIONE("exilla", VERSIONE_APP);

#define PERC_MAX    512

/* Dove sta. EXILLA_DIR la sposta: per chi ha il disco di sistema pieno e un
 * secondo disco montato, e per le prove. */
static char g_dove[PERC_MAX]    = "/exwin/app/exilla";
static char g_firefox[PERC_MAX] = "/exwin/app/exilla/firefox";
#define DOVE    g_dove
#define FIREFOX g_firefox

/* -----------------------------------------------------------------------------
 * L'avvio
 * --------------------------------------------------------------------------- */
static void dillo(const char *testo)
{
    unsigned int sw = 0, sh = 0;

    printf("exilla: %s\n", testo);
    ex_screen_size(&sw, &sh);
    if (sw != 0) ex_dlg_avviso("Exilla", testo);    /* dal menu non c'e' una console */
}

static int avvia(int argc, char **argv)
{
    static char profilo[PERC_MAX], casa_exilla[PERC_MAX];
    const char *casa = getenv("HOME");
    char *av[16];
    int   n = 0, i;
    unsigned int sw = 0, sh = 0;

    if (access(FIREFOX, F_OK) != 0) {
        dillo("Exilla non e' installata. Da una console, con la rete:   "
              "netupdate -install:exilla");
        return 1;
    }
    ex_screen_size(&sw, &sh);
    if (sw == 0) {
        printf("exilla: la scrivania non e' accesa. Prima:  exwin\n");
        return 1;
    }

    if (!casa || !casa[0] || strcmp(casa, "/") == 0) {
        casa = (getuid() == 0) ? "/root" : "/";
        setenv("HOME", casa, 1);
    }
    mkdir(casa, 0700);
    snprintf(casa_exilla, sizeof(casa_exilla), "%s/.exilla", casa);
    mkdir(casa_exilla, 0700);
    snprintf(profilo, sizeof(profilo), "%s/.exilla/profilo", casa);
    mkdir(profilo, 0700);

    setenv("MOZ_FORCE_DISABLE_E10S", "1", 1);
    setenv("EXILLA_FONTS", "/exwin/font", 0);
    setenv("LANG", "it_IT.UTF-8", 0);

    av[n++] = (char *)FIREFOX;
    av[n++] = (char *)"-no-remote";
    av[n++] = (char *)"-profile";
    av[n++] = profilo;
    for (i = 1; i < argc && n < 15; i++) av[n++] = argv[i];
    av[n] = 0;

    /* Firefox parte come processo suo, con l'ambiente preparato qui sopra, e
     * questo programma ha finito: e' una porta, non un guardiano. */
    if (spawn_ex(FIREFOX, av, environ, 0, 0) < 0) {
        dillo("Firefox non parte (memoria insufficiente, o il file e' rovinato: netupdate -check).");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    {
        const char *d = getenv("EXILLA_DIR");

        if (d && d[0] == '/' && strlen(d) + 10 < sizeof(g_dove)) {
            strcpy(g_dove, d);
            snprintf(g_firefox, sizeof(g_firefox), "%s/firefox", d);
        }
    }

    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        printf("uso: exilla [indirizzo]        apre Exilla (Firefox per EX-OS)\n");
        printf("     (EXILLA_DIR=/altro/posto la cerca li')\n");
        printf("     exilla -dove              dice se e' installata\n");
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "-dove") == 0) {
        struct stat st;

        if (stat(FIREFOX, &st) == 0)
            printf("Exilla e' installata in %s (firefox: %u MB).\n", DOVE,
                   (unsigned int)(st.st_size / (1024 * 1024)));
        else
            printf("Exilla non e' installata. Si installa con:  netupdate -install:exilla\n");
        return 0;
    }
    return avvia(argc, argv);
}
