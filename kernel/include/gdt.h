/* =============================================================================
 * kernel/include/gdt.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef GDT_H
#define GDT_H

#include "kernel.h"

void gdt_install(void);
void gdt_set_kernel_stack(uint32_t stack_top);

/* Sposta il descrittore TLS (selettore GDT_TLS_SEL) sul thread pointer del
 * processo che sta per girare. Zero per chi non ha variabili __thread. */
void gdt_set_tls_base(uint32_t base);

#endif
