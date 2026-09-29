/* =============================================================================
 * lib/exjs/parse.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Da gettoni ad albero — il secondo pezzo di ExJs
 *
 * -----------------------------------------------------------------------------
 * ! DISCESA RICORSIVA, CON UNA TABELLA PER LE PRECEDENZE
 *
 * Gli operatori binari di JavaScript sono una ventina su undici livelli di
 * precedenza. Scriverne una funzione per livello — `somma()` che chiama
 * `prodotto()` che chiama `unario()` — vuol dire undici funzioni quasi
 * identiche, e undici posti in cui sbagliare quando si aggiunge un operatore.
 * Qui c'e' UNA funzione che sale di livello guardando una tabella, e la
 * tabella e' l'unico posto dove la precedenza e' scritta.
 *
 * ! E LA PRECEDENZA E' LA COSA CHE SI SBAGLIA IN SILENZIO. `a + b * c` con le
 * precedenze invertite non da' nessun errore: da' un numero diverso. Per
 * questo il banco di prova stampa l'albero come testo e lo confronta lettera
 * per lettera — e' l'unico modo di vedere una precedenza sbagliata.
 *
 * -----------------------------------------------------------------------------
 * ! IL PUNTO E VIRGOLA CHE NON C'E' SE LO METTE IL LINGUAGGIO, e non e' una
 * comodita' da tollerare: e' una regola che CAMBIA IL SIGNIFICATO.
 *
 * Le tre regole vere:
 *   1. se il gettone che non ci si aspettava e' preceduto da un a capo, si
 *      finge un punto e virgola;
 *   2. lo si finge anche davanti a `}` e alla fine del testo;
 *   3. dopo `return`, `break`, `continue`, e prima di `++`/`--` postfissi, un
 *      a capo CHIUDE l'istruzione — sempre, anche se la riga dopo comincia
 *      con qualcosa che starebbe benissimo li'.
 *
 * La terza e' quella feroce: `return` seguito da un a capo rende `undefined`
 * qualunque cosa venga dopo. Un motore che la ignorasse eseguirebbe programmi
 * diversi da quelli scritti, e senza dire niente.
 *
 * -----------------------------------------------------------------------------
 * ! QUELLO CHE NON C'E' ANCORA: `switch`, `try/catch`, `throw`, le etichette,
 * `with`, le espressioni regolari. I gettoni ci sono gia' — li legge lex.c —
 * ma qui danno un errore che DICE che non ci sono, invece di produrre un
 * albero storto. Un motore che analizza a meta' e' peggio di uno che rifiuta.
 * ============================================================================= */

#include "exjs_int.h"

/* =============================================================================
 * Lo stato del costruttore
 * ========================================================================== */
typedef struct {
    ExJsAst    *A;
    ExJsLex     L;
    ExJsErrore *err;
    int         rotto;          /* un errore c'e' gia' stato: si smette */
} Par;

/* -----------------------------------------------------------------------------
 * Errori e nodi
 * --------------------------------------------------------------------------- */
static void p_copia(char *dst, unsigned int max, const char *s)
{
    unsigned int i = 0;
    while (s[i] && i + 1 < max) { dst[i] = s[i]; i++; }
    dst[i] = '\0';
}

/* Compone «atteso X, trovato Y» senza una printf: il motore non si porta
 * dietro la libc del sistema. */
static int p_errore(Par *P, const char *cosa, const char *invece)
{
    if (!P->rotto && P->err) {
        char        b[EXJS_ERR_LEN];
        unsigned int i = 0, j;

        for (j = 0; cosa[j] && i + 1 < sizeof(b); j++)   b[i++] = cosa[j];
        if (invece) {
            const char *t = ", trovato ";
            for (j = 0; t[j] && i + 1 < sizeof(b); j++)      b[i++] = t[j];
            for (j = 0; invece[j] && i + 1 < sizeof(b); j++) b[i++] = invece[j];
        }
        b[i] = '\0';

        P->err->riga      = P->L.t_riga;
        P->err->colonna   = P->L.t_colonna;
        P->err->posizione = P->L.inizio;
        p_copia(P->err->messaggio, EXJS_ERR_LEN, b);
    }
    P->rotto = 1;
    return -1;
}

static int nodo(Par *P, int tipo)
{
    ExJsAst  *A = P->A;
    ExJsNodo *N;
    int       i;

    if (P->rotto) return -1;
    if (A->nodi_n >= A->nodi_max) {
        A->troncato = 1;
        return p_errore(P, "lo script e' troppo grande per i nodi disponibili", 0);
    }

    i = (int)A->nodi_n++;
    N = &A->nodi[i];

    N->tipo = (unsigned char)tipo;
    N->op   = 0;
    N->a = N->b = N->c = N->d = -1;
    N->prossimo = -1;
    N->testo    = 0;
    N->numero   = 0.0;
    N->riga     = P->L.t_riga;
    return i;
}

/* Una stringa nell'arena. Rende lo scostamento; 0 e' l'arena vuota, quindi il
 * byte zero e' sempre uno '\0' messo da exjs_ast_prepara: cosi' `testo == 0`
 * vuol dire «stringa vuota» e non «nessuna stringa», e non serve un secondo
 * campo per distinguerli. */
static unsigned int arena(Par *P, const char *s, unsigned int n)
{
    ExJsAst     *A = P->A;
    unsigned int off, i;

    if (P->rotto) return 0;
    if (A->arena_n + n + 1 > A->arena_max) {
        A->troncato = 1;
        p_errore(P, "lo script e' troppo grande per l'arena dei nomi", 0);
        return 0;
    }

    off = A->arena_n;
    for (i = 0; i < n; i++) A->arena[off + i] = s[i];
    A->arena[off + n] = '\0';
    A->arena_n += n + 1;
    return off;
}

/* -----------------------------------------------------------------------------
 * Il flusso dei gettoni
 * --------------------------------------------------------------------------- */
static int tk(Par *P)   { return P->L.tipo; }

static void avanti(Par *P)
{
    if (P->rotto) return;
    if (exjs_lex_avanti(&P->L) == TK_ERRORE) P->rotto = 1;
}

static int accetta(Par *P, int t)
{
    if (P->rotto || P->L.tipo != t) return 0;
    avanti(P);
    return 1;
}

static int pretendi(Par *P, int t)
{
    if (P->rotto) return 0;
    if (P->L.tipo == t) { avanti(P); return 1; }
    {
        char b[48];
        const char *n = exjs_lex_nome(t);
        unsigned int i = 0, j;
        const char *a = "atteso ";
        for (j = 0; a[j] && i + 1 < sizeof(b); j++) b[i++] = a[j];
        for (j = 0; n[j] && i + 1 < sizeof(b); j++) b[i++] = n[j];
        b[i] = '\0';
        p_errore(P, b, exjs_lex_nome(P->L.tipo));
    }
    return 0;
}

/* =============================================================================
 * ! IL PUNTO E VIRGOLA AUTOMATICO, in un posto solo
 *
 * Chiamata alla fine di ogni istruzione che ne vuole uno. Il perche' delle tre
 * regole sta in testa al file.
 * ========================================================================== */
static void punto_virgola(Par *P)
{
    if (P->rotto) return;
    if (accetta(P, ';')) return;
    if (P->L.tipo == '}' || P->L.tipo == TK_FINE) return;
    if (P->L.a_capo_prima) return;

    p_errore(P, "atteso ';' o un a capo", exjs_lex_nome(P->L.tipo));
}

/* =============================================================================
 * GUARDARE AVANTI (29 settembre 2026)
 *
 * ! SU UNA COPIA DEL LESSICO, che poi si butta. Serve dove un gettone solo non
 * basta a decidere: `(a, b) => ...` comincia come una parentesi, `nome:` come
 * un'espressione. L'errore eventuale si rimette com'era: se il testo e'
 * sbagliato, lo dira' la lettura vera.
 * ! E SOLO QUANDO IL GETTONE CORRENTE NON E' UNA STRINGA: il lessico scioglie
 * le stringhe in un posto solo, e la copia ci scriverebbe sopra.
 * ========================================================================== */
static int sbircia(Par *P)
{
    ExJsLex    c = P->L;
    ExJsErrore e;
    int        t;

    if (P->err) e = *P->err;
    t = exjs_lex_avanti(&c);
    if (P->err) *P->err = e;
    return t;
}

/* Il gettone corrente e' il nome `s`? (`of`, `get`, `static`: nomi, non
 * parole chiave, e contano solo in certi posti.) */
static int e_nome(Par *P, const char *s)
{
    unsigned int i, n = P->L.fine - P->L.inizio;
    const char  *t = P->L.sorgente + P->L.inizio;

    if (tk(P) != TK_NOME) return 0;
    for (i = 0; i < n; i++) if (s[i] != t[i]) return 0;
    return s[n] == '\0';
}

/* Il primo carattere dopo il gettone corrente, saltando gli spazi (non i
 * commenti): per `x =>` e per `nome:`, che si chiedono a ogni nome letto e
 * non possono costare un secondo giro del lessico. `due` riceve quello dopo. */
static char dopo(Par *P, char *due)
{
    unsigned int j = P->L.pos;
    const char  *s = P->L.sorgente;

    while (j < P->L.n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) j++;
    *due = (j + 1 < P->L.n) ? s[j + 1] : '\0';
    return (j < P->L.n) ? s[j] : '\0';
}

/* `(` ... `)` seguito da `=>`? */
static int e_freccia(Par *P)
{
    ExJsLex    c = P->L;
    ExJsErrore e;
    int        prof = 0, t = tk(P), si = 0;

    if (P->err) e = *P->err;
    for (;;) {
        if (t == '(' || t == '[' || t == '{') prof++;
        else if (t == ')' || t == ']' || t == '}') { if (--prof == 0) break; }
        else if (t == TK_FINE || t == TK_ERRORE) break;
        t = exjs_lex_avanti(&c);
    }
    if (prof == 0 && t == ')') si = (exjs_lex_avanti(&c) == TK_FRECCIA);
    if (P->err) *P->err = e;
    return si;
}

/* =============================================================================
 * LE PRECEDENZE
 *
 * ! UNA TABELLA, E UN POSTO SOLO. Il numero e' il livello: piu' alto lega piu'
 * stretto. Zero vuol dire «non e' un operatore binario».
 *
 * ! `in` E `instanceof` STANNO FRA I CONFRONTI, e non e' un dettaglio
 * esotico: `for (k in o)` e' un'altra cosa, e chi costruisce il `for` deve
 * poter SPEGNERE `in` mentre legge la parte d'inizializzazione, o
 * `for (var k in o)` verrebbe letto come un confronto. Vedi `senza_in`.
 * ========================================================================== */
static int precedenza(int t, int senza_in)
{
    switch (t) {
    case TK_O_O: case TK_NULLISH:                   return 1;
    case TK_E_E:                                    return 2;
    case '|':                                       return 3;
    case '^':                                       return 4;
    case '&':                                       return 5;
    case TK_UGUALE: case TK_DIVERSO:
    case TK_ID_UGUALE: case TK_ID_DIVERSO:          return 6;
    case TK_IN:            return senza_in ? 0 : 7;
    case TK_INSTANCEOF:                             return 7;
    case '<': case '>': case TK_MIN_UG: case TK_MAG_UG: return 7;
    case TK_SHL: case TK_SHR: case TK_SHR_U:        return 8;
    case '+': case '-':                             return 9;
    case '*': case '/': case '%':                   return 10;
    case TK_POT:                                    return 11;
    default:                                        return 0;
    }
}

static int e_assegnazione(int t)
{
    switch (t) {
    case '=': case TK_PIU_UG: case TK_MENO_UG: case TK_PER_UG:
    case TK_DIV_UG: case TK_MOD_UG: case TK_SHL_UG: case TK_SHR_UG:
    case TK_SHR_U_UG: case TK_AND_UG: case TK_OR_UG: case TK_XOR_UG:
    case TK_POT_UG: case TK_NULLISH_UG: case TK_E_E_UG: case TK_O_O_UG:
        return 1;
    default:
        return 0;
    }
}

/* -----------------------------------------------------------------------------
 * Dichiarazioni in avanti: le espressioni e le istruzioni si chiamano a vicenda
 * (una funzione dentro un'espressione contiene istruzioni).
 * --------------------------------------------------------------------------- */
static int espressione(Par *P, int senza_in);
static int assegnazione(Par *P, int senza_in);
static int istruzione(Par *P);
static int blocco(Par *P);
static int funzione(Par *P, int e_dichiarazione);
static int funzione_resto(Par *P, unsigned int nome);
static int primaria(Par *P);
static int con_coda(Par *P);
static int classe(Par *P, int e_dichiarazione);
static int modello(Par *P);

/* =============================================================================
 * LE ESPRESSIONI PRIMARIE
 * ========================================================================== */
static int lista_argomenti(Par *P, int *primo)
{
    int ultimo = -1;

    *primo = -1;
    if (!pretendi(P, '(')) return 0;

    if (!accetta(P, ')')) {
        for (;;) {
            int e;

            if (tk(P) == ')') break;                /* virgola finale */
            if (accetta(P, TK_PUNTINI)) {
                int x = assegnazione(P, 0);
                e = nodo(P, N_ESPANDI);
                if (e >= 0) P->A->nodi[e].a = x;
            } else e = assegnazione(P, 0);
            if (P->rotto) return 0;
            if (*primo < 0) *primo = e; else P->A->nodi[ultimo].prossimo = e;
            ultimo = e;
            if (accetta(P, ',')) continue;
            break;
        }
        if (!pretendi(P, ')')) return 0;
    }
    return 1;
}

/* =============================================================================
 * I PARAMETRI (29 settembre 2026): un nome, un modello, un valore predefinito,
 * il resto. Stessa lettura per le funzioni, i metodi e le frecce.
 * ========================================================================== */
static int parametro(Par *P)
{
    int p = nodo(P, N_PARAMETRO), resto = 0;

    if (p < 0) return -1;
    if (accetta(P, TK_PUNTINI)) resto = 1;
    if (tk(P) == TK_NOME) {
        P->A->nodi[p].testo = arena(P, P->L.sorgente + P->L.inizio, P->L.fine - P->L.inizio);
        avanti(P);
    } else if (tk(P) == '{' || tk(P) == '[') {
        int m = primaria(P);
        if (P->rotto) return -1;
        P->A->nodi[p].b = m;
    } else {
        p_errore(P, "atteso il nome di un parametro", exjs_lex_nome(tk(P)));
        return -1;
    }
    if (!resto && accetta(P, '=')) P->A->nodi[p].a = assegnazione(P, 0);
    P->A->nodi[p].op = (unsigned char)resto;
    return P->rotto ? -1 : p;
}

static int parametri(Par *P, int *primo)
{
    int ultimo = -1;

    *primo = -1;
    if (!pretendi(P, '(')) return 0;
    while (!P->rotto && tk(P) != ')') {
        int p = parametro(P);
        if (P->rotto) return 0;
        if (*primo < 0) *primo = p; else P->A->nodi[ultimo].prossimo = p;
        ultimo = p;
        if (!accetta(P, ',')) break;
    }
    return pretendi(P, ')');
}

/* Dopo i parametri: `=> espressione` o `=> { ... }`. */
static int freccia_corpo(Par *P, int primo)
{
    int corpo, n;

    if (!pretendi(P, TK_FRECCIA)) return -1;
    if (tk(P) == '{') corpo = blocco(P);
    else {
        int e = assegnazione(P, 0), r;

        r     = nodo(P, N_RITORNA);
        corpo = nodo(P, N_BLOCCO);
        if (P->rotto) return -1;
        P->A->nodi[r].a     = e;
        P->A->nodi[corpo].a = r;
    }
    if (P->rotto) return -1;
    n = nodo(P, N_FUNZIONE);
    if (n < 0) return -1;
    P->A->nodi[n].a  = primo;
    P->A->nodi[n].b  = corpo;
    P->A->nodi[n].op = 1;
    return n;
}

/* La chiave di una voce: un nome (anche una parola chiave), una stringa, un
 * numero, o `[espressione]`, che finisce in *calc. */
static int chiave(Par *P, unsigned int *off, int *calc)
{
    *calc = -1;
    *off  = 0;
    if (accetta(P, '[')) {
        *calc = assegnazione(P, 0);
        return pretendi(P, ']');
    }
    /* ! LA CHIAVE PUO' ESSERE UN NOME, UNA STRINGA O UN NUMERO, e anche una
     * PAROLA CHIAVE: `{if: 1}` e' legale, e le pagine vere lo usano.
     * Rifiutarla vorrebbe dire non saper leggere oggetti scritti da un
     * minificatore. */
    if (tk(P) == TK_STRINGA)
        *off = arena(P, P->L.testo, P->L.testo_n);
    else if (tk(P) == TK_NUMERO || tk(P) == TK_NOME || tk(P) >= TK_VAR)
        *off = arena(P, P->L.sorgente + P->L.inizio, P->L.fine - P->L.inizio);
    else {
        p_errore(P, "atteso il nome di una proprieta'", exjs_lex_nome(tk(P)));
        return 0;
    }
    avanti(P);
    return !P->rotto;
}

/* Una voce di un oggetto letterale o di una classe. Il significato dei campi
 * sta accanto a N_VOCE in exjs_int.h. */
static int voce_oggetto(Par *P, int in_classe)
{
    int          voce = nodo(P, N_VOCE), calc = -1, v = -1, tipo = 0, statico = 0, era_nome;
    unsigned int off = 0;

    if (voce < 0) return -1;
    if (!in_classe && accetta(P, TK_PUNTINI)) {
        P->A->nodi[voce].a  = assegnazione(P, 0);
        P->A->nodi[voce].op = 2;
        return P->rotto ? -1 : voce;
    }
    if (in_classe && e_nome(P, "static")) {
        int t2 = sbircia(P);
        if (t2 != '(' && t2 != '=' && t2 != ';' && t2 != '}') { statico = 1; avanti(P); }
    }
    if (e_nome(P, "get") || e_nome(P, "set")) {
        int t2 = sbircia(P);
        if (t2 != ':' && t2 != '(' && t2 != ',' && t2 != '}' && t2 != '=' && t2 != ';') {
            tipo = e_nome(P, "get") ? 3 : 4;
            avanti(P);
        }
    }
    era_nome = (tk(P) == TK_NOME);
    if (!chiave(P, &off, &calc)) return -1;

    if (tipo == 3 || tipo == 4 || tk(P) == '(') {
        v = funzione_resto(P, off);
    } else if (in_classe) {                         /* un campo: x = 1; */
        tipo = 5;
        if (accetta(P, '=')) v = assegnazione(P, 0);
        accetta(P, ';');
    } else if (accetta(P, ':')) {
        v = assegnazione(P, 0);
    } else if (era_nome && calc < 0) {
        /* {a} vuol dire {a: a}; {a = 1} vale solo in un modello */
        int nn = nodo(P, N_NOME);
        if (nn < 0) return -1;
        P->A->nodi[nn].testo = off;
        v = nn;
        if (accetta(P, '=')) {
            int d = assegnazione(P, 0), as = nodo(P, N_ASSEGNA);
            if (as < 0) return -1;
            P->A->nodi[as].op = '=';
            P->A->nodi[as].a  = nn;
            P->A->nodi[as].b  = d;
            v = as;
        }
    } else {
        p_errore(P, "atteso ':' dopo la chiave", exjs_lex_nome(tk(P)));
        return -1;
    }
    if (P->rotto) return -1;
    P->A->nodi[voce].testo = off;
    P->A->nodi[voce].a     = v;
    P->A->nodi[voce].b     = calc;
    P->A->nodi[voce].c     = statico;
    P->A->nodi[voce].op    = (unsigned char)tipo;
    return voce;
}

static int primaria(Par *P)
{
    int n;

    if (P->rotto) return -1;

    switch (tk(P)) {
    case TK_NUMERO:
        n = nodo(P, N_NUMERO);
        if (n >= 0) P->A->nodi[n].numero = P->L.numero;
        avanti(P);
        return n;

    case TK_STRINGA: {
        unsigned int off;
        /* ! L'ARENA SI RIEMPIE PRIMA DEL NODO, e l'ordine conta: arena() puo'
         * fallire, e un nodo gia' creato con dentro uno scostamento che non
         * esiste e' peggio di nessun nodo. */
        off = arena(P, P->L.testo, P->L.testo_n);
        n = nodo(P, N_STRINGA);
        if (n >= 0) P->A->nodi[n].testo = off;
        avanti(P);
        return n;
    }

    case TK_NOME: {
        char         d2;
        unsigned int off = arena(P, P->L.sorgente + P->L.inizio,
                                 P->L.fine - P->L.inizio);
        if (dopo(P, &d2) == '=' && d2 == '>') {     /* x => ... */
            int p = nodo(P, N_PARAMETRO);
            if (p >= 0) P->A->nodi[p].testo = off;
            avanti(P);
            return freccia_corpo(P, p);
        }
        n = nodo(P, N_NOME);
        if (n >= 0) P->A->nodi[n].testo = off;
        avanti(P);
        return n;
    }

    case TK_TRUE:  n = nodo(P, N_VERO);   avanti(P); return n;
    case TK_FALSE: n = nodo(P, N_FALSO);  avanti(P); return n;
    case TK_NULL:  n = nodo(P, N_NULLO);  avanti(P); return n;
    case TK_THIS:  n = nodo(P, N_QUESTO); avanti(P); return n;

    case TK_FUNCTION:
        return funzione(P, 0);

    case TK_CLASS:
        return classe(P, 0);

    case TK_MODELLO:
        return modello(P);

    case TK_SUPER:
        n = nodo(P, N_SUPER);
        avanti(P);
        return n;

    case '(': {
        int e;
        if (e_freccia(P)) {                         /* (a, b) => ... */
            int primo;
            if (!parametri(P, &primo)) return -1;
            return freccia_corpo(P, primo);
        }
        avanti(P);
        e = espressione(P, 0);
        pretendi(P, ')');
        return e;
    }

    case '[': {
        int primo = -1, ultimo = -1;

        n = nodo(P, N_VETTORE);
        avanti(P);
        if (!accetta(P, ']')) {
            for (;;) {
                int e;

                /* ! I BUCHI ESISTONO: `[1, , 3]` ha tre elementi e quello di
                 * mezzo e' `undefined`. Trattarli come una virgola di troppo
                 * cambierebbe la lunghezza del vettore. */
                if (tk(P) == ',') e = nodo(P, N_NULLO);
                else if (accetta(P, TK_PUNTINI)) {
                    int x = assegnazione(P, 0);
                    e = nodo(P, N_ESPANDI);
                    if (e >= 0) P->A->nodi[e].a = x;
                }
                else              e = assegnazione(P, 0);
                if (P->rotto) return -1;

                if (primo < 0) primo = e; else P->A->nodi[ultimo].prossimo = e;
                ultimo = e;

                if (accetta(P, ',')) {
                    if (tk(P) == ']') { avanti(P); break; }   /* virgola finale */
                    continue;
                }
                if (!pretendi(P, ']')) return -1;
                break;
            }
        }
        if (n >= 0) P->A->nodi[n].a = primo;
        return n;
    }

    case '{': {
        int primo = -1, ultimo = -1;

        n = nodo(P, N_OGGETTO);
        avanti(P);
        while (!P->rotto && tk(P) != '}') {
            int voce = voce_oggetto(P, 0);

            if (P->rotto) return -1;
            if (primo < 0) primo = voce; else P->A->nodi[ultimo].prossimo = voce;
            ultimo = voce;
            if (!accetta(P, ',')) break;
        }
        if (!pretendi(P, '}')) return -1;
        if (n >= 0) P->A->nodi[n].a = primo;
        return n;
    }

    default:
        p_errore(P, "atteso un valore", exjs_lex_nome(tk(P)));
        return -1;
    }
}

/* =============================================================================
 * `.nome`, `[i]`, `(argomenti)` — la coda che si attacca a un valore
 *
 * ! `new` LEGA PIU' STRETTO DELLA CHIAMATA, e questo e' il punto in cui i
 * motori scritti in fretta sbagliano: `new a.b()` costruisce `a.b`, non `a`.
 * Percio' `new` legge la sua coda SENZA le chiamate, prende gli argomenti se
 * ci sono, e solo dopo la coda ricomincia.
 * ========================================================================== */
static int coda_vera(Par *P, int sin, int con_chiamate, int *opz);

/* ! UNA CATENA CON ?. HA UNA RADICE (N_CATENA): li' finisce il corto
 * circuito. `a?.b.c` con a undefined e' undefined tutto, non un errore su
 * `.c`; e la radice e' anche il punto dove il segno si spegne. */
static int coda(Par *P, int sin, int con_chiamate)
{
    int opz = 0, r = coda_vera(P, sin, con_chiamate, &opz), n;

    if (r < 0 || !opz) return r;
    n = nodo(P, N_CATENA);
    if (n >= 0) P->A->nodi[n].a = r;
    return n;
}

static int coda_vera(Par *P, int sin, int con_chiamate, int *opz)
{
    for (;;) {
        if (P->rotto) return -1;

        if (tk(P) == TK_OPZ) {
            int m;

            avanti(P);
            *opz = 1;
            if (tk(P) == '(') {
                int primo;
                if (!lista_argomenti(P, &primo)) return -1;
                m = nodo(P, N_CHIAMATA);
                if (m < 0) return -1;
                P->A->nodi[m].a = sin;
                P->A->nodi[m].b = primo;
            } else if (accetta(P, '[')) {
                int i = espressione(P, 0);
                if (!pretendi(P, ']')) return -1;
                m = nodo(P, N_INDICE);
                if (m < 0) return -1;
                P->A->nodi[m].a = sin;
                P->A->nodi[m].b = i;
            } else {
                unsigned int off;
                if (tk(P) != TK_NOME && tk(P) < TK_VAR) {
                    p_errore(P, "atteso un nome dopo '?.'", exjs_lex_nome(tk(P)));
                    return -1;
                }
                off = arena(P, P->L.sorgente + P->L.inizio, P->L.fine - P->L.inizio);
                avanti(P);
                m = nodo(P, N_MEMBRO);
                if (m < 0) return -1;
                P->A->nodi[m].a     = sin;
                P->A->nodi[m].testo = off;
            }
            P->A->nodi[m].op = 1;
            sin = m;
            continue;
        }

        if (tk(P) == '.') {
            unsigned int off;
            int m;

            avanti(P);
            /* Anche qui una parola chiave e' un nome legittimo: `o.default`. */
            if (tk(P) != TK_NOME && tk(P) < TK_VAR) {
                p_errore(P, "atteso il nome di una proprieta' dopo '.'",
                         exjs_lex_nome(tk(P)));
                return -1;
            }
            off = arena(P, P->L.sorgente + P->L.inizio,
                        P->L.fine - P->L.inizio);
            avanti(P);

            m = nodo(P, N_MEMBRO);
            if (m < 0) return -1;
            P->A->nodi[m].a     = sin;
            P->A->nodi[m].testo = off;
            sin = m;
            continue;
        }

        if (tk(P) == '[') {
            int i, m;

            avanti(P);
            i = espressione(P, 0);
            if (!pretendi(P, ']')) return -1;

            m = nodo(P, N_INDICE);
            if (m < 0) return -1;
            P->A->nodi[m].a = sin;
            P->A->nodi[m].b = i;
            sin = m;
            continue;
        }

        if (con_chiamate && tk(P) == '(') {
            int primo, m;

            if (!lista_argomenti(P, &primo)) return -1;
            m = nodo(P, N_CHIAMATA);
            if (m < 0) return -1;
            P->A->nodi[m].a = sin;
            P->A->nodi[m].b = primo;
            sin = m;
            continue;
        }

        return sin;
    }
}

static int nuovo(Par *P)
{
    int chi, primo = -1, n;

    avanti(P);                              /* `new` */

    /* Un `new` annidato: `new new F()()`. Raro, ma legale. */
    chi = (tk(P) == TK_NEW) ? nuovo(P) : primaria(P);
    if (P->rotto) return -1;

    chi = coda(P, chi, 0);                  /* niente chiamate: vedi sopra */
    if (P->rotto) return -1;

    if (tk(P) == '(' && !lista_argomenti(P, &primo)) return -1;

    n = nodo(P, N_NUOVO);
    if (n < 0) return -1;
    P->A->nodi[n].a = chi;
    P->A->nodi[n].b = primo;
    return n;
}

static int con_coda(Par *P)
{
    int sin;

    if (tk(P) == TK_NEW) sin = nuovo(P);
    else                 sin = primaria(P);

    if (P->rotto) return -1;
    return coda(P, sin, 1);
}

/* =============================================================================
 * Gli unari, e il postfisso
 * ========================================================================== */
static int unario(Par *P)
{
    int t = tk(P), n, a;

    if (P->rotto) return -1;

    if (t == '!' || t == '~' || t == '+' || t == '-' ||
        t == TK_TYPEOF || t == TK_DELETE || t == TK_VOID) {
        avanti(P);
        a = unario(P);
        n = nodo(P, N_UNARIO);
        if (n < 0) return -1;
        P->A->nodi[n].op = (unsigned char)t;
        P->A->nodi[n].a  = a;
        return n;
    }

    if (t == TK_PIU_PIU || t == TK_MENO_MENO) {
        avanti(P);
        a = unario(P);
        n = nodo(P, N_PRE);
        if (n < 0) return -1;
        P->A->nodi[n].op = (unsigned char)t;
        P->A->nodi[n].a  = a;
        return n;
    }

    a = con_coda(P);
    if (P->rotto) return -1;

    /* ! IL POSTFISSO NON ATTRAVERSA UN A CAPO. `a\n++b` sono due istruzioni,
     * non `a++ b`: e' la terza regola del punto e virgola automatico, e qui e'
     * l'unico posto dove si applica dentro un'espressione. */
    if ((tk(P) == TK_PIU_PIU || tk(P) == TK_MENO_MENO) && !P->L.a_capo_prima) {
        int op = tk(P);
        avanti(P);
        n = nodo(P, N_POST);
        if (n < 0) return -1;
        P->A->nodi[n].op = (unsigned char)op;
        P->A->nodi[n].a  = a;
        return n;
    }
    return a;
}

/* =============================================================================
 * I binari, per livelli
 *
 * ! SI SALE DI LIVELLO, NON SI SCENDE PER FUNZIONI. Il perche' sta in testa al
 * file: undici funzioni quasi identiche sono undici posti in cui sbagliare.
 * ========================================================================== */
static int binari(Par *P, int minimo, int senza_in)
{
    int sin = unario(P);

    if (P->rotto) return -1;

    for (;;) {
        int op = tk(P);
        int pr = precedenza(op, senza_in);
        int des, n;

        if (pr == 0 || pr < minimo) return sin;

        avanti(P);
        /* +1: gli operatori binari di JavaScript associano a sinistra —
         * tutti tranne `**`, che associa a destra: 2 ** 3 ** 2 e' 2 ** 9. */
        des = binari(P, op == TK_POT ? pr : pr + 1, senza_in);
        if (P->rotto) return -1;

        n = nodo(P, (op == TK_E_E || op == TK_O_O || op == TK_NULLISH) ? N_LOGICO : N_BINARIO);
        if (n < 0) return -1;
        P->A->nodi[n].op = (unsigned char)op;
        P->A->nodi[n].a  = sin;
        P->A->nodi[n].b  = des;
        sin = n;
    }
}

static int condizionale(Par *P, int senza_in)
{
    int prova = binari(P, 1, senza_in), n, si, no;

    if (P->rotto || tk(P) != '?') return prova;

    avanti(P);
    /* ! I DUE RAMI SONO ASSEGNAZIONI, NON ESPRESSIONI: `a ? b : c, d` e'
     * `(a?b:c), d` — la virgola sta fuori. E il ramo di mezzo ignora
     * `senza_in`, perche' li' dentro `in` e' di nuovo un operatore. */
    si = assegnazione(P, 0);
    if (!pretendi(P, ':')) return -1;
    no = assegnazione(P, senza_in);
    if (P->rotto) return -1;

    n = nodo(P, N_CONDIZIONE);
    if (n < 0) return -1;
    P->A->nodi[n].a = prova;
    P->A->nodi[n].b = si;
    P->A->nodi[n].c = no;
    return n;
}

static int assegnazione(Par *P, int senza_in)
{
    int sin = condizionale(P, senza_in), op, des, n;

    if (P->rotto) return -1;
    if (!e_assegnazione(tk(P))) return sin;

    op = tk(P);
    avanti(P);

    /* ! L'ASSEGNAZIONE ASSOCIA A DESTRA: `a = b = 1` e' `a = (b = 1)`. Per
     * questo si richiama se stessa invece di salire di livello. */
    des = assegnazione(P, senza_in);
    if (P->rotto) return -1;

    n = nodo(P, N_ASSEGNA);
    if (n < 0) return -1;
    P->A->nodi[n].op = (unsigned char)op;
    P->A->nodi[n].a  = sin;
    P->A->nodi[n].b  = des;
    return n;
}

static int espressione(Par *P, int senza_in)
{
    int sin = assegnazione(P, senza_in);

    while (!P->rotto && tk(P) == ',') {
        int des, n;

        avanti(P);
        des = assegnazione(P, senza_in);
        n   = nodo(P, N_VIRGOLA);
        if (n < 0) return -1;
        P->A->nodi[n].a = sin;
        P->A->nodi[n].b = des;
        sin = n;
    }
    return sin;
}

/* =============================================================================
 * LE FUNZIONI
 * ========================================================================== */
static int funzione_resto(Par *P, unsigned int nome)
{
    int primo, corpo, n;

    if (!parametri(P, &primo)) return -1;
    corpo = blocco(P);
    if (P->rotto) return -1;

    n = nodo(P, N_FUNZIONE);
    if (n < 0) return -1;
    P->A->nodi[n].testo = nome;
    P->A->nodi[n].a     = primo;
    P->A->nodi[n].b     = corpo;
    return n;
}

static int funzione(Par *P, int e_dichiarazione)
{
    unsigned int nome = 0;

    avanti(P);                          /* `function` */

    if (tk(P) == TK_NOME) {
        nome = arena(P, P->L.sorgente + P->L.inizio, P->L.fine - P->L.inizio);
        avanti(P);
    } else if (e_dichiarazione) {
        p_errore(P, "una funzione dichiarata vuole un nome", exjs_lex_nome(tk(P)));
        return -1;
    }
    return funzione_resto(P, nome);
}

/* =============================================================================
 * LE CLASSI (29 settembre 2026)
 *
 * Il costruttore c'e' sempre nell'albero: se non e' scritto se ne fa uno
 * vuoto, e in una classe derivata e' segnato (op 2) perche' passi gli
 * argomenti al padre, come dice la norma.
 * ========================================================================== */
static int classe(Par *P, int e_dichiarazione)
{
    unsigned int nome = 0;
    int          padre = -1, primo = -1, ultimo = -1, costr = -1, n;

    avanti(P);                          /* `class` */
    if (tk(P) == TK_NOME) {
        nome = arena(P, P->L.sorgente + P->L.inizio, P->L.fine - P->L.inizio);
        avanti(P);
    } else if (e_dichiarazione) {
        p_errore(P, "una classe dichiarata vuole un nome", exjs_lex_nome(tk(P)));
        return -1;
    }
    if (accetta(P, TK_EXTENDS)) padre = con_coda(P);
    if (!pretendi(P, '{')) return -1;

    while (!P->rotto && tk(P) != '}') {
        int v;

        if (accetta(P, ';')) continue;
        v = voce_oggetto(P, 1);
        if (P->rotto) return -1;
        {
            ExJsNodo *V = &P->A->nodi[v];
            const char *k = P->A->arena + V->testo;
            if (V->op == 0 && V->b < 0 && !V->c && V->testo &&
                k[0]=='c' && k[1]=='o' && k[2]=='n' && k[3]=='s' && k[4]=='t' &&
                k[5]=='r' && k[6]=='u' && k[7]=='c' && k[8]=='t' && k[9]=='o' &&
                k[10]=='r' && k[11]=='\0') {
                costr = V->a;
                continue;
            }
        }
        if (primo < 0) primo = v; else P->A->nodi[ultimo].prossimo = v;
        ultimo = v;
    }
    if (!pretendi(P, '}')) return -1;

    if (costr < 0) {
        int corpo = nodo(P, N_BLOCCO);
        costr = nodo(P, N_FUNZIONE);
        if (costr < 0 || corpo < 0) return -1;
        P->A->nodi[costr].b  = corpo;
        P->A->nodi[costr].op = (padre >= 0) ? 2 : 0;
    }
    P->A->nodi[costr].testo = nome;
    P->A->nodi[costr].op   |= 4;                /* e' un costruttore di classe */

    n = nodo(P, N_CLASSE);
    if (n < 0) return -1;
    P->A->nodi[n].testo = nome;
    P->A->nodi[n].a     = padre;
    P->A->nodi[n].b     = primo;
    P->A->nodi[n].c     = costr;
    return n;
}

/* =============================================================================
 * I MODELLI `testo ${espressione} testo` (29 settembre 2026)
 *
 * ! DIVENTANO UNA CATENA DI +, che comincia sempre da una stringa (magari
 * vuota): cosi' il + concatena anche quando la prima espressione e' un
 * numero. Le espressioni si leggono con un secondo costruttore sullo stesso
 * albero, puntato sul pezzo di sorgente fra `${` e `}`.
 * ========================================================================== */
static unsigned int modello_fine_espr(const char *s, unsigned int n, unsigned int i)
{
    int prof = 1;

    while (i < n) {
        char c = s[i];
        if (c == '{') prof++;
        else if (c == '}') { if (--prof == 0) return i; }
        else if (c == '"' || c == '\'' || c == '`') {
            char q = c;
            i++;
            while (i < n && s[i] != q) {
                if (s[i] == '\\') i++;
                else if (q == '`' && s[i] == '$' && i + 1 < n && s[i + 1] == '{') {
                    unsigned int f = modello_fine_espr(s, n, i + 2);
                    if (f >= n) return n;
                    i = f;
                }
                i++;
            }
        }
        i++;
    }
    return n;
}

static int esa1(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Il testo fra due espressioni, con gli scappamenti sciolti, dritto
 * nell'arena dell'albero (sciolto non e' mai piu' lungo dell'originale). */
static int modello_testo(Par *P, const char *s, unsigned int n)
{
    ExJsAst     *A = P->A;
    unsigned int off, i, k = 0;
    int          nd;

    if (P->rotto) return -1;
    if (A->arena_n + n + 1 > A->arena_max) {
        A->troncato = 1;
        return p_errore(P, "lo script e' troppo grande per l'arena dei nomi", 0);
    }
    off = A->arena_n;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < n) {
            char d = s[++i];
            switch (d) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            case 'r': c = '\r'; break;
            case '0': c = '\0'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            case 'v': c = '\v'; break;
            case '\n': continue;                    /* riga che continua */
            case 'x':
                if (i + 2 < n && esa1(s[i+1]) >= 0 && esa1(s[i+2]) >= 0) {
                    c = (char)(esa1(s[i+1]) * 16 + esa1(s[i+2]));
                    i += 2;
                } else c = 'x';
                break;
            case 'u':
                /* \uXXXX in UTF-8, come fanno le stringhe */
                if (i + 4 < n && esa1(s[i+1]) >= 0 && esa1(s[i+2]) >= 0 &&
                    esa1(s[i+3]) >= 0 && esa1(s[i+4]) >= 0) {
                    unsigned int u = (unsigned)(esa1(s[i+1]) << 12 | esa1(s[i+2]) << 8 |
                                                esa1(s[i+3]) << 4  | esa1(s[i+4]));
                    i += 4;
                    if (u < 0x80) c = (char)u;
                    else if (u < 0x800) {
                        A->arena[off + k++] = (char)(0xC0 | (u >> 6));
                        c = (char)(0x80 | (u & 0x3F));
                    } else {
                        A->arena[off + k++] = (char)(0xE0 | (u >> 12));
                        A->arena[off + k++] = (char)(0x80 | ((u >> 6) & 0x3F));
                        c = (char)(0x80 | (u & 0x3F));
                    }
                } else c = 'u';
                break;
            default: c = d; break;                  /* \` \$ \\ \' \" */
            }
        } else if (c == '\r') {
            if (i + 1 < n && s[i + 1] == '\n') continue;   /* CRLF e' un a capo */
            c = '\n';
        }
        A->arena[off + k++] = c;
    }
    A->arena[off + k] = '\0';
    A->arena_n += k + 1;
    nd = nodo(P, N_STRINGA);
    if (nd >= 0) P->A->nodi[nd].testo = off;
    return nd;
}

static int modello_unisci(Par *P, int acc, int x)
{
    int n;

    if (acc < 0) return x;
    n = nodo(P, N_BINARIO);
    if (n < 0) return -1;
    P->A->nodi[n].op = '+';
    P->A->nodi[n].a  = acc;
    P->A->nodi[n].b  = x;
    return n;
}

static int modello(Par *P)
{
    const char  *s = P->L.sorgente + P->L.inizio + 1;
    unsigned int n = P->L.fine - P->L.inizio - 2, i = 0, da = 0;
    int          acc = -1, riga = P->L.t_riga;

    while (!P->rotto) {
        while (i < n && !(s[i] == '$' && i + 1 < n && s[i + 1] == '{')) {
            if (s[i] == '\\') i++;
            i++;
        }
        if (i > n) i = n;
        acc = modello_unisci(P, acc, modello_testo(P, s + da, i - da));
        if (i >= n) break;
        {
            unsigned int f = modello_fine_espr(s, n, i + 2);
            Par          Q;
            int          e;

            if (f >= n) return p_errore(P, "${ senza } nel modello", 0);
            Q.A = P->A; Q.err = P->err; Q.rotto = 0;
            exjs_lex_apri(&Q.L, s + i + 2, f - (i + 2), P->L.testo, P->L.testo_max, P->err);
            Q.L.riga = riga;
            avanti(&Q);
            e = espressione(&Q, 0);
            if (!Q.rotto && tk(&Q) != TK_FINE)
                p_errore(&Q, "atteso } nel modello", exjs_lex_nome(tk(&Q)));
            if (Q.rotto) { P->rotto = 1; return -1; }
            acc = modello_unisci(P, acc, e);
            i = da = f + 1;
        }
    }
    if (P->rotto) return -1;
    avanti(P);
    return acc;
}

/* =============================================================================
 * LE ISTRUZIONI
 * ========================================================================== */
/* 1 se fra i nodi nati da `da` in poi c'e' una funzione o una classe: una
 * chiusura che puo' ricordarsi le variabili di questo giro. */
static int ha_chiusure(Par *P, unsigned int da)
{
    unsigned int i;
    for (i = da; i < P->A->nodi_n; i++)
        if (P->A->nodi[i].tipo == N_FUNZIONE || P->A->nodi[i].tipo == N_CLASSE) return 1;
    return 0;
}

/* 1 se fra queste istruzioni c'e' un let, un const o una classe: il blocco
 * avra' un ambito suo. */
static int ha_lessicali(Par *P, int n)
{
    for (; n >= 0; n = P->A->nodi[n].prossimo) {
        ExJsNodo *N = &P->A->nodi[n];
        if (N->tipo == N_VAR && N->op) return 1;
        if (N->tipo == N_CLASSE && N->testo) return 1;
    }
    return 0;
}

static int blocco(Par *P)
{
    int n, primo = -1, ultimo = -1;

    n = nodo(P, N_BLOCCO);
    if (!pretendi(P, '{')) return -1;

    while (!P->rotto && tk(P) != '}' && tk(P) != TK_FINE) {
        int s = istruzione(P);
        if (P->rotto) return -1;
        if (primo < 0) primo = s; else P->A->nodi[ultimo].prossimo = s;
        ultimo = s;
    }
    if (!pretendi(P, '}')) return -1;

    if (n >= 0) {
        P->A->nodi[n].a  = primo;
        P->A->nodi[n].op = (unsigned char)ha_lessicali(P, primo);
        if (P->A->nodi[n].op && ha_chiusure(P, (unsigned int)n + 1))
            P->A->nodi[n].op |= 2;
    }
    return n;
}

/* `var a = 1, b;` — rende il nodo N_VAR. `senza_in` serve dentro un `for`. */
static int dichiarazioni(Par *P, int senza_in)
{
    int n = nodo(P, N_VAR), primo = -1, ultimo = -1;
    int genere = (tk(P) == TK_LET) ? 1 : (tk(P) == TK_CONST) ? 2 : 0;

    avanti(P);                          /* `var`, `let`, `const` */
    if (n >= 0) P->A->nodi[n].op = (unsigned char)genere;

    for (;;) {
        unsigned int nome = 0;
        int          d, val = -1, mod = -1;

        if (tk(P) == '{' || tk(P) == '[') {
            mod = primaria(P);                      /* un modello */
            if (P->rotto) return -1;
        } else if (tk(P) != TK_NOME) {
            p_errore(P, "atteso il nome di una variabile", exjs_lex_nome(tk(P)));
            return -1;
        } else {
            nome = arena(P, P->L.sorgente + P->L.inizio, P->L.fine - P->L.inizio);
            avanti(P);
        }

        if (accetta(P, '=')) val = assegnazione(P, senza_in);
        if (P->rotto) return -1;

        d = nodo(P, N_DICHIARA);
        if (d < 0) return -1;
        P->A->nodi[d].testo = nome;
        P->A->nodi[d].a     = val;
        P->A->nodi[d].b     = mod;

        if (primo < 0) primo = d; else P->A->nodi[ultimo].prossimo = d;
        ultimo = d;

        if (accetta(P, ',')) continue;
        break;
    }

    if (n >= 0) P->A->nodi[n].a = primo;
    return n;
}

/* =============================================================================
 * ! IL `for` E' TRE ISTRUZIONI IN UNA, E UNA DI QUESTE E' `for..in`
 *
 * La difficolta' e' che non si sa quale sia finche' non si e' letta la parte
 * d'inizializzazione: `for (var k in o)` e `for (var i = 0; ...)` cominciano
 * uguali. Percio' la si legge con `in` SPENTO come operatore — vedi
 * precedenza() — e poi si guarda se il gettone successivo e' `in`.
 * ========================================================================== */
static int ciclo_for(Par *P)
{
    int          inizio = -1, n;
    unsigned int da = P->A->nodi_n;             /* per ha_chiusure */

    avanti(P);                          /* `for` */
    if (!pretendi(P, '(')) return -1;

    if (tk(P) == ';') {
        inizio = -1;
    } else if (tk(P) == TK_VAR || tk(P) == TK_LET || tk(P) == TK_CONST) {
        inizio = dichiarazioni(P, 1);
    } else {
        inizio = espressione(P, 1);
    }
    if (P->rotto) return -1;

    if (e_nome(P, "of")) {
        int oggetto, corpo;

        avanti(P);
        oggetto = assegnazione(P, 0);
        if (!pretendi(P, ')')) return -1;
        corpo = istruzione(P);
        if (P->rotto) return -1;

        n = nodo(P, N_PER_DI);
        if (n < 0) return -1;
        P->A->nodi[n].op = (unsigned char)ha_chiusure(P, da);
        P->A->nodi[n].a = inizio;
        P->A->nodi[n].b = oggetto;
        P->A->nodi[n].d = corpo;
        return n;
    }

    if (tk(P) == TK_IN) {
        int oggetto, corpo;

        avanti(P);
        oggetto = espressione(P, 0);
        if (!pretendi(P, ')')) return -1;
        corpo = istruzione(P);
        if (P->rotto) return -1;

        n = nodo(P, N_PER_IN);
        if (n < 0) return -1;
        P->A->nodi[n].op = (unsigned char)ha_chiusure(P, da);
        P->A->nodi[n].a = inizio;
        P->A->nodi[n].b = oggetto;
        P->A->nodi[n].d = corpo;
        return n;
    }

    {
        int prova = -1, passo = -1, corpo;

        if (!pretendi(P, ';')) return -1;
        if (tk(P) != ';') prova = espressione(P, 0);
        if (!pretendi(P, ';')) return -1;
        if (tk(P) != ')') passo = espressione(P, 0);
        if (!pretendi(P, ')')) return -1;

        corpo = istruzione(P);
        if (P->rotto) return -1;

        n = nodo(P, N_PER);
        if (n < 0) return -1;
        P->A->nodi[n].op = (unsigned char)ha_chiusure(P, da);
        P->A->nodi[n].a = inizio;
        P->A->nodi[n].b = prova;
        P->A->nodi[n].c = passo;
        P->A->nodi[n].d = corpo;
        return n;
    }
}

static int istruzione(Par *P)
{
    int n;

    if (P->rotto) return -1;

    switch (tk(P)) {
    case '{':   return blocco(P);
    case ';':   n = nodo(P, N_VUOTO); avanti(P); return n;

    case TK_VAR: case TK_LET: case TK_CONST:
        n = dichiarazioni(P, 0);
        punto_virgola(P);
        return n;

    case TK_FUNCTION:
        return funzione(P, 1);

    case TK_CLASS:
        return classe(P, 1);

    case TK_IF: {
        int prova, allora, altrimenti = -1;

        avanti(P);
        if (!pretendi(P, '(')) return -1;
        prova = espressione(P, 0);
        if (!pretendi(P, ')')) return -1;
        allora = istruzione(P);
        if (accetta(P, TK_ELSE)) altrimenti = istruzione(P);
        if (P->rotto) return -1;

        n = nodo(P, N_SE);
        if (n < 0) return -1;
        P->A->nodi[n].a = prova;
        P->A->nodi[n].b = allora;
        P->A->nodi[n].c = altrimenti;
        return n;
    }

    case TK_WHILE: {
        int prova, corpo;

        avanti(P);
        if (!pretendi(P, '(')) return -1;
        prova = espressione(P, 0);
        if (!pretendi(P, ')')) return -1;
        corpo = istruzione(P);
        if (P->rotto) return -1;

        n = nodo(P, N_MENTRE);
        if (n < 0) return -1;
        P->A->nodi[n].a = prova;
        P->A->nodi[n].b = corpo;
        return n;
    }

    case TK_DO: {
        int prova, corpo;

        avanti(P);
        corpo = istruzione(P);
        if (!pretendi(P, TK_WHILE)) return -1;
        if (!pretendi(P, '(')) return -1;
        prova = espressione(P, 0);
        if (!pretendi(P, ')')) return -1;
        accetta(P, ';');                /* qui il ';' e' proprio facoltativo */
        if (P->rotto) return -1;

        n = nodo(P, N_FAI);
        if (n < 0) return -1;
        P->A->nodi[n].a = prova;
        P->A->nodi[n].b = corpo;
        return n;
    }

    case TK_FOR:
        return ciclo_for(P);

    case TK_RETURN: {
        int val = -1;

        avanti(P);
        /* ! QUI STA LA REGOLA FEROCE: un a capo dopo `return` chiude
         * l'istruzione, e la funzione rende `undefined` qualunque cosa ci sia
         * sotto. Il perche' sta in testa al file. */
        if (tk(P) != ';' && tk(P) != '}' && tk(P) != TK_FINE &&
            !P->L.a_capo_prima)
            val = espressione(P, 0);
        punto_virgola(P);
        if (P->rotto) return -1;

        n = nodo(P, N_RITORNA);
        if (n < 0) return -1;
        P->A->nodi[n].a = val;
        return n;
    }

    case TK_BREAK: case TK_CONTINUE: {
        int          t = tk(P);
        unsigned int et = 0;

        avanti(P);
        if (tk(P) == TK_NOME && !P->L.a_capo_prima) {   /* un'etichetta */
            et = arena(P, P->L.sorgente + P->L.inizio, P->L.fine - P->L.inizio);
            avanti(P);
        }
        punto_virgola(P);
        n = nodo(P, t == TK_BREAK ? N_ROMPI : N_CONTINUA);
        if (n >= 0) P->A->nodi[n].testo = et;
        return n;
    }

    /* throw, try e switch (@EXJS-LACUNE, 29 settembre 2026). */
    case TK_THROW: {
        int e;

        avanti(P);
        /* ! UN A CAPO DOPO `throw` E' UN ERRORE, non un punto e virgola: la
         * norma lo vieta proprio perche' `throw` da solo non vuol dire
         * niente, e lasciarlo passare lancerebbe undefined. */
        if (P->L.a_capo_prima) { p_errore(P, "a capo dopo throw", 0); return -1; }
        e = espressione(P, 0);
        punto_virgola(P);
        if (P->rotto) return -1;
        n = nodo(P, N_LANCIA);
        if (n >= 0) P->A->nodi[n].a = e;
        return n;
    }

    case TK_TRY: {
        int prova, param = -1, cattura = -1, infine = -1;

        avanti(P);
        prova = blocco(P);
        if (accetta(P, TK_CATCH)) {
            /* `catch {` senza nome c'e' dal 2019, e il codice minimizzato
             * lo usa. */
            if (accetta(P, '(')) {
                if (tk(P) != TK_NOME) { p_errore(P, "atteso un nome in catch", exjs_lex_nome(tk(P))); return -1; }
                param = nodo(P, N_PARAMETRO);
                if (param >= 0)
                    P->A->nodi[param].testo = arena(P, P->L.sorgente + P->L.inizio,
                                                    P->L.fine - P->L.inizio);
                avanti(P);
                if (!pretendi(P, ')')) return -1;
            }
            cattura = blocco(P);
        }
        if (accetta(P, TK_FINALLY)) infine = blocco(P);
        if (P->rotto) return -1;
        if (cattura < 0 && infine < 0) { p_errore(P, "try senza catch e senza finally", 0); return -1; }

        n = nodo(P, N_PROVA);
        if (n < 0) return -1;
        P->A->nodi[n].a = prova;
        P->A->nodi[n].b = param;
        P->A->nodi[n].c = cattura;
        P->A->nodi[n].d = infine;
        return n;
    }

    case TK_SWITCH: {
        int d, primo = -1, ultimo = -1, visto_default = 0;

        avanti(P);
        if (!pretendi(P, '(')) return -1;
        d = espressione(P, 0);
        if (!pretendi(P, ')')) return -1;
        if (!pretendi(P, '{')) return -1;

        while (!P->rotto && tk(P) != '}' && tk(P) != TK_FINE) {
            int caso, prova = -1, s1 = -1, s2 = -1;

            if (accetta(P, TK_CASE)) prova = espressione(P, 0);
            else if (accetta(P, TK_DEFAULT)) {
                if (visto_default++) { p_errore(P, "due default nello stesso switch", 0); return -1; }
            } else { p_errore(P, "atteso case o default", exjs_lex_nome(tk(P))); return -1; }
            if (!pretendi(P, ':')) return -1;

            caso = nodo(P, N_CASO);
            if (caso < 0) return -1;
            while (!P->rotto && tk(P) != TK_CASE && tk(P) != TK_DEFAULT &&
                   tk(P) != '}' && tk(P) != TK_FINE) {
                int s = istruzione(P);
                if (P->rotto) return -1;
                if (s1 < 0) s1 = s; else P->A->nodi[s2].prossimo = s;
                s2 = s;
            }
            P->A->nodi[caso].a = prova;
            P->A->nodi[caso].b = s1;
            if (primo < 0) primo = caso; else P->A->nodi[ultimo].prossimo = caso;
            ultimo = caso;
        }
        if (!pretendi(P, '}')) return -1;

        n = nodo(P, N_SCEGLI);
        if (n < 0) return -1;
        P->A->nodi[n].a = d;
        P->A->nodi[n].b = primo;
        return n;
    }

    case TK_CATCH: case TK_FINALLY:
        p_errore(P, "catch o finally senza try", 0);
        return -1;

    default: {
        int e;

        char d2;

        if (tk(P) == TK_NOME && dopo(P, &d2) == ':') {  /* fuori: for (...) */
            unsigned int et = arena(P, P->L.sorgente + P->L.inizio, P->L.fine - P->L.inizio);
            int          dentro;

            avanti(P); avanti(P);
            dentro = istruzione(P);
            if (P->rotto) return -1;
            n = nodo(P, N_ETICHETTA);
            if (n < 0) return -1;
            P->A->nodi[n].testo = et;
            P->A->nodi[n].a     = dentro;
            return n;
        }
        e = espressione(P, 0);

        punto_virgola(P);
        if (P->rotto) return -1;

        n = nodo(P, N_ESPR);
        if (n < 0) return -1;
        P->A->nodi[n].a = e;
        return n;
    }
    }
}

/* =============================================================================
 * La porta d'ingresso
 * ========================================================================== */
void exjs_ast_prepara(ExJsAst *A, ExJsNodo *nodi, unsigned int nodi_max,
                      char *arena_buf, unsigned int arena_max)
{
    A->nodi      = nodi;
    A->nodi_max  = nodi_max;
    A->nodi_n    = 0;
    A->arena     = arena_buf;
    A->arena_max = arena_max;

    /* Il byte zero e' sempre uno '\0': cosi' `testo == 0` vuol dire «stringa
     * vuota» e non «nessuna stringa». Vedi arena() qui sopra. */
    A->arena_n   = 0;
    if (arena_max > 0) { arena_buf[0] = '\0'; A->arena_n = 1; }

    A->radice    = -1;
    A->troncato  = 0;
}

int exjs_analizza(ExJsAst *A, const char *sorgente, unsigned int n,
                  char *buffer_testo, unsigned int buffer_max,
                  ExJsErrore *err)
{
    Par P;
    int primo = -1, ultimo = -1, prog;

    P.A     = A;
    P.err   = err;
    P.rotto = 0;
    if (err) { err->riga = 0; err->colonna = 0; err->posizione = 0;
               err->messaggio[0] = '\0'; }

    exjs_lex_apri(&P.L, sorgente, n, buffer_testo, buffer_max, err);
    avanti(&P);

    prog = nodo(&P, N_PROGRAMMA);

    while (!P.rotto && tk(&P) != TK_FINE) {
        int s = istruzione(&P);
        if (P.rotto) break;
        if (primo < 0) primo = s; else A->nodi[ultimo].prossimo = s;
        ultimo = s;
    }

    if (P.rotto) return 0;

    if (prog >= 0) {
        A->nodi[prog].a  = primo;
        A->nodi[prog].op = (unsigned char)ha_lessicali(&P, primo);
    }
    A->radice = prog;
    return 1;
}

const char *exjs_nodo_nome(int tipo)
{
    switch (tipo) {
    case N_NUMERO:     return "numero";
    case N_STRINGA:    return "stringa";
    case N_NOME:       return "nome";
    case N_VERO:       return "vero";
    case N_FALSO:      return "falso";
    case N_NULLO:      return "nullo";
    case N_QUESTO:     return "questo";
    case N_VETTORE:    return "vettore";
    case N_OGGETTO:    return "oggetto";
    case N_VOCE:       return "voce";
    case N_FUNZIONE:   return "funzione";
    case N_PARAMETRO:  return "parametro";
    case N_UNARIO:     return "unario";
    case N_BINARIO:    return "binario";
    case N_LOGICO:     return "logico";
    case N_ASSEGNA:    return "assegna";
    case N_CONDIZIONE: return "condizione";
    case N_CHIAMATA:   return "chiamata";
    case N_NUOVO:      return "nuovo";
    case N_MEMBRO:     return "membro";
    case N_INDICE:     return "indice";
    case N_PRE:        return "pre";
    case N_POST:       return "post";
    case N_VIRGOLA:    return "virgola";
    case N_PROGRAMMA:  return "programma";
    case N_BLOCCO:     return "blocco";
    case N_VAR:        return "var";
    case N_DICHIARA:   return "dichiara";
    case N_ESPR:       return "espr";
    case N_SE:         return "se";
    case N_MENTRE:     return "mentre";
    case N_FAI:        return "fai";
    case N_PER:        return "per";
    case N_PER_IN:     return "per_in";
    case N_RITORNA:    return "ritorna";
    case N_ROMPI:      return "rompi";
    case N_CONTINUA:   return "continua";
    case N_VUOTO:      return "vuoto";
    case N_LANCIA:     return "lancia";
    case N_PROVA:      return "prova";
    case N_SCEGLI:     return "scegli";
    case N_CASO:       return "caso";
    case N_ESPANDI:    return "espandi";
    case N_PER_DI:     return "per_di";
    case N_ETICHETTA:  return "etichetta";
    case N_CLASSE:     return "classe";
    case N_SUPER:      return "super";
    case N_CATENA:     return "catena";
    default:           return "?";
    }
}
