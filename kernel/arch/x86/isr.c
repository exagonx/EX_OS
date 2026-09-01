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
#include "entropia.h"
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
 * Tabella IRQ -> PID dei driver ring3
 *
 * Un driver ring3 rivendica un IRQ hardware con irq_bind_process(). Da
 * quel momento, se non c'è un handler kernel-space registrato per quel
 * IRQ (caso normale per hardware gestito interamente in userspace, come
 * tastiera o floppy convertiti a driver ring3), il dispatcher consegna
 * una notifica IPC ai PID proprietari invece di ignorare l'interrupt.
 * 0 = slot libero (i PID validi partono da 1).
 *
 * -----------------------------------------------------------------------------
 * ! PIU' DI UN PROPRIETARIO, dal 1 settembre 2026, E NON E' UN LUSSO
 *
 * Fino a quel giorno era un PID solo, con accanto scritto «evita che due
 * driver si contendano lo stesso hardware». La frase era giusta per l'ISA,
 * dove una linea appartiene a una scheda e basta. SUL PCI E' FALSA: le quattro
 * linee INTA-INTD di uno slot si distribuiscono a rotazione fra gli slot, e
 * una macchina con più di quattro dispositivi ne ha per forza due sulla
 * stessa. Non è un caso raro: è la configurazione normale.
 *
 * ! E SI E' VISTO SUBITO. Il primo driver audio PCI provato in QEMU trovava
 * la scheda, leggeva i BAR, e moriva su `irq_bind(11) fallita (-17)`: l'IRQ 11
 * ce l'aveva la scheda di rete, avviata trenta secondi prima da /boot/avvio.sh.
 * Con un proprietario solo, «hai la rete» e «hai il suono» erano alternative.
 *
 * COME SI SERVE UNA LINEA CONDIVISA. Il dispositivo non dice di essere lui:
 * lo si scopre chiedendo a ognuno. Il kernel maschera la linea e notifica
 * TUTTI i proprietari; ognuno guarda il registro di stato della propria
 * scheda e o la serve o dice «non era mio». La linea si riapre quando hanno
 * risposto tutti — vedi irq_pendenti qui sotto.
 *
 * ! LA RIAPERTURA VUOLE L'ULTIMO, NON IL PRIMO. Riaprire alla prima risposta
 * rimetterebbe la linea in gioco mentre l'altro driver non ha ancora azzerato
 * la SUA scheda: la linea è ancora alta, e si torna esattamente alla tempesta
 * che il mascheramento esiste per evitare. Per questo c'è una maschera di chi
 * deve ancora rispondere, e non un contatore.
 * ============================================================================= */
#define IRQ_PROPRIETARI_MAX 4

static uint32_t irq_owner_pid[16][IRQ_PROPRIETARI_MAX];

/* Bit i = il proprietario nello slot i non ha ancora chiamato irq_done(). */
static uint32_t irq_pendenti[16] = { 0 };

/* C'è qualcuno su questa linea? Solo una scansione di quattro parole.
 *
 * ! NON SI CHIAMA irq_ripulisci() DAL DISPATCHER, e non è un dettaglio di
 * velocità: quella scorre la tabella dei processi con proc_get_by_pid, cioè
 * fa un ciclo dentro un ciclo DENTRO L'INTERRUPT, a ogni battuta di tasto. Il
 * lavoro di ripulitura è raro per natura — un driver muore una volta — e sta
 * dove è raro anche chiamarlo: bind, done, unbind, e la morte del processo.
 * Un proprietario morto qui non fa danno: ipc_notify_irq lo riconosce da sé e
 * rende 0, quindi non gli si aspetta nessuna risposta. */
static int irq_qualcuno(uint8_t irq)
{
    int i;

    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++)
        if (irq_owner_pid[irq][i] != 0) return 1;
    return 0;
}

/* Quanti proprietari VIVI ha questa linea, e intanto ripulisce gli slot di
 * chi è morto: un processo terminato non deve tenere chiusa una linea per
 * sempre, e nessuno chiamerà irq_done() per lui. */
static uint32_t irq_ripulisci(uint8_t irq)
{
    uint32_t vivi = 0;
    int      i;

    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++) {
        Process *p;

        if (irq_owner_pid[irq][i] == 0) continue;

        p = proc_get_by_pid(irq_owner_pid[irq][i]);
        if (p == NULL || p->state == PROC_UNUSED || p->state == PROC_ZOMBIE) {
            irq_owner_pid[irq][i] = 0;
            irq_pendenti[irq] &= ~(1u << i);
            continue;
        }
        vivi++;
    }
    return vivi;
}

/* =============================================================================
 * irq_bind_process — un driver ring3 rivendica un IRQ hardware
 *
 * Ritorna 0 su successo, <0 se il IRQ è già rivendicato da un altro
 * processo vivo (evita che due driver si contendano lo stesso hardware).
 * Sblocca l'IRQ nel PIC: da questo momento gli interrupt arrivano.
 * ============================================================================= */
int32_t irq_bind_process(uint8_t irq, uint32_t pid)
{
    int i, libero = -1;

    if (irq >= 16) return ERR(EINVAL);

    irq_ripulisci(irq);

    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++) {
        /* ! LA STESSA LINEA RICHIESTA DUE VOLTE DALLO STESSO PROCESSO NON E'
         * UN CONFLITTO. Senza questo, un driver che sonda le linee per
         * scoprire la propria e poi rivendica quella trovata riceve -EEXIST da
         * se' stesso, e il messaggio che ne esce — «IRQ gia' rivendicato» —
         * manda a cercare un altro driver che non c'e'. */
        if (irq_owner_pid[irq][i] == pid) {
            pic_unmask_irq(irq);
            return 0;
        }
        if (irq_owner_pid[irq][i] == 0 && libero < 0) libero = i;
    }

    if (libero < 0) {
        klog(LOG_WARN, "IRQ%u: gia' %d proprietari, non ce ne stanno altri",
             irq, IRQ_PROPRIETARI_MAX);
        return ERR(EEXIST);
    }

    irq_owner_pid[irq][libero] = pid;

    /* ! IL BIT DI ATTESA DELLO SLOT SI AZZERA QUI. Uno slot riusato puo'
     * portarsi dietro il «deve ancora rispondere» del proprietario di prima:
     * un bit acceso che nessuno spegnera' mai tiene la linea chiusa per
     * sempre, e il sintomo — l'hardware muto, senza un errore — non
     * assomiglia per niente a un bit rimasto acceso. */
    irq_pendenti[irq] &= ~(1u << libero);

    pic_unmask_irq(irq);
    return 0;
}

/* =============================================================================
 * irq_done_process — il driver ha servito l'interrupt, si può riaprire
 *
 * Contropartita del mascheramento in irq_dispatch(): finché il driver non
 * chiama questa, la linea resta chiusa. Il perché sta nel commento lungo
 * là sotto, e si riassume in una riga — un driver ring3 non può azzerare
 * il registro di stato della scheda mentre l'interrupt è in corso, perché
 * in quel momento non sta girando.
 *
 * Si verifica il proprietario: senza il controllo, un processo qualunque
 * potrebbe riaprire un IRQ che un altro driver sta ancora servendo, cioè
 * riaccendere la tempesta che il mascheramento serve a evitare.
 * ============================================================================= */
int32_t irq_done_process(uint8_t irq, uint32_t pid)
{
    int i, mio = -1;

    if (irq >= 16) return ERR(EINVAL);

    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++)
        if (irq_owner_pid[irq][i] == pid) { mio = i; break; }

    if (mio < 0) return ERR(EPERM);

    irq_pendenti[irq] &= ~(1u << mio);

    /* Un proprietario morto fra la notifica e adesso non deve tenere chiusa
     * la linea: irq_ripulisci gli toglie anche il bit di attesa. */
    irq_ripulisci(irq);

    /* ! SI RIAPRE QUANDO HANNO RISPOSTO TUTTI, non al primo. Il perche' sta
     * accanto a irq_pendenti, in cima al file: riaprire mentre un altro
     * driver non ha ancora azzerato la propria scheda rimette in gioco una
     * linea che e' ancora alta. */
    if (irq_pendenti[irq] == 0) pic_unmask_irq(irq);
    return 0;
}

/* =============================================================================
 * irq_unbind_uno — un driver rilascia UNA linea che aveva rivendicato
 *
 * ! ESISTE PER LA SONDA DELL'AUDIO ISA, ed e' l'unico caso che la chiede.
 * Una Sound Blaster anteriore alla 16 non dice su quale IRQ e' cablata: il
 * ponticello lo ha messo qualcuno vent'anni fa e non c'e' un registro che
 * lo riporti. L'unico modo di scoprirlo e' provocare un interrupt e
 * guardare quale linea si alza — cioe' rivendicare i quattro candidati
 * (5, 7, 10, 2/9), suonare un blocco di silenzio, e vedere quale notifica
 * arriva.
 *
 * Senza questa, dopo la sonda il driver terrebbe per sempre le tre linee
 * SBAGLIATE: l'IRQ 7 e' la parallela, il 10 e' libero per una scheda
 * qualunque, e un driver futuro se li troverebbe occupati da un processo
 * che non li usa e non sa di averli. Il claim dev'essere restituibile
 * perche' la sonda lo prende PER SCOPRIRE, non per usare.
 *
 * Rilascia solo cio' che e' proprio: -EPERM se la linea e' di un altro,
 * per la stessa ragione per cui la verifica c'e' in irq_done_process.
 * ============================================================================= */
int32_t irq_unbind_uno(uint8_t irq, uint32_t pid)
{
    int i, mio = -1;

    if (irq >= 16) return ERR(EINVAL);

    for (i = 0; i < IRQ_PROPRIETARI_MAX; i++)
        if (irq_owner_pid[irq][i] == pid) { mio = i; break; }

    if (mio < 0) return ERR(EPERM);

    irq_owner_pid[irq][mio] = 0;
    irq_pendenti[irq] &= ~(1u << mio);

    /* ! LA LINEA SI CHIUDE SOLO SE NON LA VUOLE PIU' NESSUNO. Chiuderla
     * sempre — com'era quando i proprietari erano uno solo — vorrebbe dire
     * che un driver che restituisce una linea presa in prestito per una sonda
     * spegne l'hardware di qualcun altro. */
    if (irq_ripulisci(irq) == 0) pic_mask_irq(irq);
    else if (irq_pendenti[irq] == 0) pic_unmask_irq(irq);
    return 0;
}

/* irq_unbind_process — rilascia il claim (chiamata alla terminazione) */
void irq_unbind_process(uint32_t pid)
{
    for (int i = 0; i < 16; i++) {
        int j, trovato = 0;

        for (j = 0; j < IRQ_PROPRIETARI_MAX; j++) {
            if (irq_owner_pid[i][j] != pid) continue;
            irq_owner_pid[i][j] = 0;
            irq_pendenti[i] &= ~(1u << j);
            trovato = 1;
        }
        if (!trovato) continue;

        /* ! SI CHIUDE SOLO SE ERA L'ULTIMO. Un driver che muore mentre un
         * altro condivide la sua linea non deve portarsi via anche
         * l'hardware del vicino — ed e' esattamente cio' che succedeva
         * quando i proprietari erano uno solo e questa funzione mascherava
         * sempre. */
        if (irq_ripulisci((uint8_t)i) == 0) pic_mask_irq((uint8_t)i);
        else if (irq_pendenti[i] == 0) pic_unmask_irq((uint8_t)i);
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
                "err=0x%08x - processo terminato\n",
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
    kprintf("+==============================================+\n");
    kprintf("|           KERNEL PANIC - ECCEZIONE CPU       |\n");
    kprintf("+==============================================+\n");
    kprintf("|  Vettore : %3d                               |\n", int_no);
    kprintf("|  Nome    : %-32s  |\n", name);
    kprintf("|  EIP     : 0x%08x                    |\n", frame->eip);
    kprintf("|  Errore  : 0x%08x                    |\n", frame->err_code);
    kprintf("+==============================================+\n");

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

    /* ! PRIMA DI TUTTO IL RESTO, e non e' una preferenza estetica: quello
     * che vale come entropia e' l'ISTANTE in cui l'interrupt e' arrivato,
     * e ogni riga eseguita prima di leggerlo aggiunge un ritardo
     * prevedibile che lo appiattisce. La funzione ignora da sola il timer
     * — vedi kernel/arch/x86/entropia.c. */
    entropia_evento(irq);

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
    } else if (irq < 16 && irq_qualcuno((uint8_t)irq)) {
        /* =====================================================================
         * ! SI MASCHERA PRIMA DI NOTIFICARE, E SENZA QUESTO IL PCI BLOCCA
         * LA MACCHINA.
         *
         * Un driver ring3 non gira dentro l'interrupt: riceve un messaggio
         * e verrà schedulato più tardi. Fra il nostro `iret` e il momento
         * in cui quel processo tocca davvero la scheda passano dei tick.
         *
         * Su un IRQ A FRONTE (la tastiera PS/2, IRQ1) non è un problema:
         * ogni byte produce un fronte, il PIC lo latcha una volta sola e
         * basta. Su un IRQ A LIVELLO — e tutti gli interrupt PCI lo sono —
         * il dispositivo tiene la linea alta finché non gli si azzera il
         * registro di stato. L'EOI qui sopra riabilita subito il PIC, la
         * linea è ancora alta, e l'interrupt riparte immediatamente dopo
         * l'iret. Il risultato non è "qualche interrupt di troppo": è che
         * il processo driver non riceve MAI la CPU per andare ad azzerare
         * quel registro, quindi la tempesta non finisce da sola. Sistema
         * fermo, senza panic e senza niente da leggere.
         *
         * Mascherare qui e riaprire su richiesta del driver (SYS_IRQ_DONE)
         * è la stessa disciplina degli "interrupt in un thread": la linea
         * resta chiusa esattamente per il tempo in cui nessuno può ancora
         * servirla.
         *
         * Vale anche per la tastiera, che ora deve chiamare irq_done(). Una
         * regola sola per tutti i driver è meglio di due modi di
         * rivendicare un IRQ di cui uno è quello sbagliato di default —
         * e un tasto premuto mentre l'IRQ è mascherato non si perde: il
         * fronte resta nell'IRR del PIC e viene consegnato alla riapertura.
         * ===================================================================== */
        pic_mask_irq((uint8_t)irq);

        /* ! SI NOTIFICANO TUTTI, e la linea si riapre quando hanno risposto
         * tutti. Su una linea PCI condivisa il dispositivo non dice di essere
         * lui: lo si scopre chiedendo a ognuno di guardare il proprio registro
         * di stato. Il perche' per esteso sta accanto a irq_owner_pid. */
        {
            int k;
            irq_pendenti[irq] = 0;
            for (k = 0; k < IRQ_PROPRIETARI_MAX; k++) {
                if (irq_owner_pid[irq][k] == 0) continue;
                if (ipc_notify_irq(irq_owner_pid[irq][k], (uint8_t)irq))
                    irq_pendenti[irq] |= (1u << k);
            }

            /* ! LA LINEA NON SI RIAPRE MAI DA QUI, NEMMENO SE LA NOTIFICA
             * E' CADUTA, e questa riga e' costata un blocco su hardware vero.
             *
             * C'era scritto il contrario: se nessuno ha ricevuto la notifica —
             * cassetta piena, quattro messaggi e basta — «tanto vale
             * riaprire, al massimo l'interrupt si ripresenta». Si ripresenta
             * ECCOME. Il PIC ha gia' avuto l'EOI, il controller di tastiera
             * tiene la linea alta finche' qualcuno non gli legge il byte, e
             * chi deve leggerlo e' un processo ring3 che per farlo ha bisogno
             * della CPU. Riaprire qui gliela toglie: subito dopo l'iret
             * l'interrupt riparte, trova la cassetta ancora piena, riapre, e
             * ricomincia. E' una tempesta che non finisce, ed e' esattamente
             * quella che il mascheramento esiste per evitare.
             *
             * In QEMU non si vede: la macchina e' abbastanza veloce che la
             * cassetta non si riempie mai. Su un Pentium II basta tenere
             * premuto un tasto mentre l'avvio finisce — il prompt compare e
             * poi la tastiera e' morta, con la macchina che sembra viva.
             *
             * Cosa succede adesso, che e' quello che succedeva prima di
             * questa modifica: la linea resta chiusa, e la riapre il driver
             * alla prossima irq_done() — che chiamera' di sicuro, perche' in
             * cassetta le notifiche precedenti ce le ha. Un messaggio perso
             * costa un giro di servizio in piu', non la macchina. */
        }
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
