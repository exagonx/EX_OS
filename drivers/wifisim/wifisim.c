/* =============================================================================
 * drivers/wifisim/wifisim.c — a SIMULATED WiFi card, for tests (@WIFI)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     /dev/wifisim.drv            serve "wifi0" until killed
 *     /dev/wifisim.drv -cadi      tell a running wifisim to drop the link now
 *
 * QEMU emulates no WiFi card, and the WiFi tools (wifid, `wifi`, the desktop
 * icon) must be tried somewhere before a real driver exists. This driver
 * speaks wifi_proto.h exactly as a real one will, with five made-up networks:
 *
 *     SSID            security  password     signal  note
 *     EXOS-Casa       WPA2      casa1234     -45 dBm
 *     EXOS-Ufficio    WPA2      ufficio99    -68 dBm
 *     EXOS-Ospiti     open                   -74 dBm
 *     EXOS-Vecchia    WEP       12345        -81 dBm  (a 40-bit key)
 *     EXOS-Segreta    WPA2      segreta77    -55 dBm  HIDDEN: not in the scan
 *
 * ! IT IS A CARD THAT REALLY CARRIES PACKETS. Once "connected", the link
 * side of its one service ("wifi0", net_proto.h messages) forwards every frame to and from the
 * Ethernet card ("rete0"), and reports that card's MAC. So when wifid moves
 * ip.drv onto the WiFi link, DHCP and ping really go through - in QEMU, to
 * QEMU's own network. What is NOT simulated is the radio: no 802.11 frames,
 * no encryption, no handshake. That is lib/exwifi and the real drivers.
 *
 * The password check is the real one, though: a WPA2 network accepts the
 * passphrase or the PBKDF2-SHA1 key derived from it (what wifid keeps), and
 * refuses anything else with WIFI_ERR_PAROLA.
 *
 * ! IT IS NOT STARTED BY ANYTHING ON ITS OWN. On a real machine it would
 * pretend a radio is there; it is started by hand, or by a test.
 * ============================================================================= */
#include "libc.h"
#include "net_proto.h"
#include "ip_proto.h"
#include "wifi_proto.h"
#include "excrypt.h"

EX_VERSIONE("wifisim.drv", "0.001");

#define WIFISIM_MSG_CADI   99      /* simulation only: drop the link now */

typedef struct {
    const char   *ssid;
    const char   *parola;
    int           auth;
    int           dbm;
    int           canale;
    int           nascosta;
    unsigned char bssid[6];
} Finta;

static const Finta g_reti[] = {
    { "EXOS-Casa",    "casa1234",  WIFI_AUTH_WPA2,   -45,  6, 0, {0x02,0xE0,0x05,0x00,0x00,0x01} },
    { "EXOS-Ufficio", "ufficio99", WIFI_AUTH_WPA2,   -68, 11, 0, {0x02,0xE0,0x05,0x00,0x00,0x02} },
    { "EXOS-Ospiti",  "",          WIFI_AUTH_APERTA, -74,  1, 0, {0x02,0xE0,0x05,0x00,0x00,0x03} },
    { "EXOS-Vecchia", "12345",     WIFI_AUTH_WEP,    -81,  3, 0, {0x02,0xE0,0x05,0x00,0x00,0x04} },
    { "EXOS-Segreta", "segreta77", WIFI_AUTH_WPA2,   -55,  9, 1, {0x02,0xE0,0x05,0x00,0x00,0x05} },
};
#define N_RETI ((int)(sizeof(g_reti) / sizeof(g_reti[0])))

static int        g_pid_eth = 0;          /* the Ethernet card that carries the frames */
static int        g_lettore = 0;          /* who has a NET_MSG_RICEVI pending on us */
static int        g_armato  = 0;          /* our own RICEVI pending at the Ethernet card */
static NetStato   g_eth;                  /* the Ethernet card's identity (its MAC) */
static WifiStato  g_st;

static void stato_vuoto(void)
{
    memset(&g_st, 0, sizeof(g_st));
    g_st.stato = WIFI_STATO_FERMA;
    strcpy(g_st.scheda, "simulata (wifisim)");
}

/* --- the Ethernet card underneath -------------------------------------------- */
static int eth_aggancia(void)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           i;

    g_pid_eth = ipc_attendi(NET_SERVIZIO_0, 30000);
    if (g_pid_eth <= 0) return -1;
    if (ipc_send(g_pid_eth, NET_MSG_INFO, NULL, 0) < 0) return -1;
    for (i = 0; i < 8; i++) {
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), 2000) < 0) return -1;
        if ((int)meta.sender_pid == g_pid_eth && meta.tipo == NET_MSG_STATO &&
            meta.len >= sizeof(g_eth)) {
            memcpy(&g_eth, buf, sizeof(g_eth));
            return 0;
        }
    }
    return -1;
}

/* Our receive request at the Ethernet card exists only while the link is up
 * AND someone is waiting for a frame from us: before that, the frames belong
 * to whoever else reads the Ethernet card (ip.drv, until it moves). */
static void eth_arma(void)
{
    if (g_armato || g_lettore == 0 || g_st.stato != WIFI_STATO_CONNESSA) return;
    if (ipc_send(g_pid_eth, NET_MSG_RICEVI, NULL, 0) == 0) g_armato = 1;
}

/* --- the made-up radio ---------------------------------------------------------- */
static const Finta *trova(const char *ssid)
{
    int i;

    for (i = 0; i < N_RETI; i++) if (strcmp(g_reti[i].ssid, ssid) == 0) return &g_reti[i];
    return NULL;
}

static int chiave_giusta(const Finta *f, const WifiConnetti *c, unsigned int len)
{
    if (f->auth == WIFI_AUTH_APERTA) return 1;
    if (c->parola[0] && strcmp(c->parola, f->parola) == 0) return 1;
    /* the derived key, as wifid sends it (only the extended request has it) */
    if (len >= sizeof(*c) && c->ha_psk && f->auth != WIFI_AUTH_WEP) {
        unsigned char psk[32];

        pbkdf2_sha1((const unsigned char *)f->parola, (unsigned int)strlen(f->parola),
                    (const unsigned char *)f->ssid, (unsigned int)strlen(f->ssid),
                    4096, psk, 32);
        return memcmp(psk, c->psk, 32) == 0;
    }
    return 0;
}

static void esito(int pid, int codice)
{
    WifiEsito e;

    memset(&e, 0, sizeof(e));
    e.codice = codice;
    strncpy(e.perche, wifi_perche(codice), sizeof(e.perche) - 1);
    ipc_send(pid, WIFI_MSG_ESITO, &e, sizeof(e));
}

static void connetti(int pid, const unsigned char *p, unsigned int len)
{
    WifiConnetti c;
    const Finta *f;

    memset(&c, 0, sizeof(c));
    memcpy(&c, p, len < sizeof(c) ? len : sizeof(c));
    if (len < sizeof(c)) { c.auth = -1; c.nascosta = 0; c.ha_psk = 0; }
    c.ssid[WIFI_SSID_MAX - 1] = 0;

    usleep(400000);                                   /* association takes a moment */
    f = trova(c.ssid);
    if (f == NULL || (f->nascosta && !c.nascosta)) { esito(pid, WIFI_ERR_NON_TROVATA); return; }
    if (c.auth >= 0 && c.auth != f->auth)            { esito(pid, WIFI_ERR_AUTH); return; }
    if (!chiave_giusta(f, &c, len))                  { esito(pid, WIFI_ERR_PAROLA); return; }

    g_st.stato    = WIFI_STATO_CONNESSA;
    strcpy(g_st.ssid, f->ssid);
    memcpy(g_st.bssid, f->bssid, 6);
    g_st.canale   = f->canale;
    g_st.potenza  = f->dbm;
    g_st.auth     = f->auth;
    g_st.velocita = (f->auth == WIFI_AUTH_WEP) ? 11 : 54;
    printf("wifisim: collegata a %s\n", f->ssid);
    esito(pid, 0);
    /* a fresh receive request: while we were off, the Ethernet card went
     * back to ip.drv (wifid moved it there), and our old request is void -
     * the card keeps ONE reader, the last one that asked */
    g_armato = 0;
    eth_arma();
}

static void servi(void)
{
    IpcMessage    meta;
    unsigned char buf[IPC_MSG_MAX_DATA];
    int           i;

    for (;;) {
        if (ipc_recv(&meta, buf, sizeof(buf)) < 0) continue;

        /* a frame from the Ethernet card: to whoever waits on our link */
        if ((int)meta.sender_pid == g_pid_eth) {
            if (meta.tipo == NET_MSG_FRAME) {
                g_armato = 0;
                if (g_st.stato == WIFI_STATO_CONNESSA && g_lettore) {
                    ipc_send((unsigned int)g_lettore, NET_MSG_FRAME, buf, meta.len);
                    g_st.byte_ricevuti += meta.len;
                    g_lettore = 0;
                }
            }
            continue;
        }

        switch (meta.tipo) {
        /* --- wifi0: the control ----------------------------------------------- */
        case WIFI_MSG_CERCA:
            usleep(600000);                           /* a scan visits every channel */
            for (i = 0; i < N_RETI; i++) {
                WifiRete r;

                if (g_reti[i].nascosta) continue;
                memset(&r, 0, sizeof(r));
                strcpy(r.ssid, g_reti[i].ssid);
                memcpy(r.bssid, g_reti[i].bssid, 6);
                r.canale = g_reti[i].canale;
                r.potenza = g_reti[i].dbm;
                r.auth = g_reti[i].auth;
                r.velocita_max = (g_reti[i].auth == WIFI_AUTH_WEP) ? 11 : 54;
                ipc_send(meta.sender_pid, WIFI_MSG_RETI, &r, sizeof(r));
            }
            ipc_send(meta.sender_pid, WIFI_MSG_FINE, NULL, 0);
            break;
        case WIFI_MSG_CONNETTI:
            connetti((int)meta.sender_pid, buf, meta.len);
            break;
        case WIFI_MSG_STATO:
            ipc_send(meta.sender_pid, WIFI_MSG_STATO_R, &g_st, sizeof(g_st));
            break;
        case WIFI_MSG_STACCA:
            stato_vuoto();
            esito((int)meta.sender_pid, 0);
            break;
        case WIFISIM_MSG_CADI:
            if (g_st.stato == WIFI_STATO_CONNESSA) {
                printf("wifisim: collegamento caduto (simulato)\n");
                g_st.stato = WIFI_STATO_CADUTA;
            }
            break;

        /* --- the link side of wifi0: net_proto.h ------------------------------- */
        case NET_MSG_INFO: {
            NetStato s = g_eth;

            strcpy(s.modello, "wifisim over Ethernet");
            ipc_send(meta.sender_pid, NET_MSG_STATO, &s, sizeof(s));
            break;
        }
        case NET_MSG_INVIA: {
            NetEsito e;

            if (g_st.stato != WIFI_STATO_CONNESSA) {
                e.codice = -ENETDOWN;                 /* a cable not plugged in */
                ipc_send(meta.sender_pid, NET_MSG_ESITO, &e, sizeof(e));
                break;
            }
            g_st.byte_inviati += meta.len;
            ipc_send(g_pid_eth, NET_MSG_INVIA, buf, meta.len);
            e.codice = 0;                             /* the card answers to us: we say yes */
            ipc_send(meta.sender_pid, NET_MSG_ESITO, &e, sizeof(e));
            break;
        }
        case NET_MSG_RICEVI:
            g_lettore = (int)meta.sender_pid;
            eth_arma();
            break;
        case NET_MSG_ANNULLA:
            if (g_lettore == (int)meta.sender_pid) g_lettore = 0;
            break;
        default:
            break;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "-cadi") == 0) {
        int pid = ipc_lookup(WIFI_SERVIZIO_0);

        if (pid <= 0) { printf("wifisim: non e' in esecuzione\n"); return 1; }
        ipc_send((unsigned int)pid, WIFISIM_MSG_CADI, NULL, 0);
        printf("wifisim: le ho detto di far cadere il collegamento\n");
        return 0;
    }
    if (argc > 1) {
        printf("uso: wifisim.drv            una scheda WiFi simulata (wifi0)\n");
        printf("     wifisim.drv -cadi      fa cadere il collegamento\n");
        return argc > 1 && strcmp(argv[1], "-h") == 0 ? 0 : 2;
    }

    stato_vuoto();
    if (eth_aggancia() != 0) {
        printf("wifisim: manca la scheda Ethernet (rete0) che porta i pacchetti\n");
        return 1;
    }
    if (ipc_register(WIFI_SERVIZIO_0) < 0) {
        printf("wifisim: non riesco a registrare '%s'\n", WIFI_SERVIZIO_0);
        return 1;
    }
    printf("wifisim: scheda WiFi simulata - servizio '%s', %d reti\n",
           WIFI_SERVIZIO_0, N_RETI);
    servi();
    return 0;
}
