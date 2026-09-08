/* =============================================================================
 * drivers/net/wifi_proto.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Protocollo IPC delle schede SENZA FILI.
 *
 * ! UNA SCHEDA WIRELESS NON E' UNA SCHEDA ETHERNET PIU' LENTA. Una Ethernet,
 * appena accesa, consegna pacchetti; una wireless prima deve cercare le reti
 * che si sentono, sceglierne una, autenticarsi, scambiare le chiavi — e solo
 * dopo somiglia a una scheda. Quel «prima» non entra in net_proto.h, che
 * parla di frame e basta, ed e' il motivo per cui esiste questo file.
 *
 * ! IL DRIVER REGISTRA DUE SERVIZI, ED E' LA SCELTA CHE TIENE INSIEME TUTTO.
 * Come «rete0» parla net_proto.h e lo stack IP non sa nemmeno di avere sotto
 * una radio; come «wifi0» parla questo protocollo, e ci parla soltanto il
 * comando `wifi`. Cosi' ip.drv non impara una parola nuova, e il giorno che si
 * aggiunge una seconda scheda senza fili non cambia niente da nessuna parte.
 *
 * ! FINCHE' NON SI E' ASSOCIATI, «rete0» NON CONSEGNA NIENTE. Non e' un
 * guasto: e' che non c'e' nessun collegamento sotto. Un NET_MSG_RICEVI resta
 * in attesa, esattamente come su un cavo staccato.
 *
 * -----------------------------------------------------------------------------
 * LE OPZIONI SONO LE STESSE PER TUTTE LE SCHEDE
 *
 * Sta scritto qui perche' e' una regola della famiglia, non di un driver:
 *
 *     wifi -showlist            l'elenco delle reti che si sentono
 *     wifi -connect:<SSID>      si aggancia a quella
 *     wifi -status              come va la connessione di adesso
 *     wifi -h                   le opzioni, tutte
 *
 * ! IL TIPO DI AUTENTICAZIONE NON SI CHIEDE, SI RICONOSCE. WEP, WPA, WPA2 o
 * WPA3 stanno negli information element del beacon: domandarlo a chi si
 * collega vorrebbe dire chiedergli una cosa che la rete gia' dichiara, e
 * lasciargli sbagliare la risposta. La PASSWORD invece si chiede, perche'
 * quella nel beacon non c'e'.
 * ============================================================================= */

#ifndef WIFI_PROTO_H
#define WIFI_PROTO_H

/* Il nome del servizio. Il numero e' l'indice: una seconda scheda senza fili
 * si registrera' come "wifi1", esattamente come fa "rete1". */
#define WIFI_SERVIZIO_0   "wifi0"

/* Un SSID sta in 32 byte per definizione (802.11), e non e' terminato da
 * zero: qui se ne tengono 33 per poterlo stampare senza copiarlo altrove. */
#define WIFI_SSID_MAX     33

/* --- Richieste (client -> driver) --------------------------------------- */
#define WIFI_MSG_CERCA      20   /* -> WIFI_MSG_RETI, una per messaggio    */
#define WIFI_MSG_CONNETTI   21   /* payload = WifiConnetti -> WIFI_MSG_ESITO */
#define WIFI_MSG_STATO      22   /* -> WIFI_MSG_STATO_R                    */
#define WIFI_MSG_STACCA     23   /* -> WIFI_MSG_ESITO                      */

/* --- Risposte (driver -> client) ---------------------------------------- */
#define WIFI_MSG_RETI     148    /* payload = WifiRete                     */
#define WIFI_MSG_FINE     149    /* l'elenco e' finito                     */
#define WIFI_MSG_STATO_R  150
#define WIFI_MSG_ESITO    151

/* =============================================================================
 * COME SI DIFENDE UNA RETE
 *
 * ! I NUMERI CRESCONO COL ROBUSTEZZA, e serve: un driver che ne incontra uno
 * che non conosce puo' dire «piu' nuovo di me» invece di «sconosciuto», che e'
 * un'informazione diversa e piu' utile.
 * ========================================================================== */
#define WIFI_AUTH_APERTA   0   /* nessuna: chiunque entra                   */
#define WIFI_AUTH_WEP      1   /* rotta da vent'anni, ma esiste ancora      */
#define WIFI_AUTH_WPA      2   /* TKIP                                      */
#define WIFI_AUTH_WPA2     3   /* CCMP, cioe' AES                           */
#define WIFI_AUTH_WPA3     4   /* SAE                                       */

/* Il nome, per chi lo deve scrivere a schermo. */
const char *wifi_auth_nome(int auth);

/* Una rete vista durante la ricerca. */
typedef struct {
    char           ssid[WIFI_SSID_MAX];
    unsigned char  bssid[6];      /* il MAC dell'access point              */
    int            canale;
    int            potenza;       /* dBm: piu' vicino a zero = piu' forte  */
    int            auth;          /* WIFI_AUTH_*                           */
    unsigned int   velocita_max;  /* Mbit/s dichiarati dal beacon          */
} WifiRete;

/* La richiesta di connessione.
 *
 * ! LA PASSWORD ARRIVA NEL MESSAGGIO, E VA DETTO DOV'E' STATA. Un IPC dentro
 * la stessa macchina non e' una rete: nessuno la vede passare. Ma il driver
 * NON deve tenersela dopo aver derivato la chiave, e non deve stamparla
 * nemmeno nei suoi messaggi di errore — una password in un registro e' una
 * password persa. */
typedef struct {
    char ssid[WIFI_SSID_MAX];
    char parola[64];      /* vuota se la rete e' aperta */
} WifiConnetti;

/* Lo stato del collegamento. */
#define WIFI_STATO_SPENTA     0   /* la radio non e' accesa               */
#define WIFI_STATO_FERMA      1   /* accesa, non agganciata a niente      */
#define WIFI_STATO_CERCA      2   /* sta guardando cosa c'e'              */
#define WIFI_STATO_AGGANCIA   3   /* autenticazione e associazione        */
#define WIFI_STATO_CHIAVI     4   /* scambio delle chiavi (EAPOL)         */
#define WIFI_STATO_CONNESSA   5   /* si passano i pacchetti               */
#define WIFI_STATO_CADUTA     6   /* c'era, e non c'e' piu'               */

const char *wifi_stato_nome(int stato);

typedef struct {
    int            stato;         /* WIFI_STATO_*                          */
    char           ssid[WIFI_SSID_MAX];
    unsigned char  bssid[6];
    int            canale;
    int            potenza;       /* dBm                                   */
    int            auth;          /* WIFI_AUTH_* della rete agganciata     */
    unsigned int   velocita;      /* Mbit/s in uso adesso                  */

    /* ! I BYTE SONO QUELLI DELLA RADIO, non quelli di TCP. Servono a
     * rispondere alla domanda «la scheda sta ricevendo?» quando piu' in alto
     * non arriva niente: se questi salgono e IP non vede pacchetti, il guasto
     * sta fra i due, e si sa dove guardare. */
    unsigned int   byte_inviati;
    unsigned int   byte_ricevuti;

    /* Il modello, per chi chiede -status senza sapere che scheda ha. */
    char           scheda[32];
} WifiStato;

/* Risposta a CONNETTI e STACCA: 0 = fatto, <0 = -errno o un motivo qui sotto. */
#define WIFI_ERR_NON_TROVATA   -1   /* quell'SSID non si sente             */
#define WIFI_ERR_PAROLA        -2   /* le chiavi non tornano               */
#define WIFI_ERR_AUTH          -3   /* difesa che questo driver non sa fare */
#define WIFI_ERR_FIRMWARE      -4   /* la radio non ha il suo firmware     */
#define WIFI_ERR_TEMPO         -5   /* l'access point non ha risposto      */

typedef struct {
    int codice;
    /* ! IL MOTIVO IN CHIARO ACCANTO AL NUMERO. «-2» dice poco a chi guarda
     * lo schermo; «la password non e' quella» dice tutto. Sono due lettori
     * diversi e vogliono due cose diverse. */
    char perche[96];
} WifiEsito;

const char *wifi_perche(int codice);

#endif /* WIFI_PROTO_H */
