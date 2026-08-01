/* =============================================================================
 * kernel/include/version.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Identità del sistema: nome, versione, autore, licenza.
 *
 * QUESTA È LA FONTE UNICA DI VERITÀ. Prima di luglio 2026 le stesse
 * informazioni erano duplicate in almeno cinque punti (due banner del
 * kernel, env_init/print_banner/cmd_uname della shell) più la sezione
 * [env] di /boot/kernel.cfg, che però non veniva letta da nessuno. Il
 * risultato era che modificare il file di configurazione non aveva alcun
 * effetto visibile, e il nome era scritto "ExOS" in un punto e "EX-OS"
 * ovunque altro.
 *
 * Ora esiste una sola stringa, `g_os_version`, composta a compile time da
 * queste macro. Kernel e programmi utente mostrano quella:
 *   - il kernel la stampa all'avvio;
 *   - i processi ring3 la ottengono con SYS_VERSION (vedi syscall.h), che
 *     la copia in un buffer utente — la shell la espone con `ver`/`version`.
 *
 * ---------------------------------------------------------------------------
 * REGOLA DI VERSIONAMENTO
 *
 *   EXOS_VERSION va incrementata di 0.001 a OGNI modifica del kernel.
 *
 * Formato "MAGGIORE.mmm" con tre decimali sempre presenti: 0.101 e non
 * 0.101000 né 0.1010. La versione precedente del progetto era "0.1", che
 * in questo schema si legge 0.100 — da lì la numerazione prosegue.
 *
 * È una stringa e non un numero di proposito: un float non rappresenta
 * 0.001 in modo esatto, e stamparlo richiederebbe aritmetica in virgola
 * mobile che questo kernel non usa (niente FPU inizializzata, niente
 * softfloat). Incrementarla è un'operazione manuale e deliberata: chi
 * tocca il kernel aggiorna la riga qui sotto.
 * ---------------------------------------------------------------------------
 * ============================================================================= */

#ifndef VERSION_H
#define VERSION_H

/* Nome breve e nome esteso */
#define EXOS_NAME       "EX OS"
#define EXOS_LONGNAME   "Extensible Operating System"

/* ▲ INCREMENTARE DI 0.001 A OGNI MODIFICA DEL KERNEL ▲ */
#define EXOS_VERSION    "0.142"

/* Autore e contatto */
#define EXOS_AUTHOR     "Graziano Falcone"
#define EXOS_EMAIL      "exagonx@hotmail.com"

/* Licenza */
#define EXOS_LICENSE    "GPL 2.0"
#define EXOS_COPYRIGHT  "Copyright (C) 2025"

/* Architettura di destinazione */
#define EXOS_ARCH       "x86 32-bit"

/* =============================================================================
 * g_os_version — la variabile globale richiesta.
 *
 * Stringa unica e completa, pronta da stampare. Definita in
 * kernel/version.c, dichiarata qui, `const` perché nessuno deve poterla
 * modificare a runtime: è l'identità del sistema, non uno stato.
 * ============================================================================= */
extern const char g_os_version[];

/* Variante compatta su una riga sola, per il boot silenzioso
 * (verboseboot=0) e per i log, dove il blocco multiriga sarebbe rumore. */
extern const char g_os_version_short[];

#endif /* VERSION_H */
