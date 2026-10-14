/* =============================================================================
 * bin/sha256sum/sha256sum.c — print the SHA-256 of files
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     sha256sum FILE...
 *
 * One line per file, in the format of GNU coreutils ("hash  name"), so the
 * output can be compared with what a Linux host prints for the same file.
 * That is what it was written for (2026-10-14): checking that a file read
 * by EX-OS from an ext4 volume made on Linux is byte for byte the same.
 *
 * It reads in 64 KB pieces and hashes as it goes; the file is never held
 * in memory whole. The hashing is the libc's (sha256_avvia / _dai / _fine).
 * ============================================================================= */
#include "libc.h"

EX_VERSIONE("sha256sum", "0.001");

static unsigned char g_buf[65536];

int main(int argc, char **argv)
{
    int i, errors = 0;

    if (argc < 2) {
        printf("usage: sha256sum FILE...\n");
        return 2;
    }

    for (i = 1; i < argc; i++) {
        Sha256 s;
        char   hex[65];
        int    fd = open(argv[i], 0);
        long   n;

        if (fd < 0) {
            printf("sha256sum: %s: cannot open\n", argv[i]);
            errors++;
            continue;
        }
        sha256_avvia(&s);
        while ((n = read(fd, g_buf, sizeof(g_buf))) > 0) sha256_dai(&s, g_buf, (size_t)n);
        close(fd);
        if (n < 0) {
            printf("sha256sum: %s: read error\n", argv[i]);
            errors++;
            continue;
        }
        sha256_fine_esa(&s, hex);
        printf("%s  %s\n", hex, argv[i]);
    }
    return errors ? 1 : 0;
}
