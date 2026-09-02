/* =============================================================================
 * lib/exhttp/http.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * HTTP/1.1 — la parte che non tocca la rete
 *
 * ! QUI DENTRO NON SI APRE NIENTE E NON SI LEGGE NIENTE. Si smonta un URL, si
 * costruisce una richiesta dentro un buffer, si leggono le intestazioni di una
 * risposta e si srotola un corpo «a pezzi». Tutto su byte che qualcun altro ha
 * gia' in mano.
 *
 * ! ED E' LA STESSA DIVISIONE DEI FONT, per la stessa ragione: ttf.c e raster.c
 * non includono la libc, e per questo si compilano con un `cc` sull'host e si
 * provano contro FreeType invece che dentro una macchina virtuale. Qui il
 * riferimento sara' un server vero e le risposte in scatola: entrambi si
 * guardano senza accendere niente.
 *
 * ! IL TRASPORTO NON SI VEDE DA QUESTO FILE, ED E' VOLUTO. Un giorno sotto
 * l'HTTP ci sara' il TLS: se la lettura delle intestazioni sapesse da dove
 * arrivano i byte, quel giorno andrebbe riscritta. Non lo sa, e non lo sapra'.
 *
 * =============================================================================
 * ! OGNI LUNGHEZZA CHE ARRIVA DALLA RETE HA UN TETTO, e non e' prudenza
 * generica: qui i numeri li sceglie il server, che e' letteralmente l'altra
 * parte. Una dimensione di pezzo senza limite, un'intestazione senza limite o
 * un numero di intestazioni senza limite sono tre modi diversi di far finire
 * la memoria a chi si e' collegato.
 * ============================================================================= */
#ifndef HTTP_H
#define HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_HOST_MAX       128
#define HTTP_PERCORSO_MAX   512
#define HTTP_TIPO_MAX        96
#define HTTP_POSIZIONE_MAX  512

/* ! I BISCOTTI DI UNA RISPOSTA SONO PIU' D'UNO, e per questo sono un vettore e
 * non un campo. `Set-Cookie` e' l'unica intestazione che un server manda
 * ripetuta apposta: accorparla come si fa con le altre — l'ultima vince —
 * vorrebbe dire buttarne via tre su quattro senza dirlo.
 *
 * ! E IL TETTO SI DICHIARA, come ovunque qui dentro: se ne arrivano di piu' si
 * accende `biscotti_persi`. Sei bastano ai siti che si incontrano; chi ne
 * manda dodici lo fa sapere invece di far sparire i suoi in silenzio. */
#define HTTP_BISCOTTI_MAX     6
#define HTTP_BISCOTTO_MAX   256

/* ! UN PEZZO PIU' GRANDE DI QUESTO E' UNA RISPOSTA SBAGLIATA, non una risposta
 * grande: il corpo si consuma a blocchi, quindi la dimensione di un pezzo non
 * ha nessun bisogno di essere enorme. Sedici megabyte e' gia' assurdo e serve
 * solo a fermare un numero inventato prima che diventi un conto. */
#define HTTP_PEZZO_MAX      (16u * 1024u * 1024u)

typedef struct {
    char         schema[8];
    char         host[HTTP_HOST_MAX];
    unsigned int porta;
    char         percorso[HTTP_PERCORSO_MAX];
    int          cifrato;               /* 1 = https */
} HttpUrl;

/* Smonta un URL. Rende 1 se ci e' riuscito, 0 altrimenti.
 *
 * ! SENZA SCHEMA SI ASSUME http://, perche' e' quello che la gente scrive in
 * una barra degli indirizzi. Senza percorso si assume "/". La porta assente e'
 * 80 per http e 443 per https. */
int http_url(const char *url, HttpUrl *u);

/* Scrive una richiesta GET dentro `out`. Rende i byte scritti, 0 se non ci
 * stanno.
 *
 * ! SI CHIEDE SEMPRE `Connection: close`, e per adesso e' una scelta e non una
 * mancanza. Tenere viva una connessione vuol dire sapere QUANDO la risposta e'
 * finita senza che l'altro chiuda — cioe' fidarsi di Content-Length e dei
 * pezzi in ogni caso limite. Chiudere e riaprire costa un giro di rete per
 * richiesta e toglie una classe intera di errori; si cambiera' quando il
 * browser fara' molte richieste alla stessa macchina. */
int http_richiesta(char *out, unsigned int max, const HttpUrl *u,
                   const char *agente);

/* La stessa richiesta, ma con un corpo: `corpo` non nullo vuol dire POST, e
 * porta con se' `Content-Type: application/x-www-form-urlencoded` e il
 * `Content-Length` contato sui byte veri.
 *
 * ! IL CORPO ARRIVA GIA' CODIFICATO. Chi manda un modulo ha costruito
 * «a=1&b=2» con la codifica percento, ed e' l'unico a sapere come andava
 * fatta: qui non si tocca. */
/* `vivo` a 1 chiede al server di TENERE APERTA la connessione dopo la
 * risposta, cosi' la richiesta dopo non paga un'altra stretta di mano.
 *
 * ! SU https QUELLA STRETTA E' TUTTO IL COSTO. Su un 386 emulato una
 * connessione TLS — chiave effimera, catena di certificati, firma — sono venti
 * secondi; una pagina con dieci immagini ne pagava dieci volte tanto e la
 * barra di stato sembrava bloccata. Riusare la connessione le fa costare
 * quanto i loro byte.
 *
 * ! E SI PUO' CHIEDERE SOLO SE SI SA DOVE FINISCE IL CORPO — Content-Length o
 * pezzi. Senza, la fine e' la CHIUSURA, e tenere aperta una connessione di cui
 * non si sa quando finisce la risposta vuol dire aspettare per sempre. Chi
 * chiama guarda la risposta e decide. */
/* ! I BISCOTTI DA MANDARE ARRIVANO GIA' PRONTI, «n=v; n2=v2», e questa
 * funzione li scrive e basta. Non sa che cosa sia un dominio ne' quando un
 * biscotto scade: chi tiene la dispensa lo sa, e sceglie lui quali riguardano
 * questo indirizzo. Con 0 o "" l'intestazione non si scrive affatto — e non
 * si scrive `Cookie:` vuota, che per certi server e' un'altra cosa. */
int http_richiesta_corpo(char *out, unsigned int max, const HttpUrl *u,
                         const char *agente, const char *corpo, int vivo,
                         const char *biscotti);

typedef struct {
    int          codice;                    /* 200, 404, ... */
    int          ha_lunghezza;
    unsigned int lunghezza;                 /* Content-Length */
    int          a_pezzi;                   /* Transfer-Encoding: chunked */
    char         tipo[HTTP_TIPO_MAX];       /* Content-Type */
    char         posizione[HTTP_POSIZIONE_MAX];  /* Location */
    int          chiude;                    /* «Connection: close» dal server */

    /* Le righe `Set-Cookie`, com'erano scritte: qui non si interpretano.
     * Chi le legge decide che cosa e' un dominio, un percorso e una scadenza,
     * ed e' giusto che sia lui — questa e' una libreria di trasporto. */
    char         biscotti[HTTP_BISCOTTI_MAX][HTTP_BISCOTTO_MAX];
    int          n_biscotti;
    int          biscotti_persi;            /* 1 = ce n'erano di piu' */
} HttpRisposta;

/* Legge la riga di stato e le intestazioni, se sono complete dentro `d`.
 *
 *   > 0   quanti byte occupano: il corpo comincia li'
 *     0   non ancora complete, serve leggere altro
 *   < 0   malformate: si chiude e basta
 *
 * ! LE INTESTAZIONI SI CERCANO SOLO DENTRO QUELLO CHE C'E', e chi chiama
 * richiama con piu' byte. Un parser che aspettasse dentro se stesso vorrebbe
 * conoscere il trasporto — vedi in cima. */
int http_intestazioni(const unsigned char *d, unsigned int n, HttpRisposta *r);

/* -----------------------------------------------------------------------------
 * Il corpo «a pezzi»
 *
 * ! CHUNKED NON E' UN OPZIONALE. Un server che non sa in anticipo quanto sara'
 * lunga una risposta — cioe' qualunque pagina generata al momento — non manda
 * Content-Length: manda i pezzi. Senza saperli srotolare si vedrebbero i
 * numeri esadecimali della lunghezza in mezzo al testo, e sembrerebbe un
 * difetto della pagina.
 *
 * ! ED E' UNA MACCHINA A STATI, non un ciclo che legge. I byte arrivano in
 * blocchi decisi dalla rete: un pezzo puo' finire a meta' della sua riga di
 * lunghezza, e la volta dopo si riprende da li'.
 * --------------------------------------------------------------------------- */
#define HTTP_P_DIM      0       /* sto leggendo la riga della lunghezza */
#define HTTP_P_DATI     1       /* sto copiando i byte del pezzo */
#define HTTP_P_FINE_R   2       /* mi aspetto il CR dopo i dati */
#define HTTP_P_FINE_N   3       /* mi aspetto l'LF dopo i dati */
#define HTTP_P_CODA     4       /* le intestazioni finali, che si buttano */
#define HTTP_P_FATTO    5
#define HTTP_P_ROTTO    6
/* ! SALTARE LE ESTENSIONI E' UNO STATO, NON UN CICLO. Dopo la lunghezza puo'
 * esserci «;nome=valore» fino a fine riga. Saltarlo con un ciclo dentro la
 * chiamata funziona finche' l'estensione arriva tutta insieme: se il blocco
 * finisce in mezzo, la chiamata dopo riprende in HTTP_P_DIM e le LETTERE
 * dell'estensione tornano a contare come cifre esadecimali della lunghezza.
 * Trovato dalla prova che consegna un byte per volta. */
#define HTTP_P_EXT      7

typedef struct {
    int          stato;
    unsigned int restano;       /* del pezzo in corso */
    unsigned int cifre;         /* quante ne ho gia' lette della lunghezza */
    unsigned int vuote;         /* righe vuote di seguito, per la coda */
} HttpPezzi;

void http_pezzi_avvia(HttpPezzi *p);

/* Consuma da `in` e produce in `out`. Rende i byte PRODOTTI, o -1 se il flusso
 * e' malformato; `consumati` dice quanti ne ha presi da `in`, che puo' essere
 * meno di `n` se `out` si e' riempito.
 *
 * Finito: p->stato == HTTP_P_FATTO. */
int http_pezzi(HttpPezzi *p, const unsigned char *in, unsigned int n,
               unsigned int *consumati, unsigned char *out, unsigned int max);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_H */
