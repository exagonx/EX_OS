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

/* =============================================================================
 * ! UNA STRINGA NON SI COPIA: SI PUNTA — e dietro c'e' un difetto vero,
 * trovato aprendo una pagina da settemila byte.
 *
 * Il commento di `copia_val` qui sopra dice che «una copia troncata darebbe un
 * risultato sbagliato invece di un errore». Aveva ragione, e nessuno dei suoi
 * chiamanti guardava quel che rendeva: ogni metodo di String copiava il
 * soggetto in mezzo kilobyte e lavorava sui primi 511 caratteri.
 *
 *     pagina.indexOf('riquadro')   ->  -1, su un testo che ce l'ha
 *
 * Nessun errore, nessun avviso, la risposta sbagliata. Si e' visto quando
 * XMLHttpRequest ha cominciato a consegnare documenti interi agli script:
 * prima, di stringhe lunghe in giro ce n'erano poche.
 *
 * ! LA CURA E' NON COPIARE. `exjs_a_stringa` rende un posto di SERVIZIO solo
 * per quel che stringa non e' — i numeri, che sono corti — e per una stringa
 * vera rende il suo posto nell'arena, che non si muove piu': l'arena cresce in
 * coda e non si ricompatta mai. Puntarla costa zero e non ha tetti.
 *
 * ! IL POSTO DI SERVIZIO SI RIEMPIE DA CAPO A OGNI CHIAMATA, e questa e' la
 * trappola che resta: chi ne maneggia DUE, e almeno una non e' una stringa,
 * deve comunque copiarne una. Percio' un buffer serve lo stesso, e si usa solo
 * quando serve davvero.
 * ========================================================================== */
static const char *testo_di(ExJsCtx *c, ExJsVal v, char *dst, unsigned int max)
{
    if (exjs_tipo(c, v) == EXJS_STRINGA) return exjs_a_stringa(c, v);
    copia_val(c, v, dst, max);
    return dst;
}

/* =============================================================================
 * IL FILO — una stringa costruita a pezzi dentro l'arena, senza un tetto suo
 *
 * ! SERVE A CHI PRODUCE, non a chi legge. Un `replace` su un documento intero
 * rende un documento intero: scriverlo in un buffer da mezzo kilobyte vuol
 * dire renderne mezzo. Il meccanismo sta in val.c, dove sta l'arena; qui c'e'
 * solo il modo comodo di chiamarlo.
 *
 * ! MENTRE UN FILO E' APERTO NON SI CREANO ALTRE STRINGHE. La prossima
 * scriverebbe in coda all'arena, cioe' in mezzo a questa. Chi apre un filo
 * prepara PRIMA tutto quel che gli serve — ed e' il motivo per cui `replace`
 * copia l'ago e il ricambio prima di aprirlo.
 * ========================================================================== */
typedef struct { ExJsCtx *c; unsigned int off; } Filo;

static void filo_apri(Filo *f, ExJsCtx *c)
{
    f->c   = c;
    f->off = exjs_arena_apri(c);
}

static void filo_mette(Filo *f, const char *s, unsigned int n)
{
    if (f->off != EXJS_FILO_NO && !exjs_arena_aggiungi(f->c, s, n))
        f->off = EXJS_FILO_NO;
}

static ExJsVal filo_chiudi(Filo *f)
{
    return exjs_arena_chiudi(f->c, f->off);
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
        char        tmp[TESTO_MAX];
        const char *s = testo_di(c, a[i], tmp, sizeof(tmp));

        if (i) exjs_uscita_scrivi(c, " ", 1);
        exjs_uscita_scrivi(c, s, lung(s));
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
    char tmp[TESTO_MAX];
    (void)questo; (void)dato;
    if (n == 0) return exjs_stringa(c, "", -1);
    /* Una stringa e' gia' una stringa: renderla com'e' evita di ricopiarla
     * nell'arena e, soprattutto, di troncarla. */
    if (exjs_tipo(c, a[0]) == EXJS_STRINGA) return a[0];
    return exjs_stringa(c, testo_di(c, a[0], tmp, sizeof(tmp)), -1);
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
    char        tmp[TESTO_MAX], uno[2];
    const char *b = testo_di(c, q, tmp, sizeof(tmp));
    int         i = arg_intero(c, a, n, 0, 0);

    (void)d;
    if (i < 0 || (unsigned int)i >= lung(b)) return exjs_stringa(c, "", -1);
    uno[0] = b[i]; uno[1] = '\0';
    return exjs_stringa(c, uno, -1);
}

static ExJsVal nat_charCodeAt(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char        tmp[TESTO_MAX];
    const char *b = testo_di(c, q, tmp, sizeof(tmp));
    int         i = arg_intero(c, a, n, 0, 0);

    (void)d;
    if (i < 0 || (unsigned int)i >= lung(b)) return exjs_numero(c, 0.0/0.0);
    return exjs_numero(c, (double)(unsigned char)b[i]);
}

/* ! RENDE -1 QUANDO NON C'E', e non `undefined`: mezzo web scrive
 * `if (s.indexOf(x) >= 0)`, e `undefined >= 0` e' falso ma per la ragione
 * sbagliata — funzionerebbe per caso finche' qualcuno non scrive `!= -1`. */
static ExJsVal nat_indexOf_str(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         tmp[TESTO_MAX], ago[TESTO_MAX];
    const char  *b;
    unsigned int i, j, lb, la;
    int          da = arg_intero(c, a, n, 1, 0);

    (void)d;
    /* ! L'AGO SI COPIA E IL PAGLIAIO NO: dei due, quello che puo' essere
     * lungo davvero e' il secondo. E se l'ago non ci sta si rende -1 invece
     * di cercarne uno accorciato — un ago troncato TROVEREBBE, e troverebbe
     * il posto sbagliato. */
    if (!copia_val(c, arg_di(a, n, 0), ago, sizeof(ago)))
        return exjs_numero(c, -1.0);
    b = testo_di(c, q, tmp, sizeof(tmp));
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
    char         tmp[TESTO_MAX], ago[TESTO_MAX];
    const char  *b;
    unsigned int i, j, lb, la;

    (void)d; (void)n;
    if (!copia_val(c, arg_di(a, n, 0), ago, sizeof(ago)))
        return exjs_numero(c, -1.0);
    b = testo_di(c, q, tmp, sizeof(tmp));
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
    char         tmp[TESTO_MAX];
    const char  *b = testo_di(c, q, tmp, sizeof(tmp));
    unsigned int l;
    int          da, fino;

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

    /* ! IL PEZZO SI RENDE DALLA STRINGA DI PARTENZA, con la sua lunghezza:
     * exjs_stringa la prende, e cosi' non c'e' nessun buffer d'uscita da
     * scegliere — cioe' nessuna misura oltre la quale `slice` mentirebbe. */
    return exjs_stringa(c, b + da, fino - da);
}

static ExJsVal nat_slice_str(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{ (void)d; return taglia(c, q, a, n, 1); }

static ExJsVal nat_substring(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{ (void)d; return taglia(c, q, a, n, 0); }

static ExJsVal nat_caso(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         tmp[TESTO_MAX];
    const char  *b = testo_di(c, q, tmp, sizeof(tmp));
    unsigned int i, l = lung(b);
    int          su = (d != 0);
    Filo         f;

    (void)a; (void)n;
    /* ! IL RISULTATO SI COSTRUISCE, NON SI MODIFICA IL SOGGETTO. Prima si
     * cambiava caso dentro la copia e si rendeva quella; adesso il soggetto
     * e' la stringa VERA di chi ha chiamato — nell'arena — e scriverci sopra
     * vorrebbe dire cambiare la sua variabile. */
    filo_apri(&f, c);
    for (i = 0; i < l; i++) {
        /* ! SOLO ASCII, e si dichiara: cambiare caso a un carattere accentato
         * richiede una tabella Unicode, e quella non sta in questo scaglione.
         * Cambiare i soli byte alti darebbe testo rotto. */
        char ch = b[i];

        if (su  && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
        if (!su && ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
        filo_mette(&f, &ch, 1);
    }
    return filo_chiudi(&f);
}

static ExJsVal nat_trim(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         tmp[TESTO_MAX];
    const char  *b;
    unsigned int i = 0, f;

    (void)a; (void)n; (void)d;
    b = testo_di(c, q, tmp, sizeof(tmp));
    f = lung(b);
    while (b[i] == ' ' || b[i] == '\t' || b[i] == '\n' || b[i] == '\r') i++;
    while (f > i && (b[f-1]==' '||b[f-1]=='\t'||b[f-1]=='\n'||b[f-1]=='\r')) f--;
    /* Non si tocca la stringa di partenza — potrebbe essere quella di
     * qualcun altro: si rende il tratto che resta, con la sua lunghezza. */
    return exjs_stringa(c, b + i, (int)(f - i));
}

static ExJsVal nat_split(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         tmp[TESTO_MAX], sep[TESTO_MAX];
    const char  *b;
    unsigned int i = 0, inizio = 0, ls, idx = 0;
    ExJsVal      v = exjs_vettore(c);

    (void)d;
    b = testo_di(c, q, tmp, sizeof(tmp));

    /* ! SENZA SEPARATORE SI RENDE UN VETTORE CON DENTRO LA STRINGA INTERA, non
     * le sue lettere. `"ab".split()` fa ["ab"], `"ab".split("")` fa ["a","b"].
     * Sono due cose diverse e le pagine usano tutt'e due. */
    if (n == 0 || exjs_tipo(c, a[0]) == EXJS_INDEFINITO) {
        exjs_indice_metti(c, v, 0, exjs_stringa(c, b, -1));
        return v;
    }

    /* ! UN SEPARATORE CHE NON CI STA NON SEPARA NIENTE, e si rende la stringa
     * intera: un separatore troncato taglierebbe nei posti sbagliati. */
    if (!copia_val(c, a[0], sep, sizeof(sep))) {
        exjs_indice_metti(c, v, 0, exjs_stringa(c, b, -1));
        return v;
    }
    ls = lung(sep);

    if (ls == 0) {
        for (i = 0; b[i]; i++) {
            char uno[2];
            uno[0] = b[i]; uno[1] = '\0';
            exjs_indice_metti(c, v, idx++, exjs_stringa(c, uno, -1));
        }
        return v;
    }

    /* ! I PEZZI SI RENDONO DALLA STRINGA DI PARTENZA, con la loro lunghezza,
     * invece di ricopiarli in un buffer: cosi' non c'e' una misura oltre la
     * quale un pezzo si accorcia da solo. Ogni exjs_stringa qui appende
     * all'arena, e il puntatore `b` resta valido perche' l'arena cresce in
     * coda e non si ricompatta mai. */
    for (i = 0; b[i]; ) {
        unsigned int j;

        for (j = 0; j < ls && b[i + j] == sep[j]; j++) { }
        if (j == ls) {
            exjs_indice_metti(c, v, idx++,
                              exjs_stringa(c, b + inizio, (int)(i - inizio)));
            i += ls;
            inizio = i;
            continue;
        }
        i++;
    }
    exjs_indice_metti(c, v, idx++,
                      exjs_stringa(c, b + inizio, (int)(i - inizio)));
    return v;
}

/* ! replace SOSTITUISCE LA PRIMA OCCORRENZA E BASTA, come fa JavaScript quando
 * il primo argomento e' una stringa. Sostituirle tutte richiede
 * un'espressione regolare con la `g`, e quelle non ci sono ancora: farlo lo
 * stesso vorrebbe dire un comportamento diverso da ogni altro motore. */
static ExJsVal nat_replace(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char         tmp[TESTO_MAX], ago[TESTO_MAX], nuovo[TESTO_MAX];
    const char  *b;
    unsigned int i, j, lb, la, ln;

    (void)d;
    /* ! L'AGO E IL RICAMBIO SI COPIANO PRIMA DI APRIRE IL FILO, e non e' un
     * dettaglio: dentro un filo non si possono creare altre stringhe, e
     * `exjs_a_stringa` su un numero riscrive il posto di servizio. Se non ci
     * stanno non si sostituisce niente — un ago troncato colpirebbe nel posto
     * sbagliato, e un ricambio troncato lascerebbe un testo a meta'. */
    if (!copia_val(c, arg_di(a, n, 0), ago, sizeof(ago)) ||
        !copia_val(c, arg_di(a, n, 1), nuovo, sizeof(nuovo)))
        return q;
    b = testo_di(c, q, tmp, sizeof(tmp));
    lb = lung(b); la = lung(ago); ln = lung(nuovo);

    if (la == 0 || la > lb) return exjs_stringa(c, b, -1);

    for (i = 0; i + la <= lb; i++) {
        for (j = 0; j < la && b[i + j] == ago[j]; j++) { }
        if (j == la) {
            Filo f;

            filo_apri(&f, c);
            filo_mette(&f, b, i);
            filo_mette(&f, nuovo, ln);
            filo_mette(&f, b + i + la, lb - i - la);
            return filo_chiudi(&f);
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

/* ! join NON HA PIU' TETTI (@NAVMETA, 28 settembre 2026): prima componeva in
 * mezzo kilobyte, e un separatore oltre i 31 caratteri si tagliava. Adesso e'
 * exjs_vettore_testo_sep, in val.c, in due passate — vedi li' perche'. */
static ExJsVal nat_join(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal     sep;
    const char *t;

    (void)d;
    if (n > 0 && exjs_tipo(c, a[0]) != EXJS_INDEFINITO) {
        /* il separatore nell'arena, dove non si muove: il posto di servizio
         * lo riscriverebbe la prima conversione di un numero */
        sep = (exjs_tipo(c, a[0]) == EXJS_STRINGA) ? a[0]
                                                   : exjs_stringa(c, exjs_a_stringa(c, a[0]), -1);
    } else {
        sep = exjs_stringa(c, ",", 1);
    }
    t = exjs_vettore_testo_sep(c, q, exjs_a_stringa(c, sep));
    return exjs_stringa(c, t, -1);
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

static ExJsVal nat_lastIndexOf_vet(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q), i;
    ExJsVal      cercato = arg_di(a, n, 0);

    (void)d;
    for (i = l; i > 0; i--)
        if (exjs_identici_pub(c, exjs_indice_prendi(c, q, i - 1), cercato))
            return exjs_numero(c, (double)(i - 1));
    return exjs_numero(c, -1.0);
}

/* ! shift E unshift COSTANO UN GIRO INTERO, e non c'e' modo di evitarlo con
 * elementi densi: togliere il primo vuol dire spostare tutti gli altri. Le
 * pagine li usano lo stesso, e su vettori di poche decine non si sente. */
static ExJsVal nat_shift(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q), i;
    ExJsVal      primo;

    (void)a; (void)n; (void)d;
    if (l == 0) return exjs_indefinito();

    primo = exjs_indice_prendi(c, q, 0);
    for (i = 1; i < l; i++)
        exjs_indice_metti(c, q, i - 1, exjs_indice_prendi(c, q, i));
    exjs_vettore_tronca(c, q, l - 1);
    return primo;
}

static ExJsVal nat_unshift(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q), i;
    int          k;

    (void)d;
    if (n <= 0) return exjs_numero(c, (double)l);

    /* All'indietro, o si sovrascriverebbero gli elementi non ancora spostati. */
    for (i = l; i > 0; i--)
        exjs_indice_metti(c, q, i - 1 + (unsigned int)n,
                          exjs_indice_prendi(c, q, i - 1));
    for (k = 0; k < n; k++) exjs_indice_metti(c, q, (unsigned int)k, a[k]);
    return exjs_numero(c, (double)(l + (unsigned int)n));
}

static ExJsVal nat_concat(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    ExJsVal      v = exjs_vettore(c);
    unsigned int k = 0, i, l = exjs_lunghezza(c, q);
    int          j;

    (void)d;
    for (i = 0; i < l; i++) exjs_indice_metti(c, v, k++, exjs_indice_prendi(c, q, i));

    for (j = 0; j < n; j++) {
        /* ! UN VETTORE PASSATO A concat SI APRE, un valore qualunque no:
         * `[1].concat([2,3])` fa [1,2,3] e `[1].concat(2)` fa [1,2]. E' una
         * differenza che le pagine usano, non un caso limite. */
        int          o = exjs_a_oggetto(a[j]);
        ExJsOggetto *O = exjs_ogg(c, o);

        if (O && O->classe == EXJS_CL_VETTORE) {
            unsigned int m = exjs_lunghezza(c, a[j]), t;
            for (t = 0; t < m; t++)
                exjs_indice_metti(c, v, k++, exjs_indice_prendi(c, a[j], t));
        } else {
            exjs_indice_metti(c, v, k++, a[j]);
        }
    }
    return v;
}

/* =============================================================================
 * sort
 *
 * ! SENZA CONFRONTO SI ORDINA COME TESTO, ANCHE I NUMERI, ed e' la sorpresa
 * piu' famosa di JavaScript: `[10,9,1].sort()` da' [1,10,9], perche' "10" viene
 * prima di "9". Sembra un difetto e non lo e': e' quello che dice la norma, e
 * un motore che ordinasse i numeri per valore darebbe risultati diversi da
 * ogni altro proprio sul codice che si fida del comportamento noto.
 *
 * ! `undefined` VA IN FONDO SEMPRE, e non passa dal confronto: e' l'unica
 * eccezione scritta nella norma, ed esiste perche' un confronto scritto da chi
 * usa il motore non se lo aspetterebbe mai.
 *
 * ! E L'ORDINAMENTO E' A INSERZIONE, non un quicksort. Due ragioni: i vettori
 * di una pagina sono corti — decine, non milioni — e soprattutto il confronto
 * puo' essere JavaScript, quindi ogni paragone costa mille volte piu' del
 * riordino. Con un confronto cosi' caro, il numero di CONFRONTI e' l'unica
 * cosa che conta, e a inserzione su dati quasi ordinati e' il minimo possibile.
 * ========================================================================== */
static int precede(ExJsCtx *c, ExJsVal x, ExJsVal y, ExJsVal f)
{
    if (exjs_tipo(c, f) == EXJS_FUNZIONE) {
        ExJsVal arg[2], r;

        arg[0] = x; arg[1] = y;
        r = exjs_chiama(c, f, exjs_indefinito(), arg, 2, 0);
        return exjs_a_numero(c, r) <= 0.0;
    }
    {
        char         tmp[TESTO_MAX];
        const char  *b, *sy;
        unsigned int i;

        /* ! IL PRIMO SI PUNTA SE E' UNA STRINGA, il secondo si legge dopo: se
         * si copiassero tutt'e due in mezzo kilobyte, due testi lunghi che
         * differiscono oltre il 511esimo carattere risulterebbero uguali. */
        b  = testo_di(c, x, tmp, sizeof(tmp));
        sy = exjs_a_stringa(c, y);
        for (i = 0; b[i] && b[i] == sy[i]; i++) { }
        return (int)(unsigned char)b[i] <= (int)(unsigned char)sy[i];
    }
}

static ExJsVal nat_sort(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    unsigned int l = exjs_lunghezza(c, q), i, j, fine;
    ExJsVal      f = arg_di(a, n, 0);

    (void)d;

    /* Prima gli `undefined` in fondo, e poi non li si tocca piu'. */
    fine = l;
    for (i = 0; i < fine; ) {
        if (exjs_tipo(c, exjs_indice_prendi(c, q, i)) == EXJS_INDEFINITO) {
            for (j = i; j + 1 < fine; j++)
                exjs_indice_metti(c, q, j, exjs_indice_prendi(c, q, j + 1));
            exjs_indice_metti(c, q, fine - 1, exjs_indefinito());
            fine--;
            continue;
        }
        i++;
    }

    for (i = 1; i < fine; i++) {
        ExJsVal x = exjs_indice_prendi(c, q, i);

        j = i;
        while (j > 0 && !precede(c, exjs_indice_prendi(c, q, j - 1), x, f)) {
            exjs_indice_metti(c, q, j, exjs_indice_prendi(c, q, j - 1));
            j--;
            if (exjs_finita(c)) return q;
        }
        exjs_indice_metti(c, q, j, x);
    }
    /* ! RENDE LO STESSO VETTORE, non una copia: `v.sort()` riordina `v`, e chi
     * si aspettasse una copia troverebbe l'originale gia' cambiato. */
    return q;
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
/* ! JSON_MAX NON E' PIU' IL TETTO DEL RISULTATO, e la differenza conta: quello
 * adesso e' l'arena del motore, perche' JSON.stringify scrive li'. Resta il
 * tetto di una sola cosa — la COPIA del testo da leggere, quando a JSON.parse
 * si passa qualcosa che non e' gia' una stringa (un numero, un oggetto). Se e'
 * gia' una stringa non si copia niente e non c'e' tetto. */
#define JSON_MAX        4096
#define JSON_PROFONDO   32

/* ! IL RISULTATO SI SCRIVE NELL'ARENA, non in un buffer da quattro kilobyte.
 * Prima JSON.stringify di un oggetto piu' grande di quello rendeva
 * `undefined` — che almeno era onesto — ma una stringa lunga dentro un oggetto
 * piccolo veniva TRONCATA a 511 caratteri e il JSON usciva valido e sbagliato.
 *
 * ! E MENTRE IL FILO E' APERTO NON SI CREANO STRINGHE: componi() legge e
 * basta — exjs_a_stringa su una stringa rende il suo posto nell'arena, su un
 * numero il posto di servizio, e i nomi delle proprieta' si leggono
 * dall'arena. Nessuna di queste cose alloca. */
typedef struct {
    Filo f;
    int  rotto;
} Comp;

static void c_car(Comp *C, char ch)
{
    filo_mette(&C->f, &ch, 1);
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

static int  data_tempo(ExJsCtx *c, ExJsVal v, double *t);
static void data_testo(double t, int modo, char *o);

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
        /* Una stringa sta gia' nell'arena: puntarla invece di copiarla e'
         * anche l'unico modo perche' JSON.stringify di un testo lungo non
         * renda un JSON tagliato a meta'. */
        c_stringa(C, exjs_a_stringa(c, v));
        return;

    default: break;
    }

    /* Una Date si scrive col suo toJSON: il testo ISO, o null se non e'
     * valida. */
    {
        double t;
        if (data_tempo(c, v, &t)) {
            if (t != t) { c_testo(C, "null"); return; }
            data_testo(t, 1, tmp);
            c_stringa(C, tmp);
            return;
        }
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

            int scritte = 0;

            for (i = quanti - 1; i >= 0 && !C->rotto; i--) {
                ExJsVal     pv = exjs_prop_val(c, elenco[i]);
                const char *nome = exjs_arena_leggi(c, exjs_prop_nome(c, elenco[i]));

                if (nome[0] == '\001') continue;         /* nascosta: Date */

                /* Le voci `undefined` e le funzioni SPARISCONO da un oggetto:
                 * e' cio' che fa JavaScript, e ci si conta per non serializzare
                 * i metodi. */
                if (exjs_tipo(c, pv) == EXJS_INDEFINITO ||
                    exjs_tipo(c, pv) == EXJS_FUNZIONE) continue;

                /* ! LA VIRGOLA SI CONTA SU QUELLE SCRITTE, non sull'indice:
                 * con la prima voce saltata usciva `{,"b":1}`, che nessuno
                 * rilegge. */
                if (scritte++) c_car(C, ',');
                c_stringa(C, nome);
                c_car(C, ':');
                componi(c, C, pv, profondo + 1);
            }
        }
        c_car(C, '}');
    }
}

static ExJsVal nat_stringify(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    Comp C;

    (void)q; (void)d;
    C.rotto = 0;
    filo_apri(&C.f, c);

    componi(c, &C, arg_di(a, n, 0), 0);

    /* ! SI RENDE `undefined` QUANDO NON CI SI RIESCE, come fa JavaScript
     * quando gli si passa `undefined` — e non una stringa a meta'. Una stringa
     * troncata verrebbe scritta su disco o mandata in rete come se fosse
     * intera. Adesso il caso e' raro — il tetto e' l'arena del motore, non
     * quattro kilobyte — ma resta possibile, e resta gestito. */
    if (C.rotto || C.f.off == EXJS_FILO_NO) {
        filo_chiudi(&C.f);
        return exjs_indefinito();
    }
    return filo_chiudi(&C.f);
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

/* ! ANCHE QUESTA SCRIVE NELL'ARENA. Prima costruiva il valore in mezzo
 * kilobyte: un campo di testo piu' lungo dentro un JSON — il corpo di un
 * messaggio, una pagina dentro una risposta — si rileggeva accorciato, senza
 * un errore. Si legge dall'arena e ci si scrive dentro nello stesso tempo, ed
 * e' lecito per la ragione scritta sopra: l'arena cresce in coda. */
static ExJsVal l_stringa(ExJsCtx *c, Leg *L)
{
    Filo f;

    if (L->s[L->i] != '"') { L->rotto = 1; return exjs_indefinito(); }
    L->i++;
    filo_apri(&f, c);

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
                    else { L->rotto = 1; filo_chiudi(&f); return exjs_indefinito(); }
                    u = u * 16 + (unsigned int)d;
                }
                /* In UTF-8, come fa il lessicale: e' quello che il resto del
                 * sistema legge. */
                {
                    char u8[3];

                    if (u < 0x80) {
                        u8[0] = (char)u;
                        filo_mette(&f, u8, 1);
                    } else if (u < 0x800) {
                        u8[0] = (char)(0xC0 | (u >> 6));
                        u8[1] = (char)(0x80 | (u & 0x3F));
                        filo_mette(&f, u8, 2);
                    } else {
                        u8[0] = (char)(0xE0 | (u >> 12));
                        u8[1] = (char)(0x80 | ((u >> 6) & 0x3F));
                        u8[2] = (char)(0x80 | (u & 0x3F));
                        filo_mette(&f, u8, 3);
                    }
                }
                continue;
            }
            /* ! IL FILO SI CHIUDE ANCHE USCENDO PER ERRORE, o l'arena
             * resterebbe con una stringa aperta a meta' e la prossima le
             * scriverebbe dentro. */
            default: L->rotto = 1; filo_chiudi(&f); return exjs_indefinito();
            }
        }
        filo_mette(&f, &ch, 1);
    }

    if (L->s[L->i] != '"') {
        L->rotto = 1;
        filo_chiudi(&f);
        return exjs_indefinito();
    }
    L->i++;
    return filo_chiudi(&f);
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
 * setTimeout, setInterval — e il tempo che arriva da fuori
 *
 * ! QUESTE NON FANNO SCADERE NIENTE: mettono in coda. A far passare il tempo e'
 * chi ospita, che chiama exjs_pompa con l'ora che ha lui. In un browser e' il
 * ciclo dei messaggi, in un banco di prova sono numeri inventati — ed e'
 * proprio per questo che una prova sui timer puo' essere ripetibile.
 *
 * ! L'ORA DI PARTENZA E' QUELLA DELL'ULTIMA POMPATA, non l'orologio: il motore
 * non ne ha uno. Chi non ha ancora pompato mai e' all'istante zero, il che e'
 * esattamente cio' che serve a uno script che parte con la pagina.
 * ========================================================================== */
static ExJsVal nat_setTimeout(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    int ritardo = arg_intero(c, a, n, 1, 0);
    int ripete  = (d != 0);

    (void)q;
    if (exjs_tipo(c, arg_di(a, n, 0)) != EXJS_FUNZIONE) return exjs_numero(c, 0);
    if (ritardo < 0) ritardo = 0;

    return exjs_numero(c, (double)exjs_accoda(c, a[0],
                            exjs_ora(c) + (unsigned int)ritardo,
                            ripete ? (unsigned int)ritardo : 0u));
}

static ExJsVal nat_clearTimeout(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q; (void)d;
    exjs_disdici(c, (unsigned int)arg_intero(c, a, n, 0, 0));
    return exjs_indefinito();
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

/* =============================================================================
 * Date (@EXJS-LACUNE, 28 settembre 2026)
 *
 * ! L'OROLOGIO E' DI CHI OSPITA: exjs_orologio_metti. La libreria non sa che
 * ore sono, come non sa scaricare una pagina; senza orologio `new Date()` e'
 * l'1 gennaio 1970, che e' un valore valido e non un errore.
 *
 * ! TUTTO IN UTC. EX-OS non ha fusi orari, quindi l'ora «locale» e' quella di
 * Greenwich: getHours e getUTCHours rendono lo stesso numero e
 * getTimezoneOffset rende 0. Una pagina che conta i giorni funziona; una che
 * mostra l'ora mostra quella di Londra d'inverno. Il giorno che il sistema
 * avra' un fuso, lo spostamento si mette in data_locale e basta.
 *
 * ! SOLO DIVISIONI A 32 BIT. Il giorno si ricava col double (pavimento), e
 * da li' in giu' anno, mese e giorno con gli interi di Howard Hinnant: nei
 * programmi di EX-OS la divisione a 64 bit (__divdi3) non c'e', e un `%` su
 * un long long compila sull'host e non si collega sul sistema.
 *
 * ! IL TEMPO STA IN UNA PROPRIETA' NASCOSTA, EXJS_DATA_TEMPO, il cui nome
 * comincia con \001: for..in e JSON.stringify saltano quei nomi, e nessuna
 * pagina puo' scriverne uno per sbaglio.
 * ========================================================================== */
#define MS_GIORNO   86400000.0
#define TEMPO_MAX   8.64e15              /* cento milioni di giorni, la norma */

static const char *const nomi_giorno[7] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
static const char *const nomi_mese[12] = { "Jan","Feb","Mar","Apr","May","Jun",
                                           "Jul","Aug","Sep","Oct","Nov","Dec" };

static double data_nan(void) { double z = 0.0; return z / z; }

/* Intero verso lo zero, come ToIntegerOrInfinity (senza l'infinito: chi
 * chiama ha gia' escluso i valori non finiti). */
static double tronca(double d)
{
    if (d > 9e15 || d < -9e15) return d;     /* gia' intero, e il cast sfora */
    return (double)(long long)d;
}

static int finito(double d) { return d == d && d < 1.7e308 && d > -1.7e308; }

/* TimeClip: fuori dai cento milioni di giorni il tempo non e' valido. */
static double taglia_tempo(double t)
{
    if (!finito(t) || t > TEMPO_MAX || t < -TEMPO_MAX) return data_nan();
    return tronca(t) + 0.0;              /* + 0.0 toglie il meno dallo zero */
}

/* Giorni dall'1/1/1970 di una data del calendario gregoriano (Hinnant). */
static int giorni_da_data(int y, int m, int d)
{
    int era, yoe, doy, doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;
    doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void data_da_giorni(int z, int *y, int *m, int *d)
{
    int era, doe, yoe, doy, mp;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp  = (5 * doy + 2) / 153;
    *d  = doy - (153 * mp + 2) / 5 + 1;
    *m  = mp < 10 ? mp + 3 : mp - 9;
    *y  = yoe + era * 400 + (*m <= 2);
}

/* I campi di un tempo valido: anno, mese (0-11), giorno, ore, minuti,
 * secondi, millesimi, e in f[7] il giorno della settimana (0 = domenica). */
static void data_campi(double t, int f[8])
{
    double g = pavimento(t / MS_GIORNO);
    int    r = (int)(t - g * MS_GIORNO);
    int    gi = (int)g;

    data_da_giorni(gi, &f[0], &f[1], &f[2]);
    f[1] -= 1;
    f[3] = r / 3600000;
    f[4] = r / 60000 % 60;
    f[5] = r / 1000 % 60;
    f[6] = r % 1000;
    f[7] = ((gi % 7) + 11) % 7;          /* l'1/1/1970 era un giovedi' */
}

/* MakeDay + MakeTime + MakeDate, coi campi in double perche' le pagine ci
 * mettono di tutto: il mese 13 e' gennaio dell'anno dopo, il giorno 0 e'
 * l'ultimo del mese prima. */
static double data_componi(const double f[7])
{
    double y, mo, ym, mn, giorni, ora;
    int    i;

    for (i = 0; i < 7; i++) if (!finito(f[i])) return data_nan();
    y  = tronca(f[0]);
    mo = tronca(f[1]);
    ym = y + pavimento(mo / 12.0);
    mn = mo - pavimento(mo / 12.0) * 12.0;
    if (ym > 400000.0 || ym < -400000.0) return data_nan();
    giorni = (double)giorni_da_data((int)ym, (int)mn + 1, 1) + tronca(f[2]) - 1.0;
    ora = tronca(f[3]) * 3600000.0 + tronca(f[4]) * 60000.0 +
          tronca(f[5]) * 1000.0 + tronca(f[6]);
    return giorni * MS_GIORNO + ora;
}

/* ---- scrivere ------------------------------------------------------------ */
static char *d_int(char *o, int v, int cifre)
{
    char         b[12];
    unsigned int u, n = 0;

    if (v < 0) { *o++ = '-'; u = (unsigned int)(-(v + 1)) + 1u; }
    else u = (unsigned int)v;
    do { b[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while ((int)n < cifre) b[n++] = '0';
    while (n) *o++ = b[--n];
    return o;
}

static char *d_txt(char *o, const char *s) { while (*s) *o++ = *s++; return o; }

/* modo: 0 toString, 1 toISOString, 2 toUTCString, 3 toDateString,
 * 4 toTimeString, 5 toLocaleString, 6 toLocaleDateString, 7 toLocaleTimeString */
static void data_testo(double t, int modo, char *o)
{
    int f[8], h12;

    if (t != t) { d_txt(o, "Invalid Date")[0] = '\0'; return; }
    data_campi(t, f);
    h12 = f[3] % 12 ? f[3] % 12 : 12;

    switch (modo) {
    case 1:
        if (f[0] < 0 || f[0] > 9999) { *o++ = f[0] < 0 ? '-' : '+';
                                       o = d_int(o, f[0] < 0 ? -f[0] : f[0], 6); }
        else o = d_int(o, f[0], 4);
        *o++ = '-'; o = d_int(o, f[1] + 1, 2); *o++ = '-'; o = d_int(o, f[2], 2);
        *o++ = 'T'; o = d_int(o, f[3], 2); *o++ = ':'; o = d_int(o, f[4], 2);
        *o++ = ':'; o = d_int(o, f[5], 2); *o++ = '.'; o = d_int(o, f[6], 3);
        *o++ = 'Z';
        break;
    case 2:
        o = d_txt(o, nomi_giorno[f[7]]); o = d_txt(o, ", "); o = d_int(o, f[2], 2);
        *o++ = ' '; o = d_txt(o, nomi_mese[f[1]]); *o++ = ' '; o = d_int(o, f[0], 4);
        *o++ = ' '; o = d_int(o, f[3], 2); *o++ = ':'; o = d_int(o, f[4], 2);
        *o++ = ':'; o = d_int(o, f[5], 2); o = d_txt(o, " GMT");
        break;
    case 5: case 6: case 7:
        if (modo != 7) {
            o = d_int(o, f[1] + 1, 1); *o++ = '/'; o = d_int(o, f[2], 1);
            *o++ = '/'; o = d_int(o, f[0], 1);
        }
        if (modo == 5) o = d_txt(o, ", ");
        if (modo != 6) {
            o = d_int(o, h12, 1); *o++ = ':'; o = d_int(o, f[4], 2);
            *o++ = ':'; o = d_int(o, f[5], 2); o = d_txt(o, f[3] < 12 ? " AM" : " PM");
        }
        break;
    default:                                     /* 0, 3, 4 */
        if (modo != 4) {
            o = d_txt(o, nomi_giorno[f[7]]); *o++ = ' '; o = d_txt(o, nomi_mese[f[1]]);
            *o++ = ' '; o = d_int(o, f[2], 2); *o++ = ' '; o = d_int(o, f[0], 4);
        }
        if (modo == 0) *o++ = ' ';
        if (modo != 3) {
            o = d_int(o, f[3], 2); *o++ = ':'; o = d_int(o, f[4], 2); *o++ = ':';
            o = d_int(o, f[5], 2); o = d_txt(o, " GMT+0000 (Coordinated Universal Time)");
        }
        break;
    }
    *o = '\0';
}

/* ---- leggere ------------------------------------------------------------- */
static int d_cifre(const char **ps, int max, int *v)
{
    const char *s = *ps;
    int         n = 0;

    *v = 0;
    while (n < max && *s >= '0' && *s <= '9') { *v = *v * 10 + (*s - '0'); s++; n++; }
    *ps = s;
    return n;
}

static int d_minuscola(char ch) { return (ch >= 'A' && ch <= 'Z') ? ch + 32 : ch; }

/* Il formato ISO della norma: AAAA, AAAA-MM, AAAA-MM-GG, poi facoltativi
 * THH:mm, :ss, .sss e Z o +HH:mm. Rende 0 se non lo e'. */
static int data_leggi_iso(const char *s, double *out)
{
    double f[7] = { 0, 0, 1, 0, 0, 0, 0 };
    int    v, segno = 1, n, spost = 0;

    if (*s == '+' || *s == '-') {
        segno = *s == '-' ? -1 : 1; s++;
        if (d_cifre(&s, 6, &v) != 6) return 0;
    } else if (d_cifre(&s, 4, &v) != 4) return 0;
    f[0] = segno * v;
    if (*s == '-') { s++; if (d_cifre(&s, 2, &v) != 2) return 0; f[1] = v - 1;
        if (*s == '-') { s++; if (d_cifre(&s, 2, &v) != 2) return 0; f[2] = v; } }
    if (*s == 'T' || *s == 't' || *s == ' ') {
        s++;
        if (d_cifre(&s, 2, &v) != 2 || *s != ':') return 0;
        f[3] = v; s++;
        if (d_cifre(&s, 2, &v) != 2) return 0;
        f[4] = v;
        if (*s == ':') { s++; if (d_cifre(&s, 2, &v) != 2) return 0; f[5] = v;
            if (*s == '.' || *s == ',') { s++; n = d_cifre(&s, 3, &v);
                if (!n) return 0;
                while (n < 3) { v *= 10; n++; }
                f[6] = v;
                while (*s >= '0' && *s <= '9') s++; } }
        if (*s == 'Z' || *s == 'z') s++;
        else if (*s == '+' || *s == '-') {
            int sg = *s == '-' ? -1 : 1, hh, mm = 0;
            s++;
            if (d_cifre(&s, 2, &hh) != 2) return 0;
            if (*s == ':') s++;
            d_cifre(&s, 2, &mm);
            spost = sg * (hh * 60 + mm);
        }
    }
    if (*s) return 0;
    if (f[1] > 11 || f[2] < 1 || f[2] > 31 || f[3] > 24 || f[4] > 59 || f[5] > 59)
        return 0;
    *out = data_componi(f) - spost * 60000.0;
    return 1;
}

/* Il resto, alla buona: le forme che scrive toString e toUTCString
 * («Thu Jan 01 1970 00:00:00 GMT+0000», «Thu, 01 Jan 1970 00:00:00 GMT») e
 * quelle americane «Jan 1, 1970» e «1/31/1970». I nomi dei giorni si
 * saltano, GMT e UTC anche. */
static double data_leggi(const char *s)
{
    double f[7] = { -1e9, -1, -1, 0, 0, 0, 0 };
    int    v, i, numeri[3], nn = 0, spost = 0, ora_vista = 0;

    while (*s == ' ') s++;
    if (data_leggi_iso(s, &f[0])) return taglia_tempo(f[0]);
    f[0] = -1e9;

    while (*s) {
        if (*s >= '0' && *s <= '9') {
            d_cifre(&s, 9, &v);
            if (*s == ':') {                                   /* l'ora */
                f[3] = v; s++; d_cifre(&s, 2, &v); f[4] = v; ora_vista = 1;
                if (*s == ':') { s++; d_cifre(&s, 2, &v); f[5] = v; }
                continue;
            }
            if (*s == '/') {                                   /* M/G/A */
                f[1] = v - 1; s++; d_cifre(&s, 2, &v); f[2] = v;
                if (*s == '/') { s++; d_cifre(&s, 9, &v); f[0] = v; }
                continue;
            }
            if (nn < 3) numeri[nn++] = v;
            continue;
        }
        if ((*s == '+' || *s == '-') && s[1] >= '0' && s[1] <= '9' && ora_vista) {
            int sg = *s == '-' ? -1 : 1;                       /* GMT+0100 */
            s++; d_cifre(&s, 4, &v);
            spost = sg * ((v / 100) * 60 + v % 100);
            continue;
        }
        if (d_minuscola(*s) >= 'a' && d_minuscola(*s) <= 'z') {
            char p[4]; int k = 0;
            while (d_minuscola(*s) >= 'a' && d_minuscola(*s) <= 'z') {
                if (k < 3) p[k++] = (char)d_minuscola(*s);
                s++;
            }
            p[k] = '\0';
            for (i = 0; i < 12; i++)
                if (k == 3 && d_minuscola(nomi_mese[i][0]) == p[0] &&
                    nomi_mese[i][1] == p[1] && nomi_mese[i][2] == p[2]) f[1] = i;
            if ((p[0] == 'p' || p[0] == 'a') && p[1] == 'm' && k == 2) {
                if (p[0] == 'p' && f[3] < 12) f[3] += 12;
                if (p[0] == 'a' && f[3] == 12) f[3] = 0;
            }
            continue;
        }
        s++;
    }
    /* I numeri sciolti: il giorno e l'anno, in un ordine qualunque — l'anno
     * e' quello grande. */
    for (i = 0; i < nn; i++) {
        if (numeri[i] > 31 || f[2] >= 0) { if (f[0] == -1e9) f[0] = numeri[i]; }
        else f[2] = numeri[i];
    }
    if (f[0] == -1e9 || f[1] < 0 || f[2] < 1) return data_nan();
    return taglia_tempo(data_componi(f) - spost * 60000.0);
}

/* ---- l'oggetto ----------------------------------------------------------- */
static int data_tempo(ExJsCtx *c, ExJsVal v, double *t)
{
    int k = exjs_a_oggetto(v), p;

    if (k < 0) return 0;
    p = exjs_prop_trova(c, k, EXJS_DATA_TEMPO, 0);
    if (p < 0) return 0;
    *t = exjs_a_numero(c, exjs_prop_val(c, p));
    return 1;
}

static double data_questo(ExJsCtx *c, ExJsVal q)
{
    double t;
    return data_tempo(c, q, &t) ? t : data_nan();
}

static void data_metti(ExJsCtx *c, ExJsVal q, double t)
{
    exjs_metti(c, q, EXJS_DATA_TEMPO, exjs_numero(c, t));
}

static double data_adesso(ExJsCtx *c)
{
    return taglia_tempo(pavimento(exjs_orologio_ms(c)));
}

/* I campi dagli argomenti di new Date(a, m, ...) e di Date.UTC. */
static double data_da_argomenti(ExJsCtx *c, const ExJsVal *a, int n)
{
    double f[7] = { 0, 0, 1, 0, 0, 0, 0 };
    int    i;

    for (i = 0; i < n && i < 7; i++) f[i] = exjs_a_numero(c, a[i]);
    if (finito(f[0])) { double y = tronca(f[0]); if (y >= 0 && y <= 99) f[0] = 1900 + y; }
    return taglia_tempo(data_componi(f));
}

static ExJsVal nat_Date(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char   b[80];
    double t;

    /* ! SENZA new, Date() e' una STRINGA, l'ora di adesso, e gli argomenti
     * non contano. Con new `this` e' l'oggetto appena fatto. */
    if (exjs_a_oggetto(q) < 0 || q == exjs_globale(c)) {
        data_testo(data_adesso(c), 0, b);
        return exjs_stringa(c, b, -1);
    }
    if (n == 0) t = data_adesso(c);
    else if (n == 1) {
        if (!data_tempo(c, a[0], &t)) {
            ExJsVal p = exjs_primitivo(c, a[0], 0);
            t = exjs_tipo(c, p) == EXJS_STRINGA ? data_leggi(exjs_a_stringa(c, p))
                                                : taglia_tempo(exjs_a_numero(c, p));
        }
    } else t = data_da_argomenti(c, a, n);

    data_metti(c, q, t);
    exjs_proto_metti(c, q, exjs_da_oggetto((int)(long)d));
    return q;
}

static ExJsVal nat_Date_now(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q; (void)a; (void)n; (void)d;
    return exjs_numero(c, data_adesso(c));
}

static ExJsVal nat_Date_UTC(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q; (void)d;
    return exjs_numero(c, n ? data_da_argomenti(c, a, n) : data_nan());
}

static ExJsVal nat_Date_parse(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    (void)q; (void)d;
    return exjs_numero(c, n ? data_leggi(exjs_a_stringa(c, a[0])) : data_nan());
}

/* I get: dato 0-6 un campo, 7 il giorno della settimana, 8 il tempo intero,
 * 9 lo spostamento del fuso, 10 getYear. */
static ExJsVal nat_data_get(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    double t = data_questo(c, q);
    int    f[8], k = (int)(long)d;

    (void)a; (void)n;
    if (t != t) return exjs_numero(c, t);
    if (k == 8) return exjs_numero(c, t);
    if (k == 9) return exjs_numero(c, 0.0);
    data_campi(t, f);
    return exjs_numero(c, (double)(k == 10 ? f[0] - 1900 : f[k]));
}

/* I set: dato = primo campo | (quanti argomenti al massimo << 4). setTime ha
 * dato 15. */
static ExJsVal nat_data_set(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    double t = data_questo(c, q), f[7];
    int    k = (int)(long)d & 15, max = (int)(long)d >> 4, i, g[8];

    if (exjs_a_oggetto(q) < 0) return exjs_numero(c, data_nan());
    if (k == 15) t = taglia_tempo(n ? exjs_a_numero(c, a[0]) : data_nan());
    else if (n == 0) t = data_nan();
    else {
        /* setFullYear su una data non valida riparte dall'1/1/1970. */
        if (t != t) { if (k != 0) return exjs_numero(c, t); t = 0.0; }
        data_campi(t, g);
        for (i = 0; i < 7; i++) f[i] = g[i];
        for (i = 0; i < n && i < max; i++) f[k + i] = exjs_a_numero(c, a[i]);
        t = taglia_tempo(data_componi(f));
    }
    data_metti(c, q, t);
    return exjs_numero(c, t);
}

static ExJsVal nat_data_testo(ExJsCtx *c, ExJsVal q, const ExJsVal *a, int n, void *d)
{
    char   b[80];
    double t = data_questo(c, q);

    (void)a; (void)n;
    /* toJSON di una data non valida e' null. toISOString dovrebbe lanciare
     * un RangeError, ma una nativa qui non sa lanciare: rende «Invalid Date». */
    if ((int)(long)d == 8) {
        if (t != t) return exjs_nullo();
        d = (void *)1;
    }
    data_testo(t, (int)(long)d, b);
    return exjs_stringa(c, b, -1);
}

static void data_registra(ExJsCtx *c, ExJsVal g)
{
    static const struct { const char *nome; int dato; } get[] = {
        { "getFullYear", 0 }, { "getMonth", 1 }, { "getDate", 2 }, { "getHours", 3 },
        { "getMinutes", 4 }, { "getSeconds", 5 }, { "getMilliseconds", 6 },
        { "getDay", 7 }, { "getUTCFullYear", 0 }, { "getUTCMonth", 1 },
        { "getUTCDate", 2 }, { "getUTCHours", 3 }, { "getUTCMinutes", 4 },
        { "getUTCSeconds", 5 }, { "getUTCMilliseconds", 6 }, { "getUTCDay", 7 },
        { "getTime", 8 }, { "valueOf", 8 }, { "getTimezoneOffset", 9 }, { "getYear", 10 },
    };
    static const struct { const char *nome; int dato; } set[] = {
        { "setFullYear", 0 | 3 << 4 }, { "setMonth", 1 | 2 << 4 }, { "setDate", 2 | 1 << 4 },
        { "setHours", 3 | 4 << 4 }, { "setMinutes", 4 | 3 << 4 }, { "setSeconds", 5 | 2 << 4 },
        { "setMilliseconds", 6 | 1 << 4 },
        { "setUTCFullYear", 0 | 3 << 4 }, { "setUTCMonth", 1 | 2 << 4 }, { "setUTCDate", 2 | 1 << 4 },
        { "setUTCHours", 3 | 4 << 4 }, { "setUTCMinutes", 4 | 3 << 4 },
        { "setUTCSeconds", 5 | 2 << 4 }, { "setUTCMilliseconds", 6 | 1 << 4 },
        { "setTime", 15 },
    };
    static const struct { const char *nome; int dato; } testo[] = {
        { "toString", 0 }, { "toISOString", 1 }, { "toUTCString", 2 }, { "toGMTString", 2 },
        { "toDateString", 3 }, { "toTimeString", 4 }, { "toLocaleString", 5 },
        { "toLocaleDateString", 6 }, { "toLocaleTimeString", 7 }, { "toJSON", 8 },
    };
    ExJsVal      proto = exjs_oggetto(c), D;
    unsigned int i;

    for (i = 0; i < sizeof(get) / sizeof(get[0]); i++)
        metti_nat(c, proto, get[i].nome, nat_data_get, (void *)(long)get[i].dato);
    for (i = 0; i < sizeof(set) / sizeof(set[0]); i++)
        metti_nat(c, proto, set[i].nome, nat_data_set, (void *)(long)set[i].dato);
    for (i = 0; i < sizeof(testo) / sizeof(testo[0]); i++)
        metti_nat(c, proto, testo[i].nome, nat_data_testo, (void *)(long)testo[i].dato);

    D = exjs_costruttore(c, nat_Date, (void *)(long)exjs_a_oggetto(proto), "Date");
    exjs_metti(c, g, "Date", D);
    exjs_metti(c, D, "prototype", proto);
    metti_nat(c, D, "now",   nat_Date_now,   0);
    metti_nat(c, D, "UTC",   nat_Date_UTC,   0);
    metti_nat(c, D, "parse", nat_Date_parse, 0);
}

void exjs_base_registra(ExJsCtx *c)
{
    ExJsVal g, math;

    if (exjs_base_gia_fatta(c)) return;
    exjs_base_segna(c);

    g = exjs_globale(c);

    metti_nat(c, g, "setTimeout",    nat_setTimeout,   0);
    metti_nat(c, g, "setInterval",   nat_setTimeout,   (void *)1);
    metti_nat(c, g, "clearTimeout",  nat_clearTimeout, 0);
    metti_nat(c, g, "clearInterval", nat_clearTimeout, 0);

    metti_nat(c, g, "parseInt",   nat_parseint,   0);
    metti_nat(c, g, "parseFloat", nat_parsefloat, 0);
    metti_nat(c, g, "isNaN",      nat_isnan,      0);
    metti_nat(c, g, "isFinite",   nat_isfinite,   0);
    metti_nat(c, g, "String",     nat_String,     0);
    metti_nat(c, g, "Number",     nat_Number,     0);
    metti_nat(c, g, "Boolean",    nat_Boolean,    0);
    data_registra(c, g);

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
        metti_nat_ogg(c, pv, "lastIndexOf", nat_lastIndexOf_vet, 0);
        metti_nat_ogg(c, pv, "shift",    nat_shift,       0);
        metti_nat_ogg(c, pv, "unshift",  nat_unshift,     0);
        metti_nat_ogg(c, pv, "concat",   nat_concat,      0);
        metti_nat_ogg(c, pv, "sort",     nat_sort,        0);
        metti_nat_ogg(c, pv, "forEach",  nat_forEach,     0);
        metti_nat_ogg(c, pv, "map",      nat_map,         0);
        metti_nat_ogg(c, pv, "filter",   nat_filter,      0);
    }
}

/* =============================================================================
 * QUELLO CHE NON C'E', DICHIARATO
 *
 *   Date nei fusi orari           e' tutta in UTC (vedi Date): EX-OS non ha
 *                                 fusi. Le native non lanciano, e
 *                                 toISOString di una data non valida rende
 *                                 «Invalid Date» invece del RangeError
 *   RegExp                        e con lui replace globale e split per
 *                                 espressione: e' uno scaglione suo
 *   toFixed, toString(base)       poco usati fuori dai numeri formattati
 * ========================================================================== */
