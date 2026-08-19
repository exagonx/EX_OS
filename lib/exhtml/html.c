/* =============================================================================
 * lib/exhtml/html.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il lettore di HTML. Cosa fa e perche' sta in html.h.
 *
 * Il giro e' uno solo, un carattere per volta:
 *
 *     testo        finche' non arriva un '<'
 *     tag          nome, attributi, e se si chiude da se'
 *     commento     <!-- ... -->  e  <!DOCTYPE ...>
 *     grezzo       dentro <script> e <style>: NON e' markup
 *
 * ! DENTRO <script> E <style> NON C'E' MARKUP, e non e' un dettaglio: il
 * JavaScript e' pieno di «<» e di «&», e leggerli come tag vuol dire che dal
 * primo confronto «a < b» in poi l'albero e' spazzatura. Su una pagina vera
 * questo non e' un caso raro: e' il caso normale.
 * ============================================================================= */

#include "html.h"

/* -----------------------------------------------------------------------------
 * Le poche cose che servono, invece della libc
 * --------------------------------------------------------------------------- */
static int minuscola(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int spazio(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int uguale_min(const char *a, const char *b)
{
    while (*a && *b) {
        if (minuscola((unsigned char)*a) != minuscola((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* -----------------------------------------------------------------------------
 * L'arena
 * --------------------------------------------------------------------------- */

/* Comincia una stringa nell'arena e rende il suo scostamento. */
static unsigned int arena_apri(HtmlDoc *d)
{
    return d->arena_n;
}

static void arena_car(HtmlDoc *d, int c)
{
    if (d->arena_n + 1 >= d->arena_max) { d->troncato = 1; return; }
    d->arena[d->arena_n++] = (char)c;
}

static void arena_chiudi(HtmlDoc *d)
{
    if (d->arena_n + 1 >= d->arena_max) { d->troncato = 1; return; }
    d->arena[d->arena_n++] = '\0';
}

/* -----------------------------------------------------------------------------
 * Le entita'
 *
 * ! SI SCIOLGONO LE POCHE CHE CONTANO, e si dichiara quali. Le tabelle complete
 * dell'HTML hanno piu' di duemila voci, quasi tutte per simboli che questo
 * sistema non sa nemmeno disegnare — dichiara Latin-1. Quelle qui sotto sono
 * quelle senza le quali una pagina qualunque si vede sbagliata.
 *
 * ! E CIO' CHE NON SI RICONOSCE SI LASCIA COM'E', «&» compresa. Buttarla
 * vorrebbe dire che un indirizzo con «?a=1&b=2» dentro il testo perde pezzi;
 * lasciarla e' l'unica scelta che non inventa niente.
 * --------------------------------------------------------------------------- */
static const struct { const char *nome; int car; } ENTITA[] = {
    { "amp",   '&'  }, { "lt",    '<'  }, { "gt",    '>'  },
    { "quot",  '"'  }, { "apos",  '\'' }, { "nbsp",  ' '  },
    { "eacute", 0xE9 }, { "egrave", 0xE8 }, { "agrave", 0xE0 },
    { "igrave", 0xEC }, { "ograve", 0xF2 }, { "ugrave", 0xF9 },
    { "ccedil", 0xE7 }, { "copy",   0xA9 }, { "reg",    0xAE },
    { "deg",    0xB0 }, { "euro",   'E'  }, { "hellip", '.'  },
    { "mdash",  '-'  }, { "ndash",  '-'  }, { "laquo",  0xAB },
    { "raquo",  0xBB }, { "middot", 0xB7 }, { "times",  'x'  },
    { 0, 0 }
};

/* Scioglie l'entita' che comincia a `p` (dopo la '&'). Rende quanti caratteri
 * ha consumato dall'ingresso, 0 se non e' un'entita' riconosciuta. */
static unsigned int entita(const char *p, unsigned int n, int *car)
{
    unsigned int i;

    if (n == 0) return 0;

    /* Numerica: &#65; oppure &#x41; */
    if (p[0] == '#') {
        unsigned int v = 0, k = 1, cifre = 0;
        int          esa = 0;

        if (k < n && (p[k] == 'x' || p[k] == 'X')) { esa = 1; k++; }

        while (k < n && cifre < 7) {
            int c = p[k];

            if (c >= '0' && c <= '9')                     v = v * (esa ? 16u : 10u) + (unsigned)(c - '0');
            else if (esa && c >= 'a' && c <= 'f')         v = v * 16u + (unsigned)(c - 'a' + 10);
            else if (esa && c >= 'A' && c <= 'F')         v = v * 16u + (unsigned)(c - 'A' + 10);
            else break;
            k++; cifre++;
        }
        if (cifre == 0) return 0;
        if (k < n && p[k] == ';') k++;

        /* ! OLTRE IL LATIN-1 SI METTE UN PUNTO INTERROGATIVO, e non si tace:
         * questo sistema dichiara Latin-1 e non ha i glifi per il resto. Un
         * carattere mancante che si vede e' meglio di uno che sparisce. */
        *car = (v == 0 || v > 255) ? '?' : (int)v;
        return k;
    }

    for (i = 0; ENTITA[i].nome; i++) {
        const char  *nm = ENTITA[i].nome;
        unsigned int k  = 0;

        while (nm[k] && k < n && p[k] == nm[k]) k++;
        if (nm[k] == '\0') {
            /* Il punto e virgola e' facoltativo nella pratica. */
            if (k < n && p[k] == ';') k++;
            *car = ENTITA[i].car;
            return k;
        }
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * I nodi
 * --------------------------------------------------------------------------- */
static int nodo_nuovo(HtmlDoc *d, int tipo)
{
    HtmlNodo *v;

    if (d->nodi_n >= d->nodi_max) { d->troncato = 1; return -1; }

    v = &d->nodi[d->nodi_n];
    v->tipo = (unsigned char)tipo;
    v->padre = v->primo_figlio = v->ultimo_figlio = v->prossimo = -1;
    v->attributi = -1;
    v->nome = v->testo = 0;
    return (int)d->nodi_n++;
}

static void attacca(HtmlDoc *d, int padre, int figlio)
{
    HtmlNodo *p;

    if (padre < 0 || figlio < 0) return;
    p = &d->nodi[padre];

    d->nodi[figlio].padre = padre;
    if (p->ultimo_figlio < 0) p->primo_figlio = figlio;
    else                      d->nodi[p->ultimo_figlio].prossimo = figlio;
    p->ultimo_figlio = figlio;
}

/* -----------------------------------------------------------------------------
 * Le regole dell'HTML che non sono XML
 * --------------------------------------------------------------------------- */

/* Elementi che non hanno contenuto e non si chiudono mai. */
static int vuoto(const char *nome)
{
    static const char *const V[] = {
        "br", "img", "hr", "meta", "link", "input", "area", "base",
        "col", "embed", "param", "source", "track", "wbr", 0
    };
    int i;

    for (i = 0; V[i]; i++) if (uguale_min(nome, V[i])) return 1;
    return 0;
}

/* Dentro questi, cio' che segue NON e' markup. */
static int grezzo(const char *nome)
{
    return uguale_min(nome, "script") || uguale_min(nome, "style");
}

/* ! APRIRE UN <li> CHIUDE IL <li> DI PRIMA, e senza queste regole un elenco
 * diventa una scala: ogni voce figlia della precedente, e l'indentazione
 * cresce a ogni riga. E' il difetto che si vede subito su una pagina vera,
 * perche' quasi nessuno chiude i <li> e i <p>.
 *
 * Rende 1 se aprire `nuovo` deve chiudere `aperto`. */
static int chiude_implicito(const char *aperto, const char *nuovo)
{
    if (uguale_min(aperto, "p")) {
        static const char *const B[] = {
            "p", "div", "ul", "ol", "li", "table", "h1", "h2", "h3", "h4",
            "h5", "h6", "pre", "form", "hr", "blockquote", "section",
            "article", "header", "footer", "nav", 0
        };
        int i;
        for (i = 0; B[i]; i++) if (uguale_min(nuovo, B[i])) return 1;
        return 0;
    }
    if (uguale_min(aperto, "li"))  return uguale_min(nuovo, "li");
    if (uguale_min(aperto, "dt") || uguale_min(aperto, "dd"))
        return uguale_min(nuovo, "dt") || uguale_min(nuovo, "dd");
    if (uguale_min(aperto, "tr"))  return uguale_min(nuovo, "tr");
    if (uguale_min(aperto, "td") || uguale_min(aperto, "th"))
        return uguale_min(nuovo, "td") || uguale_min(nuovo, "th") ||
               uguale_min(nuovo, "tr");
    if (uguale_min(aperto, "option")) return uguale_min(nuovo, "option");
    return 0;
}

/* -----------------------------------------------------------------------------
 * L'analisi
 * --------------------------------------------------------------------------- */
void html_prepara(HtmlDoc *d,
                  HtmlNodo *nodi, unsigned int nodi_max,
                  HtmlAttr *attr, unsigned int attr_max,
                  char *arena, unsigned int arena_max)
{
    if (!d) return;

    d->nodi = nodi;   d->nodi_max = nodi_max;   d->nodi_n = 0;
    d->attr = attr;   d->attr_max = attr_max;   d->attr_n = 0;
    d->arena = arena; d->arena_max = arena_max; d->arena_n = 0;
    d->radice = -1;
    d->troncato = 0;
}

const char *html_nome(const HtmlDoc *d, int nodo)
{
    if (!d || nodo < 0 || (unsigned)nodo >= d->nodi_n) return "";
    if (d->nodi[nodo].tipo != HTML_ELEMENTO) return "";
    return d->arena + d->nodi[nodo].nome;
}

const char *html_testo(const HtmlDoc *d, int nodo)
{
    if (!d || nodo < 0 || (unsigned)nodo >= d->nodi_n) return "";
    if (d->nodi[nodo].tipo != HTML_TESTO) return "";
    return d->arena + d->nodi[nodo].testo;
}

const char *html_attr(const HtmlDoc *d, int nodo, const char *nome)
{
    int a;

    if (!d || nodo < 0 || (unsigned)nodo >= d->nodi_n || !nome) return 0;

    for (a = d->nodi[nodo].attributi; a >= 0; a = d->attr[a].prossimo)
        if (uguale_min(d->arena + d->attr[a].nome, nome))
            return d->arena + d->attr[a].valore;
    return 0;
}

int html_analizza(HtmlDoc *d, const char *t, unsigned int n)
{
    unsigned int i = 0;
    int          pila[64];
    int          cima = 0;

    if (!d || !d->nodi || !d->arena || !t) return 0;

    d->radice = nodo_nuovo(d, HTML_ELEMENTO);
    if (d->radice < 0) return 0;
    d->nodi[d->radice].nome = arena_apri(d);
    arena_car(d, '#'); arena_car(d, 'd'); arena_car(d, 'o');
    arena_car(d, 'c');
    arena_chiudi(d);

    pila[0] = d->radice;

    while (i < n) {
        /* --- testo ---------------------------------------------------- */
        if (t[i] != '<') {
            unsigned int inizio = arena_apri(d);
            int          qualcosa = 0;

            while (i < n && t[i] != '<') {
                int c = (unsigned char)t[i];

                if (c == '&') {
                    int          car = 0;
                    unsigned int q = entita(t + i + 1, n - i - 1, &car);

                    if (q) { arena_car(d, car); i += 1 + q; qualcosa = 1; continue; }
                }

                /* ! GLI SPAZI SI RIDUCONO A UNO, ed e' cio' che l'HTML dice di
                 * fare: un documento indentato ha decine di spazi e ritorni a
                 * capo fra un tag e l'altro, che sulla pagina non si vedono.
                 * Tenerli vorrebbe dire un'impaginazione piena di buchi. */
                if (spazio(c)) {
                    while (i < n && spazio((unsigned char)t[i])) i++;
                    arena_car(d, ' ');
                    qualcosa = 1;
                    continue;
                }

                arena_car(d, c);
                qualcosa = 1;
                i++;
            }
            arena_chiudi(d);

            if (qualcosa) {
                int v = nodo_nuovo(d, HTML_TESTO);

                if (v >= 0) {
                    d->nodi[v].testo = inizio;
                    attacca(d, pila[cima], v);
                }
            }
            continue;
        }

        /* --- commenti e dichiarazioni --------------------------------- */
        if (i + 3 < n && t[i+1] == '!' && t[i+2] == '-' && t[i+3] == '-') {
            i += 4;
            while (i + 2 < n && !(t[i] == '-' && t[i+1] == '-' && t[i+2] == '>')) i++;
            i = (i + 2 < n) ? i + 3 : n;
            continue;
        }
        if (i + 1 < n && (t[i+1] == '!' || t[i+1] == '?')) {
            while (i < n && t[i] != '>') i++;
            if (i < n) i++;
            continue;
        }

        /* --- tag di chiusura ------------------------------------------ */
        if (i + 1 < n && t[i+1] == '/') {
            unsigned int nm = arena_apri(d);
            int          k;

            i += 2;
            while (i < n && !spazio((unsigned char)t[i]) && t[i] != '>')
                arena_car(d, minuscola((unsigned char)t[i++]));
            arena_chiudi(d);
            while (i < n && t[i] != '>') i++;
            if (i < n) i++;

            /* ! SI CERCA IL TAG NELLA PILA, e non si chiude solo la cima: una
             * pagina con «<b><i>testo</b>» chiude la <b> mentre la <i> e'
             * ancora aperta. Chiudendo la cima si chiuderebbe la <i> e la <b>
             * resterebbe aperta per sempre, cioe' tutto il resto della pagina
             * in grassetto. Cosi' invece si chiude fino a lei, e la <i> muore
             * con lei — che e' quello che fanno i browser veri. */
            for (k = cima; k > 0; k--)
                if (uguale_min(d->arena + d->nodi[pila[k]].nome,
                               d->arena + nm)) { cima = k - 1; break; }

            /* Non trovato: si ignora. Un tag di chiusura che non ha mai avuto
             * un'apertura non deve chiudere qualcos'altro a caso. */
            d->arena_n = nm;        /* il nome non serve piu': si restituisce */
            continue;
        }

        /* --- tag di apertura ------------------------------------------ */
        {
            unsigned int nm = arena_apri(d);
            int          v, da_se = 0, ultimo_attr = -1;

            i++;
            while (i < n && !spazio((unsigned char)t[i]) &&
                   t[i] != '>' && t[i] != '/')
                arena_car(d, minuscola((unsigned char)t[i++]));
            arena_chiudi(d);

            /* ! UN «<» CHE NON APRE UN TAG E' TESTO, E VA EMESSO. «a < b» in
             * una pagina e' comunissimo, e prima quel carattere spariva: si
             * restituiva l'arena e si scriveva '<' senza creare nessun nodo,
             * quindi non finiva da nessuna parte. Adesso diventa un nodo di
             * testo suo, e chi concatena i figli lo ritrova al suo posto. */
            if (d->arena[nm] == '\0') {
                int w;

                d->arena_n = nm;
                arena_car(d, '<');
                arena_chiudi(d);
                w = nodo_nuovo(d, HTML_TESTO);
                if (w >= 0) { d->nodi[w].testo = nm; attacca(d, pila[cima], w); }
                continue;
            }

            /* Le regole di chiusura implicita, prima di aprire. */
            while (cima > 0 &&
                   chiude_implicito(d->arena + d->nodi[pila[cima]].nome,
                                    d->arena + nm))
                cima--;

            v = nodo_nuovo(d, HTML_ELEMENTO);
            if (v < 0) return 1;            /* finiti i nodi: si smette */
            d->nodi[v].nome = nm;
            attacca(d, pila[cima], v);

            /* --- gli attributi --- */
            for (;;) {
                unsigned int an, av;
                int          a;

                while (i < n && spazio((unsigned char)t[i])) i++;
                if (i >= n) break;
                if (t[i] == '>') { i++; break; }
                if (t[i] == '/') { da_se = 1; i++; continue; }

                an = arena_apri(d);
                while (i < n && !spazio((unsigned char)t[i]) &&
                       t[i] != '=' && t[i] != '>' && t[i] != '/')
                    arena_car(d, minuscola((unsigned char)t[i++]));
                arena_chiudi(d);

                while (i < n && spazio((unsigned char)t[i])) i++;

                av = arena_apri(d);
                if (i < n && t[i] == '=') {
                    i++;
                    while (i < n && spazio((unsigned char)t[i])) i++;

                    if (i < n && (t[i] == '"' || t[i] == '\'')) {
                        char q = t[i++];

                        while (i < n && t[i] != q) {
                            int c = (unsigned char)t[i];

                            if (c == '&') {
                                int car = 0;
                                unsigned int k = entita(t + i + 1, n - i - 1, &car);

                                if (k) { arena_car(d, car); i += 1 + k; continue; }
                            }
                            arena_car(d, c); i++;
                        }
                        if (i < n) i++;
                    } else {
                        /* ! IL VALORE PUO' NON AVERE VIRGOLETTE, e succede
                         * davvero: «<td width=100>». Pretenderle vorrebbe dire
                         * perdere l'attributo proprio sulle pagine vecchie. */
                        while (i < n && !spazio((unsigned char)t[i]) && t[i] != '>')
                            arena_car(d, (unsigned char)t[i++]);
                    }
                }
                arena_chiudi(d);

                if (d->attr_n < d->attr_max) {
                    a = (int)d->attr_n++;
                    d->attr[a].nome = an;
                    d->attr[a].valore = av;
                    d->attr[a].prossimo = -1;
                    if (ultimo_attr < 0) d->nodi[v].attributi = a;
                    else                 d->attr[ultimo_attr].prossimo = a;
                    ultimo_attr = a;
                } else {
                    d->troncato = 1;
                }
            }

            /* --- grezzo: <script> e <style> --- */
            if (grezzo(d->arena + nm)) {
                unsigned int inizio = arena_apri(d);
                int          qualcosa = 0;

                while (i < n) {
                    if (t[i] == '<' && i + 1 < n && t[i+1] == '/') {
                        unsigned int k = i + 2, j = 0;
                        const char  *g = d->arena + nm;

                        while (g[j] && k < n &&
                               minuscola((unsigned char)t[k]) == g[j]) { j++; k++; }
                        if (g[j] == '\0') break;    /* e' la sua chiusura */
                    }
                    arena_car(d, (unsigned char)t[i++]);
                    qualcosa = 1;
                }
                arena_chiudi(d);

                if (qualcosa) {
                    int w = nodo_nuovo(d, HTML_TESTO);

                    if (w >= 0) { d->nodi[w].testo = inizio; attacca(d, v, w); }
                }

                /* Si consuma il tag di chiusura. */
                while (i < n && t[i] != '>') i++;
                if (i < n) i++;
                continue;
            }

            /* --- si apre, se non e' vuoto --- */
            if (!da_se && !vuoto(d->arena + nm)) {
                if (cima + 1 < (int)(sizeof(pila) / sizeof(pila[0]))) {
                    pila[++cima] = v;
                } else {
                    /* ! LA PILA HA UN FONDO, e chi la riempie e' il documento:
                     * mille <div> annidati sono un modo di far crescere lo
                     * stack di chi legge. Oltre il tetto si continua a leggere
                     * ma i figli finiscono all'ultimo livello buono, che e'
                     * una pagina un po' piatta invece di un guasto. */
                    d->troncato = 1;
                }
            }
        }
    }

    return 1;
}
