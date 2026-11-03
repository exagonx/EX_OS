/* =============================================================================
 * tools/prova-dl/prova-avvio.c — a program LINKED to shared objects
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Linked with -luno (libuno.so, which needs libdue.so): no dlopen here, the
 * libc loads both before main (lib/libc_dl.c, __exos_dl_avvio_programma).
 * Prints "prova-avvio: N NO" at the end, 0 when everything held.
 * ============================================================================= */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

extern int contatore;                    /* defined in libdue.so: a COPY relocation here */
int  doppio(int x);
int  uno_avviata(void);
int  uno_somma(void);
char *uno_copia(const char *s);
int  uno_tls(int d);

static int no;

static void vedi(const char *cosa, long avuto, long atteso)
{
    printf("  %s %s: %ld (atteso %ld)\n", avuto == atteso ? "[OK]" : "[NO]", cosa, avuto, atteso);
    if (avuto != atteso) no++;
}

static int g_visto_dal_costruttore = -1;
__attribute__((constructor)) static void mio(void) { g_visto_dal_costruttore = uno_avviata(); }

static void *filo(void *a)
{
    (void)a;
    return (void *)(long)uno_tls(100);   /* a fresh block in this thread: 5 + 100 */
}

int main(void)
{
    pthread_t t;
    void     *r = 0;
    char     *s;

    vedi("costruttore di libuno prima del programma", g_visto_dal_costruttore, 1234);
    vedi("contatore copiato da libdue", contatore, 40);
    vedi("doppio, chiamata nella PLT", doppio(21), 42);
    vedi("uno_somma", uno_somma(), 88);
    contatore = 50;                      /* the libraries must see the program's copy */
    vedi("uno_somma dopo contatore = 50", uno_somma(), 108);
    s = uno_copia("ciao");
    vedi("uno_copia usa malloc e sprintf del programma", s && strcmp(s, "[ciao]") == 0, 1);
    free(s);
    vedi("__thread nella libreria", uno_tls(1), 6);
    pthread_create(&t, 0, filo, 0);
    pthread_join(t, &r);
    vedi("__thread in un altro filo", (long)r, 105);
    vedi("__thread di nuovo nel primo", uno_tls(1), 7);
    printf("prova-avvio: %d NO\n", no);
    return no != 0;
}
