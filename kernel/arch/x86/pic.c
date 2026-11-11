/* =============================================================================
 * kernel/arch/x86/pic.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * THE TWO 8259s, FOR BOTH KERNELS (0.324, voce 98). Was copy-pasted in idt.c
 * and idt64.c, and read the mask back from the chip. Bullshit, now:
 *
 * ! THE TRUE MASK LIVES HERE (g_imr), NOT IN THE CHIP. While processor 0
 * waits for the kernel lock with an edge line on, the chip gets a NARROWER
 * mask (pic_attesa). Read it back, write it back as "the" mask, and the lines
 * closed for the wait stay closed forever. Machine fucked, nobody knows why.
 *
 * ! ONE LOCK FOR THE CHIP, interrupts off. irq_prima talks to it WITHOUT the
 * kernel lock; "read ISR" is two accesses, and another processor writing in
 * between makes the read return garbage.
 *
 * ! IRQ 8-15 GO THROUGH IRQ 2. Unmasking a slave line opens the cascade too;
 * masking never closes it (seven other devices ride it). August 2026: the NIC
 * on IRQ 11 got ZERO interrupts and "worked" on timeouts. Slow and stupid.
 * ============================================================================= */

#include "kernel.h"
#include "idt.h"

#define PIC_CASCATA 2   /* the master's line the slave hangs on */

static uint8_t           g_imr[2] = { 0xFF, 0xFF };  /* the true mask: master, slave */
static uint16_t          g_aperte;                   /* lines open during a wait */
static uint32_t          g_attese;                   /* processors waiting so (0 or 1) */
static volatile uint32_t g_pic_ll;                   /* the chip's lock */

static uintptr_t pic_chiudi(void)
{
    uintptr_t flag = read_eflags();

    interrupts_disable();
    while (__sync_lock_test_and_set(&g_pic_ll, 1u))
        while (g_pic_ll) __asm__ volatile ("pause");
    return flag;
}

static void pic_apri(uintptr_t flag)
{
    __sync_lock_release(&g_pic_ll);
    if (flag & 0x200u) interrupts_enable();
}

/* What the chip gets: the true mask, narrowed to the edge lines (and the
 * cascade, if one of them is on the slave) while a processor waits. */
static void pic_scrivi(void)
{
    uint8_t m = g_imr[0], s = g_imr[1];

    if (g_attese) {
        uint8_t am = (uint8_t)g_aperte, as = (uint8_t)(g_aperte >> 8);

        if (as) am |= (uint8_t)(1u << PIC_CASCATA);
        m |= (uint8_t)~am;
        s |= (uint8_t)~as;
    }
    port_outb(0xA1, s);         /* the slave first: see pic_unmask_irq */
    port_outb(0x21, m);
}

/* From isr_install: everything masked, the drivers open their lines. */
void pic_maschera_tutto(void)
{
    uintptr_t f = pic_chiudi();

    g_imr[0] = 0xFF; g_imr[1] = 0xFF;
    pic_scrivi();
    pic_apri(f);
}

void pic_send_eoi(uint8_t irq)
{
    uintptr_t f = pic_chiudi();

    if (irq >= 8) port_outb(0xA0, 0x20);    /* from the slave: both */
    port_outb(0x20, 0x20);
    pic_apri(f);
}

void pic_mask_irq(uint8_t irq)
{
    uintptr_t f;

    if (irq >= 16) return;
    f = pic_chiudi();
    g_imr[irq >> 3] |= (uint8_t)(1u << (irq & 7));
    pic_scrivi();
    pic_apri(f);
}

void pic_unmask_irq(uint8_t irq)
{
    uintptr_t f;

    if (irq >= 16) return;
    f = pic_chiudi();
    g_imr[irq >> 3] &= (uint8_t)~(1u << (irq & 7));
    /* ! The cascade AFTER the slave's line, never before: between the two
     * writes the master could let through an interrupt of the slave still
     * masked there, and the PIC would deliver it as a spurious IRQ 7. Here
     * both reach the chip in one pic_scrivi, which writes the slave first. */
    if (irq >= 8) g_imr[0] &= (uint8_t)~(1u << PIC_CASCATA);
    pic_scrivi();
    pic_apri(f);
}

/* The true mask, for whoever shows it (kbdprova, sonde). */
uint16_t pic_maschera(void)
{
    return (uint16_t)(g_imr[0] | ((uint16_t)g_imr[1] << 8));
}

/* ! SPURIOUS = NOTHING IN SERVICE: the request dropped before the CPU took it.
 * "Read ISR" (OCW3 0x0B) FIRST, then read (the other way round read garbage,
 * bug #6). A spurious 15 still needs the master's EOI, never the slave's.
 * 1 = spurious, 0 = real one, EOI sent. */
int pic_fine_o_spurio(uint8_t irq)
{
    uintptr_t f = pic_chiudi();
    int spurio = 0;

    if (irq == 7) {
        port_outb(0x20, 0x0B);
        if (!(port_inb(0x20) & 0x80)) spurio = 1;
    } else if (irq == 15) {
        port_outb(0xA0, 0x0B);
        if (!(port_inb(0xA0) & 0x80)) { port_outb(0x20, 0x20); spurio = 1; }
    }
    if (!spurio) {
        if (irq >= 8) port_outb(0xA0, 0x20);
        port_outb(0x20, 0x20);
    }
    pic_apri(f);
    return spurio;
}

/* Processor 0 waiting for the kernel lock: 1 = only `aperte` get through,
 * 0 = all back. Nothing is lost: the stupid 8259 keeps a masked edge in IRR,
 * and a level line is still up when the mask opens. */
void pic_attesa(int inizio, uint16_t aperte)
{
    uintptr_t f = pic_chiudi();

    if (inizio) { g_aperte = aperte; g_attese = 1; }
    else        { g_attese = 0; }
    pic_scrivi();
    pic_apri(f);
}
