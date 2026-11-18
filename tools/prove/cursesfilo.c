/* =============================================================================
 * tools/prove/cursesfilo.c - the keyboard read from a second thread
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * For tools/prova_curses.sh (kernel 0.348). VLC's text interface calls
 * getch() with timeout(1000) from a thread of its own; up to 0.347 the
 * console gave every thread but the first "end of input", and the keys
 * stayed for the shell. Here a pthread does the same until 'q', then the
 * main thread prints what it read: "CURSES filo: 120 113" for x and q.
 * ============================================================================= */
#include <curses.h>
#include <pthread.h>
#include <stdio.h>

static int letti[16], n;

static void *filo(void *x)
{
    int c;

    (void)x;
    while (n < 16) {
        c = getch();
        if (c == ERR) continue;         /* the second of timeout() went by */
        letti[n++] = c;
        if (c == 'q') break;
    }
    return 0;
}

int main(void)
{
    pthread_t f;
    int i;

    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); nonl(); timeout(1000);
    mvprintw(0, 0, "CURSES FILO PRONTO"); refresh();
    pthread_create(&f, 0, filo, 0);
    pthread_join(f, 0);
    endwin();
    printf("CURSES filo:");
    for (i = 0; i < n; i++) printf(" %d", letti[i]);
    printf("\n");
    return 0;
}
