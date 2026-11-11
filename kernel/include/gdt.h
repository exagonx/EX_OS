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
void gdt_set_kernel_stack(vaddr_t stack_top);   /* 0.315: above 4 GB at 64 bit */

/* Sposta il descrittore TLS (selettore GDT_TLS_SEL) sul thread pointer del
 * processo che sta per girare. Zero per chi non ha variabili __thread. */
void gdt_set_tls_base(uint32_t base);

/* La GDT e il TSS del processore numero n (1..): li carica chi chiama. Vedi
 * gdt.c, «una GDT e un TSS per processore». */
void gdt_installa_cpu(uint32_t n);

/* =============================================================================
 * THE I/O PERMISSION BITMAP (0.324, voce 98)
 *
 * After every CPU's TSS: one bit per port, 0 = ring 3 may `in`/`out` it.
 * 8 KB + the trailing 0xFF byte the CPU wants. All closed, except for the
 * process running there that asked (SYS_IOPORT_DIRETTO); sched_switch_to
 * closes the leaving one's ports and opens the coming one's, rewriting only
 * the bytes that were opened.
 *
 * ! WHY: a syscall per byte takes the kernel lock. Two CPUs, and a wave on
 * the parallel port came out 1.3 ms late against 180 us half-waves. Bullshit
 * timing. With the bitmap the driver never enters the kernel.
 *
 * ! NOTHING NEW IS GIVEN: only the ports ioport_bind already gave (a *.drv
 * run by root).
 * ========================================================================== */
#define GDT_MAPPA_IO_BYTE   (65536u / 8u)

void gdt_porte_apri(uint32_t base, uint32_t quante);    /* on this processor */
void gdt_porte_chiudi(void);                            /* all, on this processor */

#endif
