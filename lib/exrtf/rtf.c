/* =============================================================================
 * lib/exrtf/rtf.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * RTF, read and written (@RTF). The model is in exrtf.h.
 *
 * ! RTF IS GROUPS AND CONTROL WORDS: { opens a group that inherits the
 * character style, } closes it and gives the old style back, \word[N] changes
 * something, and everything else is text. The reader keeps a stack of states,
 * one per open group, and that is almost all of it.
 *
 * ! A DESTINATION THAT IS NOT UNDERSTOOD IS SKIPPED WITH ITS WHOLE GROUP. The
 * specification says so for \* (an optional destination), and it is also the
 * right thing for \pict, \info, \stylesheet: their contents are not text, and
 * reading them as text puts hex digits and author names into the document.
 *
 * ! \uN IS FOLLOWED BY \ucN CHARACTERS THAT REPEAT IT for readers that do not
 * know Unicode. They are skipped, or the character appears twice: "caffè"
 * would read "caffè?" .
 * ============================================================================= */
#include "exrtf.h"

/* --------------------------------------------------------------------------
 * Small helpers: this library uses nothing of the libc.
 * -------------------------------------------------------------------------- */
static unsigned int lun(const char *s) { unsigned int n = 0; while (s[n]) n++; return n; }

static int stesso(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

static int stile_uguale(const ExRtfStile *a, const ExRtfStile *b)
{
    return a->famiglia == b->famiglia && a->grassetto == b->grassetto &&
           a->corsivo == b->corsivo && a->sottolineato == b->sottolineato &&
           a->corpo == b->corpo && a->colore == b->colore && a->font == b->font;
}

void exrtf_stile_base(ExRtfStile *s)
{
    s->famiglia = EXRTF_SERIF;
    s->grassetto = s->corsivo = s->sottolineato = 0;
    s->corpo = 12;
    s->colore = 0;
    s->font = 0;
}

void exrtf_prepara(ExRtfDoc *d, char *testo, unsigned int testo_max,
                   ExRtfPezzo *pezzi, unsigned int pezzi_max,
                   unsigned char *allinea, unsigned int par_max)
{
    d->testo = testo; d->testo_max = testo_max; d->testo_n = 0;
    if (testo_max) testo[0] = '\0';
    d->pezzi = pezzi; d->pezzi_max = pezzi_max; d->pezzi_n = 0;
    d->allinea = allinea; d->par_max = par_max; d->par_n = 0;
    if (par_max) { allinea[0] = EXRTF_SINISTRA; d->par_n = 1; }
    d->font_n = 0;
    d->troncato = 0;
}

void exrtf_aggiungi(ExRtfDoc *d, const char *t, unsigned int n, const ExRtfStile *s)
{
    unsigned int i;

    for (i = 0; i < n; i++) {
        if (d->testo_n + 1 >= d->testo_max) { d->troncato = 1; return; }
        d->testo[d->testo_n] = t[i];
        if (d->pezzi_n > 0 &&
            stile_uguale(&d->pezzi[d->pezzi_n - 1].stile, s) &&
            d->pezzi[d->pezzi_n - 1].inizio + d->pezzi[d->pezzi_n - 1].lung == d->testo_n) {
            d->pezzi[d->pezzi_n - 1].lung++;
        } else if (d->pezzi_n < d->pezzi_max) {
            d->pezzi[d->pezzi_n].inizio = d->testo_n;
            d->pezzi[d->pezzi_n].lung = 1;
            d->pezzi[d->pezzi_n].stile = *s;
            d->pezzi_n++;
        } else {
            d->troncato = 1;
            return;
        }
        d->testo_n++;
        if (t[i] == '\n') {
            if (d->par_n < d->par_max) {
                d->allinea[d->par_n] = d->allinea[d->par_n - 1];
                d->par_n++;
            } else d->troncato = 1;
        }
    }
    d->testo[d->testo_n] = '\0';
}

/* =============================================================================
 * EDITING (@RTF stage 2)
 *
 * ! THE RUNS ARE FIXED UP AFTER, NOT KEPT RIGHT DURING. Every change first
 * does the plain thing - a run cut in two, a length made smaller, a new run
 * for the inserted bytes - and then unisci() drops the empty runs and joins
 * the neighbours that ended up with the same style. One place knows the
 * rule, and the three operations stay short.
 * ============================================================================= */
static void unisci(ExRtfDoc *d)
{
    unsigned int i, k = 0;

    for (i = 0; i < d->pezzi_n; i++) {
        if (d->pezzi[i].lung == 0) continue;
        if (k > 0 && stile_uguale(&d->pezzi[k - 1].stile, &d->pezzi[i].stile)) {
            d->pezzi[k - 1].lung += d->pezzi[i].lung;
            continue;
        }
        d->pezzi[k++] = d->pezzi[i];
    }
    d->pezzi_n = k;
}

/* Makes a run begin at pos. Returns its index (pezzi_n when pos is the end
 * of the text), or -1 when there is no room for one more run. */
static int spezza(ExRtfDoc *d, unsigned int pos)
{
    unsigned int i, j;

    for (i = 0; i < d->pezzi_n; i++) {
        ExRtfPezzo *p = &d->pezzi[i];

        if (p->inizio == pos) return (int)i;
        if (pos > p->inizio && pos < p->inizio + p->lung) {
            if (d->pezzi_n >= d->pezzi_max) { d->troncato = 1; return -1; }
            for (j = d->pezzi_n; j > i + 1; j--) d->pezzi[j] = d->pezzi[j - 1];
            d->pezzi[i + 1] = *p;
            d->pezzi[i + 1].inizio = pos;
            d->pezzi[i + 1].lung = p->inizio + p->lung - pos;
            p->lung = pos - p->inizio;
            d->pezzi_n++;
            return (int)(i + 1);
        }
    }
    return (int)d->pezzi_n;
}

unsigned int exrtf_paragrafo(const ExRtfDoc *d, unsigned int pos)
{
    unsigned int i, p = 0;

    if (pos > d->testo_n) pos = d->testo_n;
    for (i = 0; i < pos; i++) if (d->testo[i] == '\n') p++;
    return p;
}

unsigned int exrtf_inserisci(ExRtfDoc *d, unsigned int pos, const char *t,
                             unsigned int n, const ExRtfStile *s)
{
    unsigned int i, a_capo = 0, par, spazio;
    int k;

    if (pos > d->testo_n) pos = d->testo_n;
    spazio = d->testo_max - 1 - d->testo_n;
    if (n > spazio) { n = spazio; d->troncato = 1; }
    /* ! A NEW LINE WANTS A PARAGRAPH SLOT: the text stops at the first '\n'
     * that would find none, or the alignments would stop matching it. */
    for (i = 0; i < n; i++)
        if (t[i] == '\n') {
            if (d->par_n + a_capo >= d->par_max) { n = i; d->troncato = 1; break; }
            a_capo++;
        }
    if (n == 0) return 0;
    if (d->pezzi_n + 2 > d->pezzi_max) { d->troncato = 1; return 0; }

    par = exrtf_paragrafo(d, pos);
    k = spezza(d, pos);
    if (k < 0) return 0;

    for (i = d->testo_n + 1; i-- > pos; ) d->testo[i + n] = d->testo[i];   /* with the '\0' */
    for (i = 0; i < n; i++) d->testo[pos + i] = t[i];
    d->testo_n += n;

    for (i = (unsigned int)k; i < d->pezzi_n; i++) d->pezzi[i].inizio += n;
    for (i = d->pezzi_n; i > (unsigned int)k; i--) d->pezzi[i] = d->pezzi[i - 1];
    d->pezzi[k].inizio = pos;
    d->pezzi[k].lung = n;
    d->pezzi[k].stile = *s;
    d->pezzi_n++;
    unisci(d);

    /* The paragraph cut by the new lines: every piece keeps its alignment. */
    if (a_capo) {
        for (i = d->par_n; i-- > par + 1; ) d->allinea[i + a_capo] = d->allinea[i];
        for (i = 1; i <= a_capo; i++) d->allinea[par + i] = d->allinea[par];
        d->par_n += a_capo;
    }
    return n;
}

void exrtf_cancella(ExRtfDoc *d, unsigned int da, unsigned int a)
{
    unsigned int i, n, a_capo = 0, par;

    if (a > d->testo_n) a = d->testo_n;
    if (da >= a) return;
    n = a - da;

    /* Joined paragraphs keep the first one's alignment. */
    for (i = da; i < a; i++) if (d->testo[i] == '\n') a_capo++;
    if (a_capo) {
        par = exrtf_paragrafo(d, da);
        for (i = par + 1; i + a_capo < d->par_n; i++) d->allinea[i] = d->allinea[i + a_capo];
        d->par_n -= a_capo;
    }

    /* ! NO RUN IS CUT, so this cannot run out of room: each run loses the
     * bytes it had in [da, a) and moves back by those before it. */
    for (i = 0; i < d->pezzi_n; i++) {
        ExRtfPezzo  *p = &d->pezzi[i];
        unsigned int s = p->inizio, e = p->inizio + p->lung;
        unsigned int ts = s > da ? s : da, te = e < a ? e : a;

        if (te > ts) p->lung -= te - ts;
        if (s >= a) p->inizio = s - n;
        else if (s > da) p->inizio = da;
    }
    for (i = a; i <= d->testo_n; i++) d->testo[i - n] = d->testo[i];     /* with the '\0' */
    d->testo_n -= n;
    unisci(d);
}

int exrtf_stile_a(const ExRtfDoc *d, unsigned int pos, ExRtfStile *s)
{
    unsigned int i;

    if (d->pezzi_n == 0) return 0;
    if (pos >= d->testo_n) { *s = d->pezzi[d->pezzi_n - 1].stile; return 1; }
    for (i = 0; i < d->pezzi_n; i++)
        if (pos < d->pezzi[i].inizio + d->pezzi[i].lung) { *s = d->pezzi[i].stile; return 1; }
    *s = d->pezzi[d->pezzi_n - 1].stile;
    return 1;
}

static const char *NOMI_BASE[3];

/* The font table entry for a family: the first one of that family, or a new
 * one with the family's own name. With no table at all the family is the
 * index (see exrtf_scrivi), and nothing is added. */
static unsigned char font_di_famiglia(ExRtfDoc *d, unsigned int fam)
{
    unsigned int i, k;
    const char *n;

    if (d->font_n == 0) return (unsigned char)fam;
    for (i = 0; i < d->font_n; i++) if (d->font_fam[i] == fam) return (unsigned char)i;
    if (d->font_n >= 16) return 0;
    n = NOMI_BASE[fam < 3 ? fam : 0];
    for (k = 0; n[k] && k + 1 < EXRTF_NOME_MAX; k++) d->font_nome[d->font_n][k] = n[k];
    d->font_nome[d->font_n][k] = '\0';
    d->font_fam[d->font_n] = (unsigned char)fam;
    return (unsigned char)d->font_n++;
}

void exrtf_cambia_stile(ExRtfDoc *d, ExRtfStile *s, int cosa, unsigned int v)
{
    switch (cosa) {
    case EXRTF_C_GRASSETTO:    s->grassetto = v ? 1 : 0; break;
    case EXRTF_C_CORSIVO:      s->corsivo = v ? 1 : 0; break;
    case EXRTF_C_SOTTOLINEATO: s->sottolineato = v ? 1 : 0; break;
    case EXRTF_C_FAMIGLIA:
        if (v > EXRTF_MONO) v = EXRTF_SERIF;
        s->famiglia = (unsigned char)v;
        s->font = font_di_famiglia(d, v);
        break;
    case EXRTF_C_CORPO:        if (v >= 4 && v <= 144) s->corpo = (unsigned short)v; break;
    case EXRTF_C_COLORE:       s->colore = v & 0xFFFFFFu; break;
    default: break;
    }
}

void exrtf_cambia(ExRtfDoc *d, unsigned int da, unsigned int a, int cosa, unsigned int valore)
{
    unsigned int i;

    if (a > d->testo_n) a = d->testo_n;
    if (da >= a) return;
    if (spezza(d, a) < 0 || spezza(d, da) < 0) return;
    for (i = 0; i < d->pezzi_n; i++)
        if (d->pezzi[i].inizio >= da && d->pezzi[i].inizio < a)
            exrtf_cambia_stile(d, &d->pezzi[i].stile, cosa, valore);
    unisci(d);
}

void exrtf_allinea(ExRtfDoc *d, unsigned int da, unsigned int a, unsigned int allineamento)
{
    unsigned int p = exrtf_paragrafo(d, da), q = exrtf_paragrafo(d, a > da ? a - 1 : da);

    if (allineamento > EXRTF_GIUSTO) return;
    for (; p <= q && p < d->par_n; p++) d->allinea[p] = (unsigned char)allineamento;
}

int exrtf_e_rtf(const char *t, unsigned int n)
{
    unsigned int i = 0;

    while (i < n && (t[i] == ' ' || t[i] == '\r' || t[i] == '\n' || t[i] == '\t')) i++;
    return n - i >= 5 && t[i] == '{' && t[i+1] == '\\' && t[i+2] == 'r' &&
           t[i+3] == 't' && t[i+4] == 'f';
}

/* Windows-1252 bytes 0x80..0x9F, the ones that are not Latin-1. */
static const unsigned short CP1252[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

static unsigned int utf8(unsigned int c, char *o)
{
    if (c < 0x80)  { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | (c >> 6)); o[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) {
        o[0] = (char)(0xE0 | (c >> 12)); o[1] = (char)(0x80 | ((c >> 6) & 63));
        o[2] = (char)(0x80 | (c & 63)); return 3;
    }
    o[0] = (char)(0xF0 | (c >> 18)); o[1] = (char)(0x80 | ((c >> 12) & 63));
    o[2] = (char)(0x80 | ((c >> 6) & 63)); o[3] = (char)(0x80 | (c & 63)); return 4;
}

/* =============================================================================
 * THE READER
 * ============================================================================= */
#define PILA_MAX   64
#define COLORI_MAX 64

typedef struct {
    ExRtfStile st;
    int        salta;       /* inside a destination that is skipped */
    int        dest;        /* 0 text, 1 font table, 2 colour table */
    int        uc;          /* characters that follow \uN */
} Stato;

typedef struct {
    ExRtfDoc    *d;
    Stato        pila[PILA_MAX];
    int          liv;
    int          id_font[16];           /* \fN of the font table entries */
    unsigned int colori[COLORI_MAX];
    unsigned int colori_n;
    unsigned int r, g, b;               /* the colour being read */
    int          font_ora;              /* the entry being read in the table */
    unsigned char fam_detta[16];        /* \froman/\fswiss/\fmodern was there */
    int          da_saltare;            /* fallback characters after \u */
} Lettore;

/* ! A FAMILY NOT GIVEN IS READ FROM THE NAME. LibreOffice writes Liberation
 * Mono as \fnil — "family unknown" — and read as serif, monospaced text came
 * out in a proportional face. */
static int contiene(const char *n, const char *p)
{
    unsigned int i, j;

    for (i = 0; n[i]; i++) {
        for (j = 0; p[j] && n[i + j]; j++) {
            char a = n[i + j], b = p[j];
            if (a >= 'A' && a <= 'Z') a = (char)(a + 32);
            if (b >= 'A' && b <= 'Z') b = (char)(b + 32);
            if (a != b) break;
        }
        if (!p[j]) return 1;
    }
    return 0;
}

static void font_finisci(Lettore *L)
{
    ExRtfDoc *d = L->d;
    char     *n;
    unsigned int k;

    if (L->font_ora < 0) return;
    n = d->font_nome[L->font_ora];
    k = lun(n);
    while (k > 0 && n[k - 1] == ' ') n[--k] = '\0';
    if (L->fam_detta[L->font_ora]) return;
    if (contiene(n, "mono") || contiene(n, "courier") || contiene(n, "consol"))
        d->font_fam[L->font_ora] = EXRTF_MONO;
    else if (contiene(n, "sans") || contiene(n, "arial") || contiene(n, "helvetica") ||
             contiene(n, "verdana") || contiene(n, "calibri") || contiene(n, "tahoma") ||
             contiene(n, "segoe"))
        d->font_fam[L->font_ora] = EXRTF_SANS;
}

static void emetti(Lettore *L, unsigned int c)
{
    Stato *s = &L->pila[L->liv];
    char   b[4];

    if (s->salta) return;
    if (L->da_saltare > 0) { L->da_saltare--; return; }
    if (s->dest == 1) {                         /* a font's name */
        ExRtfDoc *d = L->d;
        if (c == ';') { font_finisci(L); return; }
        if (L->font_ora >= 0 && c >= 32 && c < 128) {
            char *n = d->font_nome[L->font_ora];
            unsigned int k = lun(n);
            if (k + 1 < EXRTF_NOME_MAX) { n[k] = (char)c; n[k + 1] = '\0'; }
        }
        return;
    }
    if (s->dest == 2) {                         /* ';' ends a colour */
        if (c == ';' && L->colori_n < COLORI_MAX)
            L->colori[L->colori_n++] = (L->r << 16) | (L->g << 8) | L->b;
        L->r = L->g = L->b = 0;
        return;
    }
    exrtf_aggiungi(L->d, b, utf8(c, b), &s->st);
}

static int font_indice(Lettore *L, int id)
{
    unsigned int i;

    for (i = 0; i < L->d->font_n; i++) if (L->id_font[i] == id) return (int)i;
    return -1;
}

static void parola(Lettore *L, const char *w, int ha_n, int n)
{
    Stato    *s = &L->pila[L->liv];
    ExRtfDoc *d = L->d;

    /* Destinations skipped whole. */
    if (stesso(w, "*") || stesso(w, "stylesheet") || stesso(w, "info") ||
        stesso(w, "pict") || stesso(w, "header") || stesso(w, "footer") ||
        stesso(w, "headerl") || stesso(w, "headerr") || stesso(w, "footerl") ||
        stesso(w, "footerr") || stesso(w, "object") || stesso(w, "listtable") ||
        stesso(w, "listoverridetable") || stesso(w, "themedata") ||
        stesso(w, "colorschememapping") || stesso(w, "latentstyles") ||
        stesso(w, "datastore") || stesso(w, "xmlnstbl") || stesso(w, "rsidtbl") ||
        stesso(w, "generator") || stesso(w, "fldinst") || stesso(w, "bkmkstart") ||
        stesso(w, "bkmkend") || stesso(w, "pgdsctbl") || stesso(w, "footnote")) {
        s->salta = 1;
        return;
    }
    if (stesso(w, "fonttbl"))  { s->dest = 1; L->font_ora = -1; return; }
    if (stesso(w, "colortbl")) { s->dest = 2; L->colori_n = 0; L->r = L->g = L->b = 0; return; }

    if (s->dest == 1) {                         /* inside the font table */
        if (stesso(w, "f") && ha_n && d->font_n < 16) {
            L->font_ora = (int)d->font_n;
            L->id_font[d->font_n] = n;
            d->font_nome[d->font_n][0] = '\0';
            d->font_fam[d->font_n] = EXRTF_SERIF;
            L->fam_detta[d->font_n] = 0;
            d->font_n++;
        } else if (L->font_ora >= 0) {
            if (stesso(w, "fswiss"))       { d->font_fam[L->font_ora] = EXRTF_SANS;  L->fam_detta[L->font_ora] = 1; }
            else if (stesso(w, "fmodern")) { d->font_fam[L->font_ora] = EXRTF_MONO;  L->fam_detta[L->font_ora] = 1; }
            else if (stesso(w, "froman"))  { d->font_fam[L->font_ora] = EXRTF_SERIF; L->fam_detta[L->font_ora] = 1; }
        }
        return;
    }
    if (s->dest == 2) {
        if (stesso(w, "red"))   L->r = (unsigned int)n & 255;
        if (stesso(w, "green")) L->g = (unsigned int)n & 255;
        if (stesso(w, "blue"))  L->b = (unsigned int)n & 255;
        return;
    }
    if (s->salta) return;

    if (stesso(w, "par") || stesso(w, "line"))  { emetti(L, '\n'); return; }
    if (stesso(w, "tab"))  { emetti(L, '\t'); return; }
    if (stesso(w, "emdash")) { emetti(L, 0x2014); return; }
    if (stesso(w, "endash")) { emetti(L, 0x2013); return; }
    if (stesso(w, "bullet")) { emetti(L, 0x2022); return; }
    if (stesso(w, "lquote")) { emetti(L, 0x2018); return; }
    if (stesso(w, "rquote")) { emetti(L, 0x2019); return; }
    if (stesso(w, "ldblquote")) { emetti(L, 0x201C); return; }
    if (stesso(w, "rdblquote")) { emetti(L, 0x201D); return; }
    if (stesso(w, "u") && ha_n) {
        emetti(L, (unsigned int)(n < 0 ? n + 65536 : n));
        L->da_saltare = s->uc;
        return;
    }
    if (stesso(w, "uc") && ha_n) { s->uc = n; return; }

    if (stesso(w, "plain")) {
        unsigned char f = s->st.font;
        exrtf_stile_base(&s->st);
        s->st.font = f;
        if (f < d->font_n) s->st.famiglia = d->font_fam[f];
        return;
    }
    if (stesso(w, "b"))  { s->st.grassetto = !(ha_n && n == 0); return; }
    if (stesso(w, "i"))  { s->st.corsivo   = !(ha_n && n == 0); return; }
    if (stesso(w, "ul")) { s->st.sottolineato = !(ha_n && n == 0); return; }
    if (stesso(w, "ulnone")) { s->st.sottolineato = 0; return; }
    if (stesso(w, "fs") && ha_n && n > 0) { s->st.corpo = (unsigned short)((n + 1) / 2); return; }
    if (stesso(w, "f") && ha_n) {
        int k = font_indice(L, n);
        if (k >= 0) { s->st.font = (unsigned char)k; s->st.famiglia = d->font_fam[k]; }
        return;
    }
    if (stesso(w, "cf") && ha_n) {
        s->st.colore = (n > 0 && (unsigned int)n < L->colori_n) ? L->colori[n] : 0;
        return;
    }
    if (stesso(w, "pard")) { d->allinea[d->par_n - 1] = EXRTF_SINISTRA; return; }
    if (stesso(w, "ql")) { d->allinea[d->par_n - 1] = EXRTF_SINISTRA; return; }
    if (stesso(w, "qc")) { d->allinea[d->par_n - 1] = EXRTF_CENTRO;   return; }
    if (stesso(w, "qr")) { d->allinea[d->par_n - 1] = EXRTF_DESTRA;   return; }
    if (stesso(w, "qj")) { d->allinea[d->par_n - 1] = EXRTF_GIUSTO;   return; }
    /* anything else: not part of the base mode, ignored */
}

int exrtf_leggi(ExRtfDoc *d, const char *t, unsigned int n)
{
    static Lettore L;
    unsigned int i = 0;

    if (!exrtf_e_rtf(t, n)) return 0;
    L.d = d;
    L.liv = 0;
    exrtf_stile_base(&L.pila[0].st);
    L.pila[0].salta = 0; L.pila[0].dest = 0; L.pila[0].uc = 1;
    L.colori_n = 0; L.font_ora = -1; L.da_saltare = 0;

    while (i < n) {
        char c = t[i];

        if (c == '{') {
            if (L.liv + 1 < PILA_MAX) { L.pila[L.liv + 1] = L.pila[L.liv]; L.liv++; }
            L.da_saltare = 0;
            i++;
            continue;
        }
        if (c == '}') {
            if (L.liv > 0) {
                if (L.pila[L.liv].dest == 1 && L.pila[L.liv - 1].dest != 1) L.font_ora = -1;
                L.liv--;
            }
            i++;
            continue;
        }
        if (c == '\r' || c == '\n') { i++; continue; }
        if (c != '\\') { emetti(&L, (unsigned char)c); i++; continue; }

        /* a control word or symbol */
        i++;
        if (i >= n) break;
        c = t[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            char w[32];
            unsigned int k = 0;
            int ha_n = 0, neg = 0, v = 0;

            while (i < n && ((t[i] >= 'a' && t[i] <= 'z') || (t[i] >= 'A' && t[i] <= 'Z'))) {
                if (k < sizeof(w) - 1) w[k++] = t[i];
                i++;
            }
            w[k] = '\0';
            if (i < n && t[i] == '-') { neg = 1; i++; }
            while (i < n && t[i] >= '0' && t[i] <= '9') { ha_n = 1; v = v * 10 + (t[i] - '0'); i++; }
            if (neg) v = -v;
            if (i < n && t[i] == ' ') i++;          /* the delimiter */
            parola(&L, w, ha_n, v);
            continue;
        }
        if (c == '\'' && i + 2 < n) {               /* \'hh: a Windows-1252 byte */
            unsigned int h = 0, j;
            for (j = 1; j <= 2; j++) {
                char x = t[i + j];
                h = h * 16 + (unsigned int)((x >= '0' && x <= '9') ? x - '0' :
                                            (x >= 'a' && x <= 'f') ? x - 'a' + 10 :
                                            (x >= 'A' && x <= 'F') ? x - 'A' + 10 : 0);
            }
            i += 3;
            emetti(&L, (h >= 0x80 && h < 0xA0) ? CP1252[h - 0x80] : h);
            continue;
        }
        if (c == '*') { parola(&L, "*", 0, 0); i++; continue; }
        if (c == '~') { emetti(&L, 0xA0); i++; continue; }
        if (c == '-' || c == '_') { i++; continue; }     /* optional hyphen */
        if (c == '\\' || c == '{' || c == '}') { emetti(&L, (unsigned char)c); i++; continue; }
        if (c == '\r' || c == '\n') { emetti(&L, '\n'); i++; continue; }  /* \<newline> = \par */
        i++;
    }
    /* A document that ends with \par has an empty last paragraph: fine. */
    return 1;
}

/* =============================================================================
 * THE WRITER
 * ============================================================================= */
typedef struct { char *o; unsigned int n, max; int pieno; } Uscita;

static void scrivi(Uscita *u, const char *s)
{
    while (*s) {
        if (u->n + 1 >= u->max) { u->pieno = 1; return; }
        u->o[u->n++] = *s++;
    }
}

static void scrivi_num(Uscita *u, int v)
{
    char b[12];
    int  k = 0;
    unsigned int x = (unsigned int)(v < 0 ? -v : v);

    if (v < 0) scrivi(u, "-");
    do { b[k++] = (char)('0' + x % 10); x /= 10; } while (x);
    while (k) { char c[2]; c[0] = b[--k]; c[1] = '\0'; scrivi(u, c); }
}

static const char *NOMI_BASE[3] = { "Liberation Serif", "Liberation Sans", "Liberation Mono" };
static const char *FAM_RTF[3]   = { "\\froman", "\\fswiss", "\\fmodern" };

unsigned int exrtf_scrivi(const ExRtfDoc *d, char *out, unsigned int max)
{
    Uscita       u;
    unsigned int colori[COLORI_MAX], nc = 0, i, k, par = 0, nf;
    int          apri = 1;

    u.o = out; u.n = 0; u.max = max; u.pieno = 0;
    if (max == 0) return 0;

    /* The colour table: every colour used, black being entry 0 (the auto). */
    for (i = 0; i < d->pezzi_n; i++) {
        unsigned int c = d->pezzi[i].stile.colore;
        for (k = 0; k < nc && colori[k] != c; k++) ;
        if (k == nc && nc < COLORI_MAX) colori[nc++] = c;
    }

    scrivi(&u, "{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl");
    nf = d->font_n ? d->font_n : 3;
    for (i = 0; i < nf; i++) {
        unsigned int fam = d->font_n ? d->font_fam[i] : i;
        scrivi(&u, "{\\f"); scrivi_num(&u, (int)i); scrivi(&u, FAM_RTF[fam < 3 ? fam : 0]);
        scrivi(&u, " ");
        scrivi(&u, d->font_n && d->font_nome[i][0] ? d->font_nome[i] : NOMI_BASE[fam < 3 ? fam : 0]);
        scrivi(&u, ";}");
    }
    scrivi(&u, "}\n{\\colortbl ;");
    for (i = 0; i < nc; i++) {
        scrivi(&u, "\\red");   scrivi_num(&u, (int)((colori[i] >> 16) & 255));
        scrivi(&u, "\\green"); scrivi_num(&u, (int)((colori[i] >> 8) & 255));
        scrivi(&u, "\\blue");  scrivi_num(&u, (int)(colori[i] & 255));
        scrivi(&u, ";");
    }
    scrivi(&u, "}\n");

    for (i = 0; i < d->pezzi_n; i++) {
        const ExRtfPezzo *p = &d->pezzi[i];
        const ExRtfStile *s = &p->stile;
        unsigned int      j = p->inizio, fine = p->inizio + p->lung, f, ci;

        /* The style of the run, written whole: simpler than differences, and
         * every reader understands it. The font: the one read, or the
         * family's own entry when the document had no table. */
        f = d->font_n ? s->font : s->famiglia;
        for (ci = 0; ci < nc && colori[ci] != s->colore; ci++) ;
        scrivi(&u, "\\plain\\f"); scrivi_num(&u, (int)f);
        scrivi(&u, "\\fs"); scrivi_num(&u, (int)s->corpo * 2);
        scrivi(&u, "\\cf"); scrivi_num(&u, (int)ci + 1);
        if (s->grassetto) scrivi(&u, "\\b");
        if (s->corsivo) scrivi(&u, "\\i");
        if (s->sottolineato) scrivi(&u, "\\ul");
        scrivi(&u, " ");

        while (j < fine) {
            unsigned char c = (unsigned char)d->testo[j];
            unsigned int  cp = c, l = 1;

            if (apri) {
                static const char *Q[4] = { "\\ql ", "\\qc ", "\\qr ", "\\qj " };
                unsigned int a = par < d->par_n ? d->allinea[par] : 0;
                scrivi(&u, "\\pard"); scrivi(&u, Q[a < 4 ? a : 0]);
                apri = 0;
            }
            if (c >= 0xF0 && j + 3 < fine + 3)      { cp = ((c & 7u) << 18) | ((d->testo[j+1] & 63u) << 12) | ((d->testo[j+2] & 63u) << 6) | (d->testo[j+3] & 63u); l = 4; }
            else if (c >= 0xE0)                     { cp = ((c & 15u) << 12) | ((d->testo[j+1] & 63u) << 6) | (d->testo[j+2] & 63u); l = 3; }
            else if (c >= 0xC0)                     { cp = ((c & 31u) << 6) | (d->testo[j+1] & 63u); l = 2; }

            if (cp == '\n') { scrivi(&u, "\\par\n"); par++; apri = 1; }
            else if (cp == '\t') scrivi(&u, "\\tab ");
            else if (cp == '\\') scrivi(&u, "\\\\");
            else if (cp == '{')  scrivi(&u, "\\{");
            else if (cp == '}')  scrivi(&u, "\\}");
            else if (cp < 128) { char b[2]; b[0] = (char)cp; b[1] = '\0'; scrivi(&u, b); }
            else if (cp < 0x10000) {
                scrivi(&u, "\\u"); scrivi_num(&u, cp > 32767 ? (int)cp - 65536 : (int)cp);
                scrivi(&u, "?");
            } else scrivi(&u, "?");
            j += l;
        }
    }
    scrivi(&u, "}\n");
    if (u.pieno) { out[0] = '\0'; return 0; }
    out[u.n] = '\0';
    return u.n;
}
