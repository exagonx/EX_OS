/* =============================================================================
 * tools/prova-ar5k/prova.c — drivers/ar5k/ar5k_conn.c against a pretend
 * access point, on the host
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The chip is pretended (finto/ar5k_hw.h): a receive ring that frames are
 * written into the way the hardware does it (self-linked tail included), and
 * a transmit queue whose frames go to the access point below. The access
 * point answers probe, authentication and association, runs the 4-way and
 * group handshakes as an authenticator (the MIC of message 2 checked with
 * its own PMK), and exchanges encrypted data frames. Run by prova.sh.
 *
 * Cases: WPA2 CCMP/CCMP; WPA2 CCMP with a TKIP group key (a "WPA/WPA2 mixed"
 * access point, like the user's); WPA with TKIP; a wrong password; a hidden
 * network; an open network; deauthentication; a replayed frame.
 * ============================================================================= */
#include "ar5k_hw.h"
#include "wifi_proto.h"
#include "exwifi.h"
#include "ar5k_conn.h"
#include <time.h>

static int g_ok, g_ko;
static void esito(const char *cosa, int buono)
{
    printf("  %-62s %s\n", cosa, buono ? "ok" : "SBAGLIATO");
    if (buono) g_ok++; else g_ko++;
}

/* --- the host's side of the EX-OS calls -------------------------------------------- */
#define FIS0 0x100000u
static u8 *g_dma;
static u32 g_dma_n;

int dma_alloc(DmaZona *z)
{
    g_dma = calloc(1, z->byte);
    g_dma_n = z->byte;
    z->virt = (uintptr_t)g_dma;
    z->fisico = FIS0;
    return g_dma ? 0 : -1;
}
static void *virt(u32 f) { return f >= FIS0 && f < FIS0 + g_dma_n ? g_dma + (f - FIS0) : NULL; }
static u32 fis(const void *p) { return FIS0 + (u32)((const u8 *)p - g_dma); }

unsigned int uptime_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned int)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

/* --- the access point --------------------------------------------------------------- */
static const u8 STA[6]   = { 0x00, 0x1f, 0x16, 0xe6, 0xcd, 0x2e };
static const u8 AP[6]    = { 0x02, 0xa0, 0x00, 0x00, 0x00, 0x01 };
static const u8 TUTTI[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static const u8 RETE_PC[6] = { 0x02, 0xbb, 0x00, 0x00, 0x00, 0x07 };   /* a host behind the AP */

typedef struct {
    char ssid[33];
    int  canale, rsn, wpa, nascosta;
    int  coppia, gruppo;            /* 4 CCMP, 2 TKIP */
    char parola[64];
    /* state */
    u8   pmk[32], anonce[32];
    WpaPtk ptk;
    int  ptk_ok, assoc, msg4, sordo;
    unsigned long long replay, pn, gpn;
    u8   gtk[32];
    int  gtk_n;
    u8   ricevuto[1600];
    int  ricevuto_n;
    int  silenzioso;                /* answers nothing to auth (out of range) */
} Ap;
static Ap g_ap;

/* the receive ring as the hardware fills it */
static u32 g_rxdp, g_canale;
static void inietta(const u8 *f, int n)
{
    struct ath5k_desc *d = virt(g_rxdp);

    if (!d) return;
    if (d->ud[0]) {                                     /* done: is there a next one? */
        if (d->ds_link == g_rxdp) return;               /* ring full: lost */
        g_rxdp = d->ds_link;
        d = virt(g_rxdp);
        if (d->ud[0]) return;
    }
    memcpy(virt(d->ds_data), f, (size_t)n);
    memset((u8 *)virt(d->ds_data) + n, 0xee, 4);       /* the FCS */
    d->ud[1] = (u32)n + 4;
    d->ud[0] = 1;
    if (d->ds_link != g_rxdp) g_rxdp = d->ds_link;
}

static void ap_testa(u8 *f, int fc0, int fc1, const u8 *a1, const u8 *a2, const u8 *a3)
{
    static u16 seq;
    memset(f, 0, 24);
    f[0] = (u8)fc0; f[1] = (u8)fc1;
    memcpy(f + 4, a1, 6); memcpy(f + 10, a2, 6); memcpy(f + 16, a3, 6);
    f[22] = (u8)(seq << 4); f[23] = (u8)(seq >> 4); seq++;
}

static int ap_elementi(u8 *p)
{
    static const u8 TASSI[] = { 1, 8, 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 50, 4, 0x30, 0x48, 0x60, 0x6c };
    int n = 0, l = (int)strlen(g_ap.ssid);

    p[n++] = 0; p[n++] = (u8)(g_ap.nascosta ? 0 : l);
    if (!g_ap.nascosta) { memcpy(p + n, g_ap.ssid, (size_t)l); n += l; }
    memcpy(p + n, TASSI, sizeof(TASSI)); n += (int)sizeof(TASSI);
    p[n++] = 3; p[n++] = 1; p[n++] = (u8)g_ap.canale;
    if (g_ap.rsn) {
        u8 e[] = { 48, 20, 1, 0, 0, 0x0f, 0xac, (u8)g_ap.gruppo, 1, 0, 0, 0x0f, 0xac, (u8)g_ap.coppia,
                   1, 0, 0, 0x0f, 0xac, 2, 0, 0 };
        memcpy(p + n, e, sizeof(e)); n += (int)sizeof(e);
    }
    if (g_ap.wpa) {
        u8 e[] = { 221, 22, 0x00, 0x50, 0xf2, 1, 1, 0, 0x00, 0x50, 0xf2, 2, 1, 0, 0x00, 0x50, 0xf2, 2,
                   1, 0, 0x00, 0x50, 0xf2, 2 };
        memcpy(p + n, e, sizeof(e)); n += (int)sizeof(e);
    }
    return n;
}

static void ap_faro(int risposta)                      /* beacon, or probe response */
{
    u8 f[400];
    int n;
    u16 cap = 0x0401 | ((g_ap.rsn || g_ap.wpa) ? 0x0010 : 0);

    ap_testa(f, risposta ? 0x50 : 0x80, 0, risposta ? STA : TUTTI, AP, AP);
    memset(f + 24, 0, 8);
    f[32] = 100; f[33] = 0;
    f[34] = (u8)cap; f[35] = (u8)(cap >> 8);
    if (risposta) { int s = g_ap.nascosta; g_ap.nascosta = 0; n = 36 + ap_elementi(f + 36); g_ap.nascosta = s; }
    else n = 36 + ap_elementi(f + 36);
    inietta(f, n);
}

/* a data frame FromDS: LLC/SNAP, encrypted with the pairwise or group key */
static void ap_dati(const u8 *da, const u8 *sa, u16 tipo, const u8 *dati, int n, int cifra)
{
    u8 f[2000], chiaro[1600];
    int m;
    int gruppo = da[0] & 1;

    ap_testa(f, 0x08, 0x02 | (cifra ? 0x40 : 0), da, AP, sa);
    chiaro[0] = 0xaa; chiaro[1] = 0xaa; chiaro[2] = 3; chiaro[3] = chiaro[4] = chiaro[5] = 0;
    chiaro[6] = (u8)(tipo >> 8); chiaro[7] = (u8)tipo;
    memcpy(chiaro + 8, dati, (size_t)n);
    m = 8 + n;
    if (cifra) {
        int c = gruppo ? g_ap.gruppo : g_ap.coppia;
        const u8 *tk = gruppo ? g_ap.gtk : g_ap.ptk.tk;
        if (c == 4) m = ccmp_cifra(tk, f, 24, gruppo ? ++g_ap.gpn : ++g_ap.pn, gruppo ? 1 : 0, chiaro, (unsigned)m, f + 24);
        /* TKIP: the AP sends with the authenticator's Michael key, bytes 16-23 */
        else m = tkip_cifra(tk, tk + 16, AP, da, sa, 0, gruppo ? ++g_ap.gpn : ++g_ap.pn, gruppo ? 1 : 0,
                            chiaro, (unsigned)m, f + 24);
    } else memcpy(f + 24, chiaro, (size_t)m);
    inietta(f, 24 + m);
}

/* EAPOL-Key frames, as in tools/prova_exwifi.c */
static unsigned int eapol_frame(unsigned int info, unsigned int klung, unsigned long long replay,
                                const u8 *nonce, const u8 *iv, const u8 *dati, unsigned int dn, u8 *f)
{
    unsigned int n = 99 + dn, i;

    memset(f, 0, n);
    f[0] = 2; f[1] = 3; f[2] = (u8)((n - 4) >> 8); f[3] = (u8)(n - 4);
    f[4] = g_ap.rsn ? 2 : 254;
    f[5] = (u8)(info >> 8); f[6] = (u8)info;
    f[7] = (u8)(klung >> 8); f[8] = (u8)klung;
    for (i = 0; i < 8; i++) f[9 + i] = (u8)(replay >> (56 - 8 * i));
    if (nonce) memcpy(f + 17, nonce, 32);
    if (iv) memcpy(f + 49, iv, 16);
    f[97] = (u8)(dn >> 8); f[98] = (u8)dn;
    memcpy(f + 99, dati, dn);
    return n;
}

static int versione(void) { return g_ap.coppia == 4 ? 2 : 1; }

static void ap_msg1(void)
{
    u8 f[600];
    unsigned int n;
    int i;

    for (i = 0; i < 32; i++) g_ap.anonce[i] = (u8)(rand());
    n = eapol_frame((unsigned)versione() | EAPOL_KI_COPPIA | EAPOL_KI_ACK, g_ap.coppia == 4 ? 16 : 32,
                    ++g_ap.replay, g_ap.anonce, NULL, NULL, 0, f);
    ap_dati(STA, AP, 0x888e, f, (int)n, 0);
}

static void ap_eapol(const u8 *e, int n)
{
    u8 c[600], mic[16], f[600], kd[160], w[200];
    unsigned int info = (unsigned)(e[5] << 8 | e[6]), k = 0, fn;
    int v = (int)(info & 7);

    if (n < 99 || n > 600) return;
    if (!g_ap.ptk_ok) {                                 /* message 2 */
        wpa_ptk(g_ap.pmk, AP, STA, g_ap.anonce, e + 17, g_ap.coppia == 4 ? WPA_CIFRARIO_CCMP : WPA_CIFRARIO_TKIP, &g_ap.ptk);
        memcpy(c, e, (size_t)n); memset(c + 81, 0, 16);
        eapol_mic(v, g_ap.ptk.kck, c, (unsigned)n, mic);
        if (memcmp(mic, e + 81, 16) != 0) { g_ap.sordo = 1; return; }   /* wrong password: silence */
        g_ap.ptk_ok = 1;
        /* message 3 */
        g_ap.gtk_n = g_ap.gruppo == 4 ? 16 : 32;
        if (g_ap.rsn) {
            { u8 e2[] = { 48, 20, 1, 0, 0, 0x0f, 0xac, (u8)g_ap.gruppo, 1, 0, 0, 0x0f, 0xac, (u8)g_ap.coppia,
                          1, 0, 0, 0x0f, 0xac, 2, 0, 0 }; memcpy(kd, e2, sizeof(e2)); k = sizeof(e2); }
            kd[k++] = 0xdd; kd[k++] = (u8)(6 + g_ap.gtk_n);
            kd[k++] = 0x00; kd[k++] = 0x0f; kd[k++] = 0xac; kd[k++] = 1; kd[k++] = 1; kd[k++] = 0;
            memcpy(kd + k, g_ap.gtk, (size_t)g_ap.gtk_n); k += (unsigned)g_ap.gtk_n;
            if (k % 8) { kd[k++] = 0xdd; while (k % 8) kd[k++] = 0; }
            aes_wrap(g_ap.ptk.kek, kd, k, w);
            fn = eapol_frame((unsigned)versione() | EAPOL_KI_COPPIA | EAPOL_KI_ACK | EAPOL_KI_MIC | EAPOL_KI_INSTALLA |
                             EAPOL_KI_SICURO | EAPOL_KI_CIFRATI, 16, ++g_ap.replay, g_ap.anonce, NULL, w, k + 8, f);
        } else {
            u8 e2[] = { 221, 22, 0x00, 0x50, 0xf2, 1, 1, 0, 0x00, 0x50, 0xf2, 2, 1, 0, 0x00, 0x50, 0xf2, 2,
                        1, 0, 0x00, 0x50, 0xf2, 2 };
            fn = eapol_frame((unsigned)versione() | EAPOL_KI_COPPIA | EAPOL_KI_ACK | EAPOL_KI_MIC | EAPOL_KI_INSTALLA,
                             32, ++g_ap.replay, g_ap.anonce, NULL, e2, sizeof(e2), f);
        }
        eapol_mic(versione(), g_ap.ptk.kck, f, fn, f + 81);
        ap_dati(STA, AP, 0x888e, f, (int)fn, 0);
        return;
    }
    if (!g_ap.msg4) {                                   /* message 4 */
        memcpy(c, e, (size_t)n); memset(c + 81, 0, 16);
        eapol_mic(v, g_ap.ptk.kck, c, (unsigned)n, mic);
        if (memcmp(mic, e + 81, 16) != 0) return;
        g_ap.msg4 = 1;
        if (!g_ap.rsn) {                                /* WPA: the group key now */
            u8 iv[16], kk[32];
            int i;
            for (i = 0; i < 16; i++) iv[i] = (u8)(0x55 + i);
            memcpy(kk, iv, 16); memcpy(kk + 16, g_ap.ptk.kek, 16);
            g_ap.gtk_n = 32;
            rc4(kk, 32, 256, g_ap.gtk, w, 32);
            fn = eapol_frame(1 | EAPOL_KI_ACK | EAPOL_KI_MIC | EAPOL_KI_SICURO | (1 << 4), 32, ++g_ap.replay,
                             NULL, iv, w, 32, f);
            eapol_mic(1, g_ap.ptk.kck, f, fn, f + 81);
            ap_dati(STA, AP, 0x888e, f, (int)fn, 1);       /* encrypted: after the 4-way */
        }
        return;
    }
}

static void ap_ricevi(const u8 *f, int n)
{
    int tipo = (f[0] >> 2) & 3, sotto = f[0] >> 4;
    u8 r[300];

    if (n < 24) return;
    if (tipo == 0 && sotto == 4) {                      /* probe request */
        if ((int)g_canale == g_ap.canale && f[25] == strlen(g_ap.ssid) && !memcmp(f + 26, g_ap.ssid, f[25]))
            ap_faro(1);
        return;
    }
    if (memcmp(f + 4, AP, 6) != 0) return;
    if (tipo == 0 && sotto == 11) {                     /* authentication */
        if (g_ap.silenzioso) return;
        ap_testa(r, 0xb0, 0, STA, AP, AP);
        memset(r + 24, 0, 6); r[26] = 2;
        inietta(r, 30);
        return;
    }
    if (tipo == 0 && sotto == 0) {                      /* association */
        ap_testa(r, 0x10, 0, STA, AP, AP);
        r[24] = 0x01; r[25] = 0x04; r[26] = 0; r[27] = 0; r[28] = 1; r[29] = 0xc0;
        inietta(r, 30);
        g_ap.assoc = 1;
        g_ap.ptk_ok = g_ap.msg4 = 0;
        if (g_ap.rsn || g_ap.wpa) ap_msg1();
        return;
    }
    if (tipo == 2 && (f[1] & 3) == 1) {                 /* data, ToDS */
        u8 c[2000];
        const u8 *p = f + 24;
        int m = n - 24;
        unsigned long long pn;

        if (f[1] & 0x40) {
            if (g_ap.coppia == 4) m = ccmp_decifra(g_ap.ptk.tk, f, 24, f + 24, (unsigned)(n - 24), c, &pn);
            else m = tkip_decifra(g_ap.ptk.tk, g_ap.ptk.tk + 24, STA, f + 16, STA, 0, f + 24, (unsigned)(n - 24), c, &pn);
            if (m < 0) { printf("  (l'AP non decifra un frame della stazione: %d)\n", m); return; }
            p = c;
        }
        if (m < 8 || p[0] != 0xaa) return;
        if (p[6] == 0x88 && p[7] == 0x8e) { ap_eapol(p + 8, m - 8); return; }
        memcpy(g_ap.ricevuto, f + 16, 6);
        memcpy(g_ap.ricevuto + 6, p + 6, (size_t)(m - 6));
        g_ap.ricevuto_n = m;
    }
}

/* --- the pretend chip ----------------------------------------------------------------- */
static struct ath5k_hw g_ah;
static u32 g_txdp;

static int setup_tx(struct ath5k_hw *ah, struct ath5k_desc *d, unsigned int len, unsigned int hl, int pad,
                    int tipo, unsigned int pot, unsigned int r0, unsigned int t0, unsigned int key,
                    unsigned int ant, unsigned int flag, unsigned int rr, unsigned int rd)
{
    d->ud[0] = 0; d->ud[1] = len; d->ud[2] = r0;
    return t0 && r0 ? 0 : -EINVAL;
}
static int proc_tx(struct ath5k_hw *ah, struct ath5k_desc *d, struct ath5k_tx_status *ts)
{
    if (!d->ud[0]) return -EINPROGRESS;
    ts->ts_status = 0; ts->ts_final_idx = 0;
    return 0;
}
static int proc_rx(struct ath5k_hw *ah, struct ath5k_desc *d, struct ath5k_rx_status *rs)
{
    if (!d->ud[0]) return -EINPROGRESS;
    rs->rs_datalen = (u16)d->ud[1]; rs->rs_status = 0; rs->rs_rssi = 50;
    return 0;
}

int ar5k_apri(Ar5kChip *k, volatile void *r, unsigned int s)
{
    memset(&g_ah, 0, sizeof(g_ah));
    g_ah.ah_setup_tx_desc = setup_tx;
    g_ah.ah_proc_tx_desc  = proc_tx;
    g_ah.ah_proc_rx_desc  = proc_rx;
    g_ah.ah_noise_floor   = -95;
    k->ah = calloc(1, sizeof(struct ath5k_hw));
    *k->ah = g_ah;
    memcpy(k->mac, STA, 6);
    return 0;
}
void ar5k_chiudi(Ar5kChip *k) { (void)k; }
void ar5k_canale_g(struct ieee80211_channel *ch, int n) { ch->center_freq = n; }
int  ath5k_hw_reset(struct ath5k_hw *ah, int op, struct ieee80211_channel *ch, bool f, bool s) { g_canale = (u32)ch->center_freq; return 0; }
int  ath5k_hw_setup_rx_desc(struct ath5k_hw *ah, struct ath5k_desc *d, u32 size, unsigned int fl) { d->ud[0] = 0; return 0; }
int  ath5k_hw_set_rxdp(struct ath5k_hw *ah, u32 p) { g_rxdp = p; return 0; }
u32  ath5k_hw_get_rxdp(struct ath5k_hw *ah) { return g_rxdp; }
int  ath5k_hw_start_rx_dma(struct ath5k_hw *ah) { return 0; }
void ath5k_hw_set_rx_filter(struct ath5k_hw *ah, u32 f) { }
void ath5k_hw_set_mcast_filter(struct ath5k_hw *ah, u32 a, u32 b) { }
void ath5k_hw_start_rx_pcu(struct ath5k_hw *ah)
{
    if ((int)g_canale == g_ap.canale) ap_faro(0);      /* a beacon heard on the AP's channel */
}
void ath5k_hw_stop_rx_pcu(struct ath5k_hw *ah) { }
int  ath5k_hw_dma_stop(struct ath5k_hw *ah) { return 0; }
int  ath5k_hw_setup_tx_queue(struct ath5k_hw *ah, int t, struct ath5k_txq_info *q) { return 1; }
int  ath5k_hw_set_txdp(struct ath5k_hw *ah, unsigned int q, u32 p) { g_txdp = p; return 0; }
int  ath5k_hw_start_tx_dma(struct ath5k_hw *ah, unsigned int q)
{
    struct ath5k_desc *d;

    while ((d = virt(g_txdp)) != NULL && !d->ud[0]) {
        d->ud[0] = 1;                                   /* sent and acknowledged */
        ap_ricevi(virt(d->ds_data), (int)d->ud[1]);
        if (!d->ds_link) break;
        g_txdp = d->ds_link;
    }
    return 0;
}
int  ath5k_hw_setup_mrr_tx_desc(struct ath5k_hw *ah, struct ath5k_desc *d, unsigned int r1, unsigned int t1,
                                unsigned int r2, unsigned int t2, unsigned int r3, unsigned int t3) { return 1; }
void ath5k_hw_set_bssid(struct ath5k_hw *ah) { }
void ath5k_hw_reg_write(struct ath5k_hw *ah, u32 val, u16 reg) { }

/* --- the cases ---------------------------------------------------------------------- */
static int collega(const char *parola, int nascosta, char *perche)
{
    WifiConnetti c;

    memset(&c, 0, sizeof(c));
    strcpy(c.ssid, g_ap.ssid);
    c.auth = -1;
    c.nascosta = nascosta;
    if (parola) {
        wpa_pmk(parola, (const u8 *)c.ssid, (unsigned)strlen(c.ssid), c.psk);
        c.ha_psk = 1;
    }
    perche[0] = 0;
    return ar5k_conn_collega(NULL, 0, &c, perche, 96);
}

static void ap_nuovo(const char *ssid, int rsn, int wpa, int coppia, int gruppo, const char *parola)
{
    int i;

    memset(&g_ap, 0, sizeof(g_ap));
    strcpy(g_ap.ssid, ssid);
    g_ap.canale = 11;
    g_ap.rsn = rsn; g_ap.wpa = wpa; g_ap.coppia = coppia; g_ap.gruppo = gruppo;
    strcpy(g_ap.parola, parola);
    wpa_pmk(parola, (const u8 *)ssid, (unsigned)strlen(ssid), g_ap.pmk);
    for (i = 0; i < 32; i++) g_ap.gtk[i] = (u8)(0xc0 + i);
}

/* one round of traffic each way, then a broadcast and a replay */
static void traffico(const char *nome)
{
    u8 eth[200], ip[100], out[1514], f[2000];
    char t[96];
    int n, i;

    for (i = 0; i < 100; i++) ip[i] = (u8)(i * 7);
    memcpy(eth, RETE_PC, 6); memcpy(eth + 6, STA, 6); eth[12] = 0x08; eth[13] = 0;
    memcpy(eth + 14, ip, 100);
    g_ap.ricevuto_n = 0;
    n = ar5k_conn_invia(eth, 114);
    snprintf(t, sizeof(t), "%s: frame verso la rete, decifrato dall'AP", nome);
    esito(t, n == 0 && g_ap.ricevuto_n == 108 && !memcmp(g_ap.ricevuto, RETE_PC, 6) &&
             g_ap.ricevuto[6] == 0x08 && !memcmp(g_ap.ricevuto + 8, ip, 100));

    ap_dati(STA, RETE_PC, 0x0800, ip, 60, 1);
    ar5k_conn_lavora();
    n = ar5k_conn_prendi(out);
    snprintf(t, sizeof(t), "%s: frame dalla rete, per noi", nome);
    esito(t, n == 74 && !memcmp(out, STA, 6) && !memcmp(out + 6, RETE_PC, 6) && out[12] == 8 && !memcmp(out + 14, ip, 60));

    ap_dati(TUTTI, RETE_PC, 0x0806, ip, 28, 1);
    ar5k_conn_lavora();
    n = ar5k_conn_prendi(out);
    snprintf(t, sizeof(t), "%s: broadcast con la chiave di gruppo", nome);
    esito(t, n == 42 && !memcmp(out, TUTTI, 6) && out[12] == 8 && out[13] == 6);

    /* the same encrypted frame twice: the second is a replay */
    {
        u32 rxdp0 = g_rxdp;
        struct ath5k_desc *d;
        ap_dati(STA, RETE_PC, 0x0800, ip, 40, 1);
        d = virt(rxdp0);
        if (d && d->ud[0]) { memcpy(f, virt(d->ds_data), d->ud[1] - 4); n = (int)d->ud[1] - 4; }
        else n = 0;
        ar5k_conn_lavora();
        ar5k_conn_prendi(out);
        if (n) inietta(f, n);
        ar5k_conn_lavora();
        snprintf(t, sizeof(t), "%s: frame ripetuto scartato", nome);
        esito(t, n && ar5k_conn_prendi(out) == 0);
    }
}

int main(void)
{
    char perche[96];
    int r;

    extern int g_eco;
    g_eco = 1;
    srand(7);
    printf("ar5k_conn.c contro un access point finto\n");

    ap_nuovo("ExagonX.prova", 1, 0, 4, 4, "una parola lunga");
    r = collega("una parola lunga", 0, perche);
    esito("WPA2 CCMP: collegata", r == 0 && ar5k_conn_attiva());
    if (r) printf("    (%d: %s)\n", r, perche);
    if (r == 0) traffico("WPA2 CCMP");
    {
        u8 d[2] = { 7, 0 }, f[64];
        ap_testa(f, 0xc0, 0, STA, AP, AP); memcpy(f + 24, d, 2);
        inietta(f, 26);
        esito("deautenticazione dall'AP: collegamento perso", ar5k_conn_lavora() == 1 && !ar5k_conn_attiva());
    }

    ap_nuovo("ExagonX.misto", 1, 1, 4, 2, "un'altra parola");
    r = collega("un'altra parola", 0, perche);
    esito("WPA/WPA2 misto (gruppo TKIP): collegata in WPA2", r == 0);
    if (r) printf("    (%d: %s)\n", r, perche);
    if (r == 0) traffico("gruppo TKIP");
    ar5k_conn_stacca();

    ap_nuovo("ExagonX.wpa", 0, 1, 2, 2, "parola wpa uno");
    r = collega("parola wpa uno", 0, perche);
    esito("WPA (TKIP): collegata", r == 0);
    if (r) printf("    (%d: %s)\n", r, perche);
    if (r == 0) traffico("WPA TKIP");
    ar5k_conn_stacca();

    ap_nuovo("ExagonX.prova", 1, 0, 4, 4, "una parola lunga");
    r = collega("parola sbagliata", 0, perche);
    esito("password sbagliata: WIFI_ERR_PAROLA", r == WIFI_ERR_PAROLA && !ar5k_conn_attiva());
    printf("    (%s)\n", perche);

    ap_nuovo("ExagonX.nascosta", 1, 0, 4, 4, "segreta segreta");
    g_ap.nascosta = 1;
    r = collega("segreta segreta", 0, perche);
    esito("rete nascosta senza saperlo: non trovata", r == WIFI_ERR_NON_TROVATA);
    r = collega("segreta segreta", 1, perche);
    esito("rete nascosta, cercata per nome: collegata", r == 0);
    ar5k_conn_stacca();

    ap_nuovo("ExagonX.aperta", 0, 0, 0, 0, "");
    r = collega(NULL, 0, perche);
    esito("rete aperta: collegata", r == 0);
    if (r == 0) {
        u8 eth[60] = { 0 }, out[1514];
        memcpy(eth, RETE_PC, 6); memcpy(eth + 6, STA, 6); eth[12] = 8;
        g_ap.ricevuto_n = 0;
        esito("rete aperta: frame in chiaro verso la rete", ar5k_conn_invia(eth, 60) == 0 && g_ap.ricevuto_n == 54);
        ap_dati(STA, RETE_PC, 0x0800, eth, 46, 0);
        ar5k_conn_lavora();
        esito("rete aperta: frame in chiaro dalla rete", ar5k_conn_prendi(out) == 60);
    }
    ar5k_conn_stacca();

    ap_nuovo("ExagonX.lontana", 1, 0, 4, 4, "una parola lunga");
    g_ap.silenzioso = 1;
    r = collega("una parola lunga", 0, perche);
    esito("access point che non risponde: WIFI_ERR_TEMPO", r == WIFI_ERR_TEMPO);

    printf("%d giuste, %d sbagliate\n", g_ok, g_ko);
    return g_ko ? 1 : 0;
}
