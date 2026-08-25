/* =============================================================================
 * lib/excurva/excurva.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ECDSA sulle due curve NIST che il web usa: SOLO la verifica
 *
 *     P-256   secp256r1, prime256v1
 *     P-384   secp384r1
 *
 * ! ESISTE PERCHE' MEZZO WEB NON SI APRIVA. Il cliente TLS di EX-OS sapeva
 * verificare solo firme RSA, e annunciava solo quelle: wikipedia.org e
 * news.ycombinator.com rispondevano «nessun cifrario in comune» e chiudevano.
 * Non e' un caso raro — e' la meta' dei certificati emessi oggi.
 *
 * ! E LE CURVE SONO DUE PERCHE' UNA NON BASTAVA, e lo si e' scoperto guardando
 * le catene vere. La chiave del sito e' quasi sempre P-256; ma l'intermedia
 * che la firma e' spesso su P-384, e la firma stessa e' ecdsa-with-SHA384.
 * Con la sola P-256 la catena di example.com si fermava al secondo anello, e
 * quella di wikipedia.org al primo.
 *
 * ! SI VERIFICA E BASTA, NON SI FIRMA. Firmare vuol dire generare un numero
 * segreto per ogni firma, e un generatore appena debole rivela la chiave
 * privata: e' successo a Sony con la PlayStation 3, e a chiunque abbia
 * riutilizzato un `k`. Un browser non ha niente da firmare; scrivere quel
 * codice «per completezza» vorrebbe dire mettere in casa un'arma carica.
 *
 * ! E NON E' A TEMPO COSTANTE, di proposito. Qui dentro passano solo numeri
 * PUBBLICI — la chiave del server, la firma, l'impronta del dialogo — e non
 * c'e' niente da nascondere a chi misura. Il codice a tempo costante costa in
 * complicazione, e la complicazione in una verifica di firma si paga in
 * difetti che accettano firme false.
 * ============================================================================= */
#ifndef EXCURVA_H
#define EXCURVA_H

#ifdef __cplusplus
extern "C" {
#endif

#define EXCURVA_P256    0
#define EXCURVA_P384    1

/* Verifica una firma ECDSA.
 *
 *   `curva`    EXCURVA_P256 o EXCURVA_P384: quella della CHIAVE, non della
 *              firma. Si ricava dall'OID dentro il certificato.
 *   `punto`    la chiave pubblica come sta nel certificato: 04 || X || Y.
 *              Le forme compresse non si accettano: sono rare nei certificati
 *              e sarebbero una radice quadrata modulare in piu' da scrivere e
 *              da provare.
 *   `impronta` l'impronta del messaggio, e la sua misura: 32 byte per SHA-256,
 *              48 per SHA-384. Le due cose si combinano liberamente — una
 *              chiave P-256 con una firma SHA-384 e' normale sul web.
 *   `r`, `s`   i due interi della firma, in byte, ordine di rete. Lo zero in
 *              testa che il DER mette si puo' lasciare.
 *
 * Rende 0 se la firma e' buona, -1 altrimenti — e -1 vuol dire «non mi fido»,
 * mai «riprova».
 */
int excurva_verifica(int curva,
                     const unsigned char *punto, unsigned int punto_n,
                     const unsigned char *impronta, unsigned int impronta_n,
                     const unsigned char *r, unsigned int r_n,
                     const unsigned char *s, unsigned int s_n);

#ifdef __cplusplus
}
#endif

#endif /* EXCURVA_H */
