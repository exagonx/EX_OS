/* =============================================================================
 * lib/excert/excert.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La catena: da un certificato a una radice di cui ci si fida
 *
 * `exbig` fa il conto di una firma. `exasn1` dice quali numeri metterci. Qui
 * si risponde alla sola domanda che conta: **posso fidarmi di chi mi ha
 * risposto?**
 *
 * ! ED E' LA PARTE CHE, SE MANCA, RENDE IL TLS PEGGIO DEL TESTO IN CHIARO.
 * Una connessione cifrata con chiunque risponda e' cifrata con chi sta in
 * mezzo, e la barra scrive `https://` — cioe' dice a chi legge che e' al
 * sicuro. Con `http://` almeno lo sa, che non lo e'.
 *
 * -----------------------------------------------------------------------------
 * ! COSA VUOL DIRE «VERIFICATA», QUI DENTRO
 *
 * Una catena e' buona se, per ogni anello:
 *
 *   1. la firma del certificato torna con la chiave pubblica di chi lo ha
 *      emesso — il conto vero, non un confronto di nomi;
 *   2. l'emittente del figlio e' il soggetto del padre, byte per byte;
 *   3. il padre e' una CA: basicConstraints con cA vero. Un certificato di
 *      sito che ne firma un altro non e' una catena, e' un attacco vecchio
 *      di vent'anni;
 *   4. la data di oggi sta dentro la validita' di TUTTI gli anelli;
 *   5. l'ultimo anello e' un certificato che sta nel magazzino, e ci sta con
 *      i suoi byte — non perche' ha lo stesso nome.
 *
 * ! E LA RADICE NON SI VERIFICA CONTRO SE STESSA. Un certificato autofirmato
 * dimostra soltanto di possedere la propria chiave, il che e' vero anche per
 * quello che si e' fatto in casa chi attacca. La radice vale perche' E' NEL
 * MAGAZZINO, e il magazzino sta sul CD di EX-OS.
 *
 * ! SHA-1 SI RIFIUTA. Le collisioni su SHA-1 si comprano, e una firma di CA
 * con SHA-1 e' esattamente il posto dove servono. Riconoscerlo per nome
 * (EXASN1_ALG_RSA_SHA1) serve a dire perche' invece di dire «non lo capisco».
 *
 * -----------------------------------------------------------------------------
 * ! L'IMPRONTA LA METTE CHI CHIAMA, E NON E' UNA STRANEZZA
 *
 * Questo file non contiene SHA-256: lo dichiara e basta. Dentro EX-OS lo
 * fornisce la libc, che ce l'ha gia' — e una seconda copia della stessa
 * funzione e' la cosa che questo progetto ha imparato a temere di piu'. Nella
 * prova sull'host lo fornisce OpenSSL, cioe' l'implementazione di
 * riferimento: quello che si sta provando qui e' la CATENA, non l'impronta.
 * ============================================================================= */

#ifndef EXCERT_H
#define EXCERT_H

#include "exasn1.h"

#ifdef __cplusplus
extern "C" {
#endif

/* La fornisce la libc dentro EX-OS, OpenSSL nella prova sull'host. */
void sha256(const void *dati, unsigned int len, unsigned char out[32]);

/* Perche' una catena e' stata rifiutata. Il numero si stampa: chi vede
 * «certificato scaduto» sa cosa fare, chi vede «non mi fido» no. */
#define EXCERT_OK               0
#define EXCERT_MALFORMATO      -1
#define EXCERT_FIRMA_SBAGLIATA -2
#define EXCERT_NOME_DIVERSO    -3
#define EXCERT_NON_E_CA        -4
#define EXCERT_SCADUTO         -5
#define EXCERT_NON_ANCORA      -6
#define EXCERT_SENZA_RADICE    -7
#define EXCERT_ALG_RIFIUTATO   -8
#define EXCERT_TROPPO_LUNGA    -9

/* Il magazzino: certificati DER di cui ci si fida, uno per voce. Chi lo
 * riempie tiene vivi i byte — qui non si copia niente, come in exasn1. */
#define EXCERT_MAGAZZINO_MAX  200

typedef struct {
    ExCert       cert[EXCERT_MAGAZZINO_MAX];
    unsigned int n;
} ExMagazzino;

/* Aggiunge un certificato al magazzino. Rende 0, o <0 se non si legge o se il
 * magazzino e' pieno. */
int excert_magazzino_aggiungi(ExMagazzino *m, const unsigned char *der,
                              unsigned int len);

/* Verifica che `figlio` sia firmato da `padre`. Rende EXCERT_OK o il motivo.
 * Non guarda i nomi ne' le date: e' il conto della firma e basta. */
int excert_firma_valida(const ExCert *figlio, const ExCert *padre);

/* La catena intera. `catena[0]` e' il certificato del sito, poi gli
 * intermedi nell'ordine in cui li manda il server. `adesso` e' la data di
 * oggi nella forma «AAAAMMGGhhmmssZ»; passando 0 le date non si guardano —
 * e chi lo fa deve avere una ragione, perche' un certificato scaduto e' un
 * certificato che qualcuno ha smesso di difendere. */
int excert_catena_valida(const ExCert *catena, unsigned int quanti,
                         const ExMagazzino *magazzino, const char *adesso);

#ifdef __cplusplus
}
#endif

#endif /* EXCERT_H */
