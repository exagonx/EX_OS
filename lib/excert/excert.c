/* =============================================================================
 * lib/excert/excert.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La catena: il come. Il perche' — e cosa vuol dire «verificata» — sta in
 * excert.h.
 *
 * ! NIENTE LIBC, come exbig e exasn1: cosi' si prova sull'host contro una
 * PKI vera costruita con openssl, invece che dentro una macchina virtuale
 * dove ogni riga cambiata costa un avvio.
 * ============================================================================= */

#include "excert.h"
#include "exbig.h"

/* =============================================================================
 * PKCS#1 v1.5: la busta che avvolge l'impronta
 *
 *     00 01 FF FF ... FF 00 || DigestInfo(impronta)
 *
 * ! SI RICOSTRUISCE E SI CONFRONTA, NON SI ANALIZZA. Guardare dentro la busta
 * con un lettore — leggere il DigestInfo, tirarne fuori l'OID, poi l'impronta
 * — vuol dire accettare tutto cio' che quel lettore lascia passare: byte in
 * piu' in coda, lunghezze scritte lunghe, parametri diversi. Sono le firme di
 * Bleichenbacher del 2006, e funzionavano proprio contro chi analizzava invece
 * di confrontare.
 *
 * Qui si costruisce la busta che DEVE esserci e si confrontano tutti i byte.
 * Se ne manca uno la firma non e' valida, e non c'e' niente da interpretare.
 * ============================================================================= */

/* DigestInfo con l'OID di SHA-256 e la lunghezza fissa: e' una costante. */
static const unsigned char DIGESTINFO_SHA256[] = {
    0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,
    0x05,0x00,0x04,0x20
};

static int confronta(const unsigned char *a, const unsigned char *b,
                     unsigned int n)
{
    unsigned int i, diverso = 0;

    /* ! IL CONFRONTO NON SI FERMA AL PRIMO BYTE DIVERSO. Qui si guardano dati
     * pubblici e il tempo non direbbe niente di segreto, ma un confronto che
     * esce presto e' un'abitudine che prima o poi finisce dove conta. */
    for (i = 0; i < n; i++) diverso |= (unsigned int)(a[i] ^ b[i]);
    return diverso == 0;
}

int excert_firma_valida(const ExCert *figlio, const ExCert *padre)
{
    ExBig firma, esp, modulo, risultato;
    unsigned char busta[EXBIG_PAROLE * 4];
    unsigned char atteso[EXBIG_PAROLE * 4];
    unsigned char impronta[32];
    unsigned int  k, i, riempimento;

    if (figlio == 0 || padre == 0) return EXCERT_MALFORMATO;

    /* ! L'ALGORITMO SI GUARDA PRIMA DI TUTTO, e SHA-1 si rifiuta per nome: le
     * collisioni si comprano, e una firma di CA e' esattamente il posto dove
     * servono a chi attacca. */
    if (figlio->alg_firma == EXASN1_ALG_RSA_SHA1) return EXCERT_ALG_RIFIUTATO;
    if (figlio->alg_firma != EXASN1_ALG_RSA_SHA256) return EXCERT_ALG_RIFIUTATO;
    if (padre->tipo_chiave != EXASN1_CHIAVE_RSA) return EXCERT_ALG_RIFIUTATO;

    if (figlio->tbs.p == 0 || figlio->firma.p == 0) return EXCERT_MALFORMATO;
    if (padre->chiave_modulo.p == 0) return EXCERT_MALFORMATO;

    k = padre->chiave_modulo.n;                 /* la busta e' lunga quanto il modulo */
    if (k < 64 || k > sizeof(busta)) return EXCERT_MALFORMATO;
    if (figlio->firma.n > k) return EXCERT_MALFORMATO;

    if (exbig_da_byte(&modulo, padre->chiave_modulo.p, padre->chiave_modulo.n) != 0)
        return EXCERT_MALFORMATO;
    if (exbig_da_byte(&esp, padre->chiave_esponente.p, padre->chiave_esponente.n) != 0)
        return EXCERT_MALFORMATO;
    if (exbig_da_byte(&firma, figlio->firma.p, figlio->firma.n) != 0)
        return EXCERT_MALFORMATO;

    if (exbig_modexp(&risultato, &firma, &esp, &modulo) != 0)
        return EXCERT_FIRMA_SBAGLIATA;          /* firma >= modulo: malformata */

    if (exbig_a_byte(&risultato, busta, k) != 0) return EXCERT_FIRMA_SBAGLIATA;

    /* La busta attesa, costruita byte per byte. */
    riempimento = k - 3 - (unsigned int)sizeof(DIGESTINFO_SHA256) - 32;
    if (riempimento < 8) return EXCERT_MALFORMATO;   /* la RFC ne vuole almeno 8 */

    sha256(figlio->tbs.p, figlio->tbs.n, impronta);

    i = 0;
    atteso[i++] = 0x00;
    atteso[i++] = 0x01;
    while (riempimento--) atteso[i++] = 0xFF;
    atteso[i++] = 0x00;
    for (riempimento = 0; riempimento < sizeof(DIGESTINFO_SHA256); riempimento++)
        atteso[i++] = DIGESTINFO_SHA256[riempimento];
    for (riempimento = 0; riempimento < 32; riempimento++)
        atteso[i++] = impronta[riempimento];

    if (i != k) return EXCERT_MALFORMATO;
    if (!confronta(busta, atteso, k)) return EXCERT_FIRMA_SBAGLIATA;
    return EXCERT_OK;
}

/* =============================================================================
 * Le date
 *
 * ! SI CONFRONTANO COME STRINGHE, e per questo exasn1 le normalizza tutte a
 * «AAAAMMGGhhmmssZ»: due secoli scritti in due modi diversi diventano la stessa
 * forma, e allora il confronto e' quello dei caratteri — nessuna aritmetica
 * sui mesi, nessun anno bisestile, nessun fuso. Le date dei certificati sono
 * in UTC per definizione.
 * ============================================================================= */
static int prima(const char *a, const char *b)
{
    unsigned int i;

    for (i = 0; i < 14; i++) {
        if (a[i] != b[i]) return a[i] < b[i];
        if (a[i] == 0) break;
    }
    return 0;
}

static int date_a_posto(const ExCert *c, const char *adesso)
{
    if (adesso == 0 || adesso[0] == 0) return EXCERT_OK;
    if (prima(adesso, c->non_prima)) return EXCERT_NON_ANCORA;
    if (prima(c->non_dopo, adesso)) return EXCERT_SCADUTO;
    return EXCERT_OK;
}

/* =============================================================================
 * Il magazzino
 * ============================================================================= */
int excert_magazzino_aggiungi(ExMagazzino *m, const unsigned char *der,
                              unsigned int len)
{
    if (m == 0) return EXCERT_MALFORMATO;
    if (m->n >= EXCERT_MAGAZZINO_MAX) return EXCERT_TROPPO_LUNGA;
    if (excert_analizza(der, len, &m->cert[m->n]) != 0) return EXCERT_MALFORMATO;
    m->n++;
    return EXCERT_OK;
}

/* Cerca nel magazzino chi ha emesso `c`, e verifica davvero la firma.
 *
 * ! NON BASTA CHE IL NOME COMBACI. Il nome dell'emittente lo scrive chi manda
 * il certificato: cercare per nome e fermarsi li' vorrebbe dire farsi
 * indicare da chi attacca quale radice usare. Il nome serve a SCEGLIERE il
 * candidato — sono duecento — e poi si fa il conto. */
static const ExCert *radice_di(const ExCert *c, const ExMagazzino *m)
{
    unsigned int i;

    for (i = 0; i < m->n; i++) {
        if (!excert_stesso_nome(&c->emittente, &m->cert[i].soggetto)) continue;
        if (!m->cert[i].e_ca) continue;
        if (excert_firma_valida(c, &m->cert[i]) == EXCERT_OK) return &m->cert[i];
    }
    return 0;
}

int excert_catena_valida(const ExCert *catena, unsigned int quanti,
                         const ExMagazzino *magazzino, const char *adesso)
{
    unsigned int i;
    int          r;

    if (catena == 0 || magazzino == 0 || quanti == 0) return EXCERT_MALFORMATO;
    if (quanti > 10) return EXCERT_TROPPO_LUNGA;    /* nessuna catena vera e' cosi' */

    /* Le date di TUTTI gli anelli, compresi gli intermedi: un intermedio
     * scaduto e' una CA che ha smesso di essere difesa. */
    for (i = 0; i < quanti; i++) {
        r = date_a_posto(&catena[i], adesso);
        if (r != EXCERT_OK) return r;
    }

    /* Da un anello al successivo. */
    for (i = 0; i + 1 < quanti; i++) {
        const ExCert *figlio = &catena[i], *padre = &catena[i + 1];

        if (!excert_stesso_nome(&figlio->emittente, &padre->soggetto))
            return EXCERT_NOME_DIVERSO;

        /* ! CHI FIRMA DEV'ESSERE UNA CA, e questo controllo e' il piu' vecchio
         * dei difetti di X.509: senza, il certificato di un sito qualunque —
         * che chiunque puo' farsi rilasciare — puo' firmare il certificato di
         * un altro sito. */
        if (!padre->e_ca) return EXCERT_NON_E_CA;

        r = excert_firma_valida(figlio, padre);
        if (r != EXCERT_OK) return r;
    }

    /* L'ultimo anello dev'essere firmato da una radice DEL MAGAZZINO. */
    if (radice_di(&catena[quanti - 1], magazzino) == 0) return EXCERT_SENZA_RADICE;

    return EXCERT_OK;
}
