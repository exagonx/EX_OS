/* =============================================================================
 * tools/prove/casoprova.c - the random numbers of the kernel (analisi voce 67)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     casoprova <file>
 *
 * Two reads of 32 bytes in a row (printed: they must have nothing in
 * common), one read of 1000 bytes in one call (before 0.331 the kernel
 * stopped at 32), then 1 MB with getrandom into <file>, for the statistics
 * on Linux (tools/prova_caso.sh). Run on a CPU without RDRAND.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <libc.h>

static unsigned char buf[65536];

int main(int argc, char **argv)
{
    unsigned char a[32], b[32];
    int  fd, k, uguali = 0;
    long tot = 0;
    ssize_t n;

    if (argc < 2) { printf("uso: casoprova FILE\n"); return 2; }
    if (getrandom(a, 32, 0) != 32 || getrandom(b, 32, 0) != 32) { printf("CASO getrandom fallita\n"); return 1; }
    printf("CASO a ");
    for (k = 0; k < 32; k++) printf("%02x", a[k]);
    printf("\nCASO b ");
    for (k = 0; k < 32; k++) { printf("%02x", b[k]); if (a[k] == b[k]) uguali++; }
    printf("\nCASO byte uguali al loro posto: %d\n", uguali);
    n = getrandom(buf, 1000, 0);
    printf("CASO una chiamata da 1000: %ld\n", (long)n);

    fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { printf("CASO non apro %s\n", argv[1]); return 1; }
    while (tot < 1048576) {
        n = getrandom(buf, sizeof(buf), 0);
        if (n <= 0) { printf("CASO getrandom: %ld\n", (long)n); break; }
        if (write(fd, buf, (size_t)n) != n) { printf("CASO write fallita\n"); break; }
        tot += n;
    }
    close(fd);
    sync();
    printf("CASO FINE %ld\n", tot);
    return 0;
}
