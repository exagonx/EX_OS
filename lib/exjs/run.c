/* =============================================================================
 * lib/exjs/run.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * L'interprete — il quarto pezzo di ExJs
 *
 * -----------------------------------------------------------------------------
 * ! CAMMINA SULL'ALBERO, e non compila in istruzioni intermedie.
 *
 * Un interprete a bytecode e' piu' veloce di tre o quattro volte, e non e' il
 * lavoro giusto adesso: il pezzo che rende questo motore utile o inutile e' il
 * legame col documento, non la velocita' del ciclo interno. Camminare
 * sull'albero costa la meta' del codice e si legge; il giorno che la velocita'
 * contera' davvero, il posto dove metterla e' dietro exjs.h, e chi usa il
 * motore non se ne accorgera'.
 *
 * -----------------------------------------------------------------------------
 * ! DUE GUARDIE, E SONO OBBLIGATORIE IN UN BROWSER.
 *
 * `while (true) {}` in una pagina non deve poter fermare il sistema, e
 * `function f(){f()}` non deve poter mangiare la pila del C fino a portarsi via
 * il processo. Percio' si contano i PASSI e si conta la PROFONDITA', e quando
 * si sfora si smette dicendolo. Un motore senza queste due guardie e' un
 * motore che si puo' usare solo su codice di cui ci si fida — cioe' non sul web.
 *
 * -----------------------------------------------------------------------------
 * ! LE DICHIARAZIONI SI ISSANO PRIMA DI ESEGUIRE, e non e' una stranezza da
 * imitare per fedelta': e' cio' che permette a una funzione di chiamarne una
 * scritta piu' sotto. Il codice vero lo fa continuamente. `var` issa il nome
 * (con valore `undefined`), `function` issa il nome E la funzione intera.
 * ============================================================================= */

#include "exjs_int.h"

#define PASSI_MAX       20000000u   /* ~qualche secondo su una macchina lenta */
#define PROFONDITA_MAX  180         /* la pila del C, con margine */

typedef struct {
    ExJsCtx     *c;
    ExJsAst     *A;
    ExJsErrore  *err;
    int          rotto;
    unsigned int passi;
    int          profondita;

    /* Come si e' usciti dall'ultima istruzione. */
    int          segnale;           /* 0 avanti, 1 break, 2 continue, 3 return */
    ExJsVal      ritorno;
} Ese;

#define SEG_AVANTI    0
#define SEG_ROMPI     1
#define SEG_CONTINUA  2
#define SEG_RITORNA   3

static ExJsVal valuta(Ese *E, int n, int ambito);
static void    esegui(Ese *E, int n, int ambito);

/* -----------------------------------------------------------------------------
 * Errori di ESECUZIONE
 *
 * ! PORTANO LA RIGA, e per questo ogni nodo se la ricorda. «undefined non e'
 * una funzione» senza un numero di riga e' il messaggio che ha reso JavaScript
 * famoso per le ragioni sbagliate.
 * --------------------------------------------------------------------------- */
static ExJsVal errore(Ese *E, int n, const char *msg, const char *dettaglio)
{
    if (!E->rotto && E->err) {
        unsigned int i = 0, j;

        for (j = 0; msg[j] && i + 1 < EXJS_ERR_LEN; j++) E->err->messaggio[i++] = msg[j];
        if (dettaglio) {
            const char *s = ": ";
            for (j = 0; s[j] && i + 1 < EXJS_ERR_LEN; j++)         E->err->messaggio[i++] = s[j];
            for (j = 0; dettaglio[j] && i + 1 < EXJS_ERR_LEN; j++) E->err->messaggio[i++] = dettaglio[j];
        }
        E->err->messaggio[i] = '\0';
        E->err->riga    = (n >= 0) ? E->A->nodi[n].riga : 0;
        E->err->colonna = 0;
    }
    E->rotto = 1;
    return exjs_indefinito();
}

static int passo(Ese *E, int n)
{
    if (E->rotto) return 0;
    if (++E->passi > PASSI_MAX) {
        errore(E, n, "lo script gira da troppo tempo e l'ho fermato", 0);
        return 0;
    }
    return 1;
}

/* -----------------------------------------------------------------------------
 * Gli ambiti
 * --------------------------------------------------------------------------- */
static int ambito_nuovo(Ese *E, int padre)
{
    int i = exjs_ogg_nuovo(E->c, EXJS_CL_AMBITO);
    ExJsOggetto *O = exjs_ogg(E->c, i);

    if (O) O->proto = padre;
    return i;
}

static void dichiara(Ese *E, int ambito, const char *nome, ExJsVal v)
{
    exjs_metti(E->c, exjs_da_oggetto(ambito), nome, v);
}

/* -----------------------------------------------------------------------------
 * ! LE DICHIARAZIONI SI ISSANO. Vedi in cima: senza, una funzione non puo'
 * chiamarne una scritta piu' sotto, e il codice vero lo fa di continuo.
 *
 * ! NON SI SCENDE DENTRO LE FUNZIONI ANNIDATE: le loro `var` appartengono al
 * LORO ambito, non a questo. Scenderci vorrebbe dire che una variabile locale
 * di una funzione interna comparirebbe in quella esterna.
 * --------------------------------------------------------------------------- */
static ExJsVal fai_funzione(Ese *E, int n, int ambito);

static void issa(Ese *E, int n, int ambito)
{
    while (n >= 0 && !E->rotto) {
        ExJsNodo *N = &E->A->nodi[n];

        switch (N->tipo) {
        case N_VAR: {
            int d;
            for (d = N->a; d >= 0; d = E->A->nodi[d].prossimo) {
                const char *nome = exjs_arena_leggi(E->c, 0);
                nome = E->A->arena + E->A->nodi[d].testo;
                if (exjs_prop_trova(E->c, ambito, nome, 0) < 0)
                    dichiara(E, ambito, nome, exjs_indefinito());
            }
            break;
        }

        case N_FUNZIONE:
            if (N->testo) {
                const char *nome = E->A->arena + N->testo;
                dichiara(E, ambito, nome, fai_funzione(E, n, ambito));
            }
            break;

        /* Dentro questi si scende: sono blocchi di ISTRUZIONI, e in JavaScript
         * un blocco non fa un ambito nuovo per `var`. */
        case N_BLOCCO:   issa(E, N->a, ambito); break;
        case N_SE:       issa(E, N->b, ambito); issa(E, N->c, ambito); break;
        case N_MENTRE:
        case N_FAI:      issa(E, N->b, ambito); break;
        case N_PER:      issa(E, N->a, ambito); issa(E, N->d, ambito); break;
        case N_PER_IN:   issa(E, N->a, ambito); issa(E, N->d, ambito); break;
        default: break;
        }

        n = N->prossimo;
    }
}

/* =============================================================================
 * LE FUNZIONI
 * ========================================================================== */
static ExJsVal fai_funzione(Ese *E, int n, int ambito)
{
    int i = exjs_ogg_nuovo(E->c, EXJS_CL_FUNZIONE);
    ExJsOggetto *O = exjs_ogg(E->c, i);

    if (!O) { errore(E, n, "memoria esaurita creando una funzione", 0); return exjs_indefinito(); }

    O->nodo     = n;
    /* ! QUI NASCE LA CHIUSURA: la funzione si ricorda l'ambito in cui e' stata
     * CREATA, non quello in cui verra' chiamata. E' tutta la differenza. */
    O->ambiente = ambito;
    O->nome     = 0;
    return exjs_da_oggetto(i);
}

static ExJsVal chiama(Ese *E, ExJsVal f, ExJsVal questo,
                      const ExJsVal *arg, int n_arg, int nodo_chiamata)
{
    ExJsOggetto *O = exjs_ogg(E->c, exjs_a_oggetto(f));
    int          amb, par, i;
    ExJsVal      r;

    if (!O || O->classe != EXJS_CL_FUNZIONE)
        return errore(E, nodo_chiamata, "non e' una funzione", 0);

    if (O->nativa) return O->nativa(E->c, questo, arg, n_arg, O->dato);

    if (++E->profondita > PROFONDITA_MAX) {
        E->profondita--;
        return errore(E, nodo_chiamata, "troppe chiamate annidate (ricorsione senza fine?)", 0);
    }

    amb = ambito_nuovo(E, O->ambiente);
    if (amb < 0) { E->profondita--; return errore(E, nodo_chiamata, "memoria esaurita", 0); }

    /* I parametri, e quelli che mancano valgono `undefined`. */
    i = 0;
    for (par = E->A->nodi[O->nodo].a; par >= 0; par = E->A->nodi[par].prossimo) {
        dichiara(E, amb, E->A->arena + E->A->nodi[par].testo,
                 (i < n_arg) ? arg[i] : exjs_indefinito());
        i++;
    }

    /* ! `arguments` E' UN VETTORE VERO, e serve piu' di quanto sembri: le
     * pagine vere lo usano per le funzioni a numero di argomenti variabile. */
    {
        ExJsVal a = exjs_vettore(E->c);
        for (i = 0; i < n_arg; i++) exjs_indice_metti(E->c, a, (unsigned int)i, arg[i]);
        dichiara(E, amb, "arguments", a);
    }

    dichiara(E, amb, "this", questo);

    {
        int corpo = E->A->nodi[O->nodo].b;
        issa(E, E->A->nodi[corpo].a, amb);
        esegui(E, corpo, amb);
    }

    r = (E->segnale == SEG_RITORNA) ? E->ritorno : exjs_indefinito();
    E->segnale = SEG_AVANTI;
    E->profondita--;
    return r;
}

ExJsVal exjs_chiama(ExJsCtx *c, ExJsVal f, ExJsVal questo,
                    const ExJsVal *arg, int n_arg, ExJsErrore *err)
{
    Ese *E = (Ese *)exjs_ese_prendi(c);

    /* ! SI PUO' CHIAMARE SOLO MENTRE IL MOTORE STA GIRANDO, ed e' esattamente
     * quello che serve: chi chiama di qui e' una funzione NATIVA — `forEach`,
     * un gestore di evento, un lavoro scaduto — e quelle girano dentro
     * exjs_esegui o dentro exjs_pompa. Fuori non c'e' nessun interprete a cui
     * chiedere, e dirlo e' meglio che rendere `undefined`. */
    if (!E) {
        if (err) {
            const char *m = "exjs_chiama fuori da un'esecuzione";
            unsigned int i = 0;
            while (m[i] && i + 1 < EXJS_ERR_LEN) { err->messaggio[i] = m[i]; i++; }
            err->messaggio[i] = '\0';
            err->riga = 0; err->colonna = 0;
        }
        return exjs_indefinito();
    }
    return chiama(E, f, questo, arg, n_arg, -1);
}

/* =============================================================================
 * LE OPERAZIONI
 * ========================================================================== */
/* La concatenazione sta in val.c: tocca l'arena, e l'arena e' sua. Qui si
 * aggiunge solo la bandiera d'errore, perche' «memoria finita» dev'essere un
 * errore con una riga, non un `undefined` che scivola avanti. */
static ExJsVal concatena(Ese *E, ExJsVal a, ExJsVal b)
{
    ExJsVal r = exjs_concat(E->c, a, b);

    if (exjs_finita(E->c))
        return errore(E, -1, "arena esaurita concatenando stringhe", 0);
    return r;
}

static int uguali_testo(ExJsCtx *c, ExJsVal a, ExJsVal b)
{
    char         tmp[128];
    const char  *sa = exjs_a_stringa(c, a), *sb;
    unsigned int i;

    for (i = 0; i < sizeof(tmp) - 1 && sa[i]; i++) tmp[i] = sa[i];
    tmp[i] = '\0';
    if (sa[i]) return 0;                 /* piu' lunga del posto: non uguali */

    sb = exjs_a_stringa(c, b);
    for (i = 0; tmp[i] && tmp[i] == sb[i]; i++) { }
    return tmp[i] == '\0' && sb[i] == '\0';
}

/* ! L'UGUAGLIANZA STRETTA NON CONVERTE NIENTE, e per questo e' quella da
 * preferire: due valori sono `===` solo se hanno lo stesso tipo. */
static int identici(ExJsCtx *c, ExJsVal a, ExJsVal b)
{
    int ta = exjs_tipo(c, a), tb = exjs_tipo(c, b);

    if (ta != tb) return 0;
    switch (ta) {
    case EXJS_INDEFINITO:
    case EXJS_NULLO:    return 1;
    case EXJS_NUMERO: {
        double x = exjs_a_numero(c, a), y = exjs_a_numero(c, b);
        return x == y;                   /* NaN != NaN, ed e' giusto cosi' */
    }
    case EXJS_STRINGA:  return uguali_testo(c, a, b);
    default:            return a == b;   /* oggetti: la stessa casella */
    }
}

/* ! L'UGUAGLIANZA LARGA CONVERTE, e le sue regole sono la parte piu' odiata di
 * JavaScript. Non si imitano per fedelta' cieca: le pagine vere ci contano, e
 * un motore che le sbaglia da' rami sbagliati senza nessun errore.
 *
 *   null == undefined   vero, e non sono uguali a nient'altro
 *   numero == stringa   la stringa diventa numero
 *   booleano == altro   il booleano diventa numero */
/* Lo stesso confronto stretto, aperto a base.c: `Array.indexOf` deve usare
 * `===` come dice la norma, e riscriverlo li' vorrebbe dire due confronti che
 * devono restare d'accordo. */
int exjs_identici_pub(ExJsCtx *c, ExJsVal a, ExJsVal b)
{
    return identici(c, a, b);
}

static int uguali(ExJsCtx *c, ExJsVal a, ExJsVal b)
{
    int ta = exjs_tipo(c, a), tb = exjs_tipo(c, b);

    if (ta == tb) return identici(c, a, b);

    if ((ta == EXJS_NULLO && tb == EXJS_INDEFINITO) ||
        (ta == EXJS_INDEFINITO && tb == EXJS_NULLO)) return 1;
    if (ta == EXJS_NULLO || ta == EXJS_INDEFINITO ||
        tb == EXJS_NULLO || tb == EXJS_INDEFINITO) return 0;

    {
        double x = exjs_a_numero(c, a), y = exjs_a_numero(c, b);
        return x == y;
    }
}

static ExJsVal binario(Ese *E, int op, ExJsVal a, ExJsVal b, int n)
{
    ExJsCtx *c = E->c;

    switch (op) {
    case '+':
        /* ! IL PIU' E' L'UNICO OPERATORE CHE GUARDA I TIPI: se uno dei due e'
         * una stringa, si concatena; altrimenti si somma. `1 + "2"` fa "12", e
         * ogni pagina del mondo ci conta. */
        if (exjs_tipo(c, a) == EXJS_STRINGA || exjs_tipo(c, b) == EXJS_STRINGA ||
            exjs_tipo(c, a) == EXJS_OGGETTO || exjs_tipo(c, b) == EXJS_OGGETTO)
            return concatena(E, a, b);
        return exjs_numero(c, exjs_a_numero(c, a) + exjs_a_numero(c, b));

    case '-': return exjs_numero(c, exjs_a_numero(c, a) - exjs_a_numero(c, b));
    case '*': return exjs_numero(c, exjs_a_numero(c, a) * exjs_a_numero(c, b));

    case '/': {
        double y = exjs_a_numero(c, b);
        /* ! DIVIDERE PER ZERO NON E' UN ERRORE IN JavaScript: fa Infinity, e
         * un motore che desse errore fermerebbe pagine che funzionano. */
        return exjs_numero(c, exjs_a_numero(c, a) / y);
    }
    case '%': {
        double x = exjs_a_numero(c, a), y = exjs_a_numero(c, b);
        double q;
        if (y == 0.0 || x != x || y != y) return exjs_numero(c, 0.0/0.0);
        q = x / y;
        q = (double)(long long)q;
        return exjs_numero(c, x - q * y);
    }

    case '<':  case '>': case TK_MIN_UG: case TK_MAG_UG: {
        /* Fra due stringhe si confronta il TESTO, altrimenti i numeri. */
        if (exjs_tipo(c, a) == EXJS_STRINGA && exjs_tipo(c, b) == EXJS_STRINGA) {
            char tmp[128];
            const char *sa = exjs_a_stringa(c, a), *sb;
            unsigned int i;
            int cmp = 0;

            for (i = 0; i < sizeof(tmp) - 1 && sa[i]; i++) tmp[i] = sa[i];
            tmp[i] = '\0';
            sb = exjs_a_stringa(c, b);
            for (i = 0; tmp[i] && tmp[i] == sb[i]; i++) { }
            cmp = (int)(unsigned char)tmp[i] - (int)(unsigned char)sb[i];

            switch (op) {
            case '<':        return exjs_booleano(cmp <  0);
            case '>':        return exjs_booleano(cmp >  0);
            case TK_MIN_UG:  return exjs_booleano(cmp <= 0);
            default:         return exjs_booleano(cmp >= 0);
            }
        }
        {
            double x = exjs_a_numero(c, a), y = exjs_a_numero(c, b);
            switch (op) {
            case '<':        return exjs_booleano(x <  y);
            case '>':        return exjs_booleano(x >  y);
            case TK_MIN_UG:  return exjs_booleano(x <= y);
            default:         return exjs_booleano(x >= y);
            }
        }
    }

    case TK_UGUALE:     return exjs_booleano( uguali(c, a, b));
    case TK_DIVERSO:    return exjs_booleano(!uguali(c, a, b));
    case TK_ID_UGUALE:  return exjs_booleano( identici(c, a, b));
    case TK_ID_DIVERSO: return exjs_booleano(!identici(c, a, b));

    /* ! GLI OPERATORI SUI BIT LAVORANO SU INTERI A 32 BIT CON SEGNO, e la
     * conversione fa parte della definizione: `2147483648 | 0` fa
     * -2147483648. Farli sui double darebbe risultati diversi da qualunque
     * altro motore. */
    case '&': case '|': case '^': case TK_SHL: case TK_SHR: case TK_SHR_U: {
        double        dx = exjs_a_numero(c, a), dy = exjs_a_numero(c, b);
        int           x  = (dx != dx) ? 0 : (int)(long long)dx;
        unsigned int  y  = (dy != dy) ? 0u : (unsigned int)(long long)dy;

        switch (op) {
        case '&':       return exjs_numero(c, (double)(x & (int)y));
        case '|':       return exjs_numero(c, (double)(x | (int)y));
        case '^':       return exjs_numero(c, (double)(x ^ (int)y));
        case TK_SHL:    return exjs_numero(c, (double)(x << (y & 31)));
        case TK_SHR:    return exjs_numero(c, (double)(x >> (y & 31)));
        default:        return exjs_numero(c,
                                (double)(((unsigned int)x) >> (y & 31)));
        }
    }

    case TK_IN: {
        int i = exjs_a_oggetto(b);
        if (i < 0) return errore(E, n, "'in' vuole un oggetto a destra", 0);
        {
            char tmp[64];
            const char *s = exjs_a_stringa(c, a);
            unsigned int k;
            for (k = 0; k < sizeof(tmp) - 1 && s[k]; k++) tmp[k] = s[k];
            tmp[k] = '\0';
            return exjs_booleano(exjs_prop_trova(c, i, tmp, 1) >= 0);
        }
    }

    default:
        return errore(E, n, "operatore non gestito", exjs_lex_nome(op));
    }
}

/* =============================================================================
 * ASSEGNARE — dove si puo' scrivere
 * ========================================================================== */
static void assegna_a(Ese *E, int dove, int ambito, ExJsVal v)
{
    ExJsNodo *N;

    if (dove < 0 || E->rotto) return;
    N = &E->A->nodi[dove];

    if (N->tipo == N_NOME) {
        const char *nome = E->A->arena + N->testo;
        int         p    = exjs_prop_trova(E->c, ambito, nome, 1);

        /* ! ASSEGNARE A UN NOME MAI DICHIARATO LO CREA SUL GLOBALE, ed e' una
         * delle scelte piu' criticate di JavaScript — ma e' quella vera, e
         * meta' del codice sul web ci si appoggia senza saperlo. */
        if (p >= 0) exjs_prop_metti_val(E->c, p, v);
        else        dichiara(E, exjs_globale_idx(E->c), nome, v);
        return;
    }

    if (N->tipo == N_MEMBRO) {
        ExJsVal o = valuta(E, N->a, ambito);
        if (E->rotto) return;
        if (exjs_a_oggetto(o) < 0) {
            errore(E, dove, "non si puo' scrivere una proprieta' qui", 0);
            return;
        }
        exjs_metti(E->c, o, E->A->arena + N->testo, v);
        return;
    }

    if (N->tipo == N_INDICE) {
        ExJsVal o = valuta(E, N->a, ambito);
        ExJsVal i = valuta(E, N->b, ambito);
        int     k;

        if (E->rotto) return;
        k = exjs_a_oggetto(o);
        if (k < 0) { errore(E, dove, "non si puo' scrivere un elemento qui", 0); return; }

        {
            ExJsOggetto *O = exjs_ogg(E->c, k);
            if (O && O->classe == EXJS_CL_VETTORE &&
                exjs_tipo(E->c, i) == EXJS_NUMERO) {
                double d = exjs_a_numero(E->c, i);
                if (d >= 0 && d == (double)(long)d) {
                    exjs_indice_metti(E->c, o, (unsigned int)d, v);
                    return;
                }
            }
        }
        {
            char tmp[64];
            const char *s = exjs_a_stringa(E->c, i);
            unsigned int j;
            for (j = 0; j < sizeof(tmp) - 1 && s[j]; j++) tmp[j] = s[j];
            tmp[j] = '\0';
            exjs_metti(E->c, o, tmp, v);
        }
        return;
    }

    errore(E, dove, "a sinistra dell'uguale ci vuole qualcosa in cui scrivere", 0);
}

/* Assegna a un nome gia' esistente nella catena degli ambiti, o lo crea sul
 * globale. E' la stessa regola di assegna_a() per N_NOME, tenuta a parte
 * perche' `var` e `for..in` hanno il nome in mano e non un nodo. */
static void assegna_a_nome(Ese *E, int ambito, const char *nome, ExJsVal v)
{
    int p;

    if (E->rotto) return;
    p = exjs_prop_trova(E->c, ambito, nome, 1);
    if (p >= 0) exjs_prop_metti_val(E->c, p, v);
    else        dichiara(E, exjs_globale_idx(E->c), nome, v);
}

/* =============================================================================
 * VALUTARE UN'ESPRESSIONE
 * ========================================================================== */
static ExJsVal valuta(Ese *E, int n, int ambito)
{
    ExJsNodo *N;
    ExJsCtx  *c = E->c;

    if (n < 0 || E->rotto) return exjs_indefinito();
    if (!passo(E, n)) return exjs_indefinito();

    N = &E->A->nodi[n];

    switch (N->tipo) {
    case N_NUMERO:  return exjs_numero(c, N->numero);
    case N_STRINGA: return exjs_stringa(c, E->A->arena + N->testo, -1);
    case N_VERO:    return exjs_booleano(1);
    case N_FALSO:   return exjs_booleano(0);
    case N_NULLO:   return exjs_nullo();

    case N_QUESTO: {
        int p = exjs_prop_trova(c, ambito, "this", 1);
        return (p >= 0) ? exjs_prop_val(c, p) : exjs_indefinito();
    }

    case N_NOME: {
        const char *nome = E->A->arena + N->testo;
        int         p    = exjs_prop_trova(c, ambito, nome, 1);

        if (p < 0) return errore(E, n, "nome non definito", nome);
        return exjs_prop_val(c, p);
    }

    case N_VETTORE: {
        ExJsVal v = exjs_vettore(c);
        int     e; unsigned int i = 0;

        for (e = N->a; e >= 0; e = E->A->nodi[e].prossimo)
            exjs_indice_metti(c, v, i++, valuta(E, e, ambito));
        return v;
    }

    case N_OGGETTO: {
        ExJsVal o = exjs_oggetto(c);
        int     v;

        for (v = N->a; v >= 0; v = E->A->nodi[v].prossimo)
            exjs_metti(c, o, E->A->arena + E->A->nodi[v].testo,
                       valuta(E, E->A->nodi[v].a, ambito));
        return o;
    }

    case N_FUNZIONE: return fai_funzione(E, n, ambito);

    case N_UNARIO: {
        if (N->op == TK_TYPEOF) {
            /* ! `typeof` SU UN NOME CHE NON C'E' NON E' UN ERRORE: rende
             * "undefined". E' l'unico modo di chiedere se una cosa esiste, e
             * mezzo web comincia proprio cosi'. */
            if (E->A->nodi[N->a].tipo == N_NOME) {
                const char *nome = E->A->arena + E->A->nodi[N->a].testo;
                if (exjs_prop_trova(c, ambito, nome, 1) < 0)
                    return exjs_stringa(c, "undefined", -1);
            }
            {
                ExJsVal v = valuta(E, N->a, ambito);
                switch (exjs_tipo(c, v)) {
                case EXJS_INDEFINITO: return exjs_stringa(c, "undefined", -1);
                case EXJS_NULLO:      return exjs_stringa(c, "object", -1);
                case EXJS_BOOLEANO:   return exjs_stringa(c, "boolean", -1);
                case EXJS_NUMERO:     return exjs_stringa(c, "number", -1);
                case EXJS_STRINGA:    return exjs_stringa(c, "string", -1);
                case EXJS_FUNZIONE:   return exjs_stringa(c, "function", -1);
                default:              return exjs_stringa(c, "object", -1);
                }
            }
        }
        {
            ExJsVal v = valuta(E, N->a, ambito);
            if (E->rotto) return v;
            switch (N->op) {
            case '!': return exjs_booleano(!exjs_a_booleano(c, v));
            case '-': return exjs_numero(c, -exjs_a_numero(c, v));
            case '+': return exjs_numero(c,  exjs_a_numero(c, v));
            case '~': {
                double d = exjs_a_numero(c, v);
                int    x = (d != d) ? 0 : (int)(long long)d;
                return exjs_numero(c, (double)(~x));
            }
            case TK_VOID:   return exjs_indefinito();
            case TK_DELETE: return exjs_booleano(1);   /* non ancora vero */
            default: return errore(E, n, "unario non gestito", exjs_lex_nome(N->op));
            }
        }
    }

    case N_BINARIO: {
        ExJsVal a = valuta(E, N->a, ambito);
        ExJsVal b = valuta(E, N->b, ambito);
        if (E->rotto) return exjs_indefinito();
        return binario(E, N->op, a, b, n);
    }

    case N_LOGICO: {
        /* ! IL CORTO CIRCUITO NON E' UN'OTTIMIZZAZIONE: `a && a.b` esiste
         * proprio perche' la destra NON si valuta se la sinistra e' falsa. E
         * il valore reso e' l'OPERANDO, non un booleano: `a || 'niente'` e' il
         * modo in cui si scrivono i valori predefiniti. */
        ExJsVal a = valuta(E, N->a, ambito);
        if (E->rotto) return a;
        if (N->op == TK_E_E) return exjs_a_booleano(c, a) ? valuta(E, N->b, ambito) : a;
        return exjs_a_booleano(c, a) ? a : valuta(E, N->b, ambito);
    }

    case N_CONDIZIONE:
        return exjs_a_booleano(c, valuta(E, N->a, ambito))
             ? valuta(E, N->b, ambito) : valuta(E, N->c, ambito);

    case N_VIRGOLA:
        valuta(E, N->a, ambito);
        return valuta(E, N->b, ambito);

    case N_ASSEGNA: {
        ExJsVal v;

        if (N->op == '=') {
            v = valuta(E, N->b, ambito);
        } else {
            ExJsVal vecchio = valuta(E, N->a, ambito);
            ExJsVal d       = valuta(E, N->b, ambito);
            int     op;

            switch (N->op) {
            case TK_PIU_UG:   op = '+';       break;
            case TK_MENO_UG:  op = '-';       break;
            case TK_PER_UG:   op = '*';       break;
            case TK_DIV_UG:   op = '/';       break;
            case TK_MOD_UG:   op = '%';       break;
            case TK_SHL_UG:   op = TK_SHL;    break;
            case TK_SHR_UG:   op = TK_SHR;    break;
            case TK_SHR_U_UG: op = TK_SHR_U;  break;
            case TK_AND_UG:   op = '&';       break;
            case TK_OR_UG:    op = '|';       break;
            default:          op = '^';       break;
            }
            v = binario(E, op, vecchio, d, n);
        }
        if (E->rotto) return v;
        assegna_a(E, N->a, ambito, v);
        return v;
    }

    case N_PRE: case N_POST: {
        ExJsVal vecchio = valuta(E, N->a, ambito);
        double  d       = exjs_a_numero(c, vecchio);
        ExJsVal nuovo   = exjs_numero(c, (N->op == TK_PIU_PIU) ? d + 1 : d - 1);

        if (E->rotto) return vecchio;
        assegna_a(E, N->a, ambito, nuovo);
        /* ! LA DIFFERENZA FRA I DUE E' SOLO QUI: `i++` rende il valore
         * PRIMA, `++i` quello DOPO. */
        return (N->tipo == N_PRE) ? nuovo : exjs_numero(c, d);
    }

    case N_MEMBRO: {
        ExJsVal o = valuta(E, N->a, ambito);
        if (E->rotto) return o;
        if (exjs_tipo(c, o) == EXJS_STRINGA) {
            const char *nome = E->A->arena + N->testo;
            if (nome[0]=='l'&&nome[1]=='e'&&nome[2]=='n'&&nome[3]=='g'&&
                nome[4]=='t'&&nome[5]=='h'&&nome[6]=='\0') {
                const char *s = exjs_a_stringa(c, o);
                unsigned int k = 0;
                while (s[k]) k++;
                return exjs_numero(c, (double)k);
            }
        }
        return exjs_prendi(c, o, E->A->arena + N->testo);
    }

    case N_INDICE: {
        ExJsVal o = valuta(E, N->a, ambito);
        ExJsVal i = valuta(E, N->b, ambito);
        int     k;

        if (E->rotto) return exjs_indefinito();
        k = exjs_a_oggetto(o);
        if (k >= 0) {
            ExJsOggetto *O = exjs_ogg(c, k);
            if (O && O->classe == EXJS_CL_VETTORE &&
                exjs_tipo(c, i) == EXJS_NUMERO) {
                double d = exjs_a_numero(c, i);
                if (d >= 0 && d == (double)(long)d)
                    return exjs_indice_prendi(c, o, (unsigned int)d);
            }
        }
        {
            char tmp[64];
            const char *s = exjs_a_stringa(c, i);
            unsigned int j;
            for (j = 0; j < sizeof(tmp) - 1 && s[j]; j++) tmp[j] = s[j];
            tmp[j] = '\0';
            return exjs_prendi(c, o, tmp);
        }
    }

    case N_CHIAMATA: {
        ExJsVal arg[16], f, questo = exjs_indefinito();
        int     a, na = 0;

        /* ! `this` E' L'OGGETTO PRIMA DEL PUNTO, e si prende QUI: `o.f()` deve
         * vedere `o` dentro `f`, e per saperlo bisogna guardare la forma della
         * chiamata, non il valore della funzione. E' il motivo per cui in
         * JavaScript `var g = o.f; g()` perde `this`. */
        if (E->A->nodi[N->a].tipo == N_MEMBRO) {
            questo = valuta(E, E->A->nodi[N->a].a, ambito);
            f      = exjs_prendi(c, questo, E->A->arena + E->A->nodi[N->a].testo);
        } else {
            f = valuta(E, N->a, ambito);
        }
        if (E->rotto) return exjs_indefinito();

        for (a = N->b; a >= 0 && na < 16; a = E->A->nodi[a].prossimo)
            arg[na++] = valuta(E, a, ambito);
        if (E->rotto) return exjs_indefinito();

        return chiama(E, f, questo, arg, na, n);
    }

    case N_NUOVO: {
        ExJsVal arg[16], f, ogg;
        int     a, na = 0;

        f = valuta(E, N->a, ambito);
        for (a = N->b; a >= 0 && na < 16; a = E->A->nodi[a].prossimo)
            arg[na++] = valuta(E, a, ambito);
        if (E->rotto) return exjs_indefinito();

        /* ! `new` FA UN OGGETTO NUOVO, LO PASSA COME `this`, E LO RENDE — a
         * meno che il costruttore non renda a sua volta un oggetto. La seconda
         * meta' e' quella che si dimentica, e le librerie vere la usano. */
        ogg = exjs_oggetto(c);
        {
            ExJsVal r = chiama(E, f, ogg, arg, na, n);
            if (exjs_a_oggetto(r) >= 0) return r;
        }
        return ogg;
    }

    default:
        return errore(E, n, "nodo non valutabile", exjs_nodo_nome(N->tipo));
    }
}

/* =============================================================================
 * ESEGUIRE UN'ISTRUZIONE
 * ========================================================================== */
static void esegui_lista(Ese *E, int n, int ambito)
{
    while (n >= 0 && !E->rotto && E->segnale == SEG_AVANTI) {
        esegui(E, n, ambito);
        n = E->A->nodi[n].prossimo;
    }
}

static void esegui(Ese *E, int n, int ambito)
{
    ExJsNodo *N;

    if (n < 0 || E->rotto) return;
    if (!passo(E, n)) return;

    N = &E->A->nodi[n];

    switch (N->tipo) {
    case N_PROGRAMMA:
    case N_BLOCCO:
        esegui_lista(E, N->a, ambito);
        return;

    case N_VUOTO:
        return;

    case N_ESPR:
        E->ritorno = valuta(E, N->a, ambito);   /* l'ultimo valore, per il banco */
        return;

    case N_VAR: {
        int d;
        for (d = N->a; d >= 0 && !E->rotto; d = E->A->nodi[d].prossimo) {
            ExJsNodo *D = &E->A->nodi[d];
            /* Il nome e' gia' stato issato: qui si assegna soltanto, e SOLO se
             * c'e' un valore. `var a;` dopo `a = 1` non deve azzerare `a`. */
            if (D->a >= 0)
                assegna_a_nome(E, ambito, E->A->arena + D->testo,
                               valuta(E, D->a, ambito));
        }
        return;
    }

    case N_FUNZIONE:
        return;                             /* gia' issata */

    case N_SE:
        if (exjs_a_booleano(E->c, valuta(E, N->a, ambito))) esegui(E, N->b, ambito);
        else                                                esegui(E, N->c, ambito);
        return;

    case N_MENTRE:
        while (!E->rotto && exjs_a_booleano(E->c, valuta(E, N->a, ambito))) {
            esegui(E, N->b, ambito);
            if (E->segnale == SEG_ROMPI)    { E->segnale = SEG_AVANTI; break; }
            if (E->segnale == SEG_CONTINUA) { E->segnale = SEG_AVANTI; }
            if (E->segnale == SEG_RITORNA) return;
            if (!passo(E, n)) return;
        }
        return;

    case N_FAI:
        do {
            esegui(E, N->b, ambito);
            if (E->segnale == SEG_ROMPI)    { E->segnale = SEG_AVANTI; break; }
            if (E->segnale == SEG_CONTINUA) { E->segnale = SEG_AVANTI; }
            if (E->segnale == SEG_RITORNA) return;
            if (!passo(E, n)) return;
        } while (!E->rotto && exjs_a_booleano(E->c, valuta(E, N->a, ambito)));
        return;

    case N_PER:
        if (N->a >= 0) esegui(E, N->a, ambito);
        while (!E->rotto) {
            if (N->b >= 0 && !exjs_a_booleano(E->c, valuta(E, N->b, ambito))) break;
            esegui(E, N->d, ambito);
            if (E->segnale == SEG_ROMPI)    { E->segnale = SEG_AVANTI; break; }
            if (E->segnale == SEG_CONTINUA) { E->segnale = SEG_AVANTI; }
            if (E->segnale == SEG_RITORNA) return;
            if (N->c >= 0) valuta(E, N->c, ambito);
            if (!passo(E, n)) return;
        }
        return;

    case N_PER_IN: {
        ExJsVal      o = valuta(E, N->b, ambito);
        int          k = exjs_a_oggetto(o);
        ExJsOggetto *O = exjs_ogg(E->c, k);
        const char  *nome_var = 0;

        if (E->rotto || !O) return;

        /* Il nome in cui mettere la chiave: o `var k`, o un nome gia' esistente. */
        if (E->A->nodi[N->a].tipo == N_VAR)
            nome_var = E->A->arena + E->A->nodi[E->A->nodi[N->a].a].testo;
        else if (E->A->nodi[N->a].tipo == N_NOME)
            nome_var = E->A->arena + E->A->nodi[N->a].testo;
        else { errore(E, n, "for..in vuole un nome a sinistra di 'in'", 0); return; }

        if (O->classe == EXJS_CL_VETTORE) {
            unsigned int i;
            for (i = 0; i < O->lunghezza && !E->rotto; i++) {
                char b[16];
                unsigned int j = 0, v = i;
                char rev[16]; int rn = 0;
                if (!v) rev[rn++] = '0';
                while (v) { rev[rn++] = (char)('0' + v % 10); v /= 10; }
                while (rn) b[j++] = rev[--rn];
                b[j] = '\0';

                assegna_a_nome(E, ambito, nome_var, exjs_stringa(E->c, b, -1));
                esegui(E, N->d, ambito);
                if (E->segnale == SEG_ROMPI)    { E->segnale = SEG_AVANTI; break; }
                if (E->segnale == SEG_CONTINUA) { E->segnale = SEG_AVANTI; }
                if (E->segnale == SEG_RITORNA) return;
            }
            return;
        }

        /* ! SOLO LE PROPRIETA' PROPRIE, non quelle del prototipo. La norma
         * dice il contrario — `for..in` risale — ma risalendo si finisce per
         * enumerare i metodi che il motore stesso ha messo li', e ogni pagina
         * del mondo scrive `hasOwnProperty` per rimediare. Qui non risale, e
         * sta scritto. */
        {
            int p;
            for (p = exjs_prop_prima(E->c, k); p >= 0 && !E->rotto; ) {
                int prossima = exjs_prop_prossima(E->c, p);

                assegna_a_nome(E, ambito, nome_var,
                               exjs_stringa_off(E->c, exjs_prop_nome(E->c, p)));
                esegui(E, N->d, ambito);
                if (E->segnale == SEG_ROMPI)    { E->segnale = SEG_AVANTI; break; }
                if (E->segnale == SEG_CONTINUA) { E->segnale = SEG_AVANTI; }
                if (E->segnale == SEG_RITORNA) return;
                p = prossima;
            }
        }
        return;
    }

    case N_RITORNA:
        E->ritorno = (N->a >= 0) ? valuta(E, N->a, ambito) : exjs_indefinito();
        E->segnale = SEG_RITORNA;
        return;

    case N_ROMPI:    E->segnale = SEG_ROMPI;    return;
    case N_CONTINUA: E->segnale = SEG_CONTINUA; return;

    default:
        valuta(E, n, ambito);
        return;
    }
}

/* =============================================================================
 * LA PORTA D'INGRESSO
 * ========================================================================== */
/* =============================================================================
 * ! LA CODA DEI LAVORI HA BISOGNO DI UN INTERPRETE, e non ne ha uno suo.
 *
 * `exjs_pompa` viene chiamata da FUORI — dal ciclo dei messaggi del browser —
 * quando nessuno script sta girando: li' `exjs_chiama` non troverebbe nessuno
 * stato d'esecuzione a cui appoggiarsi. Percio' se ne allestisce uno per la
 * durata della pompata, esattamente come fa exjs_esegui.
 *
 * ! E L'ALBERO E' QUELLO DI PRIMA, non uno nuovo: le funzioni in coda sono
 * indici dentro di lui. E' il motivo per cui l'albero adesso si allunga invece
 * di rifarsi — vedi sopra.
 * ========================================================================== */
int exjs_pompa(ExJsCtx *c, unsigned int ora_ms)
{
    Ese     E;
    int     fatti = 0;
    ExJsVal f;

    if (!c || !exjs_ast_pronto(c)) return 0;

    E.c          = c;
    E.A          = exjs_ctx_ast(c);
    E.err        = 0;
    E.rotto      = 0;
    E.passi      = 0;
    E.profondita = 0;
    E.segnale    = SEG_AVANTI;
    E.ritorno    = exjs_indefinito();

    exjs_ese_metti(c, &E);
    exjs_ora_metti(c, ora_ms);

    /* ! SI PRENDE UN LAVORO PER VOLTA E SI RICHIEDE, invece di scorrere un
     * elenco: la funzione appena eseguita puo' averne accodati altri — anzi,
     * `setInterval` lo fa sempre — e un ciclo su un elenco preso all'inizio
     * lavorerebbe su una fotografia gia' vecchia.
     *
     * ! E CIO' CHE SCADE MENTRE SI POMPA NON SI ESEGUE IN QUESTO GIRO. Senza
     * questo, un `setTimeout(f, 0)` che si riaccoda darebbe un ciclo infinito
     * dentro una sola pompata, e il browser non tornerebbe piu' a disegnare. */
    while (!E.rotto && exjs_lavoro_scaduto(c, ora_ms, &f)) {
        exjs_chiama(c, f, exjs_indefinito(), 0, 0, 0);
        fatti++;
        if (fatti > 1000) break;        /* una pompata non e' un'eternita' */
    }

    exjs_ese_metti(c, 0);
    return fatti;
}

int exjs_esegui(ExJsCtx *c, const char *sorgente, unsigned int n,
                ExJsVal *risultato, ExJsErrore *err)
{
    Ese      E;
    ExJsAst *A;

    if (!c || !sorgente) return 0;

    /* ! LA LIBRERIA DI BASE SI REGISTRA UNA VOLTA SOLA, alla prima esecuzione
     * e non in exjs_apri. Cosi' chi apre un contesto per un uso che JavaScript
     * non e' — e un giorno ci sara' — non paga le duecento proprieta' di Math,
     * String e Array. */
    exjs_base_registra(c);

    A = exjs_ctx_ast(c);

    /* =====================================================================
     * ! L'ALBERO NON SI RIFA': SI ALLUNGA. E questa e' una correzione, non una
     * scelta di comodo.
     *
     * La prima stesura rifaceva l'albero da capo a ogni exjs_esegui, e sarebbe
     * andata bene finche' uno script finiva quando finiva il suo testo. Ma una
     * funzione — una chiusura, un gestore di evento, un `setTimeout` — e' un
     * INDICE DENTRO L'ALBERO: sopravvive allo script che l'ha creata, e il
     * secondo <script> della pagina le avrebbe fatto puntare a nodi diversi.
     * Il guasto non sarebbe stato un errore: sarebbe stata una funzione che
     * esegue il codice di un'altra.
     *
     * Adesso l'albero si prepara UNA VOLTA e ogni script accoda i suoi nodi in
     * fondo, come le variabili si accumulano sul globale. Lo spazio dei nodi
     * non torna piu' indietro — e' lo stesso prezzo dell'arena, dichiarato in
     * val.c — e si paga solo su pagine che eseguono molti script.
     * ===================================================================== */
    if (!exjs_ast_pronto(c)) {
        exjs_ast_prepara(A, exjs_ctx_nodi(c), exjs_ctx_nodi_max(c),
                         exjs_ctx_ast_arena(c), exjs_ctx_ast_arena_max(c));
        exjs_ast_segna(c);
    }

    if (!exjs_analizza(A, sorgente, n, exjs_ctx_scratch(c),
                       EXJS_SCRATCH, err))
        return 0;

    E.c          = c;
    E.A          = A;
    E.err        = err;
    E.rotto      = 0;
    E.passi      = 0;
    E.profondita = 0;
    E.segnale    = SEG_AVANTI;
    E.ritorno    = exjs_indefinito();

    if (err) { err->messaggio[0] = '\0'; err->riga = 0; err->colonna = 0; }

    /* Da qui in poi le funzioni native possono richiamare il motore: vedi
     * exjs_chiama. Si toglie prima di uscire, o resterebbe un puntatore a una
     * struttura sulla pila che non esiste piu'. */
    exjs_ese_metti(c, &E);

    /* ! LE DICHIARAZIONI DI TUTTO IL PROGRAMMA PRIMA DI ESEGUIRE LA PRIMA
     * ISTRUZIONE. Vedi issa(): senza, una funzione non puo' chiamarne una
     * scritta piu' sotto. */
    issa(&E, A->nodi[A->radice].a, exjs_globale_idx(c));
    esegui(&E, A->radice, exjs_globale_idx(c));

    exjs_ese_metti(c, 0);

    if (risultato) *risultato = E.ritorno;

    /* ! LA MEMORIA FINITA E' UN ERRORE ANCHE SE NESSUNO L'HA DETTO. val.c
     * alza una bandiera e va avanti rendendo `undefined`: se qui non la si
     * guardasse, uno script troncato a meta' risulterebbe «riuscito». */
    if (!E.rotto && exjs_finita(c)) {
        errore(&E, -1, "memoria del motore esaurita", 0);
        return 0;
    }
    return !E.rotto;
}
