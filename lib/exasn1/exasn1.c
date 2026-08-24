/* =============================================================================
 * lib/exasn1/exasn1.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * DER e X.509: il come. Il perche' — e con quale diffidenza — sta in exasn1.h.
 *
 * ! NIENTE LIBC, come exbig e come il lettore TrueType: legge byte e rende
 * fette, non fa syscall e non alloca. E' cio' che permette di provarlo
 * SULL'HOST contro `openssl`, su tutti i certificati veri che questa macchina
 * ha in /etc/ssl/certs — un riferimento che non abbiamo scritto noi.
 * ============================================================================= */

#include "exasn1.h"

#define TAG_INTERO        0x02
#define TAG_BITSTRING     0x03
#define TAG_OCTETSTRING   0x04
#define TAG_NULL          0x05
#define TAG_OID           0x06
#define TAG_UTF8          0x0C
#define TAG_SEQUENZA      0x30
#define TAG_INSIEME       0x31
#define TAG_UTCTIME       0x17
#define TAG_GENTIME       0x18
#define TAG_BOOLEANO      0x01

/* =============================================================================
 * Il lettore
 *
 * ! OGNI CONTROLLO E' PRIMA DELL'ACCESSO, e in questo file non e' pedanteria:
 * la lunghezza la scrive il mittente. «off + n» puo' anche TRABOCCARE, quindi
 * i confronti si scrivono come sottrazioni sul residuo — `n > d->n - off` — e
 * mai come somme.
 * ============================================================================= */
int exder_leggi(const ExDer *d, unsigned int off, ExDerElem *out)
{
    unsigned int lung, byte_lung, i;

    if (d == 0 || d->p == 0 || out == 0) return -1;
    if (off >= d->n || d->n - off < 2) return -1;   /* serve tag + lunghezza */

    out->tag = d->p[off];

    /* ! IL TAG A PIU' BYTE NON SI LEGGE, E SI RIFIUTA. Nei certificati non
     * compare: i numeri di tag arrivano al massimo a 30. Accettarlo vorrebbe
     * dire scrivere un ciclo che nessuno esercita mai — cioe' un ciclo su
     * input ostile che non e' mai stato provato. */
    if ((out->tag & 0x1F) == 0x1F) return -1;

    if (d->p[off + 1] < 0x80) {
        lung      = d->p[off + 1];
        byte_lung = 1;
    } else {
        byte_lung = d->p[off + 1] & 0x7F;

        /* ! LA FORMA INDEFINITA E' BER, NON DER, e si rifiuta: accettarla
         * vorrebbe dire due codifiche dello stesso certificato, cioe' due
         * impronte diverse della stessa cosa. */
        if (byte_lung == 0 || byte_lung > 4) return -1;
        if (d->n - off - 2 < byte_lung) return -1;

        lung = 0;
        for (i = 0; i < byte_lung; i++)
            lung = (lung << 8) | d->p[off + 2 + i];

        /* Una lunghezza scritta su piu' byte del necessario e' BER: la si
         * lascia passare — i certificati veri non ne hanno, ma rifiutarla
         * costerebbe piu' di quanto renda. Il tetto invece serve: senza,
         * `lung` puo' valere quasi quattro miliardi e le sottrazioni qui
         * sotto non lo fermerebbero. */
        if (lung > d->n) return -1;
        byte_lung += 1;
    }

    if (d->n - off - 1 - byte_lung < lung) return -1;

    out->intestazione = 1 + byte_lung;
    out->valore.p     = d->p + off + out->intestazione;
    out->valore.n     = lung;
    return 0;
}

int exder_dentro(const ExDer *d, unsigned int off, unsigned int tag_atteso,
                 ExDer *dentro)
{
    ExDerElem e;

    if (exder_leggi(d, off, &e) != 0) return -1;
    if (e.tag != tag_atteso) return -1;
    *dentro = e.valore;
    return 0;
}

/* Quanto occupa in tutto l'elemento che comincia a `off`. */
static unsigned int misura(const ExDer *d, unsigned int off)
{
    ExDerElem e;

    if (exder_leggi(d, off, &e) != 0) return 0;
    return e.intestazione + e.valore.n;
}

/* =============================================================================
 * Gli OID, riconosciuti per byte
 *
 * ! NON SI DECODIFICA L'OID IN NUMERI, si confrontano i byte. Un OID e' una
 * costante: scriverne il codice DER e confrontarlo e' meno codice, non ha
 * casi limite, e soprattutto non ha un ciclo di decodifica su input ostile.
 * ============================================================================= */
static int uguali(const unsigned char *a, unsigned int an,
                  const unsigned char *b, unsigned int bn)
{
    unsigned int i;

    if (an != bn) return 0;
    for (i = 0; i < an; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* 1.2.840.113549.1.1.11 — sha256WithRSAEncryption e i suoi fratelli */
static const unsigned char OID_RSA_SHA256[] =
    { 0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0B };
static const unsigned char OID_RSA_SHA384[] =
    { 0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0C };
static const unsigned char OID_RSA_SHA512[] =
    { 0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x0D };
static const unsigned char OID_RSA_SHA1[] =
    { 0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x05 };
/* 1.2.840.10045.4.3.2 — ecdsa-with-SHA256 */
static const unsigned char OID_ECDSA_SHA256[] =
    { 0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03,0x02 };
/* 1.2.840.113549.1.1.1 — rsaEncryption */
static const unsigned char OID_RSA[] =
    { 0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01 };
/* 1.2.840.10045.2.1 — id-ecPublicKey ; 1.2.840.10045.3.1.7 — prime256v1 */
static const unsigned char OID_EC[] =
    { 0x2A,0x86,0x48,0xCE,0x3D,0x02,0x01 };
static const unsigned char OID_P256[] =
    { 0x2A,0x86,0x48,0xCE,0x3D,0x03,0x01,0x07 };
/* 2.5.29.19 — basicConstraints */
static const unsigned char OID_BASIC[] = { 0x55,0x1D,0x13 };

static unsigned int alg_da_oid(const ExDer *o)
{
    if (uguali(o->p, o->n, OID_RSA_SHA256, sizeof(OID_RSA_SHA256)))
        return EXASN1_ALG_RSA_SHA256;
    if (uguali(o->p, o->n, OID_RSA_SHA384, sizeof(OID_RSA_SHA384)))
        return EXASN1_ALG_RSA_SHA384;
    if (uguali(o->p, o->n, OID_RSA_SHA512, sizeof(OID_RSA_SHA512)))
        return EXASN1_ALG_RSA_SHA512;
    if (uguali(o->p, o->n, OID_RSA_SHA1, sizeof(OID_RSA_SHA1)))
        return EXASN1_ALG_RSA_SHA1;
    if (uguali(o->p, o->n, OID_ECDSA_SHA256, sizeof(OID_ECDSA_SHA256)))
        return EXASN1_ALG_ECDSA_SHA256;
    return EXASN1_ALG_IGNOTO;
}

/* =============================================================================
 * Le date
 *
 * ! DUE SECOLI IN DUE CIFRE, E LA REGOLA E' DELLA RFC 5280: da 50 in su e'
 * millenovecento, sotto e' duemila. Non e' una convenzione nostra e non si
 * puo' scegliere diversamente: e' scritta nello standard, e chi firma i
 * certificati la usa.
 *
 * Tutt'e due le forme finiscono in «AAAAMMGGhhmmssZ», cosi' confrontare due
 * date e' confrontare due stringhe.
 * ============================================================================= */
static int leggi_data(const ExDerElem *e, char *out)
{
    const unsigned char *s = e->valore.p;
    unsigned int i, n = e->valore.n, k = 0;

    if (e->tag == TAG_UTCTIME) {
        if (n < 13) return -1;
        out[k++] = (s[0] >= '5') ? '1' : '2';
        out[k++] = (s[0] >= '5') ? '9' : '0';
        for (i = 0; i < 12; i++) out[k++] = (char)s[i];
    } else if (e->tag == TAG_GENTIME) {
        if (n < 15) return -1;
        for (i = 0; i < 14; i++) out[k++] = (char)s[i];
    } else {
        return -1;
    }
    out[k++] = 'Z';
    out[k]   = '\0';
    return 0;
}

/* =============================================================================
 * La chiave pubblica: SubjectPublicKeyInfo
 * ============================================================================= */
static int leggi_chiave(const ExDer *spki, ExCert *c)
{
    ExDerElem alg, bs, oid;
    ExDer     dentro;
    unsigned int off;

    if (exder_leggi(spki, 0, &alg) != 0 || alg.tag != TAG_SEQUENZA) return -1;
    if (exder_leggi(&alg.valore, 0, &oid) != 0 || oid.tag != TAG_OID) return -1;

    off = alg.intestazione + alg.valore.n;
    if (exder_leggi(spki, off, &bs) != 0 || bs.tag != TAG_BITSTRING) return -1;
    if (bs.valore.n < 1 || bs.valore.p[0] != 0) return -1;   /* bit di scarto */

    dentro.p = bs.valore.p + 1;
    dentro.n = bs.valore.n - 1;

    if (uguali(oid.valore.p, oid.valore.n, OID_RSA, sizeof(OID_RSA))) {
        ExDer     rsa;
        ExDerElem n_el, e_el;

        /* RSAPublicKey ::= SEQUENCE { modulus INTEGER, publicExponent INTEGER } */
        if (exder_dentro(&dentro, 0, TAG_SEQUENZA, &rsa) != 0) return -1;
        if (exder_leggi(&rsa, 0, &n_el) != 0 || n_el.tag != TAG_INTERO) return -1;
        if (exder_leggi(&rsa, n_el.intestazione + n_el.valore.n, &e_el) != 0 ||
            e_el.tag != TAG_INTERO) return -1;

        c->tipo_chiave      = EXASN1_CHIAVE_RSA;
        c->chiave_modulo    = n_el.valore;
        c->chiave_esponente = e_el.valore;

        /* ! LO ZERO DAVANTI SI TOGLIE QUI. Un INTEGER DER positivo che
         * comincia con un byte >= 0x80 porta uno zero in testa per non
         * sembrare negativo: e' sintassi, non valore, e chi confronta i
         * moduli o li da' in pasto a exbig non deve sapere che esiste. */
        while (c->chiave_modulo.n > 1 && c->chiave_modulo.p[0] == 0) {
            c->chiave_modulo.p++;
            c->chiave_modulo.n--;
        }
        return 0;
    }

    if (uguali(oid.valore.p, oid.valore.n, OID_EC, sizeof(OID_EC))) {
        ExDerElem curva;

        /* La curva sta nel parametro dell'algoritmo, dopo l'OID. */
        off = oid.intestazione + oid.valore.n;
        if (exder_leggi(&alg.valore, off, &curva) != 0) return -1;
        if (curva.tag != TAG_OID) return -1;
        if (!uguali(curva.valore.p, curva.valore.n, OID_P256, sizeof(OID_P256)))
            return -1;                          /* altre curve: non ancora */
        if (dentro.n < 1 || dentro.p[0] != 0x04) return -1;  /* non compresso */

        c->tipo_chiave  = EXASN1_CHIAVE_EC_P256;
        c->chiave_punto = dentro;
        return 0;
    }

    return -1;
}

/* =============================================================================
 * Le estensioni: per adesso serve solo basicConstraints
 * ============================================================================= */
static void leggi_estensioni(const ExDer *ext, ExCert *c)
{
    unsigned int off = 0;

    /* Extensions ::= SEQUENCE OF Extension */
    while (off < ext->n) {
        ExDerElem e, oid, dopo;
        unsigned int passo = misura(ext, off), interno;

        if (passo == 0) return;
        if (exder_leggi(ext, off, &e) != 0 || e.tag != TAG_SEQUENZA) return;
        off += passo;

        if (exder_leggi(&e.valore, 0, &oid) != 0 || oid.tag != TAG_OID) continue;
        if (!uguali(oid.valore.p, oid.valore.n, OID_BASIC, sizeof(OID_BASIC)))
            continue;

        /* Dopo l'OID puo' esserci il booleano «critica», poi l'OCTET STRING. */
        interno = oid.intestazione + oid.valore.n;
        if (exder_leggi(&e.valore, interno, &dopo) != 0) continue;
        if (dopo.tag == TAG_BOOLEANO) {
            interno += dopo.intestazione + dopo.valore.n;
            if (exder_leggi(&e.valore, interno, &dopo) != 0) continue;
        }
        if (dopo.tag != TAG_OCTETSTRING) continue;

        c->ha_basic = 1;
        {
            ExDer     dentro;
            ExDerElem seq, ca;

            if (exder_leggi(&dopo.valore, 0, &seq) != 0 ||
                seq.tag != TAG_SEQUENZA) continue;
            dentro = seq.valore;

            /* BasicConstraints ::= SEQUENCE { cA BOOLEAN DEFAULT FALSE, ... }
             * ! SENZA IL BOOLEANO IL VALORE E' FALSO, per DEFAULT dello
             * standard: un DER non scrive mai i valori predefiniti, quindi
             * «assente» qui vuol dire «non e' una CA» e non «non lo so». */
            if (dentro.n == 0) continue;
            if (exder_leggi(&dentro, 0, &ca) != 0) continue;
            if (ca.tag == TAG_BOOLEANO && ca.valore.n >= 1 && ca.valore.p[0] != 0)
                c->e_ca = 1;
        }
    }
}

/* =============================================================================
 * Il certificato
 *
 *   Certificate ::= SEQUENCE {
 *       tbsCertificate       TBSCertificate,
 *       signatureAlgorithm   AlgorithmIdentifier,
 *       signatureValue       BIT STRING }
 * ============================================================================= */
int excert_analizza(const unsigned char *der, unsigned int len, ExCert *c)
{
    ExDer     tutto, corpo, tbs;
    ExDerElem el, alg, firma;
    unsigned int off, i;

    if (der == 0 || c == 0) return -1;

    for (i = 0; i < sizeof(ExCert); i++) ((unsigned char *)c)[i] = 0;

    tutto.p = der;
    tutto.n = len;

    if (exder_dentro(&tutto, 0, TAG_SEQUENZA, &corpo) != 0) return -1;

    /* --- il TBSCertificate, con la sua intestazione: e' cio' che si firma --- */
    if (exder_leggi(&corpo, 0, &el) != 0 || el.tag != TAG_SEQUENZA) return -1;
    c->tbs.p = corpo.p;
    c->tbs.n = el.intestazione + el.valore.n;
    tbs      = el.valore;

    /* --- l'algoritmo della firma e la firma ------------------------------- */
    off = c->tbs.n;
    if (exder_leggi(&corpo, off, &alg) != 0 || alg.tag != TAG_SEQUENZA) return -1;
    {
        ExDerElem oid;

        if (exder_leggi(&alg.valore, 0, &oid) != 0 || oid.tag != TAG_OID)
            return -1;
        c->alg_firma = alg_da_oid(&oid.valore);
    }

    off += alg.intestazione + alg.valore.n;
    if (exder_leggi(&corpo, off, &firma) != 0 || firma.tag != TAG_BITSTRING)
        return -1;
    if (firma.valore.n < 2 || firma.valore.p[0] != 0) return -1;
    c->firma.p = firma.valore.p + 1;
    c->firma.n = firma.valore.n - 1;

    /* --- dentro il TBSCertificate ------------------------------------------
     *
     *   [0] version, serialNumber, signature, issuer, validity, subject,
     *   subjectPublicKeyInfo, ... [3] extensions
     *
     * ! LA VERSIONE E' OPZIONALE, e si riconosce dal tag: 0xA0. Un certificato
     * v1 non ce l'ha, e allora il primo elemento e' gia' il numero di serie.
     * Contare i campi per posizione senza guardare i tag e' il modo di leggere
     * il numero di serie come se fosse la versione. */
    off = 0;
    if (exder_leggi(&tbs, off, &el) != 0) return -1;
    if (el.tag == 0xA0) off += el.intestazione + el.valore.n;

    if (exder_leggi(&tbs, off, &el) != 0 || el.tag != TAG_INTERO) return -1;
    c->numero_serie = el.valore;
    off += el.intestazione + el.valore.n;

    if (exder_leggi(&tbs, off, &el) != 0 || el.tag != TAG_SEQUENZA) return -1;
    off += el.intestazione + el.valore.n;      /* l'algoritmo, di nuovo */

    if (exder_leggi(&tbs, off, &el) != 0 || el.tag != TAG_SEQUENZA) return -1;
    c->emittente.p = tbs.p + off;
    c->emittente.n = el.intestazione + el.valore.n;
    off += c->emittente.n;

    /* Validity ::= SEQUENCE { notBefore Time, notAfter Time } */
    if (exder_leggi(&tbs, off, &el) != 0 || el.tag != TAG_SEQUENZA) return -1;
    {
        ExDer     v = el.valore;
        ExDerElem a, b;

        if (exder_leggi(&v, 0, &a) != 0) return -1;
        if (leggi_data(&a, c->non_prima) != 0) return -1;
        if (exder_leggi(&v, a.intestazione + a.valore.n, &b) != 0) return -1;
        if (leggi_data(&b, c->non_dopo) != 0) return -1;
    }
    off += el.intestazione + el.valore.n;

    if (exder_leggi(&tbs, off, &el) != 0 || el.tag != TAG_SEQUENZA) return -1;
    c->soggetto.p = tbs.p + off;
    c->soggetto.n = el.intestazione + el.valore.n;
    off += c->soggetto.n;

    if (exder_leggi(&tbs, off, &el) != 0 || el.tag != TAG_SEQUENZA) return -1;
    if (leggi_chiave(&el.valore, c) != 0) c->tipo_chiave = EXASN1_CHIAVE_IGNOTA;
    off += el.intestazione + el.valore.n;

    /* Il resto e' opzionale: si cerca soltanto il [3] con le estensioni. */
    while (off < tbs.n) {
        unsigned int passo = misura(&tbs, off);

        if (passo == 0) break;
        if (exder_leggi(&tbs, off, &el) == 0 && el.tag == 0xA3) {
            ExDer dentro;

            if (exder_dentro(&el.valore, 0, TAG_SEQUENZA, &dentro) == 0)
                leggi_estensioni(&dentro, c);
            break;
        }
        off += passo;
    }

    return 0;
}

int excert_stesso_nome(const ExDer *a, const ExDer *b)
{
    if (a == 0 || b == 0 || a->p == 0 || b->p == 0) return 0;
    return uguali(a->p, a->n, b->p, b->n);
}
