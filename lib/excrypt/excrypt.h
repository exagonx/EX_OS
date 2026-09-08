/* =============================================================================
 * lib/excrypt/excrypt.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExCrypt — la matematica di una sessione cifrata
 *
 * ! E' LA STRADA SENZA GRANDI NUMERI, ED E' UNA SCELTA FATTA UNA VOLTA SOLA.
 * Curve25519 per lo scambio ed Ed25519 per la firma sono aritmetica su numeri
 * di lunghezza FISSA, 32 byte, che si tiene in vettori di parole; RSA vorrebbe
 * un modulo esponenziale su interi da 2048 bit, cioe' una libreria di grandi
 * numeri da scrivere, provare e mantenere — un sottosistema, non un file.
 *
 * ! E OGNI PEZZO SI PROVA CONTRO I VETTORI DEL SUO RFC. Un errore qui non si
 * vede: non da' un risultato storto, da' «connessione fallita» — oppure, molto
 * peggio, una connessione che funziona e non protegge niente. I vettori sono
 * l'unico modo di sapere che questi numeri sono GLI STESSI che calcola il resto
 * del mondo.
 * ============================================================================= */
#ifndef EXCRYPT_H
#define EXCRYPT_H

/* --- AES (FIPS-197) e CCM (RFC 3610) ----------------------------------------
 *
 * ! SOLO IN AVANTI, E BASTA COSI'. CCM — il modo di WPA2 — e' CTR piu'
 * CBC-MAC, e tutt'e due chiamano AES solo in cifratura, anche per decifrare.
 * La tabella inversa sarebbe codice che nessuno chiama, cioe' codice che
 * nessuno prova.
 *
 * ! E SI SCRIVE AES QUI DOPO AVER EVITATO RSA IN extls.h, che sembra una
 * contraddizione e non lo e': li' c'era un'alternativa (le curve), qui no. Il
 * Wi-Fi protetto E' AES-CCM, e chi non lo parla non si connette. */
#define AES_RK_MAX  240      /* 4 * (14 + 1) parole: il caso a 256 bit */

typedef struct {
    unsigned char rk[AES_RK_MAX];
    unsigned int  giri;      /* 10, 12 o 14 secondo la lunghezza della chiave */
} AesChiave;

/* `byte` e' 16, 24 o 32. Rende 0, oppure -1 se la misura non e' una di quelle. */
int  aes_chiave(AesChiave *c, const unsigned char *chiave, unsigned int byte);
void aes_cifra(const AesChiave *c, const unsigned char in[16],
               unsigned char out[16]);

/* CCM. `nonce_n` sta fra 7 e 13, `tag_n` e' pari fra 4 e 16 (in WPA2: 13 e 8).
 * `in` e `out` possono essere lo stesso buffer.
 *
 * ! LA DECIFRATURA AZZERA IL CHIARO SE IL TAG NON TORNA, e rende -1. Non e'
 * prudenza: consegnare byte non autenticati «tanto poi chi chiama controlla»
 * e' il modo in cui nascono gli oracoli. */
int  aes_ccm_cifra(const AesChiave *c,
                   const unsigned char *nonce, unsigned int nonce_n,
                   const unsigned char *aad, unsigned int aad_n,
                   const unsigned char *in, unsigned char *out, unsigned int n,
                   unsigned char *tag, unsigned int tag_n);
int  aes_ccm_decifra(const AesChiave *c,
                     const unsigned char *nonce, unsigned int nonce_n,
                     const unsigned char *aad, unsigned int aad_n,
                     const unsigned char *in, unsigned char *out, unsigned int n,
                     const unsigned char *tag, unsigned int tag_n);

/* --- SHA-1, HMAC-SHA1, PBKDF2 (RFC 3174, 2104, 2898) ------------------------
 *
 * ! SHA-1 E' ROTTO E SI SCRIVE LO STESSO, perche' WPA e WPA2 derivano le
 * chiavi cosi' e lo standard e' del 2004. lib/excert continua a RIFIUTARLO
 * nelle firme dei certificati, e le due cose non si contraddicono: una
 * collisione serve a far passare un documento per un altro, e dentro HMAC non
 * aiuta. Il perche' per esteso sta in sha1.c. */
void sha1(const void *dati, unsigned int len, unsigned char out[20]);
void hmac_sha1(const unsigned char *chiave, unsigned int chiave_n,
               const unsigned char *m, unsigned int m_n,
               unsigned char out[20]);

/* Il messaggio di hmac_sha1 non puo' superare gli 8 KB: oltre, rende zeri
 * invece di troncare. Serve a PBKDF2 e al PRF di WPA, dove sono decine di
 * byte; per un flusso questa funzione va rifatta a stati. */

/* WPA vuole 4096 giri con l'SSID come sale. Abbassarli non da' un errore: da'
 * una rete che si apre in un pomeriggio invece che in un anno. */
void pbkdf2_sha1(const unsigned char *parola, unsigned int parola_n,
                 const unsigned char *sale, unsigned int sale_n,
                 unsigned int giri, unsigned char *out, unsigned int out_n);

/* --- ChaCha20 (RFC 8439) --------------------------------------------------- */
void chacha20_blocco(const unsigned char chiave[32], unsigned int contatore,
                     const unsigned char nonce[12], unsigned char fuori[64]);
void chacha20(const unsigned char chiave[32], unsigned int contatore,
              const unsigned char nonce[12],
              const unsigned char *in, unsigned char *out, unsigned int n);

/* --- Poly1305 (RFC 8439) --------------------------------------------------- */
void poly1305(const unsigned char chiave[32], const unsigned char *m,
              unsigned int n, unsigned char out[16]);

/* Confronto a tempo costante: vedi il commento in poly1305.c. */
int  poly1305_uguali(const unsigned char a[16], const unsigned char b[16]);

/* --- X25519 (RFC 7748) ------------------------------------------------------
 *
 * x25519() rende 0, oppure -1 se il segreto e' venuto tutto zeri: vuol dire che
 * il punto ricevuto era di ordine piccolo, e chi chiama DEVE rifiutare la
 * connessione invece di proseguire con un segreto che l'altro conosce gia'. */
int  x25519(unsigned char fuori[32], const unsigned char scalare[32],
            const unsigned char punto[32]);
int  x25519_pubblica(unsigned char fuori[32], const unsigned char privata[32]);

/* --- SHA-512 (FIPS 180-4) ---------------------------------------------------
 * Serve a Ed25519, che non accetta altro. Il messaggio dev'essere in memoria
 * tutto insieme: qui non si firmano file, si firmano scambi di chiavi. */
void sha512(const unsigned char *m, unsigned int n, unsigned char out[64]);

/* SHA-384: stesso motore, altro valore iniziale, 48 byte in uscita. Serve alle
 * firme ECDSA su P-384 — che sul web sono tutt'altro che rare. */
void sha384(const unsigned char *m, unsigned int n, unsigned char out[48]);

/* --- Ed25519 (RFC 8032) -----------------------------------------------------
 *
 * ! DICE CON CHI SI STA PARLANDO, ed e' la parte che X25519 da solo non da':
 * uno scambio di chiavi non firmato protegge dall'ascolto, non da chi si mette
 * in mezzo e ne fa due, uno con ciascuno.
 *
 * Il seme e' la chiave privata: 32 byte imprevedibili, e da quelli si ricava
 * tutto il resto. La firma e' deterministica — vedi ed25519.c.
 *
 * ed25519_verifica rende 0 se la firma e' buona, -1 se no. */
void ed25519_pubblica(unsigned char pub[32], const unsigned char seme[32]);
void ed25519_firma(unsigned char firma[64], const unsigned char *m,
                   unsigned int n, const unsigned char seme[32],
                   const unsigned char pub[32]);
int  ed25519_verifica(const unsigned char firma[64], const unsigned char *m,
                      unsigned int n, const unsigned char pub[32]);

#endif /* EXCRYPT_H */
