/* =============================================================================
 * drivers/ar5k/ar5k_servizio.c — the "wifi0" service of the Atheros card
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     ar5k.drv &        (no option)
 *
 * What wifid, `wifi` and the ExWin panel talk to (drivers/net/wifi_proto.h),
 * and the link ip.drv uses once wifid moves it here (net_proto.h):
 *
 *   WIFI_MSG_CERCA     a real scan of channels 1-13 (ar5k_cerca), the
 *                      networks heard, hidden ones left out. While connected
 *                      the chip stays on its channel: the last list is given
 *                      again, with the network in use.
 *   WIFI_MSG_CONNETTI  ar5k_conn.c: open, WPA or WPA2 with PSK (CCMP or
 *                      TKIP, in software). WEP, Enterprise and WPA3 are
 *                      refused with the reason. The password or key in the
 *                      request is wiped from the receive buffer at once.
 *   WIFI_MSG_STATO     radio, connection, signal, speed, bytes
 *   WIFI_MSG_STACCA    a deauthentication, the chip back in reset
 *   net_proto.h        INFO (the MAC), INVIA, RICEVI, ANNULLA: frames go
 *                      through the radio only while connected - before,
 *                      INVIA says "network down" and RICEVI waits, as on an
 *                      unplugged cable.
 *
 * On the user's HP laptop (card subsystem 103c:137b) the service also turns
 * the radio on through the EC at start - after every boot the EC has it off
 * - and serves the WiFi button between two messages (ar5k_radio.c), the job
 * ACPI would do. The button switching the radio off drops the connection.
 * While not connected the chip is in reset and no DMA runs.
 * ============================================================================= */
#include "libc.h"
#include "net_proto.h"
#include "wifi_proto.h"
#include "ar5k_conn.h"
#include "extaskbar.h"

#define RX_N    32
#define RX_BUF  2400
#define RETI    24

int ar5k_cerca(volatile void *registri, unsigned int sotto_id, DmaZona *z,
               WifiRete *out, int max);
int ar5k_ec_prendi(unsigned int sotto_id);
int ar5k_ec_radio_accesa(void);
int ar5k_ec_accendi(void);
int ar5k_ec_svuota(void);
int ar5k_ec_servi(void);
void ar5k_spia(volatile void *registri, unsigned int sotto_id, int accesa);

static WifiStato g_st;
static int       g_ec;              /* 1: the HP laptop's EC is ours */
static int       g_spenta;          /* the EC says the radio is off */
static int       g_caduta;          /* the last connection was lost */
static int       g_lettore;         /* who has a NET_MSG_RICEVI pending */

static void esito(int pid, int codice, const char *perche)
{
    WifiEsito e;

    memset(&e, 0, sizeof(e));
    e.codice = codice;
    if (perche) strncpy(e.perche, perche, sizeof(e.perche) - 1);
    ipc_send((unsigned int)pid, WIFI_MSG_ESITO, &e, sizeof(e));
}

static volatile void *g_registri;
static unsigned int   g_sotto;

static void stato_fai(void)
{
    char scheda[32];

    memcpy(scheda, g_st.scheda, sizeof(scheda));
    memset(&g_st, 0, sizeof(g_st));
    memcpy(g_st.scheda, scheda, sizeof(scheda));
    g_st.stato = g_spenta ? WIFI_STATO_SPENTA : g_caduta ? WIFI_STATO_CADUTA : WIFI_STATO_FERMA;
    ar5k_conn_stato(&g_st);
}

/* the radio's state from the EC, and the light to match it; the radio
 * switched off drops the connection */
static void aggiorna_radio(void)
{
    if (g_ec) g_spenta = !ar5k_ec_radio_accesa();
    if (g_spenta && ar5k_conn_attiva()) {
        printf("ar5k: radio spenta dal tasto: collegamento chiuso\n");
        ar5k_conn_stacca();
        g_caduta = 1;
    }
    stato_fai();
    ar5k_spia(g_registri, g_sotto, !g_spenta);
}

/* (@EXTASKBAR) the WiFi items of the status bar, once a second: state,
 * frames received and sent, sends not acknowledged, frames lost */
static void voci_barra(void)
{
    static unsigned int ultimo;
    unsigned int tx, rx, etx, erx, persi;
    const char *t;
    char x[EXTB_VOCE_TESTO];

    if (ultimo && uptime_ms() - ultimo < 1000) return;
    ultimo = uptime_ms();
    if (!ultimo) ultimo = 1;
    switch (g_st.stato) {
    case WIFI_STATO_SPENTA:    t = "WIFI off"; break;
    case WIFI_STATO_CERCA:     t = "WIFI scanning"; break;
    case WIFI_STATO_AGGANCIA:
    case WIFI_STATO_CHIAVI:    t = "WIFI connecting"; break;
    case WIFI_STATO_CONNESSA:  t = "WIFI Connected"; break;
    case WIFI_STATO_CADUTA:    t = "WIFI lost"; break;
    default:                   t = "WIFI none"; break;
    }
    extaskbar_voce("WIFI", 15, t, g_st.stato == WIFI_STATO_CADUTA);
    ar5k_conn_contatori(&tx, &rx, &etx, &erx, &persi);
    snprintf(x, sizeof(x), "RX %08u", rx % 100000000u);       extaskbar_voce("RX", 12, x, 0);
    snprintf(x, sizeof(x), "TX %08u", tx % 100000000u);       extaskbar_voce("TX", 12, x, 0);
    snprintf(x, sizeof(x), "FAIL %06u", etx % 1000000u);      extaskbar_voce("FAIL", 11, x, etx != 0);
    snprintf(x, sizeof(x), "LOST %06u", (erx + persi) % 1000000u); extaskbar_voce("LOST", 11, x, 0);
}

/* a frame from the radio to whoever waits for one - never pushed unasked
 * (net_proto.h) */
static void consegna(void)
{
    static unsigned char f[1514];
    int n;

    if (!g_lettore) return;
    n = ar5k_conn_prendi(f);
    if (n <= 0) return;
    ipc_send((unsigned int)g_lettore, NET_MSG_FRAME, f, (unsigned int)n);
    g_lettore = 0;
}

int ar5k_servi(volatile void *registri, unsigned int sotto_id, const unsigned char mac[6])
{
    static WifiRete reti[RETI];
    static unsigned char buf[IPC_MSG_MAX_DATA];
    IpcMessage    meta;
    DmaZona       z;
    int           n, i, n_reti = 0;

    memset(&g_st, 0, sizeof(g_st));
    g_st.stato = WIFI_STATO_FERMA;
    g_registri = registri;
    g_sotto    = sotto_id;
    strcpy(g_st.scheda, "Atheros AR2425 (ar5k)");

    z.byte = 4096 + RX_N * RX_BUF;
    z.virt = 0; z.fisico = 0;
    if (dma_alloc(&z) != 0) { printf("ar5k: dma_alloc(%u) rifiutata\n", z.byte); return 1; }

    g_ec = ar5k_ec_prendi(sotto_id);
    if (g_ec) {
        n = ar5k_ec_svuota();
        if (!ar5k_ec_radio_accesa() && ar5k_ec_accendi() != 0)
            printf("ar5k: non riesco ad accendere la radio dall'EC\n");
        aggiorna_radio();
        printf("ar5k: radio %s (%d eventi vecchi dell'EC scartati); servo il tasto WiFi\n",
               g_st.stato == WIFI_STATO_SPENTA ? "spenta" : "accesa", n);
    }
    if (ipc_register(WIFI_SERVIZIO_0) < 0) {
        printf("ar5k: non riesco a registrare '%s' (c'e' gia' un'altra scheda?)\n", WIFI_SERVIZIO_0);
        return 1;
    }
    printf("ar5k: servizio '%s' pronto (reti aperte, WPA e WPA2 con password).\n",
           WIFI_SERVIZIO_0);

    for (;;) {
        /* connected: the ring is looked at every tick */
        if (ipc_recv_timeout(&meta, buf, sizeof(buf), ar5k_conn_attiva() ? 10 : 100) < 0) {
            if (g_ec && ar5k_ec_servi()) aggiorna_radio();
            if (ar5k_conn_lavora()) { g_caduta = 1; aggiorna_radio(); }
            consegna();
            voci_barra();
            continue;
        }
        switch (meta.tipo) {
        case WIFI_MSG_CERCA:
            aggiorna_radio();
            if (!g_spenta && !ar5k_conn_attiva()) {
                g_st.stato = WIFI_STATO_CERCA;
                n_reti = ar5k_cerca(registri, sotto_id, &z, reti, RETI);
                aggiorna_radio();               /* ar5k_chiudi put the BIOS's GPIOs back */
            }
            if (g_spenta) n_reti = 0;
            for (i = 0; i < n_reti; i++) {
                if (ar5k_conn_attiva() && strcmp(reti[i].ssid, g_st.ssid) == 0) {
                    reti[i].potenza = g_st.potenza;
                    reti[i].canale  = g_st.canale;
                }
                ipc_send(meta.sender_pid, WIFI_MSG_RETI, &reti[i], sizeof(reti[i]));
            }
            ipc_send(meta.sender_pid, WIFI_MSG_FINE, NULL, 0);
            break;
        case WIFI_MSG_CONNETTI: {
            WifiConnetti c;
            char perche[96];
            int  r;

            memset(&c, 0, sizeof(c));
            memcpy(&c, buf, meta.len < sizeof(c) ? meta.len : sizeof(c));
            if (meta.len < sizeof(c)) { c.auth = -1; c.nascosta = 0; c.ha_psk = 0; }
            c.ssid[WIFI_SSID_MAX - 1] = 0;
            c.parola[sizeof(c.parola) - 1] = 0;
            memset(buf, 0, sizeof(buf));                /* the password goes at once */
            aggiorna_radio();
            if (g_spenta) {
                memset(&c, 0, sizeof(c));
                esito((int)meta.sender_pid, WIFI_ERR_AUTH, "la radio e' spenta: premi il tasto WiFi");
                break;
            }
            g_st.stato = WIFI_STATO_AGGANCIA;
            perche[0] = 0;
            r = ar5k_conn_collega(registri, sotto_id, &c, perche, sizeof(perche));
            memset(&c, 0, sizeof(c));
            g_caduta = 0;
            aggiorna_radio();
            esito((int)meta.sender_pid, r, r ? perche : NULL);
            break;
        }
        case WIFI_MSG_STATO:
            aggiorna_radio();
            ipc_send(meta.sender_pid, WIFI_MSG_STATO_R, &g_st, sizeof(g_st));
            break;
        case WIFI_MSG_STACCA:
            ar5k_conn_stacca();
            g_caduta = 0;
            aggiorna_radio();
            esito((int)meta.sender_pid, 0, NULL);
            break;

        /* --- the link side of wifi0 ------------------------------------------------ */
        case NET_MSG_INFO: {
            NetStato s;

            memset(&s, 0, sizeof(s));
            memcpy(s.mac, mac, 6);
            s.mtu = 1500;
            s.bus = 0xFFFFFFFFu;
            strcpy(s.modello, "Atheros AR2425 (ar5k)");
            ipc_send(meta.sender_pid, NET_MSG_STATO, &s, sizeof(s));
            break;
        }
        case NET_MSG_INVIA: {
            NetEsito e;

            e.codice = ar5k_conn_invia(buf, (int)meta.len);
            ipc_send(meta.sender_pid, NET_MSG_ESITO, &e, sizeof(e));
            break;
        }
        case NET_MSG_RICEVI:
            g_lettore = (int)meta.sender_pid;
            break;
        case NET_MSG_CONTATORI: {
            NetContatori c;

            memset(&c, 0, sizeof(c));
            ar5k_conn_contatori(&c.inviati, &c.ricevuti, &c.errori_tx, &c.errori_rx, &c.persi_coda);
            ipc_send(meta.sender_pid, NET_MSG_CONTEGGI, &c, sizeof(c));
            break;
        }
        case NET_MSG_ANNULLA:
            if (g_lettore == (int)meta.sender_pid) g_lettore = 0;
            break;
        default:
            break;
        }
        if (ar5k_conn_lavora()) { g_caduta = 1; aggiorna_radio(); }
        consegna();
        voci_barra();
    }
    return 0;
}
