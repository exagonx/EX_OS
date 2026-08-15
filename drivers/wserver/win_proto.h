/* =============================================================================
 * drivers/wserver/win_proto.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Il protocollo fra il server a finestre e i suoi client
 *
 * ! IL SERVER NON DISEGNA IL CONTENUTO DELLE FINESTRE, LO COMPONE. Ogni
 * finestra e' una zona di memoria condivisa che il CLIENT riempie di pixel; il
 * server ci mette intorno bordo e barra del titolo, le impila nell'ordine
 * giusto e le copia nel framebuffer. E' la decisione da cui discende tutto il
 * resto, e le ragioni sono tre:
 *
 *   1. un client che sbaglia a disegnare rovina la PROPRIA finestra, non lo
 *      schermo. Se il disegno stesse nel server, un errore di un'applicazione
 *      sarebbe un errore del server — cioe' di tutte le applicazioni
 *      (direttiva 3 di DIREZIONE.md: «se questo muore, cosa resta rotto?»);
 *   2. i formati di immagine — BMP oggi, JPG, PNG, ICO domani — restano FUORI
 *      dal server. Un decodificatore JPG e' migliaia di righe di codice che
 *      legge dati che arrivano da fuori: e' il posto in cui un difetto diventa
 *      un problema di sicurezza. Nel client e' un guaio di quel client;
 *   3. una finestra da 640x480x32 sono 1,2 MB. Passarli via IPC vorrebbe dire
 *      ~800 messaggi a fotogramma: non e' lentezza, e' la struttura sbagliata.
 *      Per questo la memoria condivisa e' arrivata PRIMA del server.
 *
 * ! I PIXEL SONO SEMPRE ARGB A 32 BIT, ANCHE SE LO SCHERMO NON LO E'. Il
 * client disegna in un formato solo e non guarda mai la profondita' vera; e'
 * il server a convertire quando compone, ed e' l'unico posto che sa se lo
 * schermo e' a 16, 24 o 32 bit. Un toolkit che dovesse conoscere il formato
 * dello schermo avrebbe sei strade da provare invece di una.
 *
 * ! IL CLIENT DICE QUANDO HA FINITO, e non si compone a caso. Senza un
 * «adesso guarda», il server copierebbe finestre disegnate a meta' — che si
 * vede come sfarfallio e non somiglia per niente a un errore di sincronismo.
 * ============================================================================= */

#ifndef WIN_PROTO_H
#define WIN_PROTO_H

/* Il nome con cui si trova il server. Come per 'mouse' e 'kbd': si cerca per
 * nome, non per PID, cosi' un server che riparte si ritrova da solo. */
#define WIN_SERVIZIO        "wserver"

/* Il nome della zona condivisa di una finestra: "win" piu' il numero. Lo
 * costruiscono tutt'e due i lati con win_nome_zona(). */
#define WIN_ZONA_PREFISSO   "win"

/* --- Messaggi dal CLIENT al SERVER --------------------------------------- */
#define WIN_MSG_CREA        0x5701  /* WinCrea    -> risponde WIN_MSG_CREATA */
#define WIN_MSG_AGGIORNA    0x5702  /* WinRegione: «ho finito di disegnare»  */
#define WIN_MSG_SPOSTA      0x5703  /* WinRegione: cambia posizione/misura   */
#define WIN_MSG_TITOLO      0x5704  /* WinTitolo                             */
#define WIN_MSG_DISTRUGGI   0x5705  /* WinRegione (solo il campo id)         */
#define WIN_MSG_PRIMO       0x5706  /* WinRegione: portami davanti           */

/* --- Messaggi dal SERVER al CLIENT --------------------------------------- */
#define WIN_MSG_CREATA      0x5781  /* WinCreata */
#define WIN_MSG_EVENTO      0x5782  /* WinEvento */

/* --- Gli eventi, che il toolkit gira alla procedura di finestra ---------- */
#define WIN_EV_MOUSE_GIU    1
#define WIN_EV_MOUSE_SU     2
#define WIN_EV_MOUSE_MOSSO  3
#define WIN_EV_TASTO        4
#define WIN_EV_CHIUDI       5
#define WIN_EV_DISEGNA      6   /* «ridisegnati»: sei stato scoperto */
#define WIN_EV_MISURA       7   /* sei stata ridimensionata */

/* Stili di una finestra di primo livello */
#define WIN_ST_TITOLO       0x0001  /* ha la barra del titolo */
#define WIN_ST_BORDO        0x0002
#define WIN_ST_CHIUDI       0x0004  /* ha il pulsante di chiusura */
#define WIN_ST_VISIBILE     0x0008
#define WIN_ST_SFONDO       0x0010  /* e' lo SFONDO: sta sotto a tutte e non
                                     * si sposta. Serve all'immagine di
                                     * scrivania, ed e' il motivo per cui uno
                                     * sfondo non e' un caso a parte */
/* ! SOPRA A TUTTE, E NON SI SPOSTA: e' lo stesso meccanismo dello sfondo
 * girato dall'altra parte. Serve alla barra delle applicazioni, che dev'essere
 * raggiungibile anche quando una finestra e' aperta a schermo intero — se una
 * finestra qualunque potesse coprirla, l'unico modo di tornare al menu sarebbe
 * spostare quella finestra, e con una a schermo intero non si potrebbe. */
#define WIN_ST_SOPRA        0x0020

#define WIN_TITOLO_LEN      48

/* -----------------------------------------------------------------------------
 * Le strutture che viaggiano nell'IPC
 *
 * ! SONO TUTTE DI SOLI unsigned int E char, e non e' pigrizia: attraversano il
 * confine fra due processi compilati separatamente — uno dei quali puo' essere
 * scritto in FreeBASIC. Un campo di larghezza diversa fra i due lati fa
 * leggere la larghezza di una finestra dal campo dell'altezza, cioe' da'
 * numeri plausibili e sbagliati.
 * --------------------------------------------------------------------------- */
typedef struct {
    unsigned int x, y;
    unsigned int larghezza, altezza;
    unsigned int stile;
    char         titolo[WIN_TITOLO_LEN];
} WinCrea;

typedef struct {
    unsigned int id;            /* 0 = rifiutata */
    unsigned int byte;          /* quanto e' grande la zona condivisa */
    unsigned int passo;         /* byte per riga DENTRO la zona */
    unsigned int larghezza;     /* quella concessa, che puo' non essere quella
                                 * chiesta: vedi il commento nel server */
    unsigned int altezza;
} WinCreata;

typedef struct {
    unsigned int id;
    unsigned int x, y;
    unsigned int larghezza, altezza;
} WinRegione;

typedef struct {
    unsigned int id;
    char         titolo[WIN_TITOLO_LEN];
} WinTitolo;

typedef struct {
    unsigned int id;
    unsigned int tipo;          /* WIN_EV_* */
    unsigned int x, y;          /* dentro l'area del client, non sullo schermo */
    unsigned int bottoni;       /* per il mouse */
    unsigned int tasto;         /* scancode, per WIN_EV_TASTO */
} WinEvento;

/* Il nome della zona condivisa di una finestra. Lo compongono tutt'e due i
 * lati con questa, cosi' non ci sono due modi di scriverlo. */
static void win_nome_zona(char *out, unsigned int id)
{
    unsigned int i = 0, n = id, cifre = 0, j;
    char rov[12];

    out[i++] = 'w'; out[i++] = 'i'; out[i++] = 'n';

    if (n == 0) rov[cifre++] = '0';
    while (n > 0) { rov[cifre++] = (char)('0' + (n % 10)); n /= 10; }
    for (j = 0; j < cifre; j++) out[i++] = rov[cifre - 1 - j];
    out[i] = '\0';
}

#endif /* WIN_PROTO_H */
