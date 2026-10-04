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
/* Confronto fra due stringhe corte gia' in minuscolo (la libc di chi usa
 * questa libreria non e' detto che ci sia: excss si compila anche sull'host). */
static int strcmp_c(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

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
    s->visibile     = CSS_FORSE;
    for (i = 0; i < 4; i++) {
        s->bordo[i]       = CSS_MISURA_NO;
        s->bordo_stile[i] = CSS_FORSE;
        s->bordo_col[i]   = CSS_NIENTE;
        s->imbottitura[i] = CSS_MISURA_NO;
    }
    s->larghezza      = CSS_MISURA_NO;
    s->larghezza_max  = CSS_MISURA_NO;
    s->larghezza_min  = CSS_MISURA_NO;
    s->larghezza_perc = 0;
    s->scatola_bordo  = 0;
    s->galleggia      = CSS_GALLEGGIA_NO;
    s->posizione      = CSS_POS_STATICA;
    for (i = 0; i < 4; i++) s->pos[i] = CSS_MISURA_NO;
    s->flex_colonna   = 0;
    s->flex_a_capo    = 0;
    s->flex_cresce    = 0;
    s->pulisci        = 0;
    s->giustifica     = CSS_GIU_INIZIO;
    s->allinea_voci   = CSS_ALV_STIRA;
    s->spazio_riga    = CSS_MISURA_NO;
    s->spazio_col     = CSS_MISURA_NO;
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

static int leggi_colore_grezzo(const char *v, unsigned int n, unsigned int *out);

/* ! IL BIANCO ERA «NESSUN COLORE» (trovato il 28 settembre 2026). In ARGB il
 * bianco opaco e' 0xFFFFFFFF, cioe' proprio CSS_NIENTE: `color: #fff` si
 * leggeva «non detto», e il testo bianco delle barre scure dei siti usciva
 * NERO — nero su scuro, cioe' niente. Visto con prova_disposizione.sh: il
 * testo bianco del riquadro blu era nero. Il bianco diventa 0xFFFFFFFE, un
 * blu di 254 che a vederlo e' lo stesso bianco. */
static int leggi_colore(const char *v, unsigned int n, unsigned int *out)
{
    if (!leggi_colore_grezzo(v, n, out)) return 0;
    if (*out == CSS_NIENTE) *out = 0xFFFFFFFEu;
    return 1;
}

static int leggi_colore_grezzo(const char *v, unsigned int n, unsigned int *out)
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
 * Le unita' relative: em, rem, % (@NAV-UNITA, 28 settembre 2026)
 *
 * ! UNA MISURA RELATIVA NON SI RISOLVE QUANDO SI LEGGE IL FOGLIO: 2em vale 30
 * pixel sotto un titolo e 26 sotto un paragrafo. Si scrive nel CssDich cosi'
 * com'e' — il valore in centesimi, e l'unita' — e diventa pixel in
 * css_calcola(), quando lo stile del padre e' noto (vedi risolvi_relative).
 *
 * pt e vw invece sono assolute rispetto a cose che si sanno gia' (il punto
 * tipografico e la larghezza della finestra delle @media): si convertono
 * subito, e dal numero non si distinguono dai px.
 * --------------------------------------------------------------------------- */
#define REL_BIT     0x80000000u
#define REL_EM      1u
#define REL_REM     2u
#define REL_PERC    3u
#define REL_ZERO    0x800000u            /* centesimi con segno, spostati */

static int g_media_w;                    /* la larghezza delle @media, piu' giu' */
static int g_opaca = 1, g_punta = 1;     /* vedi nascosto_applica() */
static int g_segno = 2;                  /* vedi css_segno_lista() in css.h */

static unsigned int rel_codifica(unsigned int unita, int centesimi)
{
    if (centesimi < -800000) centesimi = -800000;
    if (centesimi >  800000) centesimi =  800000;
    return REL_BIT | (unita << 24) | ((unsigned int)(centesimi + (int)REL_ZERO) & 0xFFFFFFu);
}

/* Una lunghezza, codificata per il campo CssDich.numero: px (e pt, vw) come
 * sempre — m + 32768 — oppure relativa con REL_BIT. Rende 0 se non si capisce.
 * `neg` = 0 rifiuta i negativi (padding, bordi). */
static int leggi_lunghezza(const char *v, unsigned int n, int neg, unsigned int *out)
{
    unsigned int i = 0, k;
    int segno = 1, cent = 0, cifre = 0, dec = 0;
    char u[4];

    if (i < n && (v[i] == '-' || v[i] == '+')) { if (v[i] == '-') segno = -1; i++; }
    while (i < n && v[i] >= '0' && v[i] <= '9') { cent = cent * 10 + (v[i] - '0'); i++; cifre = 1; if (cent > 1000000) return 0; }
    cent *= 100;
    if (i < n && v[i] == '.') {
        i++;
        while (i < n && v[i] >= '0' && v[i] <= '9') {
            if (dec < 2) cent += (v[i] - '0') * (dec == 0 ? 10 : 1);
            dec++; i++; cifre = 1;
        }
    }
    if (!cifre) return 0;
    cent *= segno;
    if (!neg && cent < 0) return 0;

    for (k = 0; i < n && k < 3; i++, k++) u[k] = (char)minusc((unsigned char)v[i]);
    if (i < n) return 0;                 /* un'unita' piu' lunga di tre lettere */
    u[k] = 0;

    if (k == 0 || !strcmp_c(u, "px")) {  /* il numero nudo vale px (e 0 e' 0) */
        int m = cent / 100;
        if (m < -20000) m = -20000;
        if (m >  20000) m =  20000;
        *out = (unsigned int)(m + 32768);
        return 1;
    }
    if (!strcmp_c(u, "pt")) { *out = (unsigned int)(cent * 4 / 300 + 32768); return 1; }   /* 1pt = 4/3 px */
    if (!strcmp_c(u, "vw")) { *out = (unsigned int)(g_media_w * cent / 10000 + 32768); return 1; }
    if (!strcmp_c(u, "em"))  { *out = rel_codifica(REL_EM, cent);   return 1; }
    if (!strcmp_c(u, "rem")) { *out = rel_codifica(REL_REM, cent);  return 1; }
    if (!strcmp_c(u, "%"))   { *out = rel_codifica(REL_PERC, cent); return 1; }
    return 0;
}

/* Il valore in pixel di una lunghezza relativa `num`.
 *   base_em   il corpo che vale 1em: del PADRE per font-size, dell'elemento
 *             per tutto il resto;
 *   base_perc la misura che vale 100%: il corpo del padre per font-size, la
 *             larghezza della finestra per margini e padding.
 * ! LA % DI UN MARGINE E' UN'APPROSSIMAZIONE, e va detta: il CSS la vuole
 * della larghezza del CONTENITORE, che excss non conosce — la sa
 * l'impaginatore, dopo. La finestra e' giusta per i blocchi di primo livello e
 * troppo larga per quelli annidati. */
static int rel_px(unsigned int num, int base_em, int base_perc)
{
    unsigned int unita = (num >> 24) & 0x7Fu;
    int cent = (int)(num & 0xFFFFFFu) - (int)REL_ZERO;
    int base = (unita == REL_EM) ? base_em : (unita == REL_REM) ? CSS_CORPO_PREDEFINITO : base_perc;

    /* em e rem sono volte (1.5em = 150 centesimi di volta), la % e' gia'
     * cento volte piu' grande (150% = 15000 centesimi di punto percentuale). */
    return (int)((long)cent * base / (unita == REL_PERC ? 10000 : 100));
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
    { "visibility",       CSS_P_VISIBILE   },
    { "border-top-width",    CSS_P_BORDO_LARG + 0  },
    { "border-right-width",  CSS_P_BORDO_LARG + 1  },
    { "border-bottom-width", CSS_P_BORDO_LARG + 2  },
    { "border-left-width",   CSS_P_BORDO_LARG + 3  },
    { "border-top-style",    CSS_P_BORDO_STILE + 0 },
    { "border-right-style",  CSS_P_BORDO_STILE + 1 },
    { "border-bottom-style", CSS_P_BORDO_STILE + 2 },
    { "border-left-style",   CSS_P_BORDO_STILE + 3 },
    { "border-top-color",    CSS_P_BORDO_COL + 0   },
    { "border-right-color",  CSS_P_BORDO_COL + 1   },
    { "border-bottom-color", CSS_P_BORDO_COL + 2   },
    { "border-left-color",   CSS_P_BORDO_COL + 3   },
    { "padding-top",         CSS_P_IMBOTTITURA + 0 },
    { "padding-right",       CSS_P_IMBOTTITURA + 1 },
    { "padding-bottom",      CSS_P_IMBOTTITURA + 2 },
    { "padding-left",        CSS_P_IMBOTTITURA + 3 },
    { "width",               CSS_P_LARG            },
    { "max-width",           CSS_P_LARG_MAX        },
    { "min-width",           CSS_P_LARG_MIN        },
    { "box-sizing",          CSS_P_SCATOLA         },
    { "float",               CSS_P_GALLEGGIA       },
    { "position",            CSS_P_POSIZIONE       },
    { "top",                 CSS_P_POS + 0         },
    { "right",               CSS_P_POS + 1         },
    { "bottom",              CSS_P_POS + 2         },
    { "left",                CSS_P_POS + 3         },
    { "flex-direction",      CSS_P_FLEX_DIR        },
    { "flex-wrap",           CSS_P_FLEX_CAPO       },
    { "flex-grow",           CSS_P_FLEX_CRESCE     },
    { "clear",               CSS_P_PULISCI         },
    { "justify-content",     CSS_P_GIUSTIFICA      },
    { "align-items",         CSS_P_ALLINEA_VOCI    },
    { "row-gap",             CSS_P_SPAZIO_RIGA     },
    { "column-gap",          CSS_P_SPAZIO_COL      },
    { "list-style",          CSS_P_SEGNO           },
    { "list-style-type",     CSS_P_SEGNO           },
    { "opacity",             CSS_P_OPACITA         },
    { "pointer-events",      CSS_P_PUNTATORE       },
    { "grid-row-gap",        CSS_P_SPAZIO_RIGA     },
    { "grid-column-gap",     CSS_P_SPAZIO_COL      },
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

/* -----------------------------------------------------------------------------
 * I bordi e il padding (@NAV-BORDER, 28 settembre 2026)
 * --------------------------------------------------------------------------- */

/* v, lungo n, e' la parola `p` (senza badare alle maiuscole)? */
static int parola_e(const char *v, unsigned int n, const char *p)
{
    unsigned int k = 0;

    while (k < n && p[k] && minusc((unsigned char)v[k]) == p[k]) k++;
    return k == n && p[k] == '\0';
}

/* Uno spessore di bordo: una misura, o thin / medium / thick. I tre nomi il
 * CSS non li fissa; 1, 3 e 5 pixel sono quel che fanno tutti i browser. */
static int leggi_spessore(const char *v, unsigned int n, unsigned int *out)
{
    int m;

    if (parola_e(v, n, "thin"))   m = 1;
    else if (parola_e(v, n, "medium")) m = 3;
    else if (parola_e(v, n, "thick"))  m = 5;
    else return leggi_lunghezza(v, n, 0, out);
    *out = (unsigned int)(m + 32768);
    return 1;
}

/* Uno stile di bordo: 0 per none e hidden, 1 per tutti quelli che si vedono. */
static int leggi_stile_bordo(const char *v, unsigned int n, unsigned int *out)
{
    static const char *const visti[] = {
        "solid", "dashed", "dotted", "double", "groove", "ridge", "inset", "outset", 0
    };
    int i;

    if (parola_e(v, n, "none") || parola_e(v, n, "hidden")) { *out = 0; return 1; }
    for (i = 0; visti[i]; i++) if (parola_e(v, n, visti[i])) { *out = 1; return 1; }
    return 0;
}

/* Da testo del valore al numero che finisce in CssDich. Rende 0 se il valore
 * non si capisce: la dichiarazione allora si butta, non il resto della regola. */
static int leggi_valore(unsigned short prop, const char *v, unsigned int n,
                        unsigned int *out)
{
    unsigned int c;

    switch (prop) {
    case CSS_P_COLORE:
    case CSS_P_SFONDO:
        if (!leggi_colore(v, n, &c)) return 0;
        *out = c;
        return 1;

    case CSS_P_CORPO: {
        /* Le parole di font-size: le assolute sono la scala di tutti i
         * browser riportata al nostro corpo di 15; larger e smaller sono
         * relative al padre, come 1.2em e 0.83em. */
        static const struct { const char *p; int px; } PAROLE[] = {
            { "xx-small", 9 }, { "x-small", 10 }, { "small", 13 }, { "medium", CSS_CORPO_PREDEFINITO },
            { "large", 18 }, { "x-large", 24 }, { "xx-large", 32 }, { 0, 0 }
        };
        int j;
        for (j = 0; PAROLE[j].p; j++)
            if (parola_e(v, n, PAROLE[j].p)) { *out = (unsigned int)(PAROLE[j].px + 32768); return 1; }
        if (parola_e(v, n, "larger"))  { *out = rel_codifica(REL_EM, 120); return 1; }
        if (parola_e(v, n, "smaller")) { *out = rel_codifica(REL_EM, 83);  return 1; }
        return leggi_lunghezza(v, n, 0, out);
    }

    case CSS_P_MARG_SOPRA: case CSS_P_MARG_DX:
    case CSS_P_MARG_SOTTO: case CSS_P_MARG_SX:
        if (parola_e(v, n, "auto")) { *out = (unsigned int)(CSS_MISURA_AUTO + 32768); return 1; }
        return leggi_lunghezza(v, n, 1, out);

    /* LA DISPOSIZIONE (28 settembre 2026). «auto» e «none» sono il valore
     * iniziale, che per noi e' «non detto»: 0 + 32768 - 32768 = CSS_MISURA_NO. */
    case CSS_P_LARG: case CSS_P_LARG_MAX: case CSS_P_LARG_MIN:
        if (parola_e(v, n, "auto") || parola_e(v, n, "none")) { *out = 0; return 1; }
        return leggi_lunghezza(v, n, 0, out);

    case CSS_P_POS + 0: case CSS_P_POS + 1: case CSS_P_POS + 2: case CSS_P_POS + 3:
        if (parola_e(v, n, "auto")) { *out = 0; return 1; }
        return leggi_lunghezza(v, n, 1, out);

    case CSS_P_SCATOLA:
        if (parola_e(v, n, "border-box"))  { *out = 1; return 1; }
        if (parola_e(v, n, "content-box")) { *out = 0; return 1; }
        return 0;

    case CSS_P_GALLEGGIA:
        if (parola_e(v, n, "left")  || parola_e(v, n, "inline-start")) { *out = CSS_GALLEGGIA_SX; return 1; }
        if (parola_e(v, n, "right") || parola_e(v, n, "inline-end"))   { *out = CSS_GALLEGGIA_DX; return 1; }
        if (parola_e(v, n, "none")) { *out = CSS_GALLEGGIA_NO; return 1; }
        return 0;

    case CSS_P_POSIZIONE:
        if (parola_e(v, n, "static"))   { *out = CSS_POS_STATICA;   return 1; }
        if (parola_e(v, n, "relative")) { *out = CSS_POS_RELATIVA;  return 1; }
        if (parola_e(v, n, "absolute")) { *out = CSS_POS_ASSOLUTA;  return 1; }
        if (parola_e(v, n, "fixed"))    { *out = CSS_POS_FISSA;     return 1; }
        if (parola_e(v, n, "sticky") || parola_e(v, n, "-webkit-sticky"))
            { *out = CSS_POS_APPICCICA; return 1; }
        return 0;

    case CSS_P_FLEX_DIR:
        if (parola_e(v, n, "row") || parola_e(v, n, "row-reverse"))       { *out = 0; return 1; }
        if (parola_e(v, n, "column") || parola_e(v, n, "column-reverse")) { *out = 1; return 1; }
        return 0;

    case CSS_P_FLEX_CAPO:
        if (parola_e(v, n, "wrap") || parola_e(v, n, "wrap-reverse")) { *out = 1; return 1; }
        if (parola_e(v, n, "nowrap")) { *out = 0; return 1; }
        return 0;

    case CSS_P_OPACITA: {
        /* Conta solo se e' zero: «0», «0.0», «0%», «.0». */
        unsigned int k; int zero = n > 0;
        for (k = 0; k < n; k++)
            if (v[k] != '0' && v[k] != '.' && v[k] != '%') { zero = 0; break; }
        *out = zero ? 0u : 1u;
        return 1;
    }
    case CSS_P_PUNTATORE:
        *out = parola_e(v, n, "none") ? 0u : 1u;
        return 1;
    case CSS_P_SEGNO: {
        /* «none» da solo o dentro la forma breve («none inside»). */
        unsigned int k;
        *out = 1u;
        for (k = 0; k + 4 <= n; k++)
            if ((k == 0 || v[k - 1] == ' ') && (k + 4 == n || v[k + 4] == ' ') &&
                minusc((unsigned char)v[k]) == 'n' && minusc((unsigned char)v[k + 1]) == 'o' &&
                minusc((unsigned char)v[k + 2]) == 'n' && minusc((unsigned char)v[k + 3]) == 'e')
                *out = 0u;
        return 1;
    }

    case CSS_P_PULISCI:
        if (parola_e(v, n, "none"))  { *out = 0; return 1; }
        if (parola_e(v, n, "left")  || parola_e(v, n, "inline-start")) { *out = CSS_PULISCI_SX; return 1; }
        if (parola_e(v, n, "right") || parola_e(v, n, "inline-end"))   { *out = CSS_PULISCI_DX; return 1; }
        if (parola_e(v, n, "both"))  { *out = CSS_PULISCI_SX | CSS_PULISCI_DX; return 1; }
        return 0;

    case CSS_P_GIUSTIFICA:
        if (parola_e(v, n, "flex-start") || parola_e(v, n, "start") || parola_e(v, n, "left") ||
            parola_e(v, n, "normal") || parola_e(v, n, "stretch")) { *out = CSS_GIU_INIZIO; return 1; }
        if (parola_e(v, n, "flex-end") || parola_e(v, n, "end") || parola_e(v, n, "right"))
            { *out = CSS_GIU_FINE; return 1; }
        if (parola_e(v, n, "center"))        { *out = CSS_GIU_CENTRO;  return 1; }
        if (parola_e(v, n, "space-between")) { *out = CSS_GIU_TRA;     return 1; }
        if (parola_e(v, n, "space-around"))  { *out = CSS_GIU_INTORNO; return 1; }
        if (parola_e(v, n, "space-evenly"))  { *out = CSS_GIU_UGUALE;  return 1; }
        return 0;

    case CSS_P_ALLINEA_VOCI:
        if (parola_e(v, n, "stretch") || parola_e(v, n, "normal") || parola_e(v, n, "baseline"))
            { *out = CSS_ALV_STIRA; return 1; }
        if (parola_e(v, n, "flex-start") || parola_e(v, n, "start")) { *out = CSS_ALV_INIZIO; return 1; }
        if (parola_e(v, n, "flex-end") || parola_e(v, n, "end"))     { *out = CSS_ALV_FINE;   return 1; }
        if (parola_e(v, n, "center"))                                 { *out = CSS_ALV_CENTRO; return 1; }
        return 0;

    case CSS_P_SPAZIO_RIGA: case CSS_P_SPAZIO_COL:
        if (parola_e(v, n, "normal")) { *out = 0; return 1; }
        return leggi_lunghezza(v, n, 0, out);

    case CSS_P_FLEX_CRESCE: {
        /* un numero puro, anche con la virgola: si tiene in centesimi */
        unsigned int i = 0, cent = 0, cifre = 0, dec = 0;
        while (i < n && v[i] >= '0' && v[i] <= '9') { cent = cent * 10 + (unsigned int)(v[i] - '0'); i++; cifre = 1; if (cent > 600) return 0; }
        cent *= 100;
        if (i < n && v[i] == '.') {
            i++;
            while (i < n && v[i] >= '0' && v[i] <= '9') { if (dec < 2) cent += (unsigned int)(v[i] - '0') * (dec ? 1u : 10u); dec++; i++; cifre = 1; }
        }
        if (!cifre || i != n) return 0;
        *out = cent;
        return 1;
    }

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

    case CSS_P_VISIBILE:
        if (n == 7 && minusc((unsigned char)v[0]) == 'v') { *out = 1; return 1; }
        if ((n == 6 && minusc((unsigned char)v[0]) == 'h') ||
            (n == 8 && minusc((unsigned char)v[0]) == 'c')) { *out = 0; return 1; }
        return 0;

    case CSS_P_BORDO_LARG + 0: case CSS_P_BORDO_LARG + 1:
    case CSS_P_BORDO_LARG + 2: case CSS_P_BORDO_LARG + 3:
        return leggi_spessore(v, n, out);

    case CSS_P_BORDO_STILE + 0: case CSS_P_BORDO_STILE + 1:
    case CSS_P_BORDO_STILE + 2: case CSS_P_BORDO_STILE + 3:
        return leggi_stile_bordo(v, n, out);

    case CSS_P_BORDO_COL + 0: case CSS_P_BORDO_COL + 1:
    case CSS_P_BORDO_COL + 2: case CSS_P_BORDO_COL + 3:
        if (parola_e(v, n, "currentcolor")) { *out = CSS_NIENTE; return 1; }
        if (!leggi_colore(v, n, &c)) return 0;
        *out = c;
        return 1;

    case CSS_P_IMBOTTITURA + 0: case CSS_P_IMBOTTITURA + 1:
    case CSS_P_IMBOTTITURA + 2: case CSS_P_IMBOTTITURA + 3:
        return leggi_lunghezza(v, n, 0, out);             /* il padding non e' mai negativo */

    case CSS_P_DISPLAY:
        if (parola_e(v, n, "none"))   { *out = CSS_DISPLAY_NIENTE; return 1; }
        if (parola_e(v, n, "block"))  { *out = CSS_DISPLAY_BLOCCO; return 1; }
        if (parola_e(v, n, "inline")) { *out = CSS_DISPLAY_INLINE; return 1; }
        /* (28 settembre 2026) Prima questi si buttavano, e l'elemento teneva
         * il display del suo nome: un <li> in un menu `display: flex` restava
         * una riga per voce. list-item, grid e table sono blocchi per noi:
         * la griglia e la tabella in CSS non si impaginano come tali. */
        if (parola_e(v, n, "inline-block") || parola_e(v, n, "inline-table"))
            { *out = CSS_DISPLAY_INBLOCCO; return 1; }
        if (parola_e(v, n, "flex") || parola_e(v, n, "inline-flex"))
            { *out = CSS_DISPLAY_FLEX; return 1; }
        if (parola_e(v, n, "list-item") || parola_e(v, n, "grid") ||
            parola_e(v, n, "inline-grid") || parola_e(v, n, "table") ||
            parola_e(v, n, "flow-root"))
            { *out = CSS_DISPLAY_BLOCCO; return 1; }
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


/* ! LA CODA C'E' PER LE SCORCIATOIE (@NAV-BORDER, 28 settembre 2026). Una
 * dichiarazione era una proprieta'; `border: 1px solid red` ne vale dodici,
 * `margin: 0 auto` quattro. La scorciatoia si espande qui, le voci escono una
 * per volta alle chiamate successive, e chi scorre le dichiarazioni — le
 * regole e l'attributo `style` — non si accorge di niente. */
#define DICH_CODA 16

typedef struct {
    const char    *t;
    unsigned int   i, n;
    unsigned short coda_p[DICH_CODA];
    unsigned int   coda_v[DICH_CODA];
    unsigned int   coda_n, coda_i;
} DichIter;

static void coda_metti(DichIter *it, unsigned short p, unsigned int v)
{
    if (it->coda_n < DICH_CODA) { it->coda_p[it->coda_n] = p; it->coda_v[it->coda_n] = v; it->coda_n++; }
}

/* Le parole di un valore, separate da spazi ma non dentro le parentesi —
 * rgb(1, 2, 3) e' una parola sola. Al piu' `max`; rende quante. */
static unsigned int parole(const char *v, unsigned int n, unsigned int *da,
                           unsigned int *lung, unsigned int max)
{
    unsigned int i = 0, k = 0;

    while (i < n && k < max) {
        unsigned int inizio, par = 0;

        while (i < n && spazio((unsigned char)v[i])) i++;
        if (i >= n) break;
        inizio = i;
        while (i < n && (par || !spazio((unsigned char)v[i]))) {
            if (v[i] == '(') par++;
            else if (v[i] == ')' && par) par--;
            i++;
        }
        da[k] = inizio; lung[k] = i - inizio; k++;
    }
    return k;
}

/* I quattro lati da uno a quattro valori, alla maniera del CSS: 1 = tutti,
 * 2 = sopra/sotto e destra/sinistra, 3 = sopra, destra/sinistra, sotto. */
static const unsigned char LATO_DA[4][4] = {
    { 0, 0, 0, 0 }, { 0, 1, 0, 1 }, { 0, 1, 2, 1 }, { 0, 1, 2, 3 }
};

/* Se `nome` e' una scorciatoia che si conosce, la espande nella coda di `it`
 * e rende 1 (anche se nessun valore si e' capito: la dichiarazione e' sua). */
static int scorciatoia(DichIter *it, const char *nome, unsigned int nn,
                       const char *v, unsigned int n)
{
    unsigned int da[4], lu[4], k, q, x;
    unsigned short base = 0;
    int lato = -1;

    /* margin, padding, border-width/-style/-color: da uno a quattro valori */
    if      (parola_e(nome, nn, "margin"))       base = CSS_P_MARG_SOPRA;
    else if (parola_e(nome, nn, "padding"))      base = CSS_P_IMBOTTITURA;
    else if (parola_e(nome, nn, "border-width")) base = CSS_P_BORDO_LARG;
    else if (parola_e(nome, nn, "border-style")) base = CSS_P_BORDO_STILE;
    else if (parola_e(nome, nn, "border-color")) base = CSS_P_BORDO_COL;
    if (base) {
        unsigned int vals[4];
        k = parole(v, n, da, lu, 4);
        if (k == 0) return 1;
        for (q = 0; q < k; q++) {
            /* ! «auto» NEI MARGINI E' CSS_MISURA_AUTO (dal 28 settembre 2026;
             * prima valeva zero, perche' il centraggio che chiede — margin: 0
             * auto — voleva una larghezza che non c'era). Qui va detto a parte
             * perche' leggi_valore lo capisce solo sul lato singolo, e senza
             * questa riga si perderebbe tutta la dichiarazione. */
            if (base == CSS_P_MARG_SOPRA && parola_e(v + da[q], lu[q], "auto")) {
                vals[q] = (unsigned int)(CSS_MISURA_AUTO + 32768);   /* dal 28/09 e' auto davvero */
                continue;
            }
            /* il valore di un lato si legge come quello del primo: i quattro
             * codici di ogni proprieta' sono in fila, sopra-destra-sotto-sinistra */
            if (!leggi_valore(base, v + da[q], lu[q], &vals[q])) return 1;
        }
        for (x = 0; x < 4; x++)
            coda_metti(it, (unsigned short)(base + x), vals[LATO_DA[k - 1][x]]);
        return 1;
    }

    /* flex: «1», «1 1 0%», «auto», «none» — qui conta solo quanto cresce.
     * flex-flow: la direzione e l'andare a capo, in un ordine qualunque. */
    if (parola_e(nome, nn, "flex")) {
        unsigned int w;
        k = parole(v, n, da, lu, 3);
        if (k == 0) return 1;
        if (parola_e(v + da[0], lu[0], "none"))      coda_metti(it, CSS_P_FLEX_CRESCE, 0);
        else if (parola_e(v + da[0], lu[0], "auto")) coda_metti(it, CSS_P_FLEX_CRESCE, 100);
        else if (leggi_valore(CSS_P_FLEX_CRESCE, v + da[0], lu[0], &w))
            coda_metti(it, CSS_P_FLEX_CRESCE, w);
        return 1;
    }
    /* gap: una o due lunghezze, riga e colonna */
    if (parola_e(nome, nn, "gap") || parola_e(nome, nn, "grid-gap")) {
        unsigned int a, b;
        k = parole(v, n, da, lu, 2);
        if (k == 0) return 1;
        if (!leggi_valore(CSS_P_SPAZIO_RIGA, v + da[0], lu[0], &a)) return 1;
        b = a;
        if (k == 2 && !leggi_valore(CSS_P_SPAZIO_COL, v + da[1], lu[1], &b)) return 1;
        coda_metti(it, CSS_P_SPAZIO_RIGA, a);
        coda_metti(it, CSS_P_SPAZIO_COL, b);
        return 1;
    }
    if (parola_e(nome, nn, "flex-flow")) {
        unsigned int w;
        k = parole(v, n, da, lu, 2);
        for (q = 0; q < k; q++) {
            if (leggi_valore(CSS_P_FLEX_DIR, v + da[q], lu[q], &w))       coda_metti(it, CSS_P_FLEX_DIR, w);
            else if (leggi_valore(CSS_P_FLEX_CAPO, v + da[q], lu[q], &w)) coda_metti(it, CSS_P_FLEX_CAPO, w);
        }
        return 1;
    }

    /* border e border-top/right/bottom/left: spessore, stile e colore in un
     * ordine qualunque. ! CIO' CHE NON SI DICE TORNA AL VALORE INIZIALE —
     * medium, none, currentcolor — e non resta com'era: e' la regola delle
     * scorciatoie, ed e' il modo in cui i siti TOLGONO un bordo
     * (`border: 0`, o `border: none`). */
    if (parola_e(nome, nn, "border")) lato = 4;
    else if (parola_e(nome, nn, "border-top"))    lato = 0;
    else if (parola_e(nome, nn, "border-right"))  lato = 1;
    else if (parola_e(nome, nn, "border-bottom")) lato = 2;
    else if (parola_e(nome, nn, "border-left"))   lato = 3;
    if (lato < 0) return 0;
    {
        unsigned int larg = 3u + 32768u, stile = 0, col = CSS_NIENTE, w;

        k = parole(v, n, da, lu, 3);
        for (q = 0; q < k; q++) {
            if (leggi_stile_bordo(v + da[q], lu[q], &w))      stile = w;
            else if (leggi_spessore(v + da[q], lu[q], &w))    larg = w;
            else if (parola_e(v + da[q], lu[q], "currentcolor")) col = CSS_NIENTE;
            else if (leggi_colore(v + da[q], lu[q], &w))      col = w;
            /* una parola che non si capisce (un rgb(), una variabile) si
             * salta: il bordo resta, col colore del testo */
        }
        for (x = 0; x < 4; x++) {
            if (lato != 4 && (int)x != lato) continue;
            coda_metti(it, (unsigned short)(CSS_P_BORDO_LARG + x), larg);
            coda_metti(it, (unsigned short)(CSS_P_BORDO_STILE + x), stile);
            coda_metti(it, (unsigned short)(CSS_P_BORDO_COL + x), col);
        }
    }
    return 1;
}

static int dich_prossima(DichIter *it, unsigned short *prop, unsigned int *val)
{
    for (;;) {
    if (it->coda_i < it->coda_n) {
        *prop = it->coda_p[it->coda_i];
        *val  = it->coda_v[it->coda_i];
        it->coda_i++;
        return 1;
    }
    it->coda_n = it->coda_i = 0;
    if (it->i >= it->n) return 0;
    {
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
        if (pf > pi && vf > vi &&
            scorciatoia(it, it->t + pi, pf - pi, it->t + vi, vf - vi)) continue;
        /* valore o proprieta' non capiti: si butta questa e si va avanti */
    }
    }
}

/* Posa una dichiarazione su uno stile. ! LO SWITCH E' COMPLETO APPOSTA: con
 * -Wall il compilatore segnala il giorno che si aggiunge un CSS_P_ e ci si
 * dimentica di questo punto. */
/* Le proprieta' che sono lunghezze, cioe' quelle in cui REL_BIT vuol dire
 * «relativa». ! SOLO QUESTE: un colore ARGB ha il bit alto acceso anche lui. */
static int e_lunghezza(unsigned short p)
{
    return p == CSS_P_CORPO || (p >= CSS_P_MARG_SOPRA && p <= CSS_P_MARG_SX) ||
           (p >= CSS_P_BORDO_LARG && p < CSS_P_BORDO_LARG + 4) ||
           (p >= CSS_P_IMBOTTITURA && p < CSS_P_IMBOTTITURA + 4) ||
           (p >= CSS_P_LARG && p <= CSS_P_LARG_MIN) ||
           (p >= CSS_P_POS && p < CSS_P_POS + 4) ||
           p == CSS_P_SPAZIO_RIGA || p == CSS_P_SPAZIO_COL;
}

/* Durante css_calcola, le lunghezze relative vincenti aspettano qui la fine
 * della cascata: il corpo dell'elemento, che e' la base di em, lo si sa solo
 * dopo. Fuori da css_calcola e' 0, e una relativa si risolve subito. */
static unsigned int *g_rel = 0;

static void css_posa(CssStile *s, unsigned short prop, unsigned int val);

static void posa_px(CssStile *s, unsigned short prop, int px)
{
    unsigned int *r = g_rel;

    if (px < -20000) px = -20000;
    if (px >  20000) px =  20000;
    g_rel = 0;
    css_posa(s, prop, (unsigned int)(px + 32768));
    g_rel = r;
}

/* Il bit di larghezza_perc di una delle tre larghezze. */
static unsigned char bit_perc(unsigned short prop)
{
    return prop == CSS_P_LARG ? CSS_LARG_PERC :
           prop == CSS_P_LARG_MAX ? CSS_LARG_MAX_PERC : CSS_LARG_MIN_PERC;
}

static short *campo_larg(CssStile *s, unsigned short prop)
{
    return prop == CSS_P_LARG ? &s->larghezza :
           prop == CSS_P_LARG_MAX ? &s->larghezza_max : &s->larghezza_min;
}

static void css_posa(CssStile *s, unsigned short prop, unsigned int val)
{
    /* ! LA % DI UNA LARGHEZZA NON SI RISOLVE QUI: e' del contenitore, che
     * sa solo l'impaginatore (css.h, larghezza_perc). Si tiene in centesimi
     * di punto, e vince come un valore assoluto — non dipende dal corpo. */
    if (prop >= CSS_P_LARG && prop <= CSS_P_LARG_MIN && (val & REL_BIT) &&
        ((val >> 24) & 0x7Fu) == REL_PERC) {
        int cent = (int)(val & 0xFFFFFFu) - (int)REL_ZERO;

        if (cent < 0) cent = 0;
        if (cent > 32000) cent = 32000;
        if (g_rel) g_rel[prop] = 0;
        *campo_larg(s, prop) = (short)cent;
        s->larghezza_perc |= bit_perc(prop);
        return;
    }
    if (e_lunghezza(prop)) {
        if (val & REL_BIT) {
            if (g_rel) { g_rel[prop] = val; return; }
            {   /* fuori dalla cascata (un attributo style da solo): la base e'
                 * il corpo che lo stile ha gia', o quello predefinito */
                int c = (s->corpo == CSS_MISURA_NO) ? CSS_CORPO_PREDEFINITO : s->corpo;
                posa_px(s, prop, rel_px(val, c, prop == CSS_P_CORPO ? c : g_media_w));
            }
            return;
        }
        if (g_rel) g_rel[prop] = 0;     /* un valore assoluto vince su una relativa di prima */
    }
    switch (prop) {
    case CSS_P_COLORE:     s->colore = val;                            break;
    case CSS_P_SFONDO:     s->sfondo = val;                            break;
    case CSS_P_PESO:       s->grassetto = (unsigned char)val;          break;
    case CSS_P_STILE:      s->corsivo = (unsigned char)val;            break;
    case CSS_P_FAMIGLIA:   s->famiglia = (unsigned char)val;           break;
    case CSS_P_CORPO:      s->corpo = (short)((int)val - 32768);       break;
    case CSS_P_ALLINEA:    s->allineamento = (unsigned char)val;       break;
    case CSS_P_DISPLAY:    s->display = (unsigned char)val;            break;
    case CSS_P_VISIBILE:   s->visibile = (unsigned char)val;           break;
    case CSS_P_MARG_SOPRA: s->margine[0] = (short)((int)val - 32768);  break;
    case CSS_P_MARG_DX:    s->margine[1] = (short)((int)val - 32768);  break;
    case CSS_P_MARG_SOTTO: s->margine[2] = (short)((int)val - 32768);  break;
    case CSS_P_MARG_SX:    s->margine[3] = (short)((int)val - 32768);  break;
    case CSS_P_BORDO_LARG + 0: case CSS_P_BORDO_LARG + 1:
    case CSS_P_BORDO_LARG + 2: case CSS_P_BORDO_LARG + 3:
        s->bordo[prop - CSS_P_BORDO_LARG] = (short)((int)val - 32768);              break;
    case CSS_P_BORDO_STILE + 0: case CSS_P_BORDO_STILE + 1:
    case CSS_P_BORDO_STILE + 2: case CSS_P_BORDO_STILE + 3:
        s->bordo_stile[prop - CSS_P_BORDO_STILE] = (unsigned char)val;              break;
    case CSS_P_BORDO_COL + 0: case CSS_P_BORDO_COL + 1:
    case CSS_P_BORDO_COL + 2: case CSS_P_BORDO_COL + 3:
        s->bordo_col[prop - CSS_P_BORDO_COL] = val;                                 break;
    case CSS_P_IMBOTTITURA + 0: case CSS_P_IMBOTTITURA + 1:
    case CSS_P_IMBOTTITURA + 2: case CSS_P_IMBOTTITURA + 3:
        s->imbottitura[prop - CSS_P_IMBOTTITURA] = (short)((int)val - 32768);       break;
    case CSS_P_LARG: case CSS_P_LARG_MAX: case CSS_P_LARG_MIN:
        *campo_larg(s, prop) = (short)((int)val - 32768);
        s->larghezza_perc &= (unsigned char)~bit_perc(prop);                       break;
    case CSS_P_SCATOLA:    s->scatola_bordo = (unsigned char)val;       break;
    case CSS_P_GALLEGGIA:  s->galleggia = (unsigned char)val;           break;
    case CSS_P_POSIZIONE:  s->posizione = (unsigned char)val;           break;
    case CSS_P_POS + 0: case CSS_P_POS + 1: case CSS_P_POS + 2: case CSS_P_POS + 3:
        s->pos[prop - CSS_P_POS] = (short)((int)val - 32768);               break;
    case CSS_P_FLEX_DIR:   s->flex_colonna = (unsigned char)val;        break;
    case CSS_P_FLEX_CAPO:  s->flex_a_capo = (unsigned char)val;         break;
    case CSS_P_FLEX_CRESCE: s->flex_cresce = (unsigned short)val;       break;
    case CSS_P_PULISCI:    s->pulisci = (unsigned char)val;             break;
    case CSS_P_GIUSTIFICA: s->giustifica = (unsigned char)val;          break;
    case CSS_P_ALLINEA_VOCI: s->allinea_voci = (unsigned char)val;      break;
    case CSS_P_SPAZIO_RIGA: s->spazio_riga = (short)((int)val - 32768); break;
    case CSS_P_SPAZIO_COL:  s->spazio_col  = (short)((int)val - 32768); break;
    case CSS_P_OPACITA:    g_opaca = (int)val;                          break;
    case CSS_P_PUNTATORE:  g_punta = (int)val;                          break;
    case CSS_P_SEGNO:      g_segno = (int)val;                          break;
    default: break;
    }
}

/* =============================================================================
 * «C'E' MA PER ORA NON SI VEDE» — opacity: 0 con pointer-events: none
 * (4 ottobre 2026)
 *
 * Misurato su www.tiscali.it (Tailwind): le tendine del menu e il logo che
 * compare scorrendo non sono `display: none` ne' `visibility: hidden`: sono
 * `opacity: 0` e `pointer-events: none`, e uno script le accende. Qui non si
 * disegnava ne' l'una ne' l'altra proprieta', e le tendine comparivano tutte
 * aperte in mezzo alla pagina.
 *
 * ! SERVONO TUTTE E DUE, E NON LA SOLA OPACITA'. Un elemento a opacity: 0 che
 * pero' risponde al mouse e' di solito qualcosa che una transizione sta per
 * far comparire da se' (un testo che entra in dissolvenza): nasconderlo
 * vorrebbe dire togliere dalla pagina del contenuto vero, che e' peggio di
 * mostrare un menu. Con tutte e due l'autore ha detto «non c'e'» in ogni modo
 * che aveva, e si tratta come visibility: hidden — che scende ai figli.
 *
 * ! NON STANNO IN CssStile, apposta: allungare quella struttura cambierebbe
 * la misura di un dato che i programmi passano a excss.so, e uno compilato
 * ieri si vedrebbe scrivere oltre il suo. css_calcola lavora un nodo per
 * volta: due variabili del file, azzerate all'inizio, bastano.
 * ============================================================================= */
int css_segno_lista(void) { return g_segno; }

static void nascosto_applica(CssStile *s)
{
    if (g_opaca == 0 && g_punta == 0) s->visibile = 0;
}

void css_stile_inline(const char *testo, unsigned int n, CssStile *s)
{
    DichIter       it;
    unsigned short prop;
    unsigned int   val;

    if (!testo || !s) return;

    g_opaca = g_punta = 1;
    it.t = testo; it.i = 0; it.n = n; it.coda_n = it.coda_i = 0;
    while (dich_prossima(&it, &prop, &val)) css_posa(s, prop, val);
    nascosto_applica(s);
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

                /* ! :not(:focus) ALWAYS MATCHES HERE (30 September 2026). In a
                 * page that is drawn and not touched nothing is hovered,
                 * focused, active or visited, so the negation of such a state
                 * is true for every element. Read as "cannot honour -> never
                 * matches" — the rule for the state alone — it turned the rule
                 * off: Wikipedia hides its "Vai al contenuto" links with
                 * .mw-jump-link:not(:focus){position:absolute;width:1px}, and
                 * they showed at the top of every article. */
                if (y - x > 1 && t[x] == ':' && t[x + 1] != ':') {
                    static const char *const STATI[] = {
                        "hover", "focus", "active", "visited", "focus-within",
                        "focus-visible", "target", "target-within", 0
                    };
                    char         st[32];
                    unsigned int q = 0, z;

                    for (z = x + 1; z < y && q < sizeof(st) - 1; z++)
                        st[q++] = (char)minusc((unsigned char)t[z]);
                    st[q] = '\0';
                    for (k = 0; STATI[k]; k++) if (pari(st, STATI[k])) break;
                    if (STATI[k] && z == y) continue;
                }
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

/* =============================================================================
 * @media, @supports, @layer (28 settembre 2026)
 *
 * ! FINO A OGGI SI SALTAVANO PER INTERO, e su Wikipedia voleva dire 578 regole
 * su 964 buttate via — 418 dentro un semplice «@media screen». Li' Vector
 * nasconde i menu a tendina e dispone la pagina: senza, i menu si vedevano
 * aperti e tutto finiva in colonna.
 *
 * @media: si valuta la condizione contro la LARGHEZZA DELLA FINESTRA
 * (css_media_larghezza, che il browser aggiorna); vale, e le regole dentro si
 * leggono al loro posto nell'ordine del documento. screen e all valgono, print
 * no; min-width e max-width in px, em, rem e calc(A +/- B); schema scuro,
 * movimento ridotto e le caratteristiche che non si conoscono NON valgono.
 *
 * @supports: una proprieta' e' «supportata» se QUESTO lettore la capisce — non
 * se la capisce un browser qualunque. E' il punto: una pagina che offre
 * `mask-image` o, in alternativa, un ripiego, deve ricevere il ripiego.
 *
 * @layer con un blocco: le regole dentro valgono (l'ordine dei livelli non si
 * distingue: resta quello del documento).
 * ============================================================================= */
static int g_media_w = 800;

void css_media_larghezza(int px)
{
    if (px > 0) g_media_w = px;
}

/* Una misura di una media query: «1120px», «40em», «calc(1120px - 1px)». */
static int media_misura(const char *s, unsigned int n, int *out)
{
    unsigned int i = 0;
    int          v = 0, neg = 0;

    while (i < n && spazio((unsigned char)s[i])) i++;
    if (i + 5 <= n && minusc((unsigned char)s[i]) == 'c' && s[i + 4] == '(') {
        int a, b, segno = 1;
        unsigned int j = i + 5, k;

        k = j;
        while (k < n && s[k] != '+' && !(s[k] == '-' && k > j && spazio((unsigned char)s[k - 1]))) k++;
        if (k >= n || !media_misura(s + j, k - j, &a)) return 0;
        if (s[k] == '-') segno = -1;
        j = k + 1;
        k = j;
        while (k < n && s[k] != ')') k++;
        if (!media_misura(s + j, k - j, &b)) return 0;
        *out = a + segno * b;
        return 1;
    }
    if (i < n && s[i] == '-') { neg = 1; i++; }
    if (i >= n || s[i] < '0' || s[i] > '9') return 0;
    while (i < n && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0');
    if (i < n && s[i] == '.') { i++; while (i < n && s[i] >= '0' && s[i] <= '9') i++; }
    if (i + 2 <= n && minusc((unsigned char)s[i]) == 'e' && minusc((unsigned char)s[i + 1]) == 'm') v *= 16;
    else if (i + 3 <= n && minusc((unsigned char)s[i]) == 'r' && minusc((unsigned char)s[i + 1]) == 'e') v *= 16;
    *out = neg ? -v : v;
    return 1;
}

static int pezzo_ug(const char *s, unsigned int n, const char *parola)
{
    unsigned int k = 0;

    while (k < n && parola[k] && minusc((unsigned char)s[k]) == parola[k]) k++;
    return k == n && parola[k] == '\0';
}

/* «(nome: valore)» o «(nome)», senza le parentesi. */
static int media_caratteristica(const char *s, unsigned int n)
{
    unsigned int i = 0, ni, nf, vi, vf;
    int          v;

    while (i < n && spazio((unsigned char)s[i])) i++;
    ni = i;
    while (i < n && s[i] != ':' && !spazio((unsigned char)s[i])) i++;
    nf = i;
    while (i < n && (spazio((unsigned char)s[i]) || s[i] == ':')) i++;
    vi = i;
    vf = n;
    while (vf > vi && spazio((unsigned char)s[vf - 1])) vf--;

    if (pezzo_ug(s + ni, nf - ni, "min-width") || pezzo_ug(s + ni, nf - ni, "min-device-width"))
        return media_misura(s + vi, vf - vi, &v) && g_media_w >= v;
    if (pezzo_ug(s + ni, nf - ni, "max-width") || pezzo_ug(s + ni, nf - ni, "max-device-width"))
        return media_misura(s + vi, vf - vi, &v) && g_media_w <= v;
    if (pezzo_ug(s + ni, nf - ni, "width"))
        return media_misura(s + vi, vf - vi, &v) && g_media_w == v;
    if (pezzo_ug(s + ni, nf - ni, "prefers-color-scheme"))
        return pezzo_ug(s + vi, vf - vi, "light");
    if (pezzo_ug(s + ni, nf - ni, "prefers-reduced-motion"))
        return pezzo_ug(s + vi, vf - vi, "no-preference");
    if (pezzo_ug(s + ni, nf - ni, "orientation"))
        return pezzo_ug(s + vi, vf - vi, "landscape");
    if (pezzo_ug(s + ni, nf - ni, "hover") || pezzo_ug(s + ni, nf - ni, "any-hover"))
        return vi == vf || pezzo_ug(s + vi, vf - vi, "hover");
    if (pezzo_ug(s + ni, nf - ni, "pointer") || pezzo_ug(s + ni, nf - ni, "any-pointer"))
        return vi == vf || pezzo_ug(s + vi, vf - vi, "fine");
    if (pezzo_ug(s + ni, nf - ni, "color"))
        return 1;
    return 0;       /* quel che non si conosce non vale */
}

/* Una sola query (senza virgole): [only|not] [tipo] [and (c)]... */
static int media_una(const char *s, unsigned int n)
{
    unsigned int i = 0;
    int          nega = 0, va = 1;

    while (i < n) {
        unsigned int p;

        while (i < n && spazio((unsigned char)s[i])) i++;
        if (i >= n) break;
        if (s[i] == '(') {
            int liv = 1;

            p = ++i;
            while (i < n && liv) { if (s[i] == '(') liv++; else if (s[i] == ')') liv--; i++; }
            if (!media_caratteristica(s + p, (i - 1) - p)) va = 0;
            continue;
        }
        p = i;
        while (i < n && !spazio((unsigned char)s[i]) && s[i] != '(') i++;
        if (pezzo_ug(s + p, i - p, "not"))       nega = 1;
        else if (pezzo_ug(s + p, i - p, "only") || pezzo_ug(s + p, i - p, "and")) { }
        else if (pezzo_ug(s + p, i - p, "screen") || pezzo_ug(s + p, i - p, "all")) { }
        else va = 0;                            /* print, speech, tv... */
    }
    return nega ? !va : va;
}

static int media_va(const char *s, unsigned int n)
{
    unsigned int i = 0, p = 0;
    int          liv = 0;

    if (n == 0) return 1;                        /* «@media {» = sempre */
    for (i = 0; i <= n; i++) {
        if (i < n && s[i] == '(') liv++;
        else if (i < n && s[i] == ')') liv--;
        else if (i == n || (s[i] == ',' && liv == 0)) {
            if (media_una(s + p, i - p)) return 1;
            p = i + 1;
        }
    }
    return 0;
}

/* @supports: not, and, or, (proprieta': valore), selector(...). */
static int supports_va(const char *s, unsigned int n)
{
    unsigned int i = 0;
    int          ris = -1, op = 0;       /* op: 0 nessuno, 1 and, 2 or */

    while (i < n) {
        int          v, nega = 0;
        unsigned int p;

        while (i < n && spazio((unsigned char)s[i])) i++;
        if (i >= n) break;
        if (i + 3 <= n && pezzo_ug(s + i, 3, "not") && (i + 3 == n || spazio((unsigned char)s[i + 3]) || s[i + 3] == '(')) {
            nega = 1; i += 3;
            while (i < n && spazio((unsigned char)s[i])) i++;
        }
        if (i + 3 <= n && pezzo_ug(s + i, 3, "and") && spazio((unsigned char)s[i + 3])) { op = 1; i += 3; continue; }
        if (i + 2 <= n && pezzo_ug(s + i, 2, "or") && spazio((unsigned char)s[i + 2])) { op = 2; i += 2; continue; }

        if (i + 9 <= n && pezzo_ug(s + i, 9, "selector(")) {
            int liv = 1;
            i += 9;
            while (i < n && liv) { if (s[i] == '(') liv++; else if (s[i] == ')') liv--; i++; }
            v = 1;
        } else if (i < n && s[i] == '(') {
            int liv = 1;
            unsigned int k, due = 0;

            p = ++i;
            while (i < n && liv) { if (s[i] == '(') liv++; else if (s[i] == ')') liv--; i++; }
            /* dentro: una dichiarazione (i due punti al primo livello) o un'altra condizione */
            liv = 0;
            for (k = p; k < i - 1; k++) {
                if (s[k] == '(') liv++;
                else if (s[k] == ')') liv--;
                else if (s[k] == ':' && liv == 0) { due = k; break; }
            }
            if (due) {
                unsigned short cod;
                unsigned int   a = p, b = due;
                while (a < b && spazio((unsigned char)s[a])) a++;
                while (b > a && spazio((unsigned char)s[b - 1])) b--;
                v = prop_codice(s + a, b - a, &cod);
            } else {
                v = supports_va(s + p, (i - 1) - p);
            }
        } else {
            while (i < n && !spazio((unsigned char)s[i])) i++;   /* parola ignota */
            v = 0;
        }
        if (nega) v = !v;
        if (ris < 0)      ris = v;
        else if (op == 1) ris = ris && v;
        else              ris = ris || v;
    }
    return ris > 0;
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
            int          liv = 0, entra = 0;
            unsigned int ni = i + 1, nf, pi, pf, bi = 0, bf = 0;

            /* il nome, e la condizione fino alla graffa (o al punto e virgola) */
            nf = ni;
            while (nf < n && nomeok((unsigned char)testo[nf])) nf++;
            pi = nf;
            pf = pi;
            while (pf < n && testo[pf] != '{' && testo[pf] != ';') pf++;

            while (i < n) {
                if (testo[i] == '{') { if (liv == 0) bi = i + 1; liv++; }
                else if (testo[i] == '}') { liv--; if (liv <= 0) { bf = i; i++; break; } }
                else if (testo[i] == ';' && liv == 0) { i++; break; }
                i++;
            }
            if (bi && bf > bi) {
                while (pf > pi && spazio((unsigned char)testo[pf - 1])) pf--;
                while (pi < pf && spazio((unsigned char)testo[pi])) pi++;
                if (pezzo_ug(testo + ni, nf - ni, "media"))
                    entra = media_va(testo + pi, pf - pi);
                else if (pezzo_ug(testo + ni, nf - ni, "supports"))
                    entra = supports_va(testo + pi, pf - pi);
                else if (pezzo_ug(testo + ni, nf - ni, "layer"))
                    entra = 1;
            }
            if (entra) {
                fatte += css_analizza(f, testo + bi, bf - bi, origine);
                if (f->troncato) return fatte;
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
            unsigned int prima_a, prima_p;
            DichIter     it;
            unsigned short prop;
            unsigned int   val;

            e = virgola(testo, s, sel_f);

            /* 1: a rule; 2: valid, never matches here (see the reader) */
            prima_a = f->arena_n;
            prima_p = f->pezzi_n;
            if (leggi_selettore(f, testo, s, e, &r) != 1) {
                f->arena_n = prima_a;
                f->pezzi_n = prima_p;
                s = e + 1;
                continue;
            }

            if (f->regole_n >= f->regole_max) { f->troncato = 1; return fatte; }

            it.t = testo; it.i = gr_i; it.n = gr_f; it.coda_n = it.coda_i = 0;
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

            /* ! A RULE WITH NOTHING WE UNDERSTAND GIVES BACK ITS PIECES: most
             * of a real sheet is properties this reader does not know yet,
             * and on amazon.com 2400 rules had left 17600 pieces behind. */
            if (primo < 0) { f->arena_n = prima_a; f->pezzi_n = prima_p; }

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
    unsigned int rel[CSS_P_N];
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
        out->visibile     = ereditato->visibile;   /* anche la visibilita' */
    }

    if (!f || !d || nodo < 0) return;
    if (d->nodi[nodo].tipo != HTML_ELEMENTO) return;

    for (i = 0; i < CSS_P_N; i++) rel[i] = 0;
    g_rel = rel;
    g_opaca = g_punta = 1;
    g_segno = 2;

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

    /* ! LE RELATIVE ALLA FINE, E IN QUEST'ORDINE (@NAV-UNITA). Prima il corpo,
     * che si misura su quello del PADRE; poi tutto il resto, che si misura sul
     * corpo dell'elemento appena deciso — `font-size: 2em; margin: 1em` vuol
     * dire un margine grande quanto il testo grande, non quanto quello di
     * fuori. */
    g_rel = 0;
    {
        int padre = (ereditato && ereditato->corpo != CSS_MISURA_NO) ? ereditato->corpo
                                                                     : CSS_CORPO_PREDEFINITO;
        int mio;
        unsigned short p;

        if (rel[CSS_P_CORPO]) {
            int c = rel_px(rel[CSS_P_CORPO], padre, padre);
            posa_px(out, CSS_P_CORPO, c < 1 ? 1 : c);
        }
        mio = (out->corpo == CSS_MISURA_NO) ? padre : out->corpo;
        for (p = 0; p < CSS_P_N; p++)
            if (p != CSS_P_CORPO && rel[p]) posa_px(out, p, rel_px(rel[p], mio, g_media_w));
    }
    nascosto_applica(out);
}
