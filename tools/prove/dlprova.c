/* =============================================================================
 * tools/prove/dlprova.c — dlopen and dlsym on EX-OS libraries (@DLOPEN)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Built with i386-exos-gcc against the static libc.a and run in QEMU by
 * tools/prova_dl.sh. A static program opens exzip.so by name, finds two
 * functions and CALLS them: ex_zip_apri on a file that does not exist makes
 * the library work for real, its bridges to the libc included, and
 * ex_zip_errore says why. Then the failures, each with its dlerror. Last
 * line: "dlprova: <n> NO".
 * ============================================================================= */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static int g_no = 0, g_n = 0;

static void verifica(int cond, const char *cosa)
{
    g_n++;
    printf("%s %d %s\n", cond ? "ok" : "NO", g_n, cosa);
    if (!cond) g_no++;
}

typedef void       *(*ZipApri)(const char *);
typedef const char *(*ZipErrore)(void);

int main(void)
{
    void       *h, *p;
    const char *e;
    ZipApri     apri;
    ZipErrore   errore;

    h = dlopen("exzip.so", RTLD_NOW);
    if (!h) printf("  dlerror: %s\n", dlerror());
    verifica(h != NULL, "dlopen(\"exzip.so\") per nome, cercandola");

    apri   = h ? (ZipApri)dlsym(h, "ex_zip_apri") : 0;
    errore = h ? (ZipErrore)dlsym(h, "ex_zip_errore") : 0;
    verifica(apri && errore, "dlsym trova ex_zip_apri ed ex_zip_errore");

    if (apri && errore) {
        p = apri("/non/c/e/prova.zip");
        e = errore();
        printf("  ex_zip_errore: %s\n", e ? e : "(null)");
        verifica(p == NULL && e && e[0], "la funzione trovata gira: file mancante, e il perche'");
    }

    verifica(dlsym(h, "non_esiste") == NULL, "dlsym di un nome che non c'e': NULL");
    e = dlerror();
    verifica(e && strstr(e, "undefined symbol"), "e dlerror lo dice");
    verifica(dlerror() == NULL, "dlerror si svuota dopo averlo letto");

    verifica(dlopen("nessuna.so", RTLD_LAZY) == NULL && dlerror() != NULL,
             "una libreria che non c'e': NULL e un perche'");
    verifica(dlopen("/boot/kernel.cfg", RTLD_LAZY) == NULL,
             "un file che non e' un ELF: NULL");

    p = dlopen(NULL, RTLD_NOW);
    verifica(p != NULL, "dlopen(NULL): il programma stesso");
    verifica(dlsym(p, "main") == NULL && dlerror() != NULL,
             "dlsym sul programma: niente, e lo dice (dichiarato)");
    verifica(dlclose(h) == 0, "dlclose");

    printf("dlprova: %d NO\n", g_no);
    return g_no ? 1 : 0;
}
