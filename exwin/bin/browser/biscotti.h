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
#define BIS_VAL_MAX    256
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
} Biscotto;

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
                 const char *riga, unsigned int ora_s);

/* Quel che uno script ha scritto in `document.cookie`. E' la stessa
 * grammatica, meno due cose: `HttpOnly` si ignora — un biscotto non visibile
 * agli script non puo' nascere da uno script — e il dominio non puo' allargarsi
 * oltre l'host della pagina. */
int bis_da_script(Dispensa *d, const char *host, const char *percorso,
                  const char *riga, unsigned int ora_s);

/* Riempie `fuori` con «n=v; n2=v2», scegliendo quelli che riguardano
 * host + percorso. `per_script` a 1 salta gli `HttpOnly`. */
void bis_da_mandare(const Dispensa *d, const char *host, const char *percorso,
                    int cifrata, int per_script, unsigned int ora_s,
                    char *fuori, unsigned int max);

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
