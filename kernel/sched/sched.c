/* =============================================================================
 * kernel/sched/sched.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "sched.h"
#include "vga.h"   /* VGA_N_CONSOLE: una tabella di primo piano per console */
#include "pmm.h"
#include "ipc.h"
#include "paging.h"
#include "kmalloc.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"

/* =============================================================================
 * Variabili globali scheduler
 * ============================================================================= */

/* Tick counter globale (100Hz → 1 tick ogni 10ms) */
volatile uint32_t g_ticks = 0;

/* Processo correntemente in esecuzione */
static Process *g_current = NULL;

/* Task idle (eseguito quando nessun altro processo è READY) */
static Process *g_idle_task = NULL;
Process        *g_init_task = NULL;   /* reaper kernel task, adotta gli orfani */

/* Pool di PCB statici (evita kmalloc durante il boot) */
Process g_process_pool[MAX_PROCESSES];

/* PID counter */
static uint32_t g_next_pid = 1;

/* Run queues: una per livello di priorità */
static Process *g_run_queue[PRIO_MAX + 1];

/* Contatori */
static uint32_t g_proc_count   = 0;    /* Processi attivi */
static uint32_t g_switch_count = 0;    /* Context switch eseguiti */

/* Quantum per livello di priorità */
static const uint32_t quantum_table[PRIO_MAX + 1] = {
    QUANTUM_IDLE,
    QUANTUM_LOW,
    QUANTUM_NORMAL,
    QUANTUM_HIGH,
    QUANTUM_RT,
};

/* =============================================================================
 * Funzioni helper interne
 * ============================================================================= */

/* Alloca un PCB libero dal pool */
static Process *pcb_alloc(void)
{
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (g_process_pool[i].state == PROC_UNUSED) {
            /* Azzera il PCB */
            uint8_t *p = (uint8_t *)&g_process_pool[i];
            uint32_t n = sizeof(Process);
            while (n--) *p++ = 0;
            return &g_process_pool[i];
        }
    }
    return NULL;
}

/* Aggiunge un processo alla run queue del suo livello di priorità */
static void runq_add(Process *proc)
{
    uint32_t prio = proc->priority;

    if (g_run_queue[prio] == NULL) {
        /* Lista vuota: processo punta a se stesso */
        proc->next = proc;
        proc->prev = proc;
        g_run_queue[prio] = proc;
    } else {
        /* Inserisci prima del primo elemento (coda circolare) */
        Process *head = g_run_queue[prio];
        Process *tail = head->prev;
        tail->next  = proc;
        proc->prev  = tail;
        proc->next  = head;
        head->prev  = proc;
    }
}

/* Rimuove un processo dalla run queue */
static void runq_remove(Process *proc)
{
    uint32_t prio = proc->priority;

    /* Un processo BLOCKED creato con entry=0 non viene mai aggiunto
     * alla run queue (next/prev restano NULL): evita la dereferenza. */
    if (proc->next == NULL) return;

    if (proc->next == proc) {
        /* Era l'unico elemento */
        g_run_queue[prio] = NULL;
    } else {
        proc->prev->next = proc->next;
        proc->next->prev = proc->prev;
        if (g_run_queue[prio] == proc) {
            g_run_queue[prio] = proc->next;
        }
    }
    proc->next = NULL;
    proc->prev = NULL;
}

/* Copia stringa (no libc) */
static void str_copy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* =============================================================================
 * idle_task_fn — Funzione del task idle
 * Eseguita solo quando nessun altro processo è READY.
 * ============================================================================= */
static void idle_task_fn(void)
{
    for (;;) {
        __asm__ volatile ("hlt"); /* Risparmia CPU */
    }
}

/* =============================================================================
 * init_reaper_task — Kernel task "init" (PID 2)
 *
 * Scopo: raccogliere i processi ZOMBIE il cui genitore (ppid) e' gia'
 * morto o non chiamera' mai waitpid() — gli "orfani". Senza questo, ogni
 * processo terminato il cui genitore non esegue waitpid() (es. un servizio
 * in background, un driver che si schianta, o la shell che muore prima di
 * un figlio) occupa per sempre uno slot nel pool PCB (MAX_PROCESSES=64),
 * fino al blocco totale del sistema.
 *
 * Meccanismo:
 * 1. proc_exit() re-parenta gli orfani a g_init_task->pid prima di
 *    passare allo stato ZOMBIE: se il padre originale e' gia' UNUSED/
 *    ZOMBIE, il nuovo ppid diventa quello di init.
 * 2. Questo task gira a PRIO_IDLE (uguale a idle, ma schedulato per
 *    primo tra i due): scansiona il pool, raccoglie tutti gli zombie con
 *    ppid == proprio PID, poi chiama sched_sleep per cedere la CPU e
 *    svegliarsi al prossimo tick (100ms). Non usa sched_block/unblock per
 *    semplicita': il reaper deve girare periodicamente comunque (per i
 *    casi in cui proc_exit e' gia' avvenuto prima che init fosse in
 *    esecuzione), non solo su segnale.
 * ============================================================================= */
static void init_reaper_task(void)
{
    uint32_t i;
    for (;;) {
        for (i = 0; i < MAX_PROCESSES; i++) {
            Process *p = &g_process_pool[i];
            if (p->state == PROC_ZOMBIE && p->ppid == g_init_task->pid) {
                klog(LOG_DEBUG, "INIT: riassorbo orfano PID %u ('%s')",
                     p->pid, p->name);
                proc_reap_zombie(p);
            }
        }
        sched_sleep(10);   /* cedi la CPU per ~100ms (10 tick a 100Hz) */
    }
}

/* =============================================================================
 * proc_create — Crea un nuovo processo
 *
 * name:           nome del processo
 * entry_point:    indirizzo virtuale della funzione entry
 * priority:       livello di priorità (PRIO_*)
 * is_kernel_task: 1 = task kernel (ring0), 0 = processo utente (ring3)
 *
 * Ritorna: puntatore al PCB, NULL se fallito
 * ============================================================================= */
/* Offset (in uint32_t, a partire da kernel_esp) dei campi EAX/ECX nello
 * stack iniziale costruito da proc_create — vedi il commento dettagliato
 * lì per la derivazione completa dell'ordine. */
#define PROC_STACK_OFF_EAX  11
#define PROC_STACK_OFF_ECX  10

void proc_set_entry(Process *proc, uint32_t entry_point, uint32_t user_stack_top)
{
    /* Lo stack costruito da proc_create ha, a partire da kernel_esp,
     * l'ordine: GS, FS, ES, DS, EDI, ESI, EBP, ESP(dummy), EBX, EDX,
     * ECX, EAX, EFLAGS, ret_addr — quindi EAX e' a sp[11] e ECX a
     * sp[10] a partire da kernel_esp. */
    uint32_t *sp = (uint32_t *)proc->kernel_esp;
    sp[PROC_STACK_OFF_EAX] = entry_point;
    sp[PROC_STACK_OFF_ECX] = user_stack_top;

    /* NON si sovrascrive proc->user_stack_top (kernel 0.125).
     *
     * I due valori sembrano la stessa cosa e non lo sono:
     *   - l'argomento qui e' l'ESP INIZIALE, cioe' il top meno
     *     l'allineamento a 16 byte e meno l'eventuale argv gia' impilato
     *     da sys_spawn;
     *   - proc->user_stack_top e' il TOP della regione di stack, che
     *     elf_load ha gia' scritto ed e' il riferimento da cui si
     *     calcolano quanto stack e' impegnato e quanto riservato.
     *
     * Sovrascrivendolo, quei calcoli sbagliavano di 16 byte e /bin/stack
     * riportava 7K impegnati e 255K riservati invece di 8K e 256K: un
     * errore piccolo ma che rendeva i numeri incoerenti con le costanti
     * dichiarate in sched.h, cioe' esattamente cio' che quel programma
     * serve a verificare. L'ESP iniziale vive gia' nello slot ECX del
     * contesto salvato qui sopra e non ha bisogno di un secondo posto. */
}

Process *proc_create(const char *name, uint32_t entry_point,
                     uint32_t priority, int is_kernel_task)
{
    Process  *proc;
    uint32_t  kstack;

    /* Alloca PCB */
    proc = pcb_alloc();
    if (proc == NULL) {
        klog(LOG_ERROR, "SCHED: PCB pool esaurito!");
        return NULL;
    }

    /* PID */
    proc->pid  = g_next_pid++;
    proc->ppid = g_current ? g_current->pid : 0;

    /* Nome */
    str_copy(proc->name, name, PROCESS_NAME_LEN);

    /* Priorità e quantum */
    if (priority > PRIO_MAX) priority = PRIO_NORMAL;
    proc->priority      = priority;
    proc->quantum       = quantum_table[priority];
    proc->quantum_total = quantum_table[priority];

    /* Allocazione stack kernel in RAM estesa: 128KB = 32 pagine PMM
     * contigue, mappate con identity mapping nella page directory del
     * kernel (paging_init mappa identità solo fino a _kernel_end; gli
     * stack allocati dinamicamente stanno oltre quel limite). */

{
        uint32_t npages = KERNEL_STACK_SIZE / PAGE_SIZE;
        uint32_t base   = pmm_alloc_page();
        int      contig = (base != 0);

if (contig) {
            uint32_t prev = base;
            for (uint32_t pi = 1; pi < npages && contig; pi++) {
                uint32_t p = pmm_alloc_page();
                if (p == 0 || p != prev + PAGE_SIZE) {
                    contig = 0;
                    if (p) pmm_free_page(p);
                } else {
                    prev = p;
                }
            }
            if (!contig) {
                /* Libera quanto allocato finora */
                for (uint32_t pj = 0; pj < npages; pj++) {
                    uint32_t pa = base + pj * PAGE_SIZE;
                    if (pa <= prev) pmm_free_page(pa);
                }
            }
        }
        kstack = contig ? base : 0;

if (kstack) {
            PDE *kpd = paging_get_kernel_directory();

for (uint32_t pi = 0; pi < npages; pi++) {
                uint32_t pa = kstack + pi * PAGE_SIZE;
                paging_map_page(kpd, pa, pa, PG_PRESENT | PG_WRITABLE);
            }

/* Invalida il TLB: le nuove entry di page table potrebbero
             * non essere visibili senza un reload di CR3. */
            write_cr3(read_cr3());

}
    }
    if (kstack == 0) {
        klog(LOG_ERROR, "SCHED: impossibile allocare stack kernel per PID %u", proc->pid);
        proc->state = PROC_UNUSED;
        return NULL;
    }
    proc->kernel_stack_base = kstack;
    proc->kernel_stack_top  = kstack + KERNEL_STACK_SIZE;

    /* Page Directory */
    if (is_kernel_task) {
        /* I task kernel condividono la PD del kernel */
        proc->page_directory = paging_get_kernel_directory();
    } else {
        /* I processi utente hanno la loro PD (con kernel mappato) */
        proc->page_directory = paging_create_directory();
        if (proc->page_directory == NULL) {
            for (uint32_t pj = 0; pj < KERNEL_STACK_SIZE / PAGE_SIZE; pj++)
                pmm_free_page(kstack + pj * PAGE_SIZE);
            proc->state = PROC_UNUSED;
            return NULL;
        }

        /* NOTA: non allochiamo qui uno stack utente "placeholder" via
         * kmalloc(). elf_load() (chiamato subito dopo da chi crea questo
         * processo: kernel_main.c per la shell iniziale, sys_spawn() per
         * tutti gli altri) alloca SEMPRE il vero stack utente con
         * pmm_alloc_page()+paging_map_page() a un indirizzo virtuale
         * fisso (USER_SPACE_END - PAGE_SIZE - USER_STACK_SIZE) e
         * sovrascrive proc->user_stack_base/top con quei valori — un
         * eventuale kmalloc qui verrebbe scartato subito, perso per
         * sempre (64KB leak ad ogni processo creato). Restano a 0 finche'
         * elf_load() (o equivalente) non li imposta. */
        proc->user_stack_base  = 0;
        proc->user_stack_top   = 0;
        proc->user_stack_limit = 0;   /* 0 = nessuna riserva: niente crescita */
    }

    /* ==========================================================================
     * Costruisce lo stack iniziale del processo in modo che context_switch()
     * possa "riprendere" il processo come se avesse già salvato il contesto.
     *
     * Layout stack kernel iniziale (dal basso = ESP più alto):
     *
     *   kernel_stack_top - 4  : EFLAGS (IF=1)
     *   kernel_stack_top - 8  : entry_point (EIP)
     *   kernel_stack_top - 12 : CS
     *   kernel_stack_top - 16 : SS (se ring3)
     *   kernel_stack_top - 20 : user_esp (se ring3)
     *   ... pushad, pushfd (simulato da context_switch)
     *
     * Più semplice: costruiamo lo stack come lo lascerebbe context_switch
     * dopo aver salvato il contesto. Così quando faremo context_switch verso
     * questo processo, lui eseguirà il "ret" e tornerà a proc_entry_stub().
     * ========================================================================== */

    uint32_t *sp = (uint32_t *)proc->kernel_stack_top;

    /* Uno dei due trampolini (vedi context_switch.asm) riceve l'entry
     * point in EAX e, per i processi utente, lo user_esp in ECX —
     * valori ripristinati da popad subito prima del "ret" che salta
     * allo stub. Lo stub gestisce poi correttamente il primo avvio:
     * jmp diretto in ring0 per i task kernel, iret verso ring3 (con i
     * giusti selettori CS/SS/DS) per i processi utente. Senza questo
     * trampolino, il semplice "ret" verso entry_point eseguiva il
     * codice utente ancora in ring0, con CPL/selettori/stack sbagliati. */
    extern void proc_entry_stub_user(void);
    extern void proc_entry_stub_kernel(void);
    uint32_t stub_addr = is_kernel_task
                        ? (uint32_t)proc_entry_stub_kernel
                        : (uint32_t)proc_entry_stub_user;

    /* Stack iniziale compatibile con context_switch:
     * context_switch fa: pop gs, pop fs, pop es, pop ds, popad, popfd, ret
     * Quindi dobbiamo pushare nell'ordine inverso: */

    *(--sp) = stub_addr;                 /* ret address di context_switch */
    *(--sp) = 0x00000200;               /* EFLAGS: IF=1 */
    /* pushad va scritto nell'ordine EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI
     * (EAX per primo, EDI per ultimo): un vero pushad pusha
     * cronologicamente EAX,ECX,EDX,EBX,ESP,EBP,ESI,EDI, quindi EDI e'
     * l'ultimo pushato (il piu' vicino allo sp finale) e popad lo legge
     * per primo. Scrivendo a mano con *(--sp)=X, il primo assegnamento
     * finisce piu' in ALTO in memoria: per ottenere EDI il piu' basso
     * (cioe' l'ultimo letto da popad... ATTENZIONE: popad legge EDI per
     * PRIMO, quindi EDI deve essere il piu' BASSO, cioe' l'ULTIMO scritto
     * con *(--sp)=X). */
    *(--sp) = entry_point;              /* EAX: entry point (letto dallo
                                            stub appena dopo il ret) */
    *(--sp) = is_kernel_task ? 0 : proc->user_stack_top;  /* ECX: user esp
                                            (usato solo dallo stub utente) */
    *(--sp) = 0;                        /* EDX */
    *(--sp) = 0;                        /* EBX */
    *(--sp) = 0;                        /* ESP (dummy, popad lo ignora) */
    *(--sp) = proc->kernel_stack_top;   /* EBP */
    *(--sp) = 0;                        /* ESI */
    *(--sp) = 0;                        /* EDI */
    /* DS/ES/FS/GS: il restore fa "pop gs, pop fs, pop es, pop ds" (in
     * quest'ordine), quindi GS deve essere l'ultimo pushato (il piu'
     * vicino a kernel_esp, il primo letto). */
    *(--sp) = GDT_KERNEL_DATA_SEL;       /* DS (pushato per primo, letto
                                            per ultimo da "pop ds") */
    *(--sp) = GDT_KERNEL_DATA_SEL;       /* ES */
    *(--sp) = 0x00000000;               /* FS */
    *(--sp) = 0x00000000;               /* GS (pushato per ultimo, letto
                                            per primo da "pop gs") */

    proc->kernel_esp = (uint32_t)sp;

    /* File descriptors: 0=stdin, 1=stdout, 2=stderr vuoti per ora */
    proc->fds[0].type = FD_STDIN;
    proc->fds[1].type = FD_STDOUT;
    proc->fds[2].type = FD_STDERR;

    /* Timing */
    proc->created_tick = g_ticks;

    /* Stato iniziale:
     * - kernel task (is_kernel_task=1): READY subito, entry point reale passato.
     * - processo utente con entry=0: BLOCKED — il chiamante deve fare elf_load()
     *   e proc_set_entry() prima di chiamare proc_set_ready(). In questo modo
     *   il loader ELF può girare con interrupt abilitati (necessario per il
     *   driver FDC e per i delay basati su g_ticks) senza rischiare che lo
     *   scheduler salti al processo prima che entry point e stack siano pronti.
     * - processo utente con entry!=0 (es. sys_spawn dopo elf_load): READY subito. */
    if (is_kernel_task || entry_point != 0) {
        proc->state = PROC_READY;
        runq_add(proc);
    } else {
        proc->state  = PROC_BLOCKED;
        proc->next   = NULL;   /* non in run queue: runq_remove deve sapere */
        proc->prev   = NULL;
    }
    g_proc_count++;   /* incrementato UNA sola volta */

    klog(LOG_INFO, "SCHED: processo creato PID=%u '%s' entry=0x%08x prio=%u %s",
         proc->pid, proc->name, entry_point, priority,
         is_kernel_task ? "(kernel)" : "(user)");

    return proc;
}

/* =============================================================================
 * proc_set_ready — Porta un processo dallo stato BLOCKED/creato a READY
 *
 * Permette di creare un processo "sospeso" e avviarlo solo quando e'
 * completamente inizializzato (entry point impostato, ELF caricato, ecc.)
 * senza dover tenere gli interrupt disabilitati durante la preparazione.
 *
 * Uso tipico in kernel_main.c:
 *   proc = proc_create(..., entry=0, ...);   // crea BLOCKED
 *   elf_load(...);                           // carica con IRQ attivi
 *   proc_set_entry(proc, ...);              // imposta entry point reale
 *   proc_set_ready(proc);                   // ora schedulabile
 * ============================================================================= */
void proc_set_ready(Process *proc)
{
    if (proc == NULL || proc->state == PROC_READY) return;
    proc->state = PROC_READY;
    runq_add(proc);
    klog(LOG_DEBUG, "SCHED: PID %u '%s' → READY", proc->pid, proc->name);
}

/* =============================================================================
 * sched_pick_next — Sceglie il prossimo processo da eseguire
 *
 * Algoritmo:
 *   1. Scorre le run queue dall'alta alla bassa priorità
 *   2. Prende il primo processo READY nella coda più alta
 *   3. Avanza il puntatore della coda (round-robin)
 *   4. Se nessuno è READY, usa idle_task
 * ============================================================================= */
static Process *sched_pick_next(void)
{
    int32_t prio;

    for (prio = PRIO_MAX; prio >= PRIO_IDLE; prio--) {
        Process *head = g_run_queue[prio];
        if (head == NULL) continue;

        /* Scansiona la coda circolare cercando un processo READY */
        Process *p = head;
        do {
            if (p->state == PROC_READY) {
                /* Avanza la testa della coda (round-robin) */
                g_run_queue[prio] = p->next;
                return p;
            }
            p = p->next;
        } while (p != head);
    }

    /* Nessun processo READY: usa idle */
    return g_idle_task;
}

/* =============================================================================
 * sched_switch_to — Esegue il context switch verso un processo
 * ============================================================================= */
static void sched_switch_to(Process *next)
{
    Process *prev = g_current;

    if (next == prev) return;   /* Stesso processo, nessun switch */

    /* Aggiorna stato */
    if (prev->state == PROC_RUNNING) {
        prev->state = PROC_READY;
    }
    next->state   = PROC_RUNNING;
    next->quantum = next->quantum_total;    /* Ricarica quantum */

    g_current = next;
    g_switch_count++;

    /* Aggiorna ESP0 nel TSS (stack kernel per ring3→ring0 transition) */
    gdt_set_kernel_stack(next->kernel_stack_top);

    klog(LOG_DEBUG, "SCHED: switch PID %u → PID %u (tick=%u)",
         prev->pid, next->pid, g_ticks);

    /* Context switch ASM: salva ESP di prev, carica ESP di next */
    context_switch(&prev->kernel_esp,
                    next->kernel_esp,
                    (uint32_t)next->page_directory);
}

/* =============================================================================
 * sched_tick — Handler IRQ0 (chiamato ogni 10ms a 100Hz)
 *
 * Registrato come handler per IRQ0 in sched_init().
 * ============================================================================= */
/* Impostato da sched_stop(): il tick continua a contare ma non si
 * cambia più processo. Vedi sched_stop() per il perché. */
static volatile int g_sched_stopped = 0;

static void sched_irq0_handler(InterruptFrame *frame)
{
    (void)frame;    /* Non usiamo il frame direttamente */

    g_ticks++;

    /* Sistema in arresto: g_ticks deve continuare ad avanzare (serve al
     * conto alla rovescia dello spegnimento e ai delay del driver FDC
     * durante la sincronizzazione finale), ma nessuno deve più essere
     * schedulato — il processo che ha chiesto l'arresto deve poter
     * portare a termine la procedura senza essere preemptato. */
    if (g_sched_stopped) return;

    if (g_current == NULL) return;

    /* =====================================================================
     * Sveglia i processi in sleep
     *
     * BUG CORRETTO (agosto 2026): qui c'era la sola assegnazione
     *     p->state = PROC_READY;
     * senza runq_add(). Ma sched_block() toglie il processo dalla run
     * queue con runq_remove(), che azzera next/prev, e sched_pick_next()
     * scandisce SOLO la run queue: un processo marcato READY ma non
     * reinserito non viene più scelto da nessuno.
     *
     * L'effetto era che sleep(), usleep() e sched_sleep() non tornavano
     * MAI. E non moriva solo il chiamante: la shell lo attende in
     * waitpid(), quindi un programma che dormiva un istante si portava
     * dietro tutta la console, che restava a ecoare i tasti senza
     * eseguire più niente.
     *
     * Non se n'era accorto nessuno perché in pratica nessuno dormiva:
     * le attese del driver tastiera erano già state riscritte su
     * sched_yield() a giugno (vedi il commento sulle attese a scadenza
     * reale in drivers/kbd/kbd.c), e l'unica sched_sleep() rimasta —
     * quella di tty_read_ipc() mentre cerca il servizio 'kbd' — sta in
     * un ciclo che alla prima iterazione trova già il PID e non arriva
     * mai a dormire.
     *
     * Tutte le ALTRE transizioni a PROC_READY di questo file
     * (proc_create, proc_set_ready, sched_unblock_locked) chiamano già
     * runq_add: questa era l'unica a non farlo.
     * ===================================================================== */
    {
        uint32_t i;
        for (i = 0; i < MAX_PROCESSES; i++) {
            Process *p = &g_process_pool[i];
            if (p->state == PROC_SLEEPING && g_ticks >= p->sleep_until) {
                p->state = PROC_READY;
                runq_add(p);
                klog(LOG_DEBUG, "SCHED: PID %u svegliato dal sleep", p->pid);
            }

            /* Attesa con scadenza: il processo aspetta un evento, non il
             * tempo. Qui non gli si consegna niente — lo si rimette solo
             * in condizione di girare, così ricontrolla la propria
             * condizione, la trova ancora falsa e decide da sé di
             * rinunciare. Chi lo sveglia davvero (ipc_send) azzera
             * block_until, quindi una scadenza rimasta indietro non può
             * far scattare un risveglio spurio su un'attesa successiva. */
            if (p->state == PROC_BLOCKED && p->block_until != 0 &&
                g_ticks >= p->block_until) {
                p->block_until = 0;
                p->state       = PROC_READY;
                runq_add(p);
                klog(LOG_DEBUG, "SCHED: PID %u svegliato per scadenza", p->pid);
            }
        }
    }

    /* Aggiorna contatori processo corrente */
    g_current->ticks_total++;

    /* Decrementa quantum */
    if (g_current->quantum > 0) {
        g_current->quantum--;
    }

    /* Preemption: quantum esaurito o processo non più running.
     * L'idle task non ha mai un vero "quantum" da rispettare: se e' in
     * esecuzione, ad ogni tick verifichiamo se esiste un processo READY
     * migliore e cediamo immediatamente. Senza questo, una volta che il
     * quantum iniziale dell'idle si esauriva, la condizione
     * "g_current != g_idle_task" bloccava per sempre qualunque ulteriore
     * tentativo di scheduling, impedendo l'esecuzione di qualsiasi
     * processo READY creato dopo l'avvio (es. la shell). */
    if (g_current == g_idle_task) {
        Process *next = sched_pick_next();
        if (next != g_current) {
            sched_switch_to(next);
        }
    } else if (g_current->quantum == 0) {
        Process *next = sched_pick_next();
        if (next != g_current) {
            sched_switch_to(next);
        } else {
            /* Stesso processo: ricarica quantum e continua */
            g_current->quantum = g_current->quantum_total;
        }
    }
}

/* =============================================================================
 * Funzioni pubbliche scheduler
 * ============================================================================= */

void sched_yield(void)
{
    Process *next;

    interrupts_disable();
    g_current->quantum = 0;     /* Forza preemption */
    next = sched_pick_next();
    if (next != g_current) {
        sched_switch_to(next);
    }
    interrupts_enable();
}

void sched_block(ProcState reason)
{
    interrupts_disable();

    if (reason == PROC_BLOCKED || reason == PROC_SLEEPING) {
        g_current->state = reason;
        runq_remove(g_current);
    }

    Process *next = sched_pick_next();
    sched_switch_to(next);

    interrupts_enable();
}

/* sched_unblock_locked — versione interna di sched_unblock().
 *
 * Presuppone che il chiamante abbia GIA' disabilitato le interrupt
 * (cli). interrupts_disable()/interrupts_enable() in questo kernel sono
 * cli/sti grezzi, senza contatore di annidamento: chiamare la versione
 * pubblica sched_unblock() da un punto che e' gia' dentro una sezione
 * cli (es. proc_exit()) farebbe sti() troppo presto, riattivando le
 * interrupt a meta' di un'operazione non atomica. Questa versione
 * "_locked" evita il problema: usata sia dal wrapper pubblico sia da
 * proc_exit(). */
void sched_unblock_locked(uint32_t pid)
{
    uint32_t i;

    for (i = 0; i < MAX_PROCESSES; i++) {
        if (g_process_pool[i].pid   == pid &&
            g_process_pool[i].state == PROC_BLOCKED) {
            /* L'evento è arrivato: la scadenza non serve più. Lasciarla
             * armata farebbe scattare il risveglio per timeout sulla
             * PROSSIMA attesa di questo processo, che scadrebbe subito
             * per un ritardo appartenuto a quella precedente. */
            g_process_pool[i].block_until = 0;
            g_process_pool[i].state = PROC_READY;
            runq_add(&g_process_pool[i]);
            klog(LOG_DEBUG, "SCHED: PID %u sbloccato", pid);
            break;
        }
    }
}

/* =============================================================================
 * Primo piano per console — vedi il commento in sched.h
 * ============================================================================= */
static uint32_t g_console_fg[VGA_N_CONSOLE];

uint32_t sched_console_fg(uint32_t console)
{
    if (console >= VGA_N_CONSOLE) return 0;
    return g_console_fg[console];
}

void sched_set_console_fg(uint32_t console, uint32_t pid)
{
    if (console >= VGA_N_CONSOLE) return;
    g_console_fg[console] = pid;
}

void sched_unblock(uint32_t pid)
{
    interrupts_disable();
    sched_unblock_locked(pid);
    interrupts_enable();
}

void sched_sleep(uint32_t ms)
{
    /* Converti ms in tick (100Hz → 1 tick = 10ms) */
    uint32_t ticks = (ms + 9) / 10;
    if (ticks == 0) ticks = 1;

    /* FIX BUG #2: rimossi interrupts_disable()/interrupts_enable() da qui.
     * sched_block() chiama già interrupts_disable() al suo interno e
     * interrupts_enable() prima di ritornare. Avere un secondo cli/sti
     * esterno causava: (a) doppio cli inutile, (b) sti finale su interrupt
     * già attive al ritorno di sched_block — sbagliato se in futuro si
     * aggiunge un contatore di annidamento cli/sti.
     * La scrittura di sleep_until non necessita di protezione: è un campo
     * del PCB di g_current, modificato solo da questo task. */
    g_current->sleep_until = g_ticks + ticks;
    sched_block(PROC_SLEEPING);
    /* Ritorna dopo sleep_until tick */
}

void proc_exit(int32_t exit_code)
{
    interrupts_disable();

    klog(LOG_INFO, "SCHED: PID %u '%s' terminato con codice %d",
         g_current->pid, g_current->name, exit_code);

    g_current->exit_code = exit_code;
    /* Se se ne va il processo in primo piano della sua console, il posto
     * torna libero. Senza, un programma terminato per un fault
     * lascerebbe la console con un primo piano che non esiste piu': la
     * shell tornerebbe al prompt e leggerebbe la fine dell'input a ogni
     * tentativo, cioe' un prompt vivo che non accetta piu' un comando. */
    if (sched_console_fg(g_current->console) == g_current->pid) {
        sched_set_console_fg(g_current->console, 0);
    }

    g_current->state     = PROC_ZOMBIE;
    g_proc_count--;

    /* NON liberiamo qui page_directory / user_stack / kernel_stack:
     * la page_directory di g_current e' ANCORA quella attiva in CR3 in
     * questo preciso istante (lo switch avviene sotto, dopo). Distruggerla
     * ora significherebbe liberare le pagine fisiche dello spazio
     * d'indirizzamento che la CPU sta ancora usando per eseguire QUESTO
     * stesso codice — pericoloso anche se nella pratica con il bitmap
     * allocator attuale spesso "regge" per puro caso. Lo stesso vale per
     * lo stack kernel: stiamo girando su di esso proprio ora.
     *
     * Tutte le risorse del processo vengono liberate piu' avanti, in
     * sys_waitpid(), quando il genitore raccoglie lo zombie: a quel punto
     * il processo terminato non e' "current" da molto tempo (CR3 e stack
     * sono gia' cambiati piu' volte), quindi liberarle e' sicuro. */

    /* Sveglia il genitore se e' bloccato in waitpid() in attesa di noi.
     * Prima verifica che il genitore sia ancora vivo: se e' gia' UNUSED o
     * ZOMBIE non chiamera' mai waitpid(), e questo processo rimarrebbe
     * zombie per sempre consumando uno slot PCB. In quel caso lo re-parenta
     * a g_init_task (il reaper kernel task), che lo raccogliera' al prossimo
     * ciclo. Se g_init_task non e' ancora inizializzato (avvio molto precoce),
     * lo lascia con ppid=0 — verra' gestito non appena init sara' attivo.
     *
     * Nota: usiamo sched_unblock_locked() perche' siamo gia' dentro una
     * sezione cli (vedi interrupts_disable() in cima a questa funzione): la
     * sched_unblock() pubblica farebbe sti() troppo presto. */
    if (g_current->ppid != 0) {
        uint32_t ppid = g_current->ppid;
        int parent_alive = 0;
        uint32_t j;
        for (j = 0; j < MAX_PROCESSES; j++) {
            if (g_process_pool[j].pid   == ppid &&
                g_process_pool[j].state != PROC_UNUSED &&
                g_process_pool[j].state != PROC_ZOMBIE) {
                parent_alive = 1;
                break;
            }
        }
        if (!parent_alive && g_init_task != NULL) {
            klog(LOG_DEBUG, "SCHED: PID %u orfano, adottato da init (PID %u)",
                 g_current->pid, g_init_task->pid);
            g_current->ppid = g_init_task->pid;
            /* Niente unblock: init scansiona periodicamente, non serve svegliarlo */
        } else {
            sched_unblock_locked(ppid);
        }
    }

    runq_remove(g_current);

    Process *next = sched_pick_next();
    sched_switch_to(next);

    /* Non ritorna */
    for (;;) __asm__ volatile ("hlt");
}

/* =============================================================================
 * proc_reap_zombie — Libera le risorse di un ZOMBIE e segna lo slot UNUSED
 *
 * Chiamata da sys_waitpid() (genitore esplicito) e da init_reaper_task()
 * (orfani adottati). Precondizione: p non deve essere g_current (non si
 * libera lo stack su cui si sta girando) e le interrupt possono essere
 * attive (kfree/paging_destroy_directory non richiedono cli).
 * ============================================================================= */
void proc_reap_zombie(Process *p)
{
    uint32_t child_pid  = p->pid;
    int32_t  child_code = p->exit_code;

    if (p->page_directory && p->page_directory != paging_get_kernel_directory()) {
        paging_destroy_directory(p->page_directory);
    }
    if (p->kernel_stack_base) {
        for (uint32_t pj = 0; pj < KERNEL_STACK_SIZE / PAGE_SIZE; pj++)
            pmm_free_page(p->kernel_stack_base + pj * PAGE_SIZE);
    }

    /* Deregistra eventuale nome servizio e scarta la mailbox: senza
     * questo, un driver che crasha e viene riassorbito lascerebbe il suo
     * nome "fantasma" nel registro, bloccando per sempre ipc_register()
     * per chiunque tenti di ripartire con lo stesso nome (es. restart
     * automatico del driver). */
    ipc_cleanup_process(child_pid);

    /* Rilascia eventuale IRQ hardware rivendicato: senza questo, un
     * driver che crasha lascerebbe l'IRQ orfano, permanentemente
     * mascherato o instradato a un PID ormai riciclato. */
    irq_unbind_process(child_pid);

    p->state             = PROC_UNUSED;
    p->pid               = 0;
    p->ppid              = 0;
    p->page_directory    = NULL;
    p->user_stack_base   = 0;
    p->user_stack_limit  = 0;
    p->kernel_stack_base = 0;

    klog(LOG_DEBUG, "SCHED: riassorbito PID %u (code=%d)", child_pid, child_code);
}

void proc_kill(uint32_t pid)
{
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (g_process_pool[i].pid == pid &&
            g_process_pool[i].state != PROC_UNUSED &&
            g_process_pool[i].state != PROC_ZOMBIE) {

            klog(LOG_INFO, "SCHED: kill PID %u", pid);
            g_process_pool[i].state = PROC_ZOMBIE;
            runq_remove(&g_process_pool[i]);
            g_proc_count--;
            return;
        }
    }
    klog(LOG_WARN, "SCHED: kill PID %u non trovato", pid);
}

Process *proc_get_current(void)
{
    return g_current;
}

Process *proc_get_by_pid(uint32_t pid)
{
    uint32_t i;
    for (i = 0; i < MAX_PROCESSES; i++) {
        if (g_process_pool[i].pid == pid &&
            g_process_pool[i].state != PROC_UNUSED) {
            return &g_process_pool[i];
        }
    }
    return NULL;
}

/* =============================================================================
 * sched_dump — Stampa stato scheduler (debug)
 * ============================================================================= */
void sched_dump(void)
{
    uint32_t i;
    static const char *state_names[] = {
        "UNUSED", "READY", "RUNNING", "BLOCKED", "ZOMBIE", "SLEEPING"
    };

    klog(LOG_INFO, "SCHED dump (tick=%u, switch=%u, procs=%u):",
         g_ticks, g_switch_count, g_proc_count);

    for (i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_process_pool[i];
        if (p->state == PROC_UNUSED) continue;

        const char *sname = (p->state <= PROC_SLEEPING)
                            ? state_names[p->state] : "?";
        klog(LOG_INFO, "  PID=%3u %-20s %-8s prio=%u tick=%u",
             p->pid, p->name, sname, p->priority, p->ticks_total);
    }
}

/* =============================================================================
 * sched_init — Inizializza lo scheduler
 *
 * Deve essere chiamata DOPO: gdt_install, idt_install, isr_install,
 * pmm_init, paging_init, kmalloc_init.
 * ============================================================================= */
void sched_init(void)
{
    uint32_t i;

klog(LOG_INFO, "SCHED: inizializzazione scheduler preemptive...");

    /* Azzera strutture */
    for (i = 0; i < MAX_PROCESSES; i++) {
        g_process_pool[i].state = PROC_UNUSED;
    }
    for (i = 0; i <= PRIO_MAX; i++) {
        g_run_queue[i] = NULL;
    }
g_ticks        = 0;
    g_next_pid     = 1;
    g_proc_count   = 0;
    g_switch_count = 0;

    /* -------------------------------------------------------------------------
     * PIT già configurato in entry.asm (100Hz) prima che la paginazione
     * fosse attiva. Qui registriamo solo l'handler — NON sblocchiamo
     * ancora IRQ0: farlo ora, prima che g_current sia impostato, causa
     * un crash quando il primo tick arriva e sched_irq0_handler tenta
     * di operare su g_current == NULL.
     * ------------------------------------------------------------------------- */
    irq_register_handler(0, sched_irq0_handler);

/* -------------------------------------------------------------------------
     * Crea il task idle (PID 0, priorità minima)
     * Eseguito quando nessun altro processo è READY.
     * ------------------------------------------------------------------------- */
    g_idle_task = proc_create("idle", (uint32_t)idle_task_fn, PRIO_IDLE, 1);

if (g_idle_task == NULL) {
        kpanic("SCHED: impossibile creare idle task!");
    }
    klog(LOG_INFO, "SCHED: idle task creato (PID %u)", g_idle_task->pid);

    /* -------------------------------------------------------------------------
     * Crea il task init/reaper (kernel task, priorita' IDLE)
     * Adotta gli orfani e raccoglie i loro ZOMBIE, evitando che slot PCB
     * vengano persi per sempre. Gira a PRIO_IDLE per non interferire con
     * processi utente, ma ha priorita' maggiore di idle (viene schedulato
     * prima quando entrambi sono READY) grazie all'ordine di inserimento
     * nella run queue (init viene aggiunto dopo idle, ma stesso livello).
     * Anche se avesse stessa priorita' di idle, basta che giri comunque
     * ogni pochi tick — basta PRIO_IDLE per i nostri scopi.
     * ------------------------------------------------------------------------- */
    g_init_task = proc_create("init", (uint32_t)init_reaper_task, PRIO_IDLE, 1);

if (g_init_task == NULL) {
        kpanic("SCHED: impossibile creare init task!");
    }
    klog(LOG_INFO, "SCHED: init/reaper task creato (PID %u)", g_init_task->pid);

    /* -------------------------------------------------------------------------
     * Imposta il processo corrente al task idle per avviare lo scheduler.
     * Il primo context switch avverrà al prossimo IRQ0.
     * -------------------------------------------------------------------------
     * NOTA: A questo punto NON c'è ancora un "processo kernel principale" nel
     * sistema scheduler. kernel_main() gira fuori dallo scheduler finché
     * non chiama sched_start() che fa il salto al primo processo.
     * ------------------------------------------------------------------------- */
    g_idle_task->state = PROC_RUNNING;
    g_current          = g_idle_task;

    /* Sblocca IRQ0 a livello PIC ORA (g_current è valido, safe per il primo
     * tick). Il CPU IF resta 0 finché kernel_main non chiama
     * interrupts_enable() al PASSO 12 — quindi nessun tick può ancora
     * arrivare qui. Ma il PIC deve essere già smascherato PRIMA di quel
     * punto perché fat12_init() (PASSO 13, subito dopo) usa fdc_delay_ms()
     * che attende g_ticks avanzare: se IRQ0 restasse mascherato fino a
     * sched_start() (fine boot), g_ticks non avanzerebbe mai durante il
     * caricamento del filesystem/driver, bloccando fat12_init in un hlt
     * infinito. */
    pic_unmask_irq(0);

/* FIX BUG #1: rimosso reset g_next_pid = 1.
     * Il reset causava una PID collision: idle ottiene PID 1 (g_next_pid parte
     * da 1 e viene incrementato da proc_create), init ottiene PID 2, poi il
     * reset a 1 faceva sì che il prossimo processo (la shell) ricevesse di nuovo
     * PID 1 — identico all'idle. proc_get_by_pid(1), sched_unblock_locked(ppid)
     * e il reaper puntavano all'idle invece della shell, causando hang in
     * waitpid e zombie leak. g_next_pid continua normalmente da 3 in poi. */

    klog(LOG_INFO, "SCHED: scheduler inizializzato, idle task attivo");

    /* NOTA: IRQ0 resta mascherato e sched_init() ritorna normalmente.
     * kernel_main() continua l'avvio (syscall, fat12, driver, shell) con
     * interrupt abilitati ma timer fermo — nessun tick prematuro può
     * arrivare durante il caricamento. Solo sched_start(), chiamata a
     * fine boot, sblocca IRQ0 e cede definitivamente il controllo. */
}

/* =============================================================================
 * sched_start — sblocca IRQ0 e cede il controllo allo scheduler. Non ritorna.
 * Va chiamata come ultimo passo di kernel_main(), dopo che driver, servizi
 * e shell sono stati caricati e resi READY.
 * ============================================================================= */
/* =============================================================================
 * sched_stop — congela lo scheduling senza fermare il tempo
 *
 * Usata dalla procedura di arresto (kernel/arch/x86/power.c). Da questo
 * momento sched_irq0_handler continua a incrementare g_ticks ma non
 * esegue più context switch: il processo che ha chiamato l'arresto
 * prosegue indisturbato fino allo spegnimento.
 *
 * NON maschera IRQ0, di proposito. Mascherarlo fermerebbe g_ticks, e
 * tutto ciò che serve ancora — il conto alla rovescia e i delay del
 * driver FDC durante la sincronizzazione del filesystem — è basato
 * proprio su g_ticks: si bloccherebbe per sempre. È lo stesso errore che
 * causò il deadlock del PIC a luglio 2026, in forma diversa.
 *
 * Non c'è una sched_resume(): l'arresto è definitivo per costruzione.
 * ============================================================================= */
void sched_stop(void)
{
    g_sched_stopped = 1;
    klog(LOG_INFO, "SCHED: scheduling fermato (tick ancora attivo)");
}

void sched_start(void)
{
    klog(LOG_INFO, "SCHED: avvio — sblocco IRQ0, %u processi pronti",
         g_proc_count);

pic_unmask_irq(0);

interrupts_enable();

/* Da qui il kernel è guidato interamente dagli interrupt: IRQ0 (timer)
     * esegue i context switch, il syscall gate gestisce le richieste ring3.
     * kernel_main() non riprende mai il controllo. */
    for (;;) {
        __asm__ volatile("hlt");
    }
}
