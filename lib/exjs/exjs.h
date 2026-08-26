/* =============================================================================
 * lib/exjs/exjs.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExJs — il LINGUAGGIO, e nient'altro che il linguaggio
 *
 * -----------------------------------------------------------------------------
 * ! QUESTA LIBRERIA NON SA CHE ESISTE L'HTML, ed e' la decisione piu'
 * importante di tutto il progetto JavaScript.
 *
 * `document`, `window`, gli elementi, gli eventi: niente di tutto questo sta
 * qui dentro. Qui ci sono numeri, stringhe, oggetti, funzioni e chiusure — le
 * cose che il linguaggio ha per conto suo. Il ponte col documento e' un'ALTRA
 * libreria (exdom), che usa questa e usa exhtml, e che si registra come si
 * registra qualunque oggetto nativo.
 *
 * La ragione non e' l'eleganza: e' che il giorno che questo motore non
 * bastera' — e per aprire google.com non bastera' — si sostituisce cio' che
 * sta sotto senza riscrivere il ponte. Se `document` vivesse dentro il motore,
 * cambiare motore vorrebbe dire rifare il DOM.
 *
 * -----------------------------------------------------------------------------
 * ! I VALORI SONO OPACHI, e si toccano solo attraverso queste funzioni.
 *
 * Un ExJsVal e' un numero, non una struttura da guardare dentro. Oggi e' un
 * NaN-boxing a 64 bit (il perche' e' scritto sotto, accanto al typedef);
 * domani puo' essere un puntatore con i bit bassi marcati, o quello che
 * QuickJS usa. Chi lo tratta come opaco non se ne accorge; chi ci guarda
 * dentro va riscritto.
 *
 * -----------------------------------------------------------------------------
 * ! I BUFFER SONO DI CHI CHIAMA, come in exhtml e in excss.
 *
 * La libreria non tiene stato globale: due pagine aperte insieme sono due
 * contesti che non si toccano, e non c'e' niente da rendere rientrante. Il
 * tetto della memoria lo mette chi apre la pagina, che e' l'unico ad avere
 * motivo di sceglierlo — e su una macchina da 32 MB quel motivo e' forte.
 *
 * -----------------------------------------------------------------------------
 * ! IL TEMPO NON STA QUI DENTRO, e non e' una dimenticanza.
 *
 * `setTimeout` ha bisogno di sapere che ora e', e una libreria che chiama
 * l'orologio da se' e' una libreria che non si puo' provare: la stessa prova
 * dara' risultati diversi a seconda di quando gira. Qui c'e' una CODA DI
 * LAVORI con una scadenza ciascuno, e chi ospita il motore la fa avanzare
 * passando l'ora che ha lui — `exjs_pompa(ctx, ora_ms)`. Il browser ha gia' un
 * ciclo di messaggi, quindi ce l'ha gratis; il banco di prova passa numeri
 * inventati e ottiene risultati ripetibili.
 *
 * La stessa coda servira' alle promesse (i microtask), che sono lo stesso
 * meccanismo con una scadenza pari a zero. E' per questo che c'e' adesso.
 *
 * -----------------------------------------------------------------------------
 * ! QUELLO CHE NON C'E', DICHIARATO SUBITO
 *
 * Il primo scaglione e' un ES3 utile: var, funzioni, chiusure, oggetti,
 * prototipi, Array, String, Math, JSON. Fuori restano, per adesso: le
 * espressioni regolari, `try/catch` con la pila di eccezioni completa, i
 * getter/setter, `with`, le etichette, e tutto ES5 in avanti — `let`, le
 * funzioni a freccia, le classi, `async`, i generatori, `Proxy`.
 *
 * ! E VA DETTO CHIARO CHE COSI' google.com NON SI APRE. Il suo JavaScript e'
 * ES2017 offuscato: `async/await`, `Proxy`, destrutturazione. Quello e' il
 * lavoro di un QuickJS, non di un ES3 fatto crescere. Questa interfaccia e'
 * scritta perche' quel giorno si cambi il motore e non il browser.
 * ============================================================================= */

#ifndef EXJS_H
#define EXJS_H

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * I VALORI
 *
 * ! `undefined` E `null` SONO DUE COSE DIVERSE, e chi li confonde scrive un
 * motore che sbaglia proprio dove le pagine vere ci contano: `undefined` vuol
 * dire «non c'e'», `null` vuol dire «c'e', ed e' niente». Un attributo assente
 * e un attributo svuotato non sono la stessa cosa.
 * ========================================================================== */
#define EXJS_INDEFINITO   0
#define EXJS_NULLO        1
#define EXJS_BOOLEANO     2
#define EXJS_NUMERO       3
#define EXJS_STRINGA      4
#define EXJS_OGGETTO      5
#define EXJS_FUNZIONE     6

/* =============================================================================
 * ! UN VALORE E' SESSANTAQUATTRO BIT, E DENTRO C'E' UN double VERO.
 *
 * La prima stesura diceva «l'indice di una casella», e sarebbe stato piu'
 * semplice — ma un interprete che cammina sull'albero produce un valore per
 * ogni operazione, e con una casella per valore `for (i=0;i<10000;i++) x=x+1`
 * chiederebbe ventimila caselle per un ciclo che non conserva niente. Senza un
 * raccoglitore di memoria — e in questo scaglione non c'e' — sarebbe un motore
 * che muore sul primo ciclo vero.
 *
 * Percio' NaN-boxing: un valore E' un double, e i valori che double non sono
 * si nascondono dentro i NaN silenziosi, che nella norma IEEE 754 sono
 * duemiladuecentocinquantatre miliardi di configurazioni tutte uguali fra
 * loro. Numeri e booleani non costano niente; solo stringhe e oggetti
 * occupano una casella.
 *
 * ! RESTA OPACO. Chi lo tratta come un numero da confrontare con exjs_tipo()
 * non si accorgera' del giorno che ci mettiamo QuickJS sotto; chi ci guarda
 * dentro va riscritto. Le macro che lo aprono stanno in exjs_int.h, non qui.
 * ========================================================================== */
typedef unsigned long long ExJsVal;

/* Il contesto: tutto lo stato di un'esecuzione. Chi chiama lo alloca e ne
 * decide la taglia; la libreria non ne tiene nessuno per conto suo. */
typedef struct ExJsCtx ExJsCtx;

/* =============================================================================
 * GLI ERRORI SI RACCONTANO, NON SI NUMERANO
 *
 * ! UN MOTORE CHE DICE «errore di sintassi» E BASTA E' INUTILIZZABILE. Chi
 * scrive JavaScript sbaglia in continuazione, e la differenza fra uno
 * strumento e un ostacolo e' la riga e la colonna. Qui l'errore porta con se'
 * dove e' successo e che cosa ci si aspettava.
 * ========================================================================== */
#define EXJS_ERR_LEN   160

typedef struct {
    int          riga, colonna;
    unsigned int posizione;             /* scostamento nel sorgente */
    char         messaggio[EXJS_ERR_LEN];
} ExJsErrore;

/* =============================================================================
 * LE FUNZIONI NATIVE — la sola porta da cui il mondo entra nel linguaggio
 *
 * ! TUTTO CIO' CHE NON E' LINGUAGGIO PASSA DA QUI. `console.log`, `document`,
 * `setTimeout`: ognuno e' una funzione nativa registrata da chi ospita il
 * motore. La libreria non ne conosce nemmeno il nome.
 *
 * `dato` e' il puntatore che chi registra si porta dietro — per exdom sara' il
 * documento — cosi' la stessa funzione nativa serve due pagine diverse senza
 * variabili globali di mezzo.
 * ========================================================================== */
typedef ExJsVal (*ExJsNativa)(ExJsCtx *c, ExJsVal questo,
                              const ExJsVal *arg, int n_arg, void *dato);

/* =============================================================================
 * IL CICLO DI VITA
 * ========================================================================== */

/* Quanta memoria vuole un contesto con `oggetti` caselle e un'arena di
 * `arena_byte` per le stringhe. Serve a chi deve allocare prima di creare. */
unsigned int exjs_quanto_serve(unsigned int oggetti, unsigned int arena_byte);

/* Costruisce il contesto dentro `memoria` (grande almeno quanto dice
 * exjs_quanto_serve). Rende 0 se non ci sta. */
ExJsCtx *exjs_apri(void *memoria, unsigned int byte,
                   unsigned int oggetti, unsigned int arena_byte);

/* Esegue `sorgente`. Rende 1 se e' andata, 0 se no — e in quel caso `err`
 * (se dato) dice dove e perche'. Il valore dell'ultima espressione, se
 * interessa, esce da `risultato`.
 *
 * ! LO STESSO CONTESTO SI RIUSA: due <script> nella stessa pagina vedono le
 * stesse variabili, ed e' esattamente cio' che le pagine si aspettano. */
int exjs_esegui(ExJsCtx *c, const char *sorgente, unsigned int n,
                ExJsVal *risultato, ExJsErrore *err);

/* =============================================================================
 * COSTRUIRE E GUARDARE I VALORI
 * ========================================================================== */
ExJsVal exjs_indefinito(void);
ExJsVal exjs_nullo(void);
ExJsVal exjs_booleano(int v);
ExJsVal exjs_numero(ExJsCtx *c, double v);
ExJsVal exjs_stringa(ExJsCtx *c, const char *s, int n);   /* n<0: fino a \0 */
ExJsVal exjs_oggetto(ExJsCtx *c);
ExJsVal exjs_vettore(ExJsCtx *c);
ExJsVal exjs_nativa(ExJsCtx *c, ExJsNativa f, void *dato, const char *nome);

int         exjs_tipo(ExJsCtx *c, ExJsVal v);
double      exjs_a_numero(ExJsCtx *c, ExJsVal v);
int         exjs_a_booleano(ExJsCtx *c, ExJsVal v);
const char *exjs_a_stringa(ExJsCtx *c, ExJsVal v);   /* valida fino alla
                                                      * prossima exjs_esegui */

/* =============================================================================
 * LE PROPRIETA'
 *
 * ! IL NOME E' UNA STRINGA C, non un ExJsVal, e non e' una semplificazione:
 * chi registra `document.title` scrive "title" nel sorgente C, e costringerlo
 * a costruire prima un valore-stringa vorrebbe dire tre righe dove ne basta
 * una. Gli indici dei vettori passano da exjs_indice_*, che e' la stessa cosa
 * detta con un numero.
 * ========================================================================== */
int     exjs_metti(ExJsCtx *c, ExJsVal ogg, const char *nome, ExJsVal v);
ExJsVal exjs_prendi(ExJsCtx *c, ExJsVal ogg, const char *nome);
int     exjs_indice_metti(ExJsCtx *c, ExJsVal vet, unsigned int i, ExJsVal v);
ExJsVal exjs_indice_prendi(ExJsCtx *c, ExJsVal vet, unsigned int i);
unsigned int exjs_lunghezza(ExJsCtx *c, ExJsVal vet);

/* =============================================================================
 * DOVE FINISCE console.log
 *
 * ! LO DECIDE CHI OSPITA, e non la libreria. In un browser va nella sua
 * console, in un banco di prova sullo schermo, dentro un servizio da nessuna
 * parte. Una libreria che scrivesse sullo standard output da se' porterebbe
 * con se' una decisione che non e' sua — e dentro un server grafico stamperebbe
 * su una console che nessuno guarda.
 *
 * Senza registrarne una, `console.log` non da' errore: tace. E' il
 * comportamento giusto per un motore incorporato.
 * ========================================================================== */
typedef void (*ExJsUscita)(const char *testo, unsigned int n, void *dato);

void exjs_uscita_metti(ExJsCtx *c, ExJsUscita f, void *dato);

/* L'oggetto globale: e' li' che si appendono `console`, `document`, `window`. */
ExJsVal exjs_globale(ExJsCtx *c);

/* Chiama una funzione (nativa o scritta in JavaScript). */
ExJsVal exjs_chiama(ExJsCtx *c, ExJsVal f, ExJsVal questo,
                    const ExJsVal *arg, int n_arg, ExJsErrore *err);

/* =============================================================================
 * LA CODA DEI LAVORI — i tempi, senza che la libreria guardi l'orologio
 *
 * exjs_accoda mette in coda una funzione da chiamare quando `ora_ms` avra'
 * raggiunto `quando_ms`. Rende un identificativo, che serve a disdire.
 * exjs_pompa esegue tutto cio' che e' scaduto e rende quanti ne ha eseguiti.
 * ========================================================================== */
unsigned int exjs_accoda(ExJsCtx *c, ExJsVal f, unsigned int quando_ms,
                         unsigned int ripeti_ms);
void         exjs_disdici(ExJsCtx *c, unsigned int id);
int          exjs_pompa(ExJsCtx *c, unsigned int ora_ms);

/* Quanti lavori aspettano: serve a chi ospita per sapere se ha ancora
 * qualcosa da fare prima di mettersi a dormire. */
int exjs_lavori_in_attesa(ExJsCtx *c);

/* =============================================================================
 * LA MEMORIA, guardata da fuori
 *
 * ! SI PUO' CHIEDERE QUANTO SI STA USANDO, e serve davvero su una macchina
 * piccola: il browser deve poter decidere di chiudere un contesto invece di
 * scoprire che non c'e' piu' memoria mentre impagina.
 * ========================================================================== */
void exjs_memoria(ExJsCtx *c, unsigned int *caselle_usate,
                  unsigned int *caselle_max, unsigned int *arena_usata,
                  unsigned int *arena_max);

#ifdef __cplusplus
}
#endif

#endif /* EXJS_H */
