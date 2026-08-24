/* =============================================================================
 * lib/extls/extls_pss.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * RSA-PSS, solo verifica. EMSA-PSS-VERIFY della RFC 8017, con MGF1 e SHA-256.
 *
 * ! TLS 1.3 HA TOLTO PKCS#1 v1.5 DALLA CertificateVerify, e questo non e' un
 * doppione di quello che sta in lib/excert: sono due usi diversi e tutt'e due
 * necessari. I certificati sono firmati quasi sempre in v1.5; la firma che il
 * server fa sul dialogo in corso e' PSS. Un TLS che sa fare solo v1.5 non
 * completa nessun handshake 1.3.
 *
 * ! E PSS NON SI PUO' RICOSTRUIRE INTERO, perche' porta un sale casuale: si
 * ricostruisce l'IMPRONTA FINALE — che dal sale dipende — e si confronta
 * quella. Il resto della busta si controlla pezzo per pezzo, e ogni pezzo che
 * non torna e' un no.
 * ============================================================================= */

#include "extls.h"
#include "exbig.h"

#define HLEN  EXTLS_IMPRONTA

/* =============================================================================
 * MGF1: da un'impronta, quanti byte si vogliono
 *
 * ! IL CONTATORE E' A QUATTRO BYTE, ORDINE DI RETE, e non e' un dettaglio
 * libero: e' nella definizione, e chi lo scrive a un byte ottiene una maschera
 * diversa da quella di tutti gli altri — cioe' una firma che non verifica mai,
 * senza un errore che dica perche'.
 * ============================================================================= */
static void mgf1(const unsigned char *seme, unsigned int seme_n,
                 unsigned char *out, unsigned int out_n)
{
    unsigned char buf[HLEN + 4];
    unsigned char imp[HLEN];
    unsigned int  fatti = 0, contatore = 0, i;

    for (i = 0; i < seme_n && i < HLEN; i++) buf[i] = seme[i];

    while (fatti < out_n) {
        unsigned int quanti;

        buf[seme_n + 0] = (unsigned char)(contatore >> 24);
        buf[seme_n + 1] = (unsigned char)(contatore >> 16);
        buf[seme_n + 2] = (unsigned char)(contatore >> 8);
        buf[seme_n + 3] = (unsigned char)contatore;

        sha256(buf, seme_n + 4, imp);

        quanti = out_n - fatti;
        if (quanti > HLEN) quanti = HLEN;
        for (i = 0; i < quanti; i++) out[fatti + i] = imp[i];
        fatti += quanti;
        contatore++;
    }
}

int extls_rsa_pss_verifica(const unsigned char *modulo, unsigned int modulo_n,
                           const unsigned char *esponente, unsigned int esp_n,
                           const unsigned char impronta[HLEN],
                           const unsigned char *firma, unsigned int firma_n,
                           int sale_atteso)
{
    ExBig n, e, s, r;
    unsigned char em[EXBIG_PAROLE * 4];
    unsigned char db[EXBIG_PAROLE * 4];
    unsigned char maschera[EXBIG_PAROLE * 4];
    unsigned char m_primo[8 + HLEN + EXBIG_PAROLE * 4];
    unsigned char h_primo[HLEN];
    unsigned int  em_bit, em_len, db_len, i, sale_n, pos;
    unsigned int  diverso = 0;

    if (modulo == 0 || firma == 0 || impronta == 0) return -1;

    if (exbig_da_byte(&n, modulo, modulo_n) != 0) return -1;
    if (exbig_da_byte(&e, esponente, esp_n) != 0) return -1;
    if (exbig_da_byte(&s, firma, firma_n) != 0) return -1;

    /* ! emBits E' UN BIT IN MENO DEI BIT DEL MODULO, e da li' scende tutto il
     * resto: la lunghezza della busta e quanti bit del primo byte devono
     * essere zero. Sbagliarlo di uno su un modulo la cui misura in bit non e'
     * multipla di otto vuol dire rifiutare firme buone — e succede solo su
     * ALCUNE chiavi, che e' il modo peggiore di sbagliare. */
    em_bit = exbig_bit(&n) - 1;
    em_len = (em_bit + 7) / 8;
    if (em_len < HLEN + 2 || em_len > sizeof(em)) return -1;

    if (exbig_modexp(&r, &s, &e, &n) != 0) return -1;
    if (exbig_a_byte(&r, em, em_len) != 0) return -1;

    if (em[em_len - 1] != 0xBC) return -1;

    db_len = em_len - HLEN - 1;

    /* I bit di troppo in cima devono essere zero. */
    {
        unsigned int avanzo = 8 * em_len - em_bit;

        if (avanzo > 0 && avanzo < 8) {
            if ((em[0] >> (8 - avanzo)) != 0) return -1;
        } else if (avanzo == 8) {
            if (em[0] != 0) return -1;
        }
    }

    mgf1(em + db_len, HLEN, maschera, db_len);
    for (i = 0; i < db_len; i++) db[i] = (unsigned char)(em[i] ^ maschera[i]);

    /* Gli stessi bit, azzerati anche nel DB smascherato. */
    {
        unsigned int avanzo = 8 * em_len - em_bit;

        if (avanzo >= 8) db[0] = 0;
        else if (avanzo > 0) db[0] &= (unsigned char)(0xFF >> avanzo);
    }

    /* DB = 00..00 || 01 || sale */
    pos = 0;
    while (pos < db_len && db[pos] == 0) pos++;
    if (pos >= db_len || db[pos] != 0x01) return -1;
    pos++;
    sale_n = db_len - pos;

    if (sale_atteso >= 0 && sale_n != (unsigned int)sale_atteso) return -1;

    /* M' = 00 00 00 00 00 00 00 00 || impronta || sale ; H' = SHA-256(M') */
    for (i = 0; i < 8; i++) m_primo[i] = 0;
    for (i = 0; i < HLEN; i++) m_primo[8 + i] = impronta[i];
    for (i = 0; i < sale_n; i++) m_primo[8 + HLEN + i] = db[pos + i];

    sha256(m_primo, 8 + HLEN + sale_n, h_primo);

    /* ! IL CONFRONTO NON ESCE AL PRIMO BYTE DIVERSO, per la stessa ragione
     * scritta in lib/excert: qui i dati sono pubblici, ma un confronto che
     * esce presto e' un'abitudine che prima o poi finisce dove conta. */
    for (i = 0; i < HLEN; i++)
        diverso |= (unsigned int)(h_primo[i] ^ em[db_len + i]);

    return diverso == 0 ? 0 : -1;
}
