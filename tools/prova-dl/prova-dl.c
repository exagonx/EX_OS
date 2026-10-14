/* =============================================================================
 * tools/prova-dl/prova-dl.c — the ELF loader of lib/libc_dl.c, inside EX-OS
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Built and run by tools/prova-dl/prova-dl.sh. Opens libuno.so (which needs
 * libdue.so, found through LD_LIBRARY_PATH) and checks what a shared object
 * needs: libc calls into the program, a dependency, a constructor, function
 * pointers, data through the GOT, thread-local variables per thread, dladdr,
 * dlclose, and the errors dlerror reports.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

static int g_no;

static void ok(const char *cosa, int va)
{
    printf("  %s  %s\n", va ? "[OK]" : "[NO]", cosa);
    if (!va) g_no++;
}

static int (*g_tls)(int);

static void *filo(void *arg)
{
    int a = g_tls(10), b = g_tls(1);           /* its own copy, starting at 5 */
    *(int *)arg = (a == 15 && b == 16);
    return 0;
}

int main(int argc, char **argv)
{
    const char *dove = argc > 1 ? argv[1] : "/disk/libuno.so";
    void       *h = dlopen(dove, RTLD_NOW), *p;
    Dl_info     di;
    pthread_t   t;
    int         buono = 0;

    printf("prova-dl: dlopen of a real ELF shared object\n");
    ok(h ? "dlopen libuno.so (and its DT_NEEDED libdue.so)" : dlerror(), h != 0);
    if (!h) { printf("prova-dl: %d NO\n", g_no); return 1; }

    ok("the constructor ran (DT_INIT_ARRAY)",
       ((int (*)(void))dlsym(h, "uno_avviata"))() == 1234);
    ok("a dependency's function and data, a table of pointers (88)",
       ((int (*)(void))dlsym(h, "uno_somma"))() == 88);
    {
        char *s = ((char *(*)(const char *))dlsym(h, "uno_copia"))("ciao");
        ok("malloc and sprintf of the program, called from the library", s && strcmp(s, "[ciao]") == 0);
        free(s);                               /* the same heap: freed here */
    }
    ok("dlsym finds a symbol of the dependency through the handle", dlsym(h, "doppio") != 0);
    g_tls = (int (*)(int))dlsym(h, "uno_tls");
    ok("__thread in the library: 5 + 3 = 8", g_tls(3) == 8);
    pthread_create(&t, 0, filo, &buono);
    pthread_join(t, 0);
    ok("__thread: another thread has its own copy (15, 16)", buono);
    ok("__thread: this thread still has 8 + 1", g_tls(1) == 9);

    p = dlsym(h, "uno_copia");
    ok("dladdr names the library and the symbol",
       dladdr(p, &di) && di.dli_sname && strcmp(di.dli_sname, "uno_copia") == 0 &&
       strstr(di.dli_fname, "libuno.so") != 0);
    ok("dlsym(RTLD_DEFAULT, \"strlen\") is the libc's", dlsym(RTLD_DEFAULT, "strlen") == (void *)strlen);

    ok("a missing symbol: NULL and dlerror", dlsym(h, "non_esiste") == 0 && dlerror() != 0);
    {
        void *x = dlopen("/disk/non-c-e.so", RTLD_NOW);
        const char *e = dlerror();
        ok("a missing file: NULL and dlerror", x == 0 && e != 0);
        if (e) printf("        (%s)\n", e);
    }
    {
        void *h2 = dlopen(dove, RTLD_NOW);
        ok("a second dlopen gives the same handle", h2 == h);
        dlclose(h2);
    }
    dlclose(h);
    ok("dlclose", 1);
    printf("prova-dl: %d NO\n", g_no);
    return g_no != 0;
}
