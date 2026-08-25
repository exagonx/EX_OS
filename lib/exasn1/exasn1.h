/* =============================================================================
 * lib/exasn1/exasn1.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * DER e X.509 — il secondo dei tre pezzi che mancano all'https
 *
 * `exbig` sa fare il conto di una firma. Questo file sa dire QUALI numeri
 * mettere in quel conto, e da dove prenderli: un certificato e' un albero DER,
 * e la firma sta su un pezzo di quell'albero, byte per byte com'e' arrivato.
 *
 * -----------------------------------------------------------------------------
 * ! QUESTI BYTE ARRIVANO DALLA RETE, E CHI LI MANDA NON E' AMICO
 *
 * E' la differenza fra questo lettore e tutti gli altri di EX-OS. Un font
 * malformato sta su un CD che abbiamo masterizzato noi; un certificato lo
 * sceglie chi risponde al posto del sito che si voleva. Percio':
 *
 *   - OGNI misura si controlla contro la fine del buffer PRIMA di guardare
 *     dentro. Una lunghezza DER e' un numero scritto dal mittente;
 *   - NON SI COPIA NIENTE. Ogni campo e' un puntatore dentro il buffer di chi
 *     chiama, con la sua misura: niente allocazione, niente buffer di
 *     destinazione da dimensionare, niente memcpy da sbagliare;
 *   - la profondita' e' LIMITATA. Un DER puo' annidarsi all'infinito, e un
 *     lettore ricorsivo su input ostile e' uno stack che finisce;
 *   - una lunghezza in forma indefinita si RIFIUTA: e' legale in BER, non in
 *     DER, e accettarla vuol dire accettare due codifiche dello stesso
 *     certificato — cioe' due impronte diverse della stessa cosa.
 *
 * ! E IL TBSCertificate SI TIENE COM'E' ARRIVATO, non ricostruito. La firma
 * copre quei byte esatti: rigenerarli da una struttura analizzata vorrebbe
 * dire firmare una cosa e verificarne un'altra ogni volta che il nostro
 * codificatore sceglie una forma diversa da quella del mittente. Qui il
 * TBSCertificate e' una FETTA del buffer originale.
 *
 * -----------------------------------------------------------------------------
 * ! COSA NON C'E', DICHIARATO
 *
 * Niente BER, niente CER, niente stringhe convertite (le date si leggono, i
 * nomi restano DER), niente ECDSA — la chiave si legge, ma il conto della
 * firma su curva e' un altro lavoro e sta in extls insieme alle curve. Niente
 * revoche: CRL e OCSP sono richieste in rete, non lettura di byte.
 * ============================================================================= */

#ifndef EXASN1_H
#define EXASN1_H

#ifdef __cplusplus
extern "C" {
#endif

/* Una fetta di byte dentro il buffer di chi chiama: mai posseduta, mai
 * copiata. `p` nullo vuol dire «questo campo non c'era». */
typedef struct {
    const unsigned char *p;
    unsigned int         n;
} ExDer;

/* Un elemento letto. `tag` e' il primo byte com'e' scritto nel file: 0x30
 * SEQUENCE, 0x02 INTEGER, 0x03 BIT STRING, 0xA0 [0] esplicito, e cosi' via. */
typedef struct {
    unsigned int tag;
    ExDer        valore;      /* il contenuto, senza intestazione */
    unsigned int intestazione;/* quanti byte di tag+lunghezza */
} ExDerElem;

/* Legge l'elemento che comincia a `off` dentro `d`. Rende 0, oppure -1 se la
 * lunghezza esce dal buffer, se e' in forma indefinita, o se il numero di byte
 * di lunghezza e' assurdo. */
int exder_leggi(const ExDer *d, unsigned int off, ExDerElem *out);

/* Entra nella SEQUENCE (o nell'insieme) che comincia a `off` e rende in `dentro`
 * il suo contenuto. Rende 0, o -1 se non e' costruito o se non ci sta. */
int exder_dentro(const ExDer *d, unsigned int off, unsigned int tag_atteso,
                 ExDer *dentro);

/* --- Gli algoritmi che sappiamo nominare ---------------------------------- */
#define EXASN1_ALG_IGNOTO        0
#define EXASN1_ALG_RSA_SHA256    1
#define EXASN1_ALG_RSA_SHA384    2
#define EXASN1_ALG_RSA_SHA512    3
#define EXASN1_ALG_RSA_SHA1      4     /* riconosciuto per poterlo RIFIUTARE */
#define EXASN1_ALG_ECDSA_SHA256  5

/* --- La chiave pubblica --------------------------------------------------- */
#define EXASN1_CHIAVE_IGNOTA     0
#define EXASN1_CHIAVE_RSA        1
#define EXASN1_CHIAVE_EC_P256    2

/* Un certificato analizzato. Tutte le fette puntano dentro il DER originale,
 * che deve restare vivo finche' si guarda questa struttura. */
typedef struct {
    ExDer tbs;                 /* i byte su cui si calcola l'impronta */
    ExDer emittente;           /* il DN, in DER: si confronta byte per byte */
    ExDer soggetto;
    ExDer numero_serie;

    unsigned int tipo_chiave;  /* EXASN1_CHIAVE_* */
    ExDer chiave_modulo;       /* RSA: il modulo, senza lo zero davanti */
    ExDer chiave_esponente;    /* RSA: l'esponente pubblico */
    ExDer chiave_punto;        /* EC: il punto non compresso, 04 || X || Y */

    unsigned int alg_firma;    /* EXASN1_ALG_* */
    ExDer firma;               /* il contenuto del BIT STRING, senza il byte
                                * dei bit inutilizzati */

    /* Le date come stanno nel file, normalizzate a «AAAAMMGGhhmmssZ»: una
     * UTCTime a due cifre e una GeneralizedTime a quattro diventano la stessa
     * forma, e allora confrontarle e' una strcmp. */
    char non_prima[16];
    char non_dopo[16];

    int e_ca;                  /* basicConstraints: CA vero */
    int ha_basic;              /* l'estensione c'era */

    /* subjectAltName, il contenuto della SEQUENCE: una fila di nomi con il
     * loro tag. Serve a rispondere alla sola domanda che conta per un
     * browser — questo certificato e' di QUESTO sito?
     *
     * ! E IL CommonName NON E' UNA RISPOSTA A QUELLA DOMANDA. Lo era prima del
     * 2000, e da RFC 6125 in poi un certificato con subjectAltName va guardato
     * SOLO li' dentro: accettare il CN quando il SAN c'e' vuol dire accettare
     * un nome che l'emittente non ha inteso autorizzare. Qui il CN non si
     * legge affatto — un certificato pubblico senza SAN non esiste piu'. */
    ExDer san;                 /* p nullo = l'estensione non c'era */
} ExCert;

/* Analizza un certificato X.509 in DER. Rende 0, oppure -1: e -1 vuol dire
 * «non lo capisco», che per un certificato e' la stessa cosa di «non mi
 * fido». */
int excert_analizza(const unsigned char *der, unsigned int len, ExCert *c);

/* Due nomi sono lo stesso nome se i loro DER sono identici.
 *
 * ! IL CONFRONTO E' SUI BYTE, NON SUL SIGNIFICATO, ed e' una scelta prudente:
 * X.509 avrebbe regole di equivalenza — maiuscole, spazi, codifiche diverse
 * della stessa lettera — e ognuna di quelle regole e' un modo di far sembrare
 * uguali due nomi che non lo sono. Un confronto byte per byte puo' rifiutare
 * una catena legittima costruita male; non puo' accettarne una falsa. */
int excert_stesso_nome(const ExDer *a, const ExDer *b);

#ifdef __cplusplus
}
#endif

#endif /* EXASN1_H */
