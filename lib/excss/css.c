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
static void var_azzera(void);

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
    s->sf_url         = 0;
    s->sf_ripeti      = CSS_SF_RIPETE;
    s->sf_px          = CSS_SF_PERC;        /* 0% 0%: l'angolo in alto a sinistra */
    s->sf_py          = CSS_SF_PERC;
    s->sf_mw          = 0;
    s->sf_mh          = 0;
    s->altezza        = CSS_MISURA_NO;
    s->altezza_min    = CSS_MISURA_NO;
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
    var_azzera();
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
/* calc() and its kin (P6, 25 November 2026): the low 24 bits are not a value
 * but where the TEXT of the expression is - in the sheet's arena (F) or in
 * g_loc (L), see calc_testo(). */
#define REL_CALC_F  4u
#define REL_CALC_L  5u

/* Declarations that are not a property of CssStile (P6): their CssDich
 * says where their text is in the arena. They are above CSS_P_N, so whoever
 * walks the declarations of a rule without knowing them skips them.
 *   PERSONALE  --name: value         "--name\0value\0"
 *   RIMANDATA  a value with var()    "property\0value\0", read in css_calcola
 *   FONTFACE   an @font-face block   see fontface_metti(); in no rule */
#define CSS_P_PERSONALE 0xFFF0u
#define CSS_P_RIMANDATA 0xFFF1u
#define CSS_P_FONTFACE  0xFFF2u

static int g_media_w;                    /* la larghezza delle @media, piu' giu' */
static int g_media_h;                    /* e l'altezza */
static int g_opaca = 1, g_punta = 1;     /* vedi nascosto_applica() */
static int g_segno = 2;                  /* vedi css_segno_lista() in css.h */

/* =============================================================================
 * WHAT A DECLARATION NEEDS FROM ITS ELEMENT (P6, 25 November 2026)
 *
 * A value with var() cannot be read when the sheet is read: what it says
 * depends on the element. A calc() with em or % neither. Both are kept as
 * TEXT and read in css_calcola, where these say which sheet, which element:
 *   g_fog_calc   the sheet css_calcola is working for (0 outside it)
 *   g_loc        texts made during ONE css_calcola (an attribute style's
 *                calc(), a value after var() was replaced): emptied at the
 *                start of every css_calcola, and of a css_stile_inline
 *                called from outside it.
 * ============================================================================= */
static const CssFoglio *g_fog_calc = 0;
static int              g_in_calcola = 0;
static char             g_loc[16384];
static unsigned int     g_loc_n = 1;

/* `n` bytes and a 0 into g_loc; the offset, 0 = no room */
static unsigned int loc_metti(const char *s, unsigned int n)
{
    unsigned int o = g_loc_n, i;

    if (g_loc_n + n + 1 > sizeof(g_loc)) return 0;
    for (i = 0; i < n; i++) g_loc[o + i] = s[i];
    g_loc[o + n] = 0;
    g_loc_n += n + 1;
    return o;
}

static unsigned int rel_codifica(unsigned int unita, int centesimi)
{
    if (centesimi < -800000) centesimi = -800000;
    if (centesimi >  800000) centesimi =  800000;
    return REL_BIT | (unita << 24) | ((unsigned int)(centesimi + (int)REL_ZERO) & 0xFFFFFFu);
}

/* Una lunghezza, codificata per il campo CssDich.numero: px (e pt, vw) come
 * sempre — m + 32768 — oppure relativa con REL_BIT. Rende 0 se non si capisce.
 * `neg` = 0 rifiuta i negativi (padding, bordi). */
static int e_calc(const char *v, unsigned int n);
static int leggi_calc(const char *v, unsigned int n, int neg, unsigned int *out);

static int leggi_lunghezza(const char *v, unsigned int n, int neg, unsigned int *out)
{
    unsigned int i = 0, k;
    int segno = 1, cent = 0, cifre = 0, dec = 0;
    char u[4];

    if (e_calc(v, n)) return leggi_calc(v, n, neg, out);
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
    if (!strcmp_c(u, "vh")) { *out = (unsigned int)(g_media_h * cent / 10000 + 32768); return 1; }
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
static const char *calc_testo(unsigned int num);
static int calc_valuta_testo(const char *s, int base_em, int base_perc, int perc_ignota,
                             int *px, int *tinto);

static int rel_px(unsigned int num, int base_em, int base_perc)
{
    unsigned int unita = (num >> 24) & 0x7Fu;
    int cent = (int)(num & 0xFFFFFFu) - (int)REL_ZERO;
    int base = (unita == REL_EM) ? base_em : (unita == REL_REM) ? CSS_CORPO_PREDEFINITO : base_perc;

    if (unita == REL_CALC_F || unita == REL_CALC_L) {
        int px = 0, tinto;
        const char *s = calc_testo(num);

        if (!s || !calc_valuta_testo(s, base_em, base_perc, 0, &px, &tinto)) return 0;
        return px;
    }

    /* em e rem sono volte (1.5em = 150 centesimi di volta), la % e' gia'
     * cento volte piu' grande (150% = 15000 centesimi di punto percentuale). */
    return (int)((long)cent * base / (unita == REL_PERC ? 10000 : 100));
}

/* =============================================================================
 * calc(), min(), max(), clamp() (P6, 25 November 2026)
 *
 * Google's sheets and every framework write lengths as expressions:
 * `calc(100% - 2*var(--gap))`, `min(100%, 1200px)`, `clamp(14px, 1vw +
 * 8px, 20px)`. Until today the whole declaration was thrown away.
 *
 * ! THE TEXT IS KEPT, AND READ WHEN THE ELEMENT IS KNOWN. An expression with
 * only px, pt, vw, vh, rem is a number already, and becomes one when the
 * sheet is read (leggi_calc). One with em or % depends on the element (em
 * on its font size, % on the container): its text stays in the arena and
 * rel_px reads it at the end of css_calcola, as the plain em do.
 *
 * ! THE % OF A WIDTH IS THE CONTAINER'S, which only the layout knows, and
 * CssStile has room for a percentage OR pixels, not both (posa_relativa):
 *   calc(100% / 3)      a percentage           -> 33.33%
 *   min(100%, 1200px)   the % is dropped in min/max/clamp -> 1200px
 *   calc(100% - 20px)   neither               -> the declaration is lost
 * This is the point where P7 (Taffy, a computed style of its own) takes over.
 *
 * Doubles: x87 code, no library call (the libc of the host or none).
 * ============================================================================= */
static int parola_e(const char *v, unsigned int n, const char *p);

typedef struct {
    double em;           /* px of 1em   */
    double perc;         /* px of 100%  */
    double rem;          /* px of 1rem: our body size, 16 in a media query */
    int    perc_ignota;  /* 1: % cannot be known here, and taints what uses it */
} CalcBasi;

typedef struct { double v; int lung; int tinto; } CalcVal;   /* v in px if lung */

typedef struct {
    const char     *s;
    unsigned int    i, n;
    const CalcBasi *b;
    int             prof;
} CalcL;

#define CALC_TETTO 1.0e7

static void c_spazi(CalcL *l)
{
    while (l->i < l->n && spazio((unsigned char)l->s[l->i])) l->i++;
}

static int c_lettera(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int c_cifra(int c) { return c >= '0' && c <= '9'; }

static int c_somma(CalcL *l, CalcVal *out);

/* the px of one `u` (k letters), or -1: not a length unit */
static double c_unita(const char *u, unsigned int k, const CalcBasi *b)
{
    static const struct { const char *n; double px; } FISSE[] = {
        { "px", 1.0 }, { "pt", 4.0 / 3.0 }, { "pc", 16.0 }, { "in", 96.0 },
        { "cm", 96.0 / 2.54 }, { "mm", 96.0 / 25.4 }, { "q", 96.0 / 101.6 },
        { 0, 0 }
    };
    int j;

    for (j = 0; FISSE[j].n; j++) if (parola_e(u, k, FISSE[j].n)) return FISSE[j].px;
    if (parola_e(u, k, "em") || parola_e(u, k, "ic")) return b->em;
    if (parola_e(u, k, "ex") || parola_e(u, k, "ch")) return b->em / 2;
    if (parola_e(u, k, "cap")) return b->em * 0.7;
    if (parola_e(u, k, "lh"))  return b->em * 1.2;
    if (parola_e(u, k, "rem")) return b->rem;
    if (parola_e(u, k, "rlh")) return b->rem * 1.2;
    if (parola_e(u, k, "vw") || parola_e(u, k, "svw") || parola_e(u, k, "lvw") ||
        parola_e(u, k, "dvw")) return g_media_w / 100.0;
    if (parola_e(u, k, "vh") || parola_e(u, k, "svh") || parola_e(u, k, "lvh") ||
        parola_e(u, k, "dvh")) return g_media_h / 100.0;
    if (parola_e(u, k, "vmin")) return (g_media_w < g_media_h ? g_media_w : g_media_h) / 100.0;
    if (parola_e(u, k, "vmax")) return (g_media_w > g_media_h ? g_media_w : g_media_h) / 100.0;
    return -1;
}

/* a number, with its unit or its % */
static int c_numero(CalcL *l, CalcVal *out)
{
    double v = 0, sc = 1;
    int    cifre = 0, neg = 0;
    unsigned int k;

    out->lung = out->tinto = 0;
    if (l->i < l->n && (l->s[l->i] == '+' || l->s[l->i] == '-')) { neg = l->s[l->i] == '-'; l->i++; }
    while (l->i < l->n && c_cifra(l->s[l->i])) { v = v * 10 + (l->s[l->i] - '0'); l->i++; cifre = 1; }
    if (l->i < l->n && l->s[l->i] == '.') {
        l->i++;
        while (l->i < l->n && c_cifra(l->s[l->i])) { sc /= 10; v += (l->s[l->i] - '0') * sc; l->i++; cifre = 1; }
    }
    if (!cifre) return 0;
    /* 1e3, 2E-2: an exponent only when a digit follows (1em is a unit) */
    if (l->i + 1 < l->n && (l->s[l->i] == 'e' || l->s[l->i] == 'E') &&
        (c_cifra(l->s[l->i + 1]) ||
         ((l->s[l->i + 1] == '-' || l->s[l->i + 1] == '+') && l->i + 2 < l->n && c_cifra(l->s[l->i + 2])))) {
        int e = 0, eneg = 0;

        l->i++;
        if (l->s[l->i] == '-' || l->s[l->i] == '+') { eneg = l->s[l->i] == '-'; l->i++; }
        while (l->i < l->n && c_cifra(l->s[l->i]) && e < 40) { e = e * 10 + (l->s[l->i] - '0'); l->i++; }
        while (e-- > 0) v = eneg ? v / 10 : v * 10;
    }
    if (neg) v = -v;
    if (l->i < l->n && l->s[l->i] == '%') {
        l->i++;
        if (l->b->perc_ignota) out->tinto = 1;
        out->v = v * l->b->perc / 100.0;
        out->lung = 1;
        return 1;
    }
    for (k = 0; l->i + k < l->n && c_lettera(l->s[l->i + k]); k++) {}
    if (k) {
        double u = c_unita(l->s + l->i, k, l->b);

        if (u < 0) return 0;
        l->i += k;
        out->v = v * u;
        out->lung = 1;
        return 1;
    }
    out->v = v;
    return 1;
}

/* min() and max(): a list. Under perc_ignota a tainted argument does not
 * take part (min(100%, 1200px) is 1200px); all tainted, the result is. */
static int c_lista(CalcL *l, CalcVal *out, int massimo)
{
    int     primo = 1, tutti_tinti = 1;
    CalcVal x;

    out->v = 0; out->lung = 0; out->tinto = 0;
    for (;;) {
        if (!c_somma(l, &x)) return 0;
        if (!primo && !tutti_tinti && !x.tinto && x.lung != out->lung) return 0;
        if (!x.tinto) {
            if (tutti_tinti || (massimo ? x.v > out->v : x.v < out->v)) { out->v = x.v; out->lung = x.lung; }
            tutti_tinti = 0;
        } else if (tutti_tinti) {
            out->lung = x.lung;
        }
        primo = 0;
        c_spazi(l);
        if (l->i < l->n && l->s[l->i] == ',') { l->i++; continue; }
        break;
    }
    out->tinto = tutti_tinti;
    return 1;
}

static int c_chiudi(CalcL *l)
{
    c_spazi(l);
    if (l->i >= l->n || l->s[l->i] != ')') return 0;
    l->i++;
    return 1;
}

static int c_fattore(CalcL *l, CalcVal *out)
{
    char         c;
    unsigned int a, k;
    int          ok = 0;

    c_spazi(l);
    if (l->i >= l->n || l->prof > 32) return 0;
    c = l->s[l->i];
    if (c == '(') {
        l->i++;
        l->prof++;
        ok = c_somma(l, out) && c_chiudi(l);
        l->prof--;
        return ok;
    }
    if (c_cifra(c) || c == '.' ||
        ((c == '+' || c == '-') && l->i + 1 < l->n &&
         (c_cifra(l->s[l->i + 1]) || l->s[l->i + 1] == '.')))
        return c_numero(l, out);
    if (c == '-' && l->i + 1 < l->n && (l->s[l->i + 1] == '(' || c_lettera(l->s[l->i + 1])) &&
        !(l->i + 8 <= l->n && parola_e(l->s + l->i, 8, "-webkit-")) &&
        !(l->i + 5 <= l->n && parola_e(l->s + l->i, 5, "-moz-"))) {
        l->i++;
        if (!c_fattore(l, out)) return 0;
        out->v = -out->v;
        return 1;
    }

    /* a function */
    a = l->i;
    for (k = a; k < l->n && (c_lettera(l->s[k]) || l->s[k] == '-'); k++) {}
    if (k >= l->n || l->s[k] != '(' || k == a) return 0;
    l->i = k + 1;
    l->prof++;
    if (parola_e(l->s + a, k - a, "calc") || parola_e(l->s + a, k - a, "-webkit-calc") ||
        parola_e(l->s + a, k - a, "-moz-calc")) {
        ok = c_somma(l, out) && c_chiudi(l);
    } else if (parola_e(l->s + a, k - a, "min")) {
        ok = c_lista(l, out, 0) && c_chiudi(l);
    } else if (parola_e(l->s + a, k - a, "max")) {
        ok = c_lista(l, out, 1) && c_chiudi(l);
    } else if (parola_e(l->s + a, k - a, "clamp")) {
        CalcVal x, y, z;

        /* clamp(MIN, VAL, MAX) = max(MIN, min(VAL, MAX)), a tainted one left out */
        if (c_somma(l, &x)) {
            c_spazi(l);
            if (l->i < l->n && l->s[l->i] == ',') {
                l->i++;
                if (c_somma(l, &y)) {
                    c_spazi(l);
                    if (l->i < l->n && l->s[l->i] == ',') {
                        l->i++;
                        if (c_somma(l, &z) && c_chiudi(l)) {
                            CalcVal m;

                            if (y.tinto && !z.tinto)      m = z;        /* min(VAL, MAX) */
                            else if (z.tinto && !y.tinto) m = y;
                            else                          m = (z.v < y.v) ? z : y;
                            if (!x.tinto && (m.tinto || x.v > m.v)) m = x;   /* max(MIN, .) */
                            *out = m;
                            ok = 1;
                        }
                    }
                }
            }
        }
    }
    l->prof--;
    return ok;
}

static int c_prodotto(CalcL *l, CalcVal *out)
{
    CalcVal b;

    if (!c_fattore(l, out)) return 0;
    for (;;) {
        char op;

        c_spazi(l);
        if (l->i >= l->n || (l->s[l->i] != '*' && l->s[l->i] != '/')) return 1;
        op = l->s[l->i++];
        if (!c_fattore(l, &b)) return 0;
        if (op == '*') {
            if (out->lung && b.lung) return 0;
            out->v *= b.v;
            out->lung |= b.lung;
        } else {
            if (b.lung || b.v == 0) return 0;
            out->v /= b.v;
        }
        out->tinto |= b.tinto;
        if (out->v > CALC_TETTO) out->v = CALC_TETTO;
        if (out->v < -CALC_TETTO) out->v = -CALC_TETTO;
    }
}

static int c_somma(CalcL *l, CalcVal *out)
{
    CalcVal b;

    if (!c_prodotto(l, out)) return 0;
    for (;;) {
        char op;

        c_spazi(l);
        if (l->i >= l->n || (l->s[l->i] != '+' && l->s[l->i] != '-')) return 1;
        op = l->s[l->i++];
        if (!c_prodotto(l, &b)) return 0;
        /* a bare 0 next to a length is a length (browsers forgive it) */
        if (out->lung != b.lung && !(b.v == 0 && !b.lung) && !(out->v == 0 && !out->lung)) return 0;
        out->v = (op == '+') ? out->v + b.v : out->v - b.v;
        out->lung |= b.lung;
        out->tinto |= b.tinto;
    }
}

static int c_arrotonda(double v)
{
    if (v > CALC_TETTO) v = CALC_TETTO;
    if (v < -CALC_TETTO) v = -CALC_TETTO;
    return v >= 0 ? (int)(v + 0.5) : -(int)(-v + 0.5);
}

/* The whole value `s` (n bytes), which must be one expression: its px. */
static int calc_valuta(const char *s, unsigned int n, const CalcBasi *b, int *px, int *tinto)
{
    CalcL   l;
    CalcVal v;

    l.s = s; l.i = 0; l.n = n; l.b = b; l.prof = 0;
    if (!c_somma(&l, &v)) return 0;
    c_spazi(&l);
    if (l.i != l.n) return 0;
    if (!v.lung && v.v != 0) return 0;      /* a number is not a length */
    *px = c_arrotonda(v.v);
    *tinto = v.tinto;
    return 1;
}

static int calc_valuta_testo(const char *s, int base_em, int base_perc, int perc_ignota,
                             int *px, int *tinto)
{
    CalcBasi     b;
    unsigned int n = 0;

    while (s[n]) n++;
    b.em = base_em; b.perc = base_perc; b.perc_ignota = perc_ignota;
    b.rem = CSS_CORPO_PREDEFINITO;
    return calc_valuta(s, n, &b, px, tinto);
}

static int e_calc(const char *v, unsigned int n)
{
    static const char *const NOMI[] = { "calc(", "-webkit-calc(", "-moz-calc(",
                                        "min(", "max(", "clamp(", 0 };
    int j;

    for (j = 0; NOMI[j]; j++) {
        unsigned int k = 0;

        while (NOMI[j][k] && k < n && minusc((unsigned char)v[k]) == NOMI[j][k]) k++;
        if (!NOMI[j][k]) return 1;
    }
    return 0;
}

static CssFoglio *g_fog_legge;              /* defined (= 0) with leggi_url */

static const char *calc_testo(unsigned int num)
{
    unsigned int unita = (num >> 24) & 0x7Fu, off = num & 0xFFFFFFu;

    if (unita == REL_CALC_L) return off < sizeof(g_loc) ? g_loc + off : 0;
    if (unita == REL_CALC_F && g_fog_calc && off < g_fog_calc->arena_n) return g_fog_calc->arena + off;
    return 0;
}

/* A length written as an expression. Constant: px now. Otherwise its text,
 * kept where it lives as long as the declaration does. */
static int leggi_calc(const char *v, unsigned int n, int neg, unsigned int *out)
{
    CalcBasi     b1, b2;
    int          p1, p2, t;
    unsigned int off, unita;

    b1.em = 16; b1.perc = 1000; b1.perc_ignota = 0; b1.rem = CSS_CORPO_PREDEFINITO;
    b2.em = 23; b2.perc = 1777; b2.perc_ignota = 0; b2.rem = CSS_CORPO_PREDEFINITO;
    if (!calc_valuta(v, n, &b1, &p1, &t) || !calc_valuta(v, n, &b2, &p2, &t)) return 0;
    if (p1 == p2) {
        /* ! A NEGATIVE RESULT IS CLAMPED, NOT REFUSED, where negatives are
         * not allowed: so says css-values for calc(), at computed time */
        if (!neg && p1 < 0) p1 = 0;
        if (p1 < -20000) p1 = -20000;
        if (p1 >  20000) p1 =  20000;
        *out = (unsigned int)(p1 + 32768);
        return 1;
    }
    if (g_fog_legge) {
        CssFoglio *f = g_fog_legge;

        if (f->arena_n + n + 1 > f->arena_max || f->arena_n + n + 1 > 0xFFFFFFu) { f->troncato = 1; return 0; }
        off = f->arena_n;
        for (t = 0; t < (int)n; t++) f->arena[off + (unsigned int)t] = v[t];
        f->arena[off + n] = 0;
        f->arena_n += n + 1;
        unita = REL_CALC_F;
    } else {
        off = loc_metti(v, n);
        if (!off) return 0;
        unita = REL_CALC_L;
    }
    *out = REL_BIT | (unita << 24) | off;
    return 1;
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

static int ff_famiglia(const CssFoglio *f, const char *nome, unsigned int n);
static unsigned int arena_coppia(CssFoglio *f, const char *a, unsigned int na,
                                 const char *b, unsigned int nb, int minuscolo);

/* ! A font-family IN A SHEET IS READ IN css_calcola (P6), not here: whether
 * «Roboto» is the page's own @font-face or just a name for sans-serif
 * depends on the @font-face blocks, which may come in a later sheet. The
 * list is kept in the arena, and its CssDich says FAM_RINVIO + where. */
#define FAM_RINVIO 0x40000000u

/* The list against the page's families and ours: the first page family
 * named, with the first generic after it (CSS_FAM_PAGINA), or the first of
 * ours, as before. */
static int famiglia_risolvi(const CssFoglio *f, const char *v, unsigned int n, unsigned int *out)
{
    unsigned int i = 0;
    int          k = -1;

    while (i < n) {
        unsigned int a, b;
        int          q2;

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

        if (f && k < 0) {
            int q = ff_famiglia(f, v + a, b - a);

            if (q >= 0) { k = q; continue; }
        }
        for (q2 = 0; FAMIGLIE[q2].nome; q2++)
            if (fam_uguale(v + a, b - a, FAMIGLIE[q2].nome)) {
                *out = k >= 0 ? (unsigned int)CSS_FAM_PAGINA(k, FAMIGLIE[q2].fam) : FAMIGLIE[q2].fam;
                return 1;
            }
    }
    if (k >= 0) { *out = (unsigned int)CSS_FAM_PAGINA(k, CSS_FAM_SANS); return 1; }

    /* ! NESSUN NOME RICONOSCIUTO: LA DICHIARAZIONE SI BUTTA, e non si ripiega
     * sul serif. Buttarla lascia in piedi quella ereditata, che e' quasi sempre
     * piu' vicina al vero di una scelta presa a caso da noi. */
    return 0;
}

static int leggi_famiglia(const char *v, unsigned int n, unsigned int *out)
{
    if (g_fog_legge) {
        unsigned int o = arena_coppia(g_fog_legge, v, n, "", 0, 0);

        if (!o || o >= FAM_RINVIO) return 0;
        *out = FAM_RINVIO | o;
        return 1;
    }
    return famiglia_risolvi(g_fog_calc, v, n, out);
}

/* -----------------------------------------------------------------------------
 * I nomi delle proprieta'
 * --------------------------------------------------------------------------- */
typedef struct { const char *nome; unsigned short codice; } PropNota;

static const PropNota PROPRIETA[] = {
    { "color",            CSS_P_COLORE     },
    { "background-color", CSS_P_SFONDO     },
    { "background-image",      CSS_P_SFONDO_IMM },
    { "background-repeat",     CSS_P_SFONDO_RIP },
    { "background-position-x", CSS_P_SFONDO_PX  },
    { "background-position-y", CSS_P_SFONDO_PY  },
    { "height",                CSS_P_ALT        },
    { "min-height",            CSS_P_ALT_MIN    },
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

/* =============================================================================
 * L'immagine di sfondo (10 ottobre 2026)
 *
 * ! UN INDIRIZZO NON STA IN UN NUMERO, e tutto il resto di questo file posa
 * numeri. Qui il «numero» e' un PUNTATORE al testo dell'indirizzo, che deve
 * quindi vivere quanto lo stile che lo porta:
 *   - letto da un foglio (css_analizza), il testo sta in un buffer che chi
 *     chiama riusa per il foglio dopo: l'indirizzo si copia nell'arena del
 *     foglio, con davanti quattro byte che puntano all'indirizzo del foglio
 *     stesso (css_base). Un url() dentro un foglio e' relativo AL FOGLIO, non
 *     alla pagina, ed e' la differenza fra trovare lo sprite e chiederlo a un
 *     indirizzo che non esiste;
 *   - letto da un attributo style, il testo e' gia' dentro il documento e
 *     resta li': ci si punta e basta.
 * css_sfondo_url() distingue i due casi dall'indirizzo: dentro l'arena o no.
 * ========================================================================== */
static CssFoglio *g_fog_legge = 0;          /* il foglio che css_analizza sta leggendo */
static int        g_sost_attiva;            /* defined with var_in_linea */
static unsigned int var_testo_metti_url(const char *s, unsigned int n);

static int leggi_url(const char *v, unsigned int n, unsigned int *out)
{
    unsigned int i = 0, a, b;

    if (parola_e(v, n, "none")) { *out = 0; return 1; }

    /* url( ... ) dovunque nel valore: e' cosi' che lo si trova anche dentro
     * la scorciatoia `background`. */
    for (; i + 4 <= n; i++)
        if ((v[i] == 'u' || v[i] == 'U') && (v[i + 1] == 'r' || v[i + 1] == 'R') &&
            (v[i + 2] == 'l' || v[i + 2] == 'L') && v[i + 3] == '(') break;
    if (i + 4 > n) return 0;
    a = i + 4;
    while (a < n && (spazio((unsigned char)v[a]) || v[a] == '"' || v[a] == '\'')) a++;
    b = a;
    while (b < n && v[b] != ')' && v[b] != '"' && v[b] != '\'') b++;
    while (b > a && spazio((unsigned char)v[b - 1])) b--;
    if (b <= a) return 0;

    /* ! UN'IMMAGINE SCRITTA NEL FOGLIO (data:...) NON SI COPIA: non la si sa
     * ancora disegnare, e sono migliaia di byte l'una. Il foglio di Yahoo ne
     * ha decine: copiate nell'arena la riempivano, il foglio risultava
     * «troncato» e la pagina perdeva TUTTO il suo stile per far posto a
     * immagini che non avrebbe mostrato. Vale come «nessuna immagine». Lo
     * stesso per un indirizzo irragionevolmente lungo. */
    if ((b - a >= 5 && v[a] == 'd' && v[a + 1] == 'a' && v[a + 2] == 't' &&
         v[a + 3] == 'a' && v[a + 4] == ':') || b - a > 1000) {
        *out = 0;
        return 1;
    }

    if (g_fog_legge) {
        CssFoglio   *f = g_fog_legge;
        unsigned int serve = 4 + (b - a) + 1, k;
        char        *dove;

        /* Allineato a 4: davanti all'indirizzo sta un puntatore. */
        f->arena_n = (f->arena_n + 3u) & ~3u;
        if (f->arena_n + serve > f->arena_max) { f->troncato = 1; return 0; }
        dove = f->arena + f->arena_n;
        *(const char **)(void *)dove = f->base;
        for (k = 0; k < b - a; k++) dove[4 + k] = v[a + k];
        dove[4 + (b - a)] = 0;
        f->arena_n += serve;
        *out = (unsigned int)(dove + 4);
    } else if (g_sost_attiva) {
        /* ! A VALUE MADE BY var() LIVES IN g_loc, which the next element
         * empties: the address is copied where it lasts the whole layout */
        unsigned int t = var_testo_metti_url(v + a, b - a);

        if (!t) return 0;
        *out = t;
    } else {
        *out = (unsigned int)(v + a);
    }
    return 1;
}

/* Una coordinata della posizione: pixel, percento, o una parola. */
static int leggi_sf_pos(const char *v, unsigned int n, unsigned int *out)
{
    unsigned int l;

    if (parola_e(v, n, "left") || parola_e(v, n, "top"))     { *out = CSS_SF_PERC + 0;   return 1; }
    if (parola_e(v, n, "center"))                            { *out = CSS_SF_PERC + 50;  return 1; }
    if (parola_e(v, n, "right") || parola_e(v, n, "bottom")) { *out = CSS_SF_PERC + 100; return 1; }
    if (!leggi_lunghezza(v, n, 1, &l)) return 0;
    if (l & REL_BIT) {
        int cent;

        if (((l >> 24) & 0x7Fu) != REL_PERC) return 0;          /* em, rem: rari qui */
        cent = ((int)(l & 0xFFFFFFu) - (int)REL_ZERO) / 100;    /* vedi rel_codifica */
        if (cent < 0) cent = 0;
        if (cent > 100) cent = 100;
        *out = (unsigned int)(CSS_SF_PERC + cent);
        return 1;
    }
    *out = (unsigned int)(unsigned short)(short)((int)l - 32768);
    return 1;
}

/* Un lato della misura: auto, pixel, percento; cover e contain valgono per
 * tutti e due e li scrive la scorciatoia. */
static int leggi_sf_mis(const char *v, unsigned int n, unsigned int *out)
{
    unsigned int l;

    if (parola_e(v, n, "auto"))    { *out = 0; return 1; }
    if (parola_e(v, n, "cover"))   { *out = (unsigned int)(unsigned short)(short)CSS_SF_COPRE;    return 1; }
    if (parola_e(v, n, "contain")) { *out = (unsigned int)(unsigned short)(short)CSS_SF_CONTIENE; return 1; }
    if (!leggi_lunghezza(v, n, 0, &l)) return 0;
    if (l & REL_BIT) {
        int cent;

        if (((l >> 24) & 0x7Fu) != REL_PERC) return 0;
        cent = ((int)(l & 0xFFFFFFu) - (int)REL_ZERO) / 100;
        if (cent < 1) cent = 1;
        if (cent > 400) cent = 400;
        *out = (unsigned int)(unsigned short)(short)(-(100 + cent));
        return 1;
    }
    *out = (unsigned int)(unsigned short)(short)((int)l - 32768);
    return 1;
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

    /* ! L'ALTEZZA IN PERCENTO NON SI PRENDE: sarebbe del contenitore, e qui
     * l'altezza di un contenitore la decide quel che contiene. Buttarla e'
     * meglio che leggerla come pixel. */
    case CSS_P_ALT: case CSS_P_ALT_MIN:
        if (parola_e(v, n, "auto") || parola_e(v, n, "none")) { *out = 0; return 1; }
        if (!leggi_lunghezza(v, n, 0, out)) return 0;
        if ((*out & REL_BIT) && ((*out >> 24) & 0x7Fu) == REL_PERC) return 0;
        return 1;

    case CSS_P_SFONDO_IMM:
        return leggi_url(v, n, out);

    case CSS_P_SFONDO_RIP:
        if (parola_e(v, n, "no-repeat")) { *out = CSS_SF_UNA;      return 1; }
        if (parola_e(v, n, "repeat"))    { *out = CSS_SF_RIPETE;   return 1; }
        if (parola_e(v, n, "repeat-x"))  { *out = CSS_SF_RIPETE_X; return 1; }
        if (parola_e(v, n, "repeat-y"))  { *out = CSS_SF_RIPETE_Y; return 1; }
        return 0;

    case CSS_P_SFONDO_PX: case CSS_P_SFONDO_PY:
        return leggi_sf_pos(v, n, out);

    case CSS_P_SFONDO_MW: case CSS_P_SFONDO_MH:
        return leggi_sf_mis(v, n, out);

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

/* "a\0b\0" into the arena (`a` in lower case if asked): the offset, 0 if
 * there is no room (and the sheet says it is cut). */
static unsigned int arena_coppia(CssFoglio *f, const char *a, unsigned int na,
                                 const char *b, unsigned int nb, int minuscolo)
{
    unsigned int o = f->arena_n, i;

    if (f->arena_n + na + nb + 2 > f->arena_max) { f->troncato = 1; return 0; }
    for (i = 0; i < na; i++) f->arena[o + i] = minuscolo ? (char)minusc((unsigned char)a[i]) : a[i];
    f->arena[o + na] = 0;
    for (i = 0; i < nb; i++) f->arena[o + na + 1 + i] = b[i];
    f->arena[o + na + 1 + nb] = 0;
    f->arena_n += na + nb + 2;
    return o;
}

/* Is there a var() (or env()) in the value? Not as part of a longer name. */
static int ha_var(const char *v, unsigned int n)
{
    unsigned int i;

    for (i = 0; i + 4 <= n; i++) {
        if (v[i + 3] != '(') continue;
        if (i > 0 && (nomeok((unsigned char)v[i - 1]))) continue;
        if ((minusc((unsigned char)v[i]) == 'v' && minusc((unsigned char)v[i + 1]) == 'a' &&
             minusc((unsigned char)v[i + 2]) == 'r') ||
            (minusc((unsigned char)v[i]) == 'e' && minusc((unsigned char)v[i + 1]) == 'n' &&
             minusc((unsigned char)v[i + 2]) == 'v')) return 1;
    }
    return 0;
}

static void var_in_linea(DichIter *it, const char *nome, unsigned int nn,
                         const char *v, unsigned int n);

/* =============================================================================
 * @font-face (P6, 25 November 2026)
 *
 * The block says: this family name, in this weight and style, is in these
 * files. It applies to no element: it is kept in the arena, with a CssDich
 * that no rule points to (CSS_P_FONTFACE), and css_font_face() gives the
 * blocks to the browser, which fetches and draws them.
 *
 * The record, aligned to 4: the sheet's address as an arena offset (4
 * bytes, 0 = the page), weight from and to (2 + 2), italic (1), three bytes
 * of nothing, then "family\0src\0unicode-range\0", the family in lower case
 * and without quotes.
 * ============================================================================= */
#define FF_TESTA 12

/* the next «name: value» of a block, raw; 0 at the end */
static int dich_grezza(const char *t, unsigned int *pi, unsigned int n,
                       unsigned int *na, unsigned int *nb, unsigned int *va, unsigned int *vb)
{
    unsigned int i = *pi;

    for (;;) {
        unsigned int liv = 0;

        while (i < n && (spazio((unsigned char)t[i]) || t[i] == ';')) i++;
        if (i + 1 < n && t[i] == '/' && t[i + 1] == '*') {
            for (i += 2; i + 1 < n && !(t[i] == '*' && t[i + 1] == '/'); i++) {}
            i += 2;
            continue;
        }
        if (i >= n) { *pi = n; return 0; }
        *na = i;
        while (i < n && nomeok((unsigned char)t[i])) i++;
        *nb = i;
        while (i < n && spazio((unsigned char)t[i])) i++;
        if (i >= n || t[i] != ':') {
            while (i < n && t[i] != ';') i++;
            continue;
        }
        i++;
        while (i < n && spazio((unsigned char)t[i])) i++;
        *va = i;
        while (i < n && (liv || t[i] != ';')) {
            if (t[i] == '"' || t[i] == '\'') {
                char q = t[i];

                for (i++; i < n && t[i] != q; i++) if (t[i] == '\\') i++;
            } else if (t[i] == '(') liv++;
            else if (t[i] == ')' && liv) liv--;
            if (i < n) i++;
        }
        *vb = i;
        while (*vb > *va && spazio((unsigned char)t[*vb - 1])) (*vb)--;
        *pi = i;
        return 1;
    }
}

/* a font-weight: «bold», «400», «100 900» */
static void ff_peso(const char *v, unsigned int n, unsigned short *da, unsigned short *a)
{
    unsigned int i = 0, x[2] = { 0, 0 }, k = 0;

    if (parola_e(v, n, "bold")) { *da = *a = 700; return; }
    if (parola_e(v, n, "normal")) { *da = *a = 400; return; }
    while (i < n && k < 2) {
        while (i < n && !c_cifra(v[i])) i++;
        if (i >= n) break;
        while (i < n && c_cifra(v[i])) { x[k] = x[k] * 10 + (unsigned int)(v[i] - '0'); i++; }
        while (i < n && (c_cifra(v[i]) || v[i] == '.')) i++;
        k++;
    }
    if (k == 0 || x[0] < 1 || x[0] > 1000) return;
    *da = (unsigned short)x[0];
    *a  = (unsigned short)(k == 2 && x[1] >= x[0] && x[1] <= 1000 ? x[1] : x[0]);
}

static void fontface_metti(CssFoglio *f, const char *t, unsigned int n)
{
    unsigned int   i = 0, na, nb, va, vb, fa = 0, fb = 0, sa = 0, sb = 0, ua = 0, ub = 0, o, k, base;
    unsigned short pda = 400, pa = 400;
    unsigned char  cors = 0;

    while (dich_grezza(t, &i, n, &na, &nb, &va, &vb)) {
        if (parola_e(t + na, nb - na, "font-family")) { fa = va; fb = vb; }
        else if (parola_e(t + na, nb - na, "src")) { sa = va; sb = vb; }
        else if (parola_e(t + na, nb - na, "unicode-range")) { ua = va; ub = vb; }
        else if (parola_e(t + na, nb - na, "font-weight")) ff_peso(t + va, vb - va, &pda, &pa);
        else if (parola_e(t + na, nb - na, "font-style"))
            cors = (vb - va >= 6 && (minusc((unsigned char)t[va]) == 'i' || minusc((unsigned char)t[va]) == 'o'));
    }
    while (fb > fa && (t[fa] == '"' || t[fa] == '\'' || spazio((unsigned char)t[fa]))) fa++;
    while (fb > fa && (t[fb - 1] == '"' || t[fb - 1] == '\'' || spazio((unsigned char)t[fb - 1]))) fb--;
    if (fb <= fa || sb <= sa || f->dich_n >= f->dich_max) return;

    base = f->base ? (unsigned int)(f->base - f->arena) : 0;
    f->arena_n = (f->arena_n + 3u) & ~3u;
    o = f->arena_n;
    if (o + FF_TESTA + (fb - fa) + (sb - sa) + (ub - ua) + 3 > f->arena_max) { f->troncato = 1; return; }
    f->arena[o + 0] = (char)(base & 0xFF);         f->arena[o + 1] = (char)((base >> 8) & 0xFF);
    f->arena[o + 2] = (char)((base >> 16) & 0xFF); f->arena[o + 3] = (char)((base >> 24) & 0xFF);
    f->arena[o + 4] = (char)(pda & 0xFF); f->arena[o + 5] = (char)(pda >> 8);
    f->arena[o + 6] = (char)(pa & 0xFF);  f->arena[o + 7] = (char)(pa >> 8);
    f->arena[o + 8] = (char)cors; f->arena[o + 9] = f->arena[o + 10] = f->arena[o + 11] = 0;
    k = o + FF_TESTA;
    for (i = fa; i < fb; i++) f->arena[k++] = (char)minusc((unsigned char)t[i]);
    f->arena[k++] = 0;
    for (i = sa; i < sb; i++) f->arena[k++] = t[i];
    f->arena[k++] = 0;
    for (i = ua; i < ub; i++) f->arena[k++] = t[i];
    f->arena[k++] = 0;
    f->arena_n = k;
    f->dich[f->dich_n].proprieta = CSS_P_FONTFACE;
    f->dich[f->dich_n].numero    = o;
    f->dich[f->dich_n].prossima  = -1;
    f->dich_n++;
}

/* The index of the blocks of ONE sheet, made again when the sheet grows. */
#define FF_MAX 256
static struct {
    const CssFoglio *f;
    unsigned int     dich_n, arena_n, regole_n;
    unsigned int     n, off[FF_MAX];
    unsigned char    k[FF_MAX];
    unsigned int     nk, fam[CSS_FAM_PAGINA_MAX + 1];
} g_ff;

static void ff_indice(const CssFoglio *f)
{
    unsigned int i;

    if (g_ff.f == f && g_ff.dich_n == f->dich_n && g_ff.arena_n == f->arena_n &&
        g_ff.regole_n == f->regole_n) return;
    g_ff.f = f; g_ff.dich_n = f->dich_n; g_ff.arena_n = f->arena_n; g_ff.regole_n = f->regole_n;
    g_ff.n = g_ff.nk = 0;
    for (i = 0; i < f->dich_n && g_ff.n < FF_MAX; i++) {
        const char  *nome;
        unsigned int q;

        if (f->dich[i].proprieta != CSS_P_FONTFACE) continue;
        nome = f->arena + f->dich[i].numero + FF_TESTA;
        for (q = 0; q < g_ff.nk; q++) if (ug_min(f->arena + g_ff.fam[q], nome)) break;
        if (q == g_ff.nk) {
            if (g_ff.nk > CSS_FAM_PAGINA_MAX) continue;      /* too many families */
            g_ff.fam[g_ff.nk++] = f->dich[i].numero + FF_TESTA;
        }
        g_ff.off[g_ff.n] = f->dich[i].numero;
        g_ff.k[g_ff.n] = (unsigned char)q;
        g_ff.n++;
    }
}

static int ff_famiglia(const CssFoglio *f, const char *nome, unsigned int n)
{
    unsigned int q;

    if (!f || !f->dich) return -1;
    ff_indice(f);
    for (q = 0; q < g_ff.nk; q++) {
        const char  *x = f->arena + g_ff.fam[q];
        unsigned int j = 0;

        while (j < n && x[j] && x[j] == minusc((unsigned char)nome[j])) j++;
        if (j == n && x[j] == 0) return (int)q;
    }
    return -1;
}

int css_font_face(const CssFoglio *f, unsigned int i, CssFontFace *out)
{
    const char  *r;
    unsigned int base;

    if (!f || !f->dich || !out) return 0;
    ff_indice(f);
    if (i >= g_ff.n) return 0;
    r = f->arena + g_ff.off[i];
    base = (unsigned int)(unsigned char)r[0] | (unsigned int)(unsigned char)r[1] << 8 |
           (unsigned int)(unsigned char)r[2] << 16 | (unsigned int)(unsigned char)r[3] << 24;
    out->base       = base ? f->arena + base : 0;
    out->peso_min   = (unsigned short)((unsigned char)r[4] | (unsigned char)r[5] << 8);
    out->peso_max   = (unsigned short)((unsigned char)r[6] | (unsigned char)r[7] << 8);
    out->corsivo    = (unsigned char)r[8];
    out->k          = g_ff.k[i];
    out->famiglia   = r + FF_TESTA;
    out->src        = out->famiglia;
    while (*out->src) out->src++;
    out->src++;
    out->intervallo = out->src;
    while (*out->intervallo) out->intervallo++;
    out->intervallo++;
    if (!out->intervallo[0]) out->intervallo = 0;
    return 1;
}
static void vp_metti(const char *nome, unsigned int nn, const char *val, unsigned int vn,
                     unsigned int peso);
static int  g_raccogli;

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

    /* =====================================================================
     * background: colore, immagine, ripetizione, posizione [/ misura], in
     * qualunque ordine. Ogni parola si prova per quel che puo' essere; quelle
     * che non si capiscono (scroll, fixed, padding-box...) si saltano. E'
     * anche la scorciatoia che AZZERA: `background: red` toglie l'immagine
     * messa da una regola di prima.
     * ===================================================================== */
    if (parola_e(nome, nn, "background")) {
        unsigned int bd[12], bl[12], bk, bq, val, npos = 0, col = CSS_NIENTE;
        unsigned int url = 0, rip = CSS_SF_RIPETE, px = CSS_SF_PERC, py = CSS_SF_PERC;
        unsigned int mw = 0, mh = 0;
        int          nmis = -1;             /* >= 0: si stanno leggendo le misure */

        bk = parole(v, n, bd, bl, 12);
        for (bq = 0; bq < bk; bq++) {
            const char  *w = v + bd[bq];
            unsigned int l = bl[bq];

            /* «posizione / misura», anche attaccati: 50%/cover */
            {
                unsigned int z;

                for (z = 0; z < l; z++) if (w[z] == '/') break;
                if (z < l && !(l >= 4 && w[3] == '(')) {
                    if (z > 0 && leggi_sf_pos(w, z, &val)) { if (npos == 0) px = val; else py = val; npos++; }
                    nmis = 0;
                    w += z + 1; l -= z + 1;
                    if (l == 0) continue;
                }
            }
            if (nmis >= 0 && leggi_sf_mis(w, l, &val)) {
                if (nmis == 0) { mw = val; mh = ((short)(unsigned short)val < 0 && (short)(unsigned short)val > -100) ? val : 0; }
                else mh = val;
                nmis++;
                continue;
            }
            if (leggi_url(w, l, &val)) { url = val; continue; }
            if (leggi_valore(CSS_P_SFONDO_RIP, w, l, &val)) { rip = val; continue; }
            if (leggi_sf_pos(w, l, &val)) {
                /* Una parola verticale detta per prima (top, bottom) e' la y. */
                if (parola_e(w, l, "top") || parola_e(w, l, "bottom")) py = val;
                else if (npos == 0) px = val; else py = val;
                npos++;
                continue;
            }
            if (leggi_colore(w, l, &val)) { col = val; continue; }
        }
        if (npos == 1 && py == CSS_SF_PERC) py = CSS_SF_PERC + 50;   /* un valore solo: l'altro e' center */
        coda_metti(it, CSS_P_SFONDO, col);
        coda_metti(it, CSS_P_SFONDO_IMM, url);
        coda_metti(it, CSS_P_SFONDO_RIP, rip);
        coda_metti(it, CSS_P_SFONDO_PX, px);
        coda_metti(it, CSS_P_SFONDO_PY, py);
        coda_metti(it, CSS_P_SFONDO_MW, mw);
        coda_metti(it, CSS_P_SFONDO_MH, mh);
        return 1;
    }
    if (parola_e(nome, nn, "background-position")) {
        unsigned int bd[2], bl[2], bk, a = CSS_SF_PERC, b = CSS_SF_PERC + 50;

        bk = parole(v, n, bd, bl, 2);
        if (bk >= 1 && !leggi_sf_pos(v + bd[0], bl[0], &a)) return 1;
        if (bk >= 2 && !leggi_sf_pos(v + bd[1], bl[1], &b)) return 1;
        if (bk >= 1 && (parola_e(v + bd[0], bl[0], "top") || parola_e(v + bd[0], bl[0], "bottom"))) {
            unsigned int t = a; a = (bk >= 2) ? b : CSS_SF_PERC + 50; b = t;
        }
        coda_metti(it, CSS_P_SFONDO_PX, a);
        coda_metti(it, CSS_P_SFONDO_PY, b);
        return 1;
    }
    if (parola_e(nome, nn, "background-size")) {
        unsigned int bd[2], bl[2], bk, a = 0, b = 0;

        bk = parole(v, n, bd, bl, 2);
        if (bk >= 1 && !leggi_sf_mis(v + bd[0], bl[0], &a)) return 1;
        if (bk >= 2 && !leggi_sf_mis(v + bd[1], bl[1], &b)) return 1;
        if (bk == 1 && (short)(unsigned short)a < 0 && (short)(unsigned short)a > -100) b = a;   /* cover, contain */
        coda_metti(it, CSS_P_SFONDO_MW, a);
        coda_metti(it, CSS_P_SFONDO_MH, b);
        return 1;
    }

    /* =====================================================================
     * font: [style] [variant] [weight] [stretch] size[/line-height] family
     * (P6). Google writes almost all its text this way. ! WHAT IT DOES
     * NOT SAY GOES BACK TO normal, as with every shorthand. A system font
     * («font: menu») is not ours to know: the declaration is dropped.
     * ===================================================================== */
    if (parola_e(nome, nn, "font")) {
        unsigned int fd[8], fl[8], fk, fq, stile = 0, peso = 0, corpo, fam, z, w;

        fk = parole(v, n, fd, fl, 8);
        for (fq = 0; fq < fk; fq++) {
            const char  *t = v + fd[fq];
            unsigned int l = fl[fq];

            if (parola_e(t, l, "italic") || parola_e(t, l, "oblique")) { stile = 1; continue; }
            if (parola_e(t, l, "bold") || parola_e(t, l, "bolder")) { peso = 1; continue; }
            if (parola_e(t, l, "normal") || parola_e(t, l, "small-caps") || parola_e(t, l, "lighter") ||
                parola_e(t, l, "condensed") || parola_e(t, l, "semi-condensed") ||
                parola_e(t, l, "expanded") || parola_e(t, l, "semi-expanded")) continue;
            if (l == 3 && c_cifra(t[0]) && t[1] == '0' && t[2] == '0') { peso = t[0] >= '6'; continue; }
            /* the size, perhaps with its line height glued on */
            for (z = 0; z < l && t[z] != '/'; z++) {}
            if (!leggi_valore(CSS_P_CORPO, t, z, &corpo)) return 1;
            w = fd[fq] + l;
            /* «14px / 20px»: the line height as words of its own */
            if (z == l && fq + 1 < fk && v[fd[fq + 1]] == '/') {
                w = fd[fq + 1] + fl[fq + 1];
                if (fl[fq + 1] == 1 && fq + 2 < fk) w = fd[fq + 2] + fl[fq + 2];
            }
            while (w < n && spazio((unsigned char)v[w])) w++;
            if (w >= n || !leggi_valore(CSS_P_FAMIGLIA, v + w, n - w, &fam)) return 1;
            coda_metti(it, CSS_P_STILE, stile);
            coda_metti(it, CSS_P_PESO, peso);
            coda_metti(it, CSS_P_CORPO, corpo);
            coda_metti(it, CSS_P_FAMIGLIA, fam);
            return 1;
        }
        return 1;
    }

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
        /* ! NE' UN ';' DENTRO LE PARENTESI (10 ottobre 2026): un'immagine
         * scritta nel foglio, url(data:image/png;base64,...), ha un punto e
         * virgola in mezzo, e il valore finiva li': la dichiarazione si
         * perdeva e il resto dell'indirizzo veniva letto come proprieta'. */
        {
            unsigned int par = 0;

            while (it->i < it->n && (par || it->t[it->i] != ';') &&
                   !(!par && it->i + 1 < it->n && it->t[it->i] == '/' &&
                     it->t[it->i + 1] == '*')) {
                if (it->t[it->i] == '(') par++;
                else if (it->t[it->i] == ')' && par) par--;
                it->i++;
            }
            vf = it->i;
            while (it->i < it->n && (par || it->t[it->i] != ';')) {
                if (it->t[it->i] == '(') par++;
                else if (it->t[it->i] == ')' && par) par--;
                it->i++;
            }
        }
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

        /* ! A CUSTOM PROPERTY, AND A VALUE WITH var() (P6): neither can be
         * read now. In a sheet they are kept as text (CSS_P_PERSONALE,
         * CSS_P_RIMANDATA); in an attribute style read by css_calcola the
         * var() are replaced at once and what comes out is read here; an
         * attribute style read alone, without an element, cannot. */
        if (pf - pi > 2 && it->t[pi] == '-' && it->t[pi + 1] == '-') {
            if (g_raccogli) vp_metti(it->t + pi, pf - pi, it->t + vi, vf - vi, 0xFFFFFFFFu);
            else if (g_fog_legge &&
                (*val = arena_coppia(g_fog_legge, it->t + pi, pf - pi, it->t + vi, vf - vi, 0)) != 0) {
                *prop = CSS_P_PERSONALE;
                return 1;
            }
            continue;
        }
        if (g_raccogli) continue;           /* only the custom ones are wanted */
        if (pf > pi && vf > vi && ha_var(it->t + vi, vf - vi)) {
            if (g_fog_legge) {
                if ((*val = arena_coppia(g_fog_legge, it->t + pi, pf - pi, it->t + vi, vf - vi, 1)) != 0) {
                    *prop = CSS_P_RIMANDATA;
                    return 1;
                }
                continue;
            }
            if (g_in_calcola) var_in_linea(it, it->t + pi, pf - pi, it->t + vi, vf - vi);
            continue;
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
           p == CSS_P_ALT || p == CSS_P_ALT_MIN ||
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

/* the lengths that cannot be negative: a calc() below zero becomes zero */
static int non_negativa(unsigned short p)
{
    return p == CSS_P_CORPO || (p >= CSS_P_BORDO_LARG && p < CSS_P_BORDO_LARG + 4) ||
           (p >= CSS_P_IMBOTTITURA && p < CSS_P_IMBOTTITURA + 4) ||
           (p >= CSS_P_LARG && p <= CSS_P_LARG_MIN) || p == CSS_P_ALT || p == CSS_P_ALT_MIN ||
           p == CSS_P_SPAZIO_RIGA || p == CSS_P_SPAZIO_COL;
}

/* A relative length becomes pixels: `em` px make 1em, `perc` px make 100%.
 * ! A calc() OF A WIDTH OR A HEIGHT IS READ APART (see the calc() section):
 * a pure percentage stays one, a % in min/max/clamp is left out, and what
 * still needs the container is not posed at all. */
static void posa_relativa(CssStile *s, unsigned short p, unsigned int val, int em, int perc)
{
    unsigned int unita = (val >> 24) & 0x7Fu;
    int          px;

    if ((unita == REL_CALC_F || unita == REL_CALC_L) &&
        ((p >= CSS_P_LARG && p <= CSS_P_LARG_MIN) || p == CSS_P_ALT || p == CSS_P_ALT_MIN)) {
        const char *t = calc_testo(val);
        int         v0, v1, vm, tinto;

        if (!t) return;
        /* a pure percentage: 0 at 0, and twice at twice (min(100%, 1200px)
         * is 0 at 0 too, but not linear) */
        if (p != CSS_P_ALT && p != CSS_P_ALT_MIN &&
            calc_valuta_testo(t, em, 0, 0, &v0, &tinto) && v0 == 0 &&
            calc_valuta_testo(t, em, 10000, 0, &v1, &tinto) && v1 > 0 &&
            calc_valuta_testo(t, em, 5000, 0, &vm, &tinto) && (vm * 2 - v1 <= 1 && v1 - vm * 2 <= 1)) {
            /* 100% = 10000 px: v1 is the percentage in hundredths */
            *campo_larg(s, p) = (short)(v1 > 32000 ? 32000 : v1);
            s->larghezza_perc |= bit_perc(p);
            return;
        }
        if (!calc_valuta_testo(t, em, 0, 1, &px, &tinto) || tinto) return;
    } else {
        px = rel_px(val, em, perc);
    }
    if (px < 0 && non_negativa(p)) px = 0;
    posa_px(s, p, px);
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
                posa_relativa(s, prop, val, c, prop == CSS_P_CORPO ? c : g_media_w);
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
    case CSS_P_FAMIGLIA:
        if (val & FAM_RINVIO) {
            unsigned int fam, n = 0;
            const char  *t = g_fog_calc && (val & ~FAM_RINVIO) < g_fog_calc->arena_n
                           ? g_fog_calc->arena + (val & ~FAM_RINVIO) : 0;

            if (t) while (t[n]) n++;
            if (t && famiglia_risolvi(g_fog_calc, t, n, &fam)) s->famiglia = (unsigned char)fam;
        } else s->famiglia = (unsigned char)val;
        break;
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
    case CSS_P_SFONDO_IMM: s->sf_url    = (const char *)val;            break;
    case CSS_P_SFONDO_RIP: s->sf_ripeti = (unsigned char)val;           break;
    case CSS_P_SFONDO_PX:  s->sf_px     = (short)(unsigned short)val;   break;
    case CSS_P_SFONDO_PY:  s->sf_py     = (short)(unsigned short)val;   break;
    case CSS_P_SFONDO_MW:  s->sf_mw     = (short)(unsigned short)val;   break;
    case CSS_P_SFONDO_MH:  s->sf_mh     = (short)(unsigned short)val;   break;
    case CSS_P_ALT:        s->altezza     = (short)((int)val - 32768);  break;
    case CSS_P_ALT_MIN:    s->altezza_min = (short)((int)val - 32768);  break;
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

void css_base(CssFoglio *f, const char *url)
{
    unsigned int n = 0;

    if (!f || !f->arena) return;
    f->base = 0;
    if (!url || !url[0]) return;
    while (url[n]) n++;
    if (f->arena_n + n + 1 > f->arena_max) { f->troncato = 1; return; }
    f->base = f->arena + f->arena_n;
    for (n = 0; url[n]; n++) f->arena[f->arena_n++] = url[n];
    f->arena[f->arena_n++] = 0;
}

int css_sfondo_url(const CssFoglio *f, const CssStile *s, char *out, unsigned int max,
                   const char **base)
{
    const char  *u;
    unsigned int n = 0;

    if (base) *base = 0;
    if (!s || !s->sf_url || !out || max < 2) return 0;
    u = s->sf_url;
    if (f && f->arena && u >= f->arena + 4 && u < f->arena + f->arena_max && base)
        *base = *(const char *const *)(const void *)(u - 4);
    while (u[n] && u[n] != ')' && u[n] != '"' && u[n] != '\'' && u[n] != ';' &&
           u[n] != ' ' && n + 1 < max) { out[n] = u[n]; n++; }
    out[n] = 0;
    return n > 0;
}

void css_stile_inline(const char *testo, unsigned int n, CssStile *s)
{
    DichIter       it;
    unsigned short prop;
    unsigned int   val;

    if (!testo || !s) return;

    g_fog_legge = 0;            /* un attributo style: l'indirizzo resta dov'e' */
    if (!g_in_calcola) g_loc_n = 1;
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
static int g_media_h = 600;

void css_media_larghezza(int px)
{
    if (px > 0) g_media_w = px;
}

void css_media_altezza(int px)
{
    if (px > 0) g_media_h = px;
}

/* Una misura di una media query: «1120px», «40em», «calc(1120px - 1px)».
 * ! em AND rem ARE 16 px HERE, the initial font size, as the spec says: a
 * media query does not know any element. */
static int media_misura(const char *s, unsigned int n, int *out)
{
    CalcBasi b;
    int      tinto;

    while (n > 0 && spazio((unsigned char)s[0])) { s++; n--; }
    while (n > 0 && spazio((unsigned char)s[n - 1])) n--;
    b.em = 16; b.rem = 16; b.perc = 0; b.perc_ignota = 1;
    return n > 0 && calc_valuta(s, n, &b, out, &tinto) && !tinto;
}

static int pezzo_ug(const char *s, unsigned int n, const char *parola)
{
    unsigned int k = 0;

    while (k < n && parola[k] && minusc((unsigned char)s[k]) == parola[k]) k++;
    return k == n && parola[k] == '\0';
}

/* =============================================================================
 * The media features (P6, 25 November 2026)
 *
 * ! WHAT THIS SCREEN IS, SAID ONCE: a colour screen with a mouse, light
 * scheme, 1 device pixel per CSS pixel, no wish for less motion or more
 * contrast. Each feature has its value here; `(feature)` alone is true when
 * that value is not none / 0 / no-preference, as the spec says.
 * The sizes are compared as numbers: px for width and height, thousandths for
 * aspect-ratio (w/h) and resolution (dppx).
 * ============================================================================= */
#define MF_LARG    1
#define MF_ALT     2
#define MF_RAPP    3
#define MF_RISOL   4
#define MF_PAROLA  5
#define MF_NUMERO  6

typedef struct { const char *nome; unsigned char tipo; const char *parola; int numero; } MediaCar;

static const MediaCar MEDIA_CAR[] = {
    { "width",                  MF_LARG,   0, 0 },
    { "device-width",           MF_LARG,   0, 0 },
    { "height",                 MF_ALT,    0, 0 },
    { "device-height",          MF_ALT,    0, 0 },
    { "aspect-ratio",           MF_RAPP,   0, 0 },
    { "device-aspect-ratio",    MF_RAPP,   0, 0 },
    { "resolution",             MF_RISOL,  0, 0 },
    { "-webkit-device-pixel-ratio", MF_RISOL, 0, 0 },
    { "-moz-device-pixel-ratio",    MF_RISOL, 0, 0 },
    { "device-pixel-ratio",     MF_RISOL,  0, 0 },
    { "orientation",            MF_PAROLA, 0, 0 },     /* computed */
    { "prefers-color-scheme",   MF_PAROLA, "light", 0 },
    { "prefers-reduced-motion", MF_PAROLA, "no-preference", 0 },
    { "prefers-contrast",       MF_PAROLA, "no-preference", 0 },
    { "prefers-reduced-transparency", MF_PAROLA, "no-preference", 0 },
    { "prefers-reduced-data",   MF_PAROLA, "no-preference", 0 },
    { "forced-colors",          MF_PAROLA, "none", 0 },
    { "inverted-colors",        MF_PAROLA, "none", 0 },
    { "display-mode",           MF_PAROLA, "browser", 0 },
    { "scripting",              MF_PAROLA, "enabled", 0 },
    { "update",                 MF_PAROLA, "fast", 0 },
    { "color-gamut",            MF_PAROLA, "srgb", 0 },
    { "dynamic-range",          MF_PAROLA, "standard", 0 },
    { "video-dynamic-range",    MF_PAROLA, "standard", 0 },
    { "overflow-block",         MF_PAROLA, "scroll", 0 },
    { "overflow-inline",        MF_PAROLA, "scroll", 0 },
    { "hover",                  MF_PAROLA, "hover", 0 },
    { "any-hover",              MF_PAROLA, "hover", 0 },
    { "pointer",                MF_PAROLA, "fine", 0 },
    { "any-pointer",            MF_PAROLA, "fine", 0 },
    { "color",                  MF_NUMERO, 0, 8 },
    { "color-index",            MF_NUMERO, 0, 0 },
    { "monochrome",             MF_NUMERO, 0, 0 },
    { "grid",                   MF_NUMERO, 0, 0 },
    { 0, 0, 0, 0 }
};

static int pezzo_ug(const char *s, unsigned int n, const char *parola);

static const MediaCar *media_car(const char *s, unsigned int n)
{
    int j;

    for (j = 0; MEDIA_CAR[j].nome; j++) if (pezzo_ug(s, n, MEDIA_CAR[j].nome)) return &MEDIA_CAR[j];
    return 0;
}

/* this screen's value of a feature with a size */
static int media_nostro(const MediaCar *c)
{
    switch (c->tipo) {
    case MF_LARG:  return g_media_w;
    case MF_ALT:   return g_media_h;
    case MF_RAPP:  return g_media_h > 0 ? g_media_w * 1000 / g_media_h : 1000;
    case MF_RISOL: return 1000;
    case MF_NUMERO: return c->numero;
    }
    return 0;
}

/* a value written in the query, in the feature's numbers */
static int media_suo(const MediaCar *c, const char *s, unsigned int n, int *out)
{
    unsigned int i;

    while (n > 0 && spazio((unsigned char)s[0])) { s++; n--; }
    while (n > 0 && spazio((unsigned char)s[n - 1])) n--;
    if (n == 0) return 0;
    if (c->tipo == MF_LARG || c->tipo == MF_ALT) return media_misura(s, n, out);
    if (c->tipo == MF_RAPP) {
        /* «16/9», «16 / 9», or a number */
        int a = 0, b = 0;
        unsigned int k;

        for (k = 0; k < n && s[k] != '/'; k++) {}
        for (i = 0; i < k; i++) if (c_cifra(s[i])) a = a * 10 + (s[i] - '0'); else if (!spazio((unsigned char)s[i])) break;
        if (k == n) { *out = a * 1000; return a > 0; }
        for (i = k + 1; i < n; i++) if (c_cifra(s[i])) b = b * 10 + (s[i] - '0'); else if (!spazio((unsigned char)s[i])) break;
        if (b <= 0) return 0;
        *out = a * 1000 / b;
        return 1;
    }
    {
        /* resolution: 2dppx, 2x, 192dpi, 76dpcm; a pixel ratio: a number */
        double v = 0, sc = 1;
        int    cifre = 0;

        for (i = 0; i < n && c_cifra(s[i]); i++) { v = v * 10 + (s[i] - '0'); cifre = 1; }
        if (i < n && s[i] == '.') for (i++; i < n && c_cifra(s[i]); i++) { sc /= 10; v += (s[i] - '0') * sc; cifre = 1; }
        if (!cifre) return 0;
        if (pezzo_ug(s + i, n - i, "dpi"))  v /= 96;
        else if (pezzo_ug(s + i, n - i, "dpcm")) v = v * 2.54 / 96;
        else if (i < n && !pezzo_ug(s + i, n - i, "dppx") && !pezzo_ug(s + i, n - i, "x")) return 0;
        *out = c->tipo == MF_RISOL ? (int)(v * 1000 + 0.5) : (int)v;
        return 1;
    }
}

/* the boolean context: «(hover)», «(prefers-reduced-motion)» */
static int media_vero(const MediaCar *c)
{
    if (c->tipo == MF_PAROLA) {
        if (!c->parola) return 1;                   /* orientation */
        return !(c->parola && (pezzo_ug(c->parola, 4, "none") ||
                               pezzo_ug(c->parola, 13, "no-preference")));
    }
    return media_nostro(c) != 0;
}

static int media_confronta(int a, const char *op, unsigned int k, int b)
{
    if (k == 1 && op[0] == '<') return a < b;
    if (k == 1 && op[0] == '>') return a > b;
    if (k == 1 && op[0] == '=') return a == b;
    if (k == 2 && op[0] == '<') return a <= b;
    if (k == 2 && op[0] == '>') return a >= b;
    return 0;
}

/* «(nome: valore)», «(nome)», o la forma a intervalli «(width >= 600px)»,
 * «(400px <= width < 700px)», senza le parentesi. */
static int media_caratteristica(const char *s, unsigned int n)
{
    unsigned int i = 0, ni, nf, vi, vf, k;
    int          v, noi;
    const MediaCar *c;

    /* the range form: an operator outside parentheses */
    {
        unsigned int op[2], opk[2], nop = 0, liv = 0;

        for (k = 0; k < n && nop < 2; k++) {
            if (s[k] == '(') liv++;
            else if (s[k] == ')' && liv) liv--;
            else if (!liv && (s[k] == '<' || s[k] == '>' || s[k] == '=')) {
                op[nop] = k;
                opk[nop] = (k + 1 < n && s[k + 1] == '=' && s[k] != '=') ? 2 : 1;
                k += opk[nop] - 1;
                nop++;
            }
        }
        if (nop) {
            /* the pieces: a [op b [op c]] */
            unsigned int da[3], a[3], q;
            int          nome_in = -1;

            da[0] = 0; a[0] = op[0];
            da[1] = op[0] + opk[0]; a[1] = nop > 1 ? op[1] : n;
            if (nop > 1) { da[2] = op[1] + opk[1]; a[2] = n; }
            for (q = 0; q <= nop; q++) {
                unsigned int x = da[q], y = a[q];

                while (x < y && spazio((unsigned char)s[x])) x++;
                while (y > x && spazio((unsigned char)s[y - 1])) y--;
                da[q] = x; a[q] = y;
                if (x < y && (c_lettera(s[x]) || (s[x] == '-' && x + 1 < y && c_lettera(s[x + 1]))) &&
                    media_car(s + x, y - x)) nome_in = (int)q;
            }
            if (nome_in < 0) return 0;
            c = media_car(s + da[nome_in], a[nome_in] - da[nome_in]);
            if (c->tipo == MF_PAROLA) return 0;
            noi = media_nostro(c);
            for (q = 0; q < nop; q++) {
                /* operator q is between piece q and piece q+1 */
                int sx, dx;

                if ((int)q == nome_in) {
                    sx = noi;
                    if (!media_suo(c, s + da[q + 1], a[q + 1] - da[q + 1], &dx)) return 0;
                } else if ((int)q + 1 == nome_in) {
                    if (!media_suo(c, s + da[q], a[q] - da[q], &sx)) return 0;
                    dx = noi;
                } else return 0;
                if (!media_confronta(sx, s + op[q], opk[q], dx)) return 0;
            }
            return 1;
        }
    }

    while (i < n && spazio((unsigned char)s[i])) i++;
    ni = i;
    while (i < n && s[i] != ':' && !spazio((unsigned char)s[i])) i++;
    nf = i;
    while (i < n && (spazio((unsigned char)s[i]) || s[i] == ':')) i++;
    vi = i;
    vf = n;
    while (vf > vi && spazio((unsigned char)s[vf - 1])) vf--;

    {
        int          prefisso = 0;           /* 1 min-, 2 max- */
        char         nome[48];
        unsigned int a = ni, z = 0, w;

        /* min-width, max-width; -webkit-min-device-pixel-ratio is min- of
         * -webkit-device-pixel-ratio; min--moz-device-pixel-ratio min- of
         * -moz-device-pixel-ratio */
        if (nf - a > 4 && (pezzo_ug(s + a, 4, "min-") || pezzo_ug(s + a, 4, "max-"))) {
            prefisso = minusc((unsigned char)s[a + 1]) == 'i' ? 1 : 2;
            a += 4;
        } else if (nf - a > 12 && (pezzo_ug(s + a, 12, "-webkit-min-") || pezzo_ug(s + a, 12, "-webkit-max-"))) {
            prefisso = minusc((unsigned char)s[a + 9]) == 'i' ? 1 : 2;
            for (w = 0; w < 8; w++) nome[z++] = "-webkit-"[w];
            a += 12;
        }
        for (w = a; w < nf && z < sizeof(nome) - 1; w++) nome[z++] = s[w];
        nome[z] = 0;
        c = media_car(nome, z);
        if (!c) return 0;       /* quel che non si conosce non vale */
        if (vi == vf) return prefisso ? 0 : media_vero(c);
        if (c->tipo == MF_PAROLA) {
            if (!c->parola)
                return pezzo_ug(s + vi, vf - vi, g_media_w >= g_media_h ? "landscape" : "portrait");
            return prefisso ? 0 : pezzo_ug(s + vi, vf - vi, c->parola);
        }
        if (!media_suo(c, s + vi, vf - vi, &v)) return 0;
        noi = media_nostro(c);
        return prefisso == 1 ? noi >= v : prefisso == 2 ? noi <= v : noi == v;
    }
}

/* A condition of level 4: «(a) and (b)», «(a) or (b)», «not (a)»,
 * parentheses inside parentheses. */
static int media_condizione(const char *s, unsigned int n)
{
    unsigned int i = 0;
    int          ris = -1, op = 0, nega_tutto = 0;

    while (i < n && spazio((unsigned char)s[i])) i++;
    if (i + 3 < n && pezzo_ug(s + i, 3, "not") && (spazio((unsigned char)s[i + 3]) || s[i + 3] == '(')) {
        nega_tutto = 1; i += 3;
    }
    while (i < n) {
        int v;

        while (i < n && spazio((unsigned char)s[i])) i++;
        if (i >= n) break;
        if (s[i] == '(') {
            int          liv = 1;
            unsigned int p = ++i, q, w;

            while (i < n && liv) { if (s[i] == '(') liv++; else if (s[i] == ')') liv--; i++; }
            q = liv ? n : i - 1;
            /* inside: a feature, or another condition */
            for (w = p; w < q && spazio((unsigned char)s[w]); w++) {}
            if (w < q && (s[w] == '(' || (w + 4 < q && pezzo_ug(s + w, 4, "not ")))) v = media_condizione(s + p, q - p);
            else v = media_caratteristica(s + p, q - p);
        } else {
            unsigned int p = i;

            while (i < n && !spazio((unsigned char)s[i]) && s[i] != '(') i++;
            if (pezzo_ug(s + p, i - p, "and")) { op = 1; continue; }
            if (pezzo_ug(s + p, i - p, "or"))  { op = 2; continue; }
            v = 0;                                  /* a word that is not ours */
        }
        if (ris < 0) ris = v;
        else if (op == 2) ris = ris || v;
        else ris = ris && v;
    }
    if (ris < 0) ris = 0;
    return nega_tutto ? !ris : ris;
}

/* Una sola query (senza virgole): [only|not] [tipo] [and condizione], o una
 * condizione da sola. */
static int media_una(const char *s, unsigned int n)
{
    unsigned int i = 0, p;
    int          nega = 0, va = 1;

    while (i < n && spazio((unsigned char)s[i])) i++;
    if (i >= n) return 1;
    if (s[i] == '(') return media_condizione(s + i, n - i);
    if (i + 4 < n && pezzo_ug(s + i, 4, "not ")) {
        unsigned int w = i + 4;

        while (w < n && spazio((unsigned char)s[w])) w++;
        if (w < n && s[w] == '(') return media_condizione(s + i, n - i);
    }
    p = i;
    while (i < n && !spazio((unsigned char)s[i]) && s[i] != '(') i++;
    if (pezzo_ug(s + p, i - p, "not") || pezzo_ug(s + p, i - p, "only")) {
        nega = pezzo_ug(s + p, i - p, "not");
        while (i < n && spazio((unsigned char)s[i])) i++;
        p = i;
        while (i < n && !spazio((unsigned char)s[i]) && s[i] != '(') i++;
    }
    if (!(pezzo_ug(s + p, i - p, "screen") || pezzo_ug(s + p, i - p, "all"))) va = 0;   /* print, speech, tv... */
    while (i < n && spazio((unsigned char)s[i])) i++;
    if (i + 3 <= n && pezzo_ug(s + i, 3, "and")) {
        i += 3;
        if (!media_condizione(s + i, n - i)) va = 0;
    } else if (i < n) {
        va = 0;                                     /* something after the type we cannot read */
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

/* For matchMedia(): the same answer the sheets get. */
int css_media_vale(const char *q, unsigned int n)
{
    if (!q) return 0;
    while (n > 0 && spazio((unsigned char)q[0])) { q++; n--; }
    return media_va(q, n);
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

    g_fog_legge = f;            /* gli url() vanno copiati nella sua arena */
    var_azzera();               /* a new rule may declare a custom property */

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
                else if (pezzo_ug(testo + ni, nf - ni, "font-face"))
                    fontface_metti(f, testo + bi, bf - bi);
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
    g_fog_legge = 0;
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

/* The rules that match `nodo`, in reading order, into `out` (at most `max`);
 * returns how many. Apart from css_calcola since P6: the custom properties
 * of an ancestor need the same list.
 * ! THE CANDIDATES, THEN IN READING ORDER: at equal weight the rule read
 * later wins, and that rule is the one with the higher index. Collected from
 * the buckets of the element's id, classes and type and from the universal
 * list, sorted, duplicates dropped (two classes can share a bucket). Too
 * many: the whole list, as before - slower, never wrong. */
static int abbina(const CssFoglio *f, const HtmlDoc *d, int nodo, int *out, int max)
{
    static int   cand[CSS_CAND_MAX];        /* not re-entered: see var_insieme */
    int          nc = 0, troppe = 0, a, b, n = 0;
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
        int ir = (nc < 0) ? q : cand[q];

        if (nc >= 0 && q > 0 && cand[q] == cand[q - 1]) continue;
        if (!regola_combacia(f, d, nodo, &f->regole[ir])) continue;
        if (n < max) out[n++] = ir;
    }
    return n;
}

/* =============================================================================
 * var() AND THE CUSTOM PROPERTIES (P6, 25 November 2026)
 *
 * `--x: 4px` is a property of the element like any other - cascaded,
 * inherited by the children - with a value nobody reads but var(). Google's
 * sheets set hundreds on :root and read them everywhere.
 *
 * ! THE SET OF AN ELEMENT IS ITS OWN AND ITS PARENT'S: a record of its own
 * declarations (already resolved: a var() inside a custom property reads
 * the element where it is DECLARED, as the spec wants) pointing to the
 * parent's set. A child that declares nothing shares its parent's.
 *
 * ! IT LIVES IN THE CALLER'S BUFFER (css_variabili), as everything here, and
 * for ONE LAYOUT: the browser gives it again before each one, and that is
 * what keeps it from ageing (css.h). Inside a layout it is filled lazily: an
 * element's set is made the first time a var() of its own, or of a
 * descendant, asks for it; an ancestor not met yet has its rules matched
 * once more, then it is remembered.
 *
 * The buffer, in 32-bit words: [0] unused, [1 .. nodi] one per node of the
 * document (0 not made yet, 1 empty, else where its record is), then the
 * records {parent, count, (name, value) x count} growing forward, and the
 * texts growing back from the end; a record names its texts by byte offset.
 * Full: what does not fit is not remembered, and the var() that needed it
 * takes its fallback.
 * ============================================================================= */
static unsigned int    *g_vb = 0;
static unsigned int     g_vb_max = 0;       /* words */
static unsigned int     g_vb_n = 0;         /* words used, from the front */
static unsigned int     g_vb_t = 0;         /* byte where the texts begin */
static unsigned int     g_vb_nodi = 0;
static const HtmlDoc   *g_vb_doc = 0;
static const CssFoglio *g_vb_fog = 0;

void css_variabili(void *buf, unsigned int n)
{
    unsigned int pad = (unsigned int)((4u - ((unsigned long)buf & 3u)) & 3u);

    g_vb = 0;
    g_vb_doc = 0;
    g_vb_fog = 0;
    if (!buf || n < pad + 256) return;
    g_vb = (unsigned int *)(void *)((char *)buf + pad);
    g_vb_max = (n - pad) / 4;
}

static void var_azzera(void)
{
    g_vb_doc = 0;
    g_vb_fog = 0;
}

static void var_prepara(const CssFoglio *f, const HtmlDoc *d)
{
    unsigned int i;

    if (!g_vb || (g_vb_doc == d && g_vb_fog == f)) return;
    g_vb_doc = d;
    g_vb_fog = f;
    g_vb_nodi = d->nodi_n;
    if (g_vb_nodi + 1 > g_vb_max / 2) g_vb_nodi = 0;   /* no room for the table */
    for (i = 0; i <= g_vb_nodi; i++) g_vb[i] = 0;
    g_vb_n = g_vb_nodi + 1;
    g_vb_t = g_vb_max * 4;
}

static unsigned int var_memo(int nodo)
{
    return (g_vb && nodo >= 0 && (unsigned int)nodo < g_vb_nodi) ? g_vb[1 + nodo] : 0;
}

static void var_memo_metti(int nodo, unsigned int ins)
{
    if (g_vb && nodo >= 0 && (unsigned int)nodo < g_vb_nodi) g_vb[1 + nodo] = ins ? ins : 1;
}

static const char *var_testo(unsigned int off) { return (const char *)g_vb + off; }

static unsigned int var_testo_metti(const char *s, unsigned int n)
{
    unsigned int i;

    if (!g_vb || g_vb_t < n + 1 || g_vb_t - (n + 1) < g_vb_n * 4) return 0;
    g_vb_t -= n + 1;
    for (i = 0; i < n; i++) ((char *)g_vb)[g_vb_t + i] = s[i];
    ((char *)g_vb)[g_vb_t + n] = 0;
    return g_vb_t;
}

static unsigned int lun(const char *s)
{
    unsigned int n = 0;

    while (s[n]) n++;
    return n;
}

/* the value of `nome` in set `ins` and its ancestors, or 0 */
static const char *var_cerca(unsigned int ins, const char *nome, unsigned int nn)
{
    int giri = 0;

    while (ins && giri++ < 4096) {
        const unsigned int *w = g_vb + ins;
        unsigned int        k = w[1];

        while (k-- > 0) {
            const char  *x = var_testo(w[2 + 2 * k]);
            unsigned int j = 0;

            while (j < nn && x[j] == nome[j]) j++;
            if (j == nn && x[j] == 0) return var_testo(w[3 + 2 * k]);
        }
        ins = w[0];
    }
    return 0;
}

/* The element's own declarations while its set is made. */
#define VP_MAX 2048
#define VP_DA_FARE  0
#define VP_IN_CORSO 1
#define VP_FATTA    2
#define VP_INVALIDA 3

typedef struct {
    const char    *nome, *val;
    unsigned int   nn, vn, peso, ris;
    unsigned char  stato, ciclo;
} VarPropria;

static VarPropria   g_vp[VP_MAX];
static int          g_vp_n = 0;
static int          g_vp_attivo = 0;   /* lookups look in g_vp first */
static unsigned int g_sost_ins = 0;    /* ...then in this set */

static int vp_trova(const char *nome, unsigned int nn)
{
    int k;

    for (k = 0; k < g_vp_n; k++) {
        unsigned int j = 0;

        if (g_vp[k].nn != nn) continue;
        while (j < nn && g_vp[k].nome[j] == nome[j]) j++;
        if (j == nn) return k;
    }
    return -1;
}

static void vp_metti(const char *nome, unsigned int nn, const char *val, unsigned int vn,
                     unsigned int peso)
{
    int k = vp_trova(nome, nn);

    if (k < 0) {
        if (g_vp_n >= VP_MAX) return;
        k = g_vp_n++;
        g_vp[k].nome = nome; g_vp[k].nn = nn; g_vp[k].peso = 0;
    } else if (peso < g_vp[k].peso) return;
    g_vp[k].val = val; g_vp[k].vn = vn; g_vp[k].peso = peso;
    g_vp[k].stato = VP_DA_FARE; g_vp[k].ciclo = 0; g_vp[k].ris = 0;
}

static const char *var_valore(const char *nome, unsigned int nn)
{
    if (g_vp_attivo) {
        int k = vp_trova(nome, nn);

        if (k >= 0 && g_vp[k].stato == VP_FATTA) return var_testo(g_vp[k].ris);
        /* ! AN OWN ONE THAT IS INVALID IS AS IF UNSET, and a custom property
         * unset inherits: the parent's value is the one to take */
    }
    return var_cerca(g_sost_ins, nome, nn);
}

/* `in` with every var() and env() replaced, appended to out (at *on, room
 * for `max`). 0: a var() with neither a value nor a fallback - the whole
 * declaration is then invalid, as the spec says. */
static int sost(const char *in, unsigned int n, char *out, unsigned int max, unsigned int *on, int prof)
{
    unsigned int i = 0;

    if (prof > 16) return 0;
    while (i < n) {
        if (i + 4 <= n && in[i + 3] == '(' && (i == 0 || !nomeok((unsigned char)in[i - 1])) &&
            ((minusc((unsigned char)in[i]) == 'v' && minusc((unsigned char)in[i + 1]) == 'a' &&
              minusc((unsigned char)in[i + 2]) == 'r') ||
             (minusc((unsigned char)in[i]) == 'e' && minusc((unsigned char)in[i + 1]) == 'n' &&
              minusc((unsigned char)in[i + 2]) == 'v'))) {
            int          env = minusc((unsigned char)in[i]) == 'e', ha_fb = 0, liv = 0;
            unsigned int j = i + 4, ni, nf, fi = 0, k;
            const char  *val = 0;

            while (j < n && spazio((unsigned char)in[j])) j++;
            ni = j;
            while (j < n && !spazio((unsigned char)in[j]) && in[j] != ',' && in[j] != ')') j++;
            nf = j;
            while (j < n && spazio((unsigned char)in[j])) j++;
            if (j < n && in[j] == ',') { ha_fb = 1; j++; fi = j; }
            for (k = j; k < n; k++) {
                char c = in[k];

                if (c == '"' || c == '\'') {
                    for (k++; k < n && in[k] != c; k++) if (in[k] == '\\') k++;
                    continue;
                }
                if (c == '(') liv++;
                else if (c == ')') { if (!liv) break; liv--; }
            }
            if (k >= n) return 0;                    /* never closed */
            if (!env && nf - ni > 2 && in[ni] == '-' && in[ni + 1] == '-')
                val = var_valore(in + ni, nf - ni);
            if (val) {
                unsigned int z = lun(val);

                if (*on + z + 1 >= max) return 0;
                while (z--) out[(*on)++] = *val++;
            } else if (ha_fb) {
                if (!sost(in + fi, k - fi, out, max, on, prof + 1)) return 0;
            } else if (env) {
                if (*on + 4 >= max) return 0;
                out[(*on)++] = '0'; out[(*on)++] = 'p'; out[(*on)++] = 'x';
            } else {
                return 0;
            }
            i = k + 1;
            continue;
        }
        if (*on + 1 >= max) return 0;
        out[(*on)++] = in[i++];
    }
    return 1;
}

/* One own declaration, resolved: first the own ones it reads, then itself.
 * ! A CYCLE (--a: var(--b); --b: var(--a)) makes all of it invalid: every
 * one still being resolved when the loop closes gets the mark. */
static void vp_risolvi(int k, int prof)
{
    static char  buf[8192];       /* used only after the nested calls are over */
    VarPropria  *e = &g_vp[k];
    unsigned int i, on = 0, a, b;

    if (e->stato != VP_DA_FARE) {
        if (e->stato == VP_IN_CORSO) {
            int q;

            for (q = 0; q < g_vp_n; q++) if (g_vp[q].stato == VP_IN_CORSO) g_vp[q].ciclo = 1;
        }
        return;
    }
    if (prof > 16) { e->stato = VP_INVALIDA; return; }
    e->stato = VP_IN_CORSO;
    for (i = 0; i + 6 <= e->vn; i++) {
        unsigned int j;
        int          t;

        if (e->val[i + 3] != '(' || minusc((unsigned char)e->val[i]) != 'v' ||
            minusc((unsigned char)e->val[i + 1]) != 'a' || minusc((unsigned char)e->val[i + 2]) != 'r')
            continue;
        j = i + 4;
        while (j < e->vn && spazio((unsigned char)e->val[j])) j++;
        a = j;
        while (j < e->vn && !spazio((unsigned char)e->val[j]) && e->val[j] != ',' && e->val[j] != ')') j++;
        t = vp_trova(e->val + a, j - a);
        if (t >= 0) vp_risolvi(t, prof + 1);
        e = &g_vp[k];
    }
    if (!sost(e->val, e->vn, buf, sizeof(buf), &on, 0) || e->ciclo) { e->stato = VP_INVALIDA; return; }
    a = 0; b = on;
    while (a < b && spazio((unsigned char)buf[a])) a++;
    while (b > a && spazio((unsigned char)buf[b - 1])) b--;
    e->ris = var_testo_metti(buf + a, b - a);
    e->stato = e->ris ? VP_FATTA : VP_INVALIDA;
}

static int g_raccogli = 0;   /* dich_prossima: the custom ones of an attribute style go to g_vp */

/* The set of an element whose rules are `vinte`, over its parent's. */
static unsigned int var_proprie(const CssFoglio *f, const HtmlDoc *d, int nodo,
                                unsigned int padre, const int *vinte, int nv)
{
    int          q, k, cnt = 0;
    unsigned int o, need, nomi[VP_MAX];
    const char  *st;

    g_vp_n = 0;
    for (q = 0; q < nv; q++) {
        const CssRegola *r = &f->regole[vinte[q]];
        unsigned int     p1 = (unsigned int)r->origine * 1000000u + r->peso + 1u;

        for (k = r->prima_dich; k >= 0; k = f->dich[k].prossima)
            if (f->dich[k].proprieta == CSS_P_PERSONALE) {
                const char  *nome = f->arena + f->dich[k].numero;
                unsigned int nn = lun(nome);

                vp_metti(nome, nn, nome + nn + 1, lun(nome + nn + 1), p1);
            }
    }
    /* the attribute style, above every sheet */
    st = html_attr(d, nodo, "style");
    if (st && st[0]) {
        DichIter       it;
        unsigned short p;
        unsigned int   v;

        it.t = st; it.i = 0; it.n = lun(st); it.coda_n = it.coda_i = 0;
        g_raccogli = 1;
        while (dich_prossima(&it, &p, &v)) {}
        g_raccogli = 0;
    }
    if (g_vp_n == 0) return padre;

    g_vp_attivo = 1;
    g_sost_ins = padre;
    for (k = 0; k < g_vp_n; k++) vp_risolvi(k, 0);
    g_vp_attivo = 0;

    for (k = 0; k < g_vp_n; k++) {
        if (g_vp[k].stato != VP_FATTA) continue;
        nomi[k] = var_testo_metti(g_vp[k].nome, g_vp[k].nn);
        if (nomi[k]) cnt++; else g_vp[k].stato = VP_INVALIDA;
    }
    if (cnt == 0) return padre;
    need = 2 + 2 * (unsigned int)cnt;
    if (g_vb_n + need > g_vb_t / 4) return padre;         /* full */
    o = g_vb_n;
    g_vb[o] = padre;
    g_vb[o + 1] = (unsigned int)cnt;
    for (k = 0, q = 0; k < g_vp_n; k++) {
        if (g_vp[k].stato != VP_FATTA) continue;
        g_vb[o + 2 + 2 * q] = nomi[k];
        g_vb[o + 3 + 2 * q] = g_vp[k].ris;
        q++;
    }
    g_vb_n += need;
    return o;
}

/* The set of any node, made top-down from the nearest ancestor that has one. */
static unsigned int var_insieme(const CssFoglio *f, const HtmlDoc *d, int nodo)
{
    static int   catena[256], vinte[CSS_CAND_MAX];
    int          n = 0, k;
    unsigned int ins = 0;

    while (nodo >= 0) {
        unsigned int m = var_memo(nodo);

        if (m) { ins = (m == 1) ? 0 : m; break; }
        if (n >= 256) break;                   /* deeper than that: from nothing */
        catena[n++] = nodo;
        nodo = d->nodi[nodo].padre;
    }
    for (k = n - 1; k >= 0; k--) {
        int x = catena[k];

        if (d->nodi[x].tipo == HTML_ELEMENTO)
            ins = var_proprie(f, d, x, ins, vinte, abbina(f, d, x, vinte, CSS_CAND_MAX));
        var_memo_metti(x, ins);
    }
    return ins;
}

/* The element css_calcola is working on: its set is made the first time it
 * is asked for, from the rules css_calcola has already matched. */
static const HtmlDoc *g_calc_doc = 0;
static int            g_calc_nodo = -1;
static const int     *g_calc_vinte = 0;
static int            g_calc_nv = 0;
static int            g_calc_ins = -1;
static int            g_sost_attiva = 0;   /* reading a value made by sost(): leggi_url copies */

/* the address of a background read out of a var(): in the buffer's texts,
 * as a pointer the way leggi_url gives them (see the pointer note there) */
static unsigned int var_testo_metti_url(const char *s, unsigned int n)
{
    unsigned int o = var_testo_metti(s, n);

    return o ? (unsigned int)(unsigned long)var_testo(o) : 0;
}

static unsigned int var_insieme_ora(void)
{
    unsigned int m, padre, ins;

    if (g_calc_ins >= 0) return (unsigned int)g_calc_ins;
    if (!g_vb || !g_calc_doc || !g_fog_calc) { g_calc_ins = 0; return 0; }
    m = var_memo(g_calc_nodo);
    if (m) { g_calc_ins = (m == 1) ? 0 : (int)m; return (unsigned int)g_calc_ins; }
    padre = var_insieme(g_fog_calc, g_calc_doc, g_calc_doc->nodi[g_calc_nodo].padre);
    ins = var_proprie(g_fog_calc, g_calc_doc, g_calc_nodo, padre, g_calc_vinte, g_calc_nv);
    var_memo_metti(g_calc_nodo, ins);
    g_calc_ins = (int)ins;
    return ins;
}

/* "nome:" and the value with its var() replaced, in g_loc; its offset and
 * length, 0 if a var() could not be replaced. */
static unsigned int var_scrivi(const char *nome, unsigned int nn, const char *v, unsigned int n,
                               unsigned int *lung)
{
    unsigned int o = g_loc_n, on, i;

    if (o + nn + 2 >= sizeof(g_loc)) return 0;
    for (i = 0; i < nn; i++) g_loc[o + i] = nome[i];
    g_loc[o + nn] = ':';
    on = nn + 1;
    g_sost_ins = var_insieme_ora();
    g_vp_attivo = 0;
    if (!sost(v, n, g_loc + o, (unsigned int)sizeof(g_loc) - o, &on, 0)) return 0;
    g_loc[o + on] = 0;
    g_loc_n = o + on + 1;
    *lung = on;
    return o;
}

/* An attribute style's declaration with var(): what it says goes in the
 * queue of the iterator that found it. */
static void var_in_linea(DichIter *it, const char *nome, unsigned int nn,
                         const char *v, unsigned int n)
{
    unsigned int   o, l = 0, val;
    unsigned short p;
    DichIter       sub;

    if (g_sost_attiva) return;                 /* a var() that made a var() */
    o = var_scrivi(nome, nn, v, n, &l);
    if (!o) return;
    sub.t = g_loc + o; sub.i = 0; sub.n = l; sub.coda_n = sub.coda_i = 0;
    g_sost_attiva = 1;
    while (dich_prossima(&sub, &p, &val)) coda_metti(it, p, val);
    g_sost_attiva = 0;
}

/* A sheet's declaration with var(), on the style, with the weight of its
 * rule: each property it becomes takes part in the cascade by itself. */
static void rimandata_applica(CssStile *out, const char *rec, unsigned int p1,
                              unsigned int *peso_di)
{
    unsigned int   nn = lun(rec), o, l = 0, val;
    unsigned short p;
    DichIter       sub;

    if (g_sost_attiva) return;
    o = var_scrivi(rec, nn, rec + nn + 1, lun(rec + nn + 1), &l);
    if (!o) return;
    sub.t = g_loc + o; sub.i = 0; sub.n = l; sub.coda_n = sub.coda_i = 0;
    g_sost_attiva = 1;
    while (dich_prossima(&sub, &p, &val)) {
        if (p >= CSS_P_N || p1 < peso_di[p]) continue;
        peso_di[p] = p1;
        css_posa(out, p, val);
    }
    g_sost_attiva = 0;
}

void css_calcola(const CssFoglio *f, const HtmlDoc *d, int nodo,
                 const CssStile *ereditato, CssStile *out)
{
    unsigned int peso_di[CSS_P_N];
    unsigned int rel[CSS_P_N];
    int          vinte[CSS_CAND_MAX], nv, q, k;
    unsigned int i;

    if (!out) return;
    g_fog_legge = 0;
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
    g_fog_calc = f;
    g_in_calcola = 1;
    g_loc_n = 1;
    g_calc_doc = d;
    g_calc_nodo = nodo;
    g_calc_ins = -1;
    var_prepara(f, d);

    nv = abbina(f, d, nodo, vinte, CSS_CAND_MAX);
    g_calc_vinte = vinte;
    g_calc_nv = nv;
    for (q = 0; q < nv; q++) {
        const CssRegola *r = &f->regole[vinte[q]];
        /* ! IL PESO DELL'ORIGINE STA SOPRA QUELLO DEL SELETTORE, e non
         * accanto: un `style=` con un selettore banale deve battere un
         * selettore lunghissimo di un foglio. Sommarli lascerebbe che una
         * specificita' alta scavalchi l'origine, che e' il contrario della
         * cascata. */
        unsigned int     p1 = (unsigned int)r->origine * 1000000u + r->peso + 1u;

        for (k = r->prima_dich; k >= 0; k = f->dich[k].prossima) {
            unsigned short prop = f->dich[k].proprieta;

            if (prop == CSS_P_RIMANDATA) {
                rimandata_applica(out, f->arena + f->dich[k].numero, p1, peso_di);
                continue;
            }
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
            if (p != CSS_P_CORPO && rel[p]) posa_relativa(out, p, rel[p], mio, g_media_w);
    }
    nascosto_applica(out);
    g_in_calcola = 0;
    g_fog_calc = 0;
    g_calc_doc = 0;
    g_calc_vinte = 0;
}
