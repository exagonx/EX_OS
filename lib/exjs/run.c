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

    /* ! UN'ECCEZIONE E' `rotto` PIU' QUESTI (29 settembre 2026, @EXJS-LACUNE).
     * Lo stato d'errore c'era gia' e fermava tutto risalendo: throw lo
     * riusa, e try lo spegne. `catturabile` e' 0 solo per la guardia dei
     * passi — uno script fermato perche' gira da troppo non deve potersi
     * riprendere con un catch. `ha_valore` dice se l'eccezione e' un valore
     * lanciato; se no e' un errore del motore, e il catch riceve un
     * TypeError fatto col suo messaggio. */
    int          catturabile;
    int          ha_valore;
    ExJsVal      eccezione;
    char         messaggio[EXJS_ERR_LEN];
    int          nodo_ora;          /* l'ultima istruzione, per la riga */

    /* ES2015 (29 settembre 2026). `corto`: una catena con ?. si e' fermata
     * (vedi N_CATENA). `salta`: l'etichetta di un break o continue in
     * viaggio, 0 se non ne ha. `etichetta_ciclo`: l'etichetta che il
     * prossimo ciclo si prende (vedi N_ETICHETTA). */
    int          corto;
    unsigned int salta;
    unsigned int etichetta_ciclo;
} Ese;

#define SEG_AVANTI    0
#define SEG_ROMPI     1
#define SEG_CONTINUA  2
#define SEG_RITORNA   3

static ExJsVal valuta(Ese *E, int n, int ambito);
static void    esegui(Ese *E, int n, int ambito);
static void    esegui_lista(Ese *E, int n, int ambito);

/* -----------------------------------------------------------------------------
 * Errori di ESECUZIONE
 *
 * ! PORTANO LA RIGA, e per questo ogni nodo se la ricorda. «undefined non e'
 * una funzione» senza un numero di riga e' il messaggio che ha reso JavaScript
 * famoso per le ragioni sbagliate.
 * --------------------------------------------------------------------------- */
static ExJsVal errore(Ese *E, int n, const char *msg, const char *dettaglio)
{
    if (n < 0) n = E->nodo_ora;
    if (!E->rotto) {
        unsigned int i = 0, j;
        char        *m = E->messaggio;

        for (j = 0; msg[j] && i + 1 < EXJS_ERR_LEN; j++) m[i++] = msg[j];
        if (dettaglio) {
            const char *s = ": ";
            for (j = 0; s[j] && i + 1 < EXJS_ERR_LEN; j++)         m[i++] = s[j];
            for (j = 0; dettaglio[j] && i + 1 < EXJS_ERR_LEN; j++) m[i++] = dettaglio[j];
        }
        m[i] = '\0';
        if (E->err) {
            for (j = 0; j <= i; j++) E->err->messaggio[j] = m[j];
            E->err->riga    = (n >= 0) ? E->A->nodi[n].riga : 0;
            E->err->colonna = 0;
        }
        E->catturabile = 1;
        E->ha_valore   = 0;
    }
    E->rotto = 1;
    return exjs_indefinito();
}

static int passo(Ese *E, int n)
{
    if (E->rotto) return 0;
    E->nodo_ora = n;
    if (++E->passi > PASSI_MAX) {
        errore(E, n, "lo script gira da troppo tempo e l'ho fermato", 0);
        E->catturabile = 0;
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

/* Ogni nome che un modello ({a, b: [c, d = 1], ...r}) dichiara. */
static void nomi_modello(Ese *E, int m, int ambito,
                         void (*f)(Ese *, int, const char *))
{
    ExJsNodo *M;
    int       k;

    if (m < 0) return;
    M = &E->A->nodi[m];
    switch (M->tipo) {
    case N_NOME:    f(E, ambito, E->A->arena + M->testo); break;
    case N_ASSEGNA:
    case N_ESPANDI: nomi_modello(E, M->a, ambito, f); break;
    case N_VETTORE:
        for (k = M->a; k >= 0; k = E->A->nodi[k].prossimo) nomi_modello(E, k, ambito, f);
        break;
    case N_OGGETTO:
        for (k = M->a; k >= 0; k = E->A->nodi[k].prossimo) nomi_modello(E, E->A->nodi[k].a, ambito, f);
        break;
    default: break;
    }
}

/* Dichiara il nome in amb se amb non ce l'ha gia' di suo. */
static void nome_se_manca(Ese *E, int amb, const char *nome)
{
    if (exjs_prop_trova(E->c, amb, nome, 0) < 0) dichiara(E, amb, nome, exjs_indefinito());
}

/* I nomi di una lista di N_DICHIARA. */
static void nomi_dichiarati(Ese *E, int d, int amb, void (*f)(Ese *, int, const char *))
{
    for (; d >= 0; d = E->A->nodi[d].prossimo) {
        if (E->A->nodi[d].b >= 0) nomi_modello(E, E->A->nodi[d].b, amb, f);
        else f(E, amb, E->A->arena + E->A->nodi[d].testo);
    }
}

/* Le dichiarazioni let, const e class di una lista d'istruzioni, in amb. */
static void dichiara_lessicali(Ese *E, int n, int amb)
{
    for (; n >= 0; n = E->A->nodi[n].prossimo) {
        ExJsNodo *N = &E->A->nodi[n];
        if (N->tipo == N_VAR && N->op) nomi_dichiarati(E, N->a, amb, nome_se_manca);
        else if (N->tipo == N_CLASSE && N->testo) nome_se_manca(E, amb, E->A->arena + N->testo);
    }
}

/* ! L'AMBITO DI UN BLOCCO O DI UN CICLO CON let, SE DENTRO NON CI SONO
 * CHIUSURE, SI FA UNA VOLTA E SI RIUSA a ogni giro. Ogni ambito e' un oggetto,
 * e ExJs gli oggetti non li recupera: uno per giro finirebbe la scorta del
 * navigatore (2000) in un ciclo solo. Riusarlo e' sicuro proprio perche'
 * nessuna funzione nata li' dentro puo' ricordarselo; con una chiusura dentro
 * (N_BLOCCO.op & 2, N_PER.op & 1) ogni giro ha il suo, come vuole la norma.
 * Il nodo tiene l'ultimo ambito fatto (numero) e per quale ambito di fuori
 * (testo, +1): una chiamata nuova della funzione ne fa uno nuovo. */
static int ambito_riusato(Ese *E, int n, int ambito)
{
    ExJsNodo *N = &E->A->nodi[n];
    int       a;

    if (N->testo == (unsigned int)ambito + 1) return (int)N->numero;
    a = ambito_nuovo(E, ambito);
    if (a < 0) return -1;
    N = &E->A->nodi[n];
    N->testo  = (unsigned int)ambito + 1;
    N->numero = (double)a;
    return a;
}

/* Un ambito nuovo con la copia delle variabili proprie di `da`: il giro dopo
 * di un for (let ...) con una chiusura dentro. */
static int copia_ambito(Ese *E, int da, int padre)
{
    int n = ambito_nuovo(E, padre), p;

    if (n < 0) return -1;
    for (p = exjs_prop_prima(E->c, da); p >= 0; p = exjs_prop_prossima(E->c, p))
        dichiara(E, n, exjs_arena_leggi(E->c, exjs_prop_nome(E->c, p)), exjs_prop_val(E->c, p));
    return n;
}

static void issa(Ese *E, int n, int ambito)
{
    while (n >= 0 && !E->rotto) {
        ExJsNodo *N = &E->A->nodi[n];

        switch (N->tipo) {
        case N_VAR:
            /* let e const no: sono del blocco (vedi dichiara_lessicali) */
            if (!N->op) nomi_dichiarati(E, N->a, ambito, nome_se_manca);
            break;

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
        case N_PROVA:    issa(E, N->a, ambito); issa(E, N->c, ambito); issa(E, N->d, ambito); break;
        case N_PER_DI:   issa(E, N->a, ambito); issa(E, N->d, ambito); break;
        case N_ETICHETTA: issa(E, N->a, ambito); break;
        case N_SCEGLI: {
            int k;
            for (k = N->b; k >= 0; k = E->A->nodi[k].prossimo) issa(E, E->A->nodi[k].b, ambito);
            break;
        }
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

static void lega(Ese *E, int m, ExJsVal v, int ambito, int dichiarando);
static void campi_classe(Ese *E, int cls, ExJsVal questo, int ambito);
static void super_chiama(Ese *E, int ambito, const ExJsVal *arg, int n_arg, int n);

static ExJsVal chiama(Ese *E, ExJsVal f, ExJsVal questo,
                      const ExJsVal *arg, int n_arg, int nodo_chiamata)
{
    ExJsOggetto *O = exjs_ogg(E->c, exjs_a_oggetto(f));
    int          amb, par, i, fop;
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

    fop = E->A->nodi[O->nodo].op;

    /* ! UNA FRECCIA NON HA NE' this NE' arguments SUOI: li cerca dove e'
     * nata, risalendo la catena degli ambiti. E' tutto il motivo per cui le
     * frecce esistono: `setTimeout(() => this.x, 0)` dentro un metodo. */
    if (!(fop & 1)) {
        /* ! `arguments` E' UN VETTORE VERO, e serve piu' di quanto sembri: le
         * pagine vere lo usano per le funzioni a numero di argomenti variabile. */
        ExJsVal a = exjs_vettore(E->c);
        for (i = 0; i < n_arg; i++) exjs_indice_metti(E->c, a, (unsigned int)i, arg[i]);
        dichiara(E, amb, "arguments", a);
        dichiara(E, amb, "this", questo);
    }
    /* Un metodo di classe: dove cercare `super` (il dato di una funzione
     * scritta in JavaScript e' la sua casa, +1). */
    if (O->dato) dichiara(E, amb, "\001casa", exjs_da_oggetto((int)(long)O->dato - 1));

    /* I parametri: quelli che mancano valgono `undefined`, o il loro valore
     * predefinito, calcolato qui dentro (vede i parametri prima di lui);
     * `...r` prende il resto, e un modello si destruttura. */
    i = 0;
    for (par = E->A->nodi[O->nodo].a; par >= 0 && !E->rotto; par = E->A->nodi[par].prossimo) {
        ExJsNodo *PN = &E->A->nodi[par];
        ExJsVal   v;

        if (PN->op == 1) {
            int k;
            v = exjs_vettore(E->c);
            for (k = i; k < n_arg; k++) exjs_indice_metti(E->c, v, (unsigned int)(k - i), arg[k]);
        } else {
            v = (i < n_arg) ? arg[i] : exjs_indefinito();
            if (PN->a >= 0 && exjs_tipo(E->c, v) == EXJS_INDEFINITO) v = valuta(E, PN->a, amb);
        }
        if (PN->b >= 0) lega(E, PN->b, v, amb, 1);
        else            dichiara(E, amb, E->A->arena + PN->testo, v);
        i++;
    }

    /* Il costruttore di una classe: i campi (x = 1) si mettono subito se la
     * classe non deriva da niente, altrimenti dopo super(); e il costruttore
     * che non e' scritto, in una derivata, passa tutto al padre. */
    if ((fop & 4) && !E->rotto) {
        int cls = (int)exjs_a_numero(E->c, exjs_prendi(E->c, f, "\001classe"));
        if (cls > 0 && E->A->nodi[cls].a < 0) campi_classe(E, cls, questo, amb);
        if (fop & 2) super_chiama(E, amb, arg, n_arg, nodo_chiamata);
    }

    {
        int corpo = E->A->nodi[O->nodo].b;
        issa(E, E->A->nodi[corpo].a, amb);
        /* il corpo della funzione non fa un ambito in piu': i suoi let
         * stanno in quello della chiamata, che e' gia' nuovo */
        if (E->A->nodi[corpo].op) dichiara_lessicali(E, E->A->nodi[corpo].a, amb);
        if (passo(E, corpo)) esegui_lista(E, E->A->nodi[corpo].a, amb);
    }

    r = (E->segnale == SEG_RITORNA) ? E->ritorno : exjs_indefinito();
    E->segnale = SEG_AVANTI;
    E->salta   = 0;
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
        /* ! UN OGGETTO PRIMA DIVENTA UN VALORE SEMPLICE (28 settembre 2026):
         * `o + 1` con valueOf 5 fa 6, `data + ''` e' il testo della data, un
         * vettore e' il suo testo. Prima ogni oggetto concatenava. */
        if (exjs_tipo(c, a) == EXJS_OGGETTO) a = exjs_primitivo(c, a, 0);
        if (exjs_tipo(c, b) == EXJS_OGGETTO) b = exjs_primitivo(c, b, 0);
        if (exjs_tipo(c, a) == EXJS_STRINGA || exjs_tipo(c, b) == EXJS_STRINGA)
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

    case TK_POT:
        return exjs_numero(c, exjs_potenza(exjs_a_numero(c, a), exjs_a_numero(c, b)));

    case TK_INSTANCEOF: {
        /* ! SI RISALE LA CATENA DEI PROTOTIPI di `a` cercando b.prototype.
         * I vettori e le funzioni hanno il prototipo consultato e non
         * agganciato (vedi exjs_prendi): li' si guarda la classe. */
        int          fb = exjs_a_oggetto(b), k = exjs_a_oggetto(a), pr, giri = 0;
        ExJsOggetto *F = exjs_ogg(c, fb), *O;

        if (!F || F->classe != EXJS_CL_FUNZIONE)
            return errore(E, n, "'instanceof' vuole una funzione a destra", 0);
        pr = exjs_a_oggetto(exjs_prendi(c, b, "prototype"));
        if (k < 0 || pr < 0) return exjs_booleano(0);
        O = exjs_ogg(c, k);
        if ((O->classe == EXJS_CL_VETTORE && pr == exjs_proto_vet(c)))
            return exjs_booleano(1);
        for (k = O->proto; k >= 0 && giri < 1000; k = exjs_ogg(c, k)->proto, giri++)
            if (k == pr) return exjs_booleano(1);
        return exjs_booleano(0);
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

    if (N->tipo == N_VETTORE || N->tipo == N_OGGETTO) {     /* [a, b] = [b, a] */
        lega(E, dove, v, ambito, 0);
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
 * ITERARE E DESTRUTTURARE (29 settembre 2026)
 * ========================================================================== */
static int nullo_o_indef(ExJsCtx *c, ExJsVal v)
{
    int t = exjs_tipo(c, v);
    return t == EXJS_INDEFINITO || t == EXJS_NULLO;
}

/* Quello su cui si puo' girare con for..of e ...: un vettore com'e', una
 * stringa lettera per lettera, una Map o un Set (le loro voci, "\001v"),
 * qualunque cosa abbia una `length`. Rende sempre un vettore. */
static ExJsVal elementi(Ese *E, ExJsVal v, int n)
{
    ExJsCtx     *c = E->c;
    ExJsOggetto *O = exjs_ogg(c, exjs_a_oggetto(v));
    ExJsVal      out;
    unsigned int i, l;

    if (O && O->classe == EXJS_CL_VETTORE) return v;
    if (exjs_tipo(c, v) == EXJS_STRINGA) {
        const char *s = exjs_a_stringa(c, v);
        out = exjs_vettore(c);
        for (i = 0; s[i]; i++) exjs_indice_metti(c, out, i, exjs_stringa(c, s + i, 1));
        return out;
    }
    if (!O) return errore(E, n, "non si puo' scorrere", exjs_a_stringa(c, v));
    {
        ExJsVal voci = exjs_prendi(c, v, "\001v"), k, w;
        if (exjs_a_oggetto(voci) >= 0) return voci;         /* un Set */
        k = exjs_prendi(c, v, "\001k");
        w = exjs_prendi(c, v, "\001w");
        if (exjs_a_oggetto(k) >= 0) {                       /* una Map: [k, v] */
            out = exjs_vettore(c);
            for (i = 0, l = exjs_lunghezza(c, k); i < l; i++) {
                ExJsVal cp = exjs_vettore(c);
                exjs_indice_metti(c, cp, 0, exjs_indice_prendi(c, k, i));
                exjs_indice_metti(c, cp, 1, exjs_indice_prendi(c, w, i));
                exjs_indice_metti(c, out, i, cp);
            }
            return out;
        }
    }
    {
        double d = exjs_a_numero(c, exjs_prendi(c, v, "length"));
        out = exjs_vettore(c);
        if (!(d > 0 && d < 1e7)) return out;
        for (i = 0, l = (unsigned int)d; i < l; i++) {
            char b[16];
            unsigned int k = 0, x = i;
            char r[16]; int rn = 0;
            do { r[rn++] = (char)('0' + x % 10); x /= 10; } while (x);
            while (rn) b[k++] = r[--rn];
            b[k] = '\0';
            exjs_indice_metti(c, out, i, exjs_prendi(c, v, b));
        }
        return out;
    }
}

/* Mette v nel modello m: un nome, un membro, un elemento, o un modello
 * {..} / [..] con dentro altri modelli, valori predefiniti (N_ASSEGNA) e il
 * resto (N_ESPANDI). `dichiarando` = i nomi nascono in `ambito` (i
 * parametri, le variabili di un giro); altrimenti si assegnano risalendo. */
static void lega(Ese *E, int m, ExJsVal v, int ambito, int dichiarando)
{
    ExJsCtx  *c = E->c;
    ExJsNodo *M;
    int       k;

    if (m < 0 || E->rotto) return;
    M = &E->A->nodi[m];

    switch (M->tipo) {
    case N_NOME:
        if (dichiarando) dichiara(E, ambito, E->A->arena + M->testo, v);
        else             assegna_a(E, m, ambito, v);
        return;

    case N_NULLO:                               /* un buco: [, b] */
        return;

    case N_ASSEGNA:
        if (exjs_tipo(c, v) == EXJS_INDEFINITO) v = valuta(E, M->b, ambito);
        lega(E, M->a, v, ambito, dichiarando);
        return;

    case N_VETTORE: {
        ExJsVal      vet;
        unsigned int i = 0, l;

        if (nullo_o_indef(c, v)) { errore(E, m, "non si puo' destrutturare", exjs_a_stringa(c, v)); return; }
        vet = elementi(E, v, m);
        if (E->rotto) return;
        l = exjs_lunghezza(c, vet);
        for (k = M->a; k >= 0 && !E->rotto; k = E->A->nodi[k].prossimo, i++) {
            if (E->A->nodi[k].tipo == N_ESPANDI) {
                ExJsVal      resto = exjs_vettore(c);
                unsigned int j;
                for (j = i; j < l; j++) exjs_indice_metti(c, resto, j - i, exjs_indice_prendi(c, vet, j));
                lega(E, E->A->nodi[k].a, resto, ambito, dichiarando);
                return;
            }
            lega(E, k, i < l ? exjs_indice_prendi(c, vet, i) : exjs_indefinito(), ambito, dichiarando);
        }
        return;
    }

    case N_OGGETTO:
        if (nullo_o_indef(c, v)) { errore(E, m, "non si puo' destrutturare", exjs_a_stringa(c, v)); return; }
        for (k = M->a; k >= 0 && !E->rotto; k = E->A->nodi[k].prossimo) {
            ExJsNodo *K = &E->A->nodi[k];
            char      nome[128];

            if (K->op == 2) {                   /* ...resto: le proprie non prese */
                ExJsVal r = exjs_oggetto(c);
                int     p, q, ko = exjs_a_oggetto(v);

                for (p = exjs_prop_prima(c, ko); ko >= 0 && p >= 0; p = exjs_prop_prossima(c, p)) {
                    const char *pn = exjs_arena_leggi(c, exjs_prop_nome(c, p));
                    int preso = 0;
                    if (pn[0] == '\001') continue;
                    for (q = M->a; q >= 0 && q != k; q = E->A->nodi[q].prossimo) {
                        const char *qn = E->A->arena + E->A->nodi[q].testo;
                        unsigned int z = 0;
                        while (qn[z] && qn[z] == pn[z]) z++;
                        if (qn[z] == pn[z] && E->A->nodi[q].b < 0) { preso = 1; break; }
                    }
                    if (!preso) exjs_metti(c, r, pn, exjs_prop_val(c, p));
                }
                lega(E, K->a, r, ambito, dichiarando);
                continue;
            }
            if (K->b >= 0) {
                const char  *s = exjs_a_stringa(c, valuta(E, K->b, ambito));
                unsigned int z;
                for (z = 0; s[z] && z + 1 < sizeof(nome); z++) nome[z] = s[z];
                nome[z] = '\0';
            } else {
                const char  *s = E->A->arena + K->testo;
                unsigned int z;
                for (z = 0; s[z] && z + 1 < sizeof(nome); z++) nome[z] = s[z];
                nome[z] = '\0';
            }
            if (E->rotto) return;
            {
                ExJsVal x;
                if (exjs_tipo(c, v) == EXJS_STRINGA && nome[0]=='l' && nome[1]=='e' &&
                    nome[2]=='n' && nome[3]=='g' && nome[4]=='t' && nome[5]=='h' && !nome[6]) {
                    const char *s = exjs_a_stringa(c, v);
                    unsigned int z = 0;
                    while (s[z]) z++;
                    x = exjs_numero(c, (double)z);
                } else x = exjs_prendi(c, v, nome);
                lega(E, K->a, x, ambito, dichiarando);
            }
        }
        return;

    default:
        if (!dichiarando) { assegna_a(E, m, ambito, v); return; }
        errore(E, m, "qui non si puo' dichiarare un nome", 0);
    }
}

/* I campi di una classe (x = 1; senza static) sull'oggetto nuovo. */
static void campi_classe(Ese *E, int cls, ExJsVal questo, int ambito)
{
    int k;

    for (k = E->A->nodi[cls].b; k >= 0 && !E->rotto; k = E->A->nodi[k].prossimo) {
        ExJsNodo *K = &E->A->nodi[k];
        ExJsVal   v;
        char      nome[128];
        const char *s;
        unsigned int z;

        if (K->op != 5 || K->c) continue;
        s = (K->b >= 0) ? exjs_a_stringa(E->c, valuta(E, K->b, ambito)) : E->A->arena + K->testo;
        for (z = 0; s[z] && z + 1 < sizeof(nome); z++) nome[z] = s[z];
        nome[z] = '\0';
        v = (K->a >= 0) ? valuta(E, K->a, ambito) : exjs_indefinito();
        if (E->rotto) return;
        exjs_metti(E->c, questo, nome, v);
    }
}

/* La casa del metodo che sta girando (il prototipo, o la classe per uno
 * static): e' li' che `super` comincia a cercare, un piano sopra. */
static int casa_di(Ese *E, int ambito)
{
    int p = exjs_prop_trova(E->c, ambito, "\001casa", 1);
    return (p >= 0) ? exjs_a_oggetto(exjs_prop_val(E->c, p)) : -1;
}

/* super(...): il costruttore del padre con il nostro this, poi i campi. */
static void super_chiama(Ese *E, int ambito, const ExJsVal *arg, int n_arg, int n)
{
    ExJsCtx     *c = E->c;
    int          casa = casa_di(E, ambito), p;
    ExJsOggetto *C = exjs_ogg(c, casa);
    ExJsVal      questo = exjs_indefinito(), padre, F;

    if (!C || C->proto < 0) { errore(E, n, "super() fuori da una classe derivata", 0); return; }
    p = exjs_prop_trova(c, ambito, "this", 1);
    if (p >= 0) questo = exjs_prop_val(c, p);
    padre = exjs_prendi(c, exjs_da_oggetto(C->proto), "constructor");
    if (exjs_tipo(c, padre) != EXJS_FUNZIONE) { errore(E, n, "il padre non e' una classe", 0); return; }
    chiama(E, padre, questo, arg, n_arg, n);
    if (E->rotto) return;
    F = exjs_prendi(c, exjs_da_oggetto(casa), "constructor");
    {
        int cls = (int)exjs_a_numero(c, exjs_prendi(c, F, "\001classe"));
        if (cls > 0) campi_classe(E, cls, questo, ambito);
    }
}

/* Dopo il corpo di un ciclo: 0 si continua, 1 si esce dal ciclo, 2 si esce
 * e il segnale risale (return, o un break/continue con l'etichetta di un
 * ciclo piu' fuori). `mia` e' l'etichetta di questo ciclo, 0 se non ne ha. */
static int stessa_etichetta(Ese *E, unsigned int a, unsigned int b)
{
    const char *x = E->A->arena + a, *y = E->A->arena + b;

    if (!a || !b) return 0;
    while (*x && *x == *y) { x++; y++; }
    return *x == *y;
}

static int dopo_corpo(Ese *E, unsigned int mia)
{
    if (E->segnale == SEG_ROMPI || E->segnale == SEG_CONTINUA) {
        if (!E->salta || stessa_etichetta(E, E->salta, mia)) {
            int era = E->segnale;
            E->segnale = SEG_AVANTI;
            E->salta   = 0;
            return era == SEG_ROMPI ? 1 : 0;
        }
        return 2;
    }
    return E->segnale == SEG_RITORNA ? 2 : 0;
}

/* =============================================================================
 * VALUTARE UN'ESPRESSIONE
 * ========================================================================== */
/* Un argomento di una chiamata, o tutti quelli di un ...a: rende quanti sono
 * adesso in arg (al massimo 32). */
static int argomento(Ese *E, int a, int ambito, ExJsVal *arg, int na)
{
    if (E->A->nodi[a].tipo == N_ESPANDI) {
        ExJsVal      src = elementi(E, valuta(E, E->A->nodi[a].a, ambito), a);
        unsigned int j, l = exjs_lunghezza(E->c, src);
        for (j = 0; j < l && na < 32 && !E->rotto; j++) arg[na++] = exjs_indice_prendi(E->c, src, j);
        return na;
    }
    if (na < 32) arg[na++] = valuta(E, a, ambito);
    return na;
}

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

        for (e = N->a; e >= 0 && !E->rotto; e = E->A->nodi[e].prossimo) {
            if (E->A->nodi[e].tipo == N_ESPANDI) {          /* [...a, b] */
                ExJsVal      src = elementi(E, valuta(E, E->A->nodi[e].a, ambito), e);
                unsigned int j, l = exjs_lunghezza(c, src);
                for (j = 0; j < l && !E->rotto; j++)
                    exjs_indice_metti(c, v, i++, exjs_indice_prendi(c, src, j));
                continue;
            }
            exjs_indice_metti(c, v, i++, valuta(E, e, ambito));
        }
        return v;
    }

    case N_OGGETTO: {
        ExJsVal o = exjs_oggetto(c);
        int     v;

        for (v = N->a; v >= 0 && !E->rotto; v = E->A->nodi[v].prossimo) {
            ExJsNodo   *V = &E->A->nodi[v];
            char        k[256];
            const char *nome = E->A->arena + V->testo;

            if (V->op == 2) {                               /* {...a} */
                ExJsVal src = valuta(E, V->a, ambito);
                int     ks = exjs_a_oggetto(src), p;
                ExJsOggetto *S = exjs_ogg(c, ks);

                if (S && S->classe == EXJS_CL_VETTORE) {
                    unsigned int j, l = exjs_lunghezza(c, src);
                    for (j = 0; j < l; j++)
                        exjs_metti(c, o, exjs_a_stringa(c, exjs_stringa(c, exjs_a_stringa(c, exjs_numero(c, (double)j)), -1)),
                                   exjs_indice_prendi(c, src, j));
                }
                for (p = exjs_prop_prima(c, ks); ks >= 0 && p >= 0; p = exjs_prop_prossima(c, p)) {
                    const char *pn = exjs_arena_leggi(c, exjs_prop_nome(c, p));
                    if (pn[0] == '\001') continue;
                    exjs_metti(c, o, pn, exjs_prendi(c, src, pn));
                }
                continue;
            }
            if (V->b >= 0) {                                /* [chiave]: */
                const char  *s = exjs_a_stringa(c, valuta(E, V->b, ambito));
                unsigned int z;
                for (z = 0; s[z] && z + 1 < sizeof(k); z++) k[z] = s[z];
                k[z] = '\0';
                nome = k;
            }
            if (V->op == 3 || V->op == 4) {                 /* get x() / set x(v) */
                ExJsVal fn = valuta(E, V->a, ambito);
                exjs_accessore_metti(c, o, nome, V->op == 3 ? fn : exjs_indefinito(),
                                     V->op == 4 ? fn : exjs_indefinito());
                continue;
            }
            {
                ExJsVal x = valuta(E, V->a, ambito);
                if (E->rotto) return x;
                exjs_metti(c, o, nome, x);
            }
        }
        return o;
    }

    case N_CATENA: {
        ExJsVal v;
        E->corto = 0;
        v = valuta(E, N->a, ambito);
        E->corto = 0;
        return v;
    }

    case N_SUPER: {
        ExJsOggetto *C = exjs_ogg(c, casa_di(E, ambito));
        if (!C || C->proto < 0) return errore(E, n, "super fuori da un metodo di classe", 0);
        return exjs_da_oggetto(C->proto);
    }

    case N_CLASSE: {
        ExJsVal      padre = exjs_indefinito(), F, proto;
        int          pp = -1, k;
        ExJsOggetto *FO;

        if (N->a >= 0) {
            padre = valuta(E, N->a, ambito);
            if (E->rotto) return padre;
            if (exjs_tipo(c, padre) != EXJS_FUNZIONE && exjs_tipo(c, padre) != EXJS_NULLO)
                return errore(E, n, "extends vuole una classe", 0);
            if (exjs_tipo(c, padre) == EXJS_FUNZIONE)
                pp = exjs_a_oggetto(exjs_prendi(c, padre, "prototype"));
        }
        F     = fai_funzione(E, N->c, ambito);
        proto = exjs_oggetto(c);
        if (E->rotto || exjs_a_oggetto(proto) < 0) return errore(E, n, "memoria esaurita", 0);
        FO = exjs_ogg(c, exjs_a_oggetto(F));
        exjs_ogg(c, exjs_a_oggetto(proto))->proto = pp;
        /* ! LA CLASSE RISALE AL PADRE: cosi' i metodi static si ereditano. */
        if (exjs_tipo(c, padre) == EXJS_FUNZIONE) FO->proto = exjs_a_oggetto(padre);
        FO->dato = (void *)(long)(exjs_a_oggetto(proto) + 1);
        exjs_metti(c, F, "prototype", proto);
        exjs_metti(c, proto, "constructor", F);
        exjs_metti(c, F, "\001classe", exjs_numero(c, (double)n));

        for (k = N->b; k >= 0 && !E->rotto; k = E->A->nodi[k].prossimo) {
            ExJsNodo   *V = &E->A->nodi[k];
            ExJsVal     dest = V->c ? F : proto, x;
            char        kk[256];
            const char *nome = E->A->arena + V->testo;

            if (V->b >= 0) {
                const char  *s = exjs_a_stringa(c, valuta(E, V->b, ambito));
                unsigned int z;
                for (z = 0; s[z] && z + 1 < sizeof(kk); z++) kk[z] = s[z];
                kk[z] = '\0';
                nome = kk;
            }
            if (V->op == 5) {                               /* un campo */
                if (V->c) exjs_metti(c, F, nome, V->a >= 0 ? valuta(E, V->a, ambito) : exjs_indefinito());
                continue;
            }
            x = valuta(E, V->a, ambito);
            if (E->rotto) return x;
            {
                ExJsOggetto *M = exjs_ogg(c, exjs_a_oggetto(x));
                if (M && M->nodo >= 0) M->dato = (void *)(long)(exjs_a_oggetto(dest) + 1);
            }
            if (V->op == 3 || V->op == 4)
                exjs_accessore_metti(c, dest, nome, V->op == 3 ? x : exjs_indefinito(),
                                     V->op == 4 ? x : exjs_indefinito());
            else exjs_metti(c, dest, nome, x);
        }
        return F;
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
        /* ! delete TOGLIE DAVVERO (29 settembre 2026): prima rendeva true e
         * la proprieta' restava, e `'x' in o` dopo delete o.x era ancora
         * vero. Su un elemento di vettore lascia un buco (undefined). */
        if (N->op == TK_DELETE && (E->A->nodi[N->a].tipo == N_MEMBRO ||
                                   E->A->nodi[N->a].tipo == N_INDICE)) {
            ExJsNodo *M = &E->A->nodi[N->a];
            ExJsVal   o = valuta(E, M->a, ambito), k;
            char      nome[128];
            const char *s;
            unsigned int z;
            ExJsOggetto *O;

            if (E->rotto) return o;
            if (M->tipo == N_MEMBRO) s = E->A->arena + M->testo;
            else {
                k = valuta(E, M->b, ambito);
                if (E->rotto) return k;
                O = exjs_ogg(c, exjs_a_oggetto(o));
                if (O && O->classe == EXJS_CL_VETTORE && exjs_tipo(c, k) == EXJS_NUMERO) {
                    double d = exjs_a_numero(c, k);
                    if (d >= 0 && d < (double)exjs_lunghezza(c, o) && d == (double)(long)d)
                        exjs_indice_metti(c, o, (unsigned int)d, exjs_indefinito());
                    return exjs_booleano(1);
                }
                s = exjs_a_stringa(c, k);
            }
            for (z = 0; s[z] && z + 1 < sizeof(nome); z++) nome[z] = s[z];
            nome[z] = '\0';
            exjs_togli(c, o, nome);
            return exjs_booleano(1);
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
            case TK_DELETE: return exjs_booleano(1);   /* delete x: vedi sopra */
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
        if (N->op == TK_NULLISH) return nullo_o_indef(c, a) ? valuta(E, N->b, ambito) : a;
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

        /* a ??= b, a ||= b, a &&= b: si scrive solo se serve */
        if (N->op == TK_NULLISH_UG || N->op == TK_O_O_UG || N->op == TK_E_E_UG) {
            ExJsVal vecchio = valuta(E, N->a, ambito);
            int     fai;

            if (E->rotto) return vecchio;
            fai = (N->op == TK_NULLISH_UG) ? nullo_o_indef(c, vecchio)
                : (N->op == TK_O_O_UG)     ? !exjs_a_booleano(c, vecchio)
                :                             exjs_a_booleano(c, vecchio);
            if (!fai) return vecchio;
            v = valuta(E, N->b, ambito);
            if (E->rotto) return v;
            assegna_a(E, N->a, ambito, v);
            return v;
        }

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
            case TK_POT_UG:   op = TK_POT;    break;
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
        if (E->corto & 1) return exjs_indefinito();
        if (N->op == 1 && nullo_o_indef(c, o)) { E->corto |= 1; return exjs_indefinito(); }
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
        if (E->corto & 1) return exjs_indefinito();
        if (N->op == 1 && nullo_o_indef(c, o)) { E->corto |= 1; return exjs_indefinito(); }
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
        /* 'abc'[1] e' "b" (29 settembre 2026): prima cercava una proprieta'
         * "1" sul prototipo delle stringhe, e rendeva undefined. Byte, come
         * charAt. */
        if (k < 0 && exjs_tipo(c, o) == EXJS_STRINGA && exjs_tipo(c, i) == EXJS_NUMERO) {
            double      d = exjs_a_numero(c, i);
            const char *s = exjs_a_stringa(c, o);
            unsigned int l = 0;

            while (s[l]) l++;
            if (d >= 0 && d < (double)l && d == (double)(long)d)
                return exjs_stringa(c, s + (long)d, 1);
            return exjs_indefinito();
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
        ExJsVal arg[32], f, questo = exjs_indefinito();
        int     a, na = 0;

        /* super(...) nel costruttore di una classe derivata */
        if (E->A->nodi[N->a].tipo == N_SUPER) {
            for (a = N->b; a >= 0 && na < 32; a = E->A->nodi[a].prossimo)
                na = argomento(E, a, ambito, arg, na);
            if (E->rotto) return exjs_indefinito();
            super_chiama(E, ambito, arg, na, n);
            return exjs_indefinito();
        }

        /* ! `this` E' L'OGGETTO PRIMA DEL PUNTO, e si prende QUI: `o.f()` deve
         * vedere `o` dentro `f`, e per saperlo bisogna guardare la forma della
         * chiamata, non il valore della funzione. E' il motivo per cui in
         * JavaScript `var g = o.f; g()` perde `this`. */
        if (E->A->nodi[N->a].tipo == N_MEMBRO) {
            ExJsNodo *M = &E->A->nodi[N->a];
            questo = valuta(E, M->a, ambito);
            if (E->rotto || (E->corto & 1)) return exjs_indefinito();
            if (M->op == 1 && nullo_o_indef(c, questo)) { E->corto |= 1; return exjs_indefinito(); }
            f = exjs_prendi(c, questo, E->A->arena + M->testo);
            /* super.metodo(): il metodo del padre, ma col nostro this */
            if (E->A->nodi[M->a].tipo == N_SUPER) {
                int p = exjs_prop_trova(c, ambito, "this", 1);
                questo = (p >= 0) ? exjs_prop_val(c, p) : exjs_indefinito();
            }
        } else {
            f = valuta(E, N->a, ambito);
        }
        if (E->rotto || (E->corto & 1)) return exjs_indefinito();
        if (N->op == 1 && nullo_o_indef(c, f)) { E->corto |= 1; return exjs_indefinito(); }

        for (a = N->b; a >= 0 && na < 32; a = E->A->nodi[a].prossimo)
            na = argomento(E, a, ambito, arg, na);
        if (E->rotto) return exjs_indefinito();

        return chiama(E, f, questo, arg, na, n);
    }

    case N_NUOVO: {
        ExJsVal arg[32], f, ogg;
        int     a, na = 0;

        f = valuta(E, N->a, ambito);
        for (a = N->b; a >= 0 && na < 32; a = E->A->nodi[a].prossimo)
            na = argomento(E, a, ambito, arg, na);
        if (E->rotto) return exjs_indefinito();

        /* ! `new` FA UN OGGETTO NUOVO, LO PASSA COME `this`, E LO RENDE — a
         * meno che il costruttore non renda a sua volta un oggetto. La seconda
         * meta' e' quella che si dimentica, e le librerie vere la usano. */
        ogg = exjs_oggetto(c);
        /* ! L'OGGETTO NUOVO EREDITA DA f.prototype (29 settembre 2026): i
         * metodi scritti su F.prototype si vedono da ogni `new F()`. Prima
         * l'oggetto nasceva senza prototipo, e solo Date se lo agganciava. */
        {
            ExJsOggetto *F = exjs_ogg(c, exjs_a_oggetto(f));
            if (F && F->classe == EXJS_CL_FUNZIONE && exjs_a_oggetto(ogg) >= 0) {
                int pr = exjs_a_oggetto(exjs_prendi(c, f, "prototype"));
                if (pr >= 0) exjs_ogg(c, exjs_a_oggetto(ogg))->proto = pr;
            }
        }
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

/* =============================================================================
 * LE ECCEZIONI (@EXJS-LACUNE, 29 settembre 2026)
 * ========================================================================== */
static void lancia(Ese *E, int n, ExJsVal v)
{
    /* Il messaggio e' per chi non la prende: finisce nell'errore dello
     * script, come «eccezione non presa: TypeError: ...». */
    char         t[EXJS_ERR_LEN];
    const char  *s = exjs_a_stringa(E->c, v);
    unsigned int i;

    for (i = 0; s[i] && i + 1 < sizeof(t); i++) t[i] = s[i];
    t[i] = '\0';
    errore(E, n, "eccezione non presa", t);
    E->ha_valore = 1;
    E->eccezione = v;
}

/* Il valore che riceve il catch: quello lanciato, o un TypeError fatto col
 * messaggio del motore. */
static ExJsVal eccezione_valore(Ese *E)
{
    ExJsVal f, m;

    if (E->ha_valore) return E->eccezione;
    /* Un nome che non c'e' e' un ReferenceError; il resto, TypeError. */
    {
        const char *r = "nome non definito", *m = E->messaggio;
        while (*r && *r == *m) { r++; m++; }
        f = exjs_prendi(E->c, exjs_globale(E->c), *r ? "TypeError" : "ReferenceError");
    }
    m = exjs_stringa(E->c, E->messaggio, -1);
    if (exjs_a_oggetto(f) < 0) return m;
    return chiama(E, f, exjs_indefinito(), &m, 1, -1);
}

static void spegni(Ese *E)
{
    E->rotto       = 0;
    E->ha_valore   = 0;
    E->catturabile = 0;
    E->eccezione   = exjs_indefinito();
    E->messaggio[0] = '\0';
    if (E->err) { E->err->messaggio[0] = '\0'; E->err->riga = 0; E->err->colonna = 0; }
}

static void esegui_prova(Ese *E, ExJsNodo *N, int ambito)
{
    /* Copiati: da qui si esegue altro codice, e N e' un puntatore dentro
     * l'albero, che non deve essere l'unica cosa a cui ci si appoggia. */
    int a = N->a, b = N->b, cc = N->c, d = N->d;

    esegui(E, a, ambito);

    if (E->rotto && E->catturabile && cc >= 0) {
        int amb;
        ExJsVal v;

        E->rotto = 0;                       /* per poter costruire il TypeError */
        v = eccezione_valore(E);
        spegni(E);
        amb = ambito_nuovo(E, ambito);
        if (amb < 0) { errore(E, cc, "memoria esaurita", 0); return; }
        if (b >= 0) dichiara(E, amb, E->A->arena + E->A->nodi[b].testo, v);
        esegui(E, cc, amb);
    }

    /* ! finally GIRA COMUNQUE, e poi si torna a come si era usciti — a meno
     * che finally stesso non esca in un altro modo (return, break, throw):
     * allora vince lui. Non gira dopo la guardia dei passi. */
    if (d >= 0 && (!E->rotto || E->catturabile)) {
        int         rotto = E->rotto, catt = E->catturabile, hv = E->ha_valore;
        int         seg = E->segnale;
        unsigned int salta = E->salta;
        ExJsVal     ecc = E->eccezione, rit = E->ritorno;
        char        msg[EXJS_ERR_LEN];
        ExJsErrore  err;
        unsigned int i;

        for (i = 0; i < EXJS_ERR_LEN; i++) msg[i] = E->messaggio[i];
        if (E->err) err = *E->err;
        E->rotto = 0;
        E->segnale = SEG_AVANTI;
        esegui(E, d, ambito);
        if (!E->rotto && E->segnale == SEG_AVANTI) {
            E->rotto = rotto; E->catturabile = catt; E->ha_valore = hv;
            E->segnale = seg; E->eccezione = ecc; E->ritorno = rit; E->salta = salta;
            for (i = 0; i < EXJS_ERR_LEN; i++) E->messaggio[i] = msg[i];
            if (E->err) *E->err = err;
        }
    }
}

int exjs_interrotto(ExJsCtx *c)
{
    Ese *E = (Ese *)exjs_ese_prendi(c);
    return E ? E->rotto : 0;
}

void exjs_lancia(ExJsCtx *c, ExJsVal v)
{
    Ese *E = (Ese *)exjs_ese_prendi(c);

    if (!E || E->rotto) return;
    lancia(E, -1, v);
}

void exjs_lancia_errore(ExJsCtx *c, const char *tipo, const char *msg)
{
    Ese    *E = (Ese *)exjs_ese_prendi(c);
    ExJsVal f, m;

    if (!E || E->rotto) return;
    f = exjs_prendi(c, exjs_globale(c), tipo);
    m = exjs_stringa(c, msg, -1);
    if (exjs_a_oggetto(f) >= 0) m = chiama(E, f, exjs_indefinito(), &m, 1, -1);
    if (!E->rotto) lancia(E, -1, m);
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
        if (N->op) {                        /* let, const, class */
            int amb = ambito;
            if (N->tipo == N_BLOCCO)
                amb = (N->op & 2) ? ambito_nuovo(E, ambito) : ambito_riusato(E, n, ambito);
            if (amb < 0) { errore(E, n, "memoria esaurita", 0); return; }
            dichiara_lessicali(E, N->a, amb);
            esegui_lista(E, N->a, amb);
            return;
        }
        esegui_lista(E, N->a, ambito);
        return;

    case N_CLASSE: {                        /* class A {} come istruzione */
        ExJsVal v = valuta(E, n, ambito);
        if (!E->rotto && N->testo) assegna_a_nome(E, ambito, E->A->arena + N->testo, v);
        return;
    }

    case N_ETICHETTA: {
        int t = E->A->nodi[N->a].tipo;

        /* l'etichetta la prende il ciclo, se e' un ciclo; `fuori: { ... break
         * fuori; }` la consuma qui */
        if (t == N_MENTRE || t == N_FAI || t == N_PER || t == N_PER_IN || t == N_PER_DI)
            E->etichetta_ciclo = N->testo;
        esegui(E, N->a, ambito);
        E->etichetta_ciclo = 0;
        if (E->segnale == SEG_ROMPI && stessa_etichetta(E, E->salta, N->testo)) {
            E->segnale = SEG_AVANTI;
            E->salta   = 0;
        }
        return;
    }

    case N_PER_DI: {
        unsigned int mia = E->etichetta_ciclo, i;
        ExJsVal      src, vet;
        int          dec = N->a >= 0 && E->A->nodi[N->a].tipo == N_VAR;
        int          lex = dec && E->A->nodi[N->a].op;
        int          giro = lex && (N->op & 1);

        E->etichetta_ciclo = 0;
        src = valuta(E, N->b, ambito);
        if (E->rotto) return;
        vet = elementi(E, src, n);
        if (E->rotto) return;
        {
        int fuori = ambito;
        if (lex && !giro) {
            fuori = ambito_riusato(E, n, ambito);
            if (fuori < 0) { errore(E, n, "memoria esaurita", 0); return; }
            dichiara_lessicali(E, N->a, fuori);
        }
        for (i = 0; i < exjs_lunghezza(E->c, vet) && !E->rotto; i++) {
            ExJsVal x = exjs_indice_prendi(E->c, vet, i);
            int     it = fuori, k;

            if (giro) {
                it = ambito_nuovo(E, ambito);
                if (it < 0) { errore(E, n, "memoria esaurita", 0); return; }
            }
            if (dec) {
                ExJsNodo *D = &E->A->nodi[E->A->nodi[N->a].a];
                if (D->b >= 0) lega(E, D->b, x, it, giro);
                else if (giro) dichiara(E, it, E->A->arena + D->testo, x);
                else assegna_a_nome(E, it, E->A->arena + D->testo, x);
            } else lega(E, N->a, x, fuori, 0);
            esegui(E, N->d, it);
            k = dopo_corpo(E, mia);
            if (k == 1) break;
            if (k == 2) return;
            if (!passo(E, n)) return;
        }
        }
        return;
    }

    case N_VUOTO:
        return;

    case N_ESPR:
        E->ritorno = valuta(E, N->a, ambito);   /* l'ultimo valore, per il banco */
        return;

    case N_VAR: {
        int d;
        for (d = N->a; d >= 0 && !E->rotto; d = E->A->nodi[d].prossimo) {
            ExJsNodo *D = &E->A->nodi[d];
            ExJsVal   v;
            /* Il nome e' gia' stato issato: qui si assegna soltanto, e SOLO se
             * c'e' un valore. `var a;` dopo `a = 1` non deve azzerare `a`;
             * `let a;` invece si', a ogni giro. */
            if (D->a < 0 && !N->op && D->b < 0) continue;
            v = (D->a >= 0) ? valuta(E, D->a, ambito) : exjs_indefinito();
            if (E->rotto) return;
            if (D->b >= 0) lega(E, D->b, v, ambito, 0);
            else           assegna_a_nome(E, ambito, E->A->arena + D->testo, v);
        }
        return;
    }

    case N_FUNZIONE:
        return;                             /* gia' issata */

    case N_SE:
        if (exjs_a_booleano(E->c, valuta(E, N->a, ambito))) esegui(E, N->b, ambito);
        else                                                esegui(E, N->c, ambito);
        return;

    case N_MENTRE: {
        unsigned int mia = E->etichetta_ciclo;
        int          k;

        E->etichetta_ciclo = 0;
        while (!E->rotto && exjs_a_booleano(E->c, valuta(E, N->a, ambito))) {
            esegui(E, N->b, ambito);
            k = dopo_corpo(E, mia);
            if (k == 1) break;
            if (k == 2) return;
            if (!passo(E, n)) return;
        }
        return;
    }

    case N_FAI: {
        unsigned int mia = E->etichetta_ciclo;
        int          k;

        E->etichetta_ciclo = 0;
        do {
            esegui(E, N->b, ambito);
            k = dopo_corpo(E, mia);
            if (k == 1) break;
            if (k == 2) return;
            if (!passo(E, n)) return;
        } while (!E->rotto && exjs_a_booleano(E->c, valuta(E, N->a, ambito)));
        return;
    }

    case N_PER: {
        /* ! for (let i ...) CON UNA CHIUSURA DENTRO HA UN AMBITO PER GIRO,
         * copiato dal giro prima (la norma lo chiama per-iteration
         * environment): ogni funzione nata nel giro si ricorda la SUA i.
         * Senza chiusure non serve, e non si paga: vedi ambito_riusato. */
        unsigned int mia = E->etichetta_ciclo;
        int          lex = N->a >= 0 && E->A->nodi[N->a].tipo == N_VAR && E->A->nodi[N->a].op;
        int          giro = lex && (N->op & 1), it = ambito, k;

        E->etichetta_ciclo = 0;
        if (lex) {
            it = giro ? ambito_nuovo(E, ambito) : ambito_riusato(E, n, ambito);
            if (it < 0) { errore(E, n, "memoria esaurita", 0); return; }
            dichiara_lessicali(E, N->a, it);
        }
        if (N->a >= 0) esegui(E, N->a, it);
        if (giro) it = copia_ambito(E, it, ambito);
        while (!E->rotto && it >= 0) {
            if (N->b >= 0 && !exjs_a_booleano(E->c, valuta(E, N->b, it))) break;
            esegui(E, N->d, it);
            k = dopo_corpo(E, mia);
            if (k == 1) break;
            if (k == 2) return;
            if (giro) {
                it = copia_ambito(E, it, ambito);
                if (it < 0) { errore(E, n, "memoria esaurita", 0); return; }
            }
            if (N->c >= 0) valuta(E, N->c, it);
            if (!passo(E, n)) return;
        }
        return;
    }

    case N_PER_IN: {
        ExJsVal      o = valuta(E, N->b, ambito);
        int          k = exjs_a_oggetto(o);
        ExJsOggetto *O = exjs_ogg(E->c, k);
        const char  *nome_var = 0;

        unsigned int mia = E->etichetta_ciclo;
        int          lex = E->A->nodi[N->a].tipo == N_VAR && E->A->nodi[N->a].op;
        int          giro = lex && (N->op & 1), kk;

        E->etichetta_ciclo = 0;
        if (E->rotto || !O) return;
        if (lex && !giro) {
            ambito = ambito_riusato(E, n, ambito);
            if (ambito < 0) { errore(E, n, "memoria esaurita", 0); return; }
            dichiara_lessicali(E, N->a, ambito);
        }

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

                {
                    int it = giro ? ambito_nuovo(E, ambito) : ambito;
                    if (it < 0) { errore(E, n, "memoria esaurita", 0); return; }
                    if (giro) dichiara(E, it, nome_var, exjs_stringa(E->c, b, -1));
                    else assegna_a_nome(E, ambito, nome_var, exjs_stringa(E->c, b, -1));
                    esegui(E, N->d, it);
                }
                kk = dopo_corpo(E, mia);
                if (kk == 1) break;
                if (kk == 2) return;
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

                /* I nomi che cominciano con \001 sono del motore (il tempo di
                 * una Date): non si enumerano. */
                if (exjs_arena_leggi(E->c, exjs_prop_nome(E->c, p))[0] == '\001') {
                    p = prossima;
                    continue;
                }

                {
                    int it = giro ? ambito_nuovo(E, ambito) : ambito;
                    ExJsVal kv = exjs_stringa_off(E->c, exjs_prop_nome(E->c, p));
                    if (it < 0) { errore(E, n, "memoria esaurita", 0); return; }
                    if (giro) dichiara(E, it, nome_var, kv);
                    else assegna_a_nome(E, ambito, nome_var, kv);
                    esegui(E, N->d, it);
                }
                kk = dopo_corpo(E, mia);
                if (kk == 1) break;
                if (kk == 2) return;
                p = prossima;
            }
        }
        return;
    }

    case N_RITORNA:
        E->ritorno = (N->a >= 0) ? valuta(E, N->a, ambito) : exjs_indefinito();
        E->segnale = SEG_RITORNA;
        return;

    case N_ROMPI:    E->segnale = SEG_ROMPI;    E->salta = N->testo; return;
    case N_CONTINUA: E->segnale = SEG_CONTINUA; E->salta = N->testo; return;

    case N_LANCIA: {
        ExJsVal v = valuta(E, N->a, ambito);
        if (E->rotto) return;
        lancia(E, n, v);
        return;
    }

    case N_PROVA:
        esegui_prova(E, N, ambito);
        return;

    case N_SCEGLI: {
        ExJsVal v = valuta(E, N->a, ambito);
        int     k, da = -1, predef = -1;

        if (E->rotto) return;
        /* ! I case SI PROVANO IN ORDINE, e default solo alla fine, dovunque
         * sia scritto: e' la norma, e il confronto e' ===. */
        for (k = N->b; k >= 0 && da < 0; k = E->A->nodi[k].prossimo) {
            ExJsNodo *K = &E->A->nodi[k];
            if (K->a < 0) { predef = k; continue; }
            {
                ExJsVal t = valuta(E, K->a, ambito);
                if (E->rotto) return;
                if (identici(E->c, v, t)) da = k;
            }
        }
        if (da < 0) da = predef;
        /* Da li' si cade in quelli dopo, finche' un break non ferma. */
        for (k = da; k >= 0 && !E->rotto && E->segnale == SEG_AVANTI;
             k = E->A->nodi[k].prossimo)
            esegui_lista(E, E->A->nodi[k].b, ambito);
        if (E->segnale == SEG_ROMPI && !E->salta) E->segnale = SEG_AVANTI;
        return;
    }

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
    E.catturabile = 0;
    E.ha_valore  = 0;
    E.eccezione  = exjs_indefinito();
    E.messaggio[0] = '\0';
    E.nodo_ora   = -1;
    E.corto      = 0;
    E.salta      = 0;
    E.etichetta_ciclo = 0;

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

/* =============================================================================
 * CHIAMARE UNA FUNZIONE DA FUORI
 *
 * ! QUESTA E' NATA IL GIORNO DEGLI EVENTI, e il difetto che l'ha fatta nascere
 * merita di restare scritto. exjs_chiama funziona solo mentre il motore gira,
 * ed era giusto finche' a chiamare erano `forEach` e i lavori scaduti — che
 * girano dentro exjs_esegui o dentro exjs_pompa. Un gestore di clic no: il
 * browser lo fa partire dal suo ciclo di messaggi, quando nessuno script sta
 * girando. Chiamandolo di li' si sarebbe presa la risposta onesta di
 * exjs_chiama — «fuori da un'esecuzione» — cioe' nessun gestore avrebbe mai
 * funzionato.
 *
 * ! SE UN'ESECUZIONE C'E' GIA', SI USA QUELLA. Metterne una nuova sopra
 * azzererebbe il conto dei passi e della profondita', e sarebbe la strada per
 * far girare all'infinito uno script che si fa partire un evento da se'.
 * ========================================================================== */
ExJsVal exjs_invoca(ExJsCtx *c, ExJsVal f, ExJsVal questo,
                    const ExJsVal *arg, int n_arg, ExJsErrore *err)
{
    Ese     E;
    ExJsVal r;

    if (!c) return exjs_indefinito();
    if (exjs_ese_prendi(c)) return exjs_chiama(c, f, questo, arg, n_arg, err);
    if (!exjs_ast_pronto(c)) {
        /* Nessuno script ha ancora girato: non c'e' albero, quindi non c'e'
         * nemmeno una funzione da chiamare. */
        return exjs_indefinito();
    }

    E.c          = c;
    E.A          = exjs_ctx_ast(c);
    E.err        = err;
    E.rotto      = 0;
    E.passi      = 0;
    E.profondita = 0;
    E.segnale    = SEG_AVANTI;
    E.ritorno    = exjs_indefinito();
    E.catturabile = 0;
    E.ha_valore  = 0;
    E.eccezione  = exjs_indefinito();
    E.messaggio[0] = '\0';
    E.nodo_ora   = -1;
    E.corto      = 0;
    E.salta      = 0;
    E.etichetta_ciclo = 0;

    if (err) { err->messaggio[0] = '\0'; err->riga = 0; err->colonna = 0; }

    exjs_ese_metti(c, &E);
    r = chiama(&E, f, questo, arg, n_arg, -1);
    exjs_ese_metti(c, 0);
    return r;
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
    E.catturabile = 0;
    E.ha_valore  = 0;
    E.eccezione  = exjs_indefinito();
    E.messaggio[0] = '\0';
    E.nodo_ora   = -1;
    E.corto      = 0;
    E.salta      = 0;
    E.etichetta_ciclo = 0;

    if (err) { err->messaggio[0] = '\0'; err->riga = 0; err->colonna = 0; }

    /* Da qui in poi le funzioni native possono richiamare il motore: vedi
     * exjs_chiama. Si toglie prima di uscire, o resterebbe un puntatore a una
     * struttura sulla pila che non esiste piu'.
     *
     * ! SI RIMETTE QUELLA DI PRIMA, NON ZERO, perche' exjs_esegui puo' trovarsi
     * dentro un'altra esecuzione: un gestore scritto in un attributo —
     * `onclick="..."` — e' del testo che si esegue mentre uno script sta gia'
     * girando, se e' stato quello a far partire l'evento. Azzerando, il motore
     * di fuori si sarebbe ritrovato senza esecuzione a meta' strada, e la
     * prossima funzione nativa che avesse richiamato il motore avrebbe detto
     * «fuori da un'esecuzione» in mezzo a uno script perfettamente sano. */
    {
        void *prima = exjs_ese_prendi(c);

        exjs_ese_metti(c, &E);

        /* ! LE DICHIARAZIONI DI TUTTO IL PROGRAMMA PRIMA DI ESEGUIRE LA PRIMA
         * ISTRUZIONE. Vedi issa(): senza, una funzione non puo' chiamarne una
         * scritta piu' sotto. */
        issa(&E, A->nodi[A->radice].a, exjs_globale_idx(c));
        esegui(&E, A->radice, exjs_globale_idx(c));

        exjs_ese_metti(c, prima);
    }

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
