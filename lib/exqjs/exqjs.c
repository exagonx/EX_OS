/* =============================================================================
 * lib/exqjs/exqjs.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExQJs — l'interfaccia di ExJs, con QuickJS sotto
 *
 * ! QUESTO FILE E' LA PROVA CHE LA DIVISIONE IN TRE SERVIVA. `lib/exjs` e' il
 * linguaggio, `lib/exhtml` l'albero, `lib/exdom` il ponte — e il ponte parla
 * SOLO a `exjs.h`. Cambiare motore vuol dire scrivere questo file e nient'
 * altro: exdom non cambia di una riga, il browser nemmeno. Era scritto in cima
 * a exjs.h dalla prima stesura, ed e' il momento in cui si vede se era vero.
 *
 * ! E ExJs NON SI BUTTA. Sono 4400 righe che partono in 66 KB e non chiedono
 * ne' openlibm ne' libgcc; QuickJS ne chiede 639 di kilobyte e li merita solo
 * dove servono ES2017, le espressioni regolari e Date. Quale dei due si apre
 * e' una decisione di chi ospita, non di questo file.
 *
 * =============================================================================
 * COME SI TRADUCE UN ExJsVal
 *
 * ! IL PROBLEMA E' UNO SOLO, E DECIDE TUTTO IL RESTO: ExJs non ha un
 * raccoglitore di memoria e un `ExJsVal` si tiene dove si vuole, per sempre;
 * QuickJS conta i riferimenti, e un `JSValue` messo da parte senza
 * `JS_DupValue` muore quando lo decide il motore. Il ponte tiene un involucro
 * per OGNI nodo del documento: su una pagina vera sono migliaia, e vivono
 * quanto la pagina.
 *
 * La risposta e' una TABELLA DI MANIGLIE dentro questo file. Un ExJsVal che
 * non e' un numero ne' un valore immediato porta un indice in quella tabella;
 * la tabella tiene il JSValue con un riferimento in piu', ed e' l'unica radice
 * che il raccoglitore di QuickJS vede da questa parte. Chi ha l'ExJsVal non
 * sa niente di tutto questo, ed e' il punto.
 *
 * ! LE MANIGLIE NON SI LIBERANO UNA PER UNA, e non e' pigrizia: l'interfaccia
 * non ha un `exjs_libera`, perche' ExJs non ne aveva bisogno. Si liberano
 * TUTTE INSIEME quando il contesto si chiude, cioe' quando la pagina se ne va
 * — che e' esattamente la vita che avevano prima. La differenza e' che adesso
 * la tabella ha un tetto dichiarato (`oggetti`), e quando e' piena lo dice.
 *
 * ! E LA PRESSIONE E' MOLTO MINORE DI QUANTO SEMBRI: una maniglia nasce solo
 * quando un valore ATTRAVERSA il confine C. Cio' che accade dentro il
 * JavaScript — un ciclo che costruisce diecimila stringhe — non tocca questa
 * tabella nemmeno una volta. In ExJs invece ogni valore costava una casella:
 * e' il difetto che il commento in cima a exjs.h descrive, e qui non c'e'.
 *
 * =============================================================================
 * LA CODIFICA, in 64 bit
 *
 *     un double qualunque              -> se stesso, bit per bit
 *     0xFFF8_0000_0000_0000            -> indefinito
 *     0xFFF9_0000_0000_0000            -> nullo
 *     0xFFFA_0000_0000_000{0,1}        -> falso, vero
 *     0xFFFB_0000_xxxx_xxxx            -> maniglia numero xxxxxxxx
 *
 * ! I VALORI SPECIALI STANNO NEI NaN NEGATIVI, e non nei positivi come farebbe
 * comodo: un NaN prodotto dall'aritmetica (0/0) e' il NaN positivo silenzioso
 * 0x7FF8_0000_0000_0000, e usarlo come «indefinito» vorrebbe dire che
 * `0/0 === undefined`. I NaN col bit di segno acceso non li produce nessun
 * calcolo, quindi la fascia e' libera davvero — e un NaN che arrivasse da li'
 * si riporta al positivo prima di uscire (vedi da_js).
 * ============================================================================= */

#include <string.h>
#include <stdlib.h>

#include "quickjs.h"
#include "exjs.h"

/* -----------------------------------------------------------------------------
 * La codifica
 * --------------------------------------------------------------------------- */
#define BOX_MASCHERA   0xFFFF000000000000ULL
#define BOX_INDEF      0xFFF8000000000000ULL
#define BOX_NULLO      0xFFF9000000000000ULL
#define BOX_BOOL       0xFFFA000000000000ULL
#define BOX_MAN        0xFFFB000000000000ULL

#define E_BOXED(v)     (((v) & 0xFFF8000000000000ULL) == 0xFFF8000000000000ULL)
#define TAG(v)         ((v) & BOX_MASCHERA)
#define CARICO(v)      ((unsigned int)((v) & 0xFFFFFFFFULL))

/* Il nome della proprieta' nascosta che porta la maniglia: vedi da_js. I due
 * trattini bassi ai due capi sono la convenzione di chi non vuole essere
 * scambiato per una proprieta' di qualcun altro. */
#define MANIGLIA_NOME  "__exjs_maniglia__" 

/* -----------------------------------------------------------------------------
 * Lo stato
 * --------------------------------------------------------------------------- */

/* Una funzione nativa registrata da chi ospita. */
typedef struct {
    ExJsNativa f;
    void      *dato;
} Nativa;

/* Un oggetto esotico: i due ganci piu' il dato di chi l'ha costruito. */
typedef struct {
    ExJsLeggiProp  leggi;
    ExJsScriviProp scrivi;
    void          *dato;
} Esotico;

/* Un lavoro in coda: setTimeout e setInterval. */
typedef struct {
    ExJsVal      f;
    unsigned int quando_ms;
    unsigned int ripeti_ms;      /* 0 = una volta sola */
    unsigned int id;
    int          vivo;
} Lavoro;

struct ExJsCtx {
    JSRuntime *rt;
    JSContext *ctx;

    JSValue      *man;           /* la tabella delle maniglie */
    unsigned int  man_max;
    unsigned int  man_n;         /* quante ne sono state date */

    Nativa       *nat;
    unsigned int  nat_max, nat_n;

    Esotico      *eso;
    unsigned int  eso_max, eso_n;

    Lavoro       *lav;
    unsigned int  lav_max;
    unsigned int  prossimo_id;

    /* L'arena delle stringhe di exjs_a_stringa: vedi il commento la'. */
    char         *arena;
    unsigned int  arena_max, arena_usata;

    ExJsUscita    uscita;
    void         *uscita_dato;

    JSClassID     classe_eso;

    /* Vero mentre un'esecuzione e' in corso: lo guarda exjs_invoca. */
    int           dentro;
};

/* ! LA CLASSE DEGLI ESOTICI E' UNA SOLA PER TUTTO IL PROGRAMMA, e QuickJS
 * vuole che l'identificativo sia una variabile viva quanto il runtime. Sta
 * qui, statica, e JS_NewClassID la riempie una volta sola: due contesti
 * aperti insieme — due pagine — usano la stessa classe con dati diversi. */
static JSClassID g_classe_eso;

/* =============================================================================
 * LA TABELLA DELLE MANIGLIE
 * ========================================================================== */

/* Mette `v` (di cui prende la proprieta') nella tabella e rende l'ExJsVal.
 * Se la tabella e' piena libera `v` e rende indefinito: e' un tetto
 * dichiarato, come tutti quelli di questo sistema. */
static ExJsVal man_metti(ExJsCtx *c, JSValue v)
{
    if (c->man_n >= c->man_max) {
        JS_FreeValue(c->ctx, v);
        return BOX_INDEF;
    }
    c->man[c->man_n] = v;
    return BOX_MAN | (ExJsVal)(c->man_n++);
}

/* Il JSValue dietro una maniglia. NON e' una copia: chi lo usa non lo
 * libera. */
static JSValue man_prendi(ExJsCtx *c, ExJsVal v)
{
    unsigned int i;

    if (!E_BOXED(v) || TAG(v) != BOX_MAN) return JS_UNDEFINED;
    i = CARICO(v);
    if (i >= c->man_n) return JS_UNDEFINED;
    return c->man[i];
}

/* =============================================================================
 * LA TRADUZIONE NEI DUE VERSI
 * ========================================================================== */

/* Da ExJsVal a JSValue. Il valore reso e' PRESTATO quando viene da una
 * maniglia: chi lo passa a QuickJS deve duplicarlo se QuickJS lo terra'. */
static JSValue a_js(ExJsCtx *c, ExJsVal v)
{
    double d;

    if (!E_BOXED(v)) {
        memcpy(&d, &v, sizeof(d));
        return JS_NewFloat64(c->ctx, d);
    }

    switch (TAG(v)) {
    case BOX_INDEF: return JS_UNDEFINED;
    case BOX_NULLO: return JS_NULL;
    case BOX_BOOL:  return JS_NewBool(c->ctx, (int)CARICO(v));
    case BOX_MAN:   return man_prendi(c, v);
    default:        return JS_UNDEFINED;
    }
}

/* Come sopra, ma con un riferimento in piu': serve quando il valore viene
 * consegnato a QuickJS, che lo liberera' lui. */
static JSValue a_js_dup(ExJsCtx *c, ExJsVal v)
{
    JSValue j = a_js(c, v);

    if (E_BOXED(v) && TAG(v) == BOX_MAN) return JS_DupValue(c->ctx, j);
    return j;
}

/* Da JSValue a ExJsVal. PRENDE LA PROPRIETA' di `j`: o la mette in una
 * maniglia, o la libera. */
static ExJsVal da_js(ExJsCtx *c, JSValue j)
{
    ExJsVal v;
    double  d;
    int     b;

    if (JS_IsUndefined(j) || JS_IsUninitialized(j)) return BOX_INDEF;
    if (JS_IsNull(j))                               return BOX_NULLO;

    if (JS_IsBool(j)) {
        b = JS_ToBool(c->ctx, j);
        return BOX_BOOL | (ExJsVal)(b ? 1u : 0u);
    }

    if (JS_IsNumber(j)) {
        if (JS_ToFloat64(c->ctx, &d, j) < 0) d = 0.0;
        JS_FreeValue(c->ctx, j);

        /* ! UN NaN SI RIPORTA AL POSITIVO PRIMA DI USCIRE, o cadrebbe nella
         * fascia dei valori speciali e uscirebbe da qui come «indefinito» o
         * peggio come una maniglia che non esiste. Vedi la nota in testa. */
        if (d != d) return 0x7FF8000000000000ULL;

        memcpy(&v, &d, sizeof(v));
        return v;
    }

    /* ! UNA STRINGA NON HA IDENTITA', e non si interna: due stringhe uguali
     * sono la stessa cosa per chiunque le confronti, e cercarle nella tabella
     * costerebbe senza rendere niente. */
    if (!JS_IsObject(j)) return man_metti(c, j);

    /* =========================================================================
     * ! UN OGGETTO CHE ATTRAVERSA DUE VOLTE DEVE RENDERE LA STESSA MANIGLIA, e
     * senza questo blocco non lo faceva. Il sintomo era preciso e piccolo: due
     * `addEventListener` con LA STESSA funzione registravano due gestori, e
     * `removeEventListener` non ne trovava nessuno da togliere — perche' il
     * ponte confronta gli ExJsVal, e ogni passaggio ne fabbricava uno nuovo.
     *
     * ! LA MANIGLIA SI SCRIVE SULL'OGGETTO STESSO, in una proprieta' che non si
     * elenca: e' una ricerca in tempo costante invece di scorrere duemila
     * caselle a ogni argomento di ogni chiamata nativa. Non e' enumerabile, non
     * e' scrivibile e non e' configurabile, quindi `for..in`, `Object.keys` e
     * `JSON.stringify` non la vedono. Un oggetto congelato la rifiuta: in quel
     * caso si torna a una maniglia nuova e si perde l'identita', che e' meno
     * grave che fallire.
     *
     * ! ED E' LO STESSO PROBLEMA CHE exdom RISOLVE DALL'ALTRA PARTE — «un nodo
     * si avvolge una volta sola» — visto dal lato del motore. Due tabelle,
     * stessa ragione: senza, l'uguaglianza fra due riferimenti alla stessa cosa
     * smette di valere, e cade tutto quello che ci si appoggia.
     * ===================================================================== */
    {
        JSValue h = JS_GetPropertyStr(c->ctx, j, MANIGLIA_NOME);
        int     i = -1;

        if (JS_IsNumber(h) && JS_ToInt32(c->ctx, &i, h) == 0 &&
            i >= 0 && (unsigned int)i < c->man_n) {
            JS_FreeValue(c->ctx, h);
            JS_FreeValue(c->ctx, j);
            return BOX_MAN | (ExJsVal)(unsigned int)i;
        }
        JS_FreeValue(c->ctx, h);
    }

    v = man_metti(c, j);
    if (E_BOXED(v) && TAG(v) == BOX_MAN)
        JS_DefinePropertyValueStr(c->ctx, j, MANIGLIA_NOME,
                                  JS_NewInt32(c->ctx, (int)CARICO(v)), 0);
    return v;
}

/* =============================================================================
 * GLI OGGETTI ESOTICI — i due ganci di exdom, visti da QuickJS
 *
 * ! QUICKJS CHIAMA get_property/set_property SOLO SU UNA CLASSE ESOTICA, e ci
 * arriva DOPO aver guardato le proprieta' proprie dell'oggetto: e' lo stesso
 * ordine che exjs.h dichiara — proprie, gancio, prototipo — quindi non c'e'
 * niente da riprodurre a mano. In scrittura invece il gancio viene per primo,
 * ed e' cio' che permette a `elemento.innerHTML = '...'` di finire nel
 * documento e non in una proprieta' qualunque.
 *
 * ! E IL GANCIO PUO' DIRE «NON E' MIA» RENDENDO 0. In lettura si risponde
 * «non esiste» e QuickJS prosegue col prototipo; in scrittura si risponde
 * FALSE, che per QuickJS vuol dire «non l'ho gestita io» e la manda al
 * meccanismo normale — ed e' quel che serve perche' `elemento.mioStato = 3`
 * funzioni.
 * ========================================================================== */
/* ! IL GANCIO DI LETTURA E' get_own_property E NON get_property, ED E' TUTTA
 * LA DIFFERENZA. Sembrano la stessa cosa e non lo sono:
 *
 *     get_property      QuickJS ci consegna la lettura e SI FERMA a quel che
 *                       rispondiamo. Il prototipo non viene mai guardato —
 *                       cioe' `elemento.appendChild` non esiste piu'.
 *     get_own_property  ci chiede se la proprieta' e' NOSTRA. Se diciamo di
 *                       no, prosegue lui: prima le proprie, poi noi, poi il
 *                       prototipo.
 *
 * ! E QUEL SECONDO ORDINE E' ESATTAMENTE QUELLO SCRITTO IN exjs.h — proprie,
 * gancio, prototipo — che non e' una coincidenza: e' l'ordine giusto, e due
 * motori diversi ci sono arrivati da soli. Col primo gancio le prove del
 * ponte fallivano settantacinque volte su novantadue, tutte con «not a
 * function»: i metodi stavano sul prototipo e nessuno ci arrivava piu'.
 */
static int eso_leggi(JSContext *jc, JSPropertyDescriptor *desc,
                     JSValueConst obj, JSAtom atomo)
{
    ExJsCtx    *c = (ExJsCtx *)JS_GetContextOpaque(jc);
    Esotico    *e;
    const char *nome;
    ExJsVal     fuori = 0;
    int         preso;

    e = (Esotico *)JS_GetOpaque(obj, g_classe_eso);
    if (!c || !e || !e->leggi) return 0;

    nome = JS_AtomToCString(jc, atomo);
    if (!nome) return -1;

    preso = e->leggi(c, e->dato, nome, &fuori);
    JS_FreeCString(jc, nome);

    if (!preso) return 0;          /* non e' sua: QuickJS prosegue */

    /* ! IL DESCRITTORE PUO' ESSERE NULLO, e chiede solo «esiste?»: e' la
     * strada da cui passa `'innerHTML' in elemento`. Riempirlo comunque
     * vorrebbe dire costruire un valore che nessuno legge. */
    if (desc) {
        desc->flags  = JS_PROP_C_W_E;
        desc->value  = a_js_dup(c, fuori);
        desc->getter = JS_UNDEFINED;
        desc->setter = JS_UNDEFINED;
    }
    return 1;
}

/* ! IN SCRITTURA IL GANCIO VIENE PER PRIMO, come dichiara exjs.h: e' l'unico
 * modo che ha `elemento.innerHTML = '<b>x</b>'` di finire nel documento.
 *
 * ! MA «NON E' MIA» QUI NON PUO' ESSERE UN NO E BASTA. QuickJS prende cio' che
 * rendiamo come RISULTATO dell'assegnazione: rendere 0 vorrebbe dire «non si
 * puo' scrivere», e `elemento.mioStato = 3` sparirebbe in silenzio — che e'
 * proprio il difetto che gli oggetti esotici esistono per evitare, rovesciato.
 * Quando il gancio si tira indietro, la proprieta' la scriviamo noi: da quel
 * momento e' una proprieta' propria, e le letture successive la trovano prima
 * del gancio.
 */
static int eso_scrivi(JSContext *jc, JSValueConst obj, JSAtom atomo,
                      JSValueConst valore, JSValueConst ricevente, int flag)
{
    ExJsCtx    *c = (ExJsCtx *)JS_GetContextOpaque(jc);
    Esotico    *e;
    const char *nome;
    int         preso;

    (void)ricevente; (void)flag;

    e = (Esotico *)JS_GetOpaque(obj, g_classe_eso);
    if (!c || !e || !e->scrivi) return 0;

    nome = JS_AtomToCString(jc, atomo);
    if (!nome) return -1;

    preso = e->scrivi(c, e->dato, nome, da_js(c, JS_DupValue(jc, valore)));
    JS_FreeCString(jc, nome);

    if (preso) return 1;

    return JS_DefinePropertyValue(jc, obj, atomo, JS_DupValue(jc, valore),
                                  JS_PROP_C_W_E);
}

/* ! has_property NON C'E' APPOSTA. Definendola, QuickJS smette di guardare
 * sia le proprieta' proprie sia il prototipo e si fida solo di lei: `'appendChild'
 * in elemento` direbbe di no. Senza, il giro normale passa da get_own_property
 * qui sopra e prosegue da solo — che e' quel che serve. */

static JSClassExoticMethods g_eso_metodi = {
    .get_own_property       = eso_leggi,
    .get_own_property_names = NULL,
    .delete_property        = NULL,
    .define_own_property    = NULL,
    .has_property           = NULL,
    .get_property           = NULL,
    .set_property           = eso_scrivi,
};

static JSClassDef g_eso_classe = {
    "ExDomOggetto",
    .finalizer = NULL,
    .gc_mark   = NULL,
    .call      = NULL,
    .exotic    = &g_eso_metodi,
};

/* =============================================================================
 * LE FUNZIONI NATIVE
 *
 * ! IL `dato` DI CHI REGISTRA NON PUO' STARE IN UN JSValue, e nemmeno in un
 * `magic`, che e' un intero a 16 bit. Sta in una tabella del contesto, e alla
 * funzione si passa il suo INDICE come dato: e' l'unico modo di far arrivare
 * un puntatore a 32 bit senza inventare un tipo nuovo dentro il motore.
 * ========================================================================== */
static JSValue nativa_ponte(JSContext *jc, JSValueConst questo,
                            int argc, JSValueConst *argv,
                            int magic, JSValue *dati)
{
    ExJsCtx *c = (ExJsCtx *)JS_GetContextOpaque(jc);
    ExJsVal  arg[16];
    ExJsVal  reso;
    int      i, n, indice = 0;

    (void)magic;

    if (!c) return JS_UNDEFINED;
    if (JS_ToInt32(jc, &indice, dati[0]) < 0) return JS_EXCEPTION;
    if (indice < 0 || (unsigned int)indice >= c->nat_n) return JS_UNDEFINED;

    /* ! SEDICI ARGOMENTI E BASTA, ed e' un tetto dichiarato come gli altri:
     * nessuna funzione nativa di exdom ne prende piu' di quattro, e un
     * vettore variabile qui vorrebbe dire allocare a ogni chiamata — cioe'
     * pagare su ogni clic il caso che non capita mai. */
    n = argc > 16 ? 16 : argc;
    for (i = 0; i < n; i++)
        arg[i] = da_js(c, JS_DupValue(jc, argv[i]));

    reso = c->nat[indice].f(c, da_js(c, JS_DupValue(jc, questo)),
                            arg, n, c->nat[indice].dato);
    return a_js_dup(c, reso);
}

ExJsVal exjs_nativa(ExJsCtx *c, ExJsNativa f, void *dato, const char *nome)
{
    JSValue dati[1], fn;

    (void)nome;
    if (!c || !f || c->nat_n >= c->nat_max) return BOX_INDEF;

    c->nat[c->nat_n].f    = f;
    c->nat[c->nat_n].dato = dato;

    dati[0] = JS_NewInt32(c->ctx, (int)c->nat_n);
    c->nat_n++;

    fn = JS_NewCFunctionData(c->ctx, nativa_ponte, 0, 0, 1, dati);
    JS_FreeValue(c->ctx, dati[0]);

    return da_js(c, fn);
}

/* =============================================================================
 * console.log E LA CODA DEI LAVORI — quel che ExJs offre di suo
 *
 * ! NON VENGONO DA QUICKJS, E DEVONO ESSERCI LO STESSO. `console.log` in
 * QuickJS sta in quickjs-libc, che scrive su stdout: dentro un server grafico
 * vorrebbe dire stampare su una console che nessuno guarda. `setTimeout` non
 * c'e' affatto — e' roba dell'ospite, non del linguaggio. Li mette questo
 * file, con lo stesso comportamento che avevano in ExJs, perche' chi passa da
 * un motore all'altro non deve accorgersene.
 * ========================================================================== */
static JSValue console_log(JSContext *jc, JSValueConst questo,
                           int argc, JSValueConst *argv)
{
    ExJsCtx    *c = (ExJsCtx *)JS_GetContextOpaque(jc);
    char        riga[512];
    unsigned int uso = 0;
    int         i;

    (void)questo;
    if (!c || !c->uscita) return JS_UNDEFINED;

    for (i = 0; i < argc; i++) {
        const char *s = JS_ToCString(jc, argv[i]);
        unsigned int n;

        if (!s) continue;
        if (i > 0 && uso + 1 < sizeof(riga)) riga[uso++] = ' ';

        n = (unsigned int)strlen(s);
        if (uso + n >= sizeof(riga)) n = (unsigned int)(sizeof(riga) - 1 - uso);
        memcpy(riga + uso, s, n);
        uso += n;
        JS_FreeCString(jc, s);
    }
    /* ! LA RIGA FINISCE CON UN A CAPO, come in ExJs: `console.log` e' una
     * RIGA, non un pezzo di testo, e chi raccoglie l'uscita — la barra di
     * stato del browser, il banco di prova — conta sulle righe per separare
     * due messaggi. Senza, due log di seguito diventano una parola sola. */
    if (uso + 1 < sizeof(riga)) riga[uso++] = '\n';
    riga[uso] = '\0';

    c->uscita(riga, uso, c->uscita_dato);
    return JS_UNDEFINED;
}

/* setTimeout(f, ms) / setInterval(f, ms) — `magic` distingue i due. */
static JSValue js_accoda(JSContext *jc, JSValueConst questo,
                         int argc, JSValueConst *argv, int magic)
{
    ExJsCtx     *c = (ExJsCtx *)JS_GetContextOpaque(jc);
    double       ms = 0;
    unsigned int id;

    (void)questo;
    if (!c || argc < 1 || !JS_IsFunction(jc, argv[0])) return JS_NewInt32(jc, 0);
    if (argc >= 2) JS_ToFloat64(jc, &ms, argv[1]);
    if (ms < 0) ms = 0;

    id = exjs_accoda(c, da_js(c, JS_DupValue(jc, argv[0])),
                     (unsigned int)ms, magic ? (unsigned int)ms : 0u);
    return JS_NewInt32(jc, (int)id);
}

static JSValue js_disdici(JSContext *jc, JSValueConst questo,
                          int argc, JSValueConst *argv)
{
    ExJsCtx *c = (ExJsCtx *)JS_GetContextOpaque(jc);
    int      id = 0;

    (void)questo;
    if (!c || argc < 1) return JS_UNDEFINED;
    if (JS_ToInt32(jc, &id, argv[0]) < 0) return JS_UNDEFINED;

    exjs_disdici(c, (unsigned int)id);
    return JS_UNDEFINED;
}

static void base_registra(ExJsCtx *c)
{
    JSValue g = JS_GetGlobalObject(c->ctx);
    JSValue console = JS_NewObject(c->ctx);

    JS_SetPropertyStr(c->ctx, console, "log",
                      JS_NewCFunction(c->ctx, console_log, "log", 1));
    /* ! info, warn ed error VANNO DOVE VA log, e non da nessun'altra parte:
     * una pagina che chiama console.warn non deve tacere solo perche' noi non
     * abbiamo tre canali. */
    JS_SetPropertyStr(c->ctx, console, "info",
                      JS_NewCFunction(c->ctx, console_log, "info", 1));
    JS_SetPropertyStr(c->ctx, console, "warn",
                      JS_NewCFunction(c->ctx, console_log, "warn", 1));
    JS_SetPropertyStr(c->ctx, console, "error",
                      JS_NewCFunction(c->ctx, console_log, "error", 1));
    JS_SetPropertyStr(c->ctx, g, "console", console);

    JS_SetPropertyStr(c->ctx, g, "setTimeout",
                      JS_NewCFunctionMagic(c->ctx, js_accoda, "setTimeout",
                                           2, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(c->ctx, g, "setInterval",
                      JS_NewCFunctionMagic(c->ctx, js_accoda, "setInterval",
                                           2, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(c->ctx, g, "clearTimeout",
                      JS_NewCFunction(c->ctx, js_disdici, "clearTimeout", 1));
    JS_SetPropertyStr(c->ctx, g, "clearInterval",
                      JS_NewCFunction(c->ctx, js_disdici, "clearInterval", 1));

    JS_FreeValue(c->ctx, g);
}

/* =============================================================================
 * IL CICLO DI VITA
 *
 * ! LA MEMORIA E' DI DUE PADRONI, E VA DETTO. Il blocco che arriva da chi
 * chiama tiene le tabelle di QUESTO file — maniglie, native, esotici, lavori,
 * arena — ed e' esattamente il modello di ExJs. Ma il motore no: QuickJS ha il
 * proprio allocatore e prende dalla libc. Fingere il contrario avrebbe voluto
 * dire scrivere un allocatore dentro quel blocco, cioe' la parte piu'
 * sbagliabile di un porting, per un vantaggio che nessuno ha chiesto.
 *
 * ! E PROPRIO PER QUESTO SERVE exjs_chiudi. In ExJs non c'era: liberare il
 * blocco liberava tutto. Qui, senza, ogni pagina lascerebbe dietro un runtime
 * intero. E' l'unica cosa che il porting ha dovuto aggiungere all'interfaccia,
 * ed e' un no-op dentro ExJs.
 * ========================================================================== */
unsigned int exjs_quanto_serve(unsigned int oggetti, unsigned int arena_byte)
{
    return (unsigned int)(sizeof(struct ExJsCtx)
         + (size_t)oggetti * sizeof(JSValue)
         + (size_t)(oggetti / 4u + 16u) * sizeof(Nativa)
         + (size_t)(oggetti / 2u + 16u) * sizeof(Esotico)
         + (size_t)(oggetti / 8u + 8u)  * sizeof(Lavoro)
         + arena_byte + 256u);
}

ExJsCtx *exjs_apri(void *memoria, unsigned int byte,
                   unsigned int oggetti, unsigned int arena_byte)
{
    unsigned char *p = (unsigned char *)memoria;
    ExJsCtx       *c;

    if (!memoria || oggetti < 8 || arena_byte < 64) return 0;
    if (byte < exjs_quanto_serve(oggetti, arena_byte)) return 0;

    memset(memoria, 0, byte);

    c = (ExJsCtx *)p;                       p += sizeof(struct ExJsCtx);
    c->man = (JSValue *)p;                  p += (size_t)oggetti * sizeof(JSValue);
    c->nat = (Nativa *)p;                   p += (size_t)(oggetti / 4u + 16u) * sizeof(Nativa);
    c->eso = (Esotico *)p;                  p += (size_t)(oggetti / 2u + 16u) * sizeof(Esotico);
    c->lav = (Lavoro *)p;                   p += (size_t)(oggetti / 8u + 8u) * sizeof(Lavoro);
    c->arena = (char *)p;

    c->man_max = oggetti;
    c->nat_max = oggetti / 4u + 16u;
    c->eso_max = oggetti / 2u + 16u;
    c->lav_max = oggetti / 8u + 8u;
    c->arena_max = arena_byte;
    c->prossimo_id = 1;

    c->rt = JS_NewRuntime();
    if (!c->rt) return 0;

    c->ctx = JS_NewContext(c->rt);
    if (!c->ctx) { JS_FreeRuntime(c->rt); return 0; }

    JS_SetContextOpaque(c->ctx, c);

    if (g_classe_eso == 0) {
        JS_NewClassID(c->rt, &g_classe_eso);
    }
    JS_NewClass(c->rt, g_classe_eso, &g_eso_classe);
    c->classe_eso = g_classe_eso;

    base_registra(c);
    return c;
}

void exjs_chiudi(ExJsCtx *c)
{
    unsigned int i;

    if (!c || !c->rt) return;

    /* ! LE MANIGLIE PRIMA DEL RUNTIME, e l'ordine non e' discutibile: sono
     * riferimenti dentro di lui. Liberare il runtime per primo vorrebbe dire
     * restituire dopo dei riferimenti a memoria che non c'e' piu'. */
    for (i = 0; i < c->man_n; i++) JS_FreeValue(c->ctx, c->man[i]);
    c->man_n = 0;

    JS_FreeContext(c->ctx);
    JS_FreeRuntime(c->rt);
    c->ctx = 0;
    c->rt  = 0;
}

/* =============================================================================
 * ESEGUIRE
 * ========================================================================== */

/* Riempie `err` partendo dall'eccezione che QuickJS ha lasciato. */
static void errore_da_eccezione(ExJsCtx *c, ExJsErrore *err)
{
    JSValue     exc, riga;
    const char *msg;
    int         n = 0;

    if (!err) { JS_FreeValue(c->ctx, JS_GetException(c->ctx)); return; }

    memset(err, 0, sizeof(*err));
    exc = JS_GetException(c->ctx);

    msg = JS_ToCString(c->ctx, exc);
    if (msg) {
        strncpy(err->messaggio, msg, EXJS_ERR_LEN - 1);
        JS_FreeCString(c->ctx, msg);
    } else {
        strncpy(err->messaggio, "errore senza messaggio", EXJS_ERR_LEN - 1);
    }

    /* ! LA RIGA SI CHIEDE ALL'ECCEZIONE, e puo' non esserci: `throw 3` non e'
     * un oggetto e non ha lineNumber. Zero vuol dire «non si sa», che e' la
     * verita', e non «prima riga». */
    riga = JS_GetPropertyStr(c->ctx, exc, "lineNumber");
    if (!JS_IsUndefined(riga) && JS_ToInt32(c->ctx, &n, riga) == 0)
        err->riga = n;
    JS_FreeValue(c->ctx, riga);

    JS_FreeValue(c->ctx, exc);
}

int exjs_esegui(ExJsCtx *c, const char *sorgente, unsigned int n,
                ExJsVal *risultato, ExJsErrore *err)
{
    JSValue r;
    int     dentro_prima;

    if (!c || !c->ctx || !sorgente) return 0;

    /* ! L'ARENA DELLE STRINGHE SI AZZERA QUI, e solo qui: e' esattamente cio'
     * che exjs.h promette a chi usa exjs_a_stringa — «valida fino alla
     * prossima exjs_esegui». Azzerarla anche in exjs_invoca romperebbe quella
     * promessa a un gestore di evento che tiene un nome fra le mani. */
    c->arena_usata = 0;

    dentro_prima = c->dentro;
    c->dentro = 1;

    r = JS_Eval(c->ctx, sorgente, n, "<script>", JS_EVAL_TYPE_GLOBAL);

    c->dentro = dentro_prima;

    if (JS_IsException(r)) {
        errore_da_eccezione(c, err);
        JS_FreeValue(c->ctx, r);
        if (risultato) *risultato = BOX_INDEF;
        return 0;
    }

    if (risultato) *risultato = da_js(c, r);
    else           JS_FreeValue(c->ctx, r);
    return 1;
}

/* =============================================================================
 * COSTRUIRE E GUARDARE I VALORI
 * ========================================================================== */
ExJsVal exjs_indefinito(void) { return BOX_INDEF; }
ExJsVal exjs_nullo(void)      { return BOX_NULLO; }
ExJsVal exjs_booleano(int v)  { return BOX_BOOL | (ExJsVal)(v ? 1u : 0u); }

ExJsVal exjs_numero(ExJsCtx *c, double v)
{
    ExJsVal r;

    (void)c;
    if (v != v) return 0x7FF8000000000000ULL;   /* NaN canonico: vedi da_js */
    memcpy(&r, &v, sizeof(r));
    return r;
}

ExJsVal exjs_stringa(ExJsCtx *c, const char *s, int n)
{
    if (!c || !s) return BOX_INDEF;
    if (n < 0) n = (int)strlen(s);
    return da_js(c, JS_NewStringLen(c->ctx, s, (size_t)n));
}

ExJsVal exjs_oggetto(ExJsCtx *c)
{
    if (!c) return BOX_INDEF;
    return da_js(c, JS_NewObject(c->ctx));
}

ExJsVal exjs_vettore(ExJsCtx *c)
{
    if (!c) return BOX_INDEF;
    return da_js(c, JS_NewArray(c->ctx));
}

ExJsVal exjs_esotico(ExJsCtx *c, ExJsLeggiProp leggi, ExJsScriviProp scrivi,
                     void *dato)
{
    JSValue  o;
    Esotico *e;

    if (!c || c->eso_n >= c->eso_max) return BOX_INDEF;

    e = &c->eso[c->eso_n++];
    e->leggi  = leggi;
    e->scrivi = scrivi;
    e->dato   = dato;

    o = JS_NewObjectClass(c->ctx, (int)c->classe_eso);
    if (JS_IsException(o)) { JS_FreeValue(c->ctx, o); return BOX_INDEF; }

    JS_SetOpaque(o, e);
    return da_js(c, o);
}

void *exjs_esotico_dato(ExJsCtx *c, ExJsVal v)
{
    JSValue  j;
    Esotico *e;

    if (!c) return 0;
    j = man_prendi(c, v);
    if (!JS_IsObject(j)) return 0;

    e = (Esotico *)JS_GetOpaque(j, c->classe_eso);
    return e ? e->dato : 0;
}

int exjs_proto_metti(ExJsCtx *c, ExJsVal ogg, ExJsVal proto)
{
    if (!c) return 0;
    return JS_SetPrototype(c->ctx, man_prendi(c, ogg),
                           man_prendi(c, proto)) >= 0;
}

int exjs_tipo(ExJsCtx *c, ExJsVal v)
{
    JSValue j;

    if (!E_BOXED(v)) return EXJS_NUMERO;

    switch (TAG(v)) {
    case BOX_INDEF: return EXJS_INDEFINITO;
    case BOX_NULLO: return EXJS_NULLO;
    case BOX_BOOL:  return EXJS_BOOLEANO;
    default:        break;
    }

    if (!c) return EXJS_INDEFINITO;
    j = man_prendi(c, v);

    if (JS_IsString(j))              return EXJS_STRINGA;
    if (JS_IsFunction(c->ctx, j))    return EXJS_FUNZIONE;
    if (JS_IsObject(j))              return EXJS_OGGETTO;
    if (JS_IsNumber(j))              return EXJS_NUMERO;
    if (JS_IsBool(j))                return EXJS_BOOLEANO;
    if (JS_IsNull(j))                return EXJS_NULLO;
    return EXJS_INDEFINITO;
}

double exjs_a_numero(ExJsCtx *c, ExJsVal v)
{
    double d = 0;

    if (!E_BOXED(v)) { memcpy(&d, &v, sizeof(d)); return d; }
    if (TAG(v) == BOX_BOOL) return CARICO(v) ? 1.0 : 0.0;
    if (TAG(v) == BOX_NULLO) return 0.0;
    if (!c || TAG(v) != BOX_MAN) return 0.0 / 0.0;

    if (JS_ToFloat64(c->ctx, &d, man_prendi(c, v)) < 0) {
        JS_FreeValue(c->ctx, JS_GetException(c->ctx));
        return 0.0 / 0.0;
    }
    return d;
}

int exjs_a_booleano(ExJsCtx *c, ExJsVal v)
{
    double d;

    if (!E_BOXED(v)) {
        memcpy(&d, &v, sizeof(d));
        return d != 0.0 && d == d;
    }
    if (TAG(v) == BOX_BOOL)  return (int)CARICO(v);
    if (TAG(v) == BOX_INDEF) return 0;
    if (TAG(v) == BOX_NULLO) return 0;
    if (!c) return 0;

    return JS_ToBool(c->ctx, man_prendi(c, v)) > 0;
}

/* ! LA STRINGA VIVE NELL'ARENA DI CHI CHIAMA, e non nella memoria di QuickJS.
 * `JS_ToCString` da' un puntatore che va restituito con JS_FreeCString, e
 * l'interfaccia di exjs non ha un posto dove dirlo: chi chiama si aspetta una
 * stringa che resti valida «fino alla prossima exjs_esegui». Si copia, e
 * l'arena e' il posto dove sta. */
const char *exjs_a_stringa(ExJsCtx *c, ExJsVal v)
{
    const char  *s;
    char        *fuori;
    size_t       n;
    JSValue      j;

    if (!c) return "";

    if (!E_BOXED(v) || TAG(v) != BOX_MAN) {
        j = a_js(c, v);
        s = JS_ToCStringLen(c->ctx, &n, j);
        JS_FreeValue(c->ctx, j);
    } else {
        s = JS_ToCStringLen(c->ctx, &n, man_prendi(c, v));
    }

    if (!s) return "";

    if (c->arena_usata + n + 1u > c->arena_max) {
        /* ! QUANDO L'ARENA E' PIENA SI RICOMINCIA DA CAPO, e si dice qui
         * perche' e' l'unico posto dove si puo' dire: le stringhe consegnate
         * prima diventano quelle sbagliate. E' lo stesso patto di ExJs — un
         * tetto dichiarato — e chi ne tiene una fra le mani per mezzo
         * megabyte di pagina sta gia' facendo qualcosa di strano. */
        c->arena_usata = 0;
        if (n + 1u > c->arena_max) { JS_FreeCString(c->ctx, s); return ""; }
    }

    fuori = c->arena + c->arena_usata;
    memcpy(fuori, s, n);
    fuori[n] = '\0';
    c->arena_usata += (unsigned int)n + 1u;

    JS_FreeCString(c->ctx, s);
    return fuori;
}

/* =============================================================================
 * LE PROPRIETA'
 * ========================================================================== */
int exjs_metti(ExJsCtx *c, ExJsVal ogg, const char *nome, ExJsVal v)
{
    if (!c || !nome) return 0;
    return JS_SetPropertyStr(c->ctx, man_prendi(c, ogg), nome,
                             a_js_dup(c, v)) >= 0;
}

ExJsVal exjs_prendi(ExJsCtx *c, ExJsVal ogg, const char *nome)
{
    JSValue r;

    if (!c || !nome) return BOX_INDEF;

    r = JS_GetPropertyStr(c->ctx, man_prendi(c, ogg), nome);
    if (JS_IsException(r)) {
        JS_FreeValue(c->ctx, r);
        JS_FreeValue(c->ctx, JS_GetException(c->ctx));
        return BOX_INDEF;
    }
    return da_js(c, r);
}

int exjs_indice_metti(ExJsCtx *c, ExJsVal vet, unsigned int i, ExJsVal v)
{
    if (!c) return 0;
    return JS_SetPropertyUint32(c->ctx, man_prendi(c, vet), i,
                                a_js_dup(c, v)) >= 0;
}

ExJsVal exjs_indice_prendi(ExJsCtx *c, ExJsVal vet, unsigned int i)
{
    JSValue r;

    if (!c) return BOX_INDEF;

    r = JS_GetPropertyUint32(c->ctx, man_prendi(c, vet), i);
    if (JS_IsException(r)) {
        JS_FreeValue(c->ctx, r);
        JS_FreeValue(c->ctx, JS_GetException(c->ctx));
        return BOX_INDEF;
    }
    return da_js(c, r);
}

unsigned int exjs_lunghezza(ExJsCtx *c, ExJsVal vet)
{
    JSValue      l;
    unsigned int n = 0;
    int          k = 0;

    if (!c) return 0;

    l = JS_GetPropertyStr(c->ctx, man_prendi(c, vet), "length");
    if (JS_ToInt32(c->ctx, &k, l) == 0 && k > 0) n = (unsigned int)k;
    JS_FreeValue(c->ctx, l);
    return n;
}

/* =============================================================================
 * L'USCITA E IL GLOBALE
 * ========================================================================== */
void exjs_uscita_metti(ExJsCtx *c, ExJsUscita f, void *dato)
{
    if (!c) return;
    c->uscita = f;
    c->uscita_dato = dato;
}

ExJsVal exjs_globale(ExJsCtx *c)
{
    if (!c) return BOX_INDEF;
    return da_js(c, JS_GetGlobalObject(c->ctx));
}

/* =============================================================================
 * CHIAMARE
 * ========================================================================== */
static ExJsVal chiama_davvero(ExJsCtx *c, ExJsVal f, ExJsVal questo,
                              const ExJsVal *arg, int n_arg, ExJsErrore *err)
{
    JSValue  jarg[16];
    JSValue  r;
    int      i, n;

    if (!c || !c->ctx) return BOX_INDEF;

    n = n_arg > 16 ? 16 : (n_arg < 0 ? 0 : n_arg);
    for (i = 0; i < n; i++) jarg[i] = a_js(c, arg[i]);

    r = JS_Call(c->ctx, man_prendi(c, f), a_js(c, questo), n, jarg);

    if (JS_IsException(r)) {
        errore_da_eccezione(c, err);
        JS_FreeValue(c->ctx, r);
        return BOX_INDEF;
    }
    return da_js(c, r);
}

ExJsVal exjs_chiama(ExJsCtx *c, ExJsVal f, ExJsVal questo,
                    const ExJsVal *arg, int n_arg, ExJsErrore *err)
{
    return chiama_davvero(c, f, questo, arg, n_arg, err);
}

/* ! LA DIFFERENZA FRA LE DUE, IN QUICKJS, E' MOLTO MINORE CHE IN ExJs — e va
 * detto invece di lasciarlo scoprire. La' exjs_chiama funzionava solo dentro
 * un'esecuzione, perche' il conto dei passi viveva li'; QuickJS il proprio
 * stato ce l'ha nel runtime e una chiamata da fermo e' una chiamata come le
 * altre. Le due funzioni restano due perche' l'interfaccia le ha, e chi le usa
 * non deve cambiare: `exjs_invoca` continua a essere la porta dichiarata per
 * chi arriva da fuori. */
ExJsVal exjs_invoca(ExJsCtx *c, ExJsVal f, ExJsVal questo,
                    const ExJsVal *arg, int n_arg, ExJsErrore *err)
{
    ExJsVal r;
    int     dentro_prima;

    if (!c) return BOX_INDEF;

    dentro_prima = c->dentro;
    c->dentro = 1;
    r = chiama_davvero(c, f, questo, arg, n_arg, err);
    c->dentro = dentro_prima;
    return r;
}

/* =============================================================================
 * LA CODA DEI LAVORI
 *
 * ! IL TEMPO ARRIVA DA FUORI, come in ExJs e per la stessa ragione: una
 * libreria che chiedesse l'orologio da se' darebbe prove che passano oggi e
 * falliscono domani. QuickJS un orologio ce l'ha (js__hrtime_ns), e non lo si
 * usa qui apposta.
 * ========================================================================== */
unsigned int exjs_accoda(ExJsCtx *c, ExJsVal f, unsigned int quando_ms,
                         unsigned int ripeti_ms)
{
    unsigned int i;

    if (!c) return 0;

    for (i = 0; i < c->lav_max; i++) {
        if (c->lav[i].vivo) continue;

        c->lav[i].f         = f;
        c->lav[i].quando_ms = quando_ms;
        c->lav[i].ripeti_ms = ripeti_ms;
        c->lav[i].id        = c->prossimo_id++;
        c->lav[i].vivo      = 1;
        return c->lav[i].id;
    }
    return 0;
}

void exjs_disdici(ExJsCtx *c, unsigned int id)
{
    unsigned int i;

    if (!c || id == 0) return;
    for (i = 0; i < c->lav_max; i++)
        if (c->lav[i].vivo && c->lav[i].id == id) c->lav[i].vivo = 0;
}

int exjs_pompa(ExJsCtx *c, unsigned int ora_ms)
{
    unsigned int i;
    int          fatti = 0;

    if (!c || !c->ctx) return 0;

    for (i = 0; i < c->lav_max; i++) {
        Lavoro *l = &c->lav[i];
        ExJsVal f;

        if (!l->vivo) continue;
        /* ! IL CONFRONTO E' CON UNA DIFFERENZA CON SEGNO, non con `<=`: i
         * millisecondi girano ogni quarantanove giorni, e un `<=` diretto
         * farebbe scattare tutti i lavori insieme il giorno del giro. */
        if ((int)(ora_ms - l->quando_ms) < 0) continue;

        f = l->f;
        if (l->ripeti_ms) l->quando_ms = ora_ms + l->ripeti_ms;
        else              l->vivo = 0;

        exjs_invoca(c, f, exjs_indefinito(), 0, 0, 0);
        fatti++;
    }

    /* ! E POI I LAVORI DI QUICKJS, che ExJs non aveva: sono le promesse. Una
     * pagina che fa `fetch(...).then(...)` non vedrebbe mai il `then` senza
     * questa riga, e il sintomo sarebbe uno script che semplicemente non
     * finisce di funzionare. */
    {
        /* ! IL SECONDO ARGOMENTO NON PUO' ESSERE NULLO, e il modo di scoprirlo
         * e' stato un segmentation fault dentro QuickJS: la funzione ci scrive
         * dentro il contesto del lavoro che ha eseguito, sempre, anche quando
         * a noi non interessa. Un puntatore vero e buttato via costa una
         * variabile sulla pila. */
        JSContext *quale = 0;

        while (JS_ExecutePendingJob(c->rt, &quale) > 0) fatti++;
    }

    return fatti;
}

int exjs_lavori_in_attesa(ExJsCtx *c)
{
    unsigned int i;
    int          n = 0;

    if (!c) return 0;
    for (i = 0; i < c->lav_max; i++) if (c->lav[i].vivo) n++;
    if (JS_IsJobPending(c->rt)) n++;
    return n;
}

/* =============================================================================
 * LA MEMORIA
 *
 * ! I NUMERI SONO QUELLI VERI DI QUICKJS, non quelli delle nostre tabelle, ed
 * e' la risposta giusta alla domanda che fa il browser: «quanto sto usando?».
 * Le caselle qui sono le maniglie, che sono poche e nostre; l'arena e' quella
 * delle stringhe. Chi vuole sapere se sta finendo la memoria guarda il primo
 * paio, ed e' li' che sta la verita' del motore.
 * ========================================================================== */
void exjs_memoria(ExJsCtx *c, unsigned int *caselle_usate,
                  unsigned int *caselle_max, unsigned int *arena_usata,
                  unsigned int *arena_max)
{
    JSMemoryUsage u;

    if (!c) return;

    if (caselle_usate || caselle_max) {
        memset(&u, 0, sizeof(u));
        JS_ComputeMemoryUsage(c->rt, &u);
        if (caselle_usate) *caselle_usate = (unsigned int)(u.malloc_size / 64);
        if (caselle_max)   *caselle_max   = c->man_max;
    }
    if (arena_usata) *arena_usata = c->arena_usata;
    if (arena_max)   *arena_max   = c->arena_max;
}
