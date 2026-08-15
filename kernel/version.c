/* =============================================================================
 * kernel/version.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Definizione della variabile globale di identità del sistema.
 *
 * La composizione avviene per concatenazione di letterali a compile time:
 * nessuna formattazione a runtime, nessuna allocazione, nessuna dipendenza
 * da kprintf. Questo permette di stamparla in qualunque momento del boot,
 * anche prima che heap, filesystem o paginazione esistano — condizione
 * che il banner iniziale del kernel richiede davvero.
 *
 * Per cambiare nome, autore o versione si modifica version.h, non questo
 * file: qui c'è solo il montaggio dei pezzi.
 * ============================================================================= */

#include "version.h"

/* Blocco completo, multiriga. È ciò che restituisce il comando
 * `ver`/`version` della shell e ciò che il kernel stampa al boot quando
 * verboseboot=1. */
const char g_os_version[] =
    EXOS_NAME " (" EXOS_LONGNAME ")\n"
    EXOS_COPYRIGHT " " EXOS_AUTHOR " <" EXOS_EMAIL ">\n"
    "Licenza: " EXOS_LICENSE " (GNU General Public License)\n"
    "Versione: " EXOS_VERSION " (" EXOS_ARCH ")";

/* Riga singola, per il boot silenzioso e per i log. */
const char g_os_version_short[] =
    EXOS_NAME " " EXOS_VERSION " (" EXOS_LONGNAME ") - "
    EXOS_COPYRIGHT " " EXOS_AUTHOR " - " EXOS_LICENSE;





