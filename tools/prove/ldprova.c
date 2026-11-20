/* tools/prove/ldprova.c - the shared libc, /lib/ld-exos.so (voce 105).
 * Linked without -static: the kernel starts the interpreter, which binds the
 * program to the libc. Each line OK or NO, then "ldprova: N/M". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>
#include <getopt.h>
#include <spawn.h>
#include <sys/wait.h>
#include <libc.h>          /* meminfo */

extern char **environ;

static int ok, tot;
static int costruito;
static void prova(const char *cosa, int buono)
{
    tot++; if (buono) ok++;
    printf("%s %s\n", buono ? "[OK]" : "[NO]", cosa);
}

__attribute__((constructor)) static void prima(void) { costruito = 1; }
static void alla_fine(void) { printf("ldprova: atexit ok\n"); }


/* (kernel 0.351) N copies of this program asleep: what each one costs. With
 * the libc's pages shared it is the stack, the tables and the writable
 * pages; with a copy per process it was the whole ld-exos.so on top. */
/* KB each of N copies of `chi` asleep cost, -1 if they did not start */
static long costo(const char *chi)
{
    enum { N = 6 };
    MemInfo a, b;
    pid_t   f[N];
    char   *av[] = { (char *)chi, "dorme", 0 };
    int     i, nati = 0;

    if (meminfo(&a) < 0) return -1;
    for (i = 0; i < N; i++)
        if (posix_spawn(&f[nati], chi, 0, 0, av, environ) == 0) nati++;
    sleep(3);
    if (meminfo(&b) < 0) nati = 0;
    for (i = 0; i < nati; i++) waitpid(f[i], 0, 0);
    return nati == N ? ((long)a.free_kb - (long)b.free_kb) / N : -1;
}

/* The same program linked -static (ldprova-statico, next to this one) is the
 * yardstick: stack, tables, its own pages. The shared one may cost more by
 * its writable pages (GOT, .data, what .bss it touches), not by the code. */
static void memoria(const char *me)
{
    struct stat st;
    char        statico[256];
    long        din, sta;

    snprintf(statico, sizeof(statico), "%s-statico", me);
    if (stat("/lib/ld-exos.so", &st) != 0 || access(statico, X_OK) != 0) { prova("ld-exos.so e ldprova-statico", 0); return; }
    din = costo(me);
    sta = costo(statico);
    printf("ldprova: 6 processi addormentati: %ld KB l'uno sulla libc condivisa, %ld KB statici; ld-exos.so e' %ld KB\n",
           din, sta, (long)(st.st_size / 1024));
    prova("sei copie, condivise e statiche", din >= 0 && sta >= 0);
    prova("il codice della libc non si ricopia in ogni processo (meno di 64 KB piu' dello statico)",
          din >= 0 && sta >= 0 && din - sta < 64);
}

int main(int argc, char **argv)
{
    char *argomenti[] = { "ldprova", "-x", "valore", 0 };
    int   c;
    void *h, *p;
    Dl_info di;

    if (argc > 1 && strcmp(argv[1], "dorme") == 0) { sleep(6); return 0; }
    prova("costruttore del programma", costruito == 1);
    prova("environ (copiato nel programma)", getenv("PATH") != 0);
    errno = 0;
    prova("errno", close(-1) == -1 && errno == EBADF);
    prova("stdout e printf", fprintf(stdout, "%s", "") == 0);
    optind = 1;
    c = getopt(3, argomenti, "x:");
    prova("getopt e optarg (COPY)", c == 'x' && optarg && strcmp(optarg, "valore") == 0 && optind == 3);
    p = malloc(100000);
    prova("malloc", p != 0);
    free(p);
    h = dlopen(0, RTLD_NOW);
    /* (void *)printf in a non-PIC program is its PLT slot, and as on Linux
     * that is the function's address for everybody: dlsym too */
    prova("dlsym(RTLD_DEFAULT, \"printf\") == &printf (il PLT del programma)", dlsym(RTLD_DEFAULT, "printf") == (void *)printf && dlsym(h, "printf") == (void *)printf);
    /* RTLD_NEXT from the program skips it: the libc's own printf */
    prova("dlsym(RTLD_NEXT, \"printf\") dal programma: la libc, e dladdr lo dice",
          dlsym(RTLD_NEXT, "printf") != 0 && dlsym(RTLD_NEXT, "printf") != (void *)printf &&
          dladdr(dlsym(RTLD_NEXT, "printf"), &di) && di.dli_fname && strstr(di.dli_fname, "ld-exos.so"));
    prova("dlsym(RTLD_NEXT) di cio' che non c'e'", dlsym(RTLD_NEXT, "non_esiste_davvero") == 0);
    memoria(argv[0]);
    atexit(alla_fine);
    printf("ldprova: %d/%d %s\n", ok, tot, argc > 0 ? argv[0] : "?");
    return ok == tot ? 0 : 1;
}
