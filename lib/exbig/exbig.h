/* =============================================================================
 * lib/exbig/exbig.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Interi lunghi — il primo dei tre pezzi che mancano all'https
 *
 * L'ordine e' exbig, exasn1, extls, e non e' negoziabile: si spedisce un TLS
 * che sa dire con chi sta parlando, o non lo si spedisce. Senza verifica del
 * certificato la connessione e' cifrata con CHIUNQUE risponda, e la barra
 * scrive `https://` — peggio del testo in chiaro, dove almeno chi guarda sa di
 * essere scoperto.
 *
 * -----------------------------------------------------------------------------
 * ! SOLO VERIFICA, E LA PAROLA VA PRESA ALLA LETTERA
 *
 * Qui dentro non ci sono: generazione di chiavi, prova di primalita', teorema
 * cinese del resto, esponenti segreti. Non e' una tappa: e' il confine del
 * lavoro. Verificare una firma RSA vuol dire UNA elevazione a potenza con
 * l'esponente PUBBLICO, che nei certificati veri e' 65537 — sedici
 * elevamenti al quadrato e una moltiplicazione.
 *
 * ! E CIO' CHE NON C'E' NON PUO' PERDERE SEGRETI. Un'implementazione che
 * maneggia solo numeri pubblici — modulo, esponente pubblico, firma — non ha
 * niente da far trapelare col tempo che impiega. Il giorno che qui dentro
 * entrasse una chiave privata, ogni ramo `if` di questo file diventerebbe un
 * canale laterale da chiudere. Percio' non ci entra.
 *
 * -----------------------------------------------------------------------------
 * ! NON ESISTE LA DIVISIONE A 64 BIT, ED E' IL VINCOLO CHE HA SCELTO
 *   L'ALGORITMO
 *
 * Una libreria di EX-OS si collega senza libgcc: `__udivdi3` non c'e' e il
 * collegamento FALLISCE — lo stesso muro di tsc.c e del rasterizzatore dei
 * font. La riduzione modulare della scuola ha bisogno proprio di quello: una
 * divisione a due parole per una.
 *
 * Quindi qui si usa MONTGOMERY, che il modulo non lo divide mai: lo somma. Il
 * prezzo e' entrare e uscire dalla forma di Montgomery; il guadagno e' che
 * l'unica operazione lunga e' la MOLTIPLICAZIONE, che su i386 sono due
 * istruzioni (`mull` da' i 64 bit in EDX:EAX) e libgcc non serve.
 *
 * ! IL MODULO DEV'ESSERE DISPARI, e Montgomery lo pretende. Non e' una
 * limitazione: un modulo RSA e' il prodotto di due primi grandi, e nessuno dei
 * due e' 2.
 *
 * -----------------------------------------------------------------------------
 * ! I NUMERI SONO A MISURA FISSA, E LA MISURA E' DICHIARATA
 *
 * Niente allocazione: un ExBig e' 516 byte e sta sullo stack di chi lo usa.
 * Una libreria che alloca dentro un handshake e' una libreria che puo'
 * fallire nel mezzo, e allora bisogna decidere cosa fare di meta' verifica.
 * Il tetto e' 4096 bit — il doppio di quello che i certificati veri usano
 * oggi — e chi lo supera lo SA, perche' la funzione rende un errore invece di
 * troncare in silenzio.
 * ============================================================================= */

#ifndef EXBIG_H
#define EXBIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* 4096 bit = 128 parole da 32. Il doppio dei 2048 che usano i certificati
 * veri, e la meta' di cio' che servirebbe per generare una chiave — che qui
 * non si genera. */
#define EXBIG_BIT_MAX     4096
#define EXBIG_PAROLE      (EXBIG_BIT_MAX / 32)

/* ! `n` E' QUANTE PAROLE CONTANO, NON QUANTE CE NE SONO. Le parole oltre `n`
 * sono zero per invariante: ogni funzione di questo file le lascia a zero,
 * cosi' un confronto o una copia non devono guardarle. Chi riempie `p` a mano
 * deve rispettarlo, ed e' il motivo per cui esistono exbig_da_byte() e
 * exbig_da_parola() invece di lasciar scrivere il campo. */
typedef struct {
    unsigned int n;                       /* parole significative */
    unsigned int p[EXBIG_PAROLE];         /* p[0] e' la parola MENO pesante */
} ExBig;

/* --- Costruire e leggere ---------------------------------------------------
 *
 * ! I BYTE SONO IN ORDINE DI RETE (il piu' pesante per primo), perche' e'
 * cosi' che stanno dentro un DER e dentro un record TLS. Convertire
 * all'ingresso e all'uscita, e non ogni volta che si guarda un numero, e'
 * l'unico modo di non sbagliarsi a meta' algoritmo. */
void exbig_zero(ExBig *r);
void exbig_da_parola(ExBig *r, unsigned int v);

/* Rende 0, oppure -1 se i byte non ci stanno (piu' di EXBIG_BIT_MAX bit). */
int  exbig_da_byte(ExBig *r, const unsigned char *b, unsigned int len);

/* Scrive `a` in `len` byte, riempiendo di zeri a sinistra. Rende 0, oppure -1
 * se `a` non ci sta in `len` byte — un errore, non un troncamento: una firma
 * scritta corta e' una firma diversa. */
int  exbig_a_byte(const ExBig *a, unsigned char *b, unsigned int len);

/* --- Guardare -------------------------------------------------------------- */
int  exbig_e_zero(const ExBig *a);
int  exbig_cmp(const ExBig *a, const ExBig *b);   /* -1, 0, 1 */
unsigned int exbig_bit(const ExBig *a);           /* quanti bit servono */

/* --- L'unica operazione che serve alla verifica -----------------------------
 *
 *     r = base^e mod m
 *
 * Rende 0, oppure -1 se `m` e' pari, zero, o piu' grande del tetto; oppure se
 * `base` e' maggiore o uguale a `m` — che in una verifica RSA vuol dire firma
 * malformata, non un caso da aggiustare in silenzio riducendola.
 *
 * ! IL TEMPO DIPENDE DAI BIT DI `e`, ED E' VOLUTO. Con un esponente pubblico
 * non c'e' niente da nascondere, e un'esponenziazione a tempo costante
 * costerebbe il doppio per proteggere un numero che sta scritto nel
 * certificato. */
int  exbig_modexp(ExBig *r, const ExBig *base, const ExBig *e, const ExBig *m);

/* =============================================================================
 * ARITMETICA MODULARE CON IL MODULO PREPARATO
 *
 * ! SERVE ALLE CURVE, E LA RAGIONE E' IL NUMERO DI MOLTIPLICAZIONI. Una
 * verifica RSA e' UNA esponenziazione: preparare il modulo dentro di essa non
 * si nota. Una verifica ECDSA su P-256 e' un migliaio di moltiplicazioni
 * modulari con lo STESSO modulo — e R^2, che la forma di Montgomery pretende,
 * costa da solo quanto un centinaio di quelle. Calcolarlo una volta e tenerlo
 * e' la differenza fra una firma verificata in un attimo e una che fa
 * aspettare.
 *
 * ! E I NUMERI ENTRANO ED ESCONO NORMALI, non in forma di Montgomery. La forma
 * di Montgomery resta un fatto interno: chi scrive l'aritmetica di una curva
 * ha gia' abbastanza da sbagliare senza doversi ricordare in quale mondo si
 * trova ogni variabile.
 * ========================================================================== */
typedef struct {
    ExBig        m;         /* il modulo */
    ExBig        rr;        /* R^2 mod m, per entrare in forma di Montgomery */
    unsigned int n;         /* parole del modulo */
    unsigned int n0;        /* -m^-1 mod 2^32 */
} ExBigMod;

/* Rende 0, o -1 se il modulo e' zero, pari o troppo grande. */
int  exbig_mod_prepara(ExBigMod *c, const ExBig *m);

/* r = a*b mod m, r = a+b mod m, r = a-b mod m. `a` e `b` devono essere gia'
 * ridotti: chi chiama lavora sempre dentro il campo. */
void exbig_mod_mul(ExBig *r, const ExBig *a, const ExBig *b, const ExBigMod *c);
void exbig_mod_add(ExBig *r, const ExBig *a, const ExBig *b, const ExBigMod *c);
void exbig_mod_sub(ExBig *r, const ExBig *a, const ExBig *b, const ExBigMod *c);

#ifdef __cplusplus
}
#endif

#endif /* EXBIG_H */
