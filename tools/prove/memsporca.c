/* tools/prove/memsporca.c - does anybody write into memory that is ours?
 *
 *     memsporca MB SECONDI
 *
 * Takes MB of zeros, then looks at them once a second. Nothing here writes
 * after the first pass, so whatever appears came from outside the program: a
 * card still doing DMA into pages its dead driver gave back (voce 63). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    unsigned long mb = argc > 1 ? strtoul(argv[1], 0, 10) : 64, tot, i;
    int sec = argc > 2 ? atoi(argv[2]) : 20, s;
    unsigned char *p;
    unsigned long trovati = 0;

    tot = mb * 1024 * 1024;
    p = malloc(tot);
    if (p == NULL) { printf("memsporca: niente memoria\n"); return 2; }
    memset(p, 0, tot);
    printf("memsporca: %lu MB a zero\n", mb);
    fflush(stdout);
    for (s = 0; s < sec; s++) {
        unsigned long qui = 0, primo = 0;
        sleep(1);
        for (i = 0; i < tot; i++)
            if (p[i]) { if (!qui) primo = i; qui++; }
        if (qui) {
            printf("secondo %d: %lu byte non a zero, primo a %lu:", s, qui, primo);
            for (i = 0; i < 8 && primo + i < tot; i++) printf(" %02x", p[primo + i]);
            printf("\n");
            fflush(stdout);
            memset(p, 0, tot);
            trovati += qui;
        }
    }
    printf("memsporca: %lu byte estranei\n", trovati);
    return trovati != 0;
}
