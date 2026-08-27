/* =============================================================================
 * lib/exjs/exjs_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La tabella di exjs.so — E ANCHE DI quickjs.so.
 *
 * ! LO STESSO FILE SERVE LE DUE LIBRERIE, e non e' una comodita': e' cio' che
 * garantisce che esportino gli STESSI NOMI. `lib/exjs` e `lib/exqjs` sono due
 * implementazioni della stessa interfaccia — l'ES3 scritto qui e QuickJS — e
 * il browser ne apre una senza sapere quale. Se una delle due si allontanasse
 * da `exjs.h`, questo file non compilerebbe piu' contro di lei: l'errore
 * arriva alla costruzione, invece che il giorno in cui qualcuno apre l'altra
 * libreria e non trova un simbolo.
 *
 * ! QUESTA LIBRERIA HA DUE UTENTI VERI PRIMA ANCORA DI NASCERE, ed e' il caso
 * che il criterio dei «due utenti» descrive esattamente: il browser, che
 * esegue gli script delle pagine, e exdom, che e' un'altra libreria. Collegare
 * il motore dentro il browser avrebbe voluto dire una seconda copia dentro
 * exdom.so — cioe' due interpreti nello stesso processo, ognuno col suo
 * contesto, che non si vedono. Non e' spreco di spazio: e' un guasto.
 *
 * ! SI ESPORTA TUTTO exjs.h E NIENT'ALTRO. Le macro che aprono un ExJsVal
 * stanno in exjs_int.h e li' restano: chi le usasse da fuori si legherebbe al
 * NaN-boxing di oggi, e il giorno che sotto ci sara' QuickJS andrebbe
 * riscritto — che e' precisamente cio' che l'interfaccia opaca esiste per
 * evitare.
 *
 * ! E SI ESPORTANO ANCHE LE COSTRUTTRICI BANALI — exjs_indefinito,
 * exjs_nullo, exjs_booleano — che sono tre righe l'una. Riscriverle nel
 * chiamante vorrebbe dire scrivere a mano le costanti del NaN-boxing, cioe'
 * la stessa dipendenza dai bit, entrata dalla porta di servizio.
 * ============================================================================= */

#include "exlib.h"
#include "exjs.h"

/* Il gancio che riempie i ponti verso la libc. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    "exjs_quanto_serve",
    "exjs_apri",
    "exjs_chiudi",
    "exjs_esegui",

    "exjs_indefinito",
    "exjs_nullo",
    "exjs_booleano",
    "exjs_numero",
    "exjs_stringa",
    "exjs_oggetto",
    "exjs_vettore",
    "exjs_nativa",

    "exjs_tipo",
    "exjs_a_numero",
    "exjs_a_booleano",
    "exjs_a_stringa",

    "exjs_metti",
    "exjs_prendi",
    "exjs_indice_metti",
    "exjs_indice_prendi",
    "exjs_lunghezza",

    "exjs_esotico",
    "exjs_esotico_dato",
    "exjs_proto_metti",

    "exjs_uscita_metti",
    "exjs_globale",
    "exjs_chiama",
    "exjs_invoca",

    "exjs_accoda",
    "exjs_disdici",
    "exjs_pompa",
    "exjs_lavori_in_attesa",

    "exjs_memoria",

    "__lib_avvio"
};

static void *const g_indirizzi[] = {
    (void *)exjs_quanto_serve,
    (void *)exjs_apri,
    (void *)exjs_chiudi,
    (void *)exjs_esegui,

    (void *)exjs_indefinito,
    (void *)exjs_nullo,
    (void *)exjs_booleano,
    (void *)exjs_numero,
    (void *)exjs_stringa,
    (void *)exjs_oggetto,
    (void *)exjs_vettore,
    (void *)exjs_nativa,

    (void *)exjs_tipo,
    (void *)exjs_a_numero,
    (void *)exjs_a_booleano,
    (void *)exjs_a_stringa,

    (void *)exjs_metti,
    (void *)exjs_prendi,
    (void *)exjs_indice_metti,
    (void *)exjs_indice_prendi,
    (void *)exjs_lunghezza,

    (void *)exjs_esotico,
    (void *)exjs_esotico_dato,
    (void *)exjs_proto_metti,

    (void *)exjs_uscita_metti,
    (void *)exjs_globale,
    (void *)exjs_chiama,
    (void *)exjs_invoca,

    (void *)exjs_accoda,
    (void *)exjs_disdici,
    (void *)exjs_pompa,
    (void *)exjs_lavori_in_attesa,

    (void *)exjs_memoria,

    (void *)__libc_ponti_avvia
};

typedef char exjs_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(exjs_tabella, g_nomi, g_indirizzi);
