/* =============================================================================
 * tools/prove/cursesprova.c — ncurses on the EX-OS console (kernel 0.279)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     cursesprova          draws, reads keys until 'q', then one line:
 *                          "CURSES tasti: <keyname> <keyname> ..."
 *
 * Built static with -lncurses (tools/ncurses-exos/costruisci.sh puts it in
 * the cross sysroots). tools/prova_curses.sh reads the screen back with
 * tools/schermo_testo.py while it waits for keys, then types Up, F1, Delete,
 * Page Down, q: curses must turn each escape sequence of the console into
 * its KEY_ code, as the "vga" terminfo entry describes them.
 * ============================================================================= */
#include <curses.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char tasti[256] = "";
    WINDOW *w;
    int c, n = 0;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_YELLOW, COLOR_BLUE);
    }

    box(stdscr, 0, 0);
    mvprintw(1, 2, "CURSES PRONTO %dx%d TERM=%s", COLS, LINES, termname());
    attron(A_REVERSE);
    mvprintw(2, 2, "INVERSO");
    attroff(A_REVERSE);
    attron(COLOR_PAIR(1));
    mvprintw(3, 2, "COLORE");
    attroff(COLOR_PAIR(1));
    mvprintw(4, 2, "riga che sparira'");
    mvprintw(5, 2, "riga che sale");
    refresh();
    /* deleteln: curses sends DL (or redraws): row 5 moves up to row 4 */
    move(4, 2);
    deleteln();
    box(stdscr, 0, 0);
    refresh();

    w = newwin(5, 20, 7, 10);
    box(w, 0, 0);
    mvwprintw(w, 2, 2, "FINESTRA");
    wrefresh(w);
    move(LINES - 2, 2);
    printw("tasti, q per uscire");
    refresh();

    while ((c = getch()) != 'q' && n < 16) {
        const char *k = keyname(c);
        if (strlen(tasti) + strlen(k ? k : "?") + 2 < sizeof tasti) {
            strcat(tasti, " ");
            strcat(tasti, k ? k : "?");
        }
        n++;
    }
    endwin();
    printf("CURSES tasti:%s q\n", tasti);
    return 0;
}
