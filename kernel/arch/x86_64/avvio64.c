/* =============================================================================
 * kernel/arch/x86_64/avvio64.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * DA entry.asm A kernel_main (@EXOS-64, tappa 3d — 10 ottobre 2026)
 *
 * entry.asm porta il processore in long mode e chiama kmain64. Nel kernel di
 * prova (main64.c) kmain64 sono le prove delle tappe; nel kernel INTERO e'
 * questa: fa le due cose che a 32 bit fa kernel_entry prima del C - il PIC
 * gia' rimappato e muto, l'orologio a 100 battiti - e chiama kernel_main, lo
 * stesso del kernel a 32 bit, con le informazioni che Stage 2 ha lasciato
 * all'indirizzo di sempre.
 * ============================================================================= */
#include "kernel.h"
#include "sched.h"
#include "k64.h"

#define BOOTINFO_FISICO 0xC000u

void kernel_main(BootInfo *info);

/* La voce provvisoria di k64.h: chi la usa in questa directory (idt64.c, per
 * un'eccezione prima che isr.c sia pronto) qui parla con kprintf. */
void k64_scrivi(const char *s) { kprintf("%s", s); }
void k64_esa(uint64_t v)       { kprintf("0x%08x%08x", (uint32_t)(v >> 32), (uint32_t)v); }
void k64_numero(uint64_t v)    { kprintf("%u", (uint32_t)v); }

void kmain64(void)
{
    /* il PIC: vettori 32 e 40, tutte le linee mascherate (le apre chi le usa) */
    k64_outb(0x20, 0x11); k64_outb(0xA0, 0x11);
    k64_outb(0x21, 0x20); k64_outb(0xA1, 0x28);
    k64_outb(0x21, 0x04); k64_outb(0xA1, 0x02);
    k64_outb(0x21, 0x01); k64_outb(0xA1, 0x01);
    k64_outb(0x21, 0xFF); k64_outb(0xA1, 0xFF);
    pit_configure(100);

    kernel_main((BootInfo *)DA_FISICO(BOOTINFO_FISICO));
}
