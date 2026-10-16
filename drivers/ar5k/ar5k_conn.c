/* =============================================================================
 * drivers/ar5k/ar5k_conn.c — stages 3 and 4: join a network and carry frames
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * What the wifi0 service (ar5k_servizio.c) does between WIFI_MSG_CONNETTI
 * and "IP can start", and then for every frame:
 *
 *   1. find the SSID: channels 1-13, beacons and probe responses (a probe
 *      request with the SSID is sent first when the network is hidden); the
 *      strongest access point wins, or the BSSID asked for
 *   2. open-system authentication and association. The association carries
 *      our RSN element (WPA2) or WPA element (WPA), with the ciphers chosen
 *      from what the access point offers: CCMP if it has it, else TKIP; the
 *      group cipher is the access point's (CCMP or TKIP). PSK only.
 *   3. the 4-way and group key handshakes by lib/exwifi (WpaSupplicante).
 *      Encryption is done in software: the chip's key cache is not used.
 *   4. then data: Ethernet frames from ip.drv become 802.11 data frames
 *      (LLC/SNAP, ToDS, encrypted with the pairwise key) and back. Group key
 *      rekeys are answered as they come. Deauthentication, disassociation or
 *      five seconds without a frame from the access point drop the link.
 *
 * Frames go out through ath5k's descriptors and queue code (desc.c, qcu.c,
 * dma.c) on one data queue, one at a time: the next is sent once the chip
 * says the last is done. Management frames at 1 Mbit/s; data (EAPOL too)
 * with the multi-rate retry series 24, 12, 11, 1 Mbit/s (11, 5.5, 2, 1 for an
 * 802.11b access point).
 *
 * The receive ring follows ath5k's base.c: each descriptor is set up linked
 * to itself and appended to the tail, and the descriptor RXDP points at is
 * never taken (the hardware may still be writing it).
 *
 * Everything said is written to /root/ar5k-conn.txt only - nothing on the
 * console, which the service shares with the person typing - so it can be
 * read over telnet or with `cat` when wanted. The
 * password, the PMK and the keys are never printed nor written.
 * ============================================================================= */
#include "ar5k_hw.h"
#include "ath5k/reg.h"
#include "wifi_proto.h"
#include "exwifi.h"
#include "ar5k_conn.h"
#include <stdarg.h>

#define RX_N        32
#define TX_N        4
#define BUF         2400
#define IE_MAX      512
#define CODA_N      32              /* Ethernet frames waiting for ip.drv */
#define DURATA      314             /* SIFS + an ACK at 1 Mbit/s, microseconds */
#define PERSA_MS    5000            /* nothing from the access point: gone */
#define REGISTRO    "/root/ar5k-conn.txt"

/* ath5k's hardware rate codes (base.c's ath5k_rates) */
#define R1M     0x1b
#define R2M     0x1a
#define R5M     0x19
#define R11M    0x18
#define R12M    0x0a
#define R24M    0x09

#define SUITE_TKIP  2
#define SUITE_CCMP  4

typedef struct {
    /* the chip */
    Ar5kChip  chip;
    struct ath5k_hw *ah;
    volatile void *registri;
    unsigned int sotto;
    DmaZona   z;
    int       z_ok;
    struct ath5k_desc *rx, *tx;
    u32       rx_fis, tx_fis;
    u8       *rx_buf, *tx_buf;
    u32       rx_buf_fis, tx_buf_fis;
    int       rx_testa, rx_coda;
    int       coda_tx, tx_i, tx_ultimo;
    struct ieee80211_channel ch;
    u16       seq;
    int       ofdm;                 /* the access point has 802.11g rates */
    unsigned int velocita;          /* Mbit/s of the last frame acknowledged */

    /* the network */
    char      ssid[33];
    int       ssid_n;
    u8        bssid[6];
    int       canale, dbm, trovata;
    u16       cap;
    u8        ie[IE_MAX];           /* the access point's elements */
    int       ie_n;
    int       auth;                 /* WIFI_AUTH_* */

    /* the joining */
    int       auth_fatta, assoc_fatta;
    int       stato_risposta;       /* status code of the last answer */
    u16       aid;
    int       caduta;               /* deauthenticated, disassociated or silent */
    unsigned int visto_ms;          /* last frame from the access point */

    /* the keys */
    int       cif_coppia, cif_gruppo;   /* SUITE_*, 0 = open network */
    WpaSupplicante wpa;
    int       mic_sbagliato;
    u8        tk[32];
    int       tk_ok;
    unsigned long long pn_tx;
    unsigned long long pn_rx[17];   /* per TID, [16] for frames without QoS */
    u8        gtk[4][32];
    int       gtk_ok[4];
    unsigned long long gpn_rx[4];
    unsigned int michael_ms;        /* the last Michael failure (TKIP) */
    int       michael_n;

    /* frames for ip.drv */
    u8        coda[CODA_N][1514];
    int       coda_n_byte[CODA_N];
    int       coda_testa, coda_n;

    int       attiva;
    unsigned int byte_in, byte_out;

    /* what happened to the frames, for /root/ar5k-conn.txt (every 10 s and
     * at the end) and NET_MSG_CONTATORI */
    struct {
        unsigned int dati_ap, cifrati, per_noi, gruppo, mal_cifrati, ripetuti, senza_chiave,
                     non_llc, in_chiaro_scartati, consegnati, accodati, persi_coda,
                     inviati, invii_falliti, beacon, danneggiati;
    } c;
    int       diario;               /* data frames still to describe one by one */
    unsigned int conto_ms;
} Conn;

static Conn  g_k;
static FILE *g_reg;
int          g_eco;                 /* tools/prova-ar5k: everything on the screen */

static void dire(const char *fmt, ...)
{
    char    t[256];
    va_list a;

    va_start(a, fmt);
    vsnprintf(t, sizeof(t), fmt, a);
    va_end(a);
    /* the console only when there is no file: the frame log and the counters
     * made the command line unusable (the user, 16 October 2026) */
    if (g_reg) { fputs(t, g_reg); fflush(g_reg); }
    else if (g_eco) printf("ar5k: %s", t);
}

static int vale_zero(const u8 *p, int n)
{
    while (n--) if (*p++) return 0;
    return 1;
}

/* --- the receive ring ------------------------------------------------------------ */
static u32 rx_ind(Conn *k, int i) { return k->rx_fis + (u32)(i * (int)sizeof(struct ath5k_desc)); }
static u32 tx_ind(Conn *k, int i) { return k->tx_fis + (u32)(i * (int)sizeof(struct ath5k_desc)); }

static void rx_prepara(Conn *k)
{
    struct ath5k_hw *ah = k->ah;
    int i;

    for (i = 0; i < RX_N; i++) {
        memset(&k->rx[i], 0, sizeof(k->rx[i]));
        k->rx[i].ds_data = k->rx_buf_fis + (u32)(i * BUF);
        k->rx[i].ds_link = rx_ind(k, i + 1 < RX_N ? i + 1 : i);   /* the last to itself */
        ath5k_hw_setup_rx_desc(ah, &k->rx[i], BUF, 0);
    }
    k->rx_testa = 0;
    k->rx_coda  = RX_N - 1;
    ath5k_hw_set_rxdp(ah, rx_ind(k, 0));
    ath5k_hw_start_rx_dma(ah);
    ath5k_hw_set_rx_filter(ah, AR5K_RX_FILTER_UCAST | AR5K_RX_FILTER_MCAST |
                               AR5K_RX_FILTER_BCAST | AR5K_RX_FILTER_BEACON);
    ath5k_hw_set_mcast_filter(ah, 0xffffffff, 0xffffffff);
    ath5k_hw_start_rx_pcu(ah);
}

/* the next received frame, without its FCS: its length, 0 for none yet,
 * -1 for a damaged one (skipped) */
static int rx_prendi(Conn *k, u8 *out, int max, int *dbm)
{
    struct ath5k_hw *ah = k->ah;
    struct ath5k_desc *d = &k->rx[k->rx_testa];
    struct ath5k_rx_status rs;
    int n, i = k->rx_testa;

    if (ath5k_hw_get_rxdp(ah) == rx_ind(k, i)) return 0;      /* still the hardware's */
    memset(&rs, 0, sizeof(rs));
    if (ah->ah_proc_rx_desc(ah, d, &rs) == -EINPROGRESS) return 0;
    /* no key is in the chip's cache: a protected frame comes as it was sent,
     * and the chip may flag it as not decrypted */
    if ((rs.rs_status & ~(AR5K_RXERR_DECRYPT | AR5K_RXERR_MIC)) == 0 && rs.rs_datalen > 4 + 10) {
        n = rs.rs_datalen - 4;
        if (n > max) n = max;
        memcpy(out, k->rx_buf + i * BUF, (size_t)n);
        if (dbm) *dbm = rs.rs_rssi + ah->ah_noise_floor;
    } else {
        n = -1;
    }
    /* back to the tail, linked to itself */
    d->ds_link = rx_ind(k, i);
    ath5k_hw_setup_rx_desc(ah, d, BUF, 0);
    k->rx[k->rx_coda].ds_link = rx_ind(k, i);
    k->rx_coda  = i;
    k->rx_testa = (i + 1) % RX_N;
    return n;
}

/* --- sending ----------------------------------------------------------------------- */
static int tx_coda(Conn *k)
{
    struct ath5k_txq_info qi;

    memset(&qi, 0, sizeof(qi));
    qi.tqi_subtype = AR5K_WME_AC_BE;
    qi.tqi_aifs    = AR5K_TUNE_AIFS;
    qi.tqi_cw_min  = AR5K_TUNE_CWMIN;
    qi.tqi_cw_max  = AR5K_TUNE_CWMAX;
    qi.tqi_flags   = AR5K_TXQ_FLAG_TXEOLINT_ENABLE | AR5K_TXQ_FLAG_TXDESCINT_ENABLE;
    k->coda_tx = ath5k_hw_setup_tx_queue(k->ah, AR5K_TX_QUEUE_DATA, &qi);
    return k->coda_tx < 0 ? k->coda_tx : 0;
}

/* One frame (802.11 header on, no FCS). dati: the multi-rate series of data
 * frames, else 1 Mbit/s. Returns 0 when acknowledged - or sent, for a group
 * address - -EIO when the retries ran out, -ETIMEDOUT when the chip never
 * said. */
static int tx_manda(Conn *k, const u8 *f, int n, int dati)
{
    static const unsigned int MBIT_G[4] = { 24, 12, 11, 1 }, MBIT_B[4] = { 11, 5, 2, 1 };
    struct ath5k_hw *ah = k->ah;
    struct ath5k_desc *d;
    struct ath5k_tx_status ts;
    int i = k->tx_i, gruppo = f[4] & 1, r;
    unsigned int inizio, flag = AR5K_TXDESC_INTREQ | AR5K_TXDESC_CLRDMASK;

    if (n <= 0 || n > BUF - 4) return -EINVAL;
    d = &k->tx[i];
    memcpy(k->tx_buf + i * BUF, f, (size_t)n);
    memset(d, 0, sizeof(*d));
    d->ds_data = k->tx_buf_fis + (u32)(i * BUF);
    d->ds_link = 0;
    if (gruppo) flag |= AR5K_TXDESC_NOACK;
    if (dati && !gruppo) {
        r = ah->ah_setup_tx_desc(ah, d, (unsigned)n, 24, 0, AR5K_PKT_TYPE_NORMAL, AR5K_TUNE_MAX_TXPOWER,
                                 k->ofdm ? R24M : R11M, 2, AR5K_TXKEYIX_INVALID, ah->ah_tx_ant,
                                 flag, 0, 0);
        if (r == 0)
            ath5k_hw_setup_mrr_tx_desc(ah, d, k->ofdm ? R12M : R5M, 2, k->ofdm ? R11M : R2M, 2, R1M, 3);
    } else {
        r = ah->ah_setup_tx_desc(ah, d, (unsigned)n, 24, 0, AR5K_PKT_TYPE_NORMAL, AR5K_TUNE_MAX_TXPOWER,
                                 R1M, gruppo ? 1 : 8, AR5K_TXKEYIX_INVALID, ah->ah_tx_ant,
                                 flag, 0, 0);
    }
    if (r != 0) { dire("descrittore di invio rifiutato (%d)\n", r); return -EINVAL; }

    /* the first frame after a reset is pointed at; later ones are linked to
     * the last while the queue is still enabled (TXDP cannot be written then),
 * as base.c does - TXE set again makes the queue read on */
    if (ath5k_hw_set_txdp(ah, (unsigned)k->coda_tx, tx_ind(k, i)) != 0) {
        if (k->tx_ultimo < 0) { dire("la coda di invio e' ancora attiva dopo il reset\n"); return -EIO; }
        k->tx[k->tx_ultimo].ds_link = tx_ind(k, i);
    }
    ath5k_hw_start_tx_dma(ah, (unsigned)k->coda_tx);
    k->tx_ultimo = i;
    k->tx_i = (i + 1) % TX_N;

    for (inizio = uptime_ms(); uptime_ms() - inizio < 200; ) {
        memset(&ts, 0, sizeof(ts));
        if (ah->ah_proc_tx_desc(ah, d, &ts) != -EINPROGRESS) {
            if (ts.ts_status & AR5K_TXERR_XRETRY) return -EIO;
            if (ts.ts_status) return -EIO;
            if (dati && !gruppo && ts.ts_final_idx < 4)
                k->velocita = k->ofdm ? MBIT_G[ts.ts_final_idx] : MBIT_B[ts.ts_final_idx];
            return 0;
        }
        sched_yield();
    }
    return -ETIMEDOUT;
}

static void testa_80211(Conn *k, u8 *f, int fc0, int fc1, const u8 *a1, const u8 *a2, const u8 *a3)
{
    f[0] = (u8)fc0;
    f[1] = (u8)fc1;
    f[2] = (a1[0] & 1) ? 0 : (u8)(DURATA & 0xff);
    f[3] = (a1[0] & 1) ? 0 : (u8)(DURATA >> 8);
    memcpy(f + 4, a1, 6);
    memcpy(f + 10, a2, 6);
    memcpy(f + 16, a3, 6);
    f[22] = (u8)(k->seq << 4);
    f[23] = (u8)(k->seq >> 4);
    k->seq = (u16)((k->seq + 1) & 0xfff);
}

/* --- the access point's elements ------------------------------------------------------ */
static const u8 *elemento(const u8 *ie, int n, int id, const u8 *oui4)
{
    const u8 *p = ie, *fine = ie + n;

    while (p + 2 <= fine && p + 2 + p[1] <= fine) {
        if (p[0] == id && (!oui4 || (p[1] >= 4 && memcmp(p + 2, oui4, 4) == 0))) return p;
        p += 2 + p[1];
    }
    return NULL;
}

static const u8 OUI_WPA[4] = { 0x00, 0x50, 0xf2, 0x01 };

/* RSN or WPA element of the access point: the group cipher, the pairwise one
 * we take (CCMP before TKIP), and whether PSK is offered. 0, or why not. */
static const char *leggi_sicurezza(const u8 *e, int rsn, int *gruppo, int *coppia, int *mfp)
{
    const u8 *p = e + 2, *fine = e + 2 + e[1];
    const u8 *oui = rsn ? (const u8 *)"\x00\x0f\xac" : (const u8 *)"\x00\x50\xf2";
    int n, i, ccmp = 0, tkip = 0, psk = 0;

    if (!rsn) p += 4;                                      /* OUI and type */
    *gruppo = *coppia = 0; *mfp = 0;
    if (p + 2 > fine || p[0] != 1 || p[1] != 0) return "versione dell'elemento di sicurezza sconosciuta";
    p += 2;
    if (p + 4 > fine) { *gruppo = rsn ? SUITE_CCMP : SUITE_TKIP; *coppia = *gruppo; return NULL; }
    if (memcmp(p, oui, 3) != 0) return "cifrario di gruppo sconosciuto";
    *gruppo = p[3];
    p += 4;
    if (p + 2 > fine) { *coppia = *gruppo; return NULL; }
    n = p[0] | (p[1] << 8);
    p += 2;
    for (i = 0; i < n && p + 4 <= fine; i++, p += 4)
        if (memcmp(p, oui, 3) == 0) {
            if (p[3] == SUITE_CCMP) ccmp = 1;
            if (p[3] == SUITE_TKIP) tkip = 1;
        }
    if (p + 2 > fine) psk = 1;                             /* default AKM: 802.1X... */
    else {
        n = p[0] | (p[1] << 8);
        p += 2;
        psk = 0;
        for (i = 0; i < n && p + 4 <= fine; i++, p += 4)
            if (memcmp(p, oui, 3) == 0 && p[3] == 2) psk = 1;
        if (rsn && p + 2 <= fine && (p[0] & 0x40)) *mfp = 1;   /* MFP required */
    }
    if (*gruppo != SUITE_CCMP && *gruppo != SUITE_TKIP) return "cifrario di gruppo non gestito (WEP?)";
    if (!psk) return "la rete non offre la chiave condivisa (PSK): forse e' Enterprise o WPA3";
    *coppia = ccmp ? SUITE_CCMP : tkip ? SUITE_TKIP : 0;
    if (!*coppia) return "nessun cifrario per coppia gestito";
    return NULL;
}

/* our element for the association request, and for message 2 */
static int nostro_elemento(Conn *k, int rsn, u8 *out)
{
    const u8 *oui = rsn ? (const u8 *)"\x00\x0f\xac" : (const u8 *)"\x00\x50\xf2";
    u8 *p = out;

    *p++ = rsn ? 48 : 221;
    p++;                                                   /* the length, below */
    if (!rsn) { memcpy(p, OUI_WPA, 4); p += 4; }
    *p++ = 1; *p++ = 0;                                    /* version 1 */
    memcpy(p, oui, 3); p[3] = (u8)k->cif_gruppo; p += 4;
    *p++ = 1; *p++ = 0;
    memcpy(p, oui, 3); p[3] = (u8)k->cif_coppia; p += 4;
    *p++ = 1; *p++ = 0;
    memcpy(p, oui, 3); p[3] = 2; p += 4;                   /* PSK */
    if (rsn) { *p++ = 0; *p++ = 0; }                       /* capabilities */
    out[1] = (u8)(p - out - 2);
    return (int)(p - out);
}

/* --- the frames that arrive ---------------------------------------------------------- */
static void accoda(Conn *k, const u8 *da, const u8 *sa, const u8 *tipo, const u8 *dati, int n)
{
    int i;

    if (14 + n > 1514) return;
    if (k->coda_n == CODA_N) {                             /* full: the oldest goes */
        k->coda_testa = (k->coda_testa + 1) % CODA_N;
        k->coda_n--;
        k->c.persi_coda++;
    }
    k->c.accodati++;
    i = (k->coda_testa + k->coda_n) % CODA_N;
    memcpy(k->coda[i], da, 6);
    memcpy(k->coda[i] + 6, sa, 6);
    memcpy(k->coda[i] + 12, tipo, 2);
    memcpy(k->coda[i] + 14, dati, (size_t)n);
    k->coda_n_byte[i] = 14 + n;
    k->coda_n++;
    k->byte_in += (unsigned)(14 + n);
}

static int manda_dati(Conn *k, const u8 *da, const u8 *tipo, const u8 *dati, int n);
static void eapol(Conn *k, const u8 *e, int n);

static void contromisure(Conn *k)
{
    unsigned int ora = uptime_ms();

    dire("TKIP: controllo Michael sbagliato (frame alterato?)\n");
    if (k->michael_n && ora - k->michael_ms < 60000) {
        dire("TKIP: due errori Michael in 60 s - stacco la rete, come vuole lo standard\n");
        k->caduta = 1;
    }
    k->michael_ms = ora;
    k->michael_n = 1;
}

static void dati_ricevuti(Conn *k, u8 *f, int n)
{
    static u8 chiaro[BUF];
    const u8 *a1 = f + 4, *a2 = f + 10, *a3 = f + 16, *p;
    int fc0 = f[0], fc1 = f[1], hdr = 24, tid = 16, m, gruppo = a1[0] & 1;
    unsigned long long pn = 0;

    if ((fc1 & 3) != 2 || memcmp(a2, k->bssid, 6) != 0) return;   /* only FromDS, our AP */
    if (fc0 & 0x40) return;                                        /* null data: no body */
    if (fc0 & 0x80) {                                              /* QoS */
        tid = f[24] & 15;
        hdr = 26;
        if (fc1 & 0x80) hdr += 4;                                  /* HT control */
    }
    if (n <= hdr) return;
    if (!memcmp(a3, k->chip.mac, 6)) return;                       /* our own, sent back */
    k->c.dati_ap++;
    if (gruppo) k->c.gruppo++; else k->c.per_noi++;
    if (fc1 & 0x40) k->c.cifrati++;

    if (fc1 & 0x40) {                                              /* protected */
        const u8 *corpo = f + hdr;
        int cifr = gruppo ? k->cif_gruppo : k->cif_coppia;
        int idx = corpo[3] >> 6;
        const u8 *tk = gruppo ? k->gtk[idx] : k->tk;

        if (n - hdr < 8 + 8) return;
        if (gruppo ? !k->gtk_ok[idx] : !k->tk_ok) {
            k->c.senza_chiave++;
            if (k->diario > 0) { k->diario--; dire("  rx %s chiave %d: non l'abbiamo\n", gruppo ? "gruppo" : "coppia", idx); }
            return;
        }
        if (cifr == SUITE_CCMP)
            m = ccmp_decifra(tk, f, (unsigned)hdr, corpo, (unsigned)(n - hdr), chiaro, &pn);
        else if (cifr == SUITE_TKIP) {
            m = tkip_decifra(tk, tk + 16, a2, a1, a3, tid == 16 ? 0 : tid,
                             corpo, (unsigned)(n - hdr), chiaro, &pn);
            if (m == -2) { contromisure(k); return; }
        } else return;
        if (m < 0) {
            k->c.mal_cifrati++;
            if (k->diario > 0) {
                k->diario--;
                dire("  rx %s chiave %d %s, %d byte: NON si decifra (testa %02x %02x %02x %02x)\n",
                     gruppo ? "gruppo" : "coppia", idx, cifr == SUITE_CCMP ? "CCMP" : "TKIP", n - hdr,
                     corpo[0], corpo[1], corpo[2], corpo[3]);
            }
            return;
        }
        if (gruppo) {
            if (pn <= k->gpn_rx[idx]) { k->c.ripetuti++; return; }    /* replayed */
            k->gpn_rx[idx] = pn;
        } else {
            if (pn <= k->pn_rx[tid]) { k->c.ripetuti++; return; }
            k->pn_rx[tid] = pn;
        }
        p = chiaro;
    } else {
        p = f + hdr;
        m = n - hdr;
    }
    if (m < 8 || p[0] != 0xaa || p[1] != 0xaa || p[2] != 0x03) { k->c.non_llc++; return; }   /* LLC/SNAP */
    if (p[6] == 0x88 && p[7] == 0x8e) { eapol(k, p + 8, m - 8); return; }
    if (!(fc1 & 0x40) && k->cif_coppia) { k->c.in_chiaro_scartati++; return; }   /* clear data, protected network */
    if (k->diario > 0) {
        k->diario--;
        dire("  rx %s%s tipo %02x%02x, %d byte da %02x:%02x:%02x:%02x:%02x:%02x\n", gruppo ? "gruppo" : "coppia",
             (fc1 & 0x40) ? "" : " in chiaro", p[6], p[7], m - 8, a3[0], a3[1], a3[2], a3[3], a3[4], a3[5]);
    }
    accoda(k, a1, a3, p + 6, p + 8, m - 8);
}

static void gestione_ricevuta(Conn *k, const u8 *f, int n)
{
    int sotto = f[0] >> 4;
    const u8 *b = f + 24;

    if (memcmp(f + 10, k->bssid, 6) != 0) return;
    if (memcmp(f + 4, k->chip.mac, 6) != 0 && !((f[4] & 1) && (sotto == 10 || sotto == 12))) return;
    switch (sotto) {
    case 11:                                               /* authentication */
        if (n >= 30 && b[0] == 0 && b[1] == 0 && b[2] == 2) {
            k->stato_risposta = b[4] | (b[5] << 8);
            k->auth_fatta = k->stato_risposta == 0 ? 1 : -1;
        }
        break;
    case 1: case 3:                                        /* (re)association response */
        if (n >= 30) {
            k->stato_risposta = b[2] | (b[3] << 8);
            k->aid = (u16)((b[4] | (b[5] << 8)) & 0x3fff);
            k->assoc_fatta = k->stato_risposta == 0 ? 1 : -1;
        }
        break;
    case 10: case 12:                                      /* disassociation, deauthentication */
        if (n >= 26) {
            dire("l'access point ci ha %s (motivo %d)\n", sotto == 12 ? "tolto l'autenticazione" : "disassociati",
                 b[0] | (b[1] << 8));
            k->caduta = 1;
            k->auth_fatta = k->assoc_fatta = -1;
            k->stato_risposta = -1;
        }
        break;
    default:
        break;
    }
}

/* drain the ring: 802.11 frames to their handlers */
static void pompa(Conn *k)
{
    static u8 f[BUF];
    int n, dbm = 0, giri;

    for (giri = 0; giri < RX_N; giri++) {
        n = rx_prendi(k, f, sizeof(f), &dbm);
        if (n == 0) break;
        if (n < 0) { k->c.danneggiati++; continue; }
        if (n < 24) continue;
        if (memcmp(f + 10, k->bssid, 6) == 0) {
            k->visto_ms = uptime_ms();
            if ((f[0] & 0xfc) == 0x80) { k->dbm = dbm; k->c.beacon++; }   /* beacons give the signal */
        }
        switch ((f[0] >> 2) & 3) {
        case 0: gestione_ricevuta(k, f, n); break;
        case 2: if (memcmp(f + 4, k->chip.mac, 6) == 0 || (f[4] & 1)) dati_ricevuti(k, f, n); break;
        default: break;
        }
    }
}

/* --- EAPOL: the key handshakes ----------------------------------------------------------- */
static const u8 TIPO_EAPOL[2] = { 0x88, 0x8e };

static void eapol(Conn *k, const u8 *e, int n)
{
    static u8 risposta[EAPOL_MAX];
    unsigned int rn = 0;
    int r, i;

    if (!k->cif_coppia) return;
    r = wpa_ricevi(&k->wpa, e, (unsigned)n, risposta, &rn);
    if (r < 0) {
        static const char *PERCHE[] = { "", "frame malformato", "MIC sbagliato: la password non e' quella",
                                        "frame ripetuto (replay)", "ANonce diverso dal messaggio 1",
                                        "la chiave di gruppo non si decifra" };
        dire("EAPOL rifiutato: %s\n", -r < 6 ? PERCHE[-r] : "?");
        if (r == WPA_ERR_MIC) k->mic_sbagliato = 1;
        return;
    }
    if (r == WPA_NIENTE) return;
    if (manda_dati(k, k->bssid, TIPO_EAPOL, risposta, (int)rn) != 0)
        dire("EAPOL: la risposta non e' partita\n");
    if (r == WPA_RISPONDI) dire("4-way: messaggio 1 ricevuto, 2 inviato\n");
    if (r == WPA_COPPIA) {
        memcpy(k->tk, k->wpa.ptk.tk, sizeof(k->tk));
        k->tk_ok = 1;
        k->pn_tx = 0;
        for (i = 0; i < 17; i++) k->pn_rx[i] = 0;
        dire("4-way: messaggio 3 ricevuto, 4 inviato - chiave per coppia installata\n");
    }
    if ((r == WPA_COPPIA || r == WPA_GRUPPO) && k->wpa.gtk_pronta) {
        int x = k->wpa.gtk_indice & 3;
        unsigned long long rsc = 0;

        for (i = 5; i >= 0; i--) rsc = (rsc << 8) | k->wpa.gtk_rsc[i];
        memcpy(k->gtk[x], k->wpa.gtk, k->wpa.gtk_n < 32 ? k->wpa.gtk_n : 32);
        k->gtk_ok[x] = 1;
        k->gpn_rx[x] = rsc ? rsc - 1 : 0;
        k->wpa.gtk_pronta = 0;
        dire("chiave di gruppo %d installata\n", x);
    }
}

/* Ethernet payload to the access point as a data frame, encrypted once the
 * pairwise key is in */
static int manda_dati(Conn *k, const u8 *da, const u8 *tipo, const u8 *dati, int n)
{
    static u8 f[BUF], chiaro[BUF];
    int m, cifra = k->tk_ok;

    if (n < 0 || n > 1500) return -EMSGSIZE;
    testa_80211(k, f, 0x08, 0x01 | (cifra ? 0x40 : 0), k->bssid, k->chip.mac, da);
    chiaro[0] = 0xaa; chiaro[1] = 0xaa; chiaro[2] = 0x03;
    chiaro[3] = chiaro[4] = chiaro[5] = 0;
    memcpy(chiaro + 6, tipo, 2);
    memcpy(chiaro + 8, dati, (size_t)n);
    m = 8 + n;
    if (cifra && k->cif_coppia == SUITE_CCMP)
        m = ccmp_cifra(k->tk, f, 24, ++k->pn_tx, 0, chiaro, (unsigned)m, f + 24);
    else if (cifra && k->cif_coppia == SUITE_TKIP)
        m = tkip_cifra(k->tk, k->tk + 24, k->chip.mac, da, k->chip.mac, 0, ++k->pn_tx, 0,
                       chiaro, (unsigned)m, f + 24);
    else
        memcpy(f + 24, chiaro, (size_t)m);
    return tx_manda(k, f, 24 + m, 1);
}

/* --- the chip ------------------------------------------------------------------------- */
static int sul_canale(Conn *k, int canale)
{
    int r;

    ar5k_canale_g(&k->ch, canale);
    k->ah->ah_current_channel = &k->ch;
    r = ath5k_hw_reset(k->ah, NL80211_IFTYPE_STATION, &k->ch, false, false);
    if (r != 0) { dire("canale %d: ath5k_hw_reset = %d\n", canale, r); return r; }
    k->tx_ultimo = -1;
    k->tx_i = 0;
    rx_prepara(k);
    return 0;
}

static void chip_chiudi(Conn *k)
{
    if (!k->ah) return;
    ath5k_hw_stop_rx_pcu(k->ah);
    ath5k_hw_dma_stop(k->ah);
    ar5k_chiudi(&k->chip);
    free(k->chip.ah);
    k->ah = NULL;
    k->chip.ah = NULL;
}

/* Linux's ath/key.c ath_hw_keyreset, for every entry of the key cache: the
 * cache is not cleared by power-on nor by reset, and an entry left valid
 * makes the chip "decrypt" protected frames with whatever it holds (a frame
 * whose sender has no entry takes the default slot of its key index) - the
 * frames then reach the software CCMP mangled. Linux clears it at start for
 * the same reason, and with every entry "clear" protected frames come as
 * they were sent. */
#define KC(n)       (0x8800u + (u32)(n) * 32u)
#define KC_N        128
#define KC_TIPO_NO  7u

static void chiavi_azzera(struct ath5k_hw *ah)
{
    int i;

    for (i = 0; i < KC_N; i++) {
        ath5k_hw_reg_write(ah, 0, KC(i) + 0x00);
        ath5k_hw_reg_write(ah, 0, KC(i) + 0x04);
        ath5k_hw_reg_write(ah, 0, KC(i) + 0x08);
        ath5k_hw_reg_write(ah, 0, KC(i) + 0x0c);
        ath5k_hw_reg_write(ah, 0, KC(i) + 0x10);
        ath5k_hw_reg_write(ah, KC_TIPO_NO, KC(i) + 0x14);
        ath5k_hw_reg_write(ah, 0, KC(i) + 0x18);
        ath5k_hw_reg_write(ah, 0, KC(i) + 0x1c);
    }
}

static int chip_apri(Conn *k)
{
    if (!k->z_ok) {
        k->z.byte = 4096 + (RX_N + TX_N) * BUF;
        k->z.virt = 0; k->z.fisico = 0;
        if (dma_alloc(&k->z) != 0) { dire("dma_alloc(%u) rifiutata\n", k->z.byte); return -ENOMEM; }
        k->z_ok = 1;
    }
    k->rx         = (struct ath5k_desc *)(uintptr_t)k->z.virt;
    k->rx_fis     = k->z.fisico;
    k->tx         = k->rx + RX_N;
    k->tx_fis     = k->rx_fis + RX_N * sizeof(struct ath5k_desc);
    k->rx_buf     = (u8 *)(uintptr_t)k->z.virt + 4096;
    k->rx_buf_fis = k->z.fisico + 4096;
    k->tx_buf     = k->rx_buf + RX_N * BUF;
    k->tx_buf_fis = k->rx_buf_fis + RX_N * BUF;

    if (ar5k_apri(&k->chip, k->registri, k->sotto) != 0) { chip_chiudi(k); return -EIO; }
    k->ah = k->chip.ah;
    chiavi_azzera(k->ah);
    if (tx_coda(k) != 0) { dire("coda di invio rifiutata (%d)\n", k->coda_tx); chip_chiudi(k); return -EIO; }
    return 0;
}

/* --- 1. find the network --------------------------------------------------------------- */
static void candidato(Conn *k, const u8 *f, int n, int canale_rx, int dbm, const u8 *bssid_voluto)
{
    const u8 *ie = f + 36, *e;
    int ie_n = n - 36, canale = canale_rx;

    if (n < 36 || (f[0] & 0x0c) != 0) return;
    if ((f[0] >> 4) != 8 && (f[0] >> 4) != 5) return;      /* beacon, probe response */
    e = elemento(ie, ie_n, 0, NULL);
    if (!e || e[1] != k->ssid_n || memcmp(e + 2, k->ssid, (size_t)k->ssid_n) != 0) return;
    if (!vale_zero(bssid_voluto, 6) && memcmp(f + 16, bssid_voluto, 6) != 0) return;
    e = elemento(ie, ie_n, 3, NULL);
    if (e && e[1] == 1) canale = e[2];
    if (canale != canale_rx) return;                       /* heard from the next channel */
    if (k->trovata && memcmp(f + 16, k->bssid, 6) != 0 && dbm <= k->dbm) return;
    if (k->trovata && memcmp(f + 16, k->bssid, 6) == 0 && k->ie_n && (f[0] >> 4) == 8 && dbm <= k->dbm)
        return;                                            /* same AP: keep the probe response */
    memcpy(k->bssid, f + 16, 6);
    k->canale = canale;
    k->dbm = dbm;
    k->cap = (u16)(f[34] | (f[35] << 8));
    k->ie_n = ie_n < IE_MAX ? ie_n : IE_MAX;
    memcpy(k->ie, ie, (size_t)k->ie_n);
    k->trovata = 1;
}

static void sonda(Conn *k)
{
    static const u8 TUTTI[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    static const u8 TASSI[] = { 1, 8, 0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18, 0x24,
                                50, 4, 0x30, 0x48, 0x60, 0x6c };
    u8 f[128];
    int n;

    testa_80211(k, f, 0x40, 0, TUTTI, k->chip.mac, TUTTI);
    n = 24;
    f[n++] = 0;
    f[n++] = (u8)k->ssid_n;
    memcpy(f + n, k->ssid, (size_t)k->ssid_n);
    n += k->ssid_n;
    memcpy(f + n, TASSI, sizeof(TASSI));
    n += (int)sizeof(TASSI);
    tx_manda(k, f, n, 0);
}

static int cerca(Conn *k, int nascosta, const u8 *bssid_voluto)
{
    static u8 f[BUF];
    int c, n, dbm = 0;
    unsigned int t;

    k->trovata = 0;
    memset(k->bssid, 0, 6);
    for (c = 1; c <= 13; c++) {
        memset(k->ah->common.curbssid, 0, ETH_ALEN);
        if (sul_canale(k, c) != 0) return -EIO;
        if (nascosta) sonda(k);
        for (t = uptime_ms(); uptime_ms() - t < 120; ) {
            n = rx_prendi(k, f, sizeof(f), &dbm);
            if (n == 0) { sched_yield(); continue; }
            if (n > 0) candidato(k, f, n, c, dbm, bssid_voluto);
        }
        ath5k_hw_stop_rx_pcu(k->ah);
        ath5k_hw_dma_stop(k->ah);
    }
    return k->trovata ? 0 : -1;
}

/* --- 2. authentication and association ---------------------------------------------------- */
static int aspetta(Conn *k, int *flag, unsigned int ms)
{
    unsigned int t;

    for (t = uptime_ms(); uptime_ms() - t < ms; ) {
        pompa(k);
        if (*flag) return *flag;
        sched_yield();
    }
    return 0;
}

static int autentica(Conn *k)
{
    u8 f[64];
    int prova, r;

    for (prova = 0; prova < 4; prova++) {
        testa_80211(k, f, 0xb0, 0, k->bssid, k->chip.mac, k->bssid);
        memset(f + 24, 0, 6);
        f[26] = 1;                                         /* open system, transaction 1 */
        k->auth_fatta = 0;
        r = tx_manda(k, f, 30, 0);
        if (r != 0) { dire("autenticazione: invio %d non confermato (%d)\n", prova + 1, r); continue; }
        if ((r = aspetta(k, &k->auth_fatta, 300)) == 1) return 0;
        if (r < 0) { dire("autenticazione rifiutata (stato %d)\n", k->stato_risposta); return -1; }
    }
    return -2;
}

static int associa(Conn *k, const u8 *nostro, int nostro_n)
{
    static u8 f[BUF];
    const u8 *e;
    int prova, r, n;
    u16 cap = (u16)((k->cap & 0x0431) | 0x0001);           /* ESS, privacy, short preamble/slot */

    for (prova = 0; prova < 3; prova++) {
        testa_80211(k, f, 0x00, 0, k->bssid, k->chip.mac, k->bssid);
        n = 24;
        f[n++] = (u8)cap; f[n++] = (u8)(cap >> 8);
        f[n++] = 10; f[n++] = 0;                           /* listen interval */
        f[n++] = 0; f[n++] = (u8)k->ssid_n;
        memcpy(f + n, k->ssid, (size_t)k->ssid_n); n += k->ssid_n;
        if ((e = elemento(k->ie, k->ie_n, 1, NULL)) != NULL) { memcpy(f + n, e, (size_t)(2 + e[1])); n += 2 + e[1]; }
        else { static const u8 B[] = { 1, 4, 0x82, 0x84, 0x8b, 0x96 }; memcpy(f + n, B, sizeof(B)); n += (int)sizeof(B); }
        if ((e = elemento(k->ie, k->ie_n, 50, NULL)) != NULL) { memcpy(f + n, e, (size_t)(2 + e[1])); n += 2 + e[1]; }
        if (nostro_n) { memcpy(f + n, nostro, (size_t)nostro_n); n += nostro_n; }
        k->assoc_fatta = 0;
        r = tx_manda(k, f, n, 0);
        if (r != 0) { dire("associazione: invio %d non confermato (%d)\n", prova + 1, r); continue; }
        if ((r = aspetta(k, &k->assoc_fatta, 500)) == 1) return 0;
        if (r < 0) { dire("associazione rifiutata (stato %d)\n", k->stato_risposta); return -1; }
    }
    return -2;
}

static void deautentica(Conn *k)
{
    u8 f[32];

    if (!k->ah || vale_zero(k->bssid, 6)) return;
    testa_80211(k, f, 0xc0, 0, k->bssid, k->chip.mac, k->bssid);
    f[24] = 3; f[25] = 0;                                  /* leaving */
    tx_manda(k, f, 26, 0);
}

/* =============================================================================
 * What the service calls
 * ============================================================================= */
void ar5k_conn_stacca(void)
{
    Conn *k = &g_k;

    if (k->attiva) {
        deautentica(k);
        dire("staccata da %s\n", k->ssid);
    }
    if (k->c.beacon || k->c.inviati) ar5k_conn_conta();
    memset(&k->c, 0, sizeof(k->c));
    chip_chiudi(k);
    k->attiva = 0;
    k->tk_ok = 0;
    memset(k->tk, 0, sizeof(k->tk));
    memset(k->gtk, 0, sizeof(k->gtk));
    memset(k->gtk_ok, 0, sizeof(k->gtk_ok));
    memset(&k->wpa, 0, sizeof(k->wpa));
    k->coda_n = 0;
    if (g_reg) { fclose(g_reg); g_reg = NULL; }
}

int ar5k_conn_collega(volatile void *registri, unsigned int sotto_id, const WifiConnetti *c,
                      char *perche, int perche_n)
{
    Conn *k = &g_k;
    u8 pmk[32], snonce[32], nostro[64];
    const u8 *rsn, *wpa, *e;
    const char *no = NULL;
    int nostro_n = 0, mfp = 0, r, i;
    unsigned int t;

#define ESCI(codice, ...) do { snprintf(perche, (size_t)perche_n, __VA_ARGS__); \
                               dire("%s\n", perche); r = (codice); goto fuori; } while (0)

    ar5k_conn_stacca();
    g_reg = fopen(REGISTRO, "w");
    k->registri = registri;
    k->sotto = sotto_id;
    for (k->ssid_n = 0; k->ssid_n < 32 && c->ssid[k->ssid_n]; k->ssid_n++) ;
    memcpy(k->ssid, c->ssid, (size_t)k->ssid_n);
    k->ssid[k->ssid_n] = 0;
    k->caduta = 0;
    k->mic_sbagliato = 0;
    k->michael_n = 0;
    k->velocita = 0;
    k->byte_in = k->byte_out = 0;
    memset(&k->c, 0, sizeof(k->c));
    k->diario = 40;
    k->conto_ms = uptime_ms();
    memset(pmk, 0, sizeof(pmk));
    dire("collegamento a \"%s\"%s\n", k->ssid, c->nascosta ? " (rete nascosta)" : "");

    if (chip_apri(k) != 0) ESCI(WIFI_ERR_FIRMWARE, "il chip non si inizializza");

    /* 1 */
    if (cerca(k, c->nascosta, c->bssid) != 0)
        ESCI(WIFI_ERR_NON_TROVATA, "\"%s\" non si sente su nessun canale", k->ssid);
    dire("trovata: %02x:%02x:%02x:%02x:%02x:%02x canale %d, %d dBm\n", k->bssid[0], k->bssid[1],
         k->bssid[2], k->bssid[3], k->bssid[4], k->bssid[5], k->canale, k->dbm);

    rsn = elemento(k->ie, k->ie_n, 48, NULL);
    wpa = elemento(k->ie, k->ie_n, 221, OUI_WPA);
    k->cif_coppia = k->cif_gruppo = 0;
    if (rsn || wpa) {
        no = leggi_sicurezza(rsn ? rsn : wpa, rsn != NULL, &k->cif_gruppo, &k->cif_coppia, &mfp);
        if (no) ESCI(WIFI_ERR_AUTH, "%s", no);
        if (mfp) ESCI(WIFI_ERR_AUTH, "la rete chiede la protezione dei frame di gestione (802.11w), non ancora gestita");
        k->auth = rsn ? WIFI_AUTH_WPA2 : WIFI_AUTH_WPA;
        nostro_n = nostro_elemento(k, rsn != NULL, nostro);
        dire("%s, per coppia %s, di gruppo %s\n", rsn ? "WPA2" : "WPA",
             k->cif_coppia == SUITE_CCMP ? "CCMP" : "TKIP", k->cif_gruppo == SUITE_CCMP ? "CCMP" : "TKIP");
        if (c->ha_psk) memcpy(pmk, c->psk, 32);
        else if (c->parola[0]) wpa_pmk(c->parola, (const u8 *)k->ssid, (unsigned)k->ssid_n, pmk);
        else ESCI(WIFI_ERR_PAROLA, "la rete e' protetta: serve la password");
    } else if (k->cap & 0x0010) {
        ESCI(WIFI_ERR_AUTH, "la rete usa WEP, che ar5k non sa ancora fare");
    } else {
        k->auth = WIFI_AUTH_APERTA;
        dire("rete aperta, senza cifratura\n");
    }
    k->ofdm = 0;
    for (i = 1; i <= 50; i += 49)
        if ((e = elemento(k->ie, k->ie_n, i, NULL)) != NULL) {
            int j;
            for (j = 0; j < e[1]; j++)
                if ((e[2 + j] & 0x7f) >= 12 && (e[2 + j] & 0x7f) != 22) k->ofdm = 1;
        }

    /* 2 */
    memcpy(k->ah->common.curbssid, k->bssid, ETH_ALEN);
    k->ah->common.curaid = 0;
    if (sul_canale(k, k->canale) != 0) ESCI(WIFI_ERR_FIRMWARE, "il chip non va sul canale %d", k->canale);
    k->visto_ms = uptime_ms();
    r = autentica(k);
    if (r == -1) ESCI(WIFI_ERR_AUTH, "l'access point rifiuta l'autenticazione (stato %d)", k->stato_risposta);
    if (r != 0) ESCI(WIFI_ERR_TEMPO, "l'access point non risponde all'autenticazione");
    dire("autenticata\n");
    if (k->cif_coppia) {
        if (getentropy(snonce, sizeof(snonce)) != 0) ESCI(WIFI_ERR_FIRMWARE, "nessun numero casuale dal kernel");
        memset(&k->wpa, 0, sizeof(k->wpa));
        wpa_inizia(&k->wpa, pmk, k->bssid, k->chip.mac, rsn != NULL,
                   k->cif_coppia == SUITE_CCMP ? WPA_CIFRARIO_CCMP : WPA_CIFRARIO_TKIP,
                   nostro, (unsigned)nostro_n, snonce);
        memset(pmk, 0, sizeof(pmk));
        memset(snonce, 0, sizeof(snonce));
    }
    r = associa(k, nostro, nostro_n);
    if (r == -1) ESCI(WIFI_ERR_AUTH, "l'access point rifiuta l'associazione (stato %d)", k->stato_risposta);
    if (r != 0) ESCI(WIFI_ERR_TEMPO, "l'access point non risponde all'associazione");
    k->ah->common.curaid = k->aid;
    ath5k_hw_set_bssid(k->ah);
    dire("associata, AID %u\n", k->aid);

    /* 3 */
    if (k->cif_coppia) {
        for (t = uptime_ms(); uptime_ms() - t < 4000 && !(k->tk_ok && k->gtk_ok[0] + k->gtk_ok[1] + k->gtk_ok[2] + k->gtk_ok[3]); ) {
            pompa(k);
            if (k->mic_sbagliato) ESCI(WIFI_ERR_PAROLA, "la password non e' quella (MIC sbagliato)");
            if (k->caduta) ESCI(WIFI_ERR_PAROLA, "l'access point ci ha staccati durante lo scambio delle chiavi: password sbagliata?");
            sched_yield();
        }
        /* an access point that finds the MIC of message 2 wrong says nothing:
         * message 1 came and message 3 never did - the password */
        if (!k->tk_ok && k->wpa.ptk_ok)
            ESCI(WIFI_ERR_PAROLA, "l'access point non ha accettato la chiave: la password e' sbagliata");
        if (!k->tk_ok) ESCI(WIFI_ERR_TEMPO, "l'access point non ha cominciato lo scambio delle chiavi");
        if (!(k->gtk_ok[0] + k->gtk_ok[1] + k->gtk_ok[2] + k->gtk_ok[3]))
            dire("chiave di gruppo non ancora arrivata: per ora solo i frame per noi\n");
    }
    k->attiva = 1;
    k->visto_ms = uptime_ms();
    dire("collegata a \"%s\"\n", k->ssid);
    return 0;

fuori:
    memset(pmk, 0, sizeof(pmk));
    ar5k_conn_stacca();
    return r;
#undef ESCI
}

/* to be called often while connected: frames in, the link watched. Returns
 * 1 when the link has just been lost (the chip is already closed). */
int ar5k_conn_lavora(void)
{
    Conn *k = &g_k;

    if (!k->attiva) return 0;
    pompa(k);
    if (uptime_ms() - k->conto_ms >= 10000) { k->conto_ms = uptime_ms(); ar5k_conn_conta(); }
    if (!k->caduta && uptime_ms() - k->visto_ms > PERSA_MS) {
        dire("nessun frame dall'access point da %d s: collegamento perso\n", PERSA_MS / 1000);
        k->caduta = 1;
    }
    if (k->caduta) {
        k->attiva = 0;                                     /* no deauthentication: it is gone */
        ar5k_conn_stacca();
        return 1;
    }
    return 0;
}

int ar5k_conn_attiva(void) { return g_k.attiva; }

/* one line of counters in the log */
void ar5k_conn_conta(void)
{
    Conn *k = &g_k;

    dire("conti: beacon %u, dati dall'AP %u (per noi %u, gruppo %u, cifrati %u), non decifrati %u, "
         "ripetuti %u, senza chiave %u, non LLC %u, in chiaro scartati %u, danneggiati %u; "
         "a ip.drv %u di %u (persi %u); inviati %u, falliti %u\n",
         k->c.beacon, k->c.dati_ap, k->c.per_noi, k->c.gruppo, k->c.cifrati, k->c.mal_cifrati,
         k->c.ripetuti, k->c.senza_chiave, k->c.non_llc, k->c.in_chiaro_scartati, k->c.danneggiati,
         k->c.consegnati, k->c.accodati, k->c.persi_coda, k->c.inviati, k->c.invii_falliti);
}

void ar5k_conn_contatori(unsigned int *inviati, unsigned int *ricevuti, unsigned int *err_tx,
                         unsigned int *err_rx, unsigned int *persi)
{
    *inviati  = g_k.c.inviati;
    *ricevuti = g_k.c.consegnati;
    *err_tx   = g_k.c.invii_falliti;
    *err_rx   = g_k.c.mal_cifrati + g_k.c.danneggiati;
    *persi    = g_k.c.persi_coda;
}

/* an Ethernet frame from ip.drv: 0 or -errno */
int ar5k_conn_invia(const u8 *eth, int n)
{
    Conn *k = &g_k;
    int r;

    if (!k->attiva) return -ENETDOWN;
    if (n < 14 || n > 1514) return -EINVAL;
    r = manda_dati(k, eth, eth + 12, eth + 14, n - 14);
    if (r == 0) { k->byte_out += (unsigned)n; k->c.inviati++; } else k->c.invii_falliti++;
    if (k->diario > 0) {
        k->diario--;
        dire("  tx tipo %02x%02x, %d byte a %02x:%02x:%02x:%02x:%02x:%02x: %s (%u Mbit/s)\n", eth[12], eth[13], n - 14,
             eth[0], eth[1], eth[2], eth[3], eth[4], eth[5], r == 0 ? "confermato" : r == -EIO ? "NON confermato" : "il chip non ha risposto",
             k->velocita);
    }
    /* a frame not acknowledged is lost as on a cable: TCP sends it again */
    return r == -ETIMEDOUT ? -EIO : 0;
}

/* the next Ethernet frame for ip.drv: its length, 0 for none */
int ar5k_conn_prendi(u8 *out)
{
    Conn *k = &g_k;
    int n;

    if (!k->coda_n) return 0;
    k->c.consegnati++;
    n = k->coda_n_byte[k->coda_testa];
    memcpy(out, k->coda[k->coda_testa], (size_t)n);
    k->coda_testa = (k->coda_testa + 1) % CODA_N;
    k->coda_n--;
    return n;
}

void ar5k_conn_stato(WifiStato *s)
{
    Conn *k = &g_k;

    if (!k->attiva) return;
    s->stato = WIFI_STATO_CONNESSA;
    memcpy(s->ssid, k->ssid, (size_t)k->ssid_n + 1);
    memcpy(s->bssid, k->bssid, 6);
    s->canale        = k->canale;
    s->potenza       = k->dbm;
    s->auth          = k->auth;
    s->velocita      = k->velocita ? k->velocita : (k->ofdm ? 24 : 11);
    s->byte_inviati  = k->byte_out;
    s->byte_ricevuti = k->byte_in;
}
