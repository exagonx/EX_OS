/* =============================================================================
 * kernel/include/power.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Arresto, spegnimento e riavvio. Implementazione e note sui limiti dello
 * spegnimento hardware in kernel/arch/x86/power.c.
 *
 * Nessuna di queste funzioni ritorna. Tutte sincronizzano il filesystem e
 * fermano lo scheduler prima di agire.
 * ============================================================================= */

#ifndef POWER_H
#define POWER_H

#include "kernel.h"

/* Comandi accettati da SYS_REBOOT. Duplicati in bin/sh/shell.c e in
 * lib/include/libc.h con la stessa convenzione già usata per i numeri di
 * syscall: kernel e userspace non condividono header. */
#define EXOS_RB_POWEROFF   0   /* sincronizza, 3 secondi, spegne */
#define EXOS_RB_RESTART    1   /* sincronizza e riavvia */
#define EXOS_RB_HALT       2   /* sincronizza e ferma, senza spegnere */

/* Sincronizza, ferma lo scheduler, conta 3 secondi e tenta lo
 * spegnimento hardware. Se l'hardware non lo supporta (probabile su
 * macchine reali senza ACPI) si ferma in uno stato sicuro con il
 * messaggio "e' ora sicuro spegnere". */
NORETURN void power_off(void);

/* Come sopra ma senza conto alla rovescia né tentativo di spegnimento:
 * ferma e basta. */
NORETURN void power_halt(void);

/* Sincronizza e riavvia (reset via 8042, poi triple fault). */
NORETURN void power_reboot(void);

#endif /* POWER_H */
