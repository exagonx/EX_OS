/* =============================================================================
 * lib/exzip/exzip_stub.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The stub of ExZip. Same shape as lib/exdlg/exdlg_stub.c, and the likeness is
 * on purpose: a new library is added by copying this pattern, and whoever
 * reads it the second time has nothing new to understand.
 *
 * ! IT IS LINKED, NOT OPENED BY HAND: a program that handles archives knows it
 * does at compile time, which is what tells a stub apart from eximg - that one
 * is opened by ex_immagine() when it finds itself in front of a file's bytes.
 * ============================================================================= */

#include "exzip.h"
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
    "/exwin/lib/exzip.so",
    "/cdrom/exwin/lib/exzip.so"
};

static struct {
    int pronto;
    ExZip *(*apri)(const char *);
    unsigned int (*quante)(ExZip *);
    int (*voce)(ExZip *, unsigned int, ExZipVoce *);
    int (*estrai)(ExZip *, unsigned int, const char *);
    ExZip *(*crea)(const char *);
    int (*aggiungi)(ExZip *, const char *, const char *);
    int (*finisci)(ExZip *);
    void (*chiudi)(ExZip *);
    const char *(*errore)(void);
    int (*albero)(ExZip *, const char *, const char *, int *);
} P;

static void *chiedi(const ExLibTesta *t, const char *nome)
{
    void *p = exlib_simbolo(t, nome);
    if (p == 0)
        grida_e_muori("exzip: la libreria condivisa non esporta un nome che "
                      "serve a questo programma\n");
    return p;
}

static void assicura(void)
{
    const ExLibTesta *t;

    if (P.pronto) return;

    t = exlib_apri_fra(g_dove, (int)(sizeof g_dove / sizeof g_dove[0]));
    if (t == 0)
        grida_e_muori("exzip: non trovo la libreria condivisa degli archivi.\n"
                      "       Cercata in /exwin/lib/exzip.so e "
                      "/cdrom/exwin/lib/exzip.so\n");

    P.apri     = (ExZip *(*)(const char *))                      chiedi(t, "ex_zip_apri");
    P.quante   = (unsigned int (*)(ExZip *))                     chiedi(t, "ex_zip_quante");
    P.voce     = (int (*)(ExZip *, unsigned int, ExZipVoce *))   chiedi(t, "ex_zip_voce");
    P.estrai   = (int (*)(ExZip *, unsigned int, const char *))  chiedi(t, "ex_zip_estrai");
    P.crea     = (ExZip *(*)(const char *))                      chiedi(t, "ex_zip_crea");
    P.aggiungi = (int (*)(ExZip *, const char *, const char *))  chiedi(t, "ex_zip_aggiungi");
    P.finisci  = (int (*)(ExZip *))                              chiedi(t, "ex_zip_finisci");
    P.chiudi   = (void (*)(ExZip *))                             chiedi(t, "ex_zip_chiudi");
    P.errore   = (const char *(*)(void))                         chiedi(t, "ex_zip_errore");
    /* ! FACOLTATIVA: un programma nuovo sopra un exzip.so di prima deve
     * partire lo stesso, e senza cartelle. Vedi ex_zip_aggiungi_albero. */
    P.albero   = (int (*)(ExZip *, const char *, const char *, int *))
                 exlib_simbolo(t, "ex_zip_aggiungi_albero");

    P.pronto = 1;
}

ExZip *ex_zip_apri(const char *percorso)   { assicura(); return P.apri(percorso); }
unsigned int ex_zip_quante(ExZip *z)       { assicura(); return P.quante(z); }
ExZip *ex_zip_crea(const char *percorso)   { assicura(); return P.crea(percorso); }
int ex_zip_finisci(ExZip *z)               { assicura(); return P.finisci(z); }
void ex_zip_chiudi(ExZip *z)               { assicura(); P.chiudi(z); }
const char *ex_zip_errore(void)            { assicura(); return P.errore(); }

int ex_zip_voce(ExZip *z, unsigned int i, ExZipVoce *v)
{
    assicura();
    return P.voce(z, i, v);
}

int ex_zip_estrai(ExZip *z, unsigned int i, const char *dove)
{
    assicura();
    return P.estrai(z, i, dove);
}

int ex_zip_aggiungi(ExZip *z, const char *file, const char *nome)
{
    assicura();
    return P.aggiungi(z, file, nome);
}

int ex_zip_aggiungi_albero(ExZip *z, const char *cartella, const char *nome,
                           int *saltati)
{
    assicura();
    if (saltati) *saltati = 0;
    return P.albero ? P.albero(z, cartella, nome, saltati) : -2;
}
