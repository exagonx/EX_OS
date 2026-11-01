/* =============================================================================
 * bin/dmesg/dmesg.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * dmesg — quel che il kernel ha detto (8 ottobre 2026, vuole il kernel 0.242)
 *
 *     dmesg            l'avvio, poi gli ultimi messaggi
 *     dmesg -a         solo l'avvio
 *     dmesg -u         solo gli ultimi
 *     dmesg PAROLA     solo le righe che contengono PAROLA
 *     dmesg -segui     avvisi ed errori man mano che arrivano
 *
 * -segui is for a machine that may lose its disk: it asks for nothing after
 * the first round, so it goes on printing when no program can be loaded.
 * tools/dmesg-pc.py keeps it on a telnet session and saves it on the PC.
 *
 * Il kernel tiene due zone (kernel/arch/x86/kprintf.c): i primi 32 KB detti
 * dall'accensione, che non si sovrascrivono, e un anello con gli ultimi 32.
 * Serve su una macchina vera guidata da una sessione di rete, dove lo schermo
 * non si vede e la seriale non c'e': «perche' il driver ha rinunciato» sta
 * scritto qui.
 * ============================================================================= */

#include "libc.h"

/* +0.001 a ogni modifica: `dmesg -version` la stampa. */
EX_VERSIONE("dmesg", "0.002");

#define ZONA 32768

static char g_buf[ZONA + 1];
static char g_prima[ZONA + 1];
static int  g_solo_guai = 0;

/* Stampa la zona, intera o le sole righe con `parola`. Toglie i codici di
 * colore: su un terminale di rete sarebbero rumore. */
static void stampa(int n, const char *parola)
{
    int i = 0;

    while (i < n) {
        char riga[512];
        int  k = 0;

        while (i < n && g_buf[i] != '\n') {
            char c = g_buf[i++];

            if (c == 0x1B) {                    /* ESC [ ... lettera */
                while (i < n && g_buf[i] != '\n' &&
                       !((g_buf[i] >= 'A' && g_buf[i] <= 'Z') ||
                         (g_buf[i] >= 'a' && g_buf[i] <= 'z'))) i++;
                if (i < n && g_buf[i] != '\n') i++;
                continue;
            }
            if (c == '\r') continue;
            if (k < (int)sizeof(riga) - 1) riga[k++] = c;
        }
        if (i < n) i++;                         /* l'a capo */
        riga[k] = 0;
        if (g_solo_guai && !strstr(riga, "[WARN") && !strstr(riga, "[ERR")) continue;
        if (parola == NULL || strstr(riga, parola) != NULL) printf("%s\n", riga);
    }
}

/* The ring is rewritten as it fills, so what is new is what follows the end of
 * the previous reading. */
static void segui(const char *parola)
{
    int prima = 0, n, code, i, trovato;

    g_solo_guai = 1;
    for (;;) {
        n = klog_leggi(g_buf, ZONA, 1);
        if (n < 0) return;
        code = prima < 256 ? prima : 256;
        trovato = 0;                    /* nothing in common: all of it */
        for (i = n - code; code > 0 && i >= 0; i--)
            if (memcmp(g_buf + i, g_prima + prima - code, (size_t)code) == 0) {
                trovato = i + code;
                break;
            }
        memcpy(g_prima, g_buf, (size_t)n);
        prima = n;
        memmove(g_buf, g_buf + trovato, (size_t)(n - trovato));
        stampa(n - trovato, parola);
        fflush(stdout);
        sleep(1);
    }
}

int main(int argc, char **argv)
{
    const char *parola = NULL;
    int avvio = 1, ultimi = 1, segue = 0, i, n;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-segui") == 0)  { segue = 1; }
        else if (strcmp(argv[i], "-a") == 0) ultimi = 0;
        else if (strcmp(argv[i], "-u") == 0) avvio = 0;
        else if (argv[i][0] == '-') {
            printf("uso: dmesg [-a | -u | -segui] [PAROLA]\n");
            printf("  -a      solo quel che il kernel ha detto all'avvio\n");
            printf("  -u      solo gli ultimi messaggi\n");
            printf("  -segui  avvisi ed errori man mano che arrivano\n");
            printf("  PAROLA  solo le righe che la contengono\n");
            return 2;
        } else parola = argv[i];
    }

    if (segue) { segui(parola); return 1; }

    if (avvio) {
        n = klog_leggi(g_buf, ZONA, 0);
        if (n < 0) {
            printf("dmesg: il kernel non tiene il registro dei messaggi (%d):\n"
                   "       serve il kernel 0.242 o successivo.\n", n);
            return 1;
        }
        stampa(n, parola);
    }
    if (ultimi) {
        n = klog_leggi(g_buf, ZONA, 1);
        if (n > 0) {
            if (avvio && parola == NULL) printf("----- gli ultimi messaggi -----\n");
            stampa(n, parola);
        }
    }
    return 0;
}
