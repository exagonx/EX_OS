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
 * O_RDWR, written, read back, truncated. Prints "varchiprova: N NO".
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

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
    printf("varchiprova: %d NO\n", no);
    return no ? 1 : 0;
}
