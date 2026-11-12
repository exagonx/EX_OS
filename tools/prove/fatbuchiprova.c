/* =============================================================================
 * tools/prove/fatbuchiprova.c - the holes of a FAT file are zeros (voce 71)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     fatbuchiprova <dir on a FAT volume whose free space is full of 'X'>
 *
 * Three files: one byte at 3000 (hole inside the first cluster), one byte at
 * 20000 (holes across clusters), "ab" at 0 then one byte at 5000 (the tail of
 * a cluster written in part). Everything that was not written must read 0.
 * Prints "FATBUCHI FINE N" with N the cases gone wrong.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

static unsigned char buf[32768];

static int caso(const char *dir, const char *nome, long primo_off, const char *primo,
                long off, int fine)
{
    char p[256];
    int  fd, k, sporchi = 0;
    long n, tot;

    snprintf(p, sizeof(p), "%s/%s", dir, nome);
    fd = open(p, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { printf("FATBUCHI %s: open fallita\n", nome); return 1; }
    if (primo != NULL) {
        lseek(fd, primo_off, SEEK_SET);
        write(fd, primo, strlen(primo));
    }
    lseek(fd, off, SEEK_SET);
    if (write(fd, "Z", 1) != 1) { printf("FATBUCHI %s: write fallita\n", nome); close(fd); return 1; }
    close(fd);

    fd = open(p, O_RDONLY);
    tot = off + 1;
    n = read(fd, buf, sizeof(buf));
    close(fd);
    if (n != tot) { printf("FATBUCHI %s: letti %ld di %ld\n", nome, n, tot); return 1; }
    for (k = 0; k < off; k++) {
        int scritto = primo != NULL && k >= primo_off && k < primo_off + (long)strlen(primo);

        if (!scritto && buf[k] != 0) sporchi++;
    }
    if (buf[off] != 'Z') sporchi++;
    (void)fine;
    printf("FATBUCHI %s: %d byte non zero nei buchi\n", nome, sporchi);
    return sporchi ? 1 : 0;
}

int main(int argc, char **argv)
{
    int no = 0;

    if (argc < 2) { printf("uso: fatbuchiprova DIR\n"); return 2; }
    no += caso(argv[1], "uno.bin", 0, NULL, 3000, 0);
    no += caso(argv[1], "due.bin", 0, NULL, 20000, 0);
    no += caso(argv[1], "tre.bin", 0, "ab", 5000, 0);
    printf("FATBUCHI FINE %d\n", no);
    return no ? 1 : 0;
}
