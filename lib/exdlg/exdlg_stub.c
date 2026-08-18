/* =============================================================================
 * lib/exdlg/exdlg_stub.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Lo stub di ExDlg. Stessa forma di lib/exwin/exwin_stub.c, e la somiglianza
 * e' voluta: una libreria nuova si aggiunge copiando questo schema, e chi lo
 * legge la seconda volta non deve capire niente di nuovo.
 * ============================================================================= */

#include "exdlg.h"
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
    "/exwin/lib/exdlg.so",
    "/cdrom/exwin/lib/exdlg.so"
};

static struct {
    int pronto;
    int (*apri)(char *, unsigned int);
    int (*salva)(char *, unsigned int);
    int (*avviso)(const char *, const char *);
    int (*conferma)(const char *, const char *, const char *, const char *);
} P;

static void *chiedi(const ExLibTesta *t, const char *nome)
{
    void *p = exlib_simbolo(t, nome);
    if (p == 0)
        grida_e_muori("exdlg: la libreria condivisa non esporta un nome che "
                      "serve a questo programma\n");
    return p;
}

static void assicura(void)
{
    const ExLibTesta *t;

    if (P.pronto) return;

    t = exlib_apri_fra(g_dove, (int)(sizeof g_dove / sizeof g_dove[0]));
    if (t == 0)
        grida_e_muori("exdlg: non trovo la libreria condivisa dei dialoghi.\n"
                      "       Cercata in /exwin/lib/exdlg.so e "
                      "/cdrom/exwin/lib/exdlg.so\n");

    P.apri   = (int (*)(char *, unsigned int))        chiedi(t, "ex_dlg_apri");
    P.salva  = (int (*)(char *, unsigned int))        chiedi(t, "ex_dlg_salva");
    P.avviso = (int (*)(const char *, const char *))  chiedi(t, "ex_dlg_avviso");
    P.conferma = (int (*)(const char *, const char *, const char *,
                          const char *)) chiedi(t, "ex_dlg_conferma");

    P.pronto = 1;
}

int ex_dlg_apri(char *percorso, unsigned int max)
{
    assicura();
    return P.apri(percorso, max);
}

int ex_dlg_salva(char *percorso, unsigned int max)
{
    assicura();
    return P.salva(percorso, max);
}

int ex_dlg_avviso(const char *titolo, const char *testo)
{
    assicura();
    return P.avviso(titolo, testo);
}

int ex_dlg_conferma(const char *titolo, const char *testo,
                    const char *si, const char *no)
{
    assicura();
    return P.conferma(titolo, testo, si, no);
}
