/* =============================================================================
 * lib/exjs/base.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La libreria di base — il quinto pezzo di ExJs
 *
 * -----------------------------------------------------------------------------
 * ! E' SCRITTA CON LE STESSE FUNZIONI NATIVE CHE USERA' exdom, e non con una
 * porta di servizio interna.
 *
 * `Math.floor` e `document.getElementById` si registrano nello stesso modo:
 * una ExJsNativa appesa a un oggetto. Se la libreria di base avesse una strada
 * privilegiata, sarebbe la strada di cui nessun altro dispone — e il primo a
 * scoprirlo sarebbe chi scrive il ponte col documento, nel momento peggiore.
 * Quello che si puo' fare qui si puo' fare da fuori.
 *
 * -----------------------------------------------------------------------------
 * ! QUESTA E' LA PARTE CHE LE PAGINE TOCCANO DAVVERO.
 *
 * Un motore che sa fare le chiusure ma non `indexOf` non apre niente: il codice
 * vero e' fatto per meta' di chiamate a String e ad Array. Percio' qui non c'e'
 * la norma per intero — c'e' quello che il codice vero usa, e quello che manca
 * e' scritto in fondo invece che scoperto a pagina aperta.
 *
 * -----------------------------------------------------------------------------
 * ! LE STRINGHE NON SI POSSONO TENERE FRA DUE CHIAMATE, e qui e' la trappola
 * che si ripete: exjs_a_stringa rende un posto di servizio che la chiamata
 * dopo riscrive. Ogni funzione che ne maneggia due le copia PRIMA. E' gia'
 * costato una volta in run.c, dentro la concatenazione.
 * ============================================================================= */

#include "exjs_int.h"

#define TESTO_MAX 512

/* Copia una stringa in un posto sicuro. Rende 1 se ci stava tutta: chi ne
 * maneggia due deve saperlo, perche' una copia troncata darebbe un risultato
 * sbagliato invece di un errore. */
static int copia_val(ExJsCtx *c, ExJsVal v, char *dst, unsigned int max)
{
    const char  *s = exjs_a_stringa(c, v);
    unsigned int i;

    for (i = 0; i + 1 < max && s[i]; i++) dst[i] = s[i];
    dst[i] = '\0';
    return s[i] == '\0';
}

static unsigned int lung(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

/* L'argomento i-esimo, o `undefined` se non c'e'. Non e' zucchero: meta' delle
 * funzioni di JavaScript si possono chiamare con meno argomenti del previsto,
 * e ognuna dovrebbe altrimenti scrivere lo stesso controllo. */
static ExJsVal arg_di(const ExJsVal *a, int n, int i)
{
    return (i < n) ? a[i] : exjs_indefinito();
}

static int arg_intero(ExJsCtx *c, const ExJsVal *a, int n, int i, int se_manca)
{
    double d;

    if (i >= n) return se_manca;
    d = exjs_a_numero(c, a[i]);
    if (d != d) return 0;                       /* NaN diventa zero */
    return (int)(long long)d;
}

/* =============================================================================
 * console.log
 *
 * ! DOVE FINISCE IL TESTO LO DECIDE CHI OSPITA, non questa funzione: passa da
 * exjs_uscita_scrivi, e chi non ne ha registrata nessuna semplicemente non
 * vede niente. Il perche' sta accanto a ExJsUscita in exjs.h — una libreria
 * che scrivesse sullo standard output da se' stamperebbe, dentro un server
 * grafico, su una console che nessuno guarda.
 * ========================================================================== */
static ExJsVal nat_log(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n,
                       void *dato)
{
    int i;

    (void)questo; (void)dato;
    for (i = 0; i < n; i++) {
        char b[TESTO_MAX];
        copia_val(c, a[i], b, sizeof(b));
        if (i) exjs_uscita_scrivi(c, " ", 1);
        exjs_uscita_scrivi(c, b, lung(b));
    }
    exjs_uscita_scrivi(c, "\n", 1);
    return exjs_indefinito();
}

/* =============================================================================
 * I NOMI GLOBALI
 * ========================================================================== */

/* ! parseInt SI FERMA ALLA PRIMA COSA CHE NON E' UNA CIFRA, e Number NO. Sono
 * due funzioni diverse e le pagine si appoggiano alla differenza:
 * `parseInt("12px")` fa 12, `Number("12px")` fa NaN. Chi le confonde legge
 * male ogni misura CSS. */
static ExJsVal nat_parseint(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                            int n, void *dato)
{
    char         b[TESTO_MAX];
    unsigned int i = 0;
    int          segno = 1, cifre = 0, base;
    double       v = 0.0;

    (void)questo; (void)dato;
    copia_val(c, arg_di(a, n, 0), b, sizeof(b));
    base = arg_intero(c, a, n, 1, 0);

    while (b[i] == ' ' || b[i] == '\t' || b[i] == '\n' || b[i] == '\r') i++;
    if (b[i] == '+' || b[i] == '-') { if (b[i] == '-') segno = -1; i++; }

    if ((base == 0 || base == 16) && b[i] == '0' &&
        (b[i+1] == 'x' || b[i+1] == 'X')) { i += 2; base = 16; }
    if (base == 0) base = 10;

    for (;;) {
        int d;
        if (b[i] >= '0' && b[i] <= '9')                   d = b[i] - '0';
        else if ((b[i]|0x20) >= 'a' && (b[i]|0x20) <= 'z') d = (b[i]|0x20) - 'a' + 10;
        else break;
        if (d >= base) break;
        v = v * base + d;
        i++; cifre++;
    }

    if (!cifre) return exjs_numero(c, 0.0/0.0);
    return exjs_numero(c, segno * v);
}

static ExJsVal nat_parsefloat(ExJsCtx *c, ExJsVal questo, const ExJsVal *a,
                              int n, void *dato)
{
    char         b[TESTO_MAX];
    unsigned int i = 0, fine;
    ExJsVal      v;

    (void)questo; (void)dato;
    copia_val(c, arg_di(a, n, 0), b, sizeof(b));

    while (b[i] == ' ' || b[i] == '\t' || b[i] == '\n' || b[i] == '\r') i++;
    fine = i;
    if (b[fine] == '+' || b[fine] == '-') fine++;
    while (b[fine] >= '0' && b[fine] <= '9') fine++;
    if (b[fine] == '.') { fine++; while (b[fine] >= '0' && b[fine] <= '9') fine++; }
    if (b[fine] == 'e' || b[fine] == 'E') {
        unsigned int f2 = fine + 1;
        if (b[f2] == '+' || b[f2] == '-') f2++;
        if (b[f2] >= '0' && b[f2] <= '9') {
            while (b[f2] >= '0' && b[f2] <= '9') f2++;
            fine = f2;
        }
    }
    b[fine] = '\0';

    /* La conversione vera la fa exjs_a_numero, che sa gia' leggere un numero:
     * riscriverla qui vorrebbe dire due letture di numero che devono restare
     * d'accordo. */
    v = exjs_stringa(c, b + i, -1);
    return exjs_numero(c, exjs_a_numero(c, v));
}

static ExJsVal nat_isnan(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n,
                         void *dato)
{
    double d = exjs_a_numero(c, arg_di(a, n, 0));
    (void)questo; (void)dato;
    return exjs_booleano(d != d);
}

static ExJsVal nat_isfinite(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n,
                            void *dato)
{
    double d = exjs_a_numero(c, arg_di(a, n, 0));
    (void)questo; (void)dato;
    return exjs_booleano(d == d && d < 1.7e308 && d > -1.7e308);
}

static ExJsVal nat_String(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n,
                          void *dato)
{
    char b[TESTO_MAX];
    (void)questo; (void)dato;
    if (n == 0) return exjs_stringa(c, "", -1);
    copia_val(c, a[0], b, sizeof(b));
    return exjs_stringa(c, b, -1);
}

static ExJsVal nat_Number(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n,
                          void *dato)
{
    (void)questo; (void)dato;
    if (n == 0) return exjs_numero(c, 0.0);
    return exjs_numero(c, exjs_a_numero(c, a[0]));
}

static ExJsVal nat_Boolean(ExJsCtx *c, ExJsVal questo, const ExJsVal *a, int n,
                           void *dato)
{
    (void)questo; (void)dato;
    return exjs_booleano(n > 0 && exjs_a_booleano(c, a[0]));
}

/* =============================================================================
 * Math
 *
 * ! NON SI TIRA DENTRO math.h. Il motore gira anche dove quella libreria non
 * c'e', e le sei funzioni che servono davvero — pavimento, soffitto, valore
 * assoluto, radice, potenza intera — stanno in venti righe. Il giorno che
 * servissero seno e coseno si aggiunge la dipendenza allora, per quelle.
 * ========================================================================== */
static double pavimento(double d)
{
    double i = (double)(long long)d;
    return (d < 0 && i != d) ? i - 1.0 : i;
}

static ExJsVal nat_floor(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q; (void)d;
    return exjs_numero(c, pavimento(exjs_a_numero(c, arg_di(a, n, 0))));
}

static ExJsVal nat_ceil(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    double v = exjs_a_numero(c, arg_di(a, n, 0));
    (void)q; (void)d;
    return exjs_numero(c, -pavimento(-v));
}

/* ! round IN JavaScript ARROTONDA .5 VERSO L'ALTO ANCHE PER I NEGATIVI:
 * `Math.round(-0.5)` fa -0, non -1. E' diverso da quasi ogni altra lingua, ed
 * e' scritto nella norma. */
static ExJsVal nat_round(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    double v = exjs_a_numero(c, arg_di(a, n, 0));
    (void)q; (void)d;
    return exjs_numero(c, pavimento(v + 0.5));
}

static ExJsVal nat_abs(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    double v = exjs_a_numero(c, arg_di(a, n, 0));
    (void)q; (void)d;
    return exjs_numero(c, v < 0 ? -v : v);
}

static ExJsVal nat_sqrt(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    double v = exjs_a_numero(c, arg_di(a, n, 0)), x;
    int    i;

    (void)q; (void)d;
    if (v < 0 || v != v) return exjs_numero(c, 0.0/0.0);
    if (v == 0.0) return exjs_numero(c, 0.0);

    /* Newton: venti giri bastano e avanzano per un double, e non serve
     * math.h. */
    x = v;
    for (i = 0; i < 24; i++) x = 0.5 * (x + v / x);
    return exjs_numero(c, x);
}

static ExJsVal nat_pow(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    double b = exjs_a_numero(c, arg_di(a, n, 0));
    double e = exjs_a_numero(c, arg_di(a, n, 1));
    double r = 1.0;
    long   k;

    (void)q; (void)d;
    /* ! SOLO ESPONENTI INTERI, e si dichiara: `Math.pow(2, 0.5)` vorrebbe un
     * logaritmo, e quello vuole math.h. Chi ha bisogno della radice ha
     * Math.sqrt. */
    if (e != pavimento(e)) return exjs_numero(c, 0.0/0.0);

    k = (long)e;
    if (k < 0) { b = 1.0 / b; k = -k; }
    while (k--) r *= b;
    return exjs_numero(c, r);
}

static ExJsVal nat_min(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    double m = 1.0e308 * 10.0;
    int    i;

    (void)q; (void)d;
    for (i = 0; i < n; i++) {
        double v = exjs_a_numero(c, a[i]);
        if (v != v) return exjs_numero(c, 0.0/0.0);
        if (v < m) m = v;
    }
    return exjs_numero(c, m);
}

static ExJsVal nat_max(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    double m = -1.0e308 * 10.0;
    int    i;

    (void)q; (void)d;
    for (i = 0; i < n; i++) {
        double v = exjs_a_numero(c, a[i]);
        if (v != v) return exjs_numero(c, 0.0/0.0);
        if (v > m) m = v;
    }
    return exjs_numero(c, m);
}

static ExJsVal nat_random(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q; (void)a; (void)n; (void)d;
    return exjs_numero(c, exjs_random(c));
}

/* =============================================================================
 * I METODI DELLE STRINGHE
 *
 * ! `questo` E' LA STRINGA, e arriva primitiva. Il ponte lo fa exjs_prendi,
 * che sui valori non-oggetto cerca sul prototipo: vedi val.c.
 * ========================================================================== */
static ExJsVal nat_charAt(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char b[TESTO_MAX], uno[2];
    int  i = arg_intero(c, a, n, 0, 0);

    (void)d;
    copia_val(c, q, b, sizeof(b));
    if (i < 0 || (unsigned int)i >= lung(b)) return exjs_stringa(c, "", -1);
    uno[0] = b[i]; uno[1] = '\0';
    return exjs_stringa(c, uno, -1);
}

static ExJsVal nat_charCodeAt(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char b[TESTO_MAX];
    int  i = arg_intero(c, a, n, 0, 0);

    (void)d;
    copia_val(c, q, b, sizeof(b));
    if (i < 0 || (unsigned int)i >= lung(b)) return exjs_numero(c, 0.0/0.0);
    return exjs_numero(c, (double)(unsigned char)b[i]);
}

/* ! RENDE -1 QUANDO NON C'E', e non `undefined`: mezzo web scrive
 * `if (s.indexOf(x) >= 0)`, e `undefined >= 0` e' falso ma per la ragione
 * sbagliata — funzionerebbe per caso finche' qualcuno non scrive `!= -1`. */
static ExJsVal nat_indexOf_str(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         b[TESTO_MAX], ago[TESTO_MAX];
    unsigned int i, j, lb, la;
    int          da = arg_intero(c, a, n, 1, 0);

    (void)d;
    copia_val(c, q, b, sizeof(b));
    copia_val(c, arg_di(a, n, 0), ago, sizeof(ago));
    lb = lung(b); la = lung(ago);

    if (da < 0) da = 0;
    if (la == 0) return exjs_numero(c, (double)((unsigned int)da <= lb ? da : (int)lb));
    if (la > lb) return exjs_numero(c, -1.0);

    for (i = (unsigned int)da; i + la <= lb; i++) {
        for (j = 0; j < la && b[i + j] == ago[j]; j++) { }
        if (j == la) return exjs_numero(c, (double)i);
    }
    return exjs_numero(c, -1.0);
}

static ExJsVal nat_lastIndexOf_str(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         b[TESTO_MAX], ago[TESTO_MAX];
    unsigned int i, j, lb, la;

    (void)d; (void)n;
    copia_val(c, q, b, sizeof(b));
    copia_val(c, arg_di(a, n, 0), ago, sizeof(ago));
    lb = lung(b); la = lung(ago);

    if (la == 0)  return exjs_numero(c, (double)lb);
    if (la > lb)  return exjs_numero(c, -1.0);

    for (i = lb - la + 1; i > 0; i--) {
        for (j = 0; j < la && b[i - 1 + j] == ago[j]; j++) { }
        if (j == la) return exjs_numero(c, (double)(i - 1));
    }
    return exjs_numero(c, -1.0);
}

/* ! slice E substring NON SONO LA STESSA FUNZIONE, e la differenza si vede coi
 * numeri negativi: `slice(-2)` prende gli ultimi due, `substring(-2)` tratta il
 * -2 come zero. Le pagine usano tutt'e due. */
static ExJsVal taglia(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, int e_slice)
{
    char         b[TESTO_MAX], out[TESTO_MAX];
    unsigned int l, k;
    int          da, fino;

    copia_val(c, q, b, sizeof(b));
    l = lung(b);

    da   = arg_intero(c, a, n, 0, 0);
    fino = (n > 1 && exjs_tipo(c, a[1]) != EXJS_INDEFINITO)
         ? arg_intero(c, a, n, 1, (int)l) : (int)l;

    if (e_slice) {
        if (da   < 0) da   += (int)l;
        if (fino < 0) fino += (int)l;
    }
    if (da   < 0) da   = 0;
    if (fino < 0) fino = 0;
    if (da   > (int)l) da   = (int)l;
    if (fino > (int)l) fino = (int)l;

    /* substring scambia gli estremi se sono al contrario; slice no, e rende
     * la stringa vuota. */
    if (!e_slice && da > fino) { int t = da; da = fino; fino = t; }
    if (da >= fino) return exjs_stringa(c, "", -1);

    for (k = 0; k + (unsigned int)da < (unsigned int)fino && k + 1 < sizeof(out); k++)
        out[k] = b[da + (int)k];
    out[k] = '\0';
    return exjs_stringa(c, out, -1);
}

static ExJsVal nat_slice_str(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{ (void)d; return taglia(c, q, a, n, 1); }

static ExJsVal nat_substring(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{ (void)d; return taglia(c, q, a, n, 0); }

static ExJsVal nat_caso(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         b[TESTO_MAX];
    unsigned int i;
    int          su = (d != 0);

    (void)a; (void)n;
    copia_val(c, q, b, sizeof(b));
    /* ! SOLO ASCII, e si dichiara: cambiare caso a un carattere accentato
     * richiede una tabella Unicode, e quella non sta in questo scaglione.
     * Cambiare i soli byte alti darebbe testo rotto. */
    for (i = 0; b[i]; i++) {
        if (su  && b[i] >= 'a' && b[i] <= 'z') b[i] = (char)(b[i] - 32);
        if (!su && b[i] >= 'A' && b[i] <= 'Z') b[i] = (char)(b[i] + 32);
    }
    return exjs_stringa(c, b, -1);
}

static ExJsVal nat_trim(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         b[TESTO_MAX];
    unsigned int i = 0, f;

    (void)a; (void)n; (void)d;
    copia_val(c, q, b, sizeof(b));
    f = lung(b);
    while (b[i] == ' ' || b[i] == '\t' || b[i] == '\n' || b[i] == '\r') i++;
    while (f > i && (b[f-1]==' '||b[f-1]=='\t'||b[f-1]=='\n'||b[f-1]=='\r')) f--;
    b[f] = '\0';
    return exjs_stringa(c, b + i, -1);
}

static ExJsVal nat_split(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         b[TESTO_MAX], sep[TESTO_MAX], pezzo[TESTO_MAX];
    unsigned int i = 0, k = 0, ls, idx = 0;
    ExJsVal      v = exjs_vettore(c);

    (void)d;
    copia_val(c, q, b, sizeof(b));

    /* ! SENZA SEPARATORE SI RENDE UN VETTORE CON DENTRO LA STRINGA INTERA, non
     * le sue lettere. `"ab".split()` fa ["ab"], `"ab".split("")` fa ["a","b"].
     * Sono due cose diverse e le pagine usano tutt'e due. */
    if (n == 0 || exjs_tipo(c, a[0]) == EXJS_INDEFINITO) {
        exjs_indice_metti(c, v, 0, exjs_stringa(c, b, -1));
        return v;
    }

    copia_val(c, a[0], sep, sizeof(sep));
    ls = lung(sep);

    if (ls == 0) {
        for (i = 0; b[i]; i++) {
            char uno[2];
            uno[0] = b[i]; uno[1] = '\0';
            exjs_indice_metti(c, v, idx++, exjs_stringa(c, uno, -1));
        }
        return v;
    }

    for (i = 0; b[i]; ) {
        unsigned int j;
        for (j = 0; j < ls && b[i + j] == sep[j]; j++) { }
        if (j == ls) {
            pezzo[k] = '\0';
            exjs_indice_metti(c, v, idx++, exjs_stringa(c, pezzo, -1));
            k = 0;
            i += ls;
            continue;
        }
        if (k + 1 < sizeof(pezzo)) pezzo[k++] = b[i];
        i++;
    }
    pezzo[k] = '\0';
    exjs_indice_metti(c, v, idx++, exjs_stringa(c, pezzo, -1));
    return v;
}

/* ! replace SOSTITUISCE LA PRIMA OCCORRENZA E BASTA, come fa JavaScript quando
 * il primo argomento e' una stringa. Sostituirle tutte richiede
 * un'espressione regolare con la `g`, e quelle non ci sono ancora: farlo lo
 * stesso vorrebbe dire un comportamento diverso da ogni altro motore. */
static ExJsVal nat_replace(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         b[TESTO_MAX], ago[TESTO_MAX], nuovo[TESTO_MAX], out[TESTO_MAX];
    unsigned int i, j, k = 0, lb, la;

    (void)d;
    copia_val(c, q, b, sizeof(b));
    copia_val(c, arg_di(a, n, 0), ago, sizeof(ago));
    copia_val(c, arg_di(a, n, 1), nuovo, sizeof(nuovo));
    lb = lung(b); la = lung(ago);

    if (la == 0 || la > lb) return exjs_stringa(c, b, -1);

    for (i = 0; i + la <= lb; i++) {
        for (j = 0; j < la && b[i + j] == ago[j]; j++) { }
        if (j == la) {
            for (j = 0; j < i && k + 1 < sizeof(out); j++)          out[k++] = b[j];
            for (j = 0; nuovo[j] && k + 1 < sizeof(out); j++)       out[k++] = nuovo[j];
            for (j = i + la; b[j] && k + 1 < sizeof(out); j++)      out[k++] = b[j];
            out[k] = '\0';
            return exjs_stringa(c, out, -1);
        }
    }
    return exjs_stringa(c, b, -1);
}

static ExJsVal nat_fromCharCode(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char b[64];
    int  i, k = 0;

    (void)q; (void)d;
    for (i = 0; i < n && k + 1 < (int)sizeof(b); i++) {
        int u = arg_intero(c, a, n, i, 0);
        b[k++] = (char)(u & 0xFF);
    }
    b[k] = '\0';
    return exjs_stringa(c, b, -1);
}

/* =============================================================================
 * I METODI DEI VETTORI
 * ========================================================================== */
static ExJsVal nat_push(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q);
    int          i;

    (void)d;
    for (i = 0; i < n; i++) exjs_indice_metti(c, q, l++, a[i]);
    return exjs_numero(c, (double)l);
}

static ExJsVal nat_pop(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q);
    ExJsVal      v;

    (void)a; (void)n; (void)d;
    if (l == 0) return exjs_indefinito();
    v = exjs_indice_prendi(c, q, l - 1);
    exjs_vettore_tronca(c, q, l - 1);
    return v;
}

static ExJsVal nat_join(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         sep[32], out[TESTO_MAX], pezzo[TESTO_MAX];
    unsigned int l = exjs_lunghezza(c, q), i, k = 0, j;

    (void)d;
    if (n > 0 && exjs_tipo(c, a[0]) != EXJS_INDEFINITO)
        copia_val(c, a[0], sep, sizeof(sep));
    else { sep[0] = ','; sep[1] = '\0'; }

    for (i = 0; i < l; i++) {
        ExJsVal e = exjs_indice_prendi(c, q, i);

        if (i) for (j = 0; sep[j] && k + 1 < sizeof(out); j++) out[k++] = sep[j];
        /* null e undefined diventano stringa vuota: `[1,null,2].join('-')` fa
         * "1--2", non "1-null-2". */
        if (exjs_tipo(c, e) == EXJS_INDEFINITO || exjs_tipo(c, e) == EXJS_NULLO)
            continue;
        copia_val(c, e, pezzo, sizeof(pezzo));
        for (j = 0; pezzo[j] && k + 1 < sizeof(out); j++) out[k++] = pezzo[j];
    }
    out[k] = '\0';
    return exjs_stringa(c, out, -1);
}

static ExJsVal nat_indexOf_vet(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q), i;
    ExJsVal      cercato = arg_di(a, n, 0);

    (void)d;
    /* ! IL CONFRONTO E' STRETTO (`===`), come dice la norma: `[1].indexOf('1')`
     * rende -1. Usare quello largo troverebbe cose che JavaScript non trova. */
    for (i = 0; i < l; i++)
        if (exjs_identici_pub(c, exjs_indice_prendi(c, q, i), cercato))
            return exjs_numero(c, (double)i);
    return exjs_numero(c, -1.0);
}

static ExJsVal nat_reverse(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q), i;

    (void)a; (void)n; (void)d;
    for (i = 0; i < l / 2; i++) {
        ExJsVal x = exjs_indice_prendi(c, q, i);
        ExJsVal y = exjs_indice_prendi(c, q, l - 1 - i);
        exjs_indice_metti(c, q, i, y);
        exjs_indice_metti(c, q, l - 1 - i, x);
    }
    return q;
}

static ExJsVal nat_slice_vet(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q), i, k = 0;
    ExJsVal      v = exjs_vettore(c);
    int          da, fino;

    (void)d;
    da   = arg_intero(c, a, n, 0, 0);
    fino = (n > 1 && exjs_tipo(c, a[1]) != EXJS_INDEFINITO)
         ? arg_intero(c, a, n, 1, (int)l) : (int)l;

    if (da   < 0) da   += (int)l;
    if (fino < 0) fino += (int)l;
    if (da   < 0) da   = 0;
    if (fino > (int)l) fino = (int)l;

    for (i = (unsigned int)da; i < (unsigned int)fino; i++)
        exjs_indice_metti(c, v, k++, exjs_indice_prendi(c, q, i));
    return v;
}

/* =============================================================================
 * forEach, map, filter — le funzioni che CHIAMANO codice JavaScript
 *
 * ! SONO LA PROVA CHE exjs_chiama FUNZIONA DA DENTRO UNA NATIVA, ed e' il
 * meccanismo che servira' a ogni gestore di evento del DOM: codice C che
 * chiama una funzione scritta nella pagina. Il perche' del giro attraverso lo
 * stato d'esecuzione sta accanto a exjs_chiama in run.c.
 * ========================================================================== */
static ExJsVal scorri(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, int modo)
{
    unsigned int l = exjs_lunghezza(c, q), i, k = 0;
    ExJsVal      f = arg_di(a, n, 0);
    ExJsVal      questo = arg_di(a, n, 1);
    ExJsVal      out = (modo == 0) ? exjs_indefinito() : exjs_vettore(c);

    if (exjs_tipo(c, f) != EXJS_FUNZIONE) return out;

    for (i = 0; i < l; i++) {
        ExJsVal arg[3], r;

        arg[0] = exjs_indice_prendi(c, q, i);
        arg[1] = exjs_numero(c, (double)i);
        arg[2] = q;

        r = exjs_chiama(c, f, questo, arg, 3, 0);
        if (exjs_finita(c)) break;

        if (modo == 1) exjs_indice_metti(c, out, k++, r);
        if (modo == 2 && exjs_a_booleano(c, r))
            exjs_indice_metti(c, out, k++, arg[0]);
    }
    return out;
}

static ExJsVal nat_forEach(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{ (void)d; return scorri(c, q, a, n, 0); }
static ExJsVal nat_map(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{ (void)d; return scorri(c, q, a, n, 1); }
static ExJsVal nat_filter(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{ (void)d; return scorri(c, q, a, n, 2); }

/* =============================================================================
 * JSON
 *
 * -----------------------------------------------------------------------------
 * ! JSON NON E' JavaScript, ed e' la prima cosa da tenere ferma.
 *
 * Somiglia a un oggetto letterale e non lo e': le chiavi vogliono le
 * virgolette DOPPIE, le virgolette singole non esistono, la virgola finale e'
 * un errore, i commenti non ci sono, e `undefined` non e' un valore. Un
 * analizzatore che accettasse anche il resto sarebbe piu' comodo e sbagliato:
 * accetterebbe roba che ogni altro sistema rifiuta, e il file che passa qui
 * verrebbe rifiutato altrove.
 *
 * -----------------------------------------------------------------------------
 * ! stringify SI FERMA SUI CICLI, e non e' un caso di scuola.
 *
 * `var o={}; o.io=o;` e' un attimo da scrivere e un attimo da trovarsi in una
 * struttura vera — un nodo che punta al padre, per dirne una. Senza un tetto
 * alla profondita', stringify scenderebbe per sempre e si porterebbe via la
 * pila del C. JavaScript vero solleva un'eccezione; qui, che le eccezioni non
 * ci sono ancora, si rende `undefined` e si smette.
 *
 * -----------------------------------------------------------------------------
 * ! E SI COMPONE IN UN POSTO A PARTE, NON NELL'ARENA.
 *
 * Comporre direttamente nell'arena sarebbe piu' elegante e fragile: durante la
 * discesa si chiama exjs_a_stringa, e per un vettore QUELLA scrive nell'arena.
 * Le due scritture si intreccerebbero e il risultato sarebbe una stringa a
 * pezzi — un guasto che si vede solo con un vettore dentro un oggetto, cioe'
 * tardi. Un vettore di servizio ha un tetto dichiarato e fallisce dicendolo.
 * ========================================================================== */
#define JSON_MAX        4096
#define JSON_PROFONDO   32

typedef struct {
    char        *b;
    unsigned int max, n;
    int          rotto;
} Comp;

static void c_car(Comp *C, char ch)
{
    if (C->n + 1 >= C->max) { C->rotto = 1; return; }
    C->b[C->n++] = ch;
    C->b[C->n] = '\0';
}

static void c_testo(Comp *C, const char *s)
{
    while (*s && !C->rotto) c_car(C, *s++);
}

/* ! LE VIRGOLETTE E LE BARRE VANNO PROTETTE, e i caratteri di controllo pure:
 * un a capo dentro una stringa JSON e' vietato dalla norma, e lasciarcelo
 * produce un file che nessun altro analizzatore accetta. */
static void c_stringa(Comp *C, const char *s)
{
    c_car(C, '"');
    while (*s && !C->rotto) {
        unsigned char ch = (unsigned char)*s++;

        switch (ch) {
        case '"':  c_testo(C, "\\\""); break;
        case '\\': c_testo(C, "\\\\"); break;
        case '\n': c_testo(C, "\\n");  break;
        case '\r': c_testo(C, "\\r");  break;
        case '\t': c_testo(C, "\\t");  break;
        case '\b': c_testo(C, "\\b");  break;
        case '\f': c_testo(C, "\\f");  break;
        default:
            if (ch < 0x20) {
                static const char esa[] = "0123456789abcdef";
                c_testo(C, "\\u00");
                c_car(C, esa[(ch >> 4) & 15]);
                c_car(C, esa[ch & 15]);
            } else {
                c_car(C, (char)ch);
            }
            break;
        }
    }
    c_car(C, '"');
}

static void componi(ExJsCtx *c, Comp *C, ExJsVal v, int profondo)
{
    char tmp[TESTO_MAX];

    if (C->rotto) return;
    if (profondo > JSON_PROFONDO) { C->rotto = 1; return; }

    switch (exjs_tipo(c, v)) {
    case EXJS_NULLO:      c_testo(C, "null");  return;
    /* ! `undefined` E LE FUNZIONI NON SONO VALORI JSON. Dentro un oggetto la
     * voce sparisce, dentro un vettore diventa `null` — perche' li' togliere
     * un elemento cambierebbe gli indici di tutti gli altri. Lo decide chi
     * chiama, qui si scrive `null`. */
    case EXJS_INDEFINITO:
    case EXJS_FUNZIONE:   c_testo(C, "null");  return;
    case EXJS_BOOLEANO:   c_testo(C, exjs_a_booleano(c, v) ? "true" : "false"); return;

    case EXJS_NUMERO: {
        double d = exjs_a_numero(c, v);
        /* ! NaN E Infinity NON ESISTONO IN JSON, e diventano `null`: e' quello
         * che fa JavaScript, e un file con dentro `NaN` non lo rilegge
         * nessuno. */
        if (d != d || d > 1.7e308 || d < -1.7e308) { c_testo(C, "null"); return; }
        copia_val(c, v, tmp, sizeof(tmp));
        c_testo(C, tmp);
        return;
    }

    case EXJS_STRINGA:
        copia_val(c, v, tmp, sizeof(tmp));
        c_stringa(C, tmp);
        return;

    default: break;
    }

    /* Un vettore. */
    {
        int          k = exjs_a_oggetto(v);
        ExJsOggetto *O = exjs_ogg(c, k);

        if (O && O->classe == EXJS_CL_VETTORE) {
            unsigned int i, l = exjs_lunghezza(c, v);

            c_car(C, '[');
            for (i = 0; i < l && !C->rotto; i++) {
                if (i) c_car(C, ',');
                componi(c, C, exjs_indice_prendi(c, v, i), profondo + 1);
            }
            c_car(C, ']');
            return;
        }

        /* Un oggetto: le sue proprieta' PROPRIE, in ordine di dichiarazione.
         * L'elenco e' concatenato al contrario, quindi si rovescia scrivendo
         * a ritroso — le pagine si aspettano l'ordine in cui le chiavi sono
         * state messe. */
        c_car(C, '{');
        {
            int p, elenco[256], quanti = 0, i;

            for (p = exjs_prop_prima(c, k); p >= 0 && quanti < 256;
                 p = exjs_prop_prossima(c, p))
                elenco[quanti++] = p;

            for (i = quanti - 1; i >= 0 && !C->rotto; i--) {
                ExJsVal pv = exjs_prop_val(c, elenco[i]);

                /* Le voci `undefined` e le funzioni SPARISCONO da un oggetto:
                 * e' cio' che fa JavaScript, e ci si conta per non serializzare
                 * i metodi. */
                if (exjs_tipo(c, pv) == EXJS_INDEFINITO ||
                    exjs_tipo(c, pv) == EXJS_FUNZIONE) continue;

                if (i != quanti - 1) c_car(C, ',');
                c_stringa(C, exjs_arena_leggi(c, exjs_prop_nome(c, elenco[i])));
                c_car(C, ':');
                componi(c, C, pv, profondo + 1);
            }
        }
        c_car(C, '}');
    }
}

static ExJsVal nat_stringify(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char b[JSON_MAX];
    Comp C;

    (void)q; (void)d;
    C.b = b; C.max = sizeof(b); C.n = 0; C.rotto = 0;
    b[0] = '\0';

    componi(c, &C, arg_di(a, n, 0), 0);

    /* ! SI RENDE `undefined` QUANDO NON CI SI RIESCE, come fa JavaScript
     * quando gli si passa `undefined` — e non una stringa a meta'. Una stringa
     * troncata verrebbe scritta su disco o mandata in rete come se fosse
     * intera. */
    if (C.rotto) return exjs_indefinito();
    return exjs_stringa(c, b, -1);
}

/* -----------------------------------------------------------------------------
 * JSON.parse
 *
 * ! SI LEGGE DALL'ARENA MENTRE CI SI SCRIVE DENTRO, e va bene per una ragione
 * precisa: l'arena e' un allocatore a spinta — cresce in coda e non sposta mai
 * niente. Il testo da leggere resta dov'e' anche mentre si creano le stringhe
 * e gli oggetti del risultato. Il giorno che l'arena imparasse a compattare,
 * questa funzione andrebbe rivista per prima.
 * --------------------------------------------------------------------------- */
typedef struct {
    const char  *s;
    unsigned int i;
    int          rotto;
} Leg;

static void l_spazi(Leg *L)
{
    while (L->s[L->i] == ' ' || L->s[L->i] == '\t' ||
           L->s[L->i] == '\n' || L->s[L->i] == '\r') L->i++;
}

static ExJsVal l_valore(ExJsCtx *c, Leg *L, int profondo);

static int l_uguale(Leg *L, const char *parola)
{
    unsigned int k;
    for (k = 0; parola[k]; k++)
        if (L->s[L->i + k] != parola[k]) return 0;
    L->i += k;
    return 1;
}

static ExJsVal l_stringa(ExJsCtx *c, Leg *L)
{
    char         b[TESTO_MAX];
    unsigned int k = 0;

    if (L->s[L->i] != '"') { L->rotto = 1; return exjs_indefinito(); }
    L->i++;

    while (L->s[L->i] && L->s[L->i] != '"') {
        char ch = L->s[L->i++];

        if (ch == '\\') {
            char e = L->s[L->i++];
            switch (e) {
            case 'n': ch = '\n'; break;
            case 't': ch = '\t'; break;
            case 'r': ch = '\r'; break;
            case 'b': ch = '\b'; break;
            case 'f': ch = '\f'; break;
            case '"': case '\\': case '/': ch = e; break;
            case 'u': {
                unsigned int u = 0, j;
                for (j = 0; j < 4; j++) {
                    char h = L->s[L->i++];
                    int  d;
                    if (h >= '0' && h <= '9')                d = h - '0';
                    else if ((h|0x20) >= 'a' && (h|0x20) <= 'f') d = (h|0x20) - 'a' + 10;
                    else { L->rotto = 1; return exjs_indefinito(); }
                    u = u * 16 + (unsigned int)d;
                }
                /* In UTF-8, come fa il lessicale: e' quello che il resto del
                 * sistema legge. */
                if (u < 0x80) { if (k + 1 < sizeof(b)) b[k++] = (char)u; }
                else if (u < 0x800) {
                    if (k + 2 < sizeof(b)) {
                        b[k++] = (char)(0xC0 | (u >> 6));
                        b[k++] = (char)(0x80 | (u & 0x3F));
                    }
                } else if (k + 3 < sizeof(b)) {
                    b[k++] = (char)(0xE0 | (u >> 12));
                    b[k++] = (char)(0x80 | ((u >> 6) & 0x3F));
                    b[k++] = (char)(0x80 | (u & 0x3F));
                }
                continue;
            }
            default: L->rotto = 1; return exjs_indefinito();
            }
        }
        if (k + 1 < sizeof(b)) b[k++] = ch;
    }

    if (L->s[L->i] != '"') { L->rotto = 1; return exjs_indefinito(); }
    L->i++;
    b[k] = '\0';
    return exjs_stringa(c, b, -1);
}

static ExJsVal l_numero(ExJsCtx *c, Leg *L)
{
    char         b[64];
    unsigned int k = 0;

    if (L->s[L->i] == '-') b[k++] = L->s[L->i++];
    if (L->s[L->i] < '0' || L->s[L->i] > '9') { L->rotto = 1; return exjs_indefinito(); }

    while (k + 1 < sizeof(b) &&
           ((L->s[L->i] >= '0' && L->s[L->i] <= '9') ||
            L->s[L->i] == '.' || L->s[L->i] == 'e' || L->s[L->i] == 'E' ||
            ((L->s[L->i] == '+' || L->s[L->i] == '-') &&
             (L->s[L->i-1] == 'e' || L->s[L->i-1] == 'E'))))
        b[k++] = L->s[L->i++];
    b[k] = '\0';

    return exjs_numero(c, exjs_a_numero(c, exjs_stringa(c, b, -1)));
}

static ExJsVal l_valore(ExJsCtx *c, Leg *L, int profondo)
{
    l_spazi(L);
    if (L->rotto || profondo > JSON_PROFONDO) { L->rotto = 1; return exjs_indefinito(); }

    switch (L->s[L->i]) {
    case '"': return l_stringa(c, L);

    case '{': {
        ExJsVal o = exjs_oggetto(c);

        L->i++;
        l_spazi(L);
        if (L->s[L->i] == '}') { L->i++; return o; }

        for (;;) {
            char    nome[TESTO_MAX];
            ExJsVal chiave, v;

            l_spazi(L);
            /* ! LA CHIAVE VUOLE LE VIRGOLETTE. In JavaScript `{a:1}` e'
             * legale, in JSON no — e accettarlo qui vorrebbe dire produrre e
             * accettare file che nessun altro legge. */
            if (L->s[L->i] != '"') { L->rotto = 1; return exjs_indefinito(); }
            chiave = l_stringa(c, L);
            if (L->rotto) return exjs_indefinito();
            copia_val(c, chiave, nome, sizeof(nome));

            l_spazi(L);
            if (L->s[L->i] != ':') { L->rotto = 1; return exjs_indefinito(); }
            L->i++;

            v = l_valore(c, L, profondo + 1);
            if (L->rotto) return exjs_indefinito();
            exjs_metti(c, o, nome, v);

            l_spazi(L);
            if (L->s[L->i] == ',') { L->i++; continue; }
            if (L->s[L->i] == '}') { L->i++; return o; }
            L->rotto = 1;
            return exjs_indefinito();
        }
    }

    case '[': {
        ExJsVal      v = exjs_vettore(c);
        unsigned int k = 0;

        L->i++;
        l_spazi(L);
        if (L->s[L->i] == ']') { L->i++; return v; }

        for (;;) {
            ExJsVal e = l_valore(c, L, profondo + 1);

            if (L->rotto) return exjs_indefinito();
            exjs_indice_metti(c, v, k++, e);

            l_spazi(L);
            if (L->s[L->i] == ',') { L->i++; continue; }
            if (L->s[L->i] == ']') { L->i++; return v; }
            L->rotto = 1;
            return exjs_indefinito();
        }
    }

    case 't': if (l_uguale(L, "true"))  return exjs_booleano(1); break;
    case 'f': if (l_uguale(L, "false")) return exjs_booleano(0); break;
    case 'n': if (l_uguale(L, "null"))  return exjs_nullo();     break;
    default:  return l_numero(c, L);
    }

    L->rotto = 1;
    return exjs_indefinito();
}

static ExJsVal nat_parse(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    Leg     L;
    ExJsVal v, testo = arg_di(a, n, 0);
    char    copia[JSON_MAX];

    (void)q; (void)d;

    /* Se non e' gia' una stringa lo diventa, e la copia serve: exjs_a_stringa
     * su un numero rende il posto di servizio, che la prima creazione di valore
     * riscriverebbe. */
    if (exjs_tipo(c, testo) == EXJS_STRINGA) {
        L.s = exjs_a_stringa(c, testo);     /* nell'arena: non si sposta */
    } else {
        copia_val(c, testo, copia, sizeof(copia));
        L.s = copia;
    }
    L.i = 0;
    L.rotto = 0;

    v = l_valore(c, &L, 0);
    l_spazi(&L);

    /* ! CIO' CHE AVANZA DOPO IL VALORE E' UN ERRORE. `{"a":1} spazzatura` non
     * e' JSON valido, e accettarlo vorrebbe dire leggere meta' di un file
     * corrotto credendo di averlo letto tutto. */
    if (L.rotto || L.s[L.i] != '\0') return exjs_indefinito();
    return v;
}

/* =============================================================================
 * LA REGISTRAZIONE
 *
 * ! SI FA UNA VOLTA SOLA, e run.c la chiama a ogni exjs_esegui: la bandiera sta
 * qui perche' e' un fatto di questo file, non di chi lo usa.
 * ========================================================================== */
static void metti_nat(ExJsCtx *c, ExJsVal dove, const char *nome,
                      ExJsNativa f, void *dato)
{
    exjs_metti(c, dove, nome, exjs_nativa(c, f, dato, nome));
}

static void metti_nat_ogg(ExJsCtx *c, int ogg, const char *nome,
                          ExJsNativa f, void *dato)
{
    metti_nat(c, exjs_da_oggetto(ogg), nome, f, dato);
}

void exjs_base_registra(ExJsCtx *c)
{
    ExJsVal g, math;

    if (exjs_base_gia_fatta(c)) return;
    exjs_base_segna(c);

    g = exjs_globale(c);

    metti_nat(c, g, "parseInt",   nat_parseint,   0);
    metti_nat(c, g, "parseFloat", nat_parsefloat, 0);
    metti_nat(c, g, "isNaN",      nat_isnan,      0);
    metti_nat(c, g, "isFinite",   nat_isfinite,   0);
    metti_nat(c, g, "String",     nat_String,     0);
    metti_nat(c, g, "Number",     nat_Number,     0);
    metti_nat(c, g, "Boolean",    nat_Boolean,    0);

    /* console.log, e `console` e' un oggetto perche' cosi' lo scrivono tutti. */
    {
        ExJsVal console = exjs_oggetto(c);
        metti_nat(c, console, "log", nat_log, 0);
        exjs_metti(c, g, "console", console);
    }

    math = exjs_oggetto(c);
    exjs_metti(c, g, "Math", math);
    metti_nat(c, math, "floor",  nat_floor,  0);
    metti_nat(c, math, "ceil",   nat_ceil,   0);
    metti_nat(c, math, "round",  nat_round,  0);
    metti_nat(c, math, "abs",    nat_abs,    0);
    metti_nat(c, math, "sqrt",   nat_sqrt,   0);
    metti_nat(c, math, "pow",    nat_pow,    0);
    metti_nat(c, math, "min",    nat_min,    0);
    metti_nat(c, math, "max",    nat_max,    0);
    metti_nat(c, math, "random", nat_random, 0);
    exjs_metti(c, math, "PI", exjs_numero(c, 3.14159265358979323846));
    exjs_metti(c, math, "E",  exjs_numero(c, 2.71828182845904523536));

    {
        ExJsVal json = exjs_oggetto(c);
        exjs_metti(c, g, "JSON", json);
        metti_nat(c, json, "stringify", nat_stringify, 0);
        metti_nat(c, json, "parse",     nat_parse,     0);
    }

    /* `String.fromCharCode` sta sulla FUNZIONE String, non sul prototipo: e'
     * un metodo del costruttore, e chi lo cerca lo cerca li'. */
    {
        ExJsVal S = exjs_prendi(c, g, "String");
        metti_nat(c, S, "fromCharCode", nat_fromCharCode, 0);
    }

    {
        int ps = exjs_proto_str(c);
        metti_nat_ogg(c, ps, "charAt",      nat_charAt,        0);
        metti_nat_ogg(c, ps, "charCodeAt",  nat_charCodeAt,    0);
        metti_nat_ogg(c, ps, "indexOf",     nat_indexOf_str,   0);
        metti_nat_ogg(c, ps, "lastIndexOf", nat_lastIndexOf_str, 0);
        metti_nat_ogg(c, ps, "slice",       nat_slice_str,     0);
        metti_nat_ogg(c, ps, "substring",   nat_substring,     0);
        metti_nat_ogg(c, ps, "toUpperCase", nat_caso,          (void *)1);
        metti_nat_ogg(c, ps, "toLowerCase", nat_caso,          (void *)0);
        metti_nat_ogg(c, ps, "trim",        nat_trim,          0);
        metti_nat_ogg(c, ps, "split",       nat_split,         0);
        metti_nat_ogg(c, ps, "replace",     nat_replace,       0);
    }

    {
        int pv = exjs_proto_vet(c);
        metti_nat_ogg(c, pv, "push",     nat_push,        0);
        metti_nat_ogg(c, pv, "pop",      nat_pop,         0);
        metti_nat_ogg(c, pv, "join",     nat_join,        0);
        metti_nat_ogg(c, pv, "indexOf",  nat_indexOf_vet, 0);
        metti_nat_ogg(c, pv, "reverse",  nat_reverse,     0);
        metti_nat_ogg(c, pv, "slice",    nat_slice_vet,   0);
        metti_nat_ogg(c, pv, "forEach",  nat_forEach,     0);
        metti_nat_ogg(c, pv, "map",      nat_map,         0);
        metti_nat_ogg(c, pv, "filter",   nat_filter,      0);
    }
}

/* =============================================================================
 * QUELLO CHE NON C'E', DICHIARATO
 *
 *   Array.sort                    vuole un confronto che chiama JavaScript;
 *                                 il meccanismo c'e' gia' (vedi scorri), manca
 *                                 solo l'ordinamento
 *   Date                          vuole l'orologio, e l'orologio non sta in
 *                                 questa libreria: arrivera' come nativa
 *                                 registrata da chi ospita, come setTimeout
 *   RegExp                        e con lui replace globale e split per
 *                                 espressione: e' uno scaglione suo
 *   toFixed, toString(base)       poco usati fuori dai numeri formattati
 * ========================================================================== */
