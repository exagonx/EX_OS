/* =============================================================================
 * bin/kill/kill.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * =============================================================================
 *
 * Mandare un segnale a un processo (@RDBOOT passo 4, 22 ottobre 2026)
 *
 *   kill <pid> ...         chiede di chiudersi (SIGTERM): chi ha un gestore
 *                          puo' salvare e uscire
 *   kill -9 <pid> ...      chiude subito (SIGKILL), senza chiedere
 *   kill -<n> <pid> ...    il segnale numero n; anche -KILL, -TERM, -INT,
 *                          -HUP
 *
 * ! IL PERMESSO LO DECIDE IL KERNEL (segnali.c): un utente chiude solo i
 * propri processi, root tutti, init (pid 1) nessuno. Qui si traduce la
 * risposta in parole: EPERM e ESRCH sono cose diverse da dire.
 *
 * Sta in /rdboot/bin con ps: vedi bin/ps/ps.c.
 * ============================================================================= */
#include "libc.h"

/* +0.001 a ogni modifica: `kill -version` la stampa. Vedi EX_VERSIONE in libc.h. */
EX_VERSIONE("kill", "0.001");

static int segnale_da(const char *s)
{
    if (s[0] >= '0' && s[0] <= '9') return atoi(s);
    if (strcmp(s, "KILL") == 0) return SIGKILL;
    if (strcmp(s, "TERM") == 0) return SIGTERM;
    if (strcmp(s, "INT")  == 0) return SIGINT;
    if (strcmp(s, "HUP")  == 0) return SIGHUP;
    return -1;
}

int main(int argc, char **argv)
{
    int sig = SIGTERM, i = 1, esito = 0;

    if (argc > 1 && argv[1][0] == '-') {
        sig = segnale_da(argv[1] + 1);
        if (sig <= 0) {
            printf("kill: segnale \"%s\" sconosciuto (-9, -KILL, -TERM, -INT, -HUP)\n",
                   argv[1] + 1);
            return 2;
        }
        i = 2;
    }
    if (i >= argc) {
        printf("uso: kill [-9|-<segnale>] <pid> ...\n");
        printf("     senza segnale chiede di chiudersi (TERM), -9 chiude subito\n");
        return 2;
    }

    for (; i < argc; i++) {
        int pid = atoi(argv[i]);

        if (pid <= 0) {
            printf("kill: \"%s\" non e' un pid\n", argv[i]);
            esito = 1;
            continue;
        }
        if (kill(pid, sig) == 0) continue;
        if (errno == EPERM)
            printf("kill: %d non e' tuo (o e' init): serve root\n", pid);
        else if (errno == ESRCH)
            printf("kill: nessun processo %d\n", pid);
        else
            printf("kill: %d: errore %d\n", pid, errno);
        esito = 1;
    }
    return esito;
}
