/* =============================================================================
 * tools/prove/pilaprova.c - the callers the kernel writes at a page fault
 * (kernel 0.357)
 *
 *     pilaprova          falls on purpose: a read at 0x28, like Firefox
 *
 * Five functions call each other, each with a 6 KB frame (Firefox's are big:
 * 16 words of 512 gave one caller of nsDocShellLoadState::Serialize on
 * 23/11/2026), and the last reads a NULL pointer + 0x28. The kernel writes
 * "PF:   sulla pila N:" lines; `addr2line -f -e pilaprova <addresses>` must
 * give giro4, giro3, giro2, giro1 and main (giro5 is the EIP of the fault).
 * ============================================================================= */
#include <stdio.h>
#include <string.h>

/* volatile all the way: with a plain NULL->field gcc -O2 dropped the read
 * (its value fed nothing) and the program just ended. */
static volatile long *volatile g_nessuno;

#define GIRO(n, prossimo)                                       \
    __attribute__((noinline, noclone)) int giro##n(int x)       \
    {                                                           \
        volatile char pezzo[6000];                              \
        int r;                                                  \
        memset((char *)pezzo, n, sizeof(pezzo));                \
        r = prossimo + pezzo[x % 6000];                         \
        return r + pezzo[(x * 7) % 6000];                       \
    }

__attribute__((noinline, noclone)) int giro5(int x)
{
    volatile char pezzo[6000];

    memset((char *)pezzo, 5, sizeof(pezzo));
    return pezzo[x % 6000] + (int)g_nessuno[0x28 / sizeof(long)];
}
GIRO(4, giro5(x + 1))
GIRO(3, giro4(x + 1))
GIRO(2, giro3(x + 1))
GIRO(1, giro2(x + 1))

int main(void)
{
    printf("pilaprova: cado a 0x28 dopo cinque chiamate\n");
    fflush(stdout);
    printf("pilaprova: non sono caduto (%d)\n", giro1(1));
    return 1;
}
