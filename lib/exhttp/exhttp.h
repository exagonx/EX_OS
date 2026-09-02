/* =============================================================================
 * lib/exhttp/exhttp.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExHttp — scaricare una pagina
 *
 * ! IL TRASPORTO E' UN PARAMETRO, NON UNA COSA SAPUTA, ed e' la decisione piu'
 * importante di questo file. Oggi sotto l'HTTP c'e' il TCP; domani, per
 * https://, ci sara' il TLS. Se questo codice aprisse la connessione da se',
 * quel giorno andrebbe riscritto — e sarebbe la seconda volta che si scrive
 * «leggi le intestazioni, poi il corpo», cioe' la seconda occasione di
 * sbagliarlo.
 *
 * Con un trasporto fatto di tre funzioni, il TLS diventa un altro trasporto e
 * questo file non cambia di una riga.
 *
 * ! CHI NON VUOLE SCEGLIERE NON SCEGLIE: exhttp_prendi() usa il TCP e basta, e
 * un programma che scarica una pagina in chiaro non deve sapere che esistono i
 * trasporti.
 * ============================================================================= */
#ifndef EXHTTP_H
#define EXHTTP_H

#include "http.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------------
 * Il trasporto
 *
 * ! LE FUNZIONI RENDONO CIO' CHE RENDE UN SOCKET: `leggi` i byte letti, 0 se
 * l'altro ha chiuso, negativo su errore; `scrivi` quanti ne ha presi. Sono le
 * stesse regole del TCP di EX-OS, cosi' il trasporto TCP e' un involucro sottile
 * e non una traduzione.
 * --------------------------------------------------------------------------- */
typedef struct {
    void *stato;
    int (*leggi)(void *stato, unsigned char *dst, unsigned int max,
                 unsigned int ms);
    int (*scrivi)(void *stato, const unsigned char *src, unsigned int n);
    void (*chiudi)(void *stato);

    /* ! QUANTI BYTE SI POSSONO LEGGERE ADESSO, senza aspettare: >0 se ce n'e',
     * 0 se non e' ancora arrivato niente, <0 se e' finita.
     *
     * ! E' L'UNICO MODO DI SAPERE «NON C'E' ANCORA NIENTE» SENZA CONFONDERLO
     * CON «L'ALTRO HA CHIUSO». `leggi` rende -1 sia per un timeout sia per un
     * errore, e 0 solo alla chiusura: finche' si aspettava e basta non
     * importava, ma chi legge dentro un ciclo di messaggi deve poter andarsene
     * a fare altro, e per farlo deve sapere che non c'e' niente da aspettare
     * ADESSO.
     *
     * ! PUO' ESSERE 0 (il puntatore), e allora si legge come si e' sempre
     * letto: un trasporto che non sa rispondere non deve smettere di
     * funzionare. */
    int (*quanti)(void *stato);
} ExHttpTrasporto;

/* Apre un trasporto TCP verso host:porta. Rende 1 se ci e' riuscito.
 * Il nome si risolve col DNS; un indirizzo scritto a cifre si riconosce e non
 * passa dal DNS. */
int exhttp_tcp(ExHttpTrasporto *t, const char *host, unsigned int porta);

/* -----------------------------------------------------------------------------
 * Scaricare
 * --------------------------------------------------------------------------- */
#define EXHTTP_URL_MAX      600

/* ! CINQUE REDIREZIONI E BASTA. Due pagine che si rimandano a vicenda sono un
 * anello infinito, e capita davvero — un sito mal configurato lo fa. Contarle
 * e' l'unico modo di uscirne. */
#define EXHTTP_SALTI_MAX    5

typedef struct {
    int          codice;                    /* l'ultimo codice HTTP visto */
    char         tipo[HTTP_TIPO_MAX];       /* Content-Type */
    unsigned int byte;                      /* quanti ne ha messi nel buffer */
    int          troncata;                  /* 1 = il buffer era piccolo */
    int          salti;                     /* quante redirezioni seguite */
    char         finale[EXHTTP_URL_MAX];    /* l'URL a cui si e' arrivati */
    char         errore[96];                /* perche' non ha funzionato */
} ExHttpEsito;

/* Scarica `url` dentro `buf`. Rende 1 se ha una risposta — anche un 404, che
 * e' una risposta — e 0 se non e' riuscito a parlare con nessuno; in quel caso
 * `errore` dice perche'.
 *
 * ! IL CORPO VA IN UN BUFFER DI CHI CHIAMA, e se non ci sta si TRONCA e si
 * dice. Un browser che allocasse quanto serve al server sarebbe un browser che
 * il server puo' far crescere quanto vuole; qui il tetto lo mette chi apre la
 * pagina, che e' l'unico ad avere motivo di sceglierlo.
 *
 * ! LE REDIREZIONI SI SEGUONO, e `finale` dice dove si e' finiti. Senza,
 * http://www.google.com renderebbe un 301 e una pagina vuota: e' quello che
 * risponde in chiaro, e sembrerebbe che il browser non funzioni. */
int exhttp_prendi(const char *url, unsigned char *buf, unsigned int max,
                  ExHttpEsito *e);

/* Come exhttp_prendi, ma manda `corpo` in POST (con Content-Type
 * application/x-www-form-urlencoded). Il corpo arriva GIA' CODIFICATO: chi
 * costruisce un modulo e' l'unico a sapere come andava fatta la codifica.
 *
 * ! IL CORPO VALE PER LA PRIMA RICHIESTA E BASTA: una redirezione dopo un POST
 * si segue in GET, come fanno tutti i browser. */
int exhttp_posta(const char *url, const char *corpo,
                 unsigned char *buf, unsigned int max, ExHttpEsito *e);

/* =============================================================================
 * I BISCOTTI — exhttp non li tiene, li chiede e li consegna
 *
 * ! LA DISPENSA STA IN CHI CHIAMA. Sapere che cosa e' un dominio, quando una
 * scadenza e' passata e dove si scrivono su disco sono tre cose che una
 * libreria di trasporto non ha motivo di sapere. exhttp fa la quarta: li mette
 * nella richiesta e riporta quelli che tornano.
 *
 * ! E DEVE FARLA exhttp, non chi chiama, perche' il giro delle redirezioni lo
 * fa lei. Un server che manda `302` e `Set-Cookie` insieme si aspetta che la
 * richiesta DOPO — quella che exhttp fa da se', dentro la stessa chiamata —
 * se lo porti gia' dietro. E' il caso normale di mezzo web, non un dettaglio.
 *
 * `chiedi` riempie `fuori` con «n=v; n2=v2» — gia' scelti per QUEL host e QUEL
 * percorso — o lo lascia vuoto. `arrivato` riceve una riga `Set-Cookie` com'era
 * scritta, e l'host che l'ha mandata.
 *
 * ! SENZA REGISTRARE NIENTE NON CAMBIA NIENTE: niente `Cookie:` in uscita, e i
 * `Set-Cookie` che arrivano si leggono e si buttano. Un programma che di
 * biscotti non sa niente non deve accorgersi che esistono.
 * ========================================================================== */
typedef void (*ExHttpBiscottiChiedi)(void *dato, const char *host,
                                     const char *percorso, int cifrata,
                                     char *fuori, unsigned int max);
typedef void (*ExHttpBiscottoArrivato)(void *dato, const char *host,
                                       const char *riga);

void exhttp_biscotti(ExHttpBiscottiChiedi chiedi,
                     ExHttpBiscottoArrivato arrivato, void *dato);

/* =============================================================================
 * L'ATTESA — leggere a pezzi senza smettere di essere vivi
 *
 * ! IL PROBLEMA NON E' LEGGERE A PEZZI: TCP CONSEGNA GIA' A PEZZI. Il problema
 * e' che fra un pezzo e l'altro si DORME dentro la lettura, e mentre si dorme
 * il programma non risponde piu' a niente. Su una pagina da un megabyte sono
 * decine di secondi di finestra morta.
 *
 * ! LA CURA E' CHIEDERE PRIMA E ADDORMENTARSI POI. Il trasporto sa dire quanti
 * byte ci sono ADESSO; se sono zero exhttp chiama questo gancio, chi ospita fa
 * il suo giro — ridisegna, risponde al mouse — e si riprova. Si dorme solo
 * quando c'e' qualcosa da leggere, cioe' per un istante.
 *
 * ! IL GANCIO PUO' DIRE DI SMETTERE: rendendo 0 annulla la richiesta. E' il
 * tasto Esc di chi si e' stufato di aspettare, e senza questo giro non si
 * poteva nemmeno offrire.
 *
 * ! E CHI LO REGISTRA DEVE SAPERE CHE RIENTRA IN CASA PROPRIA. Se dentro il
 * gancio si smistano i messaggi, l'applicazione puo' ritrovarsi a far partire
 * un'altra richiesta mentre questa e' a meta': chi ospita deve impedirselo con
 * una bandiera. Non e' un dettaglio da scoprire dopo.
 *
 * ! SENZA REGISTRARE NIENTE NON CAMBIA NIENTE: si legge come si e' sempre
 * letto. Un programma senza ciclo di messaggi non deve accorgersi che questo
 * meccanismo esiste.
 * ========================================================================== */
typedef int (*ExHttpAttesa)(void *dato);   /* 0 = annulla la richiesta */

void exhttp_attesa(ExHttpAttesa f, void *dato);

/* =============================================================================
 * A CHE PUNTO E' LA STRETTA DI MANO
 *
 * ! APRIRE UNA CONNESSIONE CIFRATA E' LUNGO, E CHI ASPETTA NON SA PERCHE'. Su
 * una macchina lenta sono secondi: una chiave effimera da calcolare, una
 * catena di certificati da verificare, una firma. Chi guarda vede una finestra
 * ferma e non ha modo di sapere se stia lavorando o se sia morta.
 *
 * Il gancio si chiama fra un passo e l'altro con la frase che lo descrive —
 * «verifico la catena», «controllo la firma» — e rendendo 0 ANNULLA.
 *
 * ! NON RENDE LA STRETTA INTERROMPIBILE DENTRO UN CONTO. Un x25519 o una
 * verifica di firma sono un blocco solo: fra un passo e l'altro si respira,
 * dentro no. E' scritto anche in extls.h, dove i passi si chiamano.
 * ========================================================================== */
typedef int (*ExHttpPasso)(void *dato, const char *cosa);  /* 0 = annulla */

void exhttp_passo(ExHttpPasso f, void *dato);

/* Come sopra ma su un trasporto gia' aperto, e senza seguire le redirezioni:
 * e' il mattone con cui exhttp_prendi e' fatta, ed e' quello che servira' al
 * TLS. `u` dice cosa chiedere. */
int exhttp_scambio(ExHttpTrasporto *t, const HttpUrl *u,
                   unsigned char *buf, unsigned int max, ExHttpEsito *e,
                   HttpRisposta *r);

#ifdef __cplusplus
}
#endif

#endif /* EXHTTP_H */
