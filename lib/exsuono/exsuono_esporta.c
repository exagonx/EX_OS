/* =============================================================================
 * lib/exsuono/exsuono_esporta.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Cio' che exsuono.so mette a disposizione. Stessa forma delle altre librerie
 * condivise: due elenchi appaiati, nomi e indirizzi. Un nome nuovo si aggiunge
 * IN FONDO: chi e' gia' compilato cerca per nome, e non se ne accorge.
 * ============================================================================= */

#include "exlib.h"
#include "exsuono.h"

/* Il gancio che riempie i ponti verso la libc. */
void __libc_ponti_avvia(void);

static const char *const g_nomi[] = {
    "exsuono_durata",
    "exsuono_apri",
    "exsuono_chiudi",
    "exsuono_suona",
    "exsuono_pausa",
    "exsuono_vai",
    "exsuono_passo",
    "exsuono_posizione",
    "exsuono_in_corso",
    "exsuono_volume",
    "exsuono_avvia_file",
    "exsuono_ferma_file",
    "exsuono_mix_leggi",
    "exsuono_mix_metti",

    "__lib_avvio"
};

static void *const g_indirizzi[] = {
    (void *)exsuono_durata,
    (void *)exsuono_apri,
    (void *)exsuono_chiudi,
    (void *)exsuono_suona,
    (void *)exsuono_pausa,
    (void *)exsuono_vai,
    (void *)exsuono_passo,
    (void *)exsuono_posizione,
    (void *)exsuono_in_corso,
    (void *)exsuono_volume,
    (void *)exsuono_avvia_file,
    (void *)exsuono_ferma_file,
    (void *)exsuono_mix_leggi,
    (void *)exsuono_mix_metti,

    (void *)__libc_ponti_avvia
};

typedef char exsuono_esporta_elenchi_pari[
    (sizeof(g_nomi) / sizeof(g_nomi[0]) ==
     sizeof(g_indirizzi) / sizeof(g_indirizzi[0])) ? 1 : -1];

EXLIB_TESTA(exsuono_tabella, g_nomi, g_indirizzi);
