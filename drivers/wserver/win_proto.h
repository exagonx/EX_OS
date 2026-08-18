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

/* =============================================================================
 * ! IL NOME DEL SERVIZIO E' PER UTENTE, dal 17 agosto 2026.
 *
 * root registra `wserver`; chiunque altro `<uid>:wserver`. Non e' una
 * complicazione: e' la regola del kernel — un processo non root puo'
 * registrare solo nomi che cominciano col proprio uid — e senza di essa un
 * utente potrebbe prendersi il nome `wserver` e ricevere le finestre di tutti
 * gli altri.
 *
 * ! ED E' COSI' CHE LA GRAFICA DIVENTA MULTIUTENTE. Ogni utente ha il suo
 * server, sulla sua console, e non vede quello degli altri: non perche' glielo
 * si impedisca con un controllo in piu', ma perche' non ne conosce il nome.
 *
 * La stessa funzione la usano il server per registrarsi e il toolkit per
 * cercarlo: se divergessero, il client cercherebbe un nome che nessuno ha.
 * ============================================================================= */
static inline void win_nome_servizio(char *out, unsigned int max)
{
    unsigned int uid = (unsigned int)getuid();
    unsigned int i = 0, l = 0, k;
    char rov[12];

    if (uid != 0) {
        unsigned int v = uid;

        if (v == 0) rov[l++] = '0';
        while (v > 0) { rov[l++] = (char)('0' + (v % 10u)); v /= 10u; }
        for (k = 0; k < l && i + 1 < max; k++) out[i++] = rov[l - 1 - k];
        if (i + 1 < max) out[i++] = ':';
    }

    for (k = 0; WIN_SERVIZIO[k] && i + 1 < max; k++) out[i++] = WIN_SERVIZIO[k];
    out[i] = '\0';
}

/* Il nome della zona condivisa di una finestra: "win" piu' il numero. Lo
 * costruiscono tutt'e due i lati con win_nome_zona(). */
#define WIN_ZONA_PREFISSO   "win"

/* --- Messaggi dal CLIENT al SERVER --------------------------------------- */
#define WIN_MSG_CREA        0x5701  /* WinCrea    -> risponde WIN_MSG_CREATA */
#define WIN_MSG_AGGIORNA    0x5702  /* WinRegione: «ho finito di disegnare»,
                                     * e la misura che ci ha messo dentro e'
                                     * anche la RICEVUTA di WIN_MSG_MISURATA */
#define WIN_MSG_SPOSTA      0x5703  /* WinRegione: cambia POSIZIONE          */
#define WIN_MSG_TITOLO      0x5704  /* WinTitolo                             */
#define WIN_MSG_DISTRUGGI   0x5705  /* WinRegione (solo il campo id)         */
#define WIN_MSG_PRIMO       0x5706  /* WinRegione: portami davanti           */
/* ! LA MISURA HA UN MESSAGGIO SUO, E NON E' UN DOPPIONE DI WIN_MSG_SPOSTA.
 * Fino a quando le due cose viaggiavano insieme, chiedere una misura nuova
 * voleva dire anche mandare una posizione — e il client la posizione VERA non
 * la sa: se l'utente ha trascinato la finestra per la barra del titolo, quel
 * movimento lo conosce solo il server. Il risultato sarebbe stato una finestra
 * che, ridimensionandosi, TORNA DA SOLA dove stava quando e' nata. Difetto
 * trovato rileggendo, non provando: la prova aveva la finestra ferma.
 *
 * Usa i soli campi id, larghezza e altezza; x e y si ignorano. */
#define WIN_MSG_MISURA      0x5707  /* WinRegione: cambia MISURA             */

/* --- Messaggi dal SERVER al CLIENT --------------------------------------- */
#define WIN_MSG_CREATA      0x5781  /* WinCreata */
#define WIN_MSG_EVENTO      0x5782  /* WinEvento */
#define WIN_MSG_MISURATA    0x5783  /* WinCreata: «la tua zona e' un'altra» */

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

/* ! MODALE VUOL DIRE «FINCHE' CI SONO IO, LE ALTRE DEL MIO PADRONE NON
 * RISPONDONO». Non basta stare sopra: una finestra che copre e basta lascia
 * cliccare quello che si vede intorno, e un dialogo «vuoi perdere le
 * modifiche?» a cui si puo' rispondere continuando a scrivere nel testo non
 * sta chiedendo niente.
 *
 * ! ED E' MODALE PER L'APPLICAZIONE, NON PER LO SCHERMO, che e' la decisione
 * che conta: blocca solo le finestre dello STESSO processo. Un modale di
 * sistema bloccherebbe tutto — e il giorno che il client muore con il dialogo
 * aperto, lo schermo resta bloccato e non c'e' modo di rimediare se non
 * ammazzando il server. Cosi' invece muore il client, le sue finestre se ne
 * vanno con lui, e tutto il resto non se n'e' nemmeno accorto.
 *
 * Il server non «disabilita» niente: butta i clic destinati alle finestre
 * bloccate e porta davanti la modale, cosi' chi ha cliccato vede DOVE deve
 * rispondere invece di trovarsi un'applicazione sorda. */
#define WIN_ST_MODALE       0x0040

/* =============================================================================
 * ! SI RIDIMENSIONA SOLO CHI L'HA CHIESTO, e non e' prudenza eccessiva: e' la
 * sola scelta onesta. Cambiare misura a una finestra vuol dire darle una zona
 * di pixel NUOVA e piu' grande, con dentro quella vecchia in un angolo. Chi
 * non sa rispondere a WIN_MSG_MISURATA si ritrova meta' finestra col colore di
 * partenza e i propri controlli fermi dov'erano — cioe' una finestra rotta,
 * fatta rompere dal server.
 *
 * Quindi la presa nell'angolo compare solo se questo bit c'e', e un'applicazione
 * che non sa rifare la propria disposizione semplicemente non lo mette. E'
 * l'opposto di quello che farebbe un sistema che ridimensiona tutto e lascia a
 * ognuno il compito di accorgersene.
 * ============================================================================= */
#define WIN_ST_RIDIM        0x0080

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
/* =============================================================================
 * ! «METTILA TU» E' UNA POSIZIONE COME UN'ALTRA, ED E' CIO' CHE PERMETTE DI
 * APRIRE DUE VOLTE LO STESSO PROGRAMMA.
 *
 * Un'applicazione che nasce sempre nello stesso punto va benissimo finche' e'
 * una sola: la seconda copia si sovrappone alla prima ESATTAMENTE, e chi
 * guarda crede che non si sia aperta. E' un difetto che non da' nessun
 * messaggio d'errore — il modo peggiore in cui una cosa puo' non funzionare.
 *
 * ! E LA SCELTA LA FA IL SERVER, NON L'APPLICAZIONE, perche' e' l'unico che sa
 * quante finestre ci sono gia' e quanto e' grande lo schermo. Un programma che
 * si scostasse da solo — poniamo in base al proprio PID — sceglierebbe alla
 * cieca, e due programmi DIVERSI ricadrebbero nello stesso punto lo stesso.
 * ============================================================================= */
#define WIN_XY_AUTO     0xFFFFFFFFu

typedef struct {
    unsigned int x, y;          /* WIN_XY_AUTO = «scegli tu» */
    unsigned int larghezza, altezza;
    unsigned int stile;
    char         titolo[WIN_TITOLO_LEN];
} WinCrea;

/* ! LA STESSA STRUTTURA DICE «ECCO LA TUA FINESTRA» E «ECCO LA TUA FINESTRA
 * NUOVA», cioe' WIN_MSG_CREATA e WIN_MSG_MISURATA. Sono la stessa notizia detta
 * due volte nella vita di una finestra, e due strutture diverse vorrebbero dire
 * due pezzi di codice che mappano una zona condivisa — che divergono. */
typedef struct {
    unsigned int id;            /* 0 = rifiutata */
    unsigned int byte;          /* quanto e' grande la zona condivisa */
    unsigned int passo;         /* byte per riga DENTRO la zona */
    unsigned int larghezza;     /* quella concessa, che puo' non essere quella
                                 * chiesta: vedi il commento nel server */
    unsigned int altezza;
    /* ! IL NUMERO DI GIRO ENTRA NEL NOME DELLA ZONA, ED E' CIO' CHE RENDE IL
     * CAMBIO POSSIBILE. Una zona condivisa non si allarga: si crea quella nuova
     * e si lascia morire la vecchia quando l'ultimo la chiude. Ma finche' il
     * client tiene aperta «win7», «win7» esiste ancora e ricrearla darebbe la
     * VECCHIA. Con il giro dentro il nome le due zone convivono per il tempo di
     * una consegna, e nessuno resta senza pixel in mezzo. */
    unsigned int giro;
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

/* Il nome della zona condivisa di una finestra: «win», il numero della
 * finestra, un punto e il giro — «win7.0», poi «win7.1» dopo il primo
 * ridimensionamento. Lo compongono tutt'e due i lati con questa, cosi' non ci
 * sono due modi di scriverlo.
 *
 * ! IL GIRO NON E' UN ORNAMENTO: due zone con lo stesso nome sono la STESSA
 * zona, e chiederne una nuova mentre il client tiene ancora aperta la vecchia
 * renderebbe la vecchia — con la misura di prima e nessun errore. */
static void win_nome_zona(char *out, unsigned int id, unsigned int giro)
{
    unsigned int i = 0, cifre, j, n;
    char rov[12];

    out[i++] = 'w'; out[i++] = 'i'; out[i++] = 'n';

    n = id; cifre = 0;
    if (n == 0) rov[cifre++] = '0';
    while (n > 0) { rov[cifre++] = (char)('0' + (n % 10)); n /= 10; }
    for (j = 0; j < cifre; j++) out[i++] = rov[cifre - 1 - j];

    out[i++] = '.';

    n = giro; cifre = 0;
    if (n == 0) rov[cifre++] = '0';
    while (n > 0) { rov[cifre++] = (char)('0' + (n % 10)); n /= 10; }
    for (j = 0; j < cifre; j++) out[i++] = rov[cifre - 1 - j];

    out[i] = '\0';
}

#endif /* WIN_PROTO_H */
