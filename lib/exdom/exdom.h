/* =============================================================================
 * lib/exdom/exdom.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExDom — il PONTE, e nient'altro che il ponte
 *
 * -----------------------------------------------------------------------------
 * ! QUESTA LIBRERIA E' L'UNICA CHE SA DUE COSE INSIEME: che esiste un albero
 * HTML e che esiste un motore JavaScript. Sopra di lei ci sono due librerie che
 * si ignorano — exhtml non ha mai sentito nominare uno script, exjs non sa che
 * cosa sia un tag — e questo e' stato scritto in exjs.h come la decisione piu'
 * importante di tutto il progetto. Qui se ne paga il prezzo, che e' un terzo
 * pezzo di codice; e qui se ne incassa il guadagno, che e' poter cambiare il
 * motore sotto senza toccare il browser sopra.
 *
 * Il giorno che al posto di ExJs ci sara' QuickJS, questo file resta e cambia
 * il suo .c. Il browser non se ne accorge.
 *
 * -----------------------------------------------------------------------------
 * ! UN NODO SI AVVOLGE UNA VOLTA SOLA, e non e' un'ottimizzazione.
 *
 * `document.getElementById('x') === document.getElementById('x')` deve essere
 * vero, e uno script che scrive `elemento.mioStato = 3` deve ritrovarcelo la
 * volta dopo. Tutt'e due le cose cadono se ogni chiamata costruisce un oggetto
 * nuovo, e cadono in silenzio: la pagina non da' errore, fa la cosa sbagliata.
 * Percio' c'e' una tabella nodo -> involucro, ed e' il grosso della memoria che
 * questa libreria chiede.
 *
 * -----------------------------------------------------------------------------
 * ! LE PROPRIETA' SONO VIVE, e per questo il motore ha dovuto crescere.
 *
 * `elemento.parentNode` non e' una copia fatta al momento dell'involucro: e'
 * una domanda posta all'albero adesso. Il meccanismo sono gli OGGETTI ESOTICI
 * di ExJs (vedi ExJsLeggiProp in exjs.h), aggiunti apposta: senza, `innerHTML
 * = '...'` avrebbe scritto una proprieta' JavaScript e lasciato il documento
 * com'era — senza un errore, senza un avviso, con la pagina che non cambia.
 *
 * -----------------------------------------------------------------------------
 * ! GLI ELENCHI SONO FOTOGRAFIE, E IL DOM VERO LI HA VIVI.
 *
 * `getElementsByTagName('div')` nel browser rende una NodeList che si aggiorna
 * da sola: se lo script aggiunge un div, la lista si allunga sotto i piedi di
 * chi ci sta ciclando. Qui rende un vettore normale, riempito una volta. E'
 * una differenza vera e va saputa, perche' c'e' un modo classico di scrivere
 * un ciclo che con le liste vive non finisce mai e con le nostre finisce:
 * quello che aggiunge un elemento a ogni giro. Le nostre fanno la cosa che chi
 * scrive si aspettava; il DOM vero fa quella che ha promesso. Il giorno che
 * una pagina vera dipendesse dal comportamento vivo, il posto dove metterlo e'
 * qui, e vorra' dire un oggetto esotico con un `length` che riconta.
 *
 * -----------------------------------------------------------------------------
 * ! QUELLO CHE NON C'E' ANCORA, dichiarato subito: gli EVENTI
 * (addEventListener, onclick, la propagazione) e `style` come oggetto. Sono i
 * due pezzi che seguono, e sono tenuti fuori da questo scaglione apposta: gli
 * eventi hanno bisogno che il browser sappia dire dove e' stato il clic, e
 * `style` ha bisogno di un pezzo di excss per sciogliere le dichiarazioni. Un
 * `style` finto che accetta le scritture e non le mostra sarebbe esattamente
 * la bugia silenziosa che gli oggetti esotici sono nati per evitare.
 * ============================================================================= */

#ifndef EXDOM_H
#define EXDOM_H

#include "exjs.h"
#include "html.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ExDom ExDom;

/* Quanta memoria vuole un ponte per un documento di al piu' `nodi_max` nodi,
 * con `testo_max` byte di spazio per rimettere in marcatore i sottoalberi.
 *
 * ! `nodi_max` DEV'ESSERE QUELLO DEL DOCUMENTO, non il numero di nodi che ci
 * sono adesso: gli script ne creano di nuovi, e un involucro chiesto per un
 * nodo fuori tabella non avrebbe dove stare. */
unsigned int exdom_quanto_serve(unsigned int nodi_max, unsigned int testo_max);

/* Costruisce il ponte dentro `memoria` e appende `document` e `window`
 * all'oggetto globale del motore. Rende 0 se la memoria non basta.
 *
 * ! IL DOCUMENTO E IL CONTESTO SONO DI CHI CHIAMA e devono vivere piu' a lungo
 * del ponte: qui se ne tengono i puntatori, non le copie. E' la stessa regola
 * di tutte le altre librerie — chi apre la pagina possiede tutto. */
ExDom *exdom_apri(void *memoria, unsigned int byte,
                  ExJsCtx *js, HtmlDoc *doc,
                  unsigned int nodi_max, unsigned int testo_max);

/* L'involucro JavaScript di un nodo: sempre lo stesso oggetto per lo stesso
 * nodo. Con `nodo` a -1 rende `null`, che e' cio' che il DOM rende per il
 * padre della radice e per un fratello che non c'e'. */
ExJsVal exdom_avvolgi(ExDom *D, int nodo);

/* L'indice del nodo dietro un involucro, o -1 se il valore non e' un nodo.
 * Serve al browser: dopo che uno script ha girato, sapere QUALE nodo e'
 * cambiato e' l'unico modo di non rimpaginare tutto. */
int exdom_nodo(ExDom *D, ExJsVal v);

/* L'oggetto `document`, per chi lo vuole senza cercarlo nel globale. */
ExJsVal exdom_documento(ExDom *D);

/* ! SE UNA SERIALIZZAZIONE NON C'E' STATA TUTTA LO SI PUO' CHIEDERE, invece di
 * lasciare che una pagina mostri meno di quel che c'e' senza dirlo. Diventa 1
 * la prima volta che un `innerHTML` non e' stato nello spazio disponibile e
 * non torna piu' indietro: e' una spia, non un contatore. */
int exdom_troncato(const ExDom *D);

#ifdef __cplusplus
}
#endif

#endif /* EXDOM_H */
