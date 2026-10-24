/* =============================================================================
 * exwin/bin/attesa/attesa.c — "loading, please wait", for big programs
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 *     attesa [-n NAME] [-s SECONDS] -- PROGRAM [ARGUMENTS...]
 *
 * The user (17 October 2026): Firefox works, but a program that size takes
 * time to load on an old machine; at the click a box must appear at once
 * saying it is loading and to wait, with an animated hourglass - and the same
 * box must serve every big program.
 *
 * So this program is the box AND the starter: it opens its small window,
 * draws the hourglass, starts PROGRAM (spawn_ex, with the environment it was
 * given), and closes as soon as one of these happens:
 *   - PROGRAM opens its first window (asked to the server twice a second
 *     with ex_ask_windows: the taskbar's subscription is not touched);
 *   - PROGRAM ends (if it ends with an error before any window, the box says
 *     so instead of vanishing);
 *   - SECONDS pass (300 by default: a program that takes longer is not
 *     waited for in silence for ever);
 *   - the person closes the box (the program goes on loading).
 *
 * Nobody calls it by hand: ex_spawn_program (lib/exwin) sends every program
 * file of 8 MB or more through it - the menu (pm), files opened from the
 * file manager and the desktop, the exilla launcher.
 *
 * ! EXWIN_LOG=FILE SENDS THE PROGRAM'S OUTPUT TO FILE (24 October 2026,
 * attesa 0.005): stdout and stderr both, one shared descriptor, so the lines
 * stay in order. Started from the desktop, a program writes on the text
 * console, which nobody sees and which is slow to scroll; on the real
 * machine Firefox's messages were only readable from photos of the screen.
 * The exilla launcher sets it to ~/.exilla/firefox.log. An older attesa
 * ignores the variable and the program writes on the console as before.
 *
 * ! THE HOURGLASS IS DRAWN WITH RECTANGLES, a row at a time: the toolkit has
 * no polygons. Two triangles of glass, the sand of the top one going down
 * while the bottom one fills, a thin stream between them; at the end of each
 * round it turns over (two frames on its side) and starts again.
 * ============================================================================= */
#include "libc.h"
#include "exwin.h"

#define VERSIONE_APP "0.005"
EX_VERSIONE("attesa", VERSIONE_APP);

#define FIN_W      330
#define FIN_H      120
#define CLESS_X    24        /* the hourglass, in the window */
#define CLESS_Y    18
#define CLESS_W    44
#define CLESS_H    76
#define PASSO_MS   120       /* one frame */
#define GIRO       24        /* frames of sand per round */
#define CHIEDI_OGNI 4        /* ask the server every 4 frames (~0.5 s) */

#define C_FONDO    0x00E8E8E8
#define C_VETRO    0x00F8F8FF
#define C_BORDO    0x00404040
#define C_SABBIA   0x00D8A040
#define C_LEGNO    0x00805020
#define C_TESTO    EX_BLACK

static ExWindow     g_f;
static char         g_nome[64] = "Il programma";
static int          g_figlio = -1;
static unsigned int g_passo  = 0;      /* frames since the start */
static unsigned int g_t0;
static unsigned int g_max_s  = 300;
static char         g_riga3[96] = "";
static int          g_finito = 0;      /* the child ended badly: the box stays */

/* =============================================================================
 * The hourglass
 * ============================================================================= */

/* Half-width of the glass at row y (0 = top, CLESS_H-1 = bottom): wide at
 * the ends, two pixels at the waist. */
static int mezza(int y)
{
    int meta = CLESS_H / 2, d = y < meta ? meta - y : y - meta + 1;

    return 2 + (d * (CLESS_W / 2 - 4)) / meta;
}

static void disegna_clessidra(void)
{
    int x0 = CLESS_X, y0 = CLESS_Y, cx = CLESS_X + CLESS_W / 2;
    int meta = CLESS_H / 2;
    int fase = (int)(g_passo % (GIRO + 2));
    int y;

    /* the whole square the glass can take, upright or on its side (the
     * text starts at x 90) */
    ex_fill_rect(g_f, 8, y0 - 8, 80, CLESS_H + 16, C_FONDO);

    /* turning over: two frames with the glass on its side */
    if (fase >= GIRO) {
        /* shorter than upright, so it stays inside the square: 60 pixels */
        int l = CLESS_W + 16, h = CLESS_W, xs = cx - l / 2, ys = y0 + CLESS_H / 2 - h / 2;
        int x;

        ex_fill_rect(g_f, xs - 4, ys, 4, h, C_LEGNO);
        ex_fill_rect(g_f, xs + l, ys, 4, h, C_LEGNO);
        for (x = 0; x < l; x++) {
            int m = mezza(x * CLESS_H / l);

            ex_fill_rect(g_f, xs + x, ys + h / 2 - m, 1, 2 * m, C_VETRO);
            ex_fill_rect(g_f, xs + x, ys + h / 2 - m, 1, 1, C_BORDO);
            ex_fill_rect(g_f, xs + x, ys + h / 2 + m - 1, 1, 1, C_BORDO);
        }
        /* the sand lies along the bottom side, half each way */
        for (x = 0; x < l; x++) {
            int m = mezza(x * CLESS_H / l), s = m / 2;

            if (s > 0) ex_fill_rect(g_f, xs + x, ys + h / 2 + m - 1 - s, 1, s, C_SABBIA);
        }
        ex_update(g_f);
        return;
    }

    /* the wooden ends */
    ex_fill_rect(g_f, x0 - 4, y0 - 6, CLESS_W + 8, 6, C_LEGNO);
    ex_fill_rect(g_f, x0 - 4, y0 + CLESS_H, CLESS_W + 8, 6, C_LEGNO);

    /* glass, sand on top going down, sand below piling up: `fase` of GIRO */
    {
        int alto  = (meta - 4) * (GIRO - fase) / GIRO;      /* rows of sand left on top */
        int basso = (meta - 4) * fase / GIRO;               /* rows piled below */

        for (y = 0; y < CLESS_H; y++) {
            int m = mezza(y);
            unsigned int c = C_VETRO;

            if (y < meta && y >= meta - alto) c = C_SABBIA;            /* top: lies at the bottom of the bulb */
            if (y >= meta && y >= CLESS_H - basso) c = C_SABBIA;       /* bottom: piles up from the base */
            ex_fill_rect(g_f, cx - m, y0 + y, 2 * m, 1, c);
            ex_fill_rect(g_f, cx - m, y0 + y, 1, 1, C_BORDO);
            ex_fill_rect(g_f, cx + m - 1, y0 + y, 1, 1, C_BORDO);
        }
        /* the stream through the waist, flickering so it is seen to move */
        if (alto > 0) {
            int lun = CLESS_H - basso - meta;

            for (y = 0; y < lun; y += 3)
                ex_fill_rect(g_f, cx - 1 + (int)((g_passo + (unsigned)y) % 2), y0 + meta + y, 1, 2, C_SABBIA);
        }
    }
    ex_update(g_f);
}

static void disegna_testo(void)
{
    char r1[96], r2[64];
    unsigned int s = (uptime_ms() - g_t0) / 1000u;

    ex_fill_rect(g_f, 90, 14, FIN_W - 96, FIN_H - 20, C_FONDO);
    if (g_finito) {
        snprintf(r1, sizeof(r1), "%s non e' partito.", g_nome);
        ex_draw_text(g_f, 92, 22, r1, C_TESTO);
        ex_draw_text(g_f, 92, 44, g_riga3, C_TESTO);
        ex_draw_text(g_f, 92, 80, "Chiudere questa finestra.", EX_DARK_GRAY);
        ex_update(g_f);
        return;
    }
    snprintf(r1, sizeof(r1), "%s si sta caricando.", g_nome);
    ex_draw_text(g_f, 92, 22, r1, C_TESTO);
    ex_draw_text(g_f, 92, 42, "Attendere, prego...", C_TESTO);
    snprintf(r2, sizeof(r2), "%u s", s);
    ex_draw_text(g_f, 92, 70, r2, EX_DARK_GRAY);
    if (s >= 20) ex_draw_text(g_f, 92, 90, "Su un PC datato ci vuole un po'.", EX_DARK_GRAY);
    ex_update(g_f);
}

/* =============================================================================
 * Watching the program
 * ============================================================================= */
static void diario(const char *riga);

static void fine(void)
{
    char r[64];

    snprintf(r, sizeof(r), "[%u s] chiusa\n", (uptime_ms() - g_t0) / 1000u);
    diario(r);
    ex_set_timer(g_f, 0);
    /* ! THE BOX IS TAKEN DOWN HERE, NOT LEFT TO THE SERVER (0.004, 24 October
     * 2026). The server removes a dead client's windows once a second, and
     * only when the process list fits its table: with Firefox's 150 threads
     * it did not (wserver 0.011), and the box stayed for minutes after this
     * line had been written. */
    ex_destroy(g_f);
    ex_quit(0);
}

/* 1 if the child has a window in the server's list.
 * ! THE WINDOW'S pid IS THE THREAD'S, NOT THE PROGRAM'S (0.003, 21 October
 * 2026). The server records whoever sent WIN_MSG_CREA, and on EX-OS a
 * thread has a pid of its own: on the laptop Firefox's window appeared and
 * the box stayed for the full 300 seconds. A window counts as the child's
 * when its pid's group (proc_gruppo, the tgid) is the child. */
/* ! AND WHAT IT SAW GOES IN /tmp/attesa.txt (0.003): every list whose size
 * changed - seconds, the child, each window's id, pid, group and title -
 * and why the box closed. One line per change, a few lines per start: it
 * is the answer the next time a box does not close. */
static int g_ultimo_n = -1;

static void diario(const char *riga)
{
    int fd;

    mkdir("/tmp", 0777);
    fd = open("/tmp/attesa.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);

    if (fd < 0) return;
    write(fd, riga, strlen(riga));
    close(fd);
}

static int ha_finestra(void)
{
    ExWindowEntry v[24];
    int           n = ex_list_windows(v, 24), i;

    if (n != g_ultimo_n) {
        char r[128];

        g_ultimo_n = n;
        snprintf(r, sizeof(r), "[%u s] %s pid %d: %d finestre\n",
                 (uptime_ms() - g_t0) / 1000u, g_nome, g_figlio, n);
        diario(r);
        for (i = 0; i < n && i < 24; i++) {
            snprintf(r, sizeof(r), "  id %u pid %u gruppo %d \"%.40s\"\n", v[i].id,
                     v[i].pid, proc_gruppo((int)v[i].pid), v[i].title);
            diario(r);
        }
    }
    for (i = 0; i < n && i < 24; i++) {
        if ((int)v[i].pid == g_figlio) return 1;
        if (proc_gruppo((int)v[i].pid) == g_figlio) return 1;
    }
    return 0;
}

static void controlla_figlio(void)
{
    int st = 0, r;

    if (g_figlio <= 0 || g_finito) return;
    r = waitpid(g_figlio, &st, WNOHANG);
    if (r != g_figlio) return;
    /* ended before any window: fine if it said 0 (a launcher that handed
     * over), worth saying otherwise */
    if ((st & 0xFF) == 0) { fine(); return; }
    snprintf(g_riga3, sizeof(g_riga3), "E' uscito con il codice %d.", st & 0xFF);
    g_finito = 1;
    ex_set_timer(g_f, 0);
    disegna_testo();
}

static long proc(ExWindow f, unsigned int msg, unsigned int wp, long lp)
{
    switch (msg) {
    case EXM_CLOSE:
        fine();
        return 0;

    case EXM_TIMER:
        g_passo++;
        disegna_clessidra();
        if (g_passo % 8 == 0) disegna_testo();
        if (g_passo % CHIEDI_OGNI == 0) {
            controlla_figlio();
            if (!g_finito) ex_ask_windows(g_f);
        }
        if ((uptime_ms() - g_t0) / 1000u >= g_max_s) fine();
        return EX_NO_REDRAW;

    case EXM_WINDOW_LIST:
        if (ha_finestra()) fine();
        return EX_NO_REDRAW;

    case EXM_PAINT:
        ex_fill_rect(f, 0, 0, FIN_W, FIN_H, C_FONDO);
        disegna_clessidra();
        disegna_testo();
        return 0;

    default:
        return ex_default_proc(f, msg, wp, lp);
    }
}

static void uso(void)
{
    printf("uso: attesa [-n NOME] [-s SECONDI] -- PROGRAMMA [ARGOMENTI...]\n");
    printf("  mostra \"caricamento in corso\" con una clessidra, avvia PROGRAMMA e si\n");
    printf("  chiude quando compare la sua prima finestra (o dopo SECONDI, 300).\n");
}

/* spawn_ex with stdout and stderr on EXWIN_LOG, if it is set and opens. */
static int avvia(char **prog)
{
    const char *log = getenv("EXWIN_LOG");
    SpawnRedir  r[2];
    int         fd, pid;

    if (!log || !log[0]) return spawn_ex(prog[0], prog, environ, 0, 0);
    fd = open(log, O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0600);
    if (fd < 0) return spawn_ex(prog[0], prog, environ, 0, 0);
    r[0].fd = 1; r[0].flags = 0; r[0].percorso = NULL; r[0].fd_padre = fd;
    r[1].fd = 2; r[1].flags = 0; r[1].percorso = NULL; r[1].fd_padre = fd;
    pid = spawn_ex(prog[0], prog, environ, r, 2);
    close(fd);                  /* the child holds its own reference */
    return pid;
}

int main(int argc, char **argv)
{
    ExMsg        m;
    unsigned int sw = 0, sh = 0;
    int          i = 1;
    char       **prog;
    char         titolo[96];

    for (; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            snprintf(g_nome, sizeof(g_nome), "%s", argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            g_max_s = (unsigned int)atoi(argv[++i]);
            if (g_max_s == 0) g_max_s = 300;
        } else if (argv[i][0] == '-') {
            uso();
            return 2;
        } else {
            break;
        }
    }
    if (i >= argc) { uso(); return 2; }
    prog = &argv[i];
    if (strcmp(g_nome, "Il programma") == 0) {
        const char *b = strrchr(prog[0], '/');

        snprintf(g_nome, sizeof(g_nome), "%s", b ? b + 1 : prog[0]);
    }

    ex_screen_size(&sw, &sh);
    if (sw == 0) {
        /* no desktop: nothing to show, the program starts as it would */
        return avvia(prog) < 0 ? 1 : 0;
    }

    snprintf(titolo, sizeof(titolo), "Avvio di %s", g_nome);
    g_f = ex_create("window", titolo, EX_CAPTION | EX_BORDER | EX_CLOSEBOX | EX_TOPMOST,
                    ((int)sw - FIN_W) / 2, ((int)sh - FIN_H) / 2 - 40,
                    FIN_W, FIN_H, 0, 0, proc);
    if (!g_f) return avvia(prog) < 0 ? 1 : 0;

    g_t0 = uptime_ms();
    proc(g_f, EXM_PAINT, 0, 0);

    /* ! THE BOX FIRST, THE PROGRAM AFTER: the point is that something is on
     * the screen at once, before the system starts reading a file of
     * hundreds of megabytes. */
    g_figlio = avvia(prog);
    if (g_figlio < 0) {
        snprintf(g_riga3, sizeof(g_riga3), "Il file non si avvia (%d).", g_figlio);
        g_finito = 1;
        disegna_testo();
    } else {
        ex_set_timer(g_f, PASSO_MS);
    }
    while (ex_get_message(&m)) ex_dispatch(&m);
    return 0;
}
