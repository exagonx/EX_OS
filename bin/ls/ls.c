/* bin/ls/ls.c — EX-OS
 * Elenca il contenuto di una directory.
 * Usa main(int argc, char **argv) con il trampoline _start della libc:
 *   ls           → elenca la directory corrente (getcwd)
 *   ls /bin      → elenca /bin
 *   ls /         → elenca la root
 */
#include "libc.h"

/* Voci lette per chiamata. Non è il numero massimo di voci mostrabili:
 * la directory viene percorsa a blocchi finché non finisce. */
#define BLOCCO 32

int main(int argc, char **argv)
{
    char     cwd[256];
    DirEntry entries[BLOCCO];
    int      n, i;
    int      start = 0;
    int      totale = 0;
    const char *target;

    if (argc >= 2 && argv[1] && argv[1][0] != '\0') {
        target = argv[1];
    } else {
        if (!getcwd(cwd, sizeof(cwd))) {
            cwd[0] = '/'; cwd[1] = '\0';
        }
        target = cwd;
    }

    /* TRONCAMENTO SILENZIOSO CORRETTO (luglio 2026): prima c'era una sola
     * listdir() con buffer da 64 voci, e il kernel ne restituiva comunque
     * al massimo 64 (READDIR_MAX_BATCH). Una directory più grande veniva
     * mostrata INCOMPLETA senza il minimo avviso: sembrava semplicemente
     * che quei file non esistessero. Ora si pagina con listdir_from()
     * finché la directory non è finita. */
    for (;;) {
        n = listdir_from(target, entries, BLOCCO, start);

        if (n < 0) {
            printf("ls: impossibile leggere '%s'\n", target);
            return 1;
        }
        if (n == 0) break;

        for (i = 0; i < n; i++) {
            if (entries[i].is_dir)
                printf("%s/\n", entries[i].name);
            else
                printf("%-12s %u\n", entries[i].name, entries[i].size);
        }

        totale += n;
        if (n < BLOCCO) break;      /* ultima pagina */
        start += n;
    }

    if (totale == 0) {
        printf("(vuota)\n");
    }

    return 0;
}
