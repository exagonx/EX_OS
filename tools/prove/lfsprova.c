/* =============================================================================
 * tools/prove/lfsprova.c — _FILE_OFFSET_BITS=64 in EX-OS
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Built by tools/prova_lfs.sh with -D_FILE_OFFSET_BITS=64, at 32 and 64 bit,
 * against the static libc.a. Every check prints "LFS ok ..." or "LFS NO ...";
 * the last line is "LFS fine <n>", n = how many NO.
 *
 * At 32 bit the define gives a 64-bit off_t and lseek, pread, pwrite,
 * ftruncate, fseeko, ftello, mmap, stat, fstat become the *64 functions of
 * the libc (MariaDB is built so). The kernel holds 2 GB per file: past it
 * the libc refuses (EOVERFLOW, EFBIG, end of file), it never cuts.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

static int g_no = 0;

static void verifica(int cond, const char *cosa)
{
    printf("LFS %s %s\n", cond ? "ok" : "NO", cosa);
    if (!cond) g_no++;
}

int main(int argc, char **argv)
{
    const char *f = argc > 1 ? argv[1] : "/a/lfs.dat";
    char        buf[8192], r[16];
    struct stat st;
    FILE       *fp;
    char       *m;
    int         fd, i;
    off_t       tre_giga = (off_t)3 << 30;

    verifica(sizeof(off_t) == 8, "off_t a 64 bit");
    for (i = 0; i < (int)sizeof buf; i++) buf[i] = (char)('A' + i % 26);
    fd = open(f, O_RDWR | O_CREAT | O_TRUNC, 0644);
    verifica(fd >= 0, "open");
    verifica(write(fd, buf, sizeof buf) == (ssize_t)sizeof buf, "write 8192");
    verifica(fstat(fd, &st) == 0 && st.st_size == 8192, "fstat: st_size 8192");
    verifica(stat(f, &st) == 0 && st.st_size == 8192 && S_ISREG(st.st_mode), "stat: st_size 8192, file");
    verifica(lseek(fd, 0, SEEK_END) == 8192, "lseek SEEK_END 8192");
    verifica(lseek(fd, -192, SEEK_CUR) == 8000, "lseek SEEK_CUR -192");
    memset(r, 0, sizeof r);
    verifica(pread(fd, r, 4, 4096) == 4 && memcmp(r, buf + 4096, 4) == 0, "pread a 4096");
    verifica(lseek(fd, 0, SEEK_CUR) == 8000, "pread non sposta la posizione");
    verifica(pwrite(fd, "zzzz", 4, 100) == 4 && pread(fd, r, 4, 100) == 4 && memcmp(r, "zzzz", 4) == 0,
             "pwrite a 100");
    verifica(ftruncate(fd, 5000) == 0 && fstat(fd, &st) == 0 && st.st_size == 5000, "ftruncate 5000");
    m = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 4096);
    verifica(m != MAP_FAILED && m[0] == buf[4096] && m[903] == buf[4999], "mmap dall'offset 4096");
    if (m != MAP_FAILED) munmap(m, 4096);
    /* past the kernel's 2 GB: refused, not cut down to a small position */
    errno = 0;
    verifica(lseek(fd, tre_giga, SEEK_SET) == (off_t)-1 && errno == EOVERFLOW, "lseek a 3 GB: EOVERFLOW");
    verifica(lseek(fd, 0, SEEK_CUR) == 8000, "la posizione resta dov'era");
    errno = 0;
    verifica(pwrite(fd, "x", 1, tre_giga) == -1 && errno == EFBIG, "pwrite a 3 GB: EFBIG");
    verifica(pread(fd, r, 1, tre_giga) == 0, "pread a 3 GB: fine del file");
    errno = 0;
    verifica(ftruncate(fd, tre_giga) == -1 && errno == EFBIG, "ftruncate a 3 GB: EFBIG");
    verifica(fstat(fd, &st) == 0 && st.st_size == 5000, "il file e' rimasto di 5000");
    close(fd);
    fp = fopen(f, "rb");
    verifica(fp && fseeko(fp, 4000, SEEK_SET) == 0 && ftello(fp) == 4000, "fseeko/ftello 4000");
    verifica(fp && fread(r, 1, 4, fp) == 4 && memcmp(r, buf + 4000, 4) == 0, "fread dopo fseeko");
    if (fp) fclose(fp);
    verifica(unlink(f) == 0, "unlink");
    printf("LFS fine %d\n", g_no);
    return g_no ? 1 : 0;
}
