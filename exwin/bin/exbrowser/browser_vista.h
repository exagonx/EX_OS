/* =============================================================================
 * exwin/bin/browser/browser_vista.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * IL PEZZO ESTRANEO — l'unica cosa che l'impaginato chiede a chi lo usa
 *
 * ! QUESTO E' IL TAGLIO CHE browser_priv.h ANNUNCIAVA. Li' c'era scritto:
 * «una libreria vuole che di qui non passino ne' i moduli, ne' le immagini,
 * ne' gli script: qui passano ancora tutti e tre». Passavano perche' un
 * <input> si impagina IN LINEA col testo, e la riga che lo contiene non si
 * puo' misurare senza sapere quanto e' largo quel controllo.
 *
 * ! LA RISPOSTA NON E' SPOSTARE DEL CODICE, E' NON SAPERE. L'impaginato non
 * deve sapere che cosa sia un <input>, un <img> o un <noscript>: deve poter
 * chiedere a qualcuno «di questo nodo che ne facciamo?», ricevere una misura,
 * e piu' tardi dire «adesso disegnati». Sono due domande, e stanno qui.
 *
 * ! IL RIFERIMENTO E' OPACO, ed e' cio' che rende il taglio vero. Chi risponde
 * mette in `rif` il numero che gli serve per ritrovare il suo pezzo; per
 * l'impaginato e' un intero che si porta dietro e gli restituisce al momento
 * di disegnare. Se un giorno il cliente ci mettesse un puntatore a un filmato,
 * qui dentro non cambierebbe una riga.
 * ============================================================================= */
#ifndef BROWSER_VISTA_H
#define BROWSER_VISTA_H

/* Le risposte di `misura`. */
#define VISTA_NIENTE 0   /* non e' roba mia: impaginalo come un elemento    */
#define VISTA_PEZZO  1   /* e' mio: un rettangolo w x h, riferimento `rif`  */
#define VISTA_TESTO  2   /* e' mio, ma al suo posto va impaginato del testo */
#define VISTA_SALTA  3   /* e' mio, e non occupa niente: nemmeno i figli    */

typedef struct {
    int          w, h;      /* VISTA_PEZZO: quanto occupa                    */
    int          rif;       /* il riferimento del cliente: qui nessuno lo legge */

    /* ! «STA NEL FLUSSO» VUOL DIRE «SI COMPORTA COME UNA PAROLA»: eredita il
     * collegamento che lo contiene, e diventa il nodo corrente per quel che
     * viene dopo. Un'immagine dentro un <a> si clicca e porta al link; un
     * controllo dentro un <a> no — chi ci scrive dentro non vuole andarsene. */
    int          nel_flusso;

    /* ! E QUESTI TRE NUMERI SONO UNA CONFESSIONE, non un'invenzione: sono le
     * differenze che c'erano gia' fra un controllo e un'immagine, e che
     * stavano in due tratti di codice lontani dove non si potevano
     * confrontare. Un controllo si stringe alla riga, si stacca di quattro
     * pixel e alza la riga di quattro; un'immagine non si stringe, non si
     * stacca e alza la riga di tre. Il tre e il quattro non hanno una
     * ragione: hanno una storia. Scritti qui vicini si possono unificare il
     * giorno che si decide di spostare dei pixel — non oggi, che la prova e'
     * proprio che non se ne sposti nessuno. */
    int          stringi;   /* 1 = non piu' largo della riga            */
    int          aria_dx;   /* pixel di stacco dopo il pezzo            */
    int          aria_giu;  /* di quanto la riga cresce oltre l'altezza */

    const char  *testo;     /* VISTA_TESTO: cosa impaginare al suo posto     */
    unsigned int testo_off; /* ... e dove sta, nell'arena del documento      */
} VistaPezzo;

typedef struct {
    /* ! «SI RICOMINCIA», e non e' una terza domanda: e' l'inizio della prima.
     * Un giro di impaginazione butta i pezzi vecchi e li rifa' da capo, e chi
     * tiene i propri deve buttarli nello stesso istante — non prima, o uno
     * script che reimpagina a meta' pagina si porterebbe via i controlli che
     * l'utente sta compilando; non dopo, o si accoderebbero alle copie del
     * giro precedente. Chi chiama impagina() e' in sei posti: il momento
     * giusto lo conosce solo impagina(). */
    void (*azzera)(void);

    /* Di questo nodo che ne facciamo? Rende una delle VISTA_*. */
    int  (*misura)(int nodo, VistaPezzo *p);

    /* Disegnalo li'. Le coordinate sono gia' quelle dello schermo — `y` porta
     * dentro lo scorrimento — e il ritaglio all'area del documento e' di chi
     * disegna: l'impaginato ritaglia il TESTO, che e' l'unica cosa sua. */
    void (*disegna)(int rif, int x, int y, int w, int h);
} VistaCliente;

/* Si installa una volta, prima della prima impaginazione. Senza cliente
 * l'impaginato funziona lo stesso: impagina il testo e ignora tutto il resto —
 * ed e' esattamente cio' che serve a una prova che non ha un navigatore
 * intorno. */
void vista_cliente(const VistaCliente *c);

#endif /* BROWSER_VISTA_H */
