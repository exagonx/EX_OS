/* tools/prova-dl/libuno.c - a shared object for tools/prova-dl/prova-dl.c:
 * libc functions from the program, a dependency, a constructor, a function
 * pointer table, and a thread-local variable (general-dynamic model). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int contatore;                    /* in libdue.so */
int doppio(int x);

static int g_avviata;
static __thread int t_mio = 5;

__attribute__((constructor)) static void avvio(void) { g_avviata = 1234; }

static int piu_uno(int x) { return x + 1; }
int (*const tabella[2])(int) = { piu_uno, doppio };   /* R_X86_64_64 / RELATIVE */

int uno_avviata(void) { return g_avviata; }
int uno_somma(void) { return doppio(contatore) + tabella[0](1) + tabella[1](3); }   /* 80 + 2 + 6 */
char *uno_copia(const char *s)
{
    char *p = malloc(strlen(s) + 8);
    if (p) sprintf(p, "[%s]", s);
    return p;
}
int uno_tls(int d) { t_mio += d; return t_mio; }
