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
/* ! DUE CAUSE DIVERSE, DUE CODICI. Fino a oggi «algoritmo non gestito» valeva
 * sia per la FIRMA del figlio (un OID che non conosciamo) sia per la CHIAVE
 * del padre (una curva o un tipo che non sappiamo usare). Sono due guasti che
 * si riparano in due posti diversi — uno in alg_da_oid, l'altro nella lettura
 * della SubjectPublicKeyInfo — e schiacciarli insieme costringe a indovinare
 * quale dei due si sta guardando. */
#define EXCERT_CHIAVE_RIFIUTATA -10

/* ! I NOVE CASI HANNO NOVE FRASI, e servono davvero. Il codice si teneva gia'
 * — extls lo mette in `motivo` apposta — ma nessuno lo traduceva, e quello che
 * arrivava all'utente era sempre «certificato non verificabile»: una frase che
 * vale per un certificato scaduto, per una radice che manca e per una firma
 * falsa, cioe' per tre situazioni che si risolvono in tre modi diversi.
 * Distinguerle e' la differenza fra «cambia l'orologio» e «manca una CA». */
const char *excert_perche(int codice);

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
/* ! E DICE ANCHE QUALE ANELLO SI E' ROTTO. `anello` puo' essere 0 se non
 * interessa; quando c'e', ci finisce l'indice dell'anello che ha fatto
 * fallire — 0 e' il certificato del sito. Senza, «la firma non torna» e'
 * vero per una catena di quattro e non dice quale dei quattro, che e'
 * esattamente cio' che serve sapere per capire se il guasto e' nel sito o
 * nel nostro magazzino. */
int excert_catena_valida(const ExCert *catena, unsigned int quanti,
                         const ExMagazzino *magazzino, const char *adesso,
                         unsigned int *anello);

/* =============================================================================
 * Il nome: questo certificato e' di QUESTO sito?
 *
 * ! E' LA META' DELLA VERIFICA CHE SI DIMENTICA, ed e' quella che i falsi
 * superano. Una catena valida dice soltanto che una CA di cui ci si fida ha
 * firmato quel certificato: un certificato legittimo per «esempio.it», emesso
 * davvero, ha una catena impeccabile — e non autorizza nessuno a farsi passare
 * per la banca. Senza questo controllo, chiunque possa deviare il traffico e
 * abbia UN certificato qualunque e' dentro.
 *
 * ! SI GUARDA SOLO IL subjectAltName. Il CommonName non si legge: da RFC 6125
 * un certificato che ha il SAN va confrontato li' e basta, e uno che non ce
 * l'ha non esiste piu' fra i certificati pubblici. Leggere il CN come ripiego
 * vorrebbe dire accettare un nome che l'emittente non ha inteso autorizzare.
 *
 * ! IL JOLLY VALE UN'ETICHETTA SOLA E SOLO LA PRIMA. `*.esempio.it` copre
 * `www.esempio.it` e NON copre `a.b.esempio.it` ne' `esempio.it`: e' la regola
 * che impedisce a un `*.it` di valere per tutto un paese.
 *
 * Rende EXCERT_OK o EXCERT_NOME_DIVERSO. */
int excert_nome_combacia(const ExCert *c, const char *host);

#ifdef __cplusplus
}
#endif

#endif /* EXCERT_H */
