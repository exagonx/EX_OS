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
    case TK_O_O:                                    return 1;
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
    default:                                        return 0;
    }
}

static int e_assegnazione(int t)
{
    switch (t) {
    case '=': case TK_PIU_UG: case TK_MENO_UG: case TK_PER_UG:
    case TK_DIV_UG: case TK_MOD_UG: case TK_SHL_UG: case TK_SHR_UG:
    case TK_SHR_U_UG: case TK_AND_UG: case TK_OR_UG: case TK_XOR_UG:
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
            int e = assegnazione(P, 0);
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
        unsigned int off = arena(P, P->L.sorgente + P->L.inizio,
                                 P->L.fine - P->L.inizio);
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

    case '(': {
        int e;
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
        if (!accetta(P, '}')) {
            for (;;) {
                int          v, voce;
                unsigned int off;

                /* ! LA CHIAVE PUO' ESSERE UN NOME, UNA STRINGA O UN NUMERO, e
                 * anche una PAROLA CHIAVE: `{if: 1}` e' legale, e le pagine
                 * vere lo usano. Rifiutarla vorrebbe dire non saper leggere
                 * oggetti scritti da un minificatore. */
                if (tk(P) == TK_STRINGA) {
                    off = arena(P, P->L.testo, P->L.testo_n);
                } else if (tk(P) == TK_NUMERO) {
                    /* Il numero diventa il suo testo cosi' com'e' scritto. */
                    off = arena(P, P->L.sorgente + P->L.inizio,
                                P->L.fine - P->L.inizio);
                } else if (tk(P) == TK_NOME || tk(P) >= TK_VAR) {
                    off = arena(P, P->L.sorgente + P->L.inizio,
                                P->L.fine - P->L.inizio);
                } else {
                    p_errore(P, "atteso il nome di una proprieta'",
                             exjs_lex_nome(tk(P)));
                    return -1;
                }
                avanti(P);
                if (!pretendi(P, ':')) return -1;

                v    = assegnazione(P, 0);
                voce = nodo(P, N_VOCE);
                if (P->rotto) return -1;

                P->A->nodi[voce].testo = off;
                P->A->nodi[voce].a     = v;

                if (primo < 0) primo = voce;
                else           P->A->nodi[ultimo].prossimo = voce;
                ultimo = voce;

                if (accetta(P, ',')) {
                    if (tk(P) == '}') { avanti(P); break; }
                    continue;
                }
                if (!pretendi(P, '}')) return -1;
                break;
            }
        }
        if (n >= 0) P->A->nodi[n].a = primo;
        return n;
    }

    case TK_SWITCH: case TK_TRY: case TK_THROW:
        p_errore(P, "questo scaglione di ExJs non ha ancora switch, try e throw", 0);
        return -1;

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
static int coda(Par *P, int sin, int con_chiamate)
{
    for (;;) {
        if (P->rotto) return -1;

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
        /* +1: gli operatori binari di JavaScript associano tutti a sinistra. */
        des = binari(P, pr + 1, senza_in);
        if (P->rotto) return -1;

        n = nodo(P, (op == TK_E_E || op == TK_O_O) ? N_LOGICO : N_BINARIO);
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
static int funzione(Par *P, int e_dichiarazione)
{
    int          n, primo = -1, ultimo = -1, corpo;
    unsigned int nome = 0;

    avanti(P);                          /* `function` */

    if (tk(P) == TK_NOME) {
        nome = arena(P, P->L.sorgente + P->L.inizio, P->L.fine - P->L.inizio);
        avanti(P);
    } else if (e_dichiarazione) {
        p_errore(P, "una funzione dichiarata vuole un nome", exjs_lex_nome(tk(P)));
        return -1;
    }

    if (!pretendi(P, '(')) return -1;
    if (!accetta(P, ')')) {
        for (;;) {
            unsigned int pn;
            int          p;

            if (tk(P) != TK_NOME) {
                p_errore(P, "atteso il nome di un parametro", exjs_lex_nome(tk(P)));
                return -1;
            }
            pn = arena(P, P->L.sorgente + P->L.inizio, P->L.fine - P->L.inizio);
            avanti(P);

            p = nodo(P, N_PARAMETRO);
            if (p < 0) return -1;
            P->A->nodi[p].testo = pn;

            if (primo < 0) primo = p; else P->A->nodi[ultimo].prossimo = p;
            ultimo = p;

            if (accetta(P, ',')) continue;
            break;
        }
        if (!pretendi(P, ')')) return -1;
    }

    corpo = blocco(P);
    if (P->rotto) return -1;

    n = nodo(P, N_FUNZIONE);
    if (n < 0) return -1;
    P->A->nodi[n].testo = nome;
    P->A->nodi[n].a     = primo;
    P->A->nodi[n].b     = corpo;
    return n;
}

/* =============================================================================
 * LE ISTRUZIONI
 * ========================================================================== */
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

    if (n >= 0) P->A->nodi[n].a = primo;
    return n;
}

/* `var a = 1, b;` — rende il nodo N_VAR. `senza_in` serve dentro un `for`. */
static int dichiarazioni(Par *P, int senza_in)
{
    int n = nodo(P, N_VAR), primo = -1, ultimo = -1;

    avanti(P);                          /* `var` */

    for (;;) {
        unsigned int nome;
        int          d, val = -1;

        if (tk(P) != TK_NOME) {
            p_errore(P, "atteso il nome di una variabile", exjs_lex_nome(tk(P)));
            return -1;
        }
        nome = arena(P, P->L.sorgente + P->L.inizio, P->L.fine - P->L.inizio);
        avanti(P);

        if (accetta(P, '=')) val = assegnazione(P, senza_in);
        if (P->rotto) return -1;

        d = nodo(P, N_DICHIARA);
        if (d < 0) return -1;
        P->A->nodi[d].testo = nome;
        P->A->nodi[d].a     = val;

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
    int inizio = -1, n;

    avanti(P);                          /* `for` */
    if (!pretendi(P, '(')) return -1;

    if (tk(P) == ';') {
        inizio = -1;
    } else if (tk(P) == TK_VAR) {
        inizio = dichiarazioni(P, 1);
    } else {
        inizio = espressione(P, 1);
    }
    if (P->rotto) return -1;

    if (tk(P) == TK_IN) {
        int oggetto, corpo;

        avanti(P);
        oggetto = espressione(P, 0);
        if (!pretendi(P, ')')) return -1;
        corpo = istruzione(P);
        if (P->rotto) return -1;

        n = nodo(P, N_PER_IN);
        if (n < 0) return -1;
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

    case TK_VAR:
        n = dichiarazioni(P, 0);
        punto_virgola(P);
        return n;

    case TK_FUNCTION:
        return funzione(P, 1);

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

    case TK_BREAK:
        avanti(P); punto_virgola(P);
        return nodo(P, N_ROMPI);

    case TK_CONTINUE:
        avanti(P); punto_virgola(P);
        return nodo(P, N_CONTINUA);

    case TK_SWITCH: case TK_TRY: case TK_THROW: case TK_CATCH: case TK_FINALLY:
        p_errore(P, "questo scaglione di ExJs non ha ancora switch, try e throw", 0);
        return -1;

    default: {
        int e = espressione(P, 0);

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

    if (prog >= 0) A->nodi[prog].a = primo;
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
    default:           return "?";
    }
}
