/* =============================================================================
 * tools/prove/vgaprova.c — the console's escape sequences for curses (0.279)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     vgaprova         draws, then waits 10 seconds for the screendump
 *
 * Each row of the screen comes out of one sequence: tools/prova_termios.sh
 * reads it back with tools/schermo_testo.py and compares it with what the
 * Linux console would show:
 *
 *    1 AA  AA           ICH  ESC[2@ in the middle of AAAA
 *    2                  IL   ESC[L above BBBB (then DL took a row away)
 *    3 BB               DCH  ESC[2P at the start of BBBB
 *    4 D  D             ECH  ESC[2X in DDDD; DL ESC[M removed CCCC
 *    5 EEEE
 *    7     x            VPA ESC[7d, CHA ESC[5G
 *    8 s                ESC 7, somewhere else, ESC 8
 *    9 INV N            SGR 7 / 27 (INV in reverse)
 *   10                  RI ESC M at the top of the region scrolls it down
 *   11 R2               region 10-12: R1 R2 R3 and a LF at its bottom,
 *   12 R3                  then RI: R1 gone, R4 gone
 *   13 FUORI            outside the region, never moved
 *   14 ZY               ESC(B and ESC[>0c swallowed, not printed
 * ============================================================================= */
#include <stdio.h>
#include <unistd.h>

#define E "\033"

int main(void)
{
    fputs(E "[2J" E "[H", stdout);
    fputs(E "[1;1HAAAA" E "[2;1HBBBB" E "[3;1HCCCC" E "[4;1HDDDD" E "[5;1HEEEE", stdout);
    fputs(E "[2;1H" E "[L", stdout);            /* IL */
    fputs(E "[4;1H" E "[M", stdout);            /* DL: CCCC */
    fputs(E "[1;3H" E "[2@", stdout);           /* ICH */
    fputs(E "[3;1H" E "[2P", stdout);           /* DCH */
    fputs(E "[4;2H" E "[2X", stdout);           /* ECH */
    fputs(E "[7d" E "[5Gx", stdout);            /* VPA, CHA */
    fputs(E "[8;1H" E "7" E "[20;20H" E "8s", stdout);
    fputs(E "[9;1H" E "[7mINV" E "[27m N", stdout);
    fputs(E "[13;1HFUORI", stdout);
    fputs(E "[10;12r", stdout);                 /* DECSTBM */
    fputs(E "[10;1HR1" E "[11;1HR2" E "[12;1HR3\nR4", stdout);
    fputs(E "[10;1H" E "M", stdout);            /* RI */
    fputs(E "[r", stdout);
    fputs(E "[14;1H" E "(BZ" E "[>0cY", stdout);
    fputs(E "[20;1HVGAPROVA PRONTA", stdout);
    fflush(stdout);
    sleep(10);
    fputs("\n", stdout);
    return 0;
}
