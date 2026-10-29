/* =============================================================================
 * exwin/bin/finestreprova/finestreprova.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Many windows at once: the test of wserver 0.013 and exwin.so 0.020 (voce 42,
 * point 1: no fixed number of windows).
 *
 *     finestreprova [N] [secondi]     (default 40 finestre, 5 secondi)
 *
 * Opens N windows with a title longer than the 48 bytes of old, follows the
 * list of windows (ex_track_windows) and, when the list is whole, counts its
 * own. Up to wserver 0.012 the 17th window was refused, and the list said
 * sixteen at most. It writes on the serial line:
 *
 *     FINESTRE aperte A di N          the windows the server gave
 *     FINESTRE elenco T mie M         the list, whole (T in all, M ours)
 *     FINESTRE fine 0                 all N created and all N in the list
 *     FINESTRE fine 1                 something is missing (or 60 s passed)
 *     FINESTRE chiudo A / chiuse A    around closing them all in a row
 *
 * then keeps them open the given seconds (for a photograph), closes them all
 * and exits.
 * ============================================================================= */

#include "libc.h"
#include "exwin.h"

#define VERSIONE_APP "0.001"
EX_VERSIONE("finestreprova", VERSIONE_APP);

static ExWindow    *g_fin = 0;
static int          g_n = 0, g_aperte = 0, g_esito = -1, g_tieni_ms = 5000;
static unsigned int g_t0 = 0;

static void riga(const char *fmt, int a, int b)
{
    char r[96];

    snprintf(r, sizeof(r), fmt, a, b);
    log_seriale(r);
    printf("%s\n", r);
}

static void chiudi_tutte(int codice)
{
    int i;

    riga("FINESTRE chiudo %d (dopo %d ms)", g_aperte, (int)(uptime_ms() - g_t0));
    for (i = 0; i < g_n; i++) if (g_fin[i]) ex_destroy(g_fin[i]);
    riga("FINESTRE chiuse %d", g_aperte, 0);
    ex_quit(codice);
}

static void conta(void)
{
    ExWindowEntry *v;
    int n = ex_list_windows(0, 0), i, mie = 0;
    unsigned int io = (unsigned int)getpid();

    v = (ExWindowEntry *)malloc((unsigned int)(n ? n : 1) * sizeof(ExWindowEntry));
    if (!v) return;
    n = ex_list_windows(v, n);
    for (i = 0; i < n; i++) if (v[i].pid == io) mie++;
    free(v);

    riga("FINESTRE elenco %d mie %d", n, mie);
    if (g_esito < 0 && mie == g_n && g_aperte == g_n) {
        g_esito = 0;
        riga("FINESTRE fine %d (in %d ms)", 0, (int)(uptime_ms() - g_t0));
        ex_set_timer(g_fin[0], (unsigned int)g_tieni_ms);
    }
}

static long proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_WINDOW_LIST:
        conta();
        return 0;

    case EXM_TIMER:
        if (g_esito < 0) {
            g_esito = 1;
            riga("FINESTRE fine %d (dopo %d ms)", 1, (int)(uptime_ms() - g_t0));
        }
        chiudi_tutte(g_esito);
        return 0;

    case EXM_CLOSE:
        chiudi_tutte(g_esito < 0 ? 1 : g_esito);
        return 0;

    case EXM_PAINT:
        ex_fill_rect(f, 0, 0, 2000, 2000, 0x00C0C0C0);
        ex_draw_text(f, 6, 6, "molte finestre", 0x00000000);
        ex_update(f);
        return 0;

    default:
        return ex_default_proc(f, msg, wp, lp);
    }
}

int main(int argc, char **argv)
{
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          i;
    char         titolo[160];

    g_n = (argc > 1) ? atoi(argv[1]) : 40;
    if (g_n <= 0) g_n = 40;
    if (argc > 2 && atoi(argv[2]) > 0) g_tieni_ms = atoi(argv[2]) * 1000;

    ex_screen_size(&sw, &sh);
    if (sw == 0) {
        printf("finestreprova: il server a finestre non risponde (exwin).\n");
        return 1;
    }

    g_fin = (ExWindow *)calloc((unsigned int)g_n, sizeof(ExWindow));
    if (!g_fin) return 1;
    g_t0 = uptime_ms();

    for (i = 0; i < g_n; i++) {
        snprintf(titolo, sizeof(titolo),
                 "Finestra %d di %d, con un titolo piu' lungo dei 48 byte di una volta",
                 i + 1, g_n);
        g_fin[i] = ex_create("window", titolo, EX_CAPTION | EX_BORDER | EX_CLOSEBOX,
                             EX_AUTO, EX_AUTO, 420, 60, 0, 0, proc);
        if (g_fin[i]) {
            g_aperte++;
            proc(g_fin[i], EXM_PAINT, 0, 0);
        }
    }
    riga("FINESTRE aperte %d di %d", g_aperte, g_n);
    if (g_aperte == 0) return 1;

    /* the list comes when it changes; and a timer as a safety net */
    ex_track_windows(g_fin[0]);
    ex_set_timer(g_fin[0], 60000);

    while (ex_get_message(&m)) ex_dispatch(&m);
    return g_esito < 0 ? 1 : g_esito;
}
