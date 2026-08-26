/* =============================================================================
 * lib/exhtml/exhtml_stub.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Lo stub di ExHtml. Stessa forma di lib/exhttp/exhttp_stub.c, e la somiglianza
 * e' voluta: una libreria nuova si aggiunge copiando questo schema.
 *
 * ! QUI SI GRIDA E SI MUORE, come per exwin, exdlg ed exhttp, e non si rende
 * zero come fa eximg. La differenza e' sempre la stessa: cosa puo' fare il
 * programma senza. Chi non sa leggere un PNG sa ancora disegnare; chi non sa
 * costruire un albero da del marcatore e apre pagine per mestiere non sa fare
 * piu' niente, e un errore chiaro subito vale piu' di una pagina vuota.
 * ============================================================================= */

#include "html.h"
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
    "/exwin/lib/exhtml.so",
    "/cdrom/exwin/lib/exhtml.so"
};

static struct {
    int pronto;
    void (*prepara)(HtmlDoc *, HtmlNodo *, unsigned int,
                    HtmlAttr *, unsigned int, char *, unsigned int);
    int  (*analizza)(HtmlDoc *, const char *, unsigned int);
    const char *(*nome)(const HtmlDoc *, int);
    const char *(*testo)(const HtmlDoc *, int);
    const char *(*attr)(const HtmlDoc *, int, const char *);

    /* Le mutazioni. Sono nove ponti in piu' e non uno stato in piu': la
     * libreria continua a non ricordare niente fra una chiamata e l'altra,
     * perche' il documento e' sempre quello di chi chiama. */
    int  (*crea_elemento)(HtmlDoc *, const char *);
    int  (*crea_testo)(HtmlDoc *, const char *);
    int  (*aggiungi)(HtmlDoc *, int, int);
    int  (*inserisci_prima)(HtmlDoc *, int, int, int);
    int  (*togli)(HtmlDoc *, int);
    int  (*attr_metti)(HtmlDoc *, int, const char *, const char *);
    int  (*attr_togli)(HtmlDoc *, int, const char *);
    int  (*testo_metti)(HtmlDoc *, int, const char *);
    unsigned int (*versione)(const HtmlDoc *);
} P;

static void *chiedi(const ExLibTesta *t, const char *nome)
{
    void *p = exlib_simbolo(t, nome);
    if (p == 0)
        grida_e_muori("exhtml: la libreria condivisa non esporta un nome che "
                      "serve a questo programma\n");
    return p;
}

static void assicura(void)
{
    const ExLibTesta *t;

    if (P.pronto) return;

    t = exlib_apri_fra(g_dove, (int)(sizeof g_dove / sizeof g_dove[0]));
    if (t == 0)
        grida_e_muori("exhtml: non trovo la libreria condivisa dell'HTML.\n"
                      "        Cercata in /exwin/lib/exhtml.so e "
                      "/cdrom/exwin/lib/exhtml.so\n");

    P.prepara  = (void (*)(HtmlDoc *, HtmlNodo *, unsigned int, HtmlAttr *,
                           unsigned int, char *, unsigned int))
                 chiedi(t, "html_prepara");
    P.analizza = (int (*)(HtmlDoc *, const char *, unsigned int))
                 chiedi(t, "html_analizza");
    P.nome     = (const char *(*)(const HtmlDoc *, int))chiedi(t, "html_nome");
    P.testo    = (const char *(*)(const HtmlDoc *, int))chiedi(t, "html_testo");
    P.attr     = (const char *(*)(const HtmlDoc *, int, const char *))
                 chiedi(t, "html_attr");

    P.crea_elemento   = (int (*)(HtmlDoc *, const char *))
                        chiedi(t, "html_crea_elemento");
    P.crea_testo      = (int (*)(HtmlDoc *, const char *))
                        chiedi(t, "html_crea_testo");
    P.aggiungi        = (int (*)(HtmlDoc *, int, int))
                        chiedi(t, "html_aggiungi");
    P.inserisci_prima = (int (*)(HtmlDoc *, int, int, int))
                        chiedi(t, "html_inserisci_prima");
    P.togli           = (int (*)(HtmlDoc *, int))chiedi(t, "html_togli");
    P.attr_metti      = (int (*)(HtmlDoc *, int, const char *, const char *))
                        chiedi(t, "html_attr_metti");
    P.attr_togli      = (int (*)(HtmlDoc *, int, const char *))
                        chiedi(t, "html_attr_togli");
    P.testo_metti     = (int (*)(HtmlDoc *, int, const char *))
                        chiedi(t, "html_testo_metti");
    P.versione        = (unsigned int (*)(const HtmlDoc *))
                        chiedi(t, "html_versione");

    P.pronto = 1;
}

void html_prepara(HtmlDoc *d, HtmlNodo *nodi, unsigned int nodi_max,
                  HtmlAttr *attr, unsigned int attr_max,
                  char *arena, unsigned int arena_max)
{ assicura(); P.prepara(d, nodi, nodi_max, attr, attr_max, arena, arena_max); }

int html_analizza(HtmlDoc *d, const char *testo, unsigned int n)
{ assicura(); return P.analizza(d, testo, n); }

const char *html_nome(const HtmlDoc *d, int nodo)
{ assicura(); return P.nome(d, nodo); }

const char *html_testo(const HtmlDoc *d, int nodo)
{ assicura(); return P.testo(d, nodo); }

const char *html_attr(const HtmlDoc *d, int nodo, const char *nome)
{ assicura(); return P.attr(d, nodo, nome); }

int html_crea_elemento(HtmlDoc *d, const char *nome)
{ assicura(); return P.crea_elemento(d, nome); }

int html_crea_testo(HtmlDoc *d, const char *testo)
{ assicura(); return P.crea_testo(d, testo); }

int html_aggiungi(HtmlDoc *d, int padre, int figlio)
{ assicura(); return P.aggiungi(d, padre, figlio); }

int html_inserisci_prima(HtmlDoc *d, int padre, int figlio, int riferimento)
{ assicura(); return P.inserisci_prima(d, padre, figlio, riferimento); }

int html_togli(HtmlDoc *d, int nodo)
{ assicura(); return P.togli(d, nodo); }

int html_attr_metti(HtmlDoc *d, int nodo, const char *nome, const char *valore)
{ assicura(); return P.attr_metti(d, nodo, nome, valore); }

int html_attr_togli(HtmlDoc *d, int nodo, const char *nome)
{ assicura(); return P.attr_togli(d, nodo, nome); }

int html_testo_metti(HtmlDoc *d, int nodo, const char *testo)
{ assicura(); return P.testo_metti(d, nodo, testo); }

unsigned int html_versione(const HtmlDoc *d)
{ assicura(); return P.versione(d); }
