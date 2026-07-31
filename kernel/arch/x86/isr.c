/* =============================================================================
 * kernel/arch/x86/isr.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "vga.h"
#include "idt.h"
#include "isr.h"
#include "ipc.h"
#include "syscall.h"

/* =============================================================================
 * Nomi delle eccezioni CPU (per messaggi di errore)
 * ============================================================================= */
static const char *exception_names[32] = {
    "#DE Division Error",               /*  0 */
    "#DB Debug",                        /*  1 */
    "NMI Non-Maskable Interrupt",       /*  2 */
    "#BP Breakpoint",                   /*  3 */
    "#OF Overflow",                     /*  4 */
    "#BR Bound Range Exceeded",         /*  5 */
    "#UD Invalid Opcode",               /*  6 */
    "#NM Device Not Available",         /*  7 */
    "#DF Double Fault",                 /*  8 */
    "Coprocessor Segment Overrun",      /*  9 */
    "#TS Invalid TSS",                  /* 10 */
    "#NP Segment Not Present",          /* 11 */
    "#SS Stack-Segment Fault",          /* 12 */
    "#GP General Protection Fault",     /* 13 */
    "#PF Page Fault",                   /* 14 */
    "Reserved",                         /* 15 */
    "#MF x87 FP Error",                 /* 16 */
    "#AC Alignment Check",              /* 17 */
    "#MC Machine Check",                /* 18 */
    "#XF SIMD FP Exception",            /* 19 */
    "#VE Virtualization Exception",     /* 20 */
    "#CP Control Protection",           /* 21 */
    "Reserved",                         /* 22 */
    "Reserved",                         /* 23 */
    "Reserved",                         /* 24 */
    "Reserved",                         /* 25 */
    "Reserved",                         /* 26 */
    "Reserved",                         /* 27 */
    "Reserved",                         /* 28 */
    "#HV Hypervisor Injection",         /* 29 */
    "#VC VMM Communication",            /* 30 */
    "#SX Security Exception",           /* 31 */
};

/* =============================================================================
 * Tabelle di handler registrabili
 * ============================================================================= */
static isr_handler_fn exception_handlers[32] = { NULL };
static isr_handler_fn irq_handlers[16]       = { NULL };

/* =============================================================================
 * Tabella IRQ -> PID driver ring3
 *
 * Un driver ring3 rivendica un IRQ hardware con irq_bind_process(). Da
 * quel momento, se non c'è un handler kernel-space registrato per quel
 * IRQ (caso normale per hardware gestito interamente in userspace, come
 * tastiera o floppy convertiti a driver ring3), il dispatcher consegna
 * una notifica IPC al PID proprietario invece di ignorare l'interrupt.
 * 0 = nessun proprietario (i PID validi partono da 1).
 * ============================================================================= */
static uint32_t irq_owner_pid[16] = { 0 };

/* =============================================================================
 * irq_bind_process — un driver ring3 rivendica un IRQ hardware
 *
 * Ritorna 0 su successo, <0 se il IRQ è già rivendicato da un altro
 * processo vivo (evita che due driver si contendano lo stesso hardware).
 * Sblocca l'IRQ nel PIC: da questo momento gli interrupt arrivano.
 * ============================================================================= */
int32_t irq_bind_process(uint8_t irq, uint32_t pid)
{
    if (irq >= 16) return ERR(EINVAL);

    if (irq_owner_pid[irq] != 0) {
        Process *owner = proc_get_by_pid(irq_owner_pid[irq]);
        if (owner != NULL && owner->state != PROC_UNUSED &&
            owner->state != PROC_ZOMBIE) {
            return ERR(EEXIST);  /* già rivendicato da un processo vivo */
        }
        /* Il vecchio proprietario è morto: il claim si può riassegnare */
    }

    irq_owner_pid[irq] = pid;
    pic_unmask_irq(irq);
    return 0;
}

/* irq_unbind_process — rilascia il claim (chiamata alla terminazione) */
void irq_unbind_process(uint32_t pid)
{
    for (int i = 0; i < 16; i++) {
        if (irq_owner_pid[i] == pid) {
            irq_owner_pid[i] = 0;
            pic_mask_irq((uint8_t)i);
        }
    }
}

/* =============================================================================
 * isr_register_handler — Registra handler custom per un'eccezione CPU
 * num: numero eccezione (0-31)
 * ============================================================================= */
void isr_register_handler(uint8_t num, isr_handler_fn handler)
{
    if (num < 32) {
        exception_handlers[num] = handler;
        klog(LOG_DEBUG, "ISR: handler registrato per vettore %d", num);
    }
}

/* =============================================================================
 * irq_register_handler — Registra handler per un IRQ hardware
 * irq: numero IRQ (0-15)
 * ============================================================================= */
void irq_register_handler(uint8_t irq, isr_handler_fn handler)
{
    if (irq < 16) {
        irq_handlers[irq] = handler;
        klog(LOG_DEBUG, "IRQ: handler registrato per IRQ%d", irq);
    }
}

/* =============================================================================
 * dump_registers — Stampa il contenuto di tutti i registri al momento
 * dell'eccezione. Fondamentale per il debug.
 * ============================================================================= */
static void dump_registers(InterruptFrame *f)
{
    kprintf("\n--- DUMP REGISTRI ---\n");
    kprintf("  EAX=0x%08x  EBX=0x%08x  ECX=0x%08x  EDX=0x%08x\n",
            f->eax, f->ebx, f->ecx, f->edx);
    kprintf("  ESI=0x%08x  EDI=0x%08x  EBP=0x%08x\n",
            f->esi, f->edi, f->ebp);
    kprintf("  EIP=0x%08x  CS =0x%08x  EFLAGS=0x%08x\n",
            f->eip, f->cs, f->eflags);
    kprintf("  DS =0x%08x  ERR=0x%08x  INT=%d\n",
            f->ds, f->err_code, f->int_no);

    /* Se venivamo da ring 3, stampa anche user_esp e user_ss */
    if ((f->cs & 0x3) == 3) {
        kprintf("  USP=0x%08x  USS=0x%08x  (ring 3)\n",
                f->user_esp, f->user_ss);
    }

    /* Per Page Fault: stampa CR2 (indirizzo che ha causato il fault) */
    if (f->int_no == 14) {
        uint32_t cr2 = read_cr2();
        kprintf("  CR2=0x%08x  (indirizzo page fault)\n", cr2);

        /* Decodifica error code del page fault */
        kprintf("  PF flags: %s | %s | %s\n",
                (f->err_code & 0x1) ? "protection" : "not-present",
                (f->err_code & 0x2) ? "write" : "read",
                (f->err_code & 0x4) ? "user" : "supervisor");
    }

    /* Per #GP: decodifica error code */
    if (f->int_no == 13 && f->err_code != 0) {
        kprintf("  GP selector: 0x%04x (TI=%d, RPL=%d)\n",
                f->err_code & 0xFFFC,
                (f->err_code >> 2) & 1,
                f->err_code & 0x3);
    }
    kprintf("---------------------\n");
}

/* =============================================================================
 * isr_handler — Dispatcher principale per eccezioni CPU (0-31)
 * Chiamato da isr_common_stub in isr_stubs.asm
 * ============================================================================= */
void isr_handler(InterruptFrame *frame)
{
    uint32_t int_no = frame->int_no;

    /* Se c'è un handler registrato, usa quello */
    if (int_no < 32 && exception_handlers[int_no] != NULL) {
        exception_handlers[int_no](frame);
        return;
    }

    /* Handler di default: stampa info e kpanic per eccezioni fatali */
    const char *name = (int_no < 32) ? exception_names[int_no] : "Unknown";

    /* Eccezioni non fatali gestibili */
    if (int_no == 3) {
        /* Breakpoint: utile per debug */
        klog(LOG_DEBUG, "INT3 Breakpoint a EIP=0x%x", frame->eip);
        return;
    }

    /* =========================================================================
     * Eccezione proveniente da RING3: termina il processo, non il kernel.
     *
     * Il CS salvato nel frame ha i due bit bassi uguali al CPL al momento
     * dell'eccezione: 3 = il codice girava in userspace.
     *
     * BUG CORRETTO (luglio 2026): prima QUALUNQUE eccezione senza handler
     * registrato portava a kernel panic, anche se generata da un processo
     * utente. Bastava che un programma ring3 eseguisse un'istruzione
     * privilegiata per abbattere tutto il sistema — ed era esattamente
     * ciò che accadeva con i comandi `halt` e `reboot` della shell, che
     * eseguivano `cli` e `outb` in ring3: #GP (vettore 13) e panic. Il
     * sintomo riportato era "halt mostra un kernel panic".
     *
     * L'equivalente per il #PF era già stato sistemato a giugno con lo
     * stesso ragionamento (page_fault_handler in kernel/mm/paging.c): un
     * processo che sbaglia deve morire da solo. Qui la protezione viene
     * estesa a tutte le altre eccezioni — #GP, #UD, divisione per zero.
     * Il kpanic resta per le eccezioni originate in ring0, dove indicano
     * un bug reale del kernel e proseguire sarebbe pericoloso.
     * ========================================================================= */
    if ((frame->cs & 0x3) == 3) {
        Process *p = proc_get_current();

        vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
        kprintf("\n[FAULT] PID %u '%s': eccezione %u (%s) a EIP=0x%08x "
                "err=0x%08x — processo terminato\n",
                p ? p->pid : 0, p ? p->name : "?", int_no, name,
                frame->eip, frame->err_code);
        vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

        klog(LOG_ERROR, "ISR: PID %u '%s' terminato da eccezione %u (%s) "
             "EIP=0x%08x err=0x%08x",
             p ? p->pid : 0, p ? p->name : "?", int_no, name,
             frame->eip, frame->err_code);

        /* proc_exit() non ritorna: fa il context switch verso il prossimo
         * processo pronto da qui stesso. */
        proc_exit(-4);   /* convenzione stile SIGILL/SIGSEGV */
        /* mai raggiunto */
    }

    /* Eccezione in ring0 senza handler: KERNEL PANIC */
    vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
    kprintf("\n");
    kprintf("╔══════════════════════════════════════════════╗\n");
    kprintf("║           KERNEL PANIC — ECCEZIONE CPU       ║\n");
    kprintf("╠══════════════════════════════════════════════╣\n");
    kprintf("║  Vettore : %3d                               ║\n", int_no);
    kprintf("║  Nome    : %-32s  ║\n", name);
    kprintf("║  EIP     : 0x%08x                    ║\n", frame->eip);
    kprintf("║  Errore  : 0x%08x                    ║\n", frame->err_code);
    kprintf("╚══════════════════════════════════════════════╝\n");

    dump_registers(frame);

    kprintf("\nSistema bloccato. Riavvia il computer.\n");

    /* Disabilita interrupt e halt */
    interrupts_disable();
    for(;;) {
        __asm__ volatile ("hlt");
    }
}

/* =============================================================================
 * irq_handler — Dispatcher per IRQ hardware (vettori 32-47)
 * Chiamato da irq_common_stub in isr_stubs.asm
 * ============================================================================= */
void irq_handler(InterruptFrame *frame)
{
    uint8_t irq = (uint8_t)(frame->int_no - 32);

    /* Gestisci IRQ spurio dal master PIC (IRQ7) */
    if (irq == 7) {
        /* FIX BUG #6: la sequenza corretta è prima inviare il comando OCW3
         * "leggi ISR" (0x0B) alla porta command, POI leggere il risultato
         * dalla stessa porta. L'ordine precedente era invertito: si leggeva
         * prima di inviare il comando, ottenendo un valore casuale (IRR o
         * stato precedente) invece dell'ISR effettivo. */
        port_outb(0x20, 0x0B);          /* OCW3: comando "leggi ISR" */
        uint8_t isr = port_inb(0x20);   /* Leggi In-Service Register */
        if (!(isr & 0x80)) {
            /* IRQ7 spurio: ignora (non mandare EOI) */
            return;
        }
    }

    /* Gestisci IRQ spurio dallo slave PIC (IRQ15) */
    if (irq == 15) {
        port_outb(0xA0, 0x0B);          /* OCW3 allo slave */
        uint8_t isr = port_inb(0xA0);   /* Leggi ISR slave */
        if (!(isr & 0x80)) {
            /* IRQ15 spurio: manda EOI solo al master */
            port_outb(0x20, 0x20);
            return;
        }
    }

    /* Invia End Of Interrupt al PIC PRIMA di eseguire l'handler.
     *
     * DEADLOCK RISOLTO (luglio 2026): l'EOI era inviato DOPO l'handler.
     * L'handler di IRQ0 (sched_irq0_handler) può però chiamare
     * sched_switch_to() -> context_switch(), che cambia lo stack kernel e
     * NON ritorna: riprende l'esecuzione di un altro processo. La riga
     * pic_send_eoi(irq) restava così non eseguita fino a quando il
     * processo preemptato non veniva rischedulato — cioè fino al tick
     * successivo, che il PIC però non consegnava più perché IRQ0 era
     * ancora marcato In-Service (ISR bit 0 = 1 blocca IRQ0 e tutti gli
     * IRQ di priorità inferiore).
     *
     * Sintomo: qualunque codice che attendesse l'avanzare di g_ticks dopo
     * un context switch si bloccava per sempre. Il caso tipico era
     * `hello` (o qualunque programma) lanciato dalla shell:
     *   IRQ0 -> switch alla shell (EOI mai inviato)
     *   -> shell: spawn() -> elf_load() -> fat12_read_sector()
     *   -> fdc_motor_on() -> fdc_delay_ms(300) -> hlt in attesa di g_ticks
     *   -> nessun IRQ0 possibile -> sistema congelato (irr=03 isr=01).
     *
     * L'EOI qui è sicuro: gli IRQ entrano da interrupt gate (IF=0 per
     * tutta la durata dell'handler, vedi irq_common_stub), quindi non è
     * possibile un rientro dello stesso IRQ prima dell'iret finale. */
    pic_send_eoi(irq);

    /* Chiama handler registrato se presente, altrimenti notifica il
     * driver ring3 che ha rivendicato questo IRQ (se esiste) */
    if (irq < 16 && irq_handlers[irq] != NULL) {
        irq_handlers[irq](frame);
    } else if (irq < 16 && irq_owner_pid[irq] != 0) {
        ipc_notify_irq(irq_owner_pid[irq], irq);
    } else {
        /* IRQ non gestito: log a livello debug (non panic) */
        klog(LOG_DEBUG, "IRQ%d non gestito (vettore %d)", irq, frame->int_no);
    }
}

/* =============================================================================
 * isr_install — Inizializza il sistema ISR
 * Chiamata da kernel_main() dopo idt_install()
 * ============================================================================= */
void isr_install(void)
{
    uint8_t i;

    /* Azzera tutte le tabelle di handler */
    for (i = 0; i < 32; i++) exception_handlers[i] = NULL;
    for (i = 0; i < 16; i++) irq_handlers[i] = NULL;

    /* Maschera tutti gli IRQ (verranno abilitati dai driver) */
    port_outb(0x21, 0xFF);  /* Master PIC: maschera tutto */
    port_outb(0xA1, 0xFF);  /* Slave PIC: maschera tutto */

    klog(LOG_INFO, "ISR: handler installati, tutti gli IRQ mascherati");
}
