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
    int (*riga)(const char *, const char *, char *, unsigned int);
    int (*percorso)(const char *, const char *, const char *,
                    char *, unsigned int);
    int (*chiedi)(const char *, const char *, const char *,
                  char *, unsigned int);
    int (*scegli)(const char *, const char *, const char *const *, int);
    int  (*sc_avvia)(const char *, const char *);
    int  (*sc_info)(int, ExScarico *);
    void (*sc_ferma)(int);
    int  (*sc_in_corso)(void);
    void (*sc_ferma_tutti)(void);
    void (*sc_finestra)(void);
    void (*sc_alla_fine)(ExScaricoFine, void *);
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
    P.riga   = (int (*)(const char *, const char *, char *, unsigned int))
               chiedi(t, "ex_dlg_riga");
    P.percorso = (int (*)(const char *, const char *, const char *,
                          char *, unsigned int)) chiedi(t, "ex_dlg_percorso");
    P.chiedi   = (int (*)(const char *, const char *, const char *,
                          char *, unsigned int)) chiedi(t, "ex_dlg_chiedi");

    /* ! FACOLTATIVA: senza, ex_dlg_scegli ripiega su una conferma fra la
     * prima risposta e l'ultima. Un programma nuovo sopra un exdlg.so di prima
     * deve partire lo stesso. */
    P.scegli = (int (*)(const char *, const char *, const char *const *, int))
               exlib_simbolo(t, "ex_dlg_scegli");

    /* I download: FACOLTATIVI per la stessa ragione. */
    P.sc_avvia       = (int (*)(const char *, const char *))exlib_simbolo(t, "ex_scarico_avvia");
    P.sc_info        = (int (*)(int, ExScarico *))exlib_simbolo(t, "ex_scarico_info");
    P.sc_ferma       = (void (*)(int))exlib_simbolo(t, "ex_scarico_ferma");
    P.sc_in_corso    = (int (*)(void))exlib_simbolo(t, "ex_scarichi_in_corso");
    P.sc_ferma_tutti = (void (*)(void))exlib_simbolo(t, "ex_scarichi_ferma_tutti");
    P.sc_finestra    = (void (*)(void))exlib_simbolo(t, "ex_scarichi_finestra");
    P.sc_alla_fine   = (void (*)(ExScaricoFine, void *))exlib_simbolo(t, "ex_scarichi_alla_fine");

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

int  ex_scarico_avvia(const char *u, const char *d)
{ assicura(); return P.sc_avvia ? P.sc_avvia(u, d) : EX_SC_ERR_VECCHIA; }
int  ex_scarico_info(int id, ExScarico *s)
{ assicura(); return P.sc_info ? P.sc_info(id, s) : 0; }
void ex_scarico_ferma(int id)        { assicura(); if (P.sc_ferma) P.sc_ferma(id); }
int  ex_scarichi_in_corso(void)      { assicura(); return P.sc_in_corso ? P.sc_in_corso() : 0; }
void ex_scarichi_ferma_tutti(void)   { assicura(); if (P.sc_ferma_tutti) P.sc_ferma_tutti(); }
void ex_scarichi_finestra(void)      { assicura(); if (P.sc_finestra) P.sc_finestra(); }
void ex_scarichi_alla_fine(ExScaricoFine fn, void *d)
{ assicura(); if (P.sc_alla_fine) P.sc_alla_fine(fn, d); }

int ex_dlg_scegli(const char *titolo, const char *testo,
                  const char *const *voci, int n)
{
    assicura();
    if (P.scegli) return P.scegli(titolo, testo, voci, n);
    if (!voci || n < 1) return -1;
    return P.conferma(titolo, testo, voci[0], voci[n - 1]) ? 0 : -1;
}

int ex_dlg_riga(const char *titolo, const char *domanda,
                char *valore, unsigned int max)
{
    assicura();
    return P.riga(titolo, domanda, valore, max);
}

int ex_dlg_percorso(const char *titolo, const char *etichetta, const char *ok,
                    char *percorso, unsigned int max)
{
    assicura();
    return P.percorso(titolo, etichetta, ok, percorso, max);
}

int ex_dlg_chiedi(const char *titolo, const char *domanda, const char *ok,
                  char *valore, unsigned int max)
{
    assicura();
    return P.chiedi(titolo, domanda, ok, valore, max);
}
