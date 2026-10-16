/* =============================================================================
 * kernel/arch/x86/power.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Arresto, spegnimento e riavvio del sistema.
 *
 * PERCHÉ ESISTE (luglio 2026): il comando `halt` della shell eseguiva
 * direttamente `cli; hlt`, e `reboot` faceva `inb`/`outb` sulla porta
 * 0x64 — istruzioni PRIVILEGIATE, eseguite da un processo ring3. Il
 * risultato era un #GP (vettore 13) che, non avendo un handler
 * registrato, finiva nel ramo di default di isr_handler() e produceva un
 * KERNEL PANIC. Da cui il sintomo riportato: "halt mostra un kernel
 * panic". Anche `reboot` era rotto allo stesso modo.
 *
 * Ora la sequenza sta nel kernel, dove le istruzioni privilegiate sono
 * legali, e la shell la richiede con SYS_REBOOT.
 *
 * ---------------------------------------------------------------------------
 * SPEGNIMENTO HARDWARE: cosa è realisticamente possibile
 *
 * Non esiste un'istruzione x86 "spegni il computer". Lo spegnimento è una
 * funzione del chipset, e ci si arriva in tre modi:
 *
 *   1. ACPI — si legge la tabella FADT per trovare la porta PM1a_CNT e i
 *      valori SLP_TYP dell'stato S5, poi ci si scrive sopra. È la strada
 *      corretta e generale, ma richiede un parser ACPI (RSDP -> RSDT ->
 *      FADT -> DSDT per SLP_TYP) che questo kernel non ha.
 *
 *   2. APM — chiamate BIOS INT 15h. Funzionerebbe sull'hardware d'epoca
 *      (Pentium II), ma INT 15h è una chiamata in REAL MODE: da protected
 *      mode servirebbe tornare in real mode o costruire un monitor
 *      virtual-8086. Entrambi sono lavori a sé.
 *
 *   3. Porte note degli emulatori — QEMU, Bochs e VirtualBox espongono la
 *      PM1a_CNT a indirizzi fissi e documentati. Si scrive il comando di
 *      sleep S5 direttamente, senza ACPI.
 *
 * Qui è implementata la (3), con la (1) e la (2) annotate come lavoro
 * futuro. Conseguenza da tenere presente: **in QEMU/Bochs/VirtualBox la
 * macchina si spegne davvero; su hardware reale, con ogni probabilità
 * no** — resterà nel loop di halt con il messaggio "sicuro spegnere".
 * Non è un fallimento silenzioso: il sistema è comunque in uno stato
 * sicuro, con il filesystem sincronizzato e gli interrupt disabilitati.
 * ============================================================================= */

#include "kernel.h"
#include "vga.h"
#include "sched.h"
#include "fat12.h"
#include "power.h"
#include "acpi.h"

/* =============================================================================
 * Porte di spegnimento degli ambienti virtualizzati.
 *
 * Il valore scritto è un comando ACPI PM1a_CNT: SLP_TYP nei bit 10-12 e
 * SLP_EN (bit 13) a 1. 0x2000 = solo SLP_EN, accettato da QEMU e Bochs;
 * 0x3400 = SLP_TYP=5 (soft off) + SLP_EN, richiesto da VirtualBox.
 *
 * Si provano tutte in sequenza: la prima che funziona spegne la macchina
 * e le successive non vengono mai eseguite. Scrivere su una porta non
 * implementata è innocuo — viene semplicemente ignorata.
 * ============================================================================= */
typedef struct {
    uint16_t    port;
    uint16_t    value;
    const char *nome;
} PowerOffPort;

static const PowerOffPort poweroff_ports[] = {
    { 0x0604, 0x2000, "QEMU (ACPI PM1a_CNT)"   },
    { 0xB004, 0x2000, "Bochs / QEMU legacy"     },
    { 0x4004, 0x3400, "VirtualBox"              },
};
#define POWEROFF_PORT_COUNT \
    (sizeof(poweroff_ports) / sizeof(poweroff_ports[0]))

/* =============================================================================
 * power_sync — mette il sistema in uno stato sicuro da spegnere
 *
 * Il floppy ha una cache in scrittura e mantiene FAT e root directory in
 * RAM con un flag "dirty": spegnere senza sincronizzare significherebbe
 * perdere le modifiche e, peggio, lasciare un filesystem incoerente.
 * Va fatto PRIMA di disabilitare gli interrupt, perché il driver FDC usa
 * attese basate su g_ticks e sull'IRQ6 — con IF=0 si bloccherebbe per
 * sempre in fdc_delay_ms.
 * ============================================================================= */
static void power_sync(void)
{
    int r;

    kprintf("  Sincronizzazione filesystem...");
    r = fat12_sync();
    if (r == 0) {
        kprintf(" ok\n");
    } else {
        /* Non fermiamo la procedura: l'utente ha chiesto di spegnere e
         * insistere non recupererebbe nulla. Ma lo deve sapere. */
        kprintf("\n");
        klog(LOG_ERROR, "POWER: sincronizzazione filesystem fallita (%d) - "
             "possibile perdita di dati non salvati", r);
    }
}

/* =============================================================================
 * power_countdown — conto alla rovescia visibile prima dello spegnimento
 *
 * Richiede interrupt abilitati: si appoggia a g_ticks (PIT a 100Hz), non
 * a cicli di CPU — lo stesso motivo per cui i ritardi dell'FDC sono stati
 * convertiti a giugno. Un conteggio a NOP durerebbe tempi diversi su CPU
 * diverse, e "3 secondi" sarebbero 3 secondi solo sulla macchina su cui
 * è stato tarato.
 * ============================================================================= */
static void power_countdown(uint32_t secondi)
{
    uint32_t s;

    for (s = secondi; s > 0; s--) {
        uint32_t target = g_ticks + 100;   /* 100 tick = 1 s a 100Hz */

        kprintf("  Spegnimento fra %u...\n", s);

        while (g_ticks < target) {
            __asm__ volatile ("hlt");
        }
    }
}

/* =============================================================================
 * power_try_off — tenta lo spegnimento hardware
 *
 * Ritorna solo se NESSUN metodo ha funzionato.
 * ============================================================================= */
static void power_try_off(void)
{
    uint32_t i;

    for (i = 0; i < POWEROFF_PORT_COUNT; i++) {
        port_outw(poweroff_ports[i].port, poweroff_ports[i].value);

        /* Se la macchina si è spenta non siamo più qui. Se siamo ancora
         * qui quella porta non è implementata: passa alla prossima. */
    }
}

/* =============================================================================
 * power_rest — stato finale sicuro
 *
 * Interrupt disabilitati e CPU in halt. `cli` prima di `hlt` è
 * essenziale: senza, il primo IRQ0 risveglierebbe la CPU e il loop
 * ripartirebbe consumando corrente inutilmente — e soprattutto lo
 * scheduler potrebbe rientrare in gioco su un sistema che abbiamo appena
 * dichiarato fermo.
 * ============================================================================= */
static NORETURN void power_rest(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* =============================================================================
 * power_off — spegne il sistema (o lo ferma, se l'hardware non collabora)
 * ============================================================================= */
void power_off(void)
{
    interrupts_enable();   /* servono per sync e countdown */

    vga_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    kprintf("\n  Arresto del sistema in corso.\n");
    vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    power_sync();
    sched_stop();          /* niente più context switch da qui in poi */
    power_countdown(3);

    kprintf("  Spegnimento hardware...\n");
    interrupts_disable();
    acpi_spegni();         /* a real PC: S5 through the FADT (kernel/arch/x86/acpi.c) */
    power_try_off();       /* the emulators' fixed ports */

    /* Arrivati qui, nessun metodo di spegnimento ha funzionato: è il caso
     * atteso su hardware reale senza ACPI. Il sistema resta comunque in
     * uno stato sicuro — filesystem sincronizzato, scheduler fermo,
     * interrupt disabilitati. */
    vga_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    kprintf("\n  Sistema arrestato. E' ora sicuro spegnere il computer.\n");
    vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    power_rest();
}

/* =============================================================================
 * power_halt — ferma il sistema senza tentare lo spegnimento
 *
 * Differenza da power_off(): nessun conto alla rovescia e nessuna
 * scrittura sulle porte ACPI. Serve a chi vuole leggere lo schermo prima
 * di togliere corrente, o su macchine dove lo spegnimento software non è
 * desiderato.
 * ============================================================================= */
void power_halt(void)
{
    interrupts_enable();

    vga_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    kprintf("\n  Arresto del sistema in corso.\n");
    vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    power_sync();
    sched_stop();

    vga_setcolor(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    kprintf("\n  Sistema arrestato. E' ora sicuro spegnere il computer.\n");
    vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    interrupts_disable();
    power_rest();
}

/* =============================================================================
 * power_reboot — riavvio via keyboard controller
 *
 * Il metodo classico: impulso sulla linea di reset della CPU tramite il
 * comando 0xFE del controller 8042. Va atteso che il buffer di ingresso
 * sia libero (bit 1 dello status), altrimenti il comando viene perso.
 *
 * Se il KBC non risponde si prova la "triple fault": si carica una IDT
 * vuota e si genera un interrupt. La CPU non trova il gestore, non trova
 * il gestore del doppio fault, e si resetta. È brutale ma funziona
 * ovunque — è la rete di sicurezza dei bootloader.
 * ============================================================================= */
/* About one microsecond per write to the POST port, as Linux's io_delay:
 * a count of `pause` lasted under a millisecond on the laptop's Athlon, so
 * every method was given no time before the next one started. */
static void attesa_us(uint32_t us)
{
    while (us--) port_outb(0x80, 0);
}

void power_reboot(void)
{
    uint32_t timeout;
    int      i;

    interrupts_enable();

    vga_setcolor(VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    kprintf("\n  Riavvio del sistema in corso.\n");
    vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

    power_sync();
    sched_stop();

    interrupts_disable();

    /* ! FOUR WAYS, MOST RELIABLE FIRST (14 October 2026): the user's MCP77
     * laptop "just hangs" on reboot. 0) the ACPI reset register, when the
     * firmware declares one; 1) the keyboard controller, as before; 2) the
     * chipset reset port 0xCF9 (2 = reset next, 6 = hard reset now), which
     * Intel and NVIDIA chipsets have; 3) the triple fault. */
    /* ! EACH STEP ON THE SCREEN (15 October 2026): the laptop stopped
     * after "Sincronizzazione filesystem... ok" and the warnings went only
     * to the log, so nobody could say which method had hung. */
    if (acpi_ha_reset()) {
        kprintf("  Riavvio: registro di reset ACPI\n");
        acpi_riavvia();
        attesa_us(500000);
    }

    /* ! 0xCF9 BEFORE THE 8042 (15 October 2026): on the MCP77 laptop the
     * machine stopped for good right after "Riavvio: controller della
     * tastiera (8042)" - most likely the BIOS's SMM handler that traps the
     * keyboard ports for USB legacy emulation, which never returns once
     * EX-OS owns the USB controllers. Where 0xCF9 does not exist the write
     * is an unclaimed I/O cycle and nothing happens. */

    /* Metodo 1: the chipset's reset port */
    kprintf("  Riavvio: porta 0xCF9\n");
    port_outb(0xCF9, 0x02);
    attesa_us(50);
    port_outb(0xCF9, 0x06);
    attesa_us(500000);

    /* Metodo 2: reset via 8042, ten times as Linux does, each one shown */
    kprintf("  Riavvio: controller della tastiera (8042)\n");
    for (i = 0; i < 10; i++) {
        timeout = 100000;
        while ((port_inb(0x64) & 0x02) && timeout--) attesa_us(1);
        kprintf("    tentativo %d: stato 0x%02x, mando 0xFE\n", i + 1, port_inb(0x64));
        attesa_us(50);
        port_outb(0x64, 0xFE);
        attesa_us(50);
    }
    attesa_us(500000);

    /* Metodo 3: triple fault */
    kprintf("  Riavvio: triple fault\n");
    {
        /* uintptr_t: at 64 bit lidt reads a 10-byte descriptor */
        struct { uint16_t limit; uintptr_t base; } __attribute__((packed))
            idt_nulla = { 0, 0 };
        __asm__ volatile ("lidt %0; int $0x03" :: "m"(idt_nulla));
    }

    /* Se anche il triple fault fallisce non resta che fermarsi. */
    power_rest();
}
