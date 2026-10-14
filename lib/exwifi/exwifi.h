/* =============================================================================
 * lib/exwifi/exwifi.h — WiFi security, once for every driver (@WIFI, W6)
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * What a WiFi driver needs between "associated" and "frames go through",
 * written once so that no driver carries its own copy:
 *
 *   wpa_pmk, wpa_prf, wpa_ptk   the keys (IEEE 802.11-2012 11.6.1)
 *   WpaSupplicante              the client side of the 4-way and group key
 *                               handshakes (EAPOL-Key, 11.6.6 / 11.6.7), WPA
 *                               (descriptor 254) and WPA2/RSN (descriptor 2)
 *   ccmp_cifra / ccmp_decifra   WPA2 data frames (AES-CCM, 11.4.3)
 *   tkip_cifra / tkip_decifra   WPA data frames (key mixing, RC4, Michael)
 *   wep_cifra / wep_decifra     WEP (RC4 + CRC-32)
 *
 * It sits on lib/excrypt (AES, AES-CCM, SHA-1, HMAC-SHA1, PBKDF2) and keeps
 * here the three small pieces only WiFi wants: MD5 (the MIC of WPA's
 * descriptor version 1), RC4 and CRC-32.
 *
 * Nothing here touches hardware or IPC: the driver hands frames in and sends
 * what comes out. Tested on the host against the IEEE 802.11 and RFC test
 * vectors: tools/prova_exwifi.sh.
 *
 * ! WEP AND TKIP ARE BROKEN and are here because the user asked to reach old
 * networks (wifi_proto.h WIFI_AUTH_WEP / WIFI_AUTH_WPA). Nothing here picks
 * them over CCMP: the choice is the network's.
 * ============================================================================= */
#ifndef EXWIFI_H
#define EXWIFI_H

/* --- keys -------------------------------------------------------------------- */

/* PMK of a WPA-PSK network: PBKDF2-HMAC-SHA1(passphrase, SSID, 4096, 32) */
void wpa_pmk(const char *parola, const unsigned char *ssid, unsigned int ssid_n,
             unsigned char pmk[32]);

/* PRF-n of 802.11: HMAC-SHA1(K, label || 0 || data || i), i = 0, 1, ... */
void wpa_prf(const unsigned char *k, unsigned int k_n, const char *etichetta,
             const unsigned char *dati, unsigned int dati_n,
             unsigned char *out, unsigned int out_n);

#define WPA_CIFRARIO_CCMP   1
#define WPA_CIFRARIO_TKIP   2

/* The pairwise transient key, cut in its parts. For TKIP `tk` is 32 bytes:
 * the key (16), then the Michael key the AUTHENTICATOR sends with (8: the
 * client RECEIVES with it), then the one the client sends with (8). */
typedef struct {
    unsigned char kck[16];         /* signs EAPOL-Key frames (the MIC)       */
    unsigned char kek[16];         /* encrypts the group key inside them     */
    unsigned char tk[32];
    unsigned int  tk_n;            /* 16 for CCMP, 32 for TKIP                */
} WpaPtk;

void wpa_ptk(const unsigned char pmk[32], const unsigned char aa[6],
             const unsigned char spa[6], const unsigned char anonce[32],
             const unsigned char snonce[32], int cifrario, WpaPtk *p);

/* --- EAPOL-Key, the client side ---------------------------------------------- */

/* 802.1X header (4) + the fixed part of an EAPOL-Key body (95) */
#define EAPOL_TESTA         4
#define EAPOL_KEY_FISSO     95
#define EAPOL_MAX           512

/* Key Information bits */
#define EAPOL_KI_VERSIONE   0x0007   /* 1: HMAC-MD5 + RC4, 2: HMAC-SHA1 + AES wrap */
#define EAPOL_KI_COPPIA     0x0008   /* pairwise (else group)                   */
#define EAPOL_KI_INDICE     0x0030   /* WPA only: the group key's index         */
#define EAPOL_KI_INSTALLA   0x0040
#define EAPOL_KI_ACK        0x0080
#define EAPOL_KI_MIC        0x0100
#define EAPOL_KI_SICURO     0x0200
#define EAPOL_KI_ERRORE     0x0400
#define EAPOL_KI_RICHIESTA  0x0800
#define EAPOL_KI_CIFRATI    0x1000   /* the key data is encrypted (RSN)         */

typedef struct {
    unsigned char pmk[32];
    unsigned char aa[6], spa[6];   /* the access point's address, ours       */
    unsigned char anonce[32], snonce[32];
    int           rsn;             /* 1: WPA2 (RSN), 0: WPA                    */
    int           cifrario;        /* WPA_CIFRARIO_*, the pairwise one         */
    unsigned char ie[64];          /* the RSN/WPA element of our association   */
    unsigned int  ie_n;            /* request: it travels in message 2          */

    WpaPtk        ptk;
    int           ptk_ok;          /* PTK derived (after message 1)            */
    unsigned char replay[8];
    int           replay_ok;

    /* what the handshakes produced */
    int           coppia_pronta;   /* the PTK may be installed                 */
    unsigned char gtk[32];
    unsigned int  gtk_n;
    int           gtk_indice;
    unsigned char gtk_rsc[8];      /* the group key's receive sequence counter */
    int           gtk_pronta;
} WpaSupplicante;

/* `snonce` must be random (32 bytes from the kernel's generator): the caller
 * gives it, since this library has no source of its own. */
void wpa_inizia(WpaSupplicante *s, const unsigned char pmk[32],
                const unsigned char aa[6], const unsigned char spa[6],
                int rsn, int cifrario, const unsigned char *ie, unsigned int ie_n,
                const unsigned char snonce[32]);

/* One EAPOL frame from the access point (from the 802.1X header on). When an
 * answer is due it is written in `out` (EAPOL_MAX bytes) and its length in
 * *out_n. Returns:
 *   WPA_NIENTE      nothing to answer (a frame that is not ours)
 *   WPA_RISPONDI    send `out` (message 2 of 4)
 *   WPA_COPPIA      send `out` (message 4), then install s->ptk.tk; and the
 *                   group key too if gtk_pronta (WPA2 carries it in message 3)
 *   WPA_GRUPPO      send `out` (group message 2), then install s->gtk
 *   < 0             refused: WPA_ERR_* (the frame is dropped, nothing sent) */
#define WPA_NIENTE       0
#define WPA_RISPONDI     1
#define WPA_COPPIA       2
#define WPA_GRUPPO       3
#define WPA_ERR_FORMA   -1     /* malformed, or not an EAPOL-Key              */
#define WPA_ERR_MIC     -2     /* the signature is wrong: a wrong password    */
#define WPA_ERR_REPLAY  -3     /* an old frame played again                   */
#define WPA_ERR_NONCE   -4     /* message 3 with another ANonce than 1        */
#define WPA_ERR_CHIAVE  -5     /* the key data does not decrypt               */
int wpa_ricevi(WpaSupplicante *s, const unsigned char *in, unsigned int in_n,
               unsigned char *out, unsigned int *out_n);

/* The MIC of an EAPOL-Key frame (MIC field zeroed by the caller): HMAC-MD5
 * for version 1, the first 16 bytes of HMAC-SHA1 for version 2. */
void eapol_mic(int versione, const unsigned char kck[16],
               const unsigned char *frame, unsigned int n, unsigned char mic[16]);

/* AES key wrap (RFC 3394), used for the group key in version 2. `n` is the
 * plain length, a multiple of 8, at least 16; the wrapped one is n + 8.
 * aes_unwrap returns 0, or -1 when the integrity check fails. */
void aes_wrap(const unsigned char kek[16], const unsigned char *in, unsigned int n,
              unsigned char *out);
int  aes_unwrap(const unsigned char kek[16], const unsigned char *in, unsigned int n,
                unsigned char *out);

/* --- data frames -------------------------------------------------------------
 *
 * `hdr` is the 802.11 MAC header as it goes on the air (24 to 32 bytes: with
 * Address 4 when ToDS and FromDS are both set, with QoS Control in a QoS data
 * frame), Protected bit set. The sizes are the caller's to check. */

/* CCMP: out = CCMP header (8) + ciphertext (n) + MIC (8). Returns n + 16. */
int ccmp_cifra(const unsigned char tk[16], const unsigned char *hdr, unsigned int hdr_n,
               unsigned long long pn, int indice,
               const unsigned char *in, unsigned int n, unsigned char *out);
/* in = CCMP header + ciphertext + MIC. Returns the plain length (in_n - 16)
 * and the frame's packet number in *pn - the caller drops it unless it is
 * above the last one seen (replay) - or -1 when the MIC is wrong. */
int ccmp_decifra(const unsigned char tk[16], const unsigned char *hdr, unsigned int hdr_n,
                 const unsigned char *in, unsigned int in_n, unsigned char *out,
                 unsigned long long *pn);

/* TKIP, for an MSDU that is not fragmented. `tk` is the 16-byte key, `mic`
 * the Michael key for this direction; `ta` the transmitter (Address 2), `da`
 * and `sa` the MSDU's ends; `priorita` the TID (0 without QoS).
 * out = IV/ExtIV (8) + encrypted [data + Michael MIC (8) + ICV (4)].
 * Returns n + 20. */
int tkip_cifra(const unsigned char tk[16], const unsigned char mic[8],
               const unsigned char ta[6], const unsigned char da[6],
               const unsigned char sa[6], int priorita, unsigned long long tsc,
               int indice, const unsigned char *in, unsigned int n, unsigned char *out);
/* Returns the plain length (in_n - 20) and the TSC in *tsc; -1 when the ICV
 * is wrong (noise: drop it); -2 when the ICV is right and Michael is not.
 * ! -2 IS AN ATTACK, not noise: two in 60 seconds and the standard wants the
 * client off the network for 60 s (TKIP countermeasures). That is the
 * driver's (wifid's) to do, and it must. */
int tkip_decifra(const unsigned char tk[16], const unsigned char mic[8],
                 const unsigned char ta[6], const unsigned char da[6],
                 const unsigned char sa[6], int priorita,
                 const unsigned char *in, unsigned int in_n, unsigned char *out,
                 unsigned long long *tsc);

/* The 16-byte RC4 key of one TKIP frame (phase 1 and 2 key mixing) */
void tkip_chiave_rc4(const unsigned char tk[16], const unsigned char ta[6],
                     unsigned int iv32, unsigned int iv16, unsigned char out[16]);
/* Michael over any bytes (the TKIP MIC is Michael over DA|SA|TID|0|0|0|data) */
void michael(const unsigned char chiave[8], const unsigned char *m, unsigned int n,
             unsigned char mic[8]);

/* WEP: `chiave` is 5 bytes (WEP-40) or 13 (WEP-104), `iv` should change at
 * every frame. out = IV (4) + encrypted [data + ICV (4)]. Returns n + 8. */
int wep_cifra(const unsigned char *chiave, unsigned int chiave_n,
              const unsigned char iv[3], int indice,
              const unsigned char *in, unsigned int n, unsigned char *out);
/* Returns the plain length (in_n - 8), or -1 when the ICV is wrong. */
int wep_decifra(const unsigned char *chiave, unsigned int chiave_n,
                const unsigned char *in, unsigned int in_n, unsigned char *out);

/* --- the small pieces, exported because the tests and the drivers use them -- */
void md5(const unsigned char *m, unsigned int n, unsigned char out[16]);
void hmac_md5(const unsigned char *k, unsigned int k_n,
              const unsigned char *m, unsigned int n, unsigned char out[16]);
void rc4(const unsigned char *chiave, unsigned int chiave_n, unsigned int salta,
         const unsigned char *in, unsigned char *out, unsigned int n);
unsigned int crc32_ieee(const unsigned char *m, unsigned int n);

#endif /* EXWIFI_H */
