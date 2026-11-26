/* =============================================================================
 * exwin/bin/browser/biscotti.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LA DISPENSA DEI BISCOTTI — i cookie HTTP
 *
 * -----------------------------------------------------------------------------
 * ! STA NEL BROWSER E NON IN exhttp, E NON E' PIGRIZIA. Un biscotto non e' un
 * pezzo di protocollo: e' una decisione su CHI puo' rileggere che cosa e per
 * quanto. Domini, percorsi, scadenze, `Secure`, `HttpOnly` — sono regole di
 * chi naviga, non di chi trasporta byte. exhttp li mette nella richiesta e
 * riporta quelli che tornano; a scegliere quali ci pensa questo file.
 *
 * -----------------------------------------------------------------------------
 * ! E STA IN UN FILE SUO, staccato da browser.c, per un motivo solo: cosi' si
 * puo' PROVARE. La scelta di quali biscotti mandare e' fatta di cinque regole
 * che si sbagliano in silenzio — un suffisso di dominio confrontato male manda
 * i biscotti di un sito a un altro — e nessuna di quelle regole ha bisogno di
 * uno schermo, di una rete o di un disco per essere messa alla prova. Il banco
 * e' tools/prove/bisprova.c.
 *
 * -----------------------------------------------------------------------------
 * ! L'OROLOGIO ARRIVA DA FUORI, come in exjs e in exdom. Ogni funzione che
 * puo' incontrare una scadenza prende `ora_s`, i secondi dall'epoca. Una
 * libreria che guardasse l'orologio da se' darebbe prove che passano oggi e
 * falliscono domani — ed e' esattamente cio' che le scadenze sono.
 *
 * -----------------------------------------------------------------------------
 * ! IL FILE SU DISCO NON LO SCRIVE QUESTO PEZZO. Qui si costruisce il TESTO e
 * si rilegge il testo; ad aprire $HOME/.app/browser/biscotti.txt ci pensa il
 * browser, che e' l'unico a sapere dove sta la casa dell'utente. La stessa
 * divisione dell'orologio, per la stessa ragione.
 * ============================================================================= */

#ifndef BISCOTTI_H
#define BISCOTTI_H

#define BIS_MAX         64      /* quanti biscotti in tutto            */
#define BIS_NOME_MAX    64
/* ! 4096 (P13, 26 November 2026): RFC 6265 6.1 wants at least 4096 bytes a
 * cookie, and Google's search sets one of ~900 (SG_SS, the answer of its
 * JavaScript check) and NID often over 256: cut, they were worth nothing,
 * and the search came back blank. 64 of them are 256 KB of BSS. */
#define BIS_VAL_MAX   4096
#define BIS_DOM_MAX     96
#define BIS_PERC_MAX    96

typedef struct {
    int          usato;
    char         dominio[BIS_DOM_MAX];   /* senza il punto davanti      */
    char         percorso[BIS_PERC_MAX];
    char         nome[BIS_NOME_MAX];
    char         valore[BIS_VAL_MAX];
    /* ! LA SCADENZA A ZERO VUOL DIRE «DI SESSIONE», non «scaduto». E' la
     * differenza fra un biscotto che muore chiudendo il navigatore e uno che
     * si scrive su disco, e le pagine di accesso contano su tutt'e due. */
    unsigned int scade_s;
    unsigned char sicuro;                /* Secure: solo su https       */
    unsigned char solo_http;             /* HttpOnly: gli script non lo vedono */
    /* ! SE IL DOMINIO ERA SCRITTO NEL BISCOTTO vale anche per i
     * sottodomini; se non c'era, vale SOLO per l'host che l'ha mandato.
     * Sono due cose diverse e confonderle manda i biscotti di www a un
     * sottodominio che non li ha chiesti. */
    unsigned char con_sottodomini;
    unsigned char stesso_sito;           /* SameSite, BIS_SS_*          */
    unsigned int  creato_s;              /* when it was set (the Lax POST grace) */
} Biscotto;

#define BIS_SS_NON_DETTO 0      /* no SameSite: Lax, with the POST grace */
#define BIS_SS_NONE      1
#define BIS_SS_LAX       2
#define BIS_SS_STRICT    3

/* what kind of request, for SameSite (bis_da_mandare_ss) */
#define BIS_NAV_NO       0      /* a resource, a fetch, a frame        */
#define BIS_NAV_GET      1      /* the page itself, safe method        */
#define BIS_NAV_POST     2      /* the page itself, sent by a form     */

typedef struct {
    Biscotto b[BIS_MAX];
    int      persi;         /* 1 = la dispensa era piena: e' una spia */
} Dispensa;

void bis_azzera(Dispensa *d);

/* Una riga `Set-Cookie` com'e' arrivata, con l'host e il PERCORSO della
 * richiesta che l'ha ricevuta. Rende 1 se la dispensa e' cambiata — cosi' chi
 * salva su disco sa quando.
 *
 * ! IL PERCORSO SERVE ANCHE SE IL BISCOTTO NON HA `Path`, ed e' il motivo per
 * cui e' un parametro: senza `Path` il valore predefinito e' la DIRECTORY
 * della richiesta, non «/». Metterci «/» sarebbe stato piu' semplice e piu'
 * permissivo: manderebbe a tutto il sito un biscotto che vale per una parte. */
int bis_arrivato(Dispensa *d, const char *host, const char *percorso,
                 const char *riga, unsigned int ora_s, int cifrata);

/* Quel che uno script ha scritto in `document.cookie`. E' la stessa
 * grammatica, meno due cose: `HttpOnly` si ignora — un biscotto non visibile
 * agli script non puo' nascere da uno script — e il dominio non puo' allargarsi
 * oltre l'host della pagina. */
int bis_da_script(Dispensa *d, const char *host, const char *percorso,
                  const char *riga, unsigned int ora_s, int cifrata);
/* `cifrata`: the answer (for a script, its page) came over https. From it
 * (debug/bug_BROWSER001) a page in clear does not make or overwrite Secure
 * cookies, and __Secure- / __Host- names need it. */

/* Riempie `fuori` con «n=v; n2=v2», scegliendo quelli che riguardano
 * host + percorso. `per_script` a 1 salta gli `HttpOnly`. */
void bis_da_mandare(const Dispensa *d, const char *host, const char *percorso,
                    int cifrata, int per_script, unsigned int ora_s,
                    char *fuori, unsigned int max);

/* The same, with SameSite (25 November 2026, BROWSER002 P5): `stesso_sito`
 * 1 if the request is same-site (bis_sito of the two hosts equal, and the
 * same scheme), `nav` one of BIS_NAV_*. bis_da_mandare is this one with
 * stesso_sito 1: everything that matches host and path, as before. */
void bis_da_mandare_ss(const Dispensa *d, const char *host, const char *percorso,
                       int cifrata, int per_script, unsigned int ora_s,
                       int stesso_sito, int nav, char *fuori, unsigned int max);

/* The site of a host: its registrable domain, the public suffix (Public
 * Suffix List) plus one label - "www.bbc.co.uk" -> "bbc.co.uk". An IP
 * address, or a host that is itself a public suffix, is its own site. */
void bis_sito(const char *host, char *fuori, unsigned int max);

/* Toglie quelli scaduti. Rende quanti ne ha tolti. */
int bis_pulisci(Dispensa *d, unsigned int ora_s);

/* Il testo del file: una riga per biscotto.
 * ! SOLO QUELLI CON UNA SCADENZA, e i biscotti di sessione restano dov'erano.
 * Scriverli su disco vorrebbe dire che chiudere il navigatore non chiude piu'
 * la sessione — che e' esattamente il contrario di quel che promettono. */
unsigned int bis_salva(const Dispensa *d, char *fuori, unsigned int max,
                       unsigned int ora_s);

/* Rilegge quel che bis_salva ha scritto. Quelli gia' scaduti si saltano. */
void bis_carica(Dispensa *d, const char *testo, unsigned int ora_s);

/* I secondi dall'epoca di una data `Expires`, o 0 se non si sa leggere.
 * Esposta perche' e' la parte piu' facile da sbagliare, e il banco la prova
 * da sola. */
unsigned int bis_data(const char *s);

#endif /* BISCOTTI_H */
