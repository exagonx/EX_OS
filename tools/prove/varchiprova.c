/* =============================================================================
 * tools/prove/varchiprova.c - the holes of analisi_kernel voci 4 and 46
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     varchiprova <directory on a writable disk>     run as root
 *
 * As root it makes <dir>/di-root (0644) and <dir>/mio, then drops to uid 1000
 * and tries what an ordinary user must not do:
 *   - write() and read() with a pointer into the kernel (0x100000): EFAULT;
 *   - write() and ftruncate() on a file opened O_RDONLY: EBADF / EINVAL;
 *   - read() on a file opened O_WRONLY: EBADF;
 *   - open(O_RDONLY | O_TRUNC) and truncate() of root's file: EACCES;
 *   - open(O_RDWR) of root's 0644 file: EACCES;
 * and what still must work: reading root's file, and its own file opened
 * O_RDWR, written, read back, truncated. Then (voci 47, 64, 13, 52/43, 10):
 *   - open(O_CREAT | O_EXCL) of a file that is there: EEXIST; of a new: ok;
 *   - "ESC[2147483600B" and "ESC[2147483600C" then a character: still alive;
 *   - irq_bind(1) as a user: EPERM; console_setfg of pid 1: EPERM/ESRCH;
 *   - five execv of missing files, then code far away still runs;
 *   - a child doing write(1, 0x50000000, 10) dies, the machine goes on (voce 5).
 * Prints "varchiprova: N NO".
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libc.h>

static int no = 0;

static void atteso(const char *cosa, long r, int err_voluto)
{
    int e = errno;

    if (err_voluto == 0) {
        if (r >= 0) printf("  [OK] %s\n", cosa);
        else { printf("  [NO] %s: -1 errno %d\n", cosa, e); no++; }
        return;
    }
    if (r < 0 && e == err_voluto) printf("  [OK] %s: errno %d\n", cosa, e);
    else { printf("  [NO] %s: reso %ld errno %d, voluto errno %d\n", cosa, r, e, err_voluto); no++; }
}

int main(int argc, char **argv)
{
    char root_f[256], mio_f[256], buf[64];
    int  fd;

    /* voce 5: the child that passes the kernel a pointer to nothing */
    if (argc > 1 && strcmp(argv[1], "-puntatore") == 0) {
        write(1, (char *)0x50000000, 10);
        printf("varchiprova: il figlio e' ancora vivo dopo il puntatore cattivo\n");
        return 0;
    }
    if (argc < 2 || getuid() != 0) { printf("uso: varchiprova DIR (da root)\n"); return 2; }
    snprintf(root_f, sizeof(root_f), "%s/di-root", argv[1]);
    snprintf(mio_f, sizeof(mio_f), "%s/mio", argv[1]);

    fd = open(root_f, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, "segreto di root\n", 16) != 16) { printf("varchiprova: non creo %s\n", root_f); return 2; }
    close(fd);
    chmod(root_f, 0644);
    chmod(argv[1], 0777);            /* so the user can create its own file */

    if (setuid(1000) != 0 || getuid() != 1000) { printf("varchiprova: setuid(1000) fallita\n"); return 2; }
    printf("varchiprova: ora uid %u\n", getuid());

    /* voce 4 */
    errno = 0; atteso("write dal kernel (0x100000)", write(1, (void *)0x100000, 16), EFAULT);
    fd = open(root_f, O_RDONLY);
    atteso("open O_RDONLY del file di root", fd, 0);
    errno = 0; atteso("read nel kernel (0x100000)", read(fd, (void *)0x100000, 16), EFAULT);

    /* voce 46 */
    errno = 0; atteso("write su O_RDONLY", write(fd, "x", 1), EBADF);
    errno = 0; atteso("ftruncate su O_RDONLY", ftruncate(fd, 0), EINVAL);
    errno = 0; atteso("read su O_RDONLY", read(fd, buf, sizeof(buf)), 0);
    close(fd);
    errno = 0; atteso("open O_RDONLY|O_TRUNC del file di root", open(root_f, O_RDONLY | O_TRUNC), EACCES);
    errno = 0; atteso("open O_RDWR del file di root", open(root_f, O_RDWR), EACCES);
    errno = 0; atteso("truncate del file di root", truncate(root_f, 0), EACCES);

    /* still working */
    fd = open(mio_f, O_RDWR | O_CREAT | O_TRUNC, 0644);
    atteso("open O_RDWR|O_CREAT del mio file", fd, 0);
    atteso("write sul mio file", write(fd, "ciao\n", 5), 0);
    atteso("pread sul mio file", pread(fd, buf, 5, 0), 0);
    atteso("ftruncate del mio file", ftruncate(fd, 2), 0);
    close(fd);
    fd = open(mio_f, O_WRONLY);
    errno = 0; atteso("read su O_WRONLY", read(fd, buf, 4), EBADF);
    errno = 0; atteso("pread su O_WRONLY", pread(fd, buf, 4, 0), EBADF);
    atteso("pwrite su O_WRONLY", pwrite(fd, "ok", 2, 0), 0);
    close(fd);
    {
        struct stat st;
        atteso("il file di root e' ancora intero", (stat(root_f, &st) == 0 && st.st_size == 16) ? 0 : -1, 0);
    }
    /* voce 47: O_EXCL (0x80) */
    errno = 0; atteso("O_CREAT|O_EXCL su un file che c'e'", open(mio_f, O_RDWR | O_CREAT | 0x80, 0644), EEXIST);
    {
        char nuovo[256];

        snprintf(nuovo, sizeof(nuovo), "%s/nuovo-excl", argv[1]);
        fd = open(nuovo, O_RDWR | O_CREAT | 0x80, 0644);
        atteso("O_CREAT|O_EXCL su un nome nuovo", fd, 0);
        if (fd >= 0) close(fd);
    }

    /* voce 64: the console parser */
    printf("\033[2147483600Bx\033[2147483600Cx\033[99999999999999999Ax\033[2147483647Lx\r\n");
    fflush(stdout);
    atteso("sequenze ANSI enormi: il kernel e' vivo", 0, 0);

    /* voce 13 and 52/43 */
    /* these two wrappers hand back the raw -errno, errno stays put */
    {
        int r = irq_bind(1);

        if (r == -EPERM) printf("  [OK] irq_bind(1) da utente: %d\n", r);
        else { printf("  [NO] irq_bind(1) da utente: reso %d, voluto %d\n", r, -EPERM); no++; }
    }
    {
        int r = console_setfg(1);
        int e = -r;

        if (r < 0 && (e == EPERM || e == ESRCH)) printf("  [OK] console_setfg(1) da utente: errno %d\n", e);
        else { printf("  [NO] console_setfg(1) da utente: reso %d errno %d\n", r, e); no++; }
        console_setfg((unsigned)getpid());
    }

    /* voce 10: failed execs keep the program whole */
    {
        int k;
        char *av[] = { "x", NULL };

        for (k = 0; k < 5; k++) execv("/non/esiste/proprio", av);
        atteso("5 execv fallite, il programma va avanti", 0, 0);
    }

    /* voce 5: a bad pointer kills the program, not the machine */
    {
        char *av[] = { argv[0], "-puntatore", NULL };
        int pid = spawn(argv[0], av), st = 0, f;

        if (pid > 0) waitpid(pid, &st, 0);
        f = open(mio_f, O_RDONLY);
        atteso("puntatore cattivo al kernel: muore il figlio, i file si aprono", (pid > 0 && st != 0 && f >= 0) ? 0 : -1, 0);
        if (f >= 0) close(f);
    }

    printf("varchiprova: %d NO\n", no);
    return no ? 1 : 0;
}
