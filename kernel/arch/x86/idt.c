/* =============================================================================
 * kernel/arch/x86/idt.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "idt.h"

/* =============================================================================
 * Struttura di un gate descriptor IDT (8 byte)
 *
 * Layout:
 *  Byte 0-1:  offset[15:0]   — parte bassa indirizzo handler
 *  Byte 2-3:  selector        — selettore segmento codice (0x08 = kernel code)
 *  Byte 4:    always_zero     — riservato, deve essere 0
 *  Byte 5:    flags           — tipo gate + DPL + present
 *  Byte 6-7:  offset[31:16]  — parte alta indirizzo handler
 *
 * Flags:
 *  Bit 7:   Present (P)      — 1 = gate valido
 *  Bit 6-5: DPL              — 0=ring0 (eccezioni/IRQ), 3=ring3 (syscall)
 *  Bit 4:   Storage Segment  — sempre 0 per interrupt gate
 *  Bit 3-0: Gate type:
 *    0xE = 1110b = 32-bit Interrupt Gate  (disabilita interrupt all'ingresso)
 *    0xF = 1111b = 32-bit Trap Gate       (non disabilita interrupt)
 *
 * Usiamo Interrupt Gate (0xE) per IRQ e eccezioni,
 * Trap Gate (0xF) per syscall (int 0x80).
 * ============================================================================= */

typedef struct PACKED {
    uint16_t    offset_low;     /* Handler[15:0] */
    uint16_t    selector;       /* Selettore segmento codice */
    uint8_t     always_zero;    /* Riservato */
    uint8_t     flags;          /* P | DPL | 0 | Type */
    uint16_t    offset_high;    /* Handler[31:16] */
} IDTGate;

/* IDTR per istruzione lidt */
typedef struct PACKED {
    uint16_t    limit;          /* sizeof(IDT) - 1 */
    uint32_t    base;           /* Indirizzo fisico IDT */
} IDTRegister;

/* =============================================================================
 * Tabella IDT (256 entry * 8 byte = 2048 byte)
 * ============================================================================= */
#define IDT_ENTRIES 256

static IDTGate    idt_table[IDT_ENTRIES] ALIGNED(8);
static IDTRegister idt_reg;

/* =============================================================================
 * idt_set_gate — Imposta un gate nella IDT
 *
 * num:     numero vettore (0-255)
 * handler: indirizzo della funzione handler (ISR stub da isr_stubs.asm)
 * sel:     selettore segmento (0x08 = kernel code)
 * flags:   0x8E = interrupt gate ring0, 0xEE = interrupt gate ring3
 * ============================================================================= */
void idt_set_gate(uint8_t num, uint32_t handler, uint16_t sel, uint8_t flags)
{
    idt_table[num].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt_table[num].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
    idt_table[num].selector    = sel;
    idt_table[num].always_zero = 0;
    idt_table[num].flags       = flags;
}

/* =============================================================================
 * remap_pic — Rimappa il PIC 8259 per evitare conflitti con eccezioni CPU
 *
 * Default BIOS: IRQ0-7 → INT 08h-0Fh (conflitto con eccezioni CPU 8-15!)
 * Nuovo mapping: IRQ0-7 → INT 32-39, IRQ8-15 → INT 40-47
 *
 * Processo di inizializzazione PIC (ICW1-ICW4):
 * ============================================================================= */
static void remap_pic(void)
{
    /* Salva maschere interrupt correnti */
    uint8_t mask1 = port_inb(0x21);
    uint8_t mask2 = port_inb(0xA1);

    /* ICW1: Inizializza entrambi i PIC (cascade mode, ICW4 needed) */
    port_outb(0x20, 0x11);  /* Master PIC command: init */
    io_delay();
    port_outb(0xA0, 0x11);  /* Slave PIC command: init */
    io_delay();

    /* ICW2: Vettore base */
    port_outb(0x21, 0x20);  /* Master: IRQ0 → INT 32 (0x20) */
    io_delay();
    port_outb(0xA1, 0x28);  /* Slave:  IRQ8 → INT 40 (0x28) */
    io_delay();

    /* ICW3: Cascading */
    port_outb(0x21, 0x04);  /* Master: slave connesso a IRQ2 (bit mask: bit 2) */
    io_delay();
    port_outb(0xA1, 0x02);  /* Slave:  cascade identity = 2 */
    io_delay();

    /* ICW4: Modalità 8086 */
    port_outb(0x21, 0x01);
    io_delay();
    port_outb(0xA1, 0x01);
    io_delay();

    /* Ripristina maschere (tutti gli IRQ mascherati inizialmente) */
    port_outb(0x21, mask1);
    port_outb(0xA1, mask2);
}

/* =============================================================================
 * Dichiarazioni external: stub ISR in isr_stubs.asm
 * Ogni stub salva i registri, pusha il numero vettore e chiama isr_handler()
 * ============================================================================= */

/* Eccezioni CPU (0-31) */
extern void isr0(void);   /* #DE Division Error */
extern void isr1(void);   /* #DB Debug */
extern void isr2(void);   /* NMI */
extern void isr3(void);   /* #BP Breakpoint */
extern void isr4(void);   /* #OF Overflow */
extern void isr5(void);   /* #BR Bound Range Exceeded */
extern void isr6(void);   /* #UD Invalid Opcode */
extern void isr7(void);   /* #NM Device Not Available */
extern void isr8(void);   /* #DF Double Fault (con error code) */
extern void isr9(void);   /* Coprocessor Segment Overrun (obsoleto) */
extern void isr10(void);  /* #TS Invalid TSS (con error code) */
extern void isr11(void);  /* #NP Segment Not Present (con error code) */
extern void isr12(void);  /* #SS Stack Fault (con error code) */
extern void isr13(void);  /* #GP General Protection Fault (con error code) */
extern void isr14(void);  /* #PF Page Fault (con error code, CR2 = indirizzo) */
extern void isr15(void);  /* Riservato */
extern void isr16(void);  /* #MF x87 FPU Error */
extern void isr17(void);  /* #AC Alignment Check */
extern void isr18(void);  /* #MC Machine Check */
extern void isr19(void);  /* #XM SIMD FP Exception */
extern void isr20(void);  /* #VE Virtualization Exception */
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

/* IRQ hardware (32-47) */
extern void irq0(void);   /* Timer PIT */
extern void irq1(void);   /* Tastiera PS/2 */
extern void irq2(void);   /* Cascade (non usato) */
extern void irq3(void);   /* COM2 */
extern void irq4(void);   /* COM1 */
extern void irq5(void);   /* LPT2 / Sound */
extern void irq6(void);   /* Floppy */
extern void irq7(void);   /* LPT1 / Spurious */
extern void irq8(void);   /* RTC */
extern void irq9(void);   /* ACPI */
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);  /* PS/2 Mouse */
extern void irq13(void);  /* FPU */
extern void irq14(void);  /* ATA Primary */
extern void irq15(void);  /* ATA Secondary */

/* Syscall */
extern void isr128(void); /* int 0x80 — syscall interface */

/* =============================================================================
 * idt_install — Installa la IDT completa
 * ============================================================================= */
void idt_install(void)
{
    uint32_t i;

    klog(LOG_INFO, "IDT: inizializzazione...");

    /* Azzera tutta la IDT */
    for (i = 0; i < IDT_ENTRIES; i++) {
        idt_table[i].offset_low  = 0;
        idt_table[i].offset_high = 0;
        idt_table[i].selector    = 0;
        idt_table[i].always_zero = 0;
        idt_table[i].flags       = 0;
    }

    /* Rimappa PIC prima di installare i gate IRQ */
    remap_pic();
    klog(LOG_INFO, "IDT: PIC rimappato (IRQ0→INT32, IRQ8→INT40)");

    /* Installa gate eccezioni CPU (0-31) — ring 0, interrupt gate 0x8E */
    #define IGATE0  0x8E    /* P=1, DPL=0, Type=E (interrupt gate 32-bit) */
    idt_set_gate(0,  (uint32_t)isr0,  0x08, IGATE0);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, IGATE0);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, IGATE0);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, IGATE0);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, IGATE0);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, IGATE0);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, IGATE0);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, IGATE0);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, IGATE0);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, IGATE0);
    idt_set_gate(10, (uint32_t)isr10, 0x08, IGATE0);
    idt_set_gate(11, (uint32_t)isr11, 0x08, IGATE0);
    idt_set_gate(12, (uint32_t)isr12, 0x08, IGATE0);
    idt_set_gate(13, (uint32_t)isr13, 0x08, IGATE0);
    idt_set_gate(14, (uint32_t)isr14, 0x08, IGATE0);
    idt_set_gate(15, (uint32_t)isr15, 0x08, IGATE0);
    idt_set_gate(16, (uint32_t)isr16, 0x08, IGATE0);
    idt_set_gate(17, (uint32_t)isr17, 0x08, IGATE0);
    idt_set_gate(18, (uint32_t)isr18, 0x08, IGATE0);
    idt_set_gate(19, (uint32_t)isr19, 0x08, IGATE0);
    idt_set_gate(20, (uint32_t)isr20, 0x08, IGATE0);
    idt_set_gate(21, (uint32_t)isr21, 0x08, IGATE0);
    idt_set_gate(22, (uint32_t)isr22, 0x08, IGATE0);
    idt_set_gate(23, (uint32_t)isr23, 0x08, IGATE0);
    idt_set_gate(24, (uint32_t)isr24, 0x08, IGATE0);
    idt_set_gate(25, (uint32_t)isr25, 0x08, IGATE0);
    idt_set_gate(26, (uint32_t)isr26, 0x08, IGATE0);
    idt_set_gate(27, (uint32_t)isr27, 0x08, IGATE0);
    idt_set_gate(28, (uint32_t)isr28, 0x08, IGATE0);
    idt_set_gate(29, (uint32_t)isr29, 0x08, IGATE0);
    idt_set_gate(30, (uint32_t)isr30, 0x08, IGATE0);
    idt_set_gate(31, (uint32_t)isr31, 0x08, IGATE0);

    /* Installa gate IRQ hardware (32-47) */
    idt_set_gate(32, (uint32_t)irq0,  0x08, IGATE0);
    idt_set_gate(33, (uint32_t)irq1,  0x08, IGATE0);
    idt_set_gate(34, (uint32_t)irq2,  0x08, IGATE0);
    idt_set_gate(35, (uint32_t)irq3,  0x08, IGATE0);
    idt_set_gate(36, (uint32_t)irq4,  0x08, IGATE0);
    idt_set_gate(37, (uint32_t)irq5,  0x08, IGATE0);
    idt_set_gate(38, (uint32_t)irq6,  0x08, IGATE0);
    idt_set_gate(39, (uint32_t)irq7,  0x08, IGATE0);
    idt_set_gate(40, (uint32_t)irq8,  0x08, IGATE0);
    idt_set_gate(41, (uint32_t)irq9,  0x08, IGATE0);
    idt_set_gate(42, (uint32_t)irq10, 0x08, IGATE0);
    idt_set_gate(43, (uint32_t)irq11, 0x08, IGATE0);
    idt_set_gate(44, (uint32_t)irq12, 0x08, IGATE0);
    idt_set_gate(45, (uint32_t)irq13, 0x08, IGATE0);
    idt_set_gate(46, (uint32_t)irq14, 0x08, IGATE0);
    idt_set_gate(47, (uint32_t)irq15, 0x08, IGATE0);

    /* Syscall int 0x80 — DPL=3 così i processi user possono invocarlo.
     * TRAP Gate (0xEF, Type=F): lascia IF invariato all'ingresso, quindi
     * gli interrupt restano abilitati durante la syscall. Necessario per
     * SYS_SPAWN/SYS_EXEC: chiamano elf_load() che legge settori dal floppy
     * usando fdc_delay_ms() (basata su g_ticks/IRQ0). Con un Interrupt Gate
     * (0xEE, Type=E) IF verrebbe azzerato → fdc_delay_ms → hlt infinito. */
    #define TGATE3  0xEF    /* P=1, DPL=3, Type=F (trap gate ring3) */
    idt_set_gate(128, (uint32_t)isr128, 0x08, TGATE3);

    /* Carica IDTR */
    idt_reg.limit = (uint16_t)(sizeof(idt_table) - 1);
    idt_reg.base  = (uint32_t)idt_table;

    idt_flush((uint32_t)&idt_reg);

    klog(LOG_INFO, "IDT: installata (%d gate, syscall=int 0x80)", IDT_ENTRIES);
}

/* =============================================================================
 * pic_send_eoi — Invia End Of Interrupt al PIC
 * Deve essere chiamata alla fine di ogni IRQ handler
 * ============================================================================= */
void pic_send_eoi(uint8_t irq)
{
    if (irq >= 8) {
        /* IRQ dal slave PIC: EOI a entrambi */
        port_outb(0xA0, 0x20);
    }
    /* EOI al master PIC sempre */
    port_outb(0x20, 0x20);
}

/* =============================================================================
 * pic_mask_irq / pic_unmask_irq — Abilita/disabilita singoli IRQ
 *
 * ⚠️ GLI IRQ 8-15 NON ARRIVANO ALLA CPU DA SOLI: PASSANO DA IRQ2.
 *
 * I due 8259 sono in cascata. Lo slave non ha un piedino verso la CPU:
 * quando ha un interrupt da consegnare alza la linea INT del MASTER, che
 * la vede come IRQ2. Se IRQ2 è mascherato nel master, tutto quello che
 * arriva dallo slave — IRQ8..IRQ15 — resta fuori, per quanto lo si
 * sblocchi nel registro dello slave.
 *
 * Ed è esattamente quello che succedeva fino ad agosto 2026: isr_install()
 * maschera tutto, ogni driver sblocca il proprio IRQ, e per gli IRQ bassi
 * (tastiera 1, timer 0, floppy 6) funzionava. Il primo IRQ alto è arrivato
 * con la scheda di rete su IRQ11, e non è arrivato affatto: il driver
 * riceveva ZERO notifiche e la rete andava lo stesso, perché il driver
 * guarda la scheda anche a scadenza. Un guasto che non rompe niente e
 * rallenta tutto è il tipo peggiore — l'ho trovato solo mettendo un
 * contatore delle notifiche accanto a uno dei risvegli a scadenza, e
 * leggendo `notifiche IRQ 0, battiti 131`.
 *
 * Percio' sbloccare un IRQ >= 8 sblocca anche IRQ2. E mascherarlo NON lo
 * rimaschera: IRQ2 non è una linea di nessuno, è la strada per otto
 * dispositivi, e chiuderla perché uno di loro ha finito toglierebbe
 * l'interrupt agli altri sette.
 * ============================================================================= */
#define PIC_CASCATA 2   /* la linea del master a cui è appeso lo slave */

void pic_mask_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t  mask;

    if (irq < 8) {
        port = 0x21;    /* Master */
    } else {
        port = 0xA1;    /* Slave */
        irq -= 8;
    }
    mask = port_inb(port) | (uint8_t)(1 << irq);
    port_outb(port, mask);
}

void pic_unmask_irq(uint8_t irq)
{
    uint16_t port;
    uint8_t  mask;
    int      dallo_slave = (irq >= 8);

    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq -= 8;
    }
    mask = port_inb(port) & (uint8_t)~(1 << irq);
    port_outb(port, mask);

    /* La cascata si apre DOPO, non prima: fra le due scritture il master
     * potrebbe lasciar passare un interrupt dello slave che nello slave è
     * ancora mascherato, e il PIC lo consegnerebbe come IRQ7 spurio. */
    if (dallo_slave) {
        mask = port_inb(0x21) & (uint8_t)~(1 << PIC_CASCATA);
        port_outb(0x21, mask);
    }
}
