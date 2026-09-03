/* =============================================================================
 * lib/excss/excss_stub.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Lo stub di ExCss. Stessa forma di lib/exhtml/exhtml_stub.c.
 *
 * ! QUI SI RINUNCIA IN SILENZIO, COME eximg, E NON SI MUORE COME exhtml — ed e'
 * la differenza che conta: senza l'albero una pagina non esiste, senza i fogli
 * di stile una pagina si vede lo stesso, solo senza colori e senza corpi. Un
 * browser che si rifiuta di partire perche' manca excss.so mostrerebbe zero
 * pagine invece di mostrarle tutte un po' piu' spoglie.
 *
 * Su rinuncia css_analizza rende 0 e css_calcola posa uno stile vuoto: chi
 * chiama non deve accorgersene, perche' «nessuna regola» e' gia' un caso che
 * deve saper gestire — succede su qualunque pagina senza CSS.
 * ============================================================================= */

#include "css.h"
#include "exlib.h"

static const char *const g_dove[] = {
    "/exwin/lib/excss.so",
    "/cdrom/exwin/lib/excss.so"
};

static struct {
    int cercata;
    void (*prepara)(CssFoglio *, CssRegola *, unsigned int,
                    CssDich *, unsigned int, char *, unsigned int);
    unsigned int (*analizza)(CssFoglio *, const char *, unsigned int,
                             unsigned char);
    void (*calcola)(const CssFoglio *, const HtmlDoc *, int,
                    const CssStile *, CssStile *);
    void (*inline_)(const char *, unsigned int, CssStile *);
    void (*vuoto)(CssStile *);
    int  (*colore)(const char *, unsigned int, unsigned int *);
} P;

static int assicura(void)
{
    const ExLibTesta *t;

    if (!P.cercata) {
        P.cercata = 1;      /* ! PRIMA DEL TENTATIVO: se non c'e', non si torna
                             * a cercarla a ogni elemento della pagina. */
        t = exlib_apri_fra(g_dove, (int)(sizeof g_dove / sizeof g_dove[0]));
        if (t) {
            P.prepara  = (void (*)(CssFoglio *, CssRegola *, unsigned int,
                                   CssDich *, unsigned int, char *,
                                   unsigned int))exlib_simbolo(t, "css_prepara");
            P.analizza = (unsigned int (*)(CssFoglio *, const char *,
                                           unsigned int, unsigned char))
                         exlib_simbolo(t, "css_analizza");
            P.calcola  = (void (*)(const CssFoglio *, const HtmlDoc *, int,
                                   const CssStile *, CssStile *))
                         exlib_simbolo(t, "css_calcola");
            P.inline_  = (void (*)(const char *, unsigned int, CssStile *))
                         exlib_simbolo(t, "css_stile_inline");
            P.vuoto    = (void (*)(CssStile *))exlib_simbolo(t, "css_stile_vuoto");
            P.colore   = (int (*)(const char *, unsigned int, unsigned int *))
                         exlib_simbolo(t, "css_colore");
        }
    }

    /* Uno qualunque mancante vuol dire una excss.so piu' vecchia di questo
     * programma: si rinuncia a tutto invece di chiamare un indirizzo nullo. */
    return P.prepara && P.analizza && P.calcola && P.inline_ && P.vuoto;
}

/* ! LA COPIA DI RIPIEGO STA QUI E NON PUO' MANCARE: css_stile_vuoto e' cio' che
 * rende sicuri tutti gli altri ripieghi, quindi non puo' dipendere dalla
 * libreria che potrebbe non esserci. Sono dodici righe, ed e' il prezzo giusto
 * per non avere uno stile pieno di spazzatura quando il resto rinuncia. */
static void vuoto_locale(CssStile *s)
{
    int i;

    if (!s) return;
    s->colore       = CSS_NIENTE;
    s->sfondo       = CSS_NIENTE;
    s->corpo        = CSS_MISURA_NO;
    s->grassetto    = CSS_FORSE;
    s->corsivo      = CSS_FORSE;
    s->allineamento = CSS_ALL_EREDITA;
    s->display      = CSS_DISPLAY_EREDITA;
    for (i = 0; i < 4; i++) s->margine[i] = CSS_MISURA_NO;
}

void css_prepara(CssFoglio *f, CssRegola *regole, unsigned int regole_max,
                 CssDich *dich, unsigned int dich_max,
                 char *arena, unsigned int arena_max)
{
    if (assicura()) { P.prepara(f, regole, regole_max, dich, dich_max,
                                arena, arena_max); return; }
    if (f) { f->regole_n = 0; f->dich_n = 0; f->arena_n = 0; f->troncato = 0;
             f->regole = 0; f->dich = 0; f->arena = 0; }
}

unsigned int css_analizza(CssFoglio *f, const char *testo, unsigned int n,
                          unsigned char origine)
{
    if (!assicura()) return 0;
    return P.analizza(f, testo, n, origine);
}

void css_calcola(const CssFoglio *f, const HtmlDoc *d, int nodo,
                 const CssStile *ereditato, CssStile *out)
{
    if (assicura()) { P.calcola(f, d, nodo, ereditato, out); return; }
    vuoto_locale(out);
    if (ereditato && out) {
        out->colore = ereditato->colore;
        out->corpo  = ereditato->corpo;
        out->grassetto = ereditato->grassetto;
        out->corsivo   = ereditato->corsivo;
        out->allineamento = ereditato->allineamento;
    }
}

void css_stile_inline(const char *testo, unsigned int n, CssStile *s)
{
    if (assicura()) P.inline_(testo, n, s);
}

void css_stile_vuoto(CssStile *s)
{
    if (assicura()) { P.vuoto(s); return; }
    vuoto_locale(s);
}

/* ! SENZA LA LIBRERIA I COLORI NON SI LEGGONO, e va bene cosi': un attributo
 * `bgcolor` che non si capisce lascia lo sfondo com'era. E' la stessa rinuncia
 * di tutto il resto di questo file — una pagina con meno colori e' meglio di un
 * browser che non parte. */
int css_colore(const char *v, unsigned int n, unsigned int *out)
{
    if (assicura() && P.colore) return P.colore(v, n, out);
    return 0;
}
