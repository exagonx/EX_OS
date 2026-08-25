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
