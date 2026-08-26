/* =============================================================================
 * lib/exdom/exdom_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La tabella di exdom.so.
 *
 * ! OTTO NOMI, E SONO POCHI APPOSTA. Tutto il DOM — document, gli elementi, gli
 * eventi — non passa di qui: passa dal motore JavaScript, dove exdom si e'
 * registrato da solo dentro exdom_apri(). Da fuori servono soltanto le due
 * cose che un programma C deve poter fare: aprire il ponte, e far partire un
 * evento quando l'utente tocca qualcosa.
 *
 * ! QUESTA LIBRERIA NE APRE ALTRE DUE — exjs ed exhtml — e dentro ci sono i
 * loro STUB, come excss ha quello di exhtml. E' la stessa scelta e lo stesso
 * motivo: la seconda copia di un motore dentro un processo che ne ha gia' uno
 * non e' spreco di spazio, e' un guasto.
 * ============================================================================= */

#include "exlib.h"
#include "exdom.h"

/* Il gancio che riempie i ponti verso la libc. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    "exdom_quanto_serve",
    "exdom_apri",
    "exdom_avvolgi",
    "exdom_nodo",
    "exdom_documento",
    "exdom_evento",
    "exdom_perso",
    "exdom_troncato",
    "__lib_avvio"
};

static void *const g_indirizzi[] = {
    (void *)exdom_quanto_serve,
    (void *)exdom_apri,
    (void *)exdom_avvolgi,
    (void *)exdom_nodo,
    (void *)exdom_documento,
    (void *)exdom_evento,
    (void *)exdom_perso,
    (void *)exdom_troncato,
    (void *)__libc_ponti_avvia
};

typedef char exdom_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(exdom_tabella, g_nomi, g_indirizzi);
