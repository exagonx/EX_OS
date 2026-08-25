/* =============================================================================
 * lib/exhttp/exhttp_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Cio' che exhttp.so mette a disposizione.
 *
 * ! ADESSO GLI UTENTI SONO DUE, ed e' la condizione che mancava. Il criterio di
 * questo sistema e' che una libreria condivisa conviene quando due programmi la
 * usano: finche' c'era solo `scarica`, una .so sarebbe stata macchinario per
 * niente. Con il browser sono due, e ognuno si portava la propria copia di
 * http.c, exhttp.c, dns.c e rete.c.
 *
 * ! E CHI NON SCARICA NIENTE NON LA CARICA. Un editor, un orologio, un file
 * manager non aprono un URL mai: la libreria si apre alla prima richiesta, come
 * eximg ed exfont.
 *
 * ! SI ESPORTA ANCHE http_url, E NON E' UN DI PIU'. Il browser ne ha bisogno
 * per risolvere un collegamento relativo contro l'indirizzo di adesso — e' la
 * stessa aritmetica che exhttp fa per le redirezioni, e riscriverla
 * nell'applicazione vorrebbe dire due copie che divergono.
 * ============================================================================= */

#include "exlib.h"
#include "exhttp.h"

/* Il gancio che riempie i ponti verso la libc. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    "exhttp_prendi",
    "exhttp_tcp",
    "exhttp_scambio",
    "http_url",

    /* Aggiunta il 25 agosto 2026: i moduli in POST. */
    "exhttp_posta",

    "__lib_avvio"
};

static void *const g_indirizzi[] = {
    (void *)exhttp_prendi,
    (void *)exhttp_tcp,
    (void *)exhttp_scambio,
    (void *)http_url,

    (void *)exhttp_posta,

    (void *)__libc_ponti_avvia
};

typedef char exhttp_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(exhttp_tabella, g_nomi, g_indirizzi);
