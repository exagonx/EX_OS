/* tools/prove/scalzacache.c - pushes every sector out of the kernel's cache.
 *
 *     scalzacache DEV [SETTORI]
 *
 * Reads only: one sector at a time through blkread, from far into DEV, twice the
 * size of the cache of kernel/block/blk.c (2048 sectors). Voce 63: chkdsk right
 * after an unmount disagrees with the next one; if this before it makes the
 * first one clean, the old copies were in that cache. */
#include <stdio.h>
#include <stdlib.h>

extern int blkread(const char *dev, unsigned int lba, unsigned int n, void *buf);

int main(int argc, char **argv)
{
    static unsigned char s[512];
    unsigned k, n = argc > 2 ? (unsigned)strtoul(argv[2], 0, 10) : 4096u;

    if (argc < 2) { printf("uso: scalzacache DEV [SETTORI]\n"); return 2; }
    for (k = 0; k < n; k++)
        if (blkread(argv[1], 16000000u + k, 1, s) < 0) {
            printf("lettura fallita a %u\n", k);
            return 1;
        }
    printf("letti %u settori\n", n);
    return 0;
}
