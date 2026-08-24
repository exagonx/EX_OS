/* =============================================================================
 * lib/extls/extls.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * I pezzi che TLS 1.3 chiede e che non stanno da nessun'altra parte
 *
 * Qui dentro, per adesso, tre cose:
 *
 *     HMAC-SHA256      l'impronta con una chiave: il mattone del resto
 *     HKDF             da un segreto a tutte le chiavi che servono
 *     RSA-PSS          la firma che TLS 1.3 vuole AL POSTO di PKCS#1 v1.5
 *
 * L'handshake e il record verranno dopo, e staranno accanto a questi.
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' RSA-PSS SE PKCS#1 v1.5 GIA' C'E'
 *
 * Non e' un doppione: TLS 1.3 ha TOLTO PKCS#1 v1.5 dalla CertificateVerify.
 * Restano due usi diversi e tutt'e due necessari — i certificati sono firmati
 * quasi sempre in v1.5 (e quello sta in lib/excert), mentre la firma che il
 * server fa sul dialogo in corso e' PSS. Un TLS che sa fare solo v1.5 non
 * completa nessun handshake 1.3.
 *
 * ! E PSS SI VERIFICA RICOSTRUENDO, come v1.5: si rifa' il valore atteso e si
 * confronta. La differenza e' che PSS porta un sale casuale, quindi non si puo'
 * ricostruire la busta intera — si ricostruisce l'IMPRONTA FINALE, che dal
 * sale dipende, e si confronta quella.
 *
 * -----------------------------------------------------------------------------
 * ! L'IMPRONTA LA METTE CHI CHIAMA, come in lib/excert. Dentro EX-OS e' la
 * libc; nella prova sull'host e' OpenSSL. Una seconda copia di SHA-256 e' cio'
 * che questo progetto ha imparato a temere di piu'.
 * ============================================================================= */

#ifndef EXTLS_H
#define EXTLS_H

#ifdef __cplusplus
extern "C" {
#endif

void sha256(const void *dati, unsigned int len, unsigned char out[32]);

#define EXTLS_IMPRONTA  32       /* SHA-256 */
#define EXTLS_BLOCCO    64       /* il blocco di SHA-256, per HMAC */

/* --- HMAC-SHA256 -----------------------------------------------------------
 *
 * ! LA CHIAVE PIU' LUNGA DEL BLOCCO SI ACCORCIA CON L'IMPRONTA, e non e' un
 * dettaglio: senza, due chiavi diverse ma lunghe darebbero risultati diversi
 * da quelli di chiunque altro, e l'handshake fallirebbe senza dire perche'. */
void extls_hmac(const unsigned char *chiave, unsigned int chiave_n,
                const unsigned char *dati, unsigned int dati_n,
                unsigned char out[EXTLS_IMPRONTA]);

/* --- HKDF (RFC 5869) --------------------------------------------------------
 *
 * `extract` concentra un segreto qualunque in una chiave; `expand` ne tira
 * fuori quanti byte servono, con un'etichetta che li rende diversi fra loro.
 *
 * ! IL SALE PUO' ESSERE VUOTO, e allora vale un blocco di zeri: e' la regola
 * della RFC, ed e' proprio il caso di TLS 1.3 al primo passo del key schedule.
 */
void extls_hkdf_extract(const unsigned char *sale, unsigned int sale_n,
                        const unsigned char *ikm, unsigned int ikm_n,
                        unsigned char prk[EXTLS_IMPRONTA]);

/* Rende 0, o -1 se si chiedono piu' di 255 blocchi (il tetto della RFC). */
int extls_hkdf_expand(const unsigned char prk[EXTLS_IMPRONTA],
                      const unsigned char *info, unsigned int info_n,
                      unsigned char *out, unsigned int out_n);

/* HKDF-Expand-Label di TLS 1.3: l'etichetta e' «tls13 » + nome, e la
 * struttura la impacchetta questa funzione — sbagliarne un byte vuol dire
 * chiavi diverse da quelle del server, cioe' un handshake che finisce con un
 * «bad record mac» e nessuna idea del perche'. */
int extls_expand_label(const unsigned char segreto[EXTLS_IMPRONTA],
                       const char *etichetta,
                       const unsigned char *contesto, unsigned int contesto_n,
                       unsigned char *out, unsigned int out_n);

/* --- RSA-PSS ----------------------------------------------------------------
 *
 * Verifica `firma` sul messaggio di cui `impronta` e' lo SHA-256, con la
 * chiave pubblica (`modulo`, `esponente`) in byte, ordine di rete.
 *
 * `sale_atteso` e' quanti byte di sale pretendere: in TLS 1.3 e' 32, cioe'
 * quanto l'impronta. Passando -1 si accetta il sale che c'e'.
 *
 * Rende 0 se la firma e' buona, -1 altrimenti — e -1 vuol dire «non mi fido»,
 * non «riprova». */
int extls_rsa_pss_verifica(const unsigned char *modulo, unsigned int modulo_n,
                           const unsigned char *esponente, unsigned int esp_n,
                           const unsigned char impronta[EXTLS_IMPRONTA],
                           const unsigned char *firma, unsigned int firma_n,
                           int sale_atteso);

#ifdef __cplusplus
}
#endif

#endif /* EXTLS_H */
