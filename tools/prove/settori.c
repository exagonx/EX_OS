/* tools/prove/settori.c - what is really in N sectors of a partition.
 *     settori DEV LBA N [TESTO...]
 * Reads only. Prints the first 32 bytes and how many times each TESTO occurs. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int blkread(const char *dev, unsigned int lba, unsigned int n, void *buf);
int main(int argc, char **argv)
{
    unsigned lba, n, i, k; unsigned char *b; int a;
    if (argc < 4) { printf("uso: settori DEV LBA N [TESTO...]\n"); return 2; }
    lba = strtoul(argv[2], 0, 10); n = strtoul(argv[3], 0, 10);
    b = malloc(n * 512 + 1);
    for (k = 0; k < n; k += 32) {
        unsigned q = n - k < 32 ? n - k : 32;
        if (blkread(argv[1], lba + k, q, b + k * 512) < 0) { printf("lettura fallita a %u\n", lba + k); return 1; }
    }
    for (i = 0; i < 32; i++) printf("%02x ", b[i]);
    printf("\n");
    for (a = 4; a < argc; a++) {
        unsigned l = strlen(argv[a]), c = 0, p;
        for (p = 0; p + l <= n * 512; p++) if (!memcmp(b + p, argv[a], l)) c++;
        printf("%s: %u volte\n", argv[a], c);
    }
    return 0;
}
