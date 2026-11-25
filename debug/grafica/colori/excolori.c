/* =============================================================================
 * debug/grafica/colori/excolori.c  ->  destinazione: lib/exwin/excolori.c
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 * Gli schemi di colore: dai colori base alla tavolozza intera, e il file.
 * Vedi excolori.h. Niente libc: compila dentro exwin.so come extema.c.
 * ============================================================================= */
#include "excolori.h"

/* -----------------------------------------------------------------------------
 * Gli elementi
 * --------------------------------------------------------------------------- */
static const struct { const char *chiave, *etichetta, *spiega; } g_el[EXC_N] = {
    { "scrivania",          "Scrivania (sopra)",      "Il fondo della scrivania, in alto." },
    { "scrivania2",         "Scrivania (sotto)",      "Il fondo della scrivania, in basso: uguale al primo per un colore pieno." },
    { "finestra",           "Finestre e pulsanti",    "Il grigio di finestre, pannelli e pulsanti: da qui vengono anche i rilievi." },
    { "testo",              "Testo",                  "Le scritte su finestre e pulsanti." },
    { "campo",              "Caselle e liste",        "Il fondo dove si scrive e si sceglie." },
    { "campo_testo",        "Testo nelle caselle",    "Le scritte nelle caselle e nelle liste." },
    { "selezione",          "Selezione",              "La riga scelta, la voce di menu, il pulsante predefinito, il progresso." },
    { "selezione_testo",    "Testo selezionato",      "Le scritte sopra la selezione." },
    { "barra",              "Barra attiva (sinistra)","La barra del titolo della finestra in primo piano." },
    { "barra2",             "Barra attiva (destra)",  "Dove arriva la sfumatura della barra attiva." },
    { "titolo",             "Titolo attivo",          "Il titolo sulla barra attiva." },
    { "barra_inattiva",     "Barra inattiva (sinistra)", "La barra delle finestre dietro." },
    { "barra_inattiva2",    "Barra inattiva (destra)","Dove arriva la sfumatura della barra inattiva." },
    { "titolo_inattivo",    "Titolo inattivo",        "Il titolo sulle barre inattive." },
    { "luce",               "Luce del rilievo",       "Il bordo chiaro, in alto a sinistra." },
    { "ombra",              "Ombra del rilievo",      "Il bordo scuro, in basso a destra." },
    { "suggerimento",       "Suggerimenti",           "Il fondo dei suggerimenti che compaiono sotto il mouse." },
    { "suggerimento_testo", "Testo dei suggerimenti", "Le scritte dei suggerimenti." },
    { "chiudi",             "Pulsante di chiusura",   "Il segno di chiusura sulla barra del titolo." },
};

const char *excol_chiave(int e)    { return (e >= 0 && e < EXC_N) ? g_el[e].chiave : ""; }
const char *excol_etichetta(int e) { return (e >= 0 && e < EXC_N) ? g_el[e].etichetta : ""; }
const char *excol_spiega(int e)    { return (e >= 0 && e < EXC_N) ? g_el[e].spiega : ""; }

/* -----------------------------------------------------------------------------
 * I campi della tavolozza per nome (i ritocchi)
 * --------------------------------------------------------------------------- */
#define CAMPO(n) { #n, __builtin_offsetof(ExTavolozza, n) }
static const struct { const char *nome; unsigned int off; } g_campi[] = {
    CAMPO(desktop_a), CAMPO(desktop_b), CAMPO(fondo), CAMPO(fondo_alt), CAMPO(testo),
    CAMPO(testo_spento), CAMPO(campo), CAMPO(campo_testo), CAMPO(campo_spento), CAMPO(bordo),
    CAMPO(bordo_sopra), CAMPO(luce), CAMPO(ombra), CAMPO(luce2), CAMPO(ombra2), CAMPO(accento),
    CAMPO(accento_testo), CAMPO(accento_chiaro), CAMPO(fuoco), CAMPO(puls_a), CAMPO(puls_b),
    CAMPO(puls_sopra_a), CAMPO(puls_sopra_b), CAMPO(puls_giu_a), CAMPO(puls_giu_b),
    CAMPO(puls_testo), CAMPO(barra_a), CAMPO(barra_b), CAMPO(barra_ina_a), CAMPO(barra_ina_b),
    CAMPO(titolo), CAMPO(titolo_ina), CAMPO(telaio), CAMPO(chiudi), CAMPO(chiudi_sopra),
    CAMPO(riduci), CAMPO(ingrandisci), CAMPO(scorri_fondo), CAMPO(scorri_cursore),
    CAMPO(scorri_cursore_sopra), CAMPO(menu_fondo), CAMPO(menu_testo), CAMPO(menu_sopra),
    CAMPO(menu_sopra_testo), CAMPO(ombra_finestra), CAMPO(suggerimento),
    CAMPO(suggerimento_testo), CAMPO(progresso_a), CAMPO(progresso_b),
};
#define N_CAMPI ((int)(sizeof(g_campi) / sizeof(g_campi[0])))

int excol_campi(void) { return N_CAMPI; }
const char *excol_campo_nome(int i) { return (i >= 0 && i < N_CAMPI) ? g_campi[i].nome : ""; }

static int uguale_n(const char *a, const char *b, int n)    /* a lungo n, b terminato */
{
    int i;
    for (i = 0; i < n; i++) if (a[i] != b[i] || !b[i]) return 0;
    return b[n] == 0;
}

int excol_campo(const char *nome, int n)
{
    int i;
    for (i = 0; i < N_CAMPI; i++) if (uguale_n(nome, g_campi[i].nome, n)) return i;
    return -1;
}

static unsigned int *campo_di(ExTavolozza *p, int i)
{
    return (unsigned int *)((unsigned char *)p + g_campi[i].off);
}

/* -----------------------------------------------------------------------------
 * Colori scritti
 * --------------------------------------------------------------------------- */
static int esa_cifra(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* "#RRGGBB", "RRGGBB", "0xRRGGBB", "r,g,b" (0..255, spazi ammessi) */
int excol_colore(const char *s, int n, unsigned int *out)
{
    int i = 0, k, v = 0;

    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;
    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
    if (i < n && s[i] == '#') i++;
    else if (i + 1 < n && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) i += 2;
    if (n - i == 6) {
        unsigned int c = 0;
        for (k = 0; k < 6; k++) {
            int d = esa_cifra(s[i + k]);
            if (d < 0) goto decimali;
            c = (c << 4) | (unsigned int)d;
        }
        *out = c;
        return 1;
    }
decimali:
    {
        unsigned int c = 0;
        int parti = 0, cifre = 0;

        for (i = 0; i <= n; i++) {
            char ch = i < n ? s[i] : ',';
            if (ch >= '0' && ch <= '9') { v = v * 10 + (ch - '0'); cifre++; if (v > 255) return 0; }
            else if (ch == ',') {
                if (!cifre || parti == 3) return 0;
                c = (c << 8) | (unsigned int)v; parti++; v = 0; cifre = 0;
            } else if (ch != ' ' && ch != '\t') return 0;
        }
        if (parti != 3) return 0;
        *out = c;
        return 1;
    }
}

void excol_esa(unsigned int c, char out[8])
{
    static const char h[] = "0123456789ABCDEF";
    int k;
    out[0] = '#';
    for (k = 0; k < 6; k++) out[1 + k] = h[(c >> (20 - 4 * k)) & 15u];
    out[7] = 0;
}

/* -----------------------------------------------------------------------------
 * Contrasto (WCAG 2): luminanza relativa con la curva sRGB, in decimillesimi
 * --------------------------------------------------------------------------- */
static const unsigned short g_lin[256] = {
        0,     3,     6,     9,    12,    15,    18,    21,    24,    27,    30,    33,    37,    40,    44,    48,
       52,    56,    60,    65,    70,    75,    80,    86,    91,    97,   103,   110,   116,   123,   130,   137,
      144,   152,   160,   168,   176,   185,   194,   203,   212,   222,   232,   242,   252,   262,   273,   284,
      296,   307,   319,   331,   343,   356,   369,   382,   395,   409,   423,   437,   452,   467,   482,   497,
      513,   529,   545,   561,   578,   595,   612,   630,   648,   666,   685,   704,   723,   742,   762,   782,
      802,   823,   844,   865,   887,   908,   931,   953,   976,   999,  1022,  1046,  1070,  1095,  1119,  1144,
     1170,  1195,  1221,  1248,  1274,  1301,  1329,  1356,  1384,  1413,  1441,  1470,  1500,  1529,  1559,  1590,
     1620,  1651,  1683,  1714,  1746,  1779,  1812,  1845,  1878,  1912,  1946,  1981,  2016,  2051,  2086,  2122,
     2159,  2195,  2232,  2270,  2307,  2346,  2384,  2423,  2462,  2502,  2542,  2582,  2623,  2664,  2705,  2747,
     2789,  2831,  2874,  2918,  2961,  3005,  3050,  3095,  3140,  3185,  3231,  3278,  3325,  3372,  3419,  3467,
     3515,  3564,  3613,  3663,  3712,  3763,  3813,  3864,  3916,  3968,  4020,  4072,  4125,  4179,  4233,  4287,
     4342,  4397,  4452,  4508,  4564,  4621,  4678,  4735,  4793,  4851,  4910,  4969,  5029,  5089,  5149,  5210,
     5271,  5333,  5395,  5457,  5520,  5583,  5647,  5711,  5776,  5841,  5906,  5972,  6038,  6105,  6172,  6240,
     6308,  6376,  6445,  6514,  6584,  6654,  6724,  6795,  6867,  6939,  7011,  7084,  7157,  7231,  7305,  7379,
     7454,  7529,  7605,  7682,  7758,  7835,  7913,  7991,  8070,  8148,  8228,  8308,  8388,  8469,  8550,  8632,
     8714,  8796,  8879,  8963,  9047,  9131,  9216,  9301,  9387,  9473,  9560,  9647,  9734,  9823,  9911, 10000,
};

static int lum(unsigned int c)          /* 0..10000 */
{
    return (2126 * g_lin[(c >> 16) & 255u] + 7152 * g_lin[(c >> 8) & 255u] +
            722 * g_lin[c & 255u]) / 10000;
}

int excol_contrasto(unsigned int a, unsigned int b)
{
    int la = lum(a), lb = lum(b);
    if (la < lb) { int t = la; la = lb; lb = t; }
    return (la + 500) * 100 / (lb + 500);
}

/* -----------------------------------------------------------------------------
 * Dal tema allo schema, e dallo schema alla tavolozza
 * --------------------------------------------------------------------------- */
static void copia_nome(char *d, const char *s, int max)
{
    int i;
    for (i = 0; s && s[i] && i < max - 1; i++) d[i] = s[i];
    d[i] = 0;
}

void excol_da_tema(ExColori *s, const ExTema *m)
{
    int i;
    unsigned char *p = (unsigned char *)s;

    for (i = 0; i < (int)sizeof(*s); i++) p[i] = 0;
    copia_nome(s->schema, m->titolo, EXC_NOME_MAX);
    copia_nome(s->tema, m->nome, (int)sizeof(s->tema));
    s->c[EXC_SCRIVANIA]          = m->c.desktop_a;
    s->c[EXC_SCRIVANIA2]         = m->c.desktop_b;
    s->c[EXC_FINESTRA]           = m->c.fondo;
    s->c[EXC_TESTO]              = m->c.testo;
    s->c[EXC_CAMPO]              = m->c.campo;
    s->c[EXC_CAMPO_TESTO]        = m->c.campo_testo;
    s->c[EXC_SELEZIONE]          = m->c.accento;
    s->c[EXC_SELEZIONE_TESTO]    = m->c.accento_testo;
    s->c[EXC_BARRA]              = m->c.barra_a;
    s->c[EXC_BARRA2]             = m->c.barra_b;
    s->c[EXC_TITOLO]             = m->c.titolo;
    s->c[EXC_BARRA_INA]          = m->c.barra_ina_a;
    s->c[EXC_BARRA_INA2]         = m->c.barra_ina_b;
    s->c[EXC_TITOLO_INA]         = m->c.titolo_ina;
    s->c[EXC_LUCE]               = m->c.luce;
    s->c[EXC_OMBRA]              = m->c.ombra;
    s->c[EXC_SUGGERIMENTO]       = m->c.suggerimento;
    s->c[EXC_SUGGERIMENTO_TESTO] = m->c.suggerimento_testo;
    s->c[EXC_CHIUDI]             = m->c.chiudi;
    s->sfumata   = m->f.sfumato;
    s->grassetto = m->f.titolo_grassetto;
    s->ombra     = m->f.ombra_finestra;
    s->raggio    = m->f.raggio;
}

/* Le regole: ogni colore derivato dice da quali base viene. Mescolare
 * (ext_mescola) e schiarire (ext_schiarisci) in 255esimi. Le regole sono
 * tarate perche' lo schema "Classico" ridia la tavolozza di tema_01: scarto
 * massimo 12 su un canale (i quattro campi che differivano di piu' sono
 * stati portati in tema_01 ai valori ricavati). */
void excol_applica(const ExColori *s, const ExTema *base, ExTema *out)
{
    const unsigned int *b = s->c;
    ExTavolozza *p = &out->c;
    unsigned int f = b[EXC_FINESTRA], t = b[EXC_TESTO];
    int i, scuro = lum(f) < 1800;           /* finestre scure: i rilievi si invertono di verso */

    *out = *base;

    p->desktop_a = b[EXC_SCRIVANIA];
    p->desktop_b = b[EXC_SCRIVANIA2];
    p->fondo     = f;
    p->fondo_alt = ext_schiarisci(f, scuro ? 18 : -14);
    p->telaio    = f;
    p->testo     = t;
    p->testo_spento = ext_mescola(t, f, 150);
    p->campo        = b[EXC_CAMPO];
    p->campo_testo  = b[EXC_CAMPO_TESTO];
    p->campo_spento = f;
    p->luce   = b[EXC_LUCE];
    p->ombra  = b[EXC_OMBRA];
    p->luce2  = ext_mescola(b[EXC_LUCE], f, 150);
    p->ombra2 = ext_mescola(b[EXC_OMBRA], f, 110);
    p->bordo       = ext_mescola(b[EXC_OMBRA], f, 120);
    p->bordo_sopra = b[EXC_OMBRA];
    p->accento        = b[EXC_SELEZIONE];
    p->accento_testo  = b[EXC_SELEZIONE_TESTO];
    p->accento_chiaro = ext_mescola(b[EXC_CAMPO], b[EXC_SELEZIONE], 70);
    p->fuoco = t;
    p->puls_a       = ext_schiarisci(f, 28);
    p->puls_b       = ext_schiarisci(f, -10);
    p->puls_sopra_a = ext_schiarisci(f, 60);
    p->puls_sopra_b = ext_schiarisci(f, 10);
    p->puls_giu_a   = ext_schiarisci(f, -28);
    p->puls_giu_b   = ext_schiarisci(f, -10);
    p->puls_testo   = t;
    p->barra_a     = b[EXC_BARRA];
    p->barra_b     = b[EXC_BARRA2];
    p->barra_ina_a = b[EXC_BARRA_INA];
    p->barra_ina_b = b[EXC_BARRA_INA2];
    p->titolo      = b[EXC_TITOLO];
    p->titolo_ina  = b[EXC_TITOLO_INA];
    p->chiudi       = b[EXC_CHIUDI];
    p->chiudi_sopra = ext_schiarisci(b[EXC_CHIUDI], 40);
    p->riduci = f;
    p->ingrandisci = f;
    p->scorri_fondo         = ext_mescola(f, b[EXC_LUCE], 120);
    p->scorri_cursore       = f;
    p->scorri_cursore_sopra = ext_schiarisci(f, 28);
    p->menu_fondo       = ext_mescola(f, b[EXC_CAMPO], 140);
    p->menu_testo       = t;
    p->menu_sopra       = b[EXC_SELEZIONE];
    p->menu_sopra_testo = b[EXC_SELEZIONE_TESTO];
    p->ombra_finestra   = 0x00000000u;
    p->suggerimento       = b[EXC_SUGGERIMENTO];
    p->suggerimento_testo = b[EXC_SUGGERIMENTO_TESTO];
    p->progresso_a = b[EXC_SELEZIONE];
    p->progresso_b = ext_mescola(b[EXC_SELEZIONE], b[EXC_BARRA2], 128);

    out->f.sfumato          = s->sfumata;
    out->f.titolo_grassetto = s->grassetto;
    out->f.ombra_finestra   = s->ombra < 0 ? 0 : s->ombra > 24 ? 24 : s->ombra;
    out->f.ombra_alfa       = out->f.ombra_finestra ? 110 : 0;
    out->f.raggio           = s->raggio < 0 ? 0 : s->raggio > 8 ? 8 : s->raggio;
    out->titolo = s->schema;

    for (i = 0; i < s->n_ritocchi; i++)
        if (s->rit_campo[i] >= 0 && s->rit_campo[i] < N_CAMPI)
            *campo_di(p, s->rit_campo[i]) = s->rit_valore[i] & 0xFFFFFFu;
}

/* -----------------------------------------------------------------------------
 * Il file
 * --------------------------------------------------------------------------- */
static int si_no(const char *v, int n)
{
    if (n >= 2 && (v[0] == 's' || v[0] == 'S') && (v[1] == 'i' || v[1] == 'I')) return 1;
    if (n >= 1 && v[0] == '1') return 1;
    return 0;
}

static int numero(const char *v, int n)
{
    int i, x = 0;
    for (i = 0; i < n && v[i] >= '0' && v[i] <= '9'; i++) x = x * 10 + (v[i] - '0');
    return x;
}

int excol_leggi(ExColori *s, const char *testo, int n)
{
    int i = 0, ignote = 0;

    while (i < n) {
        int a = i, e, k, kl, v, vl, rit = 0;

        while (i < n && testo[i] != '\n') i++;
        e = i;
        if (i < n) i++;
        while (a < e && (testo[a] == ' ' || testo[a] == '\t')) a++;
        if (a >= e || testo[a] == '#') continue;
        if (testo[a] == '!') { rit = 1; a++; }
        for (k = a; k < e && testo[k] != '='; k++) ;
        if (k >= e) { ignote++; continue; }
        kl = k - a;
        while (kl > 0 && (testo[a + kl - 1] == ' ' || testo[a + kl - 1] == '\t')) kl--;
        v = k + 1;
        while (v < e && (testo[v] == ' ' || testo[v] == '\t')) v++;
        vl = e - v;
        while (vl > 0 && (testo[v + vl - 1] == ' ' || testo[v + vl - 1] == '\r' ||
                          testo[v + vl - 1] == '\t')) vl--;

        if (rit) {
            int c = excol_campo(testo + a, kl);
            unsigned int col;
            if (c < 0 || !excol_colore(testo + v, vl, &col) || s->n_ritocchi >= EXC_RITOCCHI_MAX) {
                ignote++;
                continue;
            }
            s->rit_campo[s->n_ritocchi] = c;
            s->rit_valore[s->n_ritocchi++] = col;
            continue;
        }
        if (uguale_n(testo + a, "schema", kl)) {
            int j;
            for (j = 0; j < vl && j < EXC_NOME_MAX - 1; j++) s->schema[j] = testo[v + j];
            s->schema[j] = 0;
            continue;
        }
        if (uguale_n(testo + a, "tema", kl)) {
            int j;
            for (j = 0; j < vl && j < (int)sizeof(s->tema) - 1; j++) s->tema[j] = testo[v + j];
            s->tema[j] = 0;
            continue;
        }
        if (uguale_n(testo + a, "sfumata", kl))   { s->sfumata = si_no(testo + v, vl); continue; }
        if (uguale_n(testo + a, "grassetto", kl)) { s->grassetto = si_no(testo + v, vl); continue; }
        if (uguale_n(testo + a, "ombra", kl))     { s->ombra = numero(testo + v, vl); continue; }
        if (uguale_n(testo + a, "raggio", kl))    { s->raggio = numero(testo + v, vl); continue; }
        {
            int el, ok = 0;
            for (el = 0; el < EXC_N; el++)
                if (uguale_n(testo + a, g_el[el].chiave, kl)) {
                    unsigned int col;
                    if (excol_colore(testo + v, vl, &col)) { s->c[el] = col; ok = 1; }
                    break;
                }
            if (!ok) ignote++;
        }
    }
    return ignote;
}

/* scrittura in un buffer: tutte le righe, ritocchi compresi */
static int metti(char *o, int *k, int max, const char *s)
{
    while (*s) { if (*k >= max - 1) return -1; o[(*k)++] = *s++; }
    o[*k] = 0;
    return 0;
}

int excol_scrivi(const ExColori *s, char *o, int max)
{
    int k = 0, e;
    char esa[8], num[8];

    if (max < 1) return -1;
    o[0] = 0;
    if (metti(o, &k, max, "# ExWin: i colori. Li scrive Impostazioni > Colori.\n") ||
        metti(o, &k, max, "schema = ") || metti(o, &k, max, s->schema) ||
        metti(o, &k, max, "\ntema = ") || metti(o, &k, max, s->tema[0] ? s->tema : "classico") ||
        metti(o, &k, max, "\n")) return -1;
    for (e = 0; e < EXC_N; e++) {
        excol_esa(s->c[e], esa);
        if (metti(o, &k, max, g_el[e].chiave) || metti(o, &k, max, " = ") ||
            metti(o, &k, max, esa) || metti(o, &k, max, "\n")) return -1;
    }
    num[0] = (char)('0' + (s->ombra / 10) % 10); num[1] = (char)('0' + s->ombra % 10); num[2] = 0;
    if (metti(o, &k, max, "sfumata = ") || metti(o, &k, max, s->sfumata ? "si" : "no") ||
        metti(o, &k, max, "\ngrassetto = ") || metti(o, &k, max, s->grassetto ? "si" : "no") ||
        metti(o, &k, max, "\nombra = ") || metti(o, &k, max, num[0] == '0' ? num + 1 : num) ||
        metti(o, &k, max, "\nraggio = ")) return -1;
    num[0] = (char)('0' + (s->raggio % 10)); num[1] = 0;
    if (metti(o, &k, max, num) || metti(o, &k, max, "\n")) return -1;
    for (e = 0; e < s->n_ritocchi; e++) {
        excol_esa(s->rit_valore[e], esa);
        if (metti(o, &k, max, "!") || metti(o, &k, max, excol_campo_nome(s->rit_campo[e])) ||
            metti(o, &k, max, " = ") || metti(o, &k, max, esa) || metti(o, &k, max, "\n"))
            return -1;
    }
    return k;
}
