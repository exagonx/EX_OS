/* =============================================================================
 * tools/prove/lucchettiprova.c — file locks between two processes (kernel 0.291)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     lucchettiprova tieni FILE DA LUNG SECONDI [w|r]   holds a lock, then ends
 *     lucchettiprova tutti FILE [PROGRAMMA]             the cases, one process
 *     lucchettiprova altro FILE                         the cases of the other
 *
 * For tools/prova_lucchetti.sh: `tieni` runs in the background under `altro`.
 * One line per case, "LUCCHI <caso> OK" or "LUCCHI <caso> NO <why>".
 * ============================================================================= */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

static int g_falliti;

static void esito(const char *caso, int bene, const char *perche)
{
    if (bene) printf("LUCCHI %s OK\n", caso);
    else { printf("LUCCHI %s NO %s (errno %d)\n", caso, perche, errno); g_falliti++; }
    fflush(stdout);
}

static int prendi(int fd, int cmd, int tipo, long da, long lung)
{
    struct flock l;

    memset(&l, 0, sizeof(l));
    l.l_type = (short)tipo; l.l_whence = SEEK_SET; l.l_start = da; l.l_len = lung;
    return fcntl(fd, cmd, &l);
}

static int tieni(const char *f, long da, long lung, int secondi, int tipo)
{
    int fd = open(f, O_RDWR | O_CREAT, 0644);

    if (fd < 0 || prendi(fd, F_SETLK, tipo, da, lung) != 0) {
        printf("LUCCHI tieni NO non preso (errno %d)\n", errno);
        return 1;
    }
    printf("LUCCHI tieni %d\n", (int)getpid());
    fflush(stdout);
    sleep((unsigned)secondi);
    return 0;
}

/* a process that holds 0-9 for writing and 100 up for reading is in the background */
static int altro(const char *f)
{
    int          fd = open(f, O_RDWR), fd2;
    struct flock l;

    esito("aperto", fd >= 0, "open");
    esito("scrittura-rifiutata", prendi(fd, F_SETLK, F_WRLCK, 0, 10) < 0 && (errno == EAGAIN || errno == EACCES), "preso");
    esito("lettura-rifiutata", prendi(fd, F_SETLK, F_RDLCK, 5, 1) < 0, "preso");
    esito("byte-accanto", prendi(fd, F_SETLK, F_WRLCK, 10, 10) == 0, "rifiutato");
    esito("letture-insieme", prendi(fd, F_SETLK, F_RDLCK, 100, 10) == 0, "rifiutato");
    esito("scrittura-su-lettura", prendi(fd, F_SETLK, F_WRLCK, 200, 10) < 0, "preso");
    memset(&l, 0, sizeof(l));
    l.l_type = F_WRLCK; l.l_whence = SEEK_SET; l.l_start = 3; l.l_len = 1;
    esito("getlk-dice-chi", fcntl(fd, F_GETLK, &l) == 0 && l.l_type == F_WRLCK && l.l_start == 0 &&
                            l.l_len == 10 && l.l_pid > 0, "non lo dice");
    l.l_type = F_WRLCK; l.l_whence = SEEK_SET; l.l_start = 50; l.l_len = 1;
    esito("getlk-libero", fcntl(fd, F_GETLK, &l) == 0 && l.l_type == F_UNLCK, "non libero");
    /* flock is whole file: the writer of 0-9 stands in the way */
    esito("flock-nb", flock(fd, LOCK_EX | LOCK_NB) < 0, "preso");
    /* leaving the middle of 10-19 keeps both ends */
    esito("meta-lasciata", prendi(fd, F_SETLK, F_UNLCK, 13, 2) == 0, "unlock");
    /* the other fd of this process closing drops all our locks on the file */
    fd2 = open(f, O_RDONLY);
    close(fd2);
    /* ! SETLKW: the holder ends by itself, within its seconds */
    esito("attesa", prendi(fd, F_SETLKW, F_WRLCK, 0, 10) == 0, "non preso");
    close(fd);
    return g_falliti;
}

/* one process alone: an owner never conflicts with itself */
static int tutti(const char *f)
{
    int fd = open(f, O_RDWR | O_CREAT, 0644), fd2;

    esito("mio-due-volte", prendi(fd, F_SETLK, F_WRLCK, 0, 0) == 0 &&
                           prendi(fd, F_SETLK, F_WRLCK, 0, 10) == 0, "rifiutato");
    esito("mio-cambia-tipo", prendi(fd, F_SETLK, F_RDLCK, 0, 10) == 0, "rifiutato");
    esito("mio-lascia", prendi(fd, F_SETLK, F_UNLCK, 0, 0) == 0, "rifiutato");
    fd2 = open(f, O_RDWR);
    esito("mio-altro-fd", prendi(fd, F_SETLK, F_WRLCK, 0, 0) == 0 &&
                          prendi(fd2, F_SETLK, F_WRLCK, 0, 0) == 0, "rifiutato");
    close(fd2);
    close(fd);
    printf("LUCCHI FINE %d\n", g_falliti);
    return g_falliti;
}

int main(int argc, char **argv)
{
    if (argc >= 6 && strcmp(argv[1], "tieni") == 0)
        return tieni(argv[2], atol(argv[3]), atol(argv[4]), atoi(argv[5]),
                     argc > 6 && argv[6][0] == 'r' ? F_RDLCK : F_WRLCK);
    if (argc == 3 && strcmp(argv[1], "altro") == 0) {
        int r = altro(argv[2]);
        printf("LUCCHI FINE %d\n", r);
        return r;
    }
    if (argc == 3 && strcmp(argv[1], "tutti") == 0) return tutti(argv[2]);
    printf("uso: lucchettiprova tieni|tutti|altro ...\n");
    return 2;
}
