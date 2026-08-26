/* =============================================================================
 * lib/exjs/val.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * I valori, gli oggetti e le conversioni — il terzo pezzo di ExJs
 *
 * -----------------------------------------------------------------------------
 * ! LE CONVERSIONI SONO LA META' DEL LINGUAGGIO, E SONO DOVE STANNO LE
 * SORPRESE.
 *
 * `"5" * 2` fa dieci, `"5" + 2` fa "52", `[] + {}` fa "[object Object]", e
 * `null >= 0` e' vero mentre `null > 0` e' falso. Non sono stranezze da
 * imitare per fedelta' cieca: sono cio' che le pagine vere si aspettano, e un
 * motore che le sbaglia produce numeri diversi senza dare nessun errore. Sono
 * scritte nella norma una per una, e qui si seguono una per una.
 *
 * -----------------------------------------------------------------------------
 * ! IN QUESTO SCAGLIONE NON C'E' UN RACCOGLITORE DI MEMORIA, ED E' DICHIARATO.
 *
 * Un oggetto o una stringa presi non tornano piu' indietro: si esaurisce
 * l'arena e si dice. E' sopportabile perche' il NaN-boxing toglie di mezzo il
 * caso che conta — numeri e booleani non occupano niente, quindi un ciclo che
 * fa conti gira per sempre senza chiedere memoria — e perche' un contesto vive
 * quanto una pagina: si butta e si rifa'.
 *
 * Diventa insopportabile appena qualcuno concatena stringhe dentro un ciclo, e
 * quello succedera'. Il posto per un mark-sweep e' gia' preparato: gli oggetti
 * stanno in un vettore, le radici sono l'oggetto globale, la catena degli
 * ambienti e la pila dei valori — tre cose che si percorrono in venti righe.
 * ============================================================================= */

#include "exjs_int.h"

/* =============================================================================
 * NaN-BOXING
 *
 * Un double IEEE 754 ha undici bit di esponente. Quando sono tutti a uno e la
 * mantissa non e' zero, il valore e' un NaN — e le configurazioni che
 * significano tutte «non e' un numero» sono duemiladuecentocinquantatre
 * miliardi. Ci si nascondono dentro i valori che numeri non sono.
 *
 * ! IL BIT DI SEGNO DICE «QUI DENTRO C'E' UN INDICE». Senza, i quattro valori
 * singoli (undefined, null, false, true) e gli indici userebbero lo stesso
 * spazio e si potrebbero confondere.
 *
 *   0x7FFC000000000000  il NaN silenzioso che fa da marcatore
 *   | 0x8000000000000000  c'e' un indice
 *   | (tag << 48)         0 = stringa, 1 = oggetto
 *   | indice a 48 bit
 * ========================================================================== */
#define QNAN    0xFFFC000000000000ULL   /* segno + esponente + bit quiet */
#define MASC    0x7FFC000000000000ULL
#define SEGNO   0x8000000000000000ULL

#define V_INDEF (MASC | 1ULL)
#define V_NULLO (MASC | 2ULL)
#define V_FALSO (MASC | 3ULL)
#define V_VERO  (MASC | 4ULL)

#define T_STR   0x0000000000000000ULL
#define T_OGG   0x0001000000000000ULL

#define IDX_MAS 0x0000FFFFFFFFFFFFULL

#define E_DOPPIO(v)   (((v) & MASC) != MASC)
#define E_PUNT(v)     (((v) & QNAN) == QNAN)
#define PUNT_TAG(v)   ((v) & 0x0003000000000000ULL)
#define PUNT_IDX(v)   ((unsigned int)((v) & IDX_MAS))

static ExJsVal fai_punt(unsigned long long tag, unsigned int idx)
{
    return QNAN | tag | (unsigned long long)idx;
}

/* ! IL PASSAGGIO FRA double E BIT NON SI FA COL CAST DI PUNTATORE. `*(long
 * long *)&d` viola l'aliasing rigoroso, e i compilatori con l'ottimizzazione
 * accesa hanno il diritto di riordinarlo: il difetto compare solo con -O2 e
 * solo su alcune versioni, che e' il modo peggiore di scoprirlo. Un'unione fa
 * la stessa cosa ed e' consentita. */
typedef union { double d; unsigned long long u; } Ponte;

static ExJsVal da_doppio(double d)
{
    Ponte p;
    p.d = d;
    return p.u;
}

static double a_doppio(ExJsVal v)
{
    Ponte p;
    p.u = v;
    return p.d;
}

/* =============================================================================
 * IL CONTESTO
 * ========================================================================== */
struct ExJsCtx {
    ExJsOggetto *oggetti;
    unsigned int oggetti_max, oggetti_n;

    ExJsProp    *prop;
    unsigned int prop_max, prop_n;

    ExJsVal     *elem;              /* gli elementi dei vettori */
    unsigned int elem_max, elem_n;

    char        *arena;
    unsigned int arena_max, arena_n;

    int          globale;           /* indice dell'oggetto globale */
    int          finita;            /* la memoria e' esaurita: si smette */

    /* La coda dei lavori: il perche' sta in exjs.h. */
    ExJsLavoro  *lavori;
    unsigned int lavori_max;
    unsigned int prossimo_id;

    /* Un posto dove comporre una stringa da rendere a chi chiama
     * exjs_a_stringa: vale fino alla chiamata dopo, ed e' scritto nel
     * contratto. */
    char         scratch[64];

    /* =====================================================================
     * ! L'ALBERO STA DENTRO IL CONTESTO, e non lo passa chi chiama.
     *
     * exjs_esegui riceve del TESTO: se l'albero fosse suo, ogni chiamante
     * dovrebbe allocarne uno e sapere quanto grande — cioe' sapere quanti
     * nodi costa uno script che non ha ancora letto. Meglio una manopola
     * sola (`oggetti`) da cui tutto il resto discende.
     *
     * ! E SI RIFA' A OGNI exjs_esegui, MENTRE LE VARIABILI RESTANO. Due
     * <script> nella stessa pagina vedono le stesse variabili — cosi' fanno
     * le pagine vere — ma il testo del primo, una volta eseguito, non serve
     * piu' a nessuno.
     * ===================================================================== */
    /* =====================================================================
     * ! I PROTOTIPI DEI TIPI PRIMITIVI, e senza questi `'abc'.indexOf('b')`
     * non puo' esistere.
     *
     * Una stringa non e' un oggetto e non ha proprieta' sue: quando si chiede
     * `s.indexOf`, JavaScript cerca su String.prototype. Lo stesso per i
     * vettori e per i numeri. Tenerli QUI, e farli consultare da exjs_prendi,
     * vuol dire che il posto in cui si cerca una proprieta' resta UNO —
     * altrimenti l'interprete dovrebbe sapere che le stringhe sono speciali,
     * e ogni punto che legge una proprieta' andrebbe ricordato.
     * ===================================================================== */
    int          proto_str, proto_vet, proto_num, proto_fun;

    /* ! LO STATO D'ESECUZIONE IN CORSO, e serve a una cosa sola: permettere a
     * una funzione NATIVA di chiamare una funzione scritta in JavaScript.
     * `[1,2].forEach(f)` e' codice C che deve chiamare `f`, e senza un modo di
     * risalire all'interprete non potrebbe. Vale solo durante exjs_esegui, ed
     * e' zero fuori. */
    void        *ese;

    unsigned int seme;              /* per Math.random */
    int          base_fatta;        /* la libreria di base e' gia' registrata */
    int          ast_pronto;        /* l'albero e' stato preparato una volta */
    unsigned int ora_ms;            /* l'ultima ora vista da exjs_pompa */

    /* ! DOVE FINISCE console.log LO DECIDE CHI OSPITA, non questa libreria.
     * In un browser va nella sua console, in un banco di prova sullo schermo,
     * dentro un servizio da nessuna parte. Una libreria che scrivesse sullo
     * standard output da se' sarebbe una libreria che si porta dietro una
     * decisione che non e' sua — e che in un server grafico stamperebbe su una
     * console che nessuno guarda. */
    ExJsUscita   uscita;
    void        *uscita_dato;

    ExJsAst      ast;
    ExJsNodo    *nodi;
    unsigned int nodi_max;
    char        *ast_arena;
    unsigned int ast_arena_max;
    char         lex_buf[EXJS_SCRATCH];
};

ExJsAst      *exjs_ctx_ast(ExJsCtx *c)           { return &c->ast; }
ExJsNodo     *exjs_ctx_nodi(ExJsCtx *c)          { return c->nodi; }
unsigned int  exjs_ctx_nodi_max(ExJsCtx *c)      { return c->nodi_max; }
char         *exjs_ctx_ast_arena(ExJsCtx *c)     { return c->ast_arena; }
unsigned int  exjs_ctx_ast_arena_max(ExJsCtx *c) { return c->ast_arena_max; }
char         *exjs_ctx_scratch(ExJsCtx *c)       { return c->lex_buf; }

unsigned int exjs_quanto_serve(unsigned int oggetti, unsigned int arena_byte)
{
    /* ! LE PROPORZIONI SONO MISURATE SU CODICE VERO, non scelte a caso: un
     * oggetto medio ha tre o quattro proprieta', e i vettori sono meno degli
     * oggetti. Chi ha bisogni diversi alza `oggetti`, e tutto cresce con lui. */
    return (unsigned int)(sizeof(struct ExJsCtx)
         + oggetti * sizeof(ExJsOggetto)
         + oggetti * 4u * sizeof(ExJsProp)
         + oggetti * 4u * sizeof(ExJsVal)
         + oggetti / 8u * sizeof(ExJsLavoro)
         + oggetti * 8u * sizeof(ExJsNodo)     /* l'albero */
         + arena_byte / 2u                     /* i nomi dell'albero */
         + arena_byte + 128u);
}

ExJsCtx *exjs_apri(void *memoria, unsigned int byte,
                   unsigned int oggetti, unsigned int arena_byte)
{
    unsigned char *p = (unsigned char *)memoria;
    ExJsCtx       *c;

    if (!memoria || oggetti < 8 || arena_byte < 64) return 0;
    if (byte < exjs_quanto_serve(oggetti, arena_byte)) return 0;

    c = (ExJsCtx *)p;                       p += sizeof(struct ExJsCtx);
    c->oggetti = (ExJsOggetto *)p;          p += oggetti * sizeof(ExJsOggetto);
    c->prop    = (ExJsProp *)p;             p += oggetti * 4u * sizeof(ExJsProp);
    c->elem    = (ExJsVal *)p;              p += oggetti * 4u * sizeof(ExJsVal);
    c->lavori  = (ExJsLavoro *)p;           p += oggetti / 8u * sizeof(ExJsLavoro);
    c->nodi    = (ExJsNodo *)p;             p += oggetti * 8u * sizeof(ExJsNodo);
    c->ast_arena = (char *)p;               p += arena_byte / 2u;
    c->arena   = (char *)p;

    c->oggetti_max = oggetti;  c->oggetti_n = 0;
    c->prop_max    = oggetti * 4u; c->prop_n = 0;
    c->elem_max    = oggetti * 4u; c->elem_n = 0;
    c->lavori_max  = oggetti / 8u;
    c->nodi_max    = oggetti * 8u;
    c->ast_arena_max = arena_byte / 2u;
    c->arena_max   = arena_byte; c->arena_n = 0;
    c->finita      = 0;
    c->prossimo_id = 1;

    /* Il byte zero e' sempre uno '\0', come nell'albero: cosi' lo scostamento
     * zero e' «stringa vuota» e non «nessuna stringa». */
    c->arena[0] = '\0';
    c->arena_n  = 1;

    {
        unsigned int i;
        for (i = 0; i < c->lavori_max; i++) c->lavori[i].usato = 0;
    }

    c->globale = exjs_ogg_nuovo(c, EXJS_CL_OGGETTO);
    if (c->globale < 0) return 0;

    /* =====================================================================
     * ! `undefined`, `NaN` E `Infinity` SONO NOMI, NON PAROLE CHIAVE.
     *
     * Sembra un dettaglio da grammatica e non lo e': `x === undefined` e'
     * il modo in cui mezzo web controlla se una cosa esiste, e senza queste
     * tre righe darebbe «nome non definito» — cioe' un errore su codice
     * perfettamente normale. Il lessicale infatti non li conosce, e fa bene:
     * sono variabili globali che il linguaggio si trova gia' pronte, e in
     * ES3 si potevano perfino riassegnare.
     * ===================================================================== */
    c->proto_str = exjs_ogg_nuovo(c, EXJS_CL_OGGETTO);
    c->proto_vet = exjs_ogg_nuovo(c, EXJS_CL_OGGETTO);
    c->proto_num = exjs_ogg_nuovo(c, EXJS_CL_OGGETTO);
    c->proto_fun = exjs_ogg_nuovo(c, EXJS_CL_OGGETTO);
    c->ese  = 0;
    c->seme = 2463534242u;          /* un seme qualunque, ma non zero */
    c->base_fatta  = 0;
    c->ast_pronto  = 0;
    c->ora_ms      = 0;
    c->uscita      = 0;
    c->uscita_dato = 0;

    exjs_metti(c, exjs_da_oggetto(c->globale), "undefined", V_INDEF);
    exjs_metti(c, exjs_da_oggetto(c->globale), "NaN",       MASC);
    exjs_metti(c, exjs_da_oggetto(c->globale), "Infinity",
               exjs_numero(c, 1.0e308 * 10.0));

    return c;
}

void exjs_memoria(ExJsCtx *c, unsigned int *caselle_usate,
                  unsigned int *caselle_max, unsigned int *arena_usata,
                  unsigned int *arena_max)
{
    if (!c) return;
    if (caselle_usate) *caselle_usate = c->oggetti_n;
    if (caselle_max)   *caselle_max   = c->oggetti_max;
    if (arena_usata)   *arena_usata   = c->arena_n;
    if (arena_max)     *arena_max     = c->arena_max;
}

int exjs_finita(ExJsCtx *c) { return c ? c->finita : 1; }

/* =============================================================================
 * L'ARENA E GLI OGGETTI
 * ========================================================================== */
static unsigned int lung(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

unsigned int exjs_arena_metti(ExJsCtx *c, const char *s, unsigned int n)
{
    unsigned int off, i;

    if (c->arena_n + n + 1 > c->arena_max) { c->finita = 1; return 0; }

    off = c->arena_n;
    for (i = 0; i < n; i++) c->arena[off + i] = s[i];
    c->arena[off + n] = '\0';
    c->arena_n += n + 1;
    return off;
}

const char *exjs_arena_leggi(ExJsCtx *c, unsigned int off)
{
    return (off < c->arena_n) ? c->arena + off : "";
}

int exjs_ogg_nuovo(ExJsCtx *c, int classe)
{
    ExJsOggetto *O;
    int          i;

    if (c->oggetti_n >= c->oggetti_max) { c->finita = 1; return -1; }

    i = (int)c->oggetti_n++;
    O = &c->oggetti[i];

    O->classe     = (unsigned char)classe;
    O->proto      = -1;
    O->prima_prop = -1;
    O->nodo       = -1;
    O->nativa     = 0;
    O->dato       = 0;
    O->ambiente   = -1;
    O->nome       = 0;
    O->elem_off   = 0;
    O->elem_cap   = 0;
    O->lunghezza  = 0;
    return i;
}

ExJsOggetto *exjs_ogg(ExJsCtx *c, int i)
{
    return (i >= 0 && i < (int)c->oggetti_n) ? &c->oggetti[i] : 0;
}

/* =============================================================================
 * COSTRUIRE I VALORI
 * ========================================================================== */
ExJsVal exjs_indefinito(void)  { return V_INDEF; }
ExJsVal exjs_nullo(void)       { return V_NULLO; }
ExJsVal exjs_booleano(int v)   { return v ? V_VERO : V_FALSO; }

ExJsVal exjs_numero(ExJsCtx *c, double v)
{
    (void)c;
    /* ! UN NaN CALCOLATO VA NORMALIZZATO, o si confonderebbe con i valori
     * nascosti. `0/0` produce un NaN qualunque, e se per caso avesse il bit di
     * segno acceso somiglierebbe a un indice. Si rende sempre lo stesso. */
    if (v != v) return MASC;
    return da_doppio(v);
}

ExJsVal exjs_stringa(ExJsCtx *c, const char *s, int n)
{
    unsigned int off;

    if (!s) return V_INDEF;
    off = exjs_arena_metti(c, s, n < 0 ? lung(s) : (unsigned int)n);
    return fai_punt(T_STR, off);
}

ExJsVal exjs_stringa_off(ExJsCtx *c, unsigned int off)
{
    (void)c;
    return fai_punt(T_STR, off);
}

ExJsVal exjs_da_oggetto(int i)
{
    return (i < 0) ? V_INDEF : fai_punt(T_OGG, (unsigned int)i);
}

int exjs_a_oggetto(ExJsVal v)
{
    if (!E_PUNT(v) || PUNT_TAG(v) != T_OGG) return -1;
    return (int)PUNT_IDX(v);
}

unsigned int exjs_a_off(ExJsVal v)
{
    return PUNT_IDX(v);
}

ExJsVal exjs_oggetto(ExJsCtx *c)
{
    return exjs_da_oggetto(exjs_ogg_nuovo(c, EXJS_CL_OGGETTO));
}

ExJsVal exjs_vettore(ExJsCtx *c)
{
    return exjs_da_oggetto(exjs_ogg_nuovo(c, EXJS_CL_VETTORE));
}

ExJsVal exjs_nativa(ExJsCtx *c, ExJsNativa f, void *dato, const char *nome)
{
    int          i = exjs_ogg_nuovo(c, EXJS_CL_FUNZIONE);
    ExJsOggetto *O = exjs_ogg(c, i);

    if (!O) return V_INDEF;
    O->nativa = f;
    O->dato   = dato;
    O->nome   = nome ? exjs_arena_metti(c, nome, lung(nome)) : 0;
    return exjs_da_oggetto(i);
}

/* =============================================================================
 * IL TIPO
 * ========================================================================== */
int exjs_tipo(ExJsCtx *c, ExJsVal v)
{
    if (E_DOPPIO(v))  return EXJS_NUMERO;
    if (v == V_INDEF) return EXJS_INDEFINITO;
    if (v == V_NULLO) return EXJS_NULLO;
    if (v == V_FALSO || v == V_VERO) return EXJS_BOOLEANO;

    if (E_PUNT(v)) {
        if (PUNT_TAG(v) == T_STR) return EXJS_STRINGA;
        {
            ExJsOggetto *O = exjs_ogg(c, (int)PUNT_IDX(v));
            if (O && O->classe == EXJS_CL_FUNZIONE) return EXJS_FUNZIONE;
        }
        return EXJS_OGGETTO;
    }
    /* Un NaN vero, che non nasconde niente. */
    return EXJS_NUMERO;
}

/* =============================================================================
 * LE CONVERSIONI
 *
 * ! LA VERITA' DI JavaScript: sono falsi `false`, `0`, `NaN`, `""`, `null` e
 * `undefined`. TUTTO il resto e' vero — compreso `"0"`, compreso `[]`, e
 * compreso `new Boolean(false)`. Chi sbaglia questo elenco scrive `if` che
 * prendono il ramo sbagliato senza dare errore.
 * ========================================================================== */
int exjs_a_booleano(ExJsCtx *c, ExJsVal v)
{
    if (E_DOPPIO(v)) { double d = a_doppio(v); return d != 0.0 && d == d; }
    if (v == V_VERO)  return 1;
    if (v == V_FALSO || v == V_INDEF || v == V_NULLO) return 0;
    if (E_PUNT(v) && PUNT_TAG(v) == T_STR)
        return exjs_arena_leggi(c, PUNT_IDX(v))[0] != '\0';
    return 1;                                   /* ogni oggetto e' vero */
}

/* Da testo a numero, con le regole di JavaScript: spazi attorno consentiti,
 * stringa vuota = 0, e qualunque coda che non sia un numero = NaN. */
static double testo_a_numero(const char *s)
{
    double v = 0.0, f;
    int    segno = 1, cifre = 0;
    unsigned int i = 0;

    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
    if (s[i] == '\0') return 0.0;               /* "" e "  " fanno zero */

    if (s[i] == '+' || s[i] == '-') { if (s[i] == '-') segno = -1; i++; }

    if (s[i] == '0' && (s[i+1] == 'x' || s[i+1] == 'X')) {
        i += 2;
        while ((s[i] >= '0' && s[i] <= '9') ||
               ((s[i] | 0x20) >= 'a' && (s[i] | 0x20) <= 'f')) {
            int d = (s[i] <= '9') ? s[i] - '0' : ((s[i] | 0x20) - 'a') + 10;
            v = v * 16.0 + d;
            i++; cifre++;
        }
    } else {
        while (s[i] >= '0' && s[i] <= '9') { v = v*10.0 + (s[i]-'0'); i++; cifre++; }
        if (s[i] == '.') {
            double scala = 1.0;
            i++;
            while (s[i] >= '0' && s[i] <= '9') {
                scala *= 0.1; v += (s[i]-'0') * scala; i++; cifre++;
            }
        }
        if (cifre && (s[i] == 'e' || s[i] == 'E')) {
            int es = 0, sg = 1, c2 = 0;
            i++;
            if (s[i] == '+' || s[i] == '-') { if (s[i]=='-') sg = -1; i++; }
            while (s[i] >= '0' && s[i] <= '9') { es = es*10 + (s[i]-'0'); i++; c2++; if (es > 4096) es = 4096; }
            if (!c2) return 0.0/0.0;
            f = 1.0; while (es--) f *= 10.0;
            v = (sg > 0) ? v * f : v / f;
        }
    }

    if (!cifre) return 0.0/0.0;
    while (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r') i++;
    /* ! UNA CODA CHE NON E' SPAZIO RENDE NaN, e non «il numero letto finora».
     * `parseInt("12abc")` fa 12, ma `Number("12abc")` fa NaN: sono due
     * funzioni diverse, e questa e' la seconda. */
    if (s[i] != '\0') return 0.0/0.0;

    return segno * v;
}

double exjs_a_numero(ExJsCtx *c, ExJsVal v)
{
    if (E_DOPPIO(v)) return a_doppio(v);
    if (v == V_VERO)  return 1.0;
    if (v == V_FALSO) return 0.0;
    if (v == V_NULLO) return 0.0;               /* null fa zero, undefined NaN */
    if (v == V_INDEF) return 0.0/0.0;

    if (E_PUNT(v) && PUNT_TAG(v) == T_STR)
        return testo_a_numero(exjs_arena_leggi(c, PUNT_IDX(v)));

    /* Un oggetto diventa prima testo e poi numero: `[5]` fa cinque perche' il
     * suo testo e' "5". Vedi exjs_a_stringa. */
    return testo_a_numero(exjs_a_stringa(c, v));
}

/* -----------------------------------------------------------------------------
 * Da numero a testo
 *
 * ! NON SI USA printf: il motore gira anche dentro il kernel di un browser che
 * non se la porta dietro, e soprattutto le regole di JavaScript non sono
 * quelle di `%g`. Un intero si scrive senza virgola (`1`, non `1.000000`),
 * l'infinito si scrive «Infinity», e NaN si scrive «NaN».
 * --------------------------------------------------------------------------- */
static void numero_a_testo(double d, char *out, unsigned int max)
{
    char         cifre[32];
    unsigned int n = 0, i;
    int          neg = 0;
    double       ip;

    if (max < 8) { if (max) out[0] = '\0'; return; }

    if (d != d) { out[0]='N'; out[1]='a'; out[2]='N'; out[3]='\0'; return; }

    if (d < 0) { neg = 1; d = -d; }

    if (d > 1.7e308) {
        i = 0;
        if (neg) out[i++] = '-';
        out[i++]='I'; out[i++]='n'; out[i++]='f'; out[i++]='i';
        out[i++]='n'; out[i++]='i'; out[i++]='t'; out[i++]='y'; out[i]='\0';
        return;
    }

    /* La parte intera, a rovescio. */
    ip = d;
    {
        double t = ip;
        if (t < 1.0) cifre[n++] = '0';
        while (t >= 1.0 && n < sizeof(cifre)) {
            double q = t / 10.0;
            double r;
            /* pavimento a mano: il motore non tira dentro math.h per questo */
            double qi = (double)(long long)q;
            r = t - qi * 10.0;
            if (r >= 10.0) { r -= 10.0; qi += 1.0; }
            cifre[n++] = (char)('0' + (int)r);
            t = qi;
        }
    }

    i = 0;
    if (neg && i + 1 < max) out[i++] = '-';
    while (n > 0 && i + 1 < max) out[i++] = cifre[--n];

    /* La parte frazionaria: al massimo sei cifre, e gli zeri finali si
     * tolgono — `0.5` non e' `0.500000`. */
    {
        double fr = d - (double)(long long)d;
        if (fr > 1e-12) {
            unsigned int start = i, k;

            if (i + 1 < max) out[i++] = '.';
            for (k = 0; k < 6 && i + 1 < max; k++) {
                int c2;
                fr *= 10.0;
                c2 = (int)fr;
                if (c2 > 9) c2 = 9;
                out[i++] = (char)('0' + c2);
                fr -= c2;
            }
            while (i > start + 1 && out[i-1] == '0') i--;
            if (i > start && out[i-1] == '.') i--;
        }
    }
    out[i] = '\0';
}

const char *exjs_a_stringa(ExJsCtx *c, ExJsVal v)
{
    if (E_DOPPIO(v)) {
        numero_a_testo(a_doppio(v), c->scratch, sizeof(c->scratch));
        return c->scratch;
    }
    if (v == V_INDEF) return "undefined";
    if (v == V_NULLO) return "null";
    if (v == V_VERO)  return "true";
    if (v == V_FALSO) return "false";

    if (E_PUNT(v)) {
        if (PUNT_TAG(v) == T_STR) return exjs_arena_leggi(c, PUNT_IDX(v));
        {
            ExJsOggetto *O = exjs_ogg(c, (int)PUNT_IDX(v));
            if (O && O->classe == EXJS_CL_FUNZIONE) return "function";
            if (O && O->classe == EXJS_CL_VETTORE)  return exjs_vettore_testo(c, v);
        }
        return "[object Object]";
    }
    return "NaN";
}

/* =============================================================================
 * LE PROPRIETA'
 *
 * ! SI CERCANO RISALENDO IL PROTOTIPO, e questa e' l'unica ereditarieta' che
 * JavaScript ha. La stessa catena serve anche agli AMBITI delle variabili: un
 * ambito e' un oggetto il cui prototipo e' l'ambito che lo racchiude, e cosi'
 * cercare una variabile e cercare una proprieta' sono la stessa funzione. Non
 * e' un trucco: e' come il linguaggio e' fatto.
 * ========================================================================== */
static int prop_trova(ExJsCtx *c, int ogg, const char *nome, int risali)
{
    while (ogg >= 0) {
        ExJsOggetto *O = exjs_ogg(c, ogg);
        int          p;

        if (!O) return -1;
        for (p = O->prima_prop; p >= 0; p = c->prop[p].prossima) {
            const char  *n = exjs_arena_leggi(c, c->prop[p].nome);
            unsigned int i = 0;

            while (n[i] && n[i] == nome[i]) i++;
            if (n[i] == '\0' && nome[i] == '\0') return p;
        }
        if (!risali) return -1;
        ogg = O->proto;
    }
    return -1;
}

int exjs_prop_trova(ExJsCtx *c, int ogg, const char *nome, int risali)
{
    return prop_trova(c, ogg, nome, risali);
}

ExJsVal exjs_prop_val(ExJsCtx *c, int p)
{
    return (p >= 0 && p < (int)c->prop_n) ? c->prop[p].valore : V_INDEF;
}

void exjs_prop_metti_val(ExJsCtx *c, int p, ExJsVal v)
{
    if (p >= 0 && p < (int)c->prop_n) c->prop[p].valore = v;
}

int exjs_metti(ExJsCtx *c, ExJsVal ogg, const char *nome, ExJsVal v)
{
    int i = exjs_a_oggetto(ogg), p;

    if (i < 0) return 0;

    /* ! SI CERCA SENZA RISALIRE. Assegnare a `o.x` quando `x` sta nel
     * prototipo NON cambia il prototipo: crea una proprieta' su `o` che lo
     * copre. Risalire vorrebbe dire che scrivere su un oggetto cambia tutti
     * quelli che condividono il prototipo. */
    p = prop_trova(c, i, nome, 0);
    if (p >= 0) { c->prop[p].valore = v; return 1; }

    if (c->prop_n >= c->prop_max) { c->finita = 1; return 0; }
    {
        ExJsOggetto *O = exjs_ogg(c, i);
        unsigned int off = exjs_arena_metti(c, nome, lung(nome));
        int          np  = (int)c->prop_n++;

        c->prop[np].nome     = off;
        c->prop[np].valore   = v;
        c->prop[np].prossima = O->prima_prop;
        O->prima_prop = np;
    }
    return 1;
}

ExJsVal exjs_prendi(ExJsCtx *c, ExJsVal ogg, const char *nome)
{
    int i = exjs_a_oggetto(ogg), p;

    /* ! UN VALORE PRIMITIVO CERCA SUL PROPRIO PROTOTIPO. Il perche' sta
     * accanto a proto_str nella struttura: e' cosi' che `'abc'.length` e
     * `'abc'.indexOf` possono esistere su una cosa che oggetto non e'. */
    if (i < 0) {
        int pr = -1;

        switch (exjs_tipo(c, ogg)) {
        case EXJS_STRINGA: pr = c->proto_str; break;
        case EXJS_NUMERO:  pr = c->proto_num; break;
        default: return V_INDEF;
        }
        p = prop_trova(c, pr, nome, 1);
        return (p >= 0) ? c->prop[p].valore : V_INDEF;
    }

    /* `length` non e' una proprieta' come le altre su un vettore: e' un conto,
     * e deve seguire gli elementi anche quando nessuno l'ha mai scritta. */
    {
        ExJsOggetto *O = exjs_ogg(c, i);
        if (O && O->classe == EXJS_CL_VETTORE &&
            nome[0]=='l'&&nome[1]=='e'&&nome[2]=='n'&&nome[3]=='g'&&
            nome[4]=='t'&&nome[5]=='h'&&nome[6]=='\0')
            return exjs_numero(c, (double)O->lunghezza);
    }

    p = prop_trova(c, i, nome, 1);
    if (p >= 0) return c->prop[p].valore;

    /* ! IL PROTOTIPO DI UN VETTORE NON SI AGGANCIA ALLA CREAZIONE, si consulta
     * qui. Agganciarlo vorrebbe dire una proprieta' `proto` scritta in ogni
     * vettore appena nato — e i vettori nascono a migliaia dentro un ciclo. */
    {
        ExJsOggetto *O = exjs_ogg(c, i);
        int pr = -1;

        if (O && O->classe == EXJS_CL_VETTORE)       pr = c->proto_vet;
        else if (O && O->classe == EXJS_CL_FUNZIONE) pr = c->proto_fun;
        if (pr >= 0) {
            p = prop_trova(c, pr, nome, 1);
            if (p >= 0) return c->prop[p].valore;
        }
    }
    return V_INDEF;
}

unsigned int exjs_ora(ExJsCtx *c)              { return c ? c->ora_ms : 0; }
void         exjs_ora_metti(ExJsCtx *c, unsigned int t) { if (c) c->ora_ms = t; }

int  exjs_ast_pronto(ExJsCtx *c) { return c->ast_pronto; }
void exjs_ast_segna(ExJsCtx *c)  { c->ast_pronto = 1; }

/* =============================================================================
 * LA CODA DEI LAVORI
 *
 * ! IL TEMPO ARRIVA DA FUORI, e il perche' sta in exjs.h: una libreria che
 * chiedesse l'ora all'orologio darebbe prove che passano oggi e falliscono
 * domani. Qui ci sono soltanto delle scadenze da confrontare con un numero che
 * porta chi pompa.
 *
 * ! E L'IDENTIFICATIVO NON E' L'INDICE DELLA CASELLA. Se lo fosse, disdire un
 * lavoro finito e poi rimpiazzato disdirebbe quello NUOVO — un difetto che
 * compare solo quando la coda si riusa, cioe' dopo un po' che la pagina gira.
 * Il numero cresce e non torna indietro.
 * ========================================================================== */
unsigned int exjs_accoda(ExJsCtx *c, ExJsVal f, unsigned int quando_ms,
                         unsigned int ripeti_ms)
{
    unsigned int i;

    if (!c) return 0;
    for (i = 0; i < c->lavori_max; i++) {
        if (c->lavori[i].usato) continue;

        c->lavori[i].usato     = 1;
        c->lavori[i].id        = c->prossimo_id++;
        c->lavori[i].funzione  = f;
        c->lavori[i].quando_ms = quando_ms;
        c->lavori[i].ripeti_ms = ripeti_ms;
        return c->lavori[i].id;
    }
    /* ! LA CODA PIENA SI DICE RENDENDO ZERO, e zero non e' mai un
     * identificativo valido: chi accoda puo' accorgersene, e chi non guarda si
     * ritrova un timer che non parte invece di uno che parte per sbaglio. */
    return 0;
}

void exjs_disdici(ExJsCtx *c, unsigned int id)
{
    unsigned int i;

    if (!c || !id) return;
    for (i = 0; i < c->lavori_max; i++)
        if (c->lavori[i].usato && c->lavori[i].id == id) {
            c->lavori[i].usato = 0;
            return;
        }
}

int exjs_lavori_in_attesa(ExJsCtx *c)
{
    unsigned int i, n = 0;

    if (!c) return 0;
    for (i = 0; i < c->lavori_max; i++) if (c->lavori[i].usato) n++;
    return (int)n;
}

/* Prende il lavoro scaduto piu' VECCHIO e lo toglie (o lo riaccoda, se si
 * ripete). Rende 0 quando non ce n'e' piu' nessuno da fare a quest'ora.
 *
 * ! IL PIU' VECCHIO PER PRIMO, e non il primo che si trova nel vettore: due
 * `setTimeout` con scadenze diverse devono partire in ORDINE DI SCADENZA, e le
 * caselle sono in ordine di creazione. */
int exjs_lavoro_scaduto(ExJsCtx *c, unsigned int ora_ms, ExJsVal *fuori)
{
    unsigned int i;
    int          scelto = -1;

    if (!c) return 0;

    for (i = 0; i < c->lavori_max; i++) {
        if (!c->lavori[i].usato) continue;
        if (c->lavori[i].quando_ms > ora_ms) continue;
        if (scelto < 0 ||
            c->lavori[i].quando_ms < c->lavori[scelto].quando_ms) scelto = (int)i;
    }
    if (scelto < 0) return 0;

    *fuori = c->lavori[scelto].funzione;

    if (c->lavori[scelto].ripeti_ms) {
        /* ! LA PROSSIMA SCADENZA SI CONTA DA ADESSO, non da quella di prima:
         * altrimenti un intervallo rimasto indietro — una pagina ferma mezzo
         * secondo — sparerebbe tutte le esecuzioni perse una dietro l'altra. */
        c->lavori[scelto].quando_ms = ora_ms + c->lavori[scelto].ripeti_ms;
    } else {
        c->lavori[scelto].usato = 0;
    }
    return 1;
}

int  exjs_base_gia_fatta(ExJsCtx *c) { return c->base_fatta; }
void exjs_base_segna(ExJsCtx *c)     { c->base_fatta = 1; }

void exjs_uscita_metti(ExJsCtx *c, ExJsUscita f, void *dato)
{
    if (!c) return;
    c->uscita = f;
    c->uscita_dato = dato;
}

void exjs_uscita_scrivi(ExJsCtx *c, const char *s, unsigned int n)
{
    if (c && c->uscita) c->uscita(s, n, c->uscita_dato);
}

/* Toglie gli elementi in coda: serve a `pop`. Non libera niente — non c'e'
 * raccoglitore — ma la lunghezza e' quella che conta per chi legge. */
void exjs_vettore_tronca(ExJsCtx *c, ExJsVal vet, unsigned int nuova)
{
    ExJsOggetto *O = exjs_ogg(c, exjs_a_oggetto(vet));
    if (O && O->classe == EXJS_CL_VETTORE && nuova <= O->lunghezza)
        O->lunghezza = nuova;
}

int  exjs_proto_str(ExJsCtx *c) { return c->proto_str; }
int  exjs_proto_vet(ExJsCtx *c) { return c->proto_vet; }
int  exjs_proto_num(ExJsCtx *c) { return c->proto_num; }
void exjs_ese_metti(ExJsCtx *c, void *e) { c->ese = e; }
void*exjs_ese_prendi(ExJsCtx *c) { return c->ese; }

/* ! UN GENERATORE PSEUDOCASUALE PROPRIO, e non quello del sistema: Math.random
 * dev'essere RIPETIBILE nelle prove — lo stesso seme, la stessa sequenza — e
 * un motore che chiedesse l'ora all'orologio darebbe prove che passano oggi e
 * falliscono domani. xorshift32: tre righe, nessuno stato globale. */
double exjs_random(ExJsCtx *c)
{
    unsigned int x = c->seme;

    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    c->seme = x;
    return (double)(x >> 8) / 16777216.0;
}

ExJsVal exjs_globale(ExJsCtx *c) { return exjs_da_oggetto(c->globale); }
int     exjs_globale_idx(ExJsCtx *c) { return c->globale; }

/* =============================================================================
 * CONCATENARE, e il tranello che c'e' dentro
 *
 * ! exjs_a_stringa RENDE UN POSTO DI SERVIZIO CHE LA CHIAMATA DOPO RISCRIVE, e
 * sta scritto nel contratto. Chiedere le due stringhe e poi usarle vorrebbe
 * dire che la prima e' gia' cambiata quando la si copia — e con i numeri
 * succede SEMPRE, perche' e' il caso in cui il posto di servizio si usa.
 * Percio' la prima si copia subito, prima di chiedere la seconda.
 * ========================================================================== */
ExJsVal exjs_concat(ExJsCtx *c, ExJsVal a, ExJsVal b)
{
    char         tmp[96];
    const char  *sa = exjs_a_stringa(c, a), *sb;
    unsigned int off, i, j;
    int          corta;

    for (i = 0; i + 1 < sizeof(tmp) && sa[i]; i++) tmp[i] = sa[i];
    corta = (sa[i] == '\0');
    tmp[i] = '\0';

    off = c->arena_n;

    /* La prima: dalla copia se ci stava, altrimenti dall'arena (e allora era
     * gia' una stringa vera, che il posto di servizio non tocca). */
    {
        const char *s = corta ? tmp : sa;
        for (j = 0; s[j]; j++) {
            if (c->arena_n + 2 > c->arena_max) { c->finita = 1; return exjs_indefinito(); }
            c->arena[c->arena_n++] = s[j];
        }
    }

    sb = exjs_a_stringa(c, b);
    for (j = 0; sb[j]; j++) {
        if (c->arena_n + 2 > c->arena_max) { c->finita = 1; return exjs_indefinito(); }
        c->arena[c->arena_n++] = sb[j];
    }
    c->arena[c->arena_n++] = '\0';
    return fai_punt(T_STR, off);
}

/* Il giro sulle proprieta' di un oggetto, per `for..in`. Si danno gli indici e
 * non i puntatori: una proprieta' nuova puo' spostare il vettore, e un
 * puntatore tenuto attraverso una chiamata sarebbe un difetto che compare solo
 * quando il ciclo ne aggiunge una. */
int exjs_prop_prima(ExJsCtx *c, int ogg)
{
    ExJsOggetto *O = exjs_ogg(c, ogg);
    return O ? O->prima_prop : -1;
}

int exjs_prop_prossima(ExJsCtx *c, int p)
{
    return (p >= 0 && p < (int)c->prop_n) ? c->prop[p].prossima : -1;
}

unsigned int exjs_prop_nome(ExJsCtx *c, int p)
{
    return (p >= 0 && p < (int)c->prop_n) ? c->prop[p].nome : 0;
}

/* =============================================================================
 * I VETTORI
 *
 * ! GLI ELEMENTI STANNO IN UN VETTORE DENSO, non fra le proprieta'. Tenerli
 * come proprieta' di nome "0", "1", "2" costerebbe una conversione da numero a
 * testo per ogni accesso e una scansione lineare per trovarli: un ciclo su
 * mille elementi diventerebbe un milione di confronti di stringa.
 *
 * ! E CRESCONO RADDOPPIANDO, ABBANDONANDO IL VECCHIO BLOCCO. Senza raccoglitore
 * lo spazio lasciato indietro non torna: e' il prezzo dichiarato di questo
 * scaglione, e si paga solo quando un vettore cresce oltre la sua capienza.
 * ========================================================================== */
static int vettore_capienza(ExJsCtx *c, ExJsOggetto *O, unsigned int serve)
{
    unsigned int nuova, i, vecchio;

    if (serve <= O->elem_cap) return 1;

    nuova = O->elem_cap ? O->elem_cap * 2 : 8;
    while (nuova < serve) nuova *= 2;

    if (c->elem_n + nuova > c->elem_max) { c->finita = 1; return 0; }

    vecchio = O->elem_off;
    O->elem_off = c->elem_n;
    c->elem_n  += nuova;

    for (i = 0; i < O->lunghezza; i++) c->elem[O->elem_off + i] = c->elem[vecchio + i];
    for (; i < nuova; i++)             c->elem[O->elem_off + i] = V_INDEF;

    O->elem_cap = nuova;
    return 1;
}

int exjs_indice_metti(ExJsCtx *c, ExJsVal vet, unsigned int i, ExJsVal v)
{
    int          k = exjs_a_oggetto(vet);
    ExJsOggetto *O = exjs_ogg(c, k);

    if (!O || O->classe != EXJS_CL_VETTORE) return 0;
    if (!vettore_capienza(c, O, i + 1)) return 0;

    /* I buchi in mezzo esistono e valgono `undefined`: `v[5]=1` su un vettore
     * vuoto da' lunghezza sei. */
    while (O->lunghezza < i) c->elem[O->elem_off + O->lunghezza++] = V_INDEF;

    c->elem[O->elem_off + i] = v;
    if (i + 1 > O->lunghezza) O->lunghezza = i + 1;
    return 1;
}

ExJsVal exjs_indice_prendi(ExJsCtx *c, ExJsVal vet, unsigned int i)
{
    int          k = exjs_a_oggetto(vet);
    ExJsOggetto *O = exjs_ogg(c, k);

    if (!O || O->classe != EXJS_CL_VETTORE || i >= O->lunghezza) return V_INDEF;
    return c->elem[O->elem_off + i];
}

unsigned int exjs_lunghezza(ExJsCtx *c, ExJsVal vet)
{
    ExJsOggetto *O = exjs_ogg(c, exjs_a_oggetto(vet));
    return (O && O->classe == EXJS_CL_VETTORE) ? O->lunghezza : 0;
}

/* Il testo di un vettore: gli elementi separati da virgola, e `null` e
 * `undefined` che diventano stringa vuota. `[1,null,2].toString()` fa "1,,2". */
const char *exjs_vettore_testo(ExJsCtx *c, ExJsVal vet)
{
    ExJsOggetto *O = exjs_ogg(c, exjs_a_oggetto(vet));
    unsigned int i, n = 0, off;

    if (!O) return "";
    off = c->arena_n;

    for (i = 0; i < O->lunghezza; i++) {
        ExJsVal e = c->elem[O->elem_off + i];
        const char *s;
        unsigned int j;

        if (i && !exjs_arena_metti(c, ",", 1)) return "";
        if (i) c->arena_n--;                 /* si concatena: via lo '\0' */

        if (e == V_INDEF || e == V_NULLO) continue;

        s = exjs_a_stringa(c, e);
        for (j = 0; s[j]; j++) {
            if (c->arena_n + 2 > c->arena_max) { c->finita = 1; return ""; }
            c->arena[c->arena_n++] = s[j];
        }
        c->arena[c->arena_n] = '\0';
        n++;
    }
    (void)n;
    if (c->arena_n >= c->arena_max) { c->finita = 1; return ""; }
    c->arena[c->arena_n++] = '\0';
    return c->arena + off;
}
