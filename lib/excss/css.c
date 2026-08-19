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
    for (i = 0; i < 4; i++) s->margine[i] = CSS_MISURA_NO;
}

void css_prepara(CssFoglio *f,
                 CssRegola *regole, unsigned int regole_max,
                 CssDich *dich, unsigned int dich_max,
                 char *arena, unsigned int arena_max)
{
    if (!f) return;

    f->regole     = regole;
    f->regole_max = regole_max;
    f->regole_n   = 0;
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
static int leggi_pezzo(CssFoglio *f, const char *t, unsigned int i, unsigned int fine,
                       CssPezzo *p, unsigned int *peso)
{
    int qualcosa = 0;

    p->tipo = p->classe = p->id = 0;

    while (i < fine) {
        unsigned int a;

        if (t[i] == '*') { i++; qualcosa = 1; continue; }

        if (t[i] == '.' || t[i] == '#') {
            char segno = t[i];

            i++;
            a = i;
            while (i < fine && nomeok((unsigned char)t[i])) i++;
            if (i == a) return 0;
            if (segno == '.') { p->classe = arena_metti(f, t + a, i - a); *peso += 100; }
            else              { p->id     = arena_metti(f, t + a, i - a); *peso += 10000; }
            qualcosa = 1;
            continue;
        }

        if (nomeok((unsigned char)t[i])) {
            a = i;
            while (i < fine && nomeok((unsigned char)t[i])) i++;
            p->tipo = arena_metti(f, t + a, i - a);
            *peso += 1;
            qualcosa = 1;
            continue;
        }

        return 0;       /* un carattere che non sappiamo leggere */
    }
    return qualcosa;
}

/* Un selettore intero, cioe' i suoi pezzi separati da spazi. */
static int leggi_selettore(CssFoglio *f, const char *t, unsigned int i,
                           unsigned int fine, CssRegola *r)
{
    r->n_pezzi = 0;
    r->peso    = 0;

    while (i < fine) {
        unsigned int a;

        salta_vuoto(t, &i, fine);
        if (i >= fine) break;

        /* ! I COMBINATORI «>», «+», «~» NON CI SONO, e la regola si scarta
         * invece di trattarli come uno spazio: «div > p» e «div p» non sono la
         * stessa cosa, e far finta che lo siano applicherebbe lo stile ai
         * nipoti. */
        if (t[i] == '>' || t[i] == '+' || t[i] == '~') return 0;

        a = i;
        while (i < fine && !spazio((unsigned char)t[i]) &&
               t[i] != '>' && t[i] != '+' && t[i] != '~' && t[i] != '/') i++;

        if (r->n_pezzi >= CSS_SEL_PEZZI_MAX) return 0;
        if (!leggi_pezzo(f, t, a, i, &r->pezzo[r->n_pezzi], &r->peso)) return 0;
        r->n_pezzi++;
    }
    return r->n_pezzi > 0;
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
        s = sel_i;
        while (s <= sel_f) {
            unsigned int e = s;
            CssRegola    r;
            int          primo = -1, ultimo = -1;
            DichIter     it;
            unsigned short prop;
            unsigned int   val;

            while (e < sel_f && testo[e] != ',') e++;

            if (!leggi_selettore(f, testo, s, e, &r)) { s = e + 1; continue; }

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
                f->regole[f->regole_n++] = r;
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

static int pezzo_combacia(const CssFoglio *f, const HtmlDoc *d, int nodo,
                          const CssPezzo *p)
{
    if (nodo < 0 || d->nodi[nodo].tipo != HTML_ELEMENTO) return 0;

    if (p->tipo) {
        const char *n = html_nome(d, nodo);
        if (!n || !ug_min(n, f->arena + p->tipo)) return 0;
    }
    if (p->classe) {
        if (!ha_classe(html_attr(d, nodo, "class"), f->arena + p->classe)) return 0;
    }
    if (p->id) {
        const char *v = html_attr(d, nodo, "id");
        if (!v || !ug_min(v, f->arena + p->id)) return 0;
    }
    return 1;
}

/* ! LA CATENA SI RISALE DAL FONDO, ed e' l'unico modo che non esplode: il
 * pezzo piu' a destra e' l'elemento stesso — una prova sola — e solo se quella
 * passa si cercano gli antenati. Partendo da sinistra si dovrebbero provare
 * tutti i discendenti di ogni candidato. */
static int regola_combacia(const CssFoglio *f, const HtmlDoc *d, int nodo,
                           const CssRegola *r)
{
    int k, su;

    if (r->n_pezzi == 0) return 0;
    if (!pezzo_combacia(f, d, nodo, &r->pezzo[r->n_pezzi - 1])) return 0;

    su = d->nodi[nodo].padre;
    for (k = (int)r->n_pezzi - 2; k >= 0; k--) {
        int trovato = 0;

        while (su >= 0) {
            if (pezzo_combacia(f, d, su, &r->pezzo[k])) { trovato = 1; break; }
            su = d->nodi[su].padre;
        }
        if (!trovato) return 0;
        su = d->nodi[su].padre;
    }
    return 1;
}

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
    }

    if (!f || !d || nodo < 0) return;
    if (d->nodi[nodo].tipo != HTML_ELEMENTO) return;

    for (i = 0; i < f->regole_n; i++) {
        const CssRegola *r = &f->regole[i];
        unsigned int     p1;

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

    /* ! L'ATTRIBUTO `style` PER ULTIMO E SENZA CONFRONTI, perche' nella
     * cascata sta sopra ogni foglio. Il giorno che ci sara' exjs, le sue
     * assegnazioni andranno DOPO questa riga — vedi CSS_ORIGINE_JS in css.h. */
    {
        const char *st = html_attr(d, nodo, "style");
        unsigned int n = 0;

        if (st) { while (st[n]) n++; css_stile_inline(st, n, out); }
    }
}
