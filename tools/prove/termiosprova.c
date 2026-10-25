/* =============================================================================
 * tools/prove/termiosprova.c — termios with the libc.a functions (kernel 0.279)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     termiosprova             one line per check, then "TERMIOS fine"
 *     termiosprova tasti       stdin in raw mode: type  a b Up Enter q
 *
 * libctest proves the pty through ioctl (it runs on the shared libc, which
 * leaves termios to libc.a); this is the same through tcgetattr, tcsetattr,
 * cfmakeraw, tcflush, as ported software (ncurses, readline) calls them.
 *   TERMIOS ok <what>    /   TERMIOS NO <what>
 * ============================================================================= */
#include <libc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <poll.h>

static int g_male;

static void prova(const char *cosa, int giusto)
{
    printf("TERMIOS %s %s\n", giusto ? "ok" : "NO", cosa);
    if (!giusto) g_male++;
}

static int pronto(int fd, int ms)
{
    struct pollfd p;
    p.fd = fd; p.events = POLLIN; p.revents = 0;
    return poll(&p, 1, ms) == 1 && (p.revents & POLLIN);
}

static void svuota(int fd)
{
    char b[64];
    while (pronto(fd, 50)) if (read(fd, b, sizeof b) <= 0) break;
}

/* The keys from the terminal this runs on, in raw mode, as an editor or
 * ncurses reads them: no echo, no lines, one byte at a time, Enter is CR,
 * an arrow is its whole escape sequence. tools/prova_termios.sh types
 * "ab", Up, Enter, "q". */
static int tasti(void)
{
    struct termios s, r;
    unsigned char got[64], b[16];
    int n = 0, k, i, finito = 0;
    static const unsigned char atteso[] = { 'a', 'b', 27, '[', 'A', '\r', 'q' };

    if (tcgetattr(0, &s) != 0) { prova("tcgetattr sullo stdin", 0); return 1; }
    r = s;
    cfmakeraw(&r);
    r.c_cc[VMIN] = 1; r.c_cc[VTIME] = 0;
    prova("stdin in modo grezzo", tcsetattr(0, TCSAFLUSH, &r) == 0);
    prova("poll sullo stdin senza tasti: niente", !pronto(0, 200));
    printf("TERMIOS pronto ai tasti\r\n");
    fflush(stdout);
    while (!finito && n < (int)sizeof got && pronto(0, 20000)) {
        k = (int)read(0, b, sizeof b);
        if (k <= 0) break;
        for (i = 0; i < k && n < (int)sizeof got; i++) {
            got[n++] = b[i];
            if (b[i] == 'q') finito = 1;
        }
    }
    tcsetattr(0, TCSAFLUSH, &s);
    printf("TERMIOS tasti:");
    for (i = 0; i < n; i++) printf(" %02x", got[i]);
    printf("\n");
    prova("i tasti arrivano byte per byte, la freccia intera, Invio come CR",
          n == (int)sizeof atteso && memcmp(got, atteso, sizeof atteso) == 0);
    printf("TERMIOS fine %d\n", g_male);
    return g_male != 0;
}

int main(int argc, char **argv)
{
    int fd[2], n;
    char b[64];
    struct termios t, salvato;
    struct winsize w;

    if (argc > 1 && strcmp(argv[1], "tasti") == 0) return tasti();
    if (pty_apri(fd) != 0) { prova("pty_apri", 0); return 1; }

    prova("tcgetattr", tcgetattr(fd[1], &t) == 0);
    salvato = t;
    prova("isatty", isatty(fd[1]));
    prova("tcgetattr su un file che non e' un terminale: ENOTTY",
          tcgetattr(open("/a/termiosprova", 0), &t) < 0 && errno == ENOTTY);
    t = salvato;

    pty_ctl(fd[0], PTY_CTL_MISURA, (40u << 16) | 132u);
    prova("TIOCGWINSZ 40x132", ioctl(fd[1], TIOCGWINSZ, &w) == 0 &&
          w.ws_row == 40 && w.ws_col == 132);

    write(fd[1], "x\n", 2);
    n = (int)read(fd[0], b, sizeof b);
    prova("ONLCR", n == 3 && memcmp(b, "x\r\n", 3) == 0);

    /* a line typed half, then raw mode: the half line is not lost */
    write(fd[0], "ab", 2);
    svuota(fd[0]);
    cfmakeraw(&t);
    prova("tcsetattr TCSANOW raw", tcsetattr(fd[1], TCSANOW, &t) == 0);
    n = (int)read(fd[1], b, sizeof b);
    prova("la riga a meta' passa al modo grezzo", n == 2 && memcmp(b, "ab", 2) == 0);
    write(fd[0], "\033[A", 3);
    n = (int)read(fd[1], b, sizeof b);
    prova("una freccia arriva intera, tre byte", n == 3 && b[0] == 27);
    prova("niente eco in grezzo", !pronto(fd[0], 100));

    t.c_cc[VMIN] = 3; t.c_cc[VTIME] = 0;
    tcsetattr(fd[1], TCSANOW, &t);
    write(fd[0], "12", 2);
    prova("VMIN=3: con due byte poll dice: non ancora", !pronto(fd[1], 50));
    write(fd[0], "3", 1);
    n = (int)read(fd[1], b, sizeof b);
    prova("VMIN=3: al terzo si', tutti e tre", n == 3 && memcmp(b, "123", 3) == 0);

    t.c_cc[VMIN] = 5; t.c_cc[VTIME] = 3;
    tcsetattr(fd[1], TCSANOW, &t);
    write(fd[0], "ab", 2);
    {
        unsigned int prima = uptime_ms();
        n = (int)read(fd[1], b, sizeof b);
        prova("VMIN=5 VTIME=3: due byte, poi 300 ms di silenzio e si torna",
              n == 2 && uptime_ms() - prima >= 250 && uptime_ms() - prima < 1500);
    }

    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 2;
    tcsetattr(fd[1], TCSANOW, &t);
    {
        unsigned int prima = uptime_ms(), dopo;
        n = (int)read(fd[1], b, sizeof b);
        dopo = uptime_ms() - prima;
        printf("TERMIOS VMIN=0 VTIME=2: %d byte in %u ms\n", n, dopo);
        prova("VMIN=0 VTIME=2: niente, e si torna dopo 200 ms",
              n == 0 && dopo >= 150 && dopo < 1000);
    }

    write(fd[0], "zz", 2);
    prova("tcflush TCIFLUSH", tcflush(fd[1], TCIFLUSH) == 0);
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
    tcsetattr(fd[1], TCSANOW, &t);
    prova("dopo tcflush non resta niente", read(fd[1], b, sizeof b) == 0);

    prova("tcsetattr TCSAFLUSH: i modi salvati", tcsetattr(fd[1], TCSAFLUSH, &salvato) == 0);
    write(fd[0], "riga\r", 5);
    n = (int)read(fd[1], b, sizeof b);
    prova("di nuovo a righe, con ICRNL", n == 5 && memcmp(b, "riga\n", 5) == 0);
    tcgetattr(fd[1], &t);
    prova("tcgetattr rende cio' che si e' messo",
          t.c_lflag == salvato.c_lflag && t.c_iflag == salvato.c_iflag &&
          t.c_cc[VMIN] == salvato.c_cc[VMIN]);

    /* the terminal this program runs on: the console, or a pty from telnet */
    if (isatty(0)) {
        prova("tcgetattr sullo stdin", tcgetattr(0, &t) == 0);
        prova("TIOCGWINSZ sullo stdin, piu' di 0x0",
              ioctl(0, TIOCGWINSZ, &w) == 0 && w.ws_row > 0 && w.ws_col > 0);
        printf("TERMIOS stdin %ux%u\n", w.ws_col, w.ws_row);
    }

    close(fd[0]);
    close(fd[1]);
    printf("TERMIOS fine %d\n", g_male);
    return g_male != 0;
}
