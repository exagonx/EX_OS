/* =============================================================================
 * lib/extls/extls_kdf.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * HMAC-SHA256 e HKDF. Il perche' sta in extls.h; qui c'e' il come, e il come
 * e' quasi tutto nella RFC — 2104 per HMAC, 5869 per HKDF, 8446 per
 * l'etichetta di TLS 1.3.
 *
 * ! NIENTE LIBC, come exbig, exasn1 ed excert: si prova sull'host contro i
 * vettori pubblicati delle RFC, che sono il riferimento piu' solido che esista
 * — numeri stampati in uno standard, non prodotti da noi.
 * ============================================================================= */

#include "extls.h"

static void copia(unsigned char *d, const unsigned char *s, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) d[i] = s[i];
}

static void azzera(unsigned char *d, unsigned int n)
{
    unsigned int i;
    for (i = 0; i < n; i++) d[i] = 0;
}

/* =============================================================================
 * HMAC-SHA256
 *
 *     HMAC(K, m) = H((K' ^ opad) || H((K' ^ ipad) || m))
 *
 * ! LA CHIAVE PIU' LUNGA DEL BLOCCO SI ACCORCIA CON L'IMPRONTA, e non e' un
 * dettaglio di comodo: e' nella definizione. Chi lo salta ottiene un HMAC che
 * funziona benissimo — con se stesso, e con nessun altro al mondo.
 *
 * ! E IL MESSAGGIO PUO' ESSERE LUNGO, la chiave no. Qui il primo passaggio si
 * fa in due pezzi (il blocco interno e poi i dati) usando un buffer di
 * appoggio: e' il prezzo di non avere un'interfaccia a stati per SHA-256 nella
 * libc. Per TLS basta e avanza — i messaggi che si autenticano cosi' sono
 * impronte e segreti, mai flussi.
 * ============================================================================= */
#define DATI_MAX  512            /* quanto basta a TLS: impronte e segreti */

void extls_hmac(const unsigned char *chiave, unsigned int chiave_n,
                const unsigned char *dati, unsigned int dati_n,
                unsigned char out[EXTLS_IMPRONTA])
{
    unsigned char k[EXTLS_BLOCCO];
    unsigned char dentro[EXTLS_BLOCCO + DATI_MAX];
    unsigned char fuori[EXTLS_BLOCCO + EXTLS_IMPRONTA];
    unsigned int  i;

    if (dati_n > DATI_MAX) { azzera(out, EXTLS_IMPRONTA); return; }

    azzera(k, EXTLS_BLOCCO);
    if (chiave_n > EXTLS_BLOCCO) sha256(chiave, chiave_n, k);
    else                          copia(k, chiave, chiave_n);

    for (i = 0; i < EXTLS_BLOCCO; i++) dentro[i] = (unsigned char)(k[i] ^ 0x36);
    copia(dentro + EXTLS_BLOCCO, dati, dati_n);
    sha256(dentro, EXTLS_BLOCCO + dati_n, fuori + EXTLS_BLOCCO);

    for (i = 0; i < EXTLS_BLOCCO; i++) fuori[i] = (unsigned char)(k[i] ^ 0x5C);
    sha256(fuori, EXTLS_BLOCCO + EXTLS_IMPRONTA, out);
}

/* =============================================================================
 * HKDF — RFC 5869
 * ============================================================================= */
void extls_hkdf_extract(const unsigned char *sale, unsigned int sale_n,
                        const unsigned char *ikm, unsigned int ikm_n,
                        unsigned char prk[EXTLS_IMPRONTA])
{
    unsigned char zeri[EXTLS_IMPRONTA];

    /* ! SALE VUOTO VUOL DIRE UN BLOCCO DI ZERI, e non «niente sale»: lo dice
     * la RFC, ed e' il primo passo del key schedule di TLS 1.3. */
    if (sale == 0 || sale_n == 0) {
        azzera(zeri, EXTLS_IMPRONTA);
        extls_hmac(zeri, EXTLS_IMPRONTA, ikm, ikm_n, prk);
        return;
    }
    extls_hmac(sale, sale_n, ikm, ikm_n, prk);
}

int extls_hkdf_expand(const unsigned char prk[EXTLS_IMPRONTA],
                      const unsigned char *info, unsigned int info_n,
                      unsigned char *out, unsigned int out_n)
{
    unsigned char t[EXTLS_IMPRONTA];
    unsigned char blocco[EXTLS_IMPRONTA + 256 + 1];
    unsigned int  fatti = 0, t_n = 0, i;
    unsigned char contatore = 1;

    if (info_n > 256) return -1;
    /* Il tetto della RFC: 255 blocchi. Oltre, il contatore ricomincerebbe da
     * capo e due pezzi di chiave diversi sarebbero uguali. */
    if (out_n > 255u * EXTLS_IMPRONTA) return -1;

    while (fatti < out_n) {
        unsigned int n = 0, quanti;

        copia(blocco, t, t_n);          n += t_n;
        copia(blocco + n, info, info_n); n += info_n;
        blocco[n++] = contatore;

        extls_hmac(prk, EXTLS_IMPRONTA, blocco, n, t);
        t_n = EXTLS_IMPRONTA;

        quanti = out_n - fatti;
        if (quanti > EXTLS_IMPRONTA) quanti = EXTLS_IMPRONTA;
        for (i = 0; i < quanti; i++) out[fatti + i] = t[i];
        fatti += quanti;
        contatore++;
    }
    return 0;
}

/* =============================================================================
 * HKDF-Expand-Label — RFC 8446
 *
 *     struct {
 *         uint16 length;
 *         opaque label<7..255>   = "tls13 " + etichetta
 *         opaque context<0..255>
 *     } HkdfLabel;
 *
 * ! SBAGLIARNE UN BYTE NON DA' UN ERRORE, DA' CHIAVI DIVERSE. E chiavi diverse
 * dalle sue il server le segnala con «bad record mac», che non dice niente su
 * dove sia il difetto: e' il motivo per cui questa struttura si costruisce in
 * un posto solo e si prova contro un riferimento.
 * ============================================================================= */
int extls_expand_label(const unsigned char segreto[EXTLS_IMPRONTA],
                       const char *etichetta,
                       const unsigned char *contesto, unsigned int contesto_n,
                       unsigned char *out, unsigned int out_n)
{
    static const char PREFISSO[] = "tls13 ";
    unsigned char info[2 + 1 + 255 + 1 + 255];
    unsigned int  n = 0, lung = 0, i;

    while (etichetta[lung]) lung++;
    if (lung + 6 > 255 || contesto_n > 255) return -1;
    if (out_n > 0xFFFF) return -1;

    info[n++] = (unsigned char)(out_n >> 8);
    info[n++] = (unsigned char)out_n;
    info[n++] = (unsigned char)(6 + lung);
    for (i = 0; i < 6; i++) info[n++] = (unsigned char)PREFISSO[i];
    for (i = 0; i < lung; i++) info[n++] = (unsigned char)etichetta[i];
    info[n++] = (unsigned char)contesto_n;
    for (i = 0; i < contesto_n; i++) info[n++] = contesto[i];

    return extls_hkdf_expand(segreto, info, n, out, out_n);
}
