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

/* -----------------------------------------------------------------------------
 * Il volume in RAM e' arrivato intero?
 *
 * Su una chiavetta (make chiave64) la radice e' una partizione che Stage 2 ha
 * copiato in RAM leggendola col BIOS. tools/mkchiave.sh ne scrive la somma nel
 * primo settore ('EXRD' a 0x1F0, poi la somma delle parole di 32 bit col campo
 * a zero); qui la si ricalcola su cio' che c'e' in memoria e lo si DICE, in
 * una riga che resta a schermo. Quando «la shell non carica» su una macchina
 * vera, questa riga distingue un BIOS che ha letto male da tutto il resto.
 * Un volume senza la firma (il dischetto in RAM) non viene giudicato.
 * --------------------------------------------------------------------------- */
void rd_verifica(const BootInfo *info)
{
    const uint32_t *p;
    uint32_t        n, i, somma = 0, attesa;

    if (info == NULL) return;
    if (info->rd_addr == 0) {
        /* Stage 2 ha provato e non c'e' riuscito: lo ha lasciato scritto */
        klog(LOG_ERROR, "RD: il caricatore NON e' riuscito a leggere la chiavetta "
                        "col BIOS: niente volume in RAM, niente radice");
        return;
    }
    if (info->rd_byte < 512) return;
    p = (const uint32_t *)DA_FISICO(info->rd_addr);
    if (p[0x1F0 / 4] != 0x44525845u) return;            /* 'EXRD' */
    attesa = p[0x1F4 / 4];
    n = info->rd_byte / 4;
    for (i = 0; i < n; i++) if (i != 0x1F4 / 4) somma += p[i];
    if (somma == attesa)
        klog(LOG_WARN, "RD: volume in RAM INTEGRO (%u KB, somma 0x%08x)",
             info->rd_byte / 1024, somma);
    else
        klog(LOG_ERROR, "RD: volume in RAM ROTTO: somma 0x%08x, attesa 0x%08x "
                        "(%u KB) - il BIOS ha letto male la chiavetta",
             somma, attesa, info->rd_byte / 1024);
}

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
