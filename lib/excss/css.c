/* =============================================================================
 * lib/excss/css.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il lettore dei fogli di stile. Il perche' delle scelte sta in css.h.
 *
 * ! NON INCLUDE LA libc, come html.c, e per la stessa ragione: cosi' si compila
 * anche sull'host, dove il banco di prova lo mette alla frusta con fogli
 * scritti male. I difetti che contano stanno nei documenti MALFATTI, e quelli
 * si scrivono a mano.
 *
 * ! UN FOGLIO NON SI RIFIUTA MAI PER INTERO. Una regola che non si capisce si
 * SALTA fino alla graffa chiusa e si va avanti: e' cio' che fanno i browser, ed
 * e' l'unica cosa sensata: i fogli veri sono pieni di roba piu' nuova di chi la
 * legge — `@media`, funzioni, unita' che qui non ci sono — e chi si fermasse
 * alla prima non mostrerebbe mai niente.
 * ============================================================================= */

#include "css.h"

/* -----------------------------------------------------------------------------
 * Gli attrezzi, tutti locali
 * --------------------------------------------------------------------------- */
static int minusc(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int spazio(int c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

/* Un carattere che puo' stare in un nome di tag, classe, id o proprieta'. */
static int nomeok(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

static int ug_min(const char *a, const char *b)
{
    while (*a && *b && minusc((unsigned char)*a) == minusc((unsigned char)*b)) {
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

/* -----------------------------------------------------------------------------
 * L'arena
 *
 * ! LO SCOSTAMENTO 0 E' RISERVATO A «NIENTE», ed e' il motivo per cui il primo
 * byte dell'arena e' un terminatore che non appartiene a nessuno: cosi' un
 * campo a zero vuol dire «non c'e'» e non «la stringa vuota», e i due casi non
 * si confondono mai.
 * --------------------------------------------------------------------------- */
static unsigned int arena_metti(CssFoglio *f, const char *s, unsigned int n)
{
    unsigned int inizio = f->arena_n, i;

    if (n == 0) return 0;
    if (f->arena_n + n + 1 > f->arena_max) { f->troncato = 1; return 0; }

    for (i = 0; i < n; i++)
        f->arena[f->arena_n++] = (char)minusc((unsigned char)s[i]);
    f->arena[f->arena_n++] = '\0';
    return inizio;
}

void css_stile_vuoto(CssStile *s)
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
    s->famiglia     = CSS_FAM_EREDITA;
    for (i = 0; i < 4; i++) s->margine[i] = CSS_MISURA_NO;
}

void css_prepara(CssFoglio *f,
                 CssRegola *regole, unsigned int regole_max,
                 CssPezzo *pezzi, unsigned int pezzi_max,
                 CssDich *dich, unsigned int dich_max,
                 char *arena, unsigned int arena_max)
{
    int k;

    if (!f) return;

    f->regole     = regole;
    f->regole_max = regole_max;
    f->regole_n   = 0;
    f->pezzi      = pezzi;
    f->pezzi_max  = pezzi_max;
    f->pezzi_n    = 0;
    for (k = 0; k < CSS_SECCHI; k++) f->testa[k] = f->coda[k] = -1;
    f->uni_testa = f->uni_coda = -1;
    f->dich       = dich;
    f->dich_max   = dich_max;
    f->dich_n     = 0;
    f->arena      = arena;
    f->arena_max  = arena_max;
    f->arena_n    = 0;
    f->ordine     = 0;
    f->troncato   = 0;

    if (arena && arena_max > 0) { arena[0] = '\0'; f->arena_n = 1; }
}

/* -----------------------------------------------------------------------------
 * I colori
 * --------------------------------------------------------------------------- */
typedef struct { const char *nome; unsigned int argb; } ColoreNoto;

static const ColoreNoto COLORI[] = {
    { "black",   0xFF000000u }, { "white",   0xFFFFFFFFu },
    { "red",     0xFFFF0000u }, { "green",   0xFF008000u },
    { "blue",    0xFF0000FFu }, { "yellow",  0xFFFFFF00u },
    { "cyan",    0xFF00FFFFu }, { "aqua",    0xFF00FFFFu },
    { "magenta", 0xFFFF00FFu }, { "fuchsia", 0xFFFF00FFu },
    { "gray",    0xFF808080u }, { "grey",    0xFF808080u },
    { "silver",  0xFFC0C0C0u }, { "maroon",  0xFF800000u },
    { "olive",   0xFF808000u }, { "navy",    0xFF000080u },
    { "purple",  0xFF800080u }, { "teal",    0xFF008080u },
    { "lime",    0xFF00FF00u }, { "orange",  0xFFFFA500u },
    { 0, 0 }
};

static int esa(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Rende 1 e posa il colore, 0 se non l'ha capito. */
static int leggi_colore(const char *v, unsigned int n, unsigned int *out);

/* ! IL LETTORE DI COLORI E' PUBBLICO DAL 25 AGOSTO 2026, e non e' un capriccio
 * di simmetria: l'HTML vecchio scrive i colori negli ATTRIBUTI — `bgcolor`,
 * `text`, `link` — e non nei fogli di stile. Sono «suggerimenti di
 * presentazione», stanno al gradino piu' basso della cascata, e mezzo web li
 * usa ancora: la barra arancione di Hacker News e' un `bgcolor` su una
 * `<table>`. Chi li deve leggere e' il browser, e senza questa funzione
 * dovrebbe riscriversi un parser di colori — cioe' averne due che divergono.
 */
int css_colore(const char *v, unsigned int n, unsigned int *out)
{
    return leggi_colore(v, n, out);
}

static int leggi_colore(const char *v, unsigned int n, unsigned int *out)
{
    unsigned int i;

    if (n == 0) return 0;

    if (v[0] == '#') {
        int c[6], k;

        /* ! #abc E' #aabbcc, e la forma corta si incontra piu' della lunga nei
         * fogli scritti a mano. */
        if (n == 4) {
            for (k = 0; k < 3; k++) {
                int h = esa((unsigned char)v[k + 1]);
                if (h < 0) return 0;
                c[k * 2] = c[k * 2 + 1] = h;
            }
        } else if (n == 7) {
            for (k = 0; k < 6; k++) {
                c[k] = esa((unsigned char)v[k + 1]);
                if (c[k] < 0) return 0;
            }
        } else {
            return 0;
        }

        *out = 0xFF000000u
             | (unsigned int)((c[0] << 4 | c[1]) << 16)
             | (unsigned int)((c[2] << 4 | c[3]) << 8)
             | (unsigned int)( c[4] << 4 | c[5]);
        return 1;
    }

    for (i = 0; COLORI[i].nome; i++) {
        const char *p = COLORI[i].nome;
        unsigned int k = 0;

        while (k < n && p[k] && minusc((unsigned char)v[k]) == p[k]) k++;
        if (k == n && p[k] == '\0') { *out = COLORI[i].argb; return 1; }
    }
    return 0;
}

/* Una misura in pixel. `12px` e `12` valgono uguale; tutto il resto no.
 *
 * ! LE UNITA' RELATIVE SI RIFIUTANO INVECE DI ESSERE INDOVINATE. Un `2em`
 * preso per 2 pixel darebbe un testo illeggibile e sembrerebbe un difetto
 * dell'impaginazione; rifiutato, la proprieta' resta non dichiarata e si eredita
 * — che e' il comportamento giusto per un valore che non si sa calcolare. */
static int leggi_misura(const char *v, unsigned int n, int *out)
{
    unsigned int i = 0;
    int          segno = 1, val = 0, cifre = 0;

    if (i < n && (v[i] == '-' || v[i] == '+')) { if (v[i] == '-') segno = -1; i++; }
    while (i < n && v[i] >= '0' && v[i] <= '9') {
        val = val * 10 + (v[i] - '0'); i++; cifre = 1;
    }
    if (!cifre) return 0;

    /* la frazione si legge e si butta: qui i pixel sono interi */
    if (i < n && v[i] == '.') { i++; while (i < n && v[i] >= '0' && v[i] <= '9') i++; }

    if (i == n) { *out = segno * val; return 1; }
    if (i + 2 == n && minusc((unsigned char)v[i]) == 'p' &&
        minusc((unsigned char)v[i + 1]) == 'x') { *out = segno * val; return 1; }
    return 0;
}

/* -----------------------------------------------------------------------------
 * font-family: da un elenco di nomi a una delle tre facce che abbiamo
 *
 * ! SI SCORRE L'ELENCO E CI SI FERMA AL PRIMO NOME CHE SI CONOSCE. E' l'ordine
 * di preferenza del CSS: «Georgia, Times, serif» vuol dire «Georgia se ce
 * l'hai, se no Times, se no una con le grazie qualunque». Fermarsi al primo
 * riconosciuto rispetta quella scala; guardare solo la generica in coda
 * butterebbe via l'informazione piu' precisa.
 *
 * ! E I NOMI VERI CONTANO QUANTO LE GENERICHE, perche' meta' del web non
 * scrive mai la generica: «font-family: Courier New» e basta e' comunissimo, e
 * senza i nomi propri quel testo resterebbe proporzionale — cioe' il caso in
 * cui il monospazio serve davvero.
 * --------------------------------------------------------------------------- */
typedef struct { const char *nome; unsigned char fam; } FamNota;

static const FamNota FAMIGLIE[] = {
    /* monospazio */
    { "monospace",       CSS_FAM_FISSO }, { "courier",     CSS_FAM_FISSO },
    { "courier new",     CSS_FAM_FISSO }, { "consolas",    CSS_FAM_FISSO },
    { "monaco",          CSS_FAM_FISSO }, { "menlo",       CSS_FAM_FISSO },
    { "liberation mono", CSS_FAM_FISSO }, { "dejavu sans mono", CSS_FAM_FISSO },
    { "lucida console",  CSS_FAM_FISSO }, { "andale mono", CSS_FAM_FISSO },
    { "ui-monospace",    CSS_FAM_FISSO },
    /* senza grazie */
    { "sans-serif",      CSS_FAM_SANS  }, { "arial",       CSS_FAM_SANS  },
    { "helvetica",       CSS_FAM_SANS  }, { "helvetica neue", CSS_FAM_SANS },
    { "verdana",         CSS_FAM_SANS  }, { "tahoma",      CSS_FAM_SANS  },
    { "segoe ui",        CSS_FAM_SANS  }, { "roboto",      CSS_FAM_SANS  },
    { "liberation sans", CSS_FAM_SANS  }, { "dejavu sans", CSS_FAM_SANS  },
    { "open sans",       CSS_FAM_SANS  }, { "noto sans",   CSS_FAM_SANS  },
    { "system-ui",       CSS_FAM_SANS  }, { "ui-sans-serif", CSS_FAM_SANS },
    { "calibri",         CSS_FAM_SANS  }, { "trebuchet ms", CSS_FAM_SANS },
    /* con le grazie */
    { "serif",           CSS_FAM_SERIF }, { "times",       CSS_FAM_SERIF },
    { "times new roman", CSS_FAM_SERIF }, { "georgia",     CSS_FAM_SERIF },
    { "garamond",        CSS_FAM_SERIF }, { "liberation serif", CSS_FAM_SERIF },
    { "dejavu serif",    CSS_FAM_SERIF }, { "noto serif",  CSS_FAM_SERIF },
    { "ui-serif",        CSS_FAM_SERIF }, { "cambria",     CSS_FAM_SERIF },
    { 0, 0 }
};

/* Un nome dell'elenco, senza spazi ai bordi e senza virgolette. */
static int fam_uguale(const char *v, unsigned int n, const char *nome)
{
    unsigned int i = 0;

    while (nome[i]) {
        if (i >= n) return 0;
        if (minusc((unsigned char)v[i]) != nome[i]) return 0;
        i++;
    }
    return i == n;
}

static int leggi_famiglia(const char *v, unsigned int n, unsigned int *out)
{
    unsigned int i = 0;

    while (i < n) {
        unsigned int a, b, k;

        while (i < n && (v[i] == ' ' || v[i] == '\t' ||
                         v[i] == '"' || v[i] == '\'')) i++;
        a = i;
        while (i < n && v[i] != ',') i++;
        b = i;
        if (i < n) i++;                      /* la virgola */

        /* via gli spazi e le virgolette in coda */
        while (b > a && (v[b - 1] == ' ' || v[b - 1] == '\t' ||
                         v[b - 1] == '"' || v[b - 1] == '\'')) b--;
        if (b <= a) continue;

        for (k = 0; FAMIGLIE[k].nome; k++)
            if (fam_uguale(v + a, b - a, FAMIGLIE[k].nome)) {
                *out = FAMIGLIE[k].fam;
                return 1;
            }
    }

    /* ! NESSUN NOME RICONOSCIUTO: LA DICHIARAZIONE SI BUTTA, e non si ripiega
     * sul serif. Buttarla lascia in piedi quella ereditata, che e' quasi sempre
     * piu' vicina al vero di una scelta presa a caso da noi. */
    return 0;
}

/* -----------------------------------------------------------------------------
 * I nomi delle proprieta'
 * --------------------------------------------------------------------------- */
typedef struct { const char *nome; unsigned short codice; } PropNota;

static const PropNota PROPRIETA[] = {
    { "color",            CSS_P_COLORE     },
    { "background-color", CSS_P_SFONDO     },
    { "background",       CSS_P_SFONDO     },
    { "font-weight",      CSS_P_PESO       },
    { "font-style",       CSS_P_STILE      },
    { "font-size",        CSS_P_CORPO      },
    { "text-align",       CSS_P_ALLINEA    },
    { "display",          CSS_P_DISPLAY    },
    { "margin-top",       CSS_P_MARG_SOPRA },
    { "margin-right",     CSS_P_MARG_DX    },
    { "margin-bottom",    CSS_P_MARG_SOTTO },
    { "margin-left",      CSS_P_MARG_SX    },
    { "font-family",      CSS_P_FAMIGLIA   },
    { 0, 0 }
};

static int prop_codice(const char *s, unsigned int n, unsigned short *out)
{
    unsigned int i;

    for (i = 0; PROPRIETA[i].nome; i++) {
        const char  *p = PROPRIETA[i].nome;
        unsigned int k = 0;

        while (k < n && p[k] && minusc((unsigned char)s[k]) == p[k]) k++;
        if (k == n && p[k] == '\0') { *out = PROPRIETA[i].codice; return 1; }
    }
    return 0;
}

/* Da testo del valore al numero che finisce in CssDich. Rende 0 se il valore
 * non si capisce: la dichiarazione allora si butta, non il resto della regola. */
static int leggi_valore(unsigned short prop, const char *v, unsigned int n,
                        unsigned int *out)
{
    unsigned int c;
    int          m;

    switch (prop) {
    case CSS_P_COLORE:
    case CSS_P_SFONDO:
        if (!leggi_colore(v, n, &c)) return 0;
        *out = c;
        return 1;

    case CSS_P_CORPO:
    case CSS_P_MARG_SOPRA: case CSS_P_MARG_DX:
    case CSS_P_MARG_SOTTO: case CSS_P_MARG_SX:
        if (!leggi_misura(v, n, &m)) return 0;
        if (m < -20000) m = -20000;
        if (m >  20000) m =  20000;
        *out = (unsigned int)(m + 32768);      /* senza segno per il campo */
        return 1;

    case CSS_P_PESO: {
        int peso = 0;

        if (leggi_misura(v, n, &peso)) { *out = (peso >= 600) ? 1u : 0u; return 1; }
        if (n == 4 && minusc((unsigned char)v[0]) == 'b') { *out = 1u; return 1; }
        if (n == 6 && minusc((unsigned char)v[0]) == 'n') { *out = 0u; return 1; }
        return 0;
    }

    case CSS_P_STILE:
        if (n == 6 && minusc((unsigned char)v[0]) == 'i') { *out = 1u; return 1; }
        if (n == 7 && minusc((unsigned char)v[0]) == 'o') { *out = 1u; return 1; }
        if (n == 6 && minusc((unsigned char)v[0]) == 'n') { *out = 0u; return 1; }
        return 0;

    case CSS_P_ALLINEA:
        if (minusc((unsigned char)v[0]) == 'l') { *out = CSS_ALL_SX;     return 1; }
        if (minusc((unsigned char)v[0]) == 'c') { *out = CSS_ALL_CENTRO; return 1; }
        if (minusc((unsigned char)v[0]) == 'r') { *out = CSS_ALL_DX;     return 1; }
        return 0;

    case CSS_P_FAMIGLIA:
        return leggi_famiglia(v, n, out);

    case CSS_P_DISPLAY:
        if (n == 4 && minusc((unsigned char)v[0]) == 'n') { *out = CSS_DISPLAY_NIENTE; return 1; }
        if (n == 5 && minusc((unsigned char)v[0]) == 'b') { *out = CSS_DISPLAY_BLOCCO; return 1; }
        if (n == 6 && minusc((unsigned char)v[0]) == 'i') { *out = CSS_DISPLAY_INLINE; return 1; }
        return 0;

    default:
        return 0;
    }
}

/* -----------------------------------------------------------------------------
 * Le dichiarazioni: «prop: valore; prop: valore»
 *
 * Un lettore solo per tutt'e due i posti in cui compaiono — dentro le graffe di
 * una regola e dentro un attributo `style` — perche' sono la stessa cosa, e due
 * lettori sarebbero due modi di sbagliarla.
 * --------------------------------------------------------------------------- */
/* ! I COMMENTI STANNO ANCHE DENTRO UN SELETTORE, e il primo giro non li
 * prevedeva: una regola con un commento fra il nome del tag e la graffa veniva
 * scartata INTERA, perche' la barra non e' un carattere da nome. Trovato dal
 * banco di prova sull'host, non guardando il codice. Qui contano come spazio,
 * che e' quello che sono.
 *
 * ! E LA CONSEGUENZA ERA MUTA: nessun errore, nessun avviso — solo una regola
 * che non si applicava. E' la forma peggiore, perche' si va a cercare il
 * guasto nel foglio invece che nel lettore. */
static void salta_vuoto(const char *t, unsigned int *i, unsigned int fine)
{
    for (;;) {
        while (*i < fine && spazio((unsigned char)t[*i])) (*i)++;
        if (*i + 1 < fine && t[*i] == '/' && t[*i + 1] == '*') {
            *i += 2;
            while (*i + 1 < fine && !(t[*i] == '*' && t[*i + 1] == '/')) (*i)++;
            *i = (*i + 1 < fine) ? *i + 2 : fine;
            continue;
        }
        return;
    }
}


typedef struct {
    const char  *t;
    unsigned int i, n;
} DichIter;

static int dich_prossima(DichIter *it, unsigned short *prop, unsigned int *val)
{
    while (it->i < it->n) {
        unsigned int pi, pf, vi, vf;

        for (;;) {
            unsigned int prima = it->i;

            salta_vuoto(it->t, &it->i, it->n);
            while (it->i < it->n && it->t[it->i] == ';') it->i++;
            if (it->i == prima) break;
        }
        if (it->i >= it->n) return 0;

        pi = it->i;
        while (it->i < it->n && nomeok((unsigned char)it->t[it->i])) it->i++;
        pf = it->i;

        while (it->i < it->n && spazio((unsigned char)it->t[it->i])) it->i++;
        if (it->i >= it->n || it->t[it->i] != ':') {
            /* Non e' una dichiarazione: si salta fino al ';' e si riprova. */
            while (it->i < it->n && it->t[it->i] != ';') it->i++;
            continue;
        }
        it->i++;

        salta_vuoto(it->t, &it->i, it->n);
        vi = it->i;
        /* ! UN COMMENTO CHIUDE IL VALORE, esattamente come il punto e virgola:
         * un commento in coda a una dichiarazione finirebbe dentro il valore e
         * ne farebbe fallire la lettura, cioe' una nota innocua spegnerebbe la
         * proprieta'. */
        while (it->i < it->n && it->t[it->i] != ';' &&
               !(it->i + 1 < it->n && it->t[it->i] == '/' &&
                 it->t[it->i + 1] == '*')) it->i++;
        vf = it->i;
        while (it->i < it->n && it->t[it->i] != ';') it->i++;
        while (vf > vi && spazio((unsigned char)it->t[vf - 1])) vf--;

        /* ! «!important» SI TOGLIE E SI IGNORA, e va detto: qui non c'e' il
         * livello in piu' della cascata che gli spetta. Toglierlo dal valore
         * serve a non far fallire la lettura del valore stesso — altrimenti
         * `color: red !important` non sarebbe nemmeno rosso. */
        {
            unsigned int k;

            for (k = vi; k < vf; k++) {
                if (it->t[k] != '!') continue;
                vf = k;
                while (vf > vi && spazio((unsigned char)it->t[vf - 1])) vf--;
                break;
            }
        }

        if (pf > pi && vf > vi && prop_codice(it->t + pi, pf - pi, prop) &&
            leggi_valore(*prop, it->t + vi, vf - vi, val))
            return 1;
        /* valore o proprieta' non capiti: si butta questa e si va avanti */
    }
    return 0;
}

/* Posa una dichiarazione su uno stile. ! LO SWITCH E' COMPLETO APPOSTA: con
 * -Wall il compilatore segnala il giorno che si aggiunge un CSS_P_ e ci si
 * dimentica di questo punto. */
static void css_posa(CssStile *s, unsigned short prop, unsigned int val)
{
    switch (prop) {
    case CSS_P_COLORE:     s->colore = val;                            break;
    case CSS_P_SFONDO:     s->sfondo = val;                            break;
    case CSS_P_PESO:       s->grassetto = (unsigned char)val;          break;
    case CSS_P_STILE:      s->corsivo = (unsigned char)val;            break;
    case CSS_P_FAMIGLIA:   s->famiglia = (unsigned char)val;           break;
    case CSS_P_CORPO:      s->corpo = (short)((int)val - 32768);       break;
    case CSS_P_ALLINEA:    s->allineamento = (unsigned char)val;       break;
    case CSS_P_DISPLAY:    s->display = (unsigned char)val;            break;
    case CSS_P_MARG_SOPRA: s->margine[0] = (short)((int)val - 32768);  break;
    case CSS_P_MARG_DX:    s->margine[1] = (short)((int)val - 32768);  break;
    case CSS_P_MARG_SOTTO: s->margine[2] = (short)((int)val - 32768);  break;
    case CSS_P_MARG_SX:    s->margine[3] = (short)((int)val - 32768);  break;
    default: break;
    }
}

void css_stile_inline(const char *testo, unsigned int n, CssStile *s)
{
    DichIter       it;
    unsigned short prop;
    unsigned int   val;

    if (!testo || !s) return;

    it.t = testo; it.i = 0; it.n = n;
    while (dich_prossima(&it, &prop, &val)) css_posa(s, prop, val);
}

/* -----------------------------------------------------------------------------
 * I selettori
 *
 * ! UN SELETTORE PIU' LUNGO DI CSS_SEL_PEZZI_MAX SI SCARTA, NON SI ACCORCIA, e
 * la differenza e' tutta: tenere gli ultimi quattro pezzi di «body div ul li a»
 * darebbe un selettore che corrisponde a PIU' elementi dell'originale, cioe'
 * colori applicati dove non dovevano. Scartato, quella regola semplicemente non
 * si applica — meno stile, mai stile sbagliato.
 * --------------------------------------------------------------------------- */
/* -----------------------------------------------------------------------------
 * I selettori — compounds, combinators, lists (24 September 2026)
 *
 * ! WHAT CHROMIUM AND GECKO BOTH DO, AND THE SPEC SAYS: a selector is read
 * into compounds joined by combinators, a list is split on its top-level
 * commas, and an INVALID selector makes the whole rule invalid. What this
 * reader knows and cannot honour — :hover, ::before, a chain longer than
 * CSS_SEL_PEZZI_MAX — is not invalid: that selector just never matches, and
 * the others of its list still apply.
 * --------------------------------------------------------------------------- */
static int pari(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int inizia(const char *a, const char *p)
{
    while (*p && *a == *p) { a++; p++; }
    return *p == '\0';
}

/* A raw copy into the arena, zeros included: how the lists are stored. */
static unsigned int arena_crudo(CssFoglio *f, const char *s, unsigned int n)
{
    unsigned int inizio = f->arena_n, i;

    if (n == 0) return 0;
    if (f->arena_n + n > f->arena_max) { f->troncato = 1; return 0; }
    for (i = 0; i < n; i++) f->arena[f->arena_n++] = s[i];
    return inizio;
}

/* ! A NAME, WITH ITS ESCAPES TAKEN OUT. `.md\:flex` and `.w-1\/2` are how
 * utility frameworks write their classes, and `\31 0` a class that starts with
 * a digit: until today every one of those rules was thrown away. A hex escape
 * becomes its character in UTF-8, eaten with the one space that may end it.
 * Returns the length written in `out` (0 = no name here). */
static unsigned int leggi_nome(const char *t, unsigned int *pi, unsigned int fine,
                               char *out, unsigned int max)
{
    unsigned int i = *pi, n = 0;

    while (i < fine && n + 4 < max) {
        unsigned char c = (unsigned char)t[i];

        if (c == '\\' && i + 1 < fine) {
            if (esa((unsigned char)t[i + 1]) >= 0) {
                unsigned long cp = 0;
                int k = 0;

                i++;
                while (i < fine && k < 6 && esa((unsigned char)t[i]) >= 0) {
                    cp = cp * 16 + (unsigned long)esa((unsigned char)t[i]); i++; k++;
                }
                if (i < fine && spazio((unsigned char)t[i])) i++;
                if (cp == 0 || cp > 0x10FFFF) cp = 0xFFFD;
                if (cp < 0x80) out[n++] = (char)cp;
                else if (cp < 0x800) { out[n++] = (char)(0xC0 | (cp >> 6)); out[n++] = (char)(0x80 | (cp & 63)); }
                else if (cp < 0x10000) { out[n++] = (char)(0xE0 | (cp >> 12)); out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63)); }
                else { out[n++] = (char)(0xF0 | (cp >> 18)); out[n++] = (char)(0x80 | ((cp >> 12) & 63)); out[n++] = (char)(0x80 | ((cp >> 6) & 63)); out[n++] = (char)(0x80 | (cp & 63)); }
                continue;
            }
            out[n++] = t[i + 1];
            i += 2;
            continue;
        }
        if (!nomeok(c) && c < 0x80) break;
        out[n++] = (char)c;
        i++;
    }
    *pi = i;
    out[n] = '\0';
    return n;
}

#define LIS_MAX 512     /* the classes, or the attribute tests, of ONE compound */

static int lista_metti(char *l, unsigned int *n, const char *s, unsigned int k, int minuscolo)
{
    unsigned int i;

    if (*n + k + 2 >= LIS_MAX) return 0;
    for (i = 0; i < k; i++) l[(*n)++] = minuscolo ? (char)minusc((unsigned char)s[i]) : s[i];
    return 1;
}

/* an+b for :nth-*(): "odd", "even", "3", "n", "-n+3", "2n-1". */
static int leggi_nth(const char *t, unsigned int a, unsigned int b, short *pa, short *pb)
{
    int segno = 1, v = 0, cifre = 0, na = 0, nb = 0;

    while (a < b && spazio((unsigned char)t[a])) a++;
    while (b > a && spazio((unsigned char)t[b - 1])) b--;
    if (b - a == 3 && minusc((unsigned char)t[a]) == 'o' && minusc((unsigned char)t[a + 1]) == 'd' &&
        minusc((unsigned char)t[a + 2]) == 'd') { *pa = 2; *pb = 1; return 1; }
    if (b - a == 4 && minusc((unsigned char)t[a]) == 'e' && minusc((unsigned char)t[a + 1]) == 'v' &&
        minusc((unsigned char)t[a + 2]) == 'e' && minusc((unsigned char)t[a + 3]) == 'n') {
        *pa = 2; *pb = 0; return 1;
    }
    if (a < b && (t[a] == '+' || t[a] == '-')) { if (t[a] == '-') segno = -1; a++; }
    while (a < b && t[a] >= '0' && t[a] <= '9') { v = v * 10 + (t[a] - '0'); a++; cifre++; }
    if (a < b && minusc((unsigned char)t[a]) == 'n') {
        na = segno * (cifre ? v : 1);
        a++;
        while (a < b && spazio((unsigned char)t[a])) a++;
        if (a < b) {
            if (t[a] != '+' && t[a] != '-') return 0;
            segno = (t[a] == '-') ? -1 : 1;
            a++;
            while (a < b && spazio((unsigned char)t[a])) a++;
            v = 0; cifre = 0;
            while (a < b && t[a] >= '0' && t[a] <= '9') { v = v * 10 + (t[a] - '0'); a++; cifre++; }
            if (!cifre) return 0;
            nb = segno * v;
        }
    } else {
        if (!cifre) return 0;
        nb = segno * v;
    }
    if (a != b) return 0;               /* "of S", and anything else */
    *pa = (short)na; *pb = (short)nb;
    return 1;
}

/* Valid pseudo-classes and pseudo-elements that can NEVER match in this
 * layout: nobody hovers or focuses, no content is generated. */
static const char *const MAI[] = {
    "hover", "focus", "active", "visited", "focus-within", "focus-visible",
    "target", "before", "after", "first-line", "first-letter", "selection",
    "placeholder", "placeholder-shown", "marker", "backdrop", "indeterminate",
    "default", "required", "optional", "valid", "invalid", "in-range",
    "out-of-range", "read-only", "read-write", "fullscreen", "is", "where",
    "has", "lang", "dir", "host", "defined", "autofill", "modal",
    "popover-open", "user-invalid", "user-valid", "scope", "target-within", 0
};

/* One compound, from *pi. Returns 1 (read), 2 (read, never matches here) or
 * 0 (not a selector: the rule is invalid). Stops on a space, a combinator or
 * the end.
 *
 * The attribute list holds, for each test: op ('e' = exists, '=', '~', '|',
 * '^', '$', '*'), the name '\0', the value '\0', the flag ('i' or 0); a zero
 * op ends it. */
static int leggi_composto(CssFoglio *f, const char *t, unsigned int *pi,
                          unsigned int fine, CssPezzo *p, unsigned int *peso)
{
    char         cl[LIS_MAX], at[LIS_MAX], nm[128];
    unsigned int ncl = 0, nat = 0, i = *pi, n;
    int          esito = 1, qualcosa = 0;

    p->tipo = p->id = p->classi = p->attr = p->nega = 0;
    p->pseudo = 0; p->nth_a = p->nth_b = 0; p->comb = 0;

    if (i < fine && t[i] == '*') { i++; qualcosa = 1; }
    else if (i < fine && ((nomeok((unsigned char)t[i]) && t[i] != '-') || t[i] == '\\')) {
        n = leggi_nome(t, &i, fine, nm, sizeof(nm));
        if (!n) return 0;
        p->tipo = arena_metti(f, nm, n);
        *peso += 1;
        qualcosa = 1;
    }

    while (i < fine) {
        char c = t[i];

        if (spazio((unsigned char)c) || c == '>' || c == '+' || c == '~' || c == '/') break;

        if (c == '#' || c == '.') {
            i++;
            n = leggi_nome(t, &i, fine, nm, sizeof(nm));
            if (!n) return 0;
            if (c == '#') { p->id = arena_metti(f, nm, n); *peso += 10000; }
            else {
                if (!lista_metti(cl, &ncl, nm, n, 1)) return 2;
                cl[ncl++] = '\0';
                *peso += 100;
            }
            qualcosa = 1;
            continue;
        }

        if (c == '[') {
            char         op = 'e', flag = 0, val[256];
            unsigned int nv = 0;

            i++;
            while (i < fine && spazio((unsigned char)t[i])) i++;
            n = leggi_nome(t, &i, fine, nm, sizeof(nm));
            if (!n) return 0;
            while (i < fine && spazio((unsigned char)t[i])) i++;
            if (i < fine && t[i] != ']') {
                if (t[i] == '=') { op = '='; i++; }
                else if (i + 1 < fine && t[i + 1] == '=' &&
                         (t[i] == '~' || t[i] == '|' || t[i] == '^' || t[i] == '$' || t[i] == '*')) {
                    op = t[i]; i += 2;
                } else return 0;
                while (i < fine && spazio((unsigned char)t[i])) i++;
                if (i < fine && (t[i] == '"' || t[i] == '\'')) {
                    char q = t[i++];

                    while (i < fine && t[i] != q) {
                        if (t[i] == '\\' && i + 1 < fine) i++;
                        if (nv < sizeof(val) - 1) val[nv++] = t[i];
                        i++;
                    }
                    if (i >= fine) return 0;
                    i++;
                } else {
                    nv = leggi_nome(t, &i, fine, val, sizeof(val));
                    if (!nv) return 0;
                }
                val[nv] = '\0';
                while (i < fine && spazio((unsigned char)t[i])) i++;
                if (i < fine && (t[i] == 'i' || t[i] == 'I' || t[i] == 's' || t[i] == 'S')) {
                    if (t[i] == 'i' || t[i] == 'I') flag = 'i';
                    i++;
                    while (i < fine && spazio((unsigned char)t[i])) i++;
                }
            }
            if (i >= fine || t[i] != ']') return 0;
            i++;
            if (nat + n + nv + 5 >= LIS_MAX) return 2;
            at[nat++] = op;
            lista_metti(at, &nat, nm, n, 1);
            at[nat++] = '\0';
            lista_metti(at, &nat, val, nv, 0);
            at[nat++] = '\0';
            at[nat++] = flag;
            *peso += 100;
            qualcosa = 1;
            continue;
        }

        if (c == ':') {
            int          elemento = 0, k;
            unsigned int ba = 0, bf = 0;

            i++;
            if (i < fine && t[i] == ':') { elemento = 1; i++; }
            n = leggi_nome(t, &i, fine, nm, sizeof(nm));
            if (!n) return 0;
            for (k = 0; nm[k]; k++) nm[k] = (char)minusc((unsigned char)nm[k]);
            if (i < fine && t[i] == '(') {      /* the argument, nested parentheses */
                int  liv = 1;
                char q = 0;

                i++; ba = i;
                while (i < fine) {
                    if (q) { if (t[i] == q) q = 0; }
                    else if (t[i] == '"' || t[i] == '\'') q = t[i];
                    else if (t[i] == '(') liv++;
                    else if (t[i] == ')' && --liv == 0) break;
                    i++;
                }
                if (i >= fine) return 0;
                bf = i++;
            }
            qualcosa = 1;
            if (elemento) { esito = 2; continue; }        /* ::anything */
            *peso += 100;

            if      (pari(nm, "first-child"))   p->pseudo |= CSS_PS_PRIMO;
            else if (pari(nm, "last-child"))    p->pseudo |= CSS_PS_ULTIMO;
            else if (pari(nm, "only-child"))    p->pseudo |= CSS_PS_UNICO;
            else if (pari(nm, "first-of-type")) p->pseudo |= CSS_PS_PRIMO | CSS_PS_DI_TIPO;
            else if (pari(nm, "last-of-type"))  p->pseudo |= CSS_PS_ULTIMO | CSS_PS_DI_TIPO;
            else if (pari(nm, "only-of-type"))  p->pseudo |= CSS_PS_UNICO | CSS_PS_DI_TIPO;
            else if (pari(nm, "empty"))         p->pseudo |= CSS_PS_VUOTO;
            else if (pari(nm, "root"))          p->pseudo |= CSS_PS_RADICE;
            else if (pari(nm, "link") || pari(nm, "any-link")) p->pseudo |= CSS_PS_LINK;
            else if (pari(nm, "checked"))       p->pseudo |= CSS_PS_ACCESO;
            else if (pari(nm, "disabled"))      p->pseudo |= CSS_PS_SPENTO;
            else if (pari(nm, "enabled"))       p->pseudo |= CSS_PS_ABILITATO;
            else if (inizia(nm, "nth-") && ba) {
                if (!leggi_nth(t, ba, bf, &p->nth_a, &p->nth_b)) { esito = 2; continue; }
                if      (pari(nm, "nth-child"))        p->pseudo |= CSS_PS_NTH;
                else if (pari(nm, "nth-last-child"))   p->pseudo |= CSS_PS_NTH_ULT;
                else if (pari(nm, "nth-of-type"))      p->pseudo |= CSS_PS_NTH | CSS_PS_DI_TIPO;
                else if (pari(nm, "nth-last-of-type")) p->pseudo |= CSS_PS_NTH_ULT | CSS_PS_DI_TIPO;
                else return 0;
            } else if (pari(nm, "not") && ba) {
                /* ! ONE SIMPLE SELECTOR ONLY — .c, #i, tag, [a...] — kept as
                 * text and read again at match time. Anything longer does not
                 * match here, rather than match wrongly. The specificity is the
                 * argument's: 100 is already counted, right for a class or an
                 * attribute; an id or a type corrects it. */
                unsigned int x = ba, y = bf, j;

                while (x < y && spazio((unsigned char)t[x])) x++;
                while (y > x && spazio((unsigned char)t[y - 1])) y--;
                for (j = x; j < y; j++)
                    if (spazio((unsigned char)t[j]) || t[j] == ',' || t[j] == '>' ||
                        t[j] == ':' || t[j] == '+' || t[j] == '~' || t[j] == '\\') break;
                for (k = (int)x + 1; k < (int)y && j == y; k++)
                    if (t[k] == '.' || t[k] == '#' || (t[k] == '[' && k > (int)x)) j = (unsigned int)k;
                if (j < y || x == y || p->nega) { esito = 2; continue; }
                if (t[x] == '#') *peso += 10000 - 100;
                else if (t[x] != '.' && t[x] != '[' && t[x] != '*') *peso -= 99;
                else if (t[x] == '*') *peso -= 100;
                p->nega = arena_crudo(f, t + x, y - x);
                if (p->nega && !arena_crudo(f, "\0", 1)) p->nega = 0;
                if (!p->nega) esito = 2;
            } else {
                for (k = 0; MAI[k]; k++) if (pari(nm, MAI[k])) break;
                if (MAI[k]) { esito = 2; continue; }
                return 0;                       /* unknown: invalid */
            }
            continue;
        }
        return 0;                               /* a character we cannot read */
    }

    if (!qualcosa) return 0;
    if (ncl) {
        cl[ncl++] = '\0';
        p->classi = arena_crudo(f, cl, ncl);
        if (!p->classi) return 2;
    }
    if (nat) {
        at[nat++] = 0;
        p->attr = arena_crudo(f, at, nat);
        if (!p->attr) return 2;
    }
    *pi = i;
    return esito;
}

/* A whole selector. Same three answers as leggi_composto. */
static int leggi_selettore(CssFoglio *f, const char *t, unsigned int i,
                           unsigned int fine, CssRegola *r)
{
    int           esito = 1;
    unsigned char comb = 0;

    r->n_pezzi     = 0;
    r->peso        = 0;
    r->pezzo_primo = f->pezzi_n;
    salta_vuoto(t, &i, fine);
    if (i >= fine) return 0;

    while (i < fine) {
        int          e;
        unsigned int prima;
        CssPezzo    *p;

        if (r->n_pezzi >= CSS_SEL_PEZZI_MAX) return 2;
        if (f->pezzi_n >= f->pezzi_max) { f->troncato = 1; return 2; }
        p = &f->pezzi[f->pezzi_n];
        e = leggi_composto(f, t, &i, fine, p, &r->peso);
        if (e == 0) return 0;
        if (e == 2) esito = 2;
        p->comb = comb;
        f->pezzi_n++;
        r->n_pezzi++;

        prima = i;
        salta_vuoto(t, &i, fine);
        if (i >= fine) break;
        if (t[i] == '>' || t[i] == '+' || t[i] == '~') {
            comb = (unsigned char)t[i++];
            salta_vuoto(t, &i, fine);
            if (i >= fine) return 0;            /* «div >» */
        } else if (i > prima) {
            comb = ' ';
        } else {
            return 0;
        }
    }
    return r->n_pezzi ? esito : 0;
}

/* Where the next top-level comma of a selector list is, or `fine`: commas in
 * parentheses — :not(a, b) — or quotes — [title="x,y"] — do not split. */
static unsigned int virgola(const char *t, unsigned int i, unsigned int fine)
{
    int  liv = 0;
    char q = 0;

    for (; i < fine; i++) {
        char c = t[i];

        if (q) { if (c == '\\') i++; else if (c == q) q = 0; continue; }
        if (c == '"' || c == '\'') q = c;
        else if (c == '(' || c == '[') liv++;
        else if ((c == ')' || c == ']') && liv > 0) liv--;
        else if (c == ',' && liv == 0) return i;
    }
    return fine;
}

/* -----------------------------------------------------------------------------
 * L'indice delle regole (see CssFoglio)
 * --------------------------------------------------------------------------- */
/* FNV-1a on the kind and the name, lowercased: the arena holds names
 * lowercased, and the element's are lowercased here the same way. */
static unsigned int secchio(char tipo, const char *s)
{
    unsigned int h = 2166136261u;

    h = (h ^ (unsigned char)tipo) * 16777619u;
    while (*s && !spazio((unsigned char)*s)) {
        h = (h ^ (unsigned char)minusc((unsigned char)*s)) * 16777619u;
        s++;
    }
    return h & (CSS_SECCHI - 1);
}

static void indice_metti(CssFoglio *f, int k)
{
    const CssRegola *r = &f->regole[k];
    const CssPezzo  *p = &f->pezzi[r->pezzo_primo + r->n_pezzi - 1];
    int             *testa, *coda;

    if (p->id)          { unsigned int b = secchio('#', f->arena + p->id);     testa = &f->testa[b]; coda = &f->coda[b]; }
    else if (p->classi) { unsigned int b = secchio('.', f->arena + p->classi); testa = &f->testa[b]; coda = &f->coda[b]; }
    else if (p->tipo)   { unsigned int b = secchio('t', f->arena + p->tipo);   testa = &f->testa[b]; coda = &f->coda[b]; }
    else                { testa = &f->uni_testa; coda = &f->uni_coda; }

    /* at the tail: every bucket stays in reading order */
    if (*coda < 0) *testa = k;
    else           f->regole[*coda].seguente = k;
    *coda = k;
}

unsigned int css_analizza(CssFoglio *f, const char *testo, unsigned int n,
                          unsigned char origine)
{
    unsigned int i = 0, fatte = 0;

    if (!f || !f->regole || !f->dich || !f->arena || !testo) return 0;

    while (i < n) {
        unsigned int sel_i, sel_f, gr_i, gr_f, s;

        while (i < n && spazio((unsigned char)testo[i])) i++;
        if (i >= n) break;

        /* I commenti stanno dove capita, anche in mezzo a un selettore. */
        if (i + 1 < n && testo[i] == '/' && testo[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(testo[i] == '*' && testo[i + 1] == '/')) i++;
            i = (i + 1 < n) ? i + 2 : n;
            continue;
        }

        /* ! LE REGOLE @ SI SALTANO PER INTERO, con le loro graffe annidate:
         * `@media` ne ha dentro delle altre, e saltare fino alla prima graffa
         * chiusa lascerebbe il resto del blocco a fare da selettore. */
        if (testo[i] == '@') {
            int liv = 0;

            while (i < n) {
                if (testo[i] == '{') liv++;
                else if (testo[i] == '}') { liv--; if (liv <= 0) { i++; break; } }
                else if (testo[i] == ';' && liv == 0) { i++; break; }
                i++;
            }
            continue;
        }

        sel_i = i;
        while (i < n && testo[i] != '{') i++;
        if (i >= n) break;
        sel_f = i;
        i++;

        gr_i = i;
        while (i < n && testo[i] != '}') i++;
        gr_f = i;
        if (i < n) i++;

        /* Un selettore per volta: «h1, h2, .box» sono tre regole con le stesse
         * dichiarazioni. */
        /* ! AN INVALID SELECTOR INVALIDATES THE WHOLE LIST, as the spec and
         * both engines say: «a, b:nonsense {...}» applies to nothing. So the
         * list is read once to judge it — writing nothing that stays in the
         * arena — and once more to make the rules. */
        {
            unsigned int s2 = sel_i;
            int          valida = 1;

            while (s2 <= sel_f && valida) {
                unsigned int e2 = virgola(testo, s2, sel_f);
                unsigned int prima = f->arena_n, prima_p = f->pezzi_n;
                CssRegola    r2;

                if (leggi_selettore(f, testo, s2, e2, &r2) == 0) valida = 0;
                f->arena_n = prima;
                f->pezzi_n = prima_p;
                s2 = e2 + 1;
            }
            if (!valida) continue;
        }

        s = sel_i;
        while (s <= sel_f) {
            unsigned int e = s;
            CssRegola    r;
            int          primo = -1, ultimo = -1;
            DichIter     it;
            unsigned short prop;
            unsigned int   val;

            e = virgola(testo, s, sel_f);

            /* 1: a rule; 2: valid, never matches here (see the reader) */
            {
                unsigned int prima_a = f->arena_n, prima_p = f->pezzi_n;

                if (leggi_selettore(f, testo, s, e, &r) != 1) {
                    f->arena_n = prima_a;
                    f->pezzi_n = prima_p;
                    s = e + 1;
                    continue;
                }
            }

            if (f->regole_n >= f->regole_max) { f->troncato = 1; return fatte; }

            it.t = testo; it.i = gr_i; it.n = gr_f;
            while (dich_prossima(&it, &prop, &val)) {
                if (f->dich_n >= f->dich_max) { f->troncato = 1; break; }
                f->dich[f->dich_n].proprieta = prop;
                f->dich[f->dich_n].numero    = val;
                f->dich[f->dich_n].prossima  = -1;
                if (primo < 0) primo = (int)f->dich_n;
                else           f->dich[ultimo].prossima = (int)f->dich_n;
                ultimo = (int)f->dich_n;
                f->dich_n++;
            }

            if (primo >= 0) {
                r.origine    = origine;
                r.ordine     = f->ordine++;
                r.prima_dich = primo;
                r.seguente   = -1;
                f->regole[f->regole_n] = r;
                indice_metti(f, (int)f->regole_n);
                f->regole_n++;
                fatte++;
            }

            s = e + 1;
        }
    }
    return fatte;
}

/* -----------------------------------------------------------------------------
 * La corrispondenza
 * --------------------------------------------------------------------------- */

/* Il valore di `class` e' un ELENCO separato da spazi: «box grande scelto». */
static int ha_classe(const char *elenco, const char *voluta)
{
    unsigned int i = 0;

    if (!elenco || !voluta || !voluta[0]) return 0;

    while (elenco[i]) {
        unsigned int a, k;

        while (elenco[i] && spazio((unsigned char)elenco[i])) i++;
        a = i;
        while (elenco[i] && !spazio((unsigned char)elenco[i])) i++;
        if (i == a) break;

        for (k = 0; voluta[k] && a + k < i; k++)
            if (minusc((unsigned char)elenco[a + k]) != voluta[k]) break;
        if (voluta[k] == '\0' && a + k == i) return 1;
    }
    return 0;
}

/* The previous ELEMENT sibling, or -1. The tree knows only the next one. */
static int fratello_prima(const HtmlDoc *d, int n)
{
    int p = d->nodi[n].padre, f, prima = -1;

    if (p < 0) return -1;
    for (f = d->nodi[p].primo_figlio; f >= 0 && f != n; f = d->nodi[f].prossimo)
        if (d->nodi[f].tipo == HTML_ELEMENTO) prima = f;
    return prima;
}

/* Where `n` stands among its element siblings (of the same type, if asked):
 * 1-based from the front, and how many there are. */
static void posto(const HtmlDoc *d, int n, int di_tipo, int *da_capo, int *quanti)
{
    int p = d->nodi[n].padre, f;
    const char *nome = di_tipo ? html_nome(d, n) : 0;

    *da_capo = 0; *quanti = 0;
    if (p < 0) { *da_capo = *quanti = 1; return; }
    for (f = d->nodi[p].primo_figlio; f >= 0; f = d->nodi[f].prossimo) {
        if (d->nodi[f].tipo != HTML_ELEMENTO) continue;
        if (di_tipo && !ug_min(html_nome(d, f), nome)) continue;
        (*quanti)++;
        if (f == n) *da_capo = *quanti;
    }
}

static int nth(int a, int b, int i)
{
    if (a == 0) return i == b;
    return (i - b) % a == 0 && (i - b) / a >= 0;
}

/* Does value `v` pass the test `op voluto` (flag 'i': ignoring case)? */
static int attr_passa(const char *v, char op, const char *voluto, char flag)
{
    unsigned int nv = 0, nw = 0, i;

    if (!v) return 0;
    if (op == 'e') return 1;
    while (v[nv]) nv++;
    while (voluto[nw]) nw++;

#define UGC(a, b) (flag == 'i' ? minusc((unsigned char)(a)) == minusc((unsigned char)(b)) : (a) == (b))
    switch (op) {
    case '=':
        if (nv != nw) return 0;
        for (i = 0; i < nv; i++) if (!UGC(v[i], voluto[i])) return 0;
        return 1;
    case '^':
        if (nw == 0 || nv < nw) return 0;
        for (i = 0; i < nw; i++) if (!UGC(v[i], voluto[i])) return 0;
        return 1;
    case '$':
        if (nw == 0 || nv < nw) return 0;
        for (i = 0; i < nw; i++) if (!UGC(v[nv - nw + i], voluto[i])) return 0;
        return 1;
    case '*': {
        unsigned int a;

        if (nw == 0 || nv < nw) return 0;
        for (a = 0; a + nw <= nv; a++) {
            for (i = 0; i < nw; i++) if (!UGC(v[a + i], voluto[i])) break;
            if (i == nw) return 1;
        }
        return 0;
    }
    case '|':
        if (nv < nw) return 0;
        for (i = 0; i < nw; i++) if (!UGC(v[i], voluto[i])) return 0;
        return nv == nw || v[nw] == '-';
    case '~': {
        unsigned int a = 0, b;

        if (nw == 0) return 0;
        while (a < nv) {
            while (a < nv && spazio((unsigned char)v[a])) a++;
            b = a;
            while (b < nv && !spazio((unsigned char)v[b])) b++;
            if (b - a == nw) {
                for (i = 0; i < nw; i++) if (!UGC(v[a + i], voluto[i])) break;
                if (i == nw) return 1;
            }
            a = b;
        }
        return 0;
    }
    }
#undef UGC
    return 0;
}

/* The attribute tests of a list (see leggi_composto): all must pass. */
static int attr_tutti(const HtmlDoc *d, int nodo, const char *l)
{
    while (*l) {
        char        op = *l++;
        const char *nome = l, *val;
        char        flag;

        while (*l) l++;
        l++;
        val = l;
        while (*l) l++;
        l++;
        flag = *l++;
        if (!attr_passa(html_attr(d, nodo, nome), op, val, flag)) return 0;
    }
    return 1;
}

/* The simple selector inside :not(), read at match time: does it match? */
static int semplice(const HtmlDoc *d, int nodo, const char *s)
{
    char buf[256];
    unsigned int n = 0;

    if (s[0] == '*') return 1;
    if (s[0] == '.' || s[0] == '#') {
        while (s[n + 1] && n < sizeof(buf) - 1) { buf[n] = (char)minusc((unsigned char)s[n + 1]); n++; }
        buf[n] = '\0';
        if (s[0] == '.') return ha_classe(html_attr(d, nodo, "class"), buf);
        { const char *v = html_attr(d, nodo, "id"); return v && ug_min(v, buf); }
    }
    if (s[0] == '[') {
        /* read it with the same reader, into a throw-away sheet */
        CssFoglio    f;
        CssPezzo     p;
        char         ar[LIS_MAX + 16];
        unsigned int i = 0, peso = 0, fine = 0;

        while (s[fine]) fine++;
        f.arena = ar; f.arena_max = sizeof(ar); f.arena_n = 1; f.troncato = 0; ar[0] = '\0';
        if (leggi_composto(&f, s, &i, fine, &p, &peso) != 1 || !p.attr) return 0;
        return attr_tutti(d, nodo, ar + p.attr);
    }
    { const char *nome = html_nome(d, nodo); return nome && ug_min(nome, s); }
}

static int pezzo_combacia(const CssFoglio *f, const HtmlDoc *d, int nodo,
                          const CssPezzo *p)
{
    const char *nome;

    if (nodo < 0 || d->nodi[nodo].tipo != HTML_ELEMENTO) return 0;
    nome = html_nome(d, nodo);

    if (p->tipo && (!nome || !ug_min(nome, f->arena + p->tipo))) return 0;
    if (p->id) {
        const char *v = html_attr(d, nodo, "id");
        if (!v || !ug_min(v, f->arena + p->id)) return 0;
    }
    if (p->classi) {
        const char *c = f->arena + p->classi, *elenco = html_attr(d, nodo, "class");

        while (*c) {
            if (!ha_classe(elenco, c)) return 0;
            while (*c) c++;
            c++;
        }
    }
    if (p->attr && !attr_tutti(d, nodo, f->arena + p->attr)) return 0;
    if (p->nega && semplice(d, nodo, f->arena + p->nega)) return 0;

    if (p->pseudo) {
        unsigned short ps = p->pseudo;
        int di_tipo = (ps & CSS_PS_DI_TIPO) != 0, da_capo, quanti;

        if (ps & (CSS_PS_PRIMO | CSS_PS_ULTIMO | CSS_PS_UNICO | CSS_PS_NTH | CSS_PS_NTH_ULT)) {
            posto(d, nodo, di_tipo, &da_capo, &quanti);
            if ((ps & CSS_PS_PRIMO)   && da_capo != 1) return 0;
            if ((ps & CSS_PS_ULTIMO)  && da_capo != quanti) return 0;
            if ((ps & CSS_PS_UNICO)   && quanti != 1) return 0;
            if ((ps & CSS_PS_NTH)     && !nth(p->nth_a, p->nth_b, da_capo)) return 0;
            if ((ps & CSS_PS_NTH_ULT) && !nth(p->nth_a, p->nth_b, quanti - da_capo + 1)) return 0;
        }
        if (ps & CSS_PS_VUOTO) {
            int f2;

            for (f2 = d->nodi[nodo].primo_figlio; f2 >= 0; f2 = d->nodi[f2].prossimo) {
                if (d->nodi[f2].tipo == HTML_ELEMENTO) return 0;
                if (d->nodi[f2].tipo == HTML_TESTO) {
                    const char *tx = html_testo(d, f2);
                    if (tx && tx[0]) return 0;
                }
            }
        }
        if ((ps & CSS_PS_RADICE) && d->nodi[nodo].padre != d->radice) return 0;
        if (ps & CSS_PS_LINK) {
            if (!nome || !(ug_min(nome, "a") || ug_min(nome, "area")) ||
                !html_attr(d, nodo, "href")) return 0;
        }
        if (ps & CSS_PS_ACCESO) {
            if (nome && ug_min(nome, "option")) { if (!html_attr(d, nodo, "selected")) return 0; }
            else if (!html_attr(d, nodo, "checked")) return 0;
        }
        if (ps & (CSS_PS_SPENTO | CSS_PS_ABILITATO)) {
            int modulo = nome && (ug_min(nome, "input") || ug_min(nome, "button") ||
                                  ug_min(nome, "select") || ug_min(nome, "textarea") ||
                                  ug_min(nome, "option") || ug_min(nome, "fieldset"));
            int spento = html_attr(d, nodo, "disabled") != 0;

            if (!modulo) return 0;
            if ((ps & CSS_PS_SPENTO) && !spento) return 0;
            if ((ps & CSS_PS_ABILITATO) && spento) return 0;
        }
    }
    return 1;
}

/* ! FROM THE RIGHT, AS EVERY ENGINE DOES — and now recursive. The rightmost
 * compound is the element itself: one test, and only if it passes are the
 * others looked for. With descendant combinators alone the first matching
 * ancestor was always right; with `>`, `+`, `~` in the chain it is not
 * («a > b c»: the first `b` above may not be a child of an `a`, a higher
 * one may), so a failure goes back and tries the next candidate. The budget
 * stops a pathological sheet from eating the machine: past it, no match. */
static int da_pezzo(const CssFoglio *f, const HtmlDoc *d, int nodo,
                    const CssRegola *r, int k, int *budget)
{
    int su;

    if (--(*budget) < 0) return 0;
    if (!pezzo_combacia(f, d, nodo, &f->pezzi[r->pezzo_primo + k])) return 0;
    if (k == 0) return 1;

    switch (f->pezzi[r->pezzo_primo + k].comb) {
    case '>':
        su = d->nodi[nodo].padre;
        return su >= 0 && da_pezzo(f, d, su, r, k - 1, budget);
    case '+':
        su = fratello_prima(d, nodo);
        return su >= 0 && da_pezzo(f, d, su, r, k - 1, budget);
    case '~':
        for (su = fratello_prima(d, nodo); su >= 0; su = fratello_prima(d, su))
            if (da_pezzo(f, d, su, r, k - 1, budget)) return 1;
        return 0;
    default:
        for (su = d->nodi[nodo].padre; su >= 0; su = d->nodi[su].padre)
            if (da_pezzo(f, d, su, r, k - 1, budget)) return 1;
        return 0;
    }
}

static int regola_combacia(const CssFoglio *f, const HtmlDoc *d, int nodo,
                           const CssRegola *r)
{
    int budget = 4096;

    if (r->n_pezzi == 0) return 0;
    return da_pezzo(f, d, nodo, r, (int)r->n_pezzi - 1, &budget);
}

#define CSS_CAND_MAX 2048    /* candidate rules for one element */

void css_calcola(const CssFoglio *f, const HtmlDoc *d, int nodo,
                 const CssStile *ereditato, CssStile *out)
{
    unsigned int peso_di[CSS_P_N];
    unsigned int i;
    int          k;

    if (!out) return;
    css_stile_vuoto(out);
    for (i = 0; i < CSS_P_N; i++) peso_di[i] = 0;

    /* ! SI EREDITA SOLO CIO' CHE SI EREDITA DAVVERO. Il colore del testo e il
     * carattere scendono ai figli; lo sfondo, i margini e il `display` no —
     * un paragrafo dentro un riquadro rosso non e' un paragrafo rosso. */
    if (ereditato) {
        out->colore       = ereditato->colore;
        out->corpo        = ereditato->corpo;
        out->grassetto    = ereditato->grassetto;
        out->corsivo      = ereditato->corsivo;
        out->allineamento = ereditato->allineamento;
        out->famiglia     = ereditato->famiglia;   /* il carattere scende */
    }

    if (!f || !d || nodo < 0) return;
    if (d->nodi[nodo].tipo != HTML_ELEMENTO) return;

    {
        /* ! THE CANDIDATES, THEN IN READING ORDER: at equal weight the rule
         * read later wins, and that rule is the one with the higher index.
         * Collected from the buckets of the element's id, classes and type
         * and from the universal list, sorted, duplicates dropped (two
         * classes can share a bucket). Too many: the whole list, as before —
         * slower, never wrong. */
        int          cand[CSS_CAND_MAX];
        int          nc = 0, troppe = 0, a, b;
        const char  *nome = html_nome(d, nodo);
        const char  *idv  = html_attr(d, nodo, "id");
        const char  *cls  = html_attr(d, nodo, "class");
        int          lista[3 + 64], nl = 0, q;

        lista[nl++] = f->uni_testa;
        if (nome) lista[nl++] = f->testa[secchio('t', nome)];
        if (idv && idv[0]) lista[nl++] = f->testa[secchio('#', idv)];
        if (cls) {
            const char *c = cls;

            while (*c && nl < (int)(sizeof(lista) / sizeof(lista[0]))) {
                while (*c && spazio((unsigned char)*c)) c++;
                if (!*c) break;
                lista[nl++] = f->testa[secchio('.', c)];
                while (*c && !spazio((unsigned char)*c)) c++;
            }
        }
        for (q = 0; q < nl && !troppe; q++)
            for (a = lista[q]; a >= 0; a = f->regole[a].seguente) {
                if (nc >= CSS_CAND_MAX) { troppe = 1; break; }
                cand[nc++] = a;
            }
        if (troppe) {
            nc = 0;
            for (a = 0; a < (int)f->regole_n && a < CSS_CAND_MAX; a++) cand[nc++] = a;
            if (f->regole_n > CSS_CAND_MAX) { nc = -1; }
        } else {
            for (a = 1; a < nc; a++) {          /* insertion sort: small lists */
                int v = cand[a];

                for (b = a - 1; b >= 0 && cand[b] > v; b--) cand[b + 1] = cand[b];
                cand[b + 1] = v;
            }
        }

        for (q = 0; nc < 0 ? q < (int)f->regole_n : q < nc; q++) {
            int              ir = (nc < 0) ? q : cand[q];
            const CssRegola *r;
            unsigned int     p1;

            if (nc >= 0 && q > 0 && cand[q] == cand[q - 1]) continue;
            r = &f->regole[ir];
            if (!regola_combacia(f, d, nodo, r)) continue;

        /* ! IL PESO DELL'ORIGINE STA SOPRA QUELLO DEL SELETTORE, e non
         * accanto: un `style=` con un selettore banale deve battere un
         * selettore lunghissimo di un foglio. Sommarli lascerebbe che una
         * specificita' alta scavalchi l'origine, che e' il contrario della
         * cascata. */
        p1 = (unsigned int)r->origine * 1000000u + r->peso + 1u;

        for (k = r->prima_dich; k >= 0; k = f->dich[k].prossima) {
            unsigned short prop = f->dich[k].proprieta;

            if (prop >= CSS_P_N) continue;
            /* «>=» e non «>»: a parita' di peso vince chi arriva dopo, e le
             * regole stanno gia' nell'ordine in cui sono state lette. */
            if (p1 < peso_di[prop]) continue;
            peso_di[prop] = p1;
            css_posa(out, prop, f->dich[k].numero);
        }
        }
    }

    /* ! L'ATTRIBUTO `style` PER ULTIMO E SENZA CONFRONTI, perche' nella
     * cascata sta sopra ogni foglio. Il giorno che ci sara' exjs, le sue
     * assegnazioni andranno DOPO questa riga — vedi CSS_ORIGINE_JS in css.h. */
    {
        const char *st = html_attr(d, nodo, "style");
        unsigned int n = 0;

        if (st) { while (st[n]) n++; css_stile_inline(st, n, out); }
    }
}
