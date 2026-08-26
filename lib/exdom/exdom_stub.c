/* =============================================================================
 * lib/exdom/exdom_stub.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Lo stub di ExDom. Stessa forma di quello di exhtml e di quello di exjs.
 *
 * ! E' CORTO PERCHE' IL DOM NON PASSA DI QUI. Otto ponti, e sette li usera' il
 * browser una volta sola per pagina: aprire, chiedere il documento, far
 * partire un evento. Tutto il resto — document, gli elementi, gli attributi —
 * vive dentro il motore JavaScript, dove exdom_apri() lo ha appeso all'oggetto
 * globale. Uno stub lungo qui sarebbe stato il segno che il ponte era stato
 * disegnato male.
 * ============================================================================= */

#include "exdom.h"
#include "exlib.h"

#define SYS_WRITE   4
#define SYS_EXIT    1

static void grida_e_muori(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    __asm__ volatile ("int $0x80" :: "a"(SYS_WRITE), "b"(2), "c"(s), "d"(n) : "memory");
    __asm__ volatile ("int $0x80" :: "a"(SYS_EXIT), "b"(1) : "memory");
    for (;;) { }
}

static const char *const g_dove[] = {
    "/exwin/lib/exdom.so",
    "/cdrom/exwin/lib/exdom.so"
};

static struct {
    int pronto;
    unsigned int (*quanto_serve)(unsigned int, unsigned int, unsigned int);
    ExDom *(*apri)(void *, unsigned int, ExJsCtx *, HtmlDoc *,
                   unsigned int, unsigned int, unsigned int);
    ExJsVal (*avvolgi)(ExDom *, int);
    int (*nodo)(ExDom *, ExJsVal);
    ExJsVal (*documento)(ExDom *);
    int (*evento)(ExDom *, int, const char *, ExJsErrore *);
    int (*perso)(const ExDom *);
    int (*troncato)(const ExDom *);
} P;

static void *chiedi(const ExLibTesta *t, const char *nome)
{
    void *p = exlib_simbolo(t, nome);

    if (p == 0)
        grida_e_muori("exdom: la libreria condivisa non esporta un nome che "
                      "serve a questo programma\n");
    return p;
}

static void assicura(void)
{
    const ExLibTesta *t;

    if (P.pronto) return;

    t = exlib_apri_fra(g_dove, (int)(sizeof g_dove / sizeof g_dove[0]));
    if (t == 0)
        grida_e_muori("exdom: non trovo la libreria condivisa del DOM.\n"
                      "       Cercata in /exwin/lib/exdom.so e "
                      "/cdrom/exwin/lib/exdom.so\n");

    P.quanto_serve = (unsigned int (*)(unsigned int, unsigned int, unsigned int))
                     chiedi(t, "exdom_quanto_serve");
    P.apri         = (ExDom *(*)(void *, unsigned int, ExJsCtx *, HtmlDoc *,
                                 unsigned int, unsigned int, unsigned int))
                     chiedi(t, "exdom_apri");
    P.avvolgi      = (ExJsVal (*)(ExDom *, int))chiedi(t, "exdom_avvolgi");
    P.nodo         = (int (*)(ExDom *, ExJsVal))chiedi(t, "exdom_nodo");
    P.documento    = (ExJsVal (*)(ExDom *))chiedi(t, "exdom_documento");
    P.evento       = (int (*)(ExDom *, int, const char *, ExJsErrore *))
                     chiedi(t, "exdom_evento");
    P.perso        = (int (*)(const ExDom *))chiedi(t, "exdom_perso");
    P.troncato     = (int (*)(const ExDom *))chiedi(t, "exdom_troncato");

    P.pronto = 1;
}

unsigned int exdom_quanto_serve(unsigned int nodi_max, unsigned int testo_max,
                                unsigned int ascolti_max)
{ assicura(); return P.quanto_serve(nodi_max, testo_max, ascolti_max); }

ExDom *exdom_apri(void *memoria, unsigned int byte,
                  ExJsCtx *js, HtmlDoc *doc,
                  unsigned int nodi_max, unsigned int testo_max,
                  unsigned int ascolti_max)
{ assicura(); return P.apri(memoria, byte, js, doc, nodi_max, testo_max,
                            ascolti_max); }

ExJsVal exdom_avvolgi(ExDom *D, int nodo)
{ assicura(); return P.avvolgi(D, nodo); }

int exdom_nodo(ExDom *D, ExJsVal v)
{ assicura(); return P.nodo(D, v); }

ExJsVal exdom_documento(ExDom *D)
{ assicura(); return P.documento(D); }

int exdom_evento(ExDom *D, int nodo, const char *tipo, ExJsErrore *err)
{ assicura(); return P.evento(D, nodo, tipo, err); }

int exdom_perso(const ExDom *D)
{ assicura(); return P.perso(D); }

int exdom_troncato(const ExDom *D)
{ assicura(); return P.troncato(D); }
