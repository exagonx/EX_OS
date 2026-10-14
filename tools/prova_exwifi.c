/* =============================================================================
 * tools/prova_exwifi.c — lib/exwifi on the host, against published vectors
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Built and run by tools/prova_exwifi.sh. Vectors: RFC 1321 / 2202 (MD5,
 * HMAC-MD5), RFC 3394 (key wrap), IEEE 802.11-2012 Annex M (passphrase to
 * PSK, PRF, CCMP, TKIP mixing, Michael), the classic RC4 and CRC-32 checks.
 * The handshakes have no vector of their own: a small authenticator below
 * plays the access point (built from the same primitives, so it proves the
 * frames and the state machine, not the primitives - those have vectors).
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include "excrypt.h"
#include "exwifi.h"

static int g_ok = 0, g_ko = 0;

static void esito(const char *cosa, int buono)
{
    printf("  %-48s %s\n", cosa, buono ? "ok" : "SBAGLIATO");
    if (buono) g_ok++; else g_ko++;
}

static int hex(const char *s, unsigned char *out)
{
    int n = 0;
    unsigned int v;

    while (*s) {
        if (*s == ' ' || *s == '-' || *s == ':') { s++; continue; }
        if (sscanf(s, "%2x", &v) != 1) break;
        out[n++] = (unsigned char)v;
        s += 2;
    }
    return n;
}

static int uguale(const unsigned char *a, const char *atteso)
{
    unsigned char b[512];
    int n = hex(atteso, b);

    return memcmp(a, b, (size_t)n) == 0;
}

/* --- a tiny authenticator ------------------------------------------------------ */
static unsigned int ap_frame(int rsn, unsigned int info, unsigned int klung, unsigned long long replay,
                             const unsigned char *nonce, const unsigned char *iv,
                             const unsigned char *dati, unsigned int dati_n, unsigned char *f)
{
    unsigned int n = 99 + dati_n, i;

    memset(f, 0, n);
    f[0] = 2; f[1] = 3; f[2] = (unsigned char)((n - 4) >> 8); f[3] = (unsigned char)(n - 4);
    f[4] = rsn ? 2 : 254;
    f[5] = (unsigned char)(info >> 8); f[6] = (unsigned char)info;
    f[7] = (unsigned char)(klung >> 8); f[8] = (unsigned char)klung;
    for (i = 0; i < 8; i++) f[9 + i] = (unsigned char)(replay >> (56 - 8 * i));
    if (nonce) memcpy(f + 17, nonce, 32);
    if (iv) memcpy(f + 49, iv, 16);
    f[97] = (unsigned char)(dati_n >> 8); f[98] = (unsigned char)dati_n;
    memcpy(f + 99, dati, dati_n);
    return n;
}

static void ap_firma(int versione, const unsigned char kck[16], unsigned char *f, unsigned int n)
{
    memset(f + 81, 0, 16);
    eapol_mic(versione, kck, f, n, f + 81);
}

static int ap_mic_giusto(int versione, const unsigned char kck[16], const unsigned char *f, unsigned int n)
{
    unsigned char c[600], m[16];

    memcpy(c, f, n);
    memset(c + 81, 0, 16);
    eapol_mic(versione, kck, c, n, m);
    return memcmp(m, f + 81, 16) == 0;
}

static void prova_stretta(int rsn, int cifrario, const char *parola_ap, const char *parola_cl)
{
    static const unsigned char aa[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
    static const unsigned char spa[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    unsigned char  pmk_ap[32], pmk_cl[32], anonce[32], snonce[32], f[600], r[EAPOL_MAX], gtk[32], kd[128], w[136];
    unsigned char  ie[24] = { 0x30, 0x14, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 2, 0, 0 };
    unsigned int   n, rn, versione = cifrario == WPA_CIFRARIO_CCMP ? 2 : 1, gtk_n = cifrario == WPA_CIFRARIO_CCMP ? 16 : 32, i;
    WpaSupplicante s;
    WpaPtk         ptk;
    int            esito_r;
    char           nome[80];
    const char    *tipo = rsn ? "WPA2" : "WPA";
    int            sbagliata = strcmp(parola_ap, parola_cl) != 0;

    for (i = 0; i < 32; i++) { anonce[i] = (unsigned char)(0x10 + i); snonce[i] = (unsigned char)(0x80 + i); gtk[i] = (unsigned char)(0xC0 + i); }
    wpa_pmk(parola_ap, (const unsigned char *)"EXOS-Casa", 9, pmk_ap);
    wpa_pmk(parola_cl, (const unsigned char *)"EXOS-Casa", 9, pmk_cl);
    wpa_inizia(&s, pmk_cl, aa, spa, rsn, cifrario, ie, 22, snonce);

    /* message 1 */
    n = ap_frame(rsn, versione | EAPOL_KI_COPPIA | EAPOL_KI_ACK, cifrario == WPA_CIFRARIO_CCMP ? 16 : 32, 1, anonce, 0, 0, 0, f);
    esito_r = wpa_ricevi(&s, f, n, r, &rn);
    snprintf(nome, sizeof(nome), "%s %s: messaggio 1 -> 2", tipo, cifrario == WPA_CIFRARIO_CCMP ? "CCMP" : "TKIP");
    wpa_ptk(pmk_ap, aa, spa, anonce, r + 17, cifrario, &ptk);
    if (sbagliata) {
        snprintf(nome, sizeof(nome), "%s: password sbagliata, l'AP rifiuta il 2", tipo);
        /* the access point finds the MIC of message 2 wrong: that is how a
         * wrong password shows up, and the AP gives up there */
        esito(nome, esito_r == WPA_RISPONDI && !ap_mic_giusto((int)versione, ptk.kck, r, rn));
        return;
    }
    esito(nome, esito_r == WPA_RISPONDI && ap_mic_giusto((int)versione, ptk.kck, r, rn) &&
                memcmp(r + 99, ie, 22) == 0 && memcmp(r + 17, snonce, 32) == 0);

    /* message 3 */
    if (rsn) {
        unsigned int k = 0;
        memcpy(kd, ie, 22); k = 22;
        kd[k++] = 0xdd; kd[k++] = (unsigned char)(6 + gtk_n);
        kd[k++] = 0x00; kd[k++] = 0x0f; kd[k++] = 0xac; kd[k++] = 1; kd[k++] = 1; kd[k++] = 0;
        memcpy(kd + k, gtk, gtk_n); k += gtk_n;
        if (k % 8) { kd[k++] = 0xdd; while (k % 8) kd[k++] = 0; }  /* 802.11 padding */
        if (versione == 2) { aes_wrap(ptk.kek, kd, k, w); n = ap_frame(1, versione | EAPOL_KI_COPPIA | EAPOL_KI_ACK | EAPOL_KI_MIC | EAPOL_KI_INSTALLA | EAPOL_KI_SICURO | EAPOL_KI_CIFRATI, 16, 2, anonce, 0, w, k + 8, f); }
        else {
            unsigned char iv[16], kk[32];
            for (i = 0; i < 16; i++) iv[i] = (unsigned char)(0x33 + i);
            memcpy(kk, iv, 16); memcpy(kk + 16, ptk.kek, 16);
            rc4(kk, 32, 256, kd, w, k);
            n = ap_frame(1, versione | EAPOL_KI_COPPIA | EAPOL_KI_ACK | EAPOL_KI_MIC | EAPOL_KI_INSTALLA | EAPOL_KI_SICURO | EAPOL_KI_CIFRATI, 32, 2, anonce, iv, w, k, f);
        }
    } else {
        n = ap_frame(0, versione | EAPOL_KI_COPPIA | EAPOL_KI_ACK | EAPOL_KI_MIC | EAPOL_KI_INSTALLA, 32, 2, anonce, 0, ie, 22, f);
    }
    f[65] = 0x07;                                                /* an RSC to see come back */
    ap_firma((int)versione, ptk.kck, f, n);
    esito_r = wpa_ricevi(&s, f, n, r, &rn);
    snprintf(nome, sizeof(nome), "%s %s: messaggio 3 -> 4, chiavi", tipo, cifrario == WPA_CIFRARIO_CCMP ? "CCMP" : "TKIP");
    esito(nome, esito_r == WPA_COPPIA && ap_mic_giusto((int)versione, ptk.kck, r, rn) &&
                memcmp(s.ptk.tk, ptk.tk, ptk.tk_n) == 0 && s.ptk.tk_n == ptk.tk_n &&
                (!rsn || (s.gtk_pronta && s.gtk_n == gtk_n && memcmp(s.gtk, gtk, gtk_n) == 0 && s.gtk_indice == 1 && s.gtk_rsc[0] == 7)) &&
                (((r[5] << 8 | r[6]) & EAPOL_KI_SICURO) != 0) == (rsn != 0));

    /* the same message 3 played again: refused */
    esito_r = wpa_ricevi(&s, f, n, r, &rn);
    snprintf(nome, sizeof(nome), "%s: messaggio 3 ripetuto rifiutato", tipo);
    esito(nome, esito_r == WPA_ERR_REPLAY);

    /* a bit flipped in a signed frame: refused */
    f[30] ^= 1; f[16] = 9;
    esito_r = wpa_ricevi(&s, f, n, r, &rn);
    snprintf(nome, sizeof(nome), "%s: messaggio 3 alterato rifiutato", tipo);
    esito(nome, esito_r == WPA_ERR_MIC || esito_r == WPA_ERR_NONCE);

    /* group key handshake (WPA: here comes the GTK) */
    s.gtk_pronta = 0;
    for (i = 0; i < 32; i++) gtk[i] = (unsigned char)(0xA0 + i);
    if (rsn) {
        unsigned int k = 0;
        kd[k++] = 0xdd; kd[k++] = (unsigned char)(6 + gtk_n);
        kd[k++] = 0x00; kd[k++] = 0x0f; kd[k++] = 0xac; kd[k++] = 1; kd[k++] = 2; kd[k++] = 0;
        memcpy(kd + k, gtk, gtk_n); k += gtk_n;
        if (k % 8) { kd[k++] = 0xdd; while (k % 8) kd[k++] = 0; }
        if (versione == 2) { aes_wrap(ptk.kek, kd, k, w); n = ap_frame(1, versione | EAPOL_KI_ACK | EAPOL_KI_MIC | EAPOL_KI_SICURO | EAPOL_KI_CIFRATI, 0, 3, 0, 0, w, k + 8, f); }
        else {
            unsigned char iv[16], kk[32];
            for (i = 0; i < 16; i++) iv[i] = (unsigned char)(0x44 + i);
            memcpy(kk, iv, 16); memcpy(kk + 16, ptk.kek, 16);
            rc4(kk, 32, 256, kd, w, k);
            n = ap_frame(1, versione | EAPOL_KI_ACK | EAPOL_KI_MIC | EAPOL_KI_SICURO | EAPOL_KI_CIFRATI, 0, 3, 0, iv, w, k, f);
        }
    } else {
        unsigned char iv[16], kk[32];
        for (i = 0; i < 16; i++) iv[i] = (unsigned char)(0x55 + i);
        memcpy(kk, iv, 16); memcpy(kk + 16, ptk.kek, 16);
        rc4(kk, 32, 256, gtk, w, gtk_n);
        n = ap_frame(0, versione | EAPOL_KI_ACK | EAPOL_KI_MIC | EAPOL_KI_SICURO | (2 << 4), gtk_n, 3, 0, iv, w, gtk_n, f);
    }
    ap_firma((int)versione, ptk.kck, f, n);
    esito_r = wpa_ricevi(&s, f, n, r, &rn);
    snprintf(nome, sizeof(nome), "%s %s: chiave di gruppo 1 -> 2", tipo, cifrario == WPA_CIFRARIO_CCMP ? "CCMP" : "TKIP");
    esito(nome, esito_r == WPA_GRUPPO && ap_mic_giusto((int)versione, ptk.kck, r, rn) &&
                s.gtk_pronta && s.gtk_n == gtk_n && memcmp(s.gtk, gtk, gtk_n) == 0 && s.gtk_indice == 2);
}

int main(void)
{
    unsigned char o[256], b[256], c[256];
    int n;

    printf("lib/exwifi, vettori di prova:\n");

    /* MD5, HMAC-MD5 */
    md5((const unsigned char *)"abc", 3, o);
    esito("MD5 \"abc\" (RFC 1321)", uguale(o, "900150983cd24fb0d6963f7d28e17f72"));
    md5((const unsigned char *)"12345678901234567890123456789012345678901234567890123456789012345678901234567890", 80, o);
    esito("MD5 80 cifre (RFC 1321)", uguale(o, "57edf4a22be3c955ac49da2e2107b67a"));
    memset(b, 0x0b, 16);
    hmac_md5(b, 16, (const unsigned char *)"Hi There", 8, o);
    esito("HMAC-MD5 caso 1 (RFC 2202)", uguale(o, "9294727a3638bb1c13f48ef8158bfc9d"));
    hmac_md5((const unsigned char *)"Jefe", 4, (const unsigned char *)"what do ya want for nothing?", 28, o);
    esito("HMAC-MD5 caso 2 (RFC 2202)", uguale(o, "750c783e6ab0b503eaa86e310a5db738"));

    /* RC4, CRC-32 */
    rc4((const unsigned char *)"Key", 3, 0, (const unsigned char *)"Plaintext", o, 9);
    esito("RC4 Key/Plaintext", uguale(o, "bbf316e8d940af0ad3"));
    rc4((const unsigned char *)"Secret", 6, 0, (const unsigned char *)"Attack at dawn", o, 14);
    esito("RC4 Secret/Attack at dawn", uguale(o, "45a01f645fc35b383552544b9bf5"));
    esito("CRC-32 \"123456789\"", crc32_ieee((const unsigned char *)"123456789", 9) == 0xCBF43926u);

    /* passphrase -> PSK, PRF */
    wpa_pmk("password", (const unsigned char *)"IEEE", 4, o);
    esito("PSK password/IEEE (802.11 M.4)", uguale(o, "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e"));
    wpa_pmk("ThisIsAPassword", (const unsigned char *)"ThisIsASSID", 11, o);
    esito("PSK ThisIsAPassword/ThisIsASSID (M.4)", uguale(o, "0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af"));
    memset(b, 0x0b, 20);
    wpa_prf(b, 20, "prefix", (const unsigned char *)"Hi There", 8, o, 64);
    esito("PRF-512 \"prefix\" (802.11 M.3)", uguale(o,
        "bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606e17d8da35402ffee"
        "75df78c3d31e0f889f012120c0862beb67753e7439ae242edb8373698356cf5a"));

    /* AES key wrap */
    hex("000102030405060708090A0B0C0D0E0F", b);
    hex("00112233445566778899AABBCCDDEEFF", c);
    aes_wrap(b, c, 16, o);
    esito("AES key wrap (RFC 3394 4.1)", uguale(o, "1FA68B0A8112B447AEF34BD8FB5A7B829D3E862371D2CFE5"));
    esito("AES key unwrap (RFC 3394 4.1)", aes_unwrap(b, o, 24, c + 32) == 0 && memcmp(c, c + 32, 16) == 0);
    o[5] ^= 1;
    esito("AES key unwrap, dato alterato rifiutato", aes_unwrap(b, o, 24, c + 32) == -1);

    /* CCMP, 802.11-2012 M.6.4 */
    {
        unsigned char tk[16], hdr[24], pt[20], out[64], back[64];
        unsigned long long pn = 0;

        hex("c97c1f67ce371185514a8a19f2bdd52f", tk);
        hex("08 48 c3 2c 0f d2 e1 28 a5 7c 50 30 f1 84 44 08 ab ae a5 b8 fc ba 80 33", hdr);
        hex("f8 ba 1a 55 d0 2f 85 ae 96 7b b6 2f b6 cd a8 eb 7e 78 a0 50", pt);
        n = ccmp_cifra(tk, hdr, 24, 0xB5039776E70CULL, 0, pt, 20, out);
        esito("CCMP cifratura (802.11 M.6.4)", n == 36 && uguale(out,
            "0c e7 00 20 76 97 03 b5 f3 d0 a2 fe 9a 3d bf 23 42 a6 43 e4 32 46 e8 0c 3c 04 d0 19 78 45 ce 0b 16 f9 76 23"));
        n = ccmp_decifra(tk, hdr, 24, out, 36, back, &pn);
        esito("CCMP decifratura e PN", n == 20 && memcmp(back, pt, 20) == 0 && pn == 0xB5039776E70CULL);
        out[20] ^= 0x01;
        esito("CCMP, un bit cambiato rifiutato", ccmp_decifra(tk, hdr, 24, out, 36, back, &pn) == -1);
    }

    /* TKIP key mixing and Michael, 802.11 M.6.1 / M.6.3 */
    {
        unsigned char tk[16], ta[6];

        hex("000102030405060708090a0b0c0d0e0f", tk);
        hex("102233445566", ta);
        tkip_chiave_rc4(tk, ta, 0, 0, o);
        esito("TKIP chiave RC4, IV16 0000", uguale(o, "00200033ea8d2f60ca6d1374234a660b"));
        tkip_chiave_rc4(tk, ta, 0, 1, o);
        esito("TKIP chiave RC4, IV16 0001", uguale(o, "00200190ffdc314389a9d9d074fd20aa"));
        hex("63893b250840b8ae0bd0fa7e61d2783e", tk);
        hex("64f2eaeddc25", ta);
        tkip_chiave_rc4(tk, ta, 0x20DCFD43, 0xFFFF, o);
        esito("TKIP chiave RC4, IV32 20DCFD43 IV16 FFFF", uguale(o, "ff7fff93810fc6e58f5dd3262515 44ce"));
    }
    {
        static const struct { const char *k, *m, *mic; } v[] = {
            { "0000000000000000", "",        "82925c1ca1d130b8" },
            { "82925c1ca1d130b8", "M",       "434721ca40639b3f" },
            { "434721ca40639b3f", "Mi",      "e8f9becae97e5d29" },
            { "e8f9becae97e5d29", "Mic",     "90038fc6cf13c1db" },
            { "90038fc6cf13c1db", "Mich",    "d55e100510128986" },
            { "d55e100510128986", "Michael", "0a942b124ecaa546" } };
        int i, buoni = 0;

        for (i = 0; i < 6; i++) {
            hex(v[i].k, b);
            michael(b, (const unsigned char *)v[i].m, (unsigned int)strlen(v[i].m), o);
            buoni += uguale(o, v[i].mic);
        }
        esito("Michael, 6 vettori (802.11 M.6.1)", buoni == 6);
    }

    /* TKIP and WEP round trips, and their refusals */
    {
        unsigned char tk[16], mic[8], ta[6] = { 2, 0, 0, 0, 0, 1 }, da[6] = { 1, 2, 3, 4, 5, 6 }, sa[6] = { 6, 5, 4, 3, 2, 1 };
        unsigned char msg[40], out[80], back[80];
        unsigned long long tsc = 0;
        int i;

        for (i = 0; i < 16; i++) tk[i] = (unsigned char)(i * 7);
        for (i = 0; i < 8; i++) mic[i] = (unsigned char)(i * 13);
        for (i = 0; i < 40; i++) msg[i] = (unsigned char)i;
        n = tkip_cifra(tk, mic, ta, da, sa, 0, 0x123456789AULL, 1, msg, 40, out);
        esito("TKIP andata e ritorno", n == 60 && tkip_decifra(tk, mic, ta, da, sa, 0, out, 60, back, &tsc) == 40 &&
                                       memcmp(back, msg, 40) == 0 && tsc == 0x123456789AULL);
        mic[0] ^= 1;
        esito("TKIP, Michael sbagliato = -2 (contromisure)", tkip_decifra(tk, mic, ta, da, sa, 0, out, 60, back, &tsc) == -2);
        out[30] ^= 1;
        esito("TKIP, ICV sbagliato = -1", tkip_decifra(tk, mic, ta, da, sa, 0, out, 60, back, &tsc) == -1);

        n = wep_cifra((const unsigned char *)"12345", 5, (const unsigned char *)"\x01\x02\x03", 0, msg, 40, out);
        esito("WEP-40 andata e ritorno", n == 48 && wep_decifra((const unsigned char *)"12345", 5, out, 48, back) == 40 &&
                                         memcmp(back, msg, 40) == 0);
        esito("WEP, chiave sbagliata rifiutata", wep_decifra((const unsigned char *)"12346", 5, out, 48, back) == -1);
    }

    /* the handshakes */
    prova_stretta(1, WPA_CIFRARIO_CCMP, "casa1234", "casa1234");
    prova_stretta(0, WPA_CIFRARIO_TKIP, "casa1234", "casa1234");
    prova_stretta(1, WPA_CIFRARIO_TKIP, "casa1234", "casa1234");
    prova_stretta(1, WPA_CIFRARIO_CCMP, "casa1234", "casa4321");

    printf("%d giuste, %d sbagliate\n", g_ok, g_ko);
    return g_ko ? 1 : 0;
}
