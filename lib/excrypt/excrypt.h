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
