/* =============================================================================
 * tools/prove/discovel.c — how fast files are written and read (@DISCO-VELOCE)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     discovel FILE MB PEZZO      writes MB megabytes in write()s of PEZZO
 *                                 bytes, then reads them back in read()s of
 *                                 PEZZO bytes and checks every byte; then
 *                                 rewrites one piece in seven in place and
 *                                 checks the whole file again (a read-ahead
 *                                 that served the old bytes would show)
 *     discovel -l FILE PEZZO      only reads FILE, in read()s of PEZZO bytes
 *
 * Prints one line per phase, "DISCOVEL <fase> <byte> <ms> <KB/s>", for
 * tools/prova_disco_veloce.sh to collect. Built with the EX-OS cross gcc
 * (static), put on the test disk with debugfs.
 * ============================================================================= */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static unsigned long ms(void)
{
    struct timespec t;

    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long)t.tv_sec * 1000ul + (unsigned long)t.tv_nsec / 1000000ul;
}

static void riga(const char *fase, unsigned long byte, unsigned long t)
{
    if (t == 0) t = 1;
    printf("DISCOVEL %s %lu %lu %lu\n", fase, byte, t, (byte / 1024ul) * 1000ul / t);
}

/* The byte at position i: not compressible by anything on the way, and a
 * block read from the wrong place does not match by chance. */
static unsigned long g_pezzo = 0;          /* > 0: one piece in seven rewritten */

static unsigned char atteso(unsigned long i)
{
    unsigned long x = i * 2654435761ul + (i >> 9);
    unsigned char c = (unsigned char)(x ^ (x >> 13));

    if (g_pezzo && (i / g_pezzo) % 7ul == 3ul) c ^= 0x5Au;
    return c;
}

static int leggi_tutto(const char *f, unsigned long pezzo, int controlla)
{
    unsigned char *b = malloc(pezzo);
    unsigned long  tot = 0, t0, k, sbagli = 0;
    int            fd = open(f, O_RDONLY), n;

    if (fd < 0 || b == NULL) { printf("discovel: non apro %s\n", f); return 1; }
    t0 = ms();
    while ((n = (int)read(fd, b, pezzo)) > 0) {
        if (controlla)
            for (k = 0; k < (unsigned long)n; k++)
                if (b[k] != atteso(tot + k) && sbagli++ < 5)
                    printf("discovel: byte %lu: %u invece di %u\n", tot + k, b[k], atteso(tot + k));
        tot += (unsigned long)n;
    }
    riga("lettura", tot, ms() - t0);
    close(fd);
    free(b);
    if (sbagli) { printf("DISCOVEL ERRORI %lu\n", sbagli); return 1; }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned long mb, pezzo, tot, t0, k;
    unsigned char *b;
    int fd;

    if (argc == 4 && strcmp(argv[1], "-l") == 0)
        return leggi_tutto(argv[2], strtoul(argv[3], NULL, 10), 0);
    if (argc != 4) {
        printf("uso: discovel FILE MB PEZZO | discovel -l FILE PEZZO\n");
        return 2;
    }
    mb = strtoul(argv[2], NULL, 10);
    pezzo = strtoul(argv[3], NULL, 10);
    b = malloc(pezzo);
    if (b == NULL || pezzo == 0) return 1;

    unlink(argv[1]);
    fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { printf("discovel: non creo %s\n", argv[1]); return 1; }
    t0 = ms();
    for (tot = 0; tot < mb * 1048576ul; tot += pezzo) {
        for (k = 0; k < pezzo; k++) b[k] = atteso(tot + k);
        if (write(fd, b, pezzo) != (long)pezzo) { printf("discovel: scrittura fallita a %lu\n", tot); return 1; }
    }
    close(fd);
    riga("scrittura", tot, ms() - t0);
    if (leggi_tutto(argv[1], pezzo, 1) != 0) return 1;

    /* In place, without truncating: the sectors under the read-ahead window
     * change, and the next read must see the new ones. */
    fd = open(argv[1], O_WRONLY);
    if (fd < 0) { printf("discovel: non riapro %s\n", argv[1]); return 1; }
    g_pezzo = pezzo;
    t0 = ms();
    for (tot = 3ul * pezzo; tot < mb * 1048576ul; tot += 7ul * pezzo) {
        for (k = 0; k < pezzo; k++) b[k] = atteso(tot + k);
        if (lseek(fd, (off_t)tot, SEEK_SET) != (off_t)tot ||
            write(fd, b, pezzo) != (long)pezzo) {
            printf("discovel: riscrittura fallita a %lu\n", tot);
            return 1;
        }
    }
    close(fd);
    riga("riscrittura", mb * 1048576ul / 7ul, ms() - t0);
    free(b);
    return leggi_tutto(argv[1], pezzo, 1);
}
