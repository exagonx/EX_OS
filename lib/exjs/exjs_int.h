/* =============================================================================
 * lib/exjs/exjs_int.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Quello che i pezzi di ExJs si dicono fra loro, e che fuori non si vede.
 *
 * ! NON E' UN SECONDO exjs.h. Li' c'e' il contratto con chi USA il motore, e
 * quello deve restare identico anche il giorno che sotto ci mettiamo QuickJS.
 * Qui c'e' la forma di cio' che sta dentro, e questa puo' cambiare a ogni
 * scaglione: se una struttura di questo file finisce in un header pubblico,
 * il motore ha smesso di essere sostituibile.
 * ============================================================================= */

#ifndef EXJS_INT_H
#define EXJS_INT_H

#include "exjs.h"

/* =============================================================================
 * I GETTONI
 *
 * ! I NUMERI NON SI SCELGONO A CASO: i primi coincidono con il carattere che
 * li rappresenta, cosi' un gettone di un carattere solo si confronta con
 * `t == '('` invece che con un nome inventato. Quelli veri cominciano dopo
 * l'ASCII, dove non possono collidere con niente.
 * ========================================================================== */
#define TK_FINE        0
#define TK_ERRORE      1

#define TK_PRIMO       128
#define TK_NOME        128      /* identificatore */
#define TK_NUMERO      129
#define TK_STRINGA     130

/* Operatori di piu' caratteri. Uno per riga, e il nome dice come si scrive. */
#define TK_UGUALE      140      /* ==   */
#define TK_DIVERSO     141      /* !=   */
#define TK_ID_UGUALE   142      /* ===  */
#define TK_ID_DIVERSO  143      /* !==  */
#define TK_MIN_UG      144      /* <=   */
#define TK_MAG_UG      145      /* >=   */
#define TK_E_E         146      /* &&   */
#define TK_O_O         147      /* ||   */
#define TK_PIU_PIU     148      /* ++   */
#define TK_MENO_MENO   149      /* --   */
#define TK_PIU_UG      150      /* +=   */
#define TK_MENO_UG     151      /* -=   */
#define TK_PER_UG      152      /* *=   */
#define TK_DIV_UG      153      /* /=   */
#define TK_MOD_UG      154      /* %=   */
#define TK_SHL         155      /* <<   */
#define TK_SHR         156      /* >>   */
#define TK_SHR_U       157      /* >>>  */
#define TK_SHL_UG      158      /* <<=  */
#define TK_SHR_UG      159      /* >>=  */
#define TK_SHR_U_UG    160      /* >>>= */
#define TK_AND_UG      161      /* &=   */
#define TK_OR_UG       162      /* |=   */
#define TK_XOR_UG      163      /* ^=   */

/* Le parole chiave. Sono gettoni a se' e non nomi, perche' `if` non e' una
 * variabile che si possa chiamare cosi': deciderlo qui evita che il
 * costruttore dell'albero confronti stringhe a ogni passo. */
#define TK_VAR         180
#define TK_FUNCTION    181
#define TK_RETURN      182
#define TK_IF          183
#define TK_ELSE        184
#define TK_WHILE       185
#define TK_FOR         186
#define TK_BREAK       187
#define TK_CONTINUE    188
#define TK_NEW         189
#define TK_DELETE      190
#define TK_TYPEOF      191
#define TK_IN          192
#define TK_INSTANCEOF  193
#define TK_THIS        194
#define TK_NULL        195
#define TK_TRUE        196
#define TK_FALSE       197
#define TK_DO          198
#define TK_SWITCH      199
#define TK_CASE        200
#define TK_DEFAULT     201
#define TK_TRY         202
#define TK_CATCH       203
#define TK_FINALLY     204
#define TK_THROW       205
#define TK_VOID        206

/* =============================================================================
 * IL LETTORE DI GETTONI
 *
 * ! SI TIENE ANCHE DOV'ERA IL GETTONE, non solo cos'era. Riga e colonna
 * servono al messaggio d'errore, e ricavarle dopo — ricontando le righe dal
 * principio a ogni errore — vorrebbe dire un secondo pezzo di codice che deve
 * essere d'accordo col primo su cosa sia una riga.
 *
 * ! E SI TIENE SE PRIMA C'ERA UN A CAPO. Serve all'inserimento automatico del
 * punto e virgola, che in JavaScript non e' una comodita': `return` seguito da
 * un a capo rende `undefined` qualunque cosa ci sia sulla riga dopo, e un
 * motore che non lo sapesse eseguirebbe un programma diverso da quello scritto.
 * ========================================================================== */
typedef struct {
    const char  *sorgente;
    unsigned int n;
    unsigned int pos;

    int          riga, colonna;

    /* il gettone corrente */
    int          tipo;
    unsigned int inizio, fine;      /* scostamenti nel sorgente */
    int          t_riga, t_colonna;
    int          a_capo_prima;      /* c'era un fine riga prima di questo? */

    double       numero;            /* se tipo == TK_NUMERO */

    /* Le stringhe arrivano gia' con gli scappamenti sciolti: chi legge un
     * gettone non deve rifare il lavoro dell'analizzatore. */
    char        *testo;
    unsigned int testo_max, testo_n;

    ExJsErrore  *err;
} ExJsLex;

void exjs_lex_apri(ExJsLex *L, const char *sorgente, unsigned int n,
                   char *buffer_testo, unsigned int buffer_max,
                   ExJsErrore *err);

/* Avanza al gettone successivo. Rende il tipo, TK_FINE alla fine, TK_ERRORE
 * dopo aver riempito `err`. */
int exjs_lex_avanti(ExJsLex *L);

/* Il nome di un gettone, per i messaggi d'errore. Sempre una stringa valida. */
const char *exjs_lex_nome(int tipo);

#endif /* EXJS_INT_H */
