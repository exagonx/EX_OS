/* =============================================================================
 * lib/exjs/exjs_int.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Quello che i pezzi di ExJs si dicono fra loro, e che fuori non si vede.
 *
 * ! NON E' UN SECONDO exjs.h. Li' c'e' il contratto con chi USA il motore, e
 * quello deve restare identico anche il giorno che sotto ci mettiamo QuickJS.
 * Qui c'e' la forma di cio' che sta dentro, e questa puo' cambiare a ogni
 * scaglione: se una struttura di questo file finisce in un header pubblico,
 * il motore ha smesso di essere sostituibile.
 * ============================================================================= */

#ifndef EXJS_INT_H
#define EXJS_INT_H

#include "exjs.h"

/* =============================================================================
 * I GETTONI
 *
 * ! I NUMERI NON SI SCELGONO A CASO: i primi coincidono con il carattere che
 * li rappresenta, cosi' un gettone di un carattere solo si confronta con
 * `t == '('` invece che con un nome inventato. Quelli veri cominciano dopo
 * l'ASCII, dove non possono collidere con niente.
 * ========================================================================== */
#define TK_FINE        0
#define TK_ERRORE      1

#define TK_PRIMO       128
#define TK_NOME        128      /* identificatore */
#define TK_NUMERO      129
#define TK_STRINGA     130

/* Operatori di piu' caratteri. Uno per riga, e il nome dice come si scrive. */
#define TK_UGUALE      140      /* ==   */
#define TK_DIVERSO     141      /* !=   */
#define TK_ID_UGUALE   142      /* ===  */
#define TK_ID_DIVERSO  143      /* !==  */
#define TK_MIN_UG      144      /* <=   */
#define TK_MAG_UG      145      /* >=   */
#define TK_E_E         146      /* &&   */
#define TK_O_O         147      /* ||   */
#define TK_PIU_PIU     148      /* ++   */
#define TK_MENO_MENO   149      /* --   */
#define TK_PIU_UG      150      /* +=   */
#define TK_MENO_UG     151      /* -=   */
#define TK_PER_UG      152      /* *=   */
#define TK_DIV_UG      153      /* /=   */
#define TK_MOD_UG      154      /* %=   */
#define TK_SHL         155      /* <<   */
#define TK_SHR         156      /* >>   */
#define TK_SHR_U       157      /* >>>  */
#define TK_SHL_UG      158      /* <<=  */
#define TK_SHR_UG      159      /* >>=  */
#define TK_SHR_U_UG    160      /* >>>= */
#define TK_AND_UG      161      /* &=   */
#define TK_OR_UG       162      /* |=   */
#define TK_XOR_UG      163      /* ^=   */

/* Le parole chiave. Sono gettoni a se' e non nomi, perche' `if` non e' una
 * variabile che si possa chiamare cosi': deciderlo qui evita che il
 * costruttore dell'albero confronti stringhe a ogni passo. */
#define TK_VAR         180
#define TK_FUNCTION    181
#define TK_RETURN      182
#define TK_IF          183
#define TK_ELSE        184
#define TK_WHILE       185
#define TK_FOR         186
#define TK_BREAK       187
#define TK_CONTINUE    188
#define TK_NEW         189
#define TK_DELETE      190
#define TK_TYPEOF      191
#define TK_IN          192
#define TK_INSTANCEOF  193
#define TK_THIS        194
#define TK_NULL        195
#define TK_TRUE        196
#define TK_FALSE       197
#define TK_DO          198
#define TK_SWITCH      199
#define TK_CASE        200
#define TK_DEFAULT     201
#define TK_TRY         202
#define TK_CATCH       203
#define TK_FINALLY     204
#define TK_THROW       205
#define TK_VOID        206

/* =============================================================================
 * IL LETTORE DI GETTONI
 *
 * ! SI TIENE ANCHE DOV'ERA IL GETTONE, non solo cos'era. Riga e colonna
 * servono al messaggio d'errore, e ricavarle dopo — ricontando le righe dal
 * principio a ogni errore — vorrebbe dire un secondo pezzo di codice che deve
 * essere d'accordo col primo su cosa sia una riga.
 *
 * ! E SI TIENE SE PRIMA C'ERA UN A CAPO. Serve all'inserimento automatico del
 * punto e virgola, che in JavaScript non e' una comodita': `return` seguito da
 * un a capo rende `undefined` qualunque cosa ci sia sulla riga dopo, e un
 * motore che non lo sapesse eseguirebbe un programma diverso da quello scritto.
 * ========================================================================== */
typedef struct {
    const char  *sorgente;
    unsigned int n;
    unsigned int pos;

    int          riga, colonna;

    /* il gettone corrente */
    int          tipo;
    unsigned int inizio, fine;      /* scostamenti nel sorgente */
    int          t_riga, t_colonna;
    int          a_capo_prima;      /* c'era un fine riga prima di questo? */

    double       numero;            /* se tipo == TK_NUMERO */

    /* Le stringhe arrivano gia' con gli scappamenti sciolti: chi legge un
     * gettone non deve rifare il lavoro dell'analizzatore. */
    char        *testo;
    unsigned int testo_max, testo_n;

    ExJsErrore  *err;
} ExJsLex;

/* =============================================================================
 * L'ALBERO
 *
 * ! E' UN VETTORE PIATTO CON GLI INDICI, come HtmlDoc, e per le stesse due
 * ragioni. La prima e' il tetto: chi apre la pagina decide quanti nodi puo'
 * costare uno script, e lo decide UNA VOLTA invece di scoprirlo mentre
 * l'allocatore dice di no a meta' analisi. La seconda e' che un albero fatto
 * di puntatori sparsi si percorre saltando per tutta la memoria, e su una
 * macchina piccola quei salti sono il costo vero.
 *
 * ! LE LISTE SI FANNO CON `prossimo`, non con un vettore di figli. Gli
 * argomenti di una chiamata, le istruzioni di un blocco, le voci di un oggetto:
 * sono tutte liste di lunghezza ignota fino alla fine, e una catena non ha
 * bisogno di sapere quanto sara' lunga prima di cominciare.
 *
 * ! E OGNI NODO SI RICORDA LA SUA RIGA. Serve agli errori di ESECUZIONE, non a
 * quelli di sintassi: «undefined non e' una funzione» senza un numero di riga
 * e' esattamente il messaggio che ha reso JavaScript famoso per le ragioni
 * sbagliate.
 * ========================================================================== */

/* --- espressioni --- */
#define N_NUMERO        1
#define N_STRINGA       2
#define N_NOME          3
#define N_VERO          4
#define N_FALSO         5
#define N_NULLO         6
#define N_QUESTO        7
#define N_VETTORE       8       /* [1, 2]      a = primo elemento     */
#define N_OGGETTO       9       /* {a: 1}      a = prima voce         */
#define N_VOCE         10       /* una voce di oggetto: testo, a=valore */
#define N_FUNZIONE     11       /* testo=nome, a=parametri, b=corpo   */
#define N_PARAMETRO    12
#define N_UNARIO       13       /* op, a                              */
#define N_BINARIO      14       /* op, a, b                           */
#define N_LOGICO       15       /* && ||  — a corto circuito          */
#define N_ASSEGNA      16       /* op (= o += ...), a=dove, b=cosa    */
#define N_CONDIZIONE   17       /* a ? b : c                          */
#define N_CHIAMATA     18       /* a=chi, b=primo argomento           */
#define N_NUOVO        19       /* new: a=chi, b=primo argomento      */
#define N_MEMBRO       20       /* a.nome:  a=oggetto, testo=nome     */
#define N_INDICE       21       /* a[b]                               */
#define N_PRE          22       /* ++a  --a:  op, a                   */
#define N_POST         23       /* a++  a--:  op, a                   */
#define N_VIRGOLA      24       /* a, b                               */

/* --- istruzioni --- */
#define N_PROGRAMMA    40       /* a = prima istruzione               */
#define N_BLOCCO       41
#define N_VAR          42       /* a = prima dichiarazione            */
#define N_DICHIARA     43       /* testo=nome, a=valore iniziale o -1 */
#define N_ESPR         44
#define N_SE           45       /* a=prova, b=allora, c=altrimenti    */
#define N_MENTRE       46       /* a=prova, b=corpo                   */
#define N_FAI          47       /* do..while: a=prova, b=corpo        */
#define N_PER          48       /* a=inizio, b=prova, c=passo, d=corpo*/
#define N_PER_IN       49       /* a=dove metto, b=oggetto, d=corpo   */
#define N_RITORNA      50       /* a = valore o -1                    */
#define N_ROMPI        51
#define N_CONTINUA     52
#define N_VUOTO        53

typedef struct {
    unsigned char tipo;
    unsigned char op;               /* per unario/binario/assegna: il gettone */
    int           a, b, c, d;       /* figli, -1 = niente */
    int           prossimo;         /* il fratello nella lista, -1 */
    unsigned int  testo;            /* scostamento nell'arena */
    double        numero;
    int           riga;
} ExJsNodo;

typedef struct {
    ExJsNodo    *nodi;
    unsigned int nodi_max, nodi_n;

    char        *arena;
    unsigned int arena_max, arena_n;

    int          radice;

    /* ! CHE SIA FINITO LO SPAZIO SI DICE, non si lascia intuire da uno script
     * eseguito a meta'. Vale la stessa regola di HtmlDoc.troncato: un motore
     * che esegue meno di quello che c'e' senza avvisare fa credere che lo
     * script fosse cosi'. */
    int          troncato;
} ExJsAst;

/* =============================================================================
 * GLI OGGETTI, DENTRO
 *
 * ! UN AMBITO E' UN OGGETTO, e il suo prototipo e' l'ambito che lo racchiude.
 * Non e' un trucco per risparmiare codice: e' come JavaScript e' fatto —
 * cercare una variabile e cercare una proprieta' sono la stessa operazione
 * lungo la stessa catena. Tenerli separati vorrebbe dire due meccanismi che
 * devono restare d'accordo, e le chiusure sono proprio il punto in cui i due
 * si toccano.
 * ========================================================================== */
#define EXJS_CL_OGGETTO   0
#define EXJS_CL_VETTORE   1
#define EXJS_CL_FUNZIONE  2
#define EXJS_CL_AMBITO    3

typedef struct {
    unsigned int nome;              /* scostamento nell'arena */
    ExJsVal      valore;
    int          prossima;
} ExJsProp;

typedef struct {
    unsigned char classe;
    int           proto;            /* prototipo, o ambito che racchiude */
    int           prima_prop;

    /* --- se e' una funzione --- */
    int           nodo;             /* il N_FUNZIONE nell'albero, -1 se nativa */
    ExJsNativa    nativa;
    void         *dato;
    int           ambiente;         /* la chiusura: dove e' nata */
    unsigned int  nome;

    /* --- se e' un vettore --- */
    unsigned int  elem_off, elem_cap, lunghezza;
} ExJsOggetto;

/* Un lavoro in coda: il perche' sta in exjs.h, accanto a exjs_accoda. */
typedef struct {
    int          usato;
    unsigned int id;
    ExJsVal      funzione;
    unsigned int quando_ms;
    unsigned int ripeti_ms;         /* 0 = una volta sola */
} ExJsLavoro;

/* Il posto dove l'analizzatore lessicale scioglie le stringhe: una sola
 * stringa per volta, quindi basta che ci stia la piu' lunga di uno script. */
#define EXJS_SCRATCH  1024

/* --- val.c --- */
ExJsAst      *exjs_ctx_ast(ExJsCtx *c);
ExJsNodo     *exjs_ctx_nodi(ExJsCtx *c);
unsigned int  exjs_ctx_nodi_max(ExJsCtx *c);
char         *exjs_ctx_ast_arena(ExJsCtx *c);
unsigned int  exjs_ctx_ast_arena_max(ExJsCtx *c);
char         *exjs_ctx_scratch(ExJsCtx *c);
unsigned int exjs_arena_metti(ExJsCtx *c, const char *s, unsigned int n);
const char  *exjs_arena_leggi(ExJsCtx *c, unsigned int off);
int          exjs_ogg_nuovo(ExJsCtx *c, int classe);
ExJsOggetto *exjs_ogg(ExJsCtx *c, int i);
ExJsVal      exjs_da_oggetto(int i);
int          exjs_a_oggetto(ExJsVal v);
unsigned int exjs_a_off(ExJsVal v);
ExJsVal      exjs_stringa_off(ExJsCtx *c, unsigned int off);
int          exjs_prop_trova(ExJsCtx *c, int ogg, const char *nome, int risali);
ExJsVal      exjs_prop_val(ExJsCtx *c, int p);
void         exjs_prop_metti_val(ExJsCtx *c, int p, ExJsVal v);
int          exjs_globale_idx(ExJsCtx *c);
int          exjs_finita(ExJsCtx *c);
const char  *exjs_vettore_testo(ExJsCtx *c, ExJsVal vet);
ExJsVal      exjs_concat(ExJsCtx *c, ExJsVal a, ExJsVal b);
int          exjs_prop_prima(ExJsCtx *c, int ogg);
int          exjs_prop_prossima(ExJsCtx *c, int p);
unsigned int exjs_prop_nome(ExJsCtx *c, int p);

void exjs_ast_prepara(ExJsAst *A, ExJsNodo *nodi, unsigned int nodi_max,
                      char *arena, unsigned int arena_max);

/* Costruisce l'albero. Rende 1 se ci e' riuscito, 0 riempiendo `err`.
 * `buffer_testo` serve all'analizzatore lessicale per sciogliere le stringhe:
 * basta che sia grande quanto la stringa piu' lunga dello script. */
int exjs_analizza(ExJsAst *A, const char *sorgente, unsigned int n,
                  char *buffer_testo, unsigned int buffer_max,
                  ExJsErrore *err);

/* Il nome di un tipo di nodo, per il banco di prova e per le diagnostiche. */
const char *exjs_nodo_nome(int tipo);

void exjs_lex_apri(ExJsLex *L, const char *sorgente, unsigned int n,
                   char *buffer_testo, unsigned int buffer_max,
                   ExJsErrore *err);

/* Avanza al gettone successivo. Rende il tipo, TK_FINE alla fine, TK_ERRORE
 * dopo aver riempito `err`. */
int exjs_lex_avanti(ExJsLex *L);

/* Il nome di un gettone, per i messaggi d'errore. Sempre una stringa valida. */
const char *exjs_lex_nome(int tipo);

#endif /* EXJS_INT_H */
