/* =============================================================================
 * kernel/arch/x86_64/colla64.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LA COLLA: quel che il kernel comune chiede e a 64 bit non c'e' ancora
 * (@EXOS-64, tappa 3 — 10 ottobre 2026)
 *
 * Il registro dei messaggi (kprintf.c), l'allocatore delle pagine (pmm.c) e
 * lo heap (kmalloc.c) sono file COMUNI: qui si collegano tali e quali. Ma
 * chiamano tre funzioni della console e una degli interrupt che nel kernel a
 * 32 bit stanno in vga.c e in entry.asm. Queste sono le loro controfigure:
 * scrivono sulla seriale e sullo schermo di testo, come main64.c.
 *
 * ! E' UN FILE CHE DEVE SPARIRE, un pezzo per volta: ogni funzione qui dentro
 * e' un debito con il file vero. Quando vga.c sara' collegato le tre della
 * console se ne vanno, e cosi' via. Finche' c'e', dice quanto manca.
 * ============================================================================= */
#include "k64.h"

static void uno(char c)
{
    char b[2];

    b[0] = c; b[1] = '\0';
    k64_scrivi(b);
}

void vga_putchar(char c)                    { uno(c); }
void vga_clear(void)                        { }
void vga_setcolor(uint8_t fg, uint8_t bg)   { (void)fg; (void)bg; }

void interrupts_disable(void) { __asm__ volatile("cli"); }
void interrupts_enable(void)  { __asm__ volatile("sti"); }

/* Lo scambio su disco (swap.c) arriva con i processi. Fino ad allora non c'e'
 * niente da mandare via e nessuno slot da rendere: la paginazione, a memoria
 * finita, lo dice invece di fare posto. */
int  swap_sfratta(void)             { return 0; }
void swap_slot_molla(uint32_t slot) { (void)slot; }
