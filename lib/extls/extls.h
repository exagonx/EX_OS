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

#include "excert.h"

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

/* =============================================================================
 * IL CLIENTE TLS 1.3
 *
 * ! UNA SOLA STRADA, E SCELTA APPOSTA. TLS 1.3 permette una decina di
 * combinazioni; qui se ne parla UNA:
 *
 *     scambio di chiavi   X25519
 *     cifrario            TLS_CHACHA20_POLY1305_SHA256
 *     firma del server    RSA-PSS con SHA-256, ECDSA su P-256 e P-384
 *
 * Non e' poverta': e' il fatto che ognuna delle altre combinazioni sarebbe
 * un'altra implementazione da provare, e una crittografia provata a meta' e'
 * peggio di una che non c'e' — perche' la barra scrive `https://` lo stesso.
 * Questi tre pezzi EX-OS li ha gia' e sono gia' provati contro OpenSSL:
 * X25519, ChaCha20 e Poly1305 stanno in lib/excrypt dai tempi di sshd.
 *
 * ! QUESTO PARAGRAFO DICEVA UNA COSA FALSA DA SETTIMANE, e si tiene la
 * correzione invece di cancellarlo. Diceva: «un sito che offre SOLO
 * certificati ECDSA non si apre: la verifica della catena vuole RSA e la P-256
 * non c'e'». Era vero quando e' stato scritto; dal 25 agosto 2026 lib/excert
 * verifica ECDSA su P-256 e P-384, e il client annuncia le due firme
 * corrispondenti — sta scritto venti righe piu' in basso, dentro il codice.
 * Chi leggeva l'intestazione e chi leggeva il codice ottenevano due risposte
 * diverse, e l'intestazione e' quella che si legge per prima.
 *
 * ! IL PREZZO VERO, OGGI: la catena si verifica con RSA PKCS#1 v1.5
 * (SHA-256/384/512) e con ECDSA su P-256 e P-384. Fuori restano le curve piu'
 * grandi, Ed25519, e RSA-PSS DENTRO i certificati — che e' un'altra cosa dal
 * PSS della CertificateVerify, quello c'e'.
 *
 * ! E LA VERIFICA NON E' OPZIONALE. Senza magazzino di CA la stretta di mano
 * fallisce: non c'e' un modo di dire «cifra e fidati». Cifrare con chiunque
 * risponda vuol dire cifrare con chi sta in mezzo, e in quel caso il lucchetto
 * mente a chi lo guarda.
 * ============================================================================= */

/* Il trasporto sotto: TCP, e nient'altro che questo cliente debba sapere.
 * `leggi` rende i byte letti, 0 se l'altro ha chiuso, <0 per errore. */
typedef struct {
    void *stato;
    int (*leggi)(void *stato, unsigned char *dst, unsigned int max,
                 unsigned int ms);
    int (*scrivi)(void *stato, const unsigned char *src, unsigned int n);
} ExTlsSotto;

#define EXTLS_OK                 0
#define EXTLS_ERR_RETE          -1   /* la connessione e' caduta */
#define EXTLS_ERR_PROTOCOLLO    -2   /* byte che non stanno in TLS 1.3 */
#define EXTLS_ERR_VERSIONE      -3   /* l'altro non parla 1.3 */
#define EXTLS_ERR_CIFRARIO      -4   /* non ha scelto il nostro */
#define EXTLS_ERR_CERTIFICATO   -5   /* catena non valida */
#define EXTLS_ERR_NOME          -6   /* certificato di un altro sito */
#define EXTLS_ERR_FIRMA         -7   /* CertificateVerify sbagliata */
#define EXTLS_ERR_FINISHED      -8   /* le chiavi non coincidono */
#define EXTLS_ERR_ALLERTA       -9   /* l'altro ha detto di no */
#define EXTLS_ERR_SPAZIO       -10   /* un messaggio piu' grande dei buffer */
#define EXTLS_ERR_HRR          -11   /* HelloRetryRequest: non gestito */
#define EXTLS_ERR_USO          -12   /* argomenti mancanti */

/* Quanti byte occupa una connessione. Chi chiama alloca: qui dentro non si
 * chiama malloc, cosi' la stessa libreria gira dentro EX-OS e sull'host. */
unsigned int extls_misura(void);

/* La stretta di mano. `t` e' `extls_misura()` byte azzerati o no, non importa.
 * `adesso` e' l'ora in «AAAAMMGGhhmmssZ», per le date dei certificati.
 * `casuale` riempie di byte imprevedibili — dentro EX-OS e' getrandom().
 *
 * Rende EXTLS_OK o uno dei codici qui sopra. */
int extls_stretta(void *t, const ExTlsSotto *sotto, const char *host,
                  const ExMagazzino *magazzino, const char *adesso,
                  void (*casuale)(unsigned char *, unsigned int));

/* Dopo la stretta: dati applicativi, cifrati. Stessa firma del trasporto. */
int  extls_leggi(void *t, unsigned char *dst, unsigned int max,
                 unsigned int ms);
int  extls_scrivi(void *t, const unsigned char *src, unsigned int n);

/* Manda close_notify. Il trasporto sotto lo chiude chi l'ha aperto. */
void extls_chiudi(void *t);

/* =============================================================================
 * Il magazzino delle CA, letto da un file PEM
 *
 * `der` e' dove finiscono i byte decifrati, e DEVE restare vivo quanto il
 * magazzino: ExCert non copia niente, tiene fette che puntano li' dentro.
 *
 * Rende quanti certificati ha capito (>= 0), -1 per argomenti sbagliati, -2 se
 * `der` e' troppo piccolo. Un certificato che non si capisce si salta.
 * ============================================================================= */
int extls_magazzino_pem(ExMagazzino *m, const char *pem, unsigned int pem_n,
                        unsigned char *der, unsigned int der_max,
                        unsigned int *der_usati);

/* Il codice dell'ultimo allarme ricevuto (0 = nessuno). Vale la pena
 * stamparlo accanto a EXTLS_ERR_ALLERTA: 40 e' «non abbiamo niente in
 * comune», 112 «questo nome non e' mio», 48 «non mi fido del tuo
 * certificato». */
unsigned int extls_allarme(void *t);

/* L'ultimo errore incontrato leggendo, DOPO la stretta: `extls_leggi` rende
 * byte e non codici, e senza questo «zero byte» non dice perche'. */
int extls_ultimo(void *t);

/* Quando la catena non e' valida: il codice EXCERT_* che l'ha detto. Nove casi
 * diversi che «certificato non verificabile» non distingue. */
int extls_motivo(void *t);

/* Quale anello della catena ha fatto fallire: 0 e' il certificato del sito,
 * l'ultimo e' quello che avrebbe dovuto agganciarsi a una radice. Ha senso
 * solo dopo un EXTLS_ERR_CERTIFICATO. */
int extls_anello(void *t);

/* Una riga in italiano per un codice di errore. */
const char *extls_perche(int codice);

#ifdef __cplusplus
}
#endif

#endif /* EXTLS_H */
