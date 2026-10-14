/* =============================================================================
 * drivers/net/wifid_proto.h — the WiFi connection manager's protocol (@WIFI)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * wifid (bin/wifid) is the one process that decides which network the radio
 * joins: it keeps the saved networks, connects at start-up and again after a
 * drop, gives the address (DHCP or static) to ip.drv, and tells whoever asks
 * what happened. The two faces the user sees talk to it, never to the driver:
 *
 *     wifi            (bin/wifi)          the command line
 *     the WiFi icon   (exwin/bin/pm)      the desktop bar, and its panel
 *                     (exwin/bin/exwifi)
 *
 * The driver below speaks wifi_proto.h, and only wifid speaks to it.
 *
 * ! WHO MAY CHANGE WHAT is decided here, from the sender's uid (proc_uid(),
 * kernel 0.250 - an IPC message does not carry it):
 *   - wifid run by root, with start-up connection on (the system network):
 *     only root changes it; any user sees the state and uses the connection.
 *   - otherwise wifid belongs to the user who started it, and that user
 *     manages it.
 * A refused request gets WIFID_ERR_PERMESSO, with the reason in the text.
 * ============================================================================= */
#ifndef WIFID_PROTO_H
#define WIFID_PROTO_H

#include "wifi_proto.h"

#define WIFID_SERVIZIO        "wifid"

/* --- requests ------------------------------------------------------------ */
#define WIFID_MSG_STATO        1   /*                    -> WIFID_MSG_STATO_R  */
#define WIFID_MSG_CERCA        2   /*                    -> WIFID_MSG_RETE..., WIFID_MSG_FINE */
#define WIFID_MSG_CONNETTI     3   /* WifidProfilo       -> WIFID_MSG_ESITO     */
#define WIFID_MSG_STACCA       4   /*                    -> WIFID_MSG_ESITO     */
#define WIFID_MSG_SALVATE      5   /*                    -> WIFID_MSG_PROFILO..., WIFID_MSG_FINE */
#define WIFID_MSG_SALVA        6   /* WifidProfilo       -> WIFID_MSG_ESITO     */
#define WIFID_MSG_DIMENTICA    7   /* SSID (char[33])    -> WIFID_MSG_ESITO     */
#define WIFID_MSG_PREDEFINITA  8   /* SSID, "" = none    -> WIFID_MSG_ESITO     */
#define WIFID_MSG_AVVIO        9   /* int: 1 on, 0 off   -> WIFID_MSG_ESITO  (root only) */
#define WIFID_MSG_ASCOLTA     10   /*                    -> WIFID_MSG_EVENTO from now on */

/* --- answers --------------------------------------------------------------- */
#define WIFID_MSG_STATO_R    130
#define WIFID_MSG_RETE       131   /* WifidRete            */
#define WIFID_MSG_PROFILO    132   /* WifidProfilo, without the key */
#define WIFID_MSG_FINE       133   /* a list is over        */
#define WIFID_MSG_ESITO      134   /* WifiEsito (wifi_proto.h) */
#define WIFID_MSG_EVENTO     135   /* WifidEvento, pushed   */

#define WIFID_ERR_PERMESSO   -20   /* only root may change the system network */
#define WIFID_ERR_SCHEDA     -21   /* no WiFi driver answers ("wifi0")        */
#define WIFID_ERR_PIENO      -22   /* too many saved networks                 */
#define WIFID_ERR_IP         -23   /* connected, but no address (DHCP failed) */

#define WIFID_SALVATE_MAX     16

/* How the address is obtained, per network */
#define WIFID_IP_DHCP         0
#define WIFID_IP_STATICO      1

/* A saved (or to be saved) network. In a request the passphrase travels in
 * `parola`; wifid derives the key and forgets the passphrase. In an answer
 * (WIFID_MSG_PROFILO) `parola` is always empty. */
typedef struct {
    char          ssid[WIFI_SSID_MAX];
    char          parola[64];
    int           auth;            /* WIFI_AUTH_*, or -1: as the network says */
    int           nascosta;        /* hidden: probe for it by name            */
    int           predefinita;     /* the forced choice, over the strongest   */
    int           salva;           /* in WIFID_MSG_CONNETTI: save it as well  */
    int           ip_metodo;       /* WIFID_IP_DHCP / WIFID_IP_STATICO        */
    unsigned char ip[4], maschera[4], gateway[4], dns[4];
} WifidProfilo;

/* A network heard by the radio, with what wifid knows about it */
typedef struct {
    WifiRete rete;                 /* wifi_proto.h: SSID, BSSID, channel, dBm... */
    int      salvata;
    int      predefinita;
} WifidRete;

/* The state, for the command line and the icon */
typedef struct {
    WifiStato     radio;           /* the driver's own (wifi_proto.h)           */
    int           scheda;          /* 0: no WiFi driver answers                 */
    int           ip_ok;           /* the address is set                         */
    unsigned char ip[4], maschera[4], gateway[4], dns[4];
    int           ip_metodo;
    int           avvio;           /* start-up connection on (root's setting)    */
    int           sistema;         /* 1: this is root's system connection        */
    unsigned int  proprietario;    /* uid wifid runs as                           */
    int           uscita;          /* 1: ip.drv is using the radio right now     */
} WifidStato;

/* Something happened: the icon shows `testo` in its balloon */
#define WIFID_EV_COLLEGATA   1
#define WIFID_EV_CADUTA      2
#define WIFID_EV_STACCATA    3
#define WIFID_EV_ERRORE      4
#define WIFID_EV_IP          5
typedef struct {
    int  tipo;                     /* WIFID_EV_*                                 */
    char ssid[WIFI_SSID_MAX];
    char testo[96];                /* ASCII, for the screen                      */
} WifidEvento;

#endif /* WIFID_PROTO_H */
