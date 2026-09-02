/* =============================================================================
 * lib/exhttp/exhttp_stub.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Lo stub di ExHttp. Stessa forma di lib/exdlg/exdlg_stub.c, e la somiglianza
 * e' voluta: una libreria nuova si aggiunge copiando questo schema.
 *
 * ! QUI SI GRIDA E SI MUORE, come per exwin ed exdlg, e non si rende zero come
 * fa eximg. La differenza e' cosa puo' fare il programma senza: chi non sa
 * leggere un PNG sa ancora disegnare, chi non sa parlare HTTP e si chiama
 * `scarica` non sa fare piu' niente. Un errore chiaro subito vale piu' di un
 * fallimento silenzioso a ogni indirizzo.
 * ============================================================================= */

#include "exhttp.h"
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
    "/exwin/lib/exhttp.so",
    "/cdrom/exwin/lib/exhttp.so"
};

static struct {
    int pronto;
    int (*prendi)(const char *, unsigned char *, unsigned int, ExHttpEsito *);
    int (*tcp)(ExHttpTrasporto *, const char *, unsigned int);
    int (*scambio)(ExHttpTrasporto *, const HttpUrl *, unsigned char *,
                   unsigned int, ExHttpEsito *, HttpRisposta *);
    int (*url)(const char *, HttpUrl *);
    void (*biscotti)(ExHttpBiscottiChiedi, ExHttpBiscottoArrivato, void *);
    void (*attesa)(ExHttpAttesa, void *);
    int (*posta)(const char *, const char *, unsigned char *, unsigned int,
                 ExHttpEsito *);
} P;

static void *chiedi(const ExLibTesta *t, const char *nome)
{
    void *p = exlib_simbolo(t, nome);
    if (p == 0)
        grida_e_muori("exhttp: la libreria condivisa non esporta un nome che "
                      "serve a questo programma\n");
    return p;
}

static void assicura(void)
{
    const ExLibTesta *t;

    if (P.pronto) return;

    t = exlib_apri_fra(g_dove, (int)(sizeof g_dove / sizeof g_dove[0]));
    if (t == 0)
        grida_e_muori("exhttp: non trovo la libreria condivisa della rete.\n"
                      "        Cercata in /exwin/lib/exhttp.so e "
                      "/cdrom/exwin/lib/exhttp.so\n");

    P.prendi  = (int (*)(const char *, unsigned char *, unsigned int,
                         ExHttpEsito *))chiedi(t, "exhttp_prendi");
    P.tcp     = (int (*)(ExHttpTrasporto *, const char *, unsigned int))
                chiedi(t, "exhttp_tcp");
    P.scambio = (int (*)(ExHttpTrasporto *, const HttpUrl *, unsigned char *,
                         unsigned int, ExHttpEsito *, HttpRisposta *))
                chiedi(t, "exhttp_scambio");
    P.url     = (int (*)(const char *, HttpUrl *))chiedi(t, "http_url");
    P.posta   = (int (*)(const char *, const char *, unsigned char *,
                         unsigned int, ExHttpEsito *))chiedi(t, "exhttp_posta");
    P.biscotti = (void (*)(ExHttpBiscottiChiedi, ExHttpBiscottoArrivato, void *))
                 chiedi(t, "exhttp_biscotti");
    P.attesa   = (void (*)(ExHttpAttesa, void *))chiedi(t, "exhttp_attesa");

    P.pronto = 1;
}

int exhttp_prendi(const char *url, unsigned char *buf, unsigned int max,
                  ExHttpEsito *e)
{ assicura(); return P.prendi(url, buf, max, e); }

void exhttp_biscotti(ExHttpBiscottiChiedi chiedi,
                     ExHttpBiscottoArrivato arrivato, void *dato)
{ assicura(); P.biscotti(chiedi, arrivato, dato); }

void exhttp_attesa(ExHttpAttesa f, void *dato)
{ assicura(); P.attesa(f, dato); }

int exhttp_posta(const char *url, const char *corpo, unsigned char *buf,
                 unsigned int max, ExHttpEsito *e)
{ assicura(); return P.posta(url, corpo, buf, max, e); }

int exhttp_tcp(ExHttpTrasporto *t, const char *host, unsigned int porta)
{ assicura(); return P.tcp(t, host, porta); }

int exhttp_scambio(ExHttpTrasporto *t, const HttpUrl *u, unsigned char *buf,
                   unsigned int max, ExHttpEsito *e, HttpRisposta *r)
{ assicura(); return P.scambio(t, u, buf, max, e, r); }

int http_url(const char *url, HttpUrl *u)
{ assicura(); return P.url(url, u); }
