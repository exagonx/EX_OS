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
#include "fat12.h"   /* fat12_motor_tick: il motore del floppy si ferma dal tick */
#include "vga.h"   /* VGA_N_CONSOLE: una tabella di primo piano per console */
#include "pmm.h"
#include "ipc.h"
#include "shm.h"
#include "pipe.h"
#include "paging.h"
#include "kmalloc.h"
#include "gdt.h"
#include "idt.h"
#include "isr.h"
#include "vfs.h"   /* vfs_close: l'eseguibile aperto per il caricamento su richiesta */

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
/* =============================================================================
 * ! GLI ULTIMI SLOT SONO DI root, dal 17 agosto 2026.
 *
 * I processi sono 64 in tutto e non c'era nessun limite per utente: un utente
 * normale poteva riempirli tutti — un ciclo che fa spawn di se' stesso ci mette
 * un istante — e da quel momento NESSUNO puo' piu' avviare niente,
 * l'amministratore compreso. Non e' un fastidio: e' un sistema in cui il
 * rimedio diventa impossibile proprio quando serve.
 *
 * ! LA RISERVA E' PER root, NON PER IL SISTEMA, e la differenza conta: un
 * numero di slot tenuti liberi «per sicurezza» si riempirebbero comunque al
 * primo processo di sistema che parte. Riservarli a chi puo' spegnere e
 * riparare e' l'unica regola che garantisce il rimedio.
 *
 * Otto su 64: bastano una shell, un `id`, un `kill` e qualche margine.
 * ============================================================================= */
#define SLOT_RISERVATI_ROOT   8

static Process *pcb_alloc(void)
{
    uint32_t i, liberi = 0;
    Process *self = g_current;

    if (self != NULL && self->uid != 0) {
        for (i = 0; i < MAX_PROCESSES; i++)
            if (g_process_pool[i].state == PROC_UNUSED) liberi++;

        if (liberi <= SLOT_RISERVATI_ROOT) {
            klog(LOG_WARN, "SCHED: PID %u (uid %u) rifiutato: restano %u slot, "
                 "riservati a root", self->pid, self->uid, liberi);
            return NULL;
        }
    }

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

/* =============================================================================
 * proc_chiudi_fd — rilascia i descrittori di un processo che sta morendo
 *
 * ! SI CHIAMA QUANDO IL PROCESSO MUORE, NON QUANDO IL PADRE LO RACCOGLIE,
 * e la differenza non e' teorica.
 *
 * Fino alla 0.158 i descrittori si chiudevano in proc_reap_zombie(), cioe'
 * quando il genitore faceva waitpid(). Con i soli file passava
 * inosservato: uno zombie che tiene aperto un file per qualche
 * millisecondo non da' fastidio a nessuno.
 *
 * Con le pipe e' uno STALLO. Il caso e' esattamente `cmd1 | cmd2`:
 *
 *     il figlio scrive, poi esce            -> ZOMBIE
 *     i suoi fd restano contati              -> la pipe crede di avere
 *                                               ancora uno scrittore
 *     il padre e' bloccato nella read        -> non arrivera' mai a
 *                                               waitpid()
 *     nessuno chiama proc_reap_zombie()      -> nessuno decrementa
 *
 * Due processi fermi ad aspettarsi a vicenda, senza nessun errore. E' il
 * motivo per cui su Unix i descrittori si chiudono in do_exit() e non in
 * wait(): uno zombie non deve trattenere risorse di I/O, solo il proprio
 * codice di uscita.
 *
 * ! PRESUPPONE `cli`: la chiamano proc_exit() e proc_kill(), che girano
 * entrambe in sezione critica. Da qui le varianti _locked delle chiusure
 * di pipe — vedi kernel/include/pipe.h.
 *
 * ! E' IDEMPOTENTE (rimette FD_UNUSED), cosi' la spazzata di sicurezza
 * che resta in proc_reap_zombie() non chiude niente due volte.
 * ============================================================================= */
static void proc_chiudi_fd(Process *p)
{
    int fd;

    for (fd = 0; fd < MAX_FD; fd++) {
        switch (p->fdt[fd].type) {
            case FD_FILE:
                vfs_close((int)p->fdt[fd].inode);
                break;
            case FD_PIPE_R:
                pipe_chiudi_lettore_locked((int)p->fdt[fd].inode);
                break;
            case FD_PIPE_W:
                pipe_chiudi_scrittore_locked((int)p->fdt[fd].inode);
                break;
            default:
                continue;   /* stdin/stdout/stderr e i driver: niente da fare */
        }
        p->fdt[fd].type        = FD_UNUSED;
        p->fdt[fd].inode       = 0;
        p->fdt[fd].driver_data = NULL;
    }
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

    /* ! OGNI PROCESSO E' CAPOGRUPPO DI SE STESSO, e i descrittori sono i suoi.
     * Con queste due righe tutto il kernel che non sa niente di fili continua
     * a funzionare: `fdt` punta a `fds`, `tgid` vale `pid`, e le condizioni
     * «sono un filo?» sono false dappertutto senza un solo caso speciale. */
    proc->tgid       = proc->pid;
    proc->fdt        = proc->fds;
    proc->filo_posto = 0;

    /* Nome */
    str_copy(proc->name, name, PROCESS_NAME_LEN);

    /* Nessuna immagine su file finche' non ne carica una elf_load: -1 e
     * non 0, perche' 0 e' un handle VFS legittimo e chiuderlo per errore
     * chiuderebbe il file di qualcun altro. */
    proc->exe_handle = -1;
    proc->n_vma      = 0;

    /* Stato x87 di partenza. pcb_alloc ha azzerato il PCB, e zero NON e'
     * uno stato valido per FRSTOR: la parola di controllo a zero smaschera
     * tutte le eccezioni di virgola mobile e i tag dicono che tutti gli
     * otto registri contengono un valore buono. Il primo processo a
     * toccare la FPU prenderebbe un'eccezione senza aver fatto niente. */
    fpu_init_state(proc->fpu_state);

    /* Priorità e quantum */
    if (priority > PRIO_MAX) priority = PRIO_NORMAL;
    proc->priority      = priority;
    proc->quantum       = quantum_table[priority];
    proc->quantum_total = quantum_table[priority];

    /* Allocazione stack kernel: 128KB = 32 pagine PMM contigue, mappate
     * con identity mapping nella page directory del kernel.
     *
     * ! DALLA FASCIA KERNEL, e questo e' il caso in cui sbagliare costa
     * di piu' di tutti. Lo stack kernel di un processo viene usato dalla
     * CPU al suo indirizzo fisico (e' quello che finisce in TSS.ESP0)
     * nell'istante esatto in cui arriva un interrupt mentre gira QUEL
     * processo, cioe' con la SUA page directory caricata. Uno stack
     * allocato sopra USER_SPACE_BASE non sarebbe mappato li' dentro: il
     * page fault avverrebbe durante la commutazione di stack, prima che
     * una sola istruzione del kernel possa dire cosa e' successo — e il
     * fault handler, per gestirlo, avrebbe bisogno dello stesso stack.
     * Triplo fault, macchina che si riavvia, nessuna diagnostica.
     *
     * pmm_alloc_pages_kernel fa in un colpo la ricerca di pagine contigue
     * che prima si faceva a mano allocando e ributtando. */

{
        uint32_t npages = KERNEL_STACK_SIZE / PAGE_SIZE;

        kstack = pmm_alloc_pages_kernel(npages);

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
    proc->fdt[0].type = FD_STDIN;
    proc->fdt[1].type = FD_STDOUT;
    proc->fdt[2].type = FD_STDERR;

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
        /* ! NASCENTE, NON BLOCCATO: vedi ProcState in sched.h. Un bloccato lo
         * sveglia chiunque conosca il suo PID, e qui il PID c'e' gia' mentre
         * entry point e stack ancora no. */
        proc->state  = PROC_NASCENTE;
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
    klog(LOG_DEBUG, "SCHED: PID %u '%s' -> READY", proc->pid, proc->name);
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

    /* E il thread pointer: il descrittore che i processi tengono in GS
     * punta al blocco TLS di CHI sta per girare. Zero per chi non ne ha —
     * vedi Process.tls_tp e gdt_set_tls_base(). Va fatto qui, accanto
     * all'ESP0, perche' sono le due sole cose che dipendono dal processo e
     * non viaggiano nei registri salvati da context_switch. */
    gdt_set_tls_base(next->tls_tp);

    klog(LOG_DEBUG, "SCHED: switch PID %u -> PID %u (tick=%u)",
         prev->pid, next->pid, g_ticks);

    /* Coprocessore x87: lo stato NON e' nei registri che salva
     * context_switch, quindi va spostato qui, in C, prima di cambiare
     * stack. Salvare sempre invece di usare lo switch pigro (CR0.TS piu'
     * un handler di #NM che ripristina solo a chi la FPU la usa davvero)
     * e' una scelta: a 100 tick al secondo sono ~200 cicli ogni 10 ms,
     * cioe' niente, e in cambio non c'e' un proprietario della FPU da
     * tenere aggiornato quando un processo muore — che e' il modo in cui
     * lo switch pigro si sbaglia.
     *
     * L'ordine conta: FNSAVE azzera la FPU dopo aver salvato, quindi il
     * ripristino del prossimo deve venire dopo il salvataggio del
     * precedente. Chi torna qui piu' tardi trova il proprio stato gia'
     * ricaricato da chi lo ha risvegliato. */
    fpu_save(prev->fpu_state);
    fpu_restore(next->fpu_state);

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

    /* ! IL MOTORE DEL FLOPPY SI SPEGNE DA QUI, e non da dentro il driver.
     * Il driver, quando servirebbe, e' fermo in attesa o ha appena rinunciato
     * per errore: l'unica cosa che continua a girare e' questo tick. Costa tre
     * confronti e, due secondi dopo l'ultimo accesso, una out. Il perche' per
     * esteso sta accanto a FDC_INATTIVITA_TICK in kernel/fs/fat12.c.
     *
     * ! PRIMA DEL RITORNO PER g_sched_stopped, apposta: durante lo spegnimento
     * il disco si sincronizza e poi resta fermo, e non c'e' motivo di lasciarlo
     * girare fino al taglio dell'alimentazione. */
    fat12_motor_tick();

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
        /* ! IL TESTIMONE DI UN RISVEGLIO SBAGLIATO, ed e' l'unico posto dove
         * metterlo: qui passano IPC, il VFS e i pty, tutti a svegliare PER
         * PID. Un PID appena riciclato puo' appartenere a un processo che sta
         * ancora nascendo — e farlo partire vorrebbe dire ring 3 con EIP=0 e
         * senza stack. Non si sveglia, e si dice a chi legge il log che
         * qualcuno tiene in mano un PID che non e' piu' quello di prima. */
        if (g_process_pool[i].pid   == pid &&
            g_process_pool[i].state == PROC_NASCENTE) {
            klog(LOG_WARN, "SCHED: risveglio del PID %u '%s' ignorato: sta "
                 "ancora nascendo (PID riciclato?)",
                 pid, g_process_pool[i].name);
            return;
        }

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

/* Alza il flag e sveglia. Non fa controlli di permesso: quelli stanno in
 * sys_interrompi, perche' chi chiama da dentro il kernel — la disciplina di
 * linea di un pty davanti a un Ctrl+C — non ha un uid con cui confrontarsi. */
int proc_interrompi_locked(uint32_t pid)
{
    Process *p = proc_get_by_pid(pid);

    if (p == NULL) return -1;

    p->interrotto = 1;
    if (p->state == PROC_BLOCKED) sched_unblock_locked(pid);
    /* Un nascente non e' in nessuna coda e non si sveglia: proc_kill lo
     * trovera' comunque, perche' guarda lo stato e non la coda. */
    return 0;
}

/* ! LA VERSIONE CON IL LUCCHETTO ESISTE PERCHE' CHI CHIAMA DA DENTRO IL KERNEL
 * LO HA GIA' PRESO. La disciplina di linea di un pty gira a interrupt
 * disabilitati: chiamando la variante che li riabilita alla fine, aprirebbe
 * una sezione critica a meta' — e il resto della funzione continuerebbe
 * credendo di averla ancora. E' un difetto che non da' nessun sintomo finche'
 * un interrupt non capita esattamente li'. */
int proc_interrompi(uint32_t pid)
{
    int r;

    interrupts_disable();
    r = proc_interrompi_locked(pid);
    interrupts_enable();
    return r;
}

int proc_interrotto(void)
{
    Process *p = proc_get_current();

    return (p && p->interrotto) ? 1 : 0;
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

/* =============================================================================
 * I FILI
 *
 * ! PER LO SCHEDULER UN FILO E' UN TASK, e non c'e' una riga qui sotto che lo
 * cambi: stessa run queue, stesso quanto, stesso context_switch. L'unica cosa
 * che si fa diversamente e' NON allocare una page directory nuova — si copia
 * quella del capogruppo, e da quel momento i due task vedono la stessa
 * memoria. context_switch riceve gia' il CR3 come parametro, quindi passare
 * due volte lo stesso valore e' esattamente cio' che serve e non costa niente:
 * su x86 ricaricare CR3 con lo stesso valore non svuota nemmeno il TLB.
 *
 * ! LO STACK DEL FILO STA IN UNA PIAZZOLA SUA, dentro la banda riservata da
 * elf_load. Non si cerca «un posto libero qualunque» nello spazio di
 * indirizzamento: si numerano le piazzole, e il numero e' anche il modo di
 * sapere quale e' occupata — basta guardare i membri del gruppo.
 * ============================================================================= */

/* Quanti membri vivi (non zombie, non liberi) ha il gruppo. */
int proc_gruppo_vivi(uint32_t tgid)
{
    uint32_t i;
    int      n = 0;

    for (i = 0; i < MAX_PROCESSES; i++)
        if (g_process_pool[i].state != PROC_UNUSED &&
            g_process_pool[i].state != PROC_ZOMBIE &&
            g_process_pool[i].tgid  == tgid)
            n++;
    return n;
}

/* La prima piazzola libera, o 0 se sono tutte prese. */
static uint32_t filo_posto_libero(uint32_t tgid)
{
    uint32_t posto, i;

    for (posto = 1; posto < FILO_MAX; posto++) {
        int preso = 0;

        for (i = 0; i < MAX_PROCESSES; i++)
            if (g_process_pool[i].state != PROC_UNUSED &&
                g_process_pool[i].tgid       == tgid &&
                g_process_pool[i].filo_posto == posto) { preso = 1; break; }
        if (!preso) return posto;
    }
    return 0;
}

/* Il buffer dell'immagine TLS. Statico perche' TLS_MAX e' 64 KB e sullo stack
 * del kernel non ci starebbero; uno solo perche' proc_thread_crea gira dentro
 * una chiamata di sistema e finisce prima che un'altra cominci. */
static uint8_t g_tls_buf[TLS_MAX];

int proc_thread_crea(uint32_t entry, uint32_t arg)
{
    Process *capo, *filo;
    uint32_t posto, cima, base, pg;
    uint32_t esp;

    if (g_current == NULL) return ERR(ESRCH);

    capo = proc_get_by_pid(g_current->tgid);
    if (capo == NULL) return ERR(ESRCH);

    /* ! LA BANDA LA PREPARA elf_load, e se non c'e' non si inventa: un
     * processo il cui spazio di indirizzamento non e' stato preparato da li'
     * (un task kernel, o qualcosa costruito a mano) non ha una banda, e
     * mettere uno stack «da qualche parte» vorrebbe dire sovrascrivere lo
     * heap di qualcun altro. */
    if (capo->fili_banda == 0) return ERR(ENOSYS);

    posto = filo_posto_libero(capo->tgid);
    if (posto == 0) return ERR(EAGAIN);     /* piazzole finite: FILO_MAX */

    cima = capo->fili_banda - (posto - 1) * (FILO_STACK_SIZE + PAGE_SIZE);
    base = cima - FILO_STACK_SIZE;

    /* ! LO STACK DI UN FILO SI IMPEGNA TUTTO SUBITO, al contrario di quello
     * del processo che cresce su richiesta. Il motivo e' page_fault_handler:
     * sa far crescere UNO stack — quello descritto da user_stack_limit nel
     * PCB — e non saprebbe dire a quale filo appartiene una pagina mancante
     * dentro la banda. Sessantaquattro kilobyte impegnati sono il prezzo
     * onesto di non dover insegnare al gestore dei fault una cosa che si puo'
     * evitare del tutto. */
    for (pg = 0; pg < FILO_STACK_SIZE / PAGE_SIZE; pg++) {
        uint32_t fis = pmm_alloc_page();

        if (fis == 0) {
            klog(LOG_ERROR, "SCHED: memoria finita creando lo stack del filo");
            while (pg--) {
                uint32_t v = base + pg * PAGE_SIZE;
                uint32_t f = paging_get_physical(capo->page_directory, v);
                paging_unmap_page(capo->page_directory, v);
                if (f) pmm_free_page(f);
            }
            return ERR(ENOMEM);
        }
        paging_azzera_fisica(fis);
        if (paging_map_page(capo->page_directory, base + pg * PAGE_SIZE, fis,
                            PG_PRESENT | PG_WRITABLE | PG_USER) != 0) {
            pmm_free_page(fis);
            return ERR(ENOMEM);
        }
    }

    /* =====================================================================
     * ! L'IMMAGINE TLS SI LEGGE PRIMA DI CREARE IL TASK, e non e' un
     * riordino estetico: e' la correzione di un difetto che si vedeva una
     * volta su tre.
     *
     * vfs_read PUO' BLOCCARE. Leggendola dopo proc_create, il filo esisteva
     * gia' — con il contesto che proc_create gli aveva costruito, cioe' con
     * ESP a zero perche' user_stack_top non era ancora stato scritto — e in
     * quella finestra lo scheduler poteva metterlo in esecuzione. Il sintomo
     * era un page fault a 0xfffffffc all'ingresso della funzione del filo:
     * la prima `push` con lo stack a zero. Intermittente, perche' dipendeva
     * da dove il disco decideva di far aspettare chi leggeva.
     *
     * ! LA REGOLA CHE NE ESCE: fra la creazione di un task e il momento in
     * cui e' pronto non ci deve stare NIENTE che possa bloccare. Tutto quel
     * che vuole aspettare si fa prima, quando un task a meta' non esiste
     * ancora.
     * ===================================================================== */
    if (capo->tls_tp != 0 && capo->tls_filesz > 0 && capo->exe_handle >= 0) {
        uint32_t fatti = 0;

        while (fatti < capo->tls_filesz && fatti < TLS_MAX) {
            uint32_t quanti = capo->tls_filesz - fatti;
            int      letti;

            if (quanti > PAGE_SIZE) quanti = PAGE_SIZE;
            letti = vfs_read(capo->exe_handle, g_tls_buf + fatti, quanti,
                             capo->tls_off + fatti);
            if (letti <= 0) {
                klog(LOG_ERROR, "SCHED: non riesco a leggere l'immagine TLS "
                                "per il filo");
                return ERR(EIO);
            }
            fatti += (uint32_t)letti;
        }
    }

    filo = proc_create(capo->name, entry, capo->priority, 0);
    if (filo == NULL) return ERR(EAGAIN);   /* pool dei PCB esaurito */

    /* ! LA PAGE DIRECTORY NUOVA CHE proc_create HA APPENA FATTO SI BUTTA, e
     * non e' uno spreco da correggere: chiedergli di non farla vorrebbe dire
     * un parametro in piu' su una funzione usata da mezzo kernel. Una pagina
     * allocata e liberata subito costa una manciata di istruzioni; un
     * parametro in piu' costa a chi legge, per sempre. */
    if (filo->page_directory &&
        filo->page_directory != paging_get_kernel_directory())
        paging_destroy_directory(filo->page_directory);

    filo->page_directory = capo->page_directory;
    filo->tgid           = capo->tgid;
    filo->fdt            = capo->fdt;          /* i descrittori sono in comune */
    filo->filo_posto     = posto;
    filo->fili_banda     = 0;                  /* la banda e' del capogruppo */
    filo->ppid           = capo->ppid;
    filo->console        = capo->console;
    filo->uid            = capo->uid;
    filo->gid            = capo->gid;
    filo->heap_start     = capo->heap_start;
    filo->heap_end       = capo->heap_end;
    filo->heap_max       = capo->heap_max;

    /* =====================================================================
     * IL BLOCCO TLS DEL FILO — in cima al suo stack
     *
     * ! OGNI FILO HA IL SUO, e non e' un lusso: `__thread` vuol dire «una
     * copia per flusso», e condividerne una sola fra piu' fili e' proprio la
     * cosa che quella parola promette di non fare. Il codice di terzi la usa
     * senza chiedere il permesso — bfd dichiara `static TLS bfd_error_type
     * bfd_error` — e la std di Rust ci costruisce sopra i suoi thread-local.
     *
     * ! E L'IMMAGINE INIZIALE SI RILEGGE DAL FILE, non si copia da quella del
     * capogruppo. Copiare la sua vorrebbe dire far partire il filo con i
     * VALORI DI ADESSO di un altro flusso — un contatore a meta', un
     * puntatore a un oggetto che il capogruppo sta usando. Quel che serve e'
     * lo stato iniziale, e lo stato iniziale sta nell'eseguibile, che e'
     * ancora aperto (exe_handle) per il caricamento su richiesta.
     *
     * ! STA IN CIMA ALLO STACK DEL FILO, ed e' il posto che non costa niente:
     * quelle pagine sono gia' mappate, lo spazio e' gia' suo, e nessun altro
     * puo' arrivarci. E' anche dove lo mette glibc. Lo stack comincia sotto
     * il blocco, quindi crescendo si allontana invece di avvicinarsi.
     * ===================================================================== */
    /* Anche quando il programma non ha variabili __thread il blocco si fa: sono
     * gli otto byte del TCB, e servono alla libc per trovare errno (vedi
     * elf_load, dove per la stessa ragione lo si fa a tutti i processi). */
    if (capo->tls_tp != 0) {
        uint32_t tot  = ALIGN_UP(capo->tls_dim + TLS_TCB_SIZE, 16);
        uint32_t tbase, tp;

        if (tot > FILO_STACK_SIZE / 4) {
            klog(LOG_ERROR, "SCHED: blocco TLS di %u byte: troppo per lo "
                            "stack di un filo", tot);
            proc_kill(filo->pid);
            return ERR(ENOMEM);
        }

        tbase = (cima - tot) & ~15u;
        tp    = tbase + capo->tls_dim;

        /* La parte inizializzata, copiata dal buffer letto PRIMA di creare il
         * task; il resto resta a zero, perche' le pagine dello stack sono
         * state azzerate quando le abbiamo mappate. Qui non si blocca piu'
         * niente: si scrive e basta. */
        {
            uint32_t k;

            for (k = 0; k < capo->tls_filesz && k < TLS_MAX; k++) {
                uint32_t va  = tbase + k;
                uint32_t fis = paging_get_physical(capo->page_directory,
                                                   va & ~(PAGE_SIZE - 1));
                uint8_t *dst;

                if (fis == 0) { proc_kill(filo->pid); return ERR(EFAULT); }
                dst = (uint8_t *)paging_finestra_apri(fis);
                dst[va % PAGE_SIZE] = g_tls_buf[k];
                paging_finestra_chiudi();
            }
        }

        /* Il puntatore a se stesso in testa al TCB: %gs:0 legge questo. */
        {
            uint32_t fis = paging_get_physical(capo->page_directory,
                                               tp & ~(PAGE_SIZE - 1));
            uint32_t *tcb;

            if (fis == 0) { proc_kill(filo->pid); return ERR(EFAULT); }
            tcb = (uint32_t *)paging_finestra_apri(fis);
            tcb[(tp % PAGE_SIZE) / 4] = tp;
            paging_finestra_chiudi();
        }

        filo->tls_base = tbase;
        filo->tls_tp   = tp;
        cima           = tbase;     /* lo stack del filo comincia sotto */
    } else {
        filo->tls_tp   = 0;
        filo->tls_base = 0;
    }

    str_copy(filo->cwd, capo->cwd, VFS_PATH_MAX);

    filo->user_stack_base  = base;
    filo->user_stack_top   = cima;
    filo->user_stack_limit = 0;     /* impegnato tutto: niente crescita */

    /* L'argomento sullo stack, come lo vuole una funzione C: alla partenza
     * ESP punta all'indirizzo di ritorno, e l'argomento sta subito sopra.
     * L'indirizzo di ritorno e' zero apposta — un filo che torna dalla sua
     * funzione invece di chiamare thread_esci() salta a 0 e si prende un
     * fault che si vede, invece di proseguire dentro memoria a caso. */
    esp = (cima - 16) & ~15u;
    {
        uint32_t fis  = paging_get_physical(capo->page_directory, esp - PAGE_SIZE);
        uint32_t *pila;
        uint32_t off;

        esp -= 8;
        fis = paging_get_physical(capo->page_directory, esp & ~(PAGE_SIZE - 1));
        if (fis == 0) { proc_kill(filo->pid); return ERR(ENOMEM); }
        off  = esp % PAGE_SIZE;
        pila = (uint32_t *)paging_finestra_apri(fis);
        pila[off / 4]     = 0;      /* indirizzo di ritorno: nessuno */
        pila[off / 4 + 1] = arg;    /* il primo argomento */
        paging_finestra_chiudi();
    }

    proc_set_entry(filo, entry, esp);
    proc_set_ready(filo);

    klog(LOG_INFO, "SCHED: filo %u del gruppo %u, posto %u, stack 0x%08x-0x%08x, "
                   "tls 0x%08x", filo->pid, filo->tgid, posto, base, cima,
         filo->tls_tp);
    return (int)filo->pid;
}

/* =============================================================================
 * L'ATTESA CHE DORME
 *
 * ! CHI ASPETTA NON CONSUMA NIENTE, ed e' tutta la differenza con il lucchetto
 * che gira cedendo la CPU: quello resta READY e lo scheduler continua a
 * sceglierlo per scoprire ogni volta che non puo' fare niente. Qui il filo
 * esce dalla coda e non ci torna finche' qualcuno non lo chiama per nome —
 * cioe' per l'indirizzo su cui si e' fermato.
 *
 * ! E CHI SVEGLIA NON SA CHI STA SVEGLIANDO: cerca l'indirizzo, non il pid.
 * E' cio' che permette a un lucchetto di essere un intero e basta, senza
 * doversi ricordare chi c'e' in coda.
 * ============================================================================= */
int proc_attesa_sveglia(uint32_t dove, int quanti)
{
    uint32_t i, tgid = g_current ? g_current->tgid : 0;
    int      svegliati = 0;

    for (i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_process_pool[i];

        if (quanti > 0 && svegliati >= quanti) break;
        if (p->state != PROC_BLOCKED) continue;
        if (p->attesa_dove != dove)   continue;
        if (p->tgid != tgid)          continue;   /* l'indirizzo vale nel gruppo */

        p->attesa_dove = 0;
        p->block_until = 0;
        sched_unblock_locked(p->pid);
        svegliati++;
    }
    return svegliati;
}

/* =============================================================================
 * LA CANCELLAZIONE ORDINATA — si CHIEDE, non si impone
 *
 * ! UCCIDERE UN FILO SI POTEVA GIA': il tid e' un pid, e `kill` funziona. Il
 * punto e' che non si DEVE — un filo ucciso lascia i lucchetti presi, i file
 * aperti a meta' e la memoria che stava sistemando com'era. Dentro un processo
 * solo, dove tutti vedono le stesse pagine, quello non e' un processo che muore
 * male: e' un programma che continua a girare su strutture rotte.
 *
 * ! PERCIO' QUI NON SI FERMA NIENTE: si lascia un messaggio, e il filo lo
 * legge dove gli fa comodo. Il kernel fa due cose sole, e sono quelle che dal
 * di fuori non si possono fare: mettere il messaggio dove il filo lo trovera',
 * e SCROLLARE chi sta dormendo — perche' un filo addormentato non guarda
 * niente, e senza una scrollata la richiesta arriverebbe solo il giorno che
 * qualcun altro lo sveglia per un altro motivo.
 * ============================================================================= */
int proc_filo_ferma(uint32_t tid)
{
    Process *self = g_current;
    uint32_t i;

    if (self == NULL) return ERR(ESRCH);

    for (i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_process_pool[i];

        if (p->state == PROC_UNUSED || p->state == PROC_ZOMBIE) continue;
        if (p->pid != tid) continue;

        /* ! DEVE ESSERE DEL NOSTRO GRUPPO, come per thread_attendi: chiedere a
         * un task altrui di fermarsi sarebbe un modo per disturbare un
         * programma che non e' nostro. */
        if (p->tgid != self->tgid) return ERR(ESRCH);

        p->ferma = 1;

        if (p->state == PROC_BLOCKED && p->attesa_dove != 0) {
            /* Dorme su un'attesa: lo si sveglia adesso, e la scrollata e'
             * questa. Al risveglio ricontrollera' la sua condizione — e chi
             * vuole potersi fermare guarda `ferma` nella stessa occhiata. */
            p->attesa_dove = 0;
            p->block_until = 0;
            sched_unblock_locked(p->pid);
        } else {
            /* ! NON DORME ANCORA, ED E' IL CASO PERICOLOSO: puo' aver appena
             * guardato `ferma` e stare per addormentarsi. La scrollata si
             * lascia scritta, e la raccogliera' la sua prossima attesa —
             * altrimenti dormirebbe dopo aver guardato, e la richiesta
             * resterebbe li' senza nessuno che la legga. */
            p->scuoti = 1;
        }
        return 0;
    }
    return ERR(ESRCH);
}

void proc_gruppo_termina(uint32_t tgid, uint32_t risparmia_pid)
{
    uint32_t i;

    for (i = 0; i < MAX_PROCESSES; i++) {
        Process *p = &g_process_pool[i];

        if (p->state == PROC_UNUSED || p->state == PROC_ZOMBIE) continue;
        if (p->tgid != tgid || p->pid == risparmia_pid) continue;

        /* ! NON SI CHIUDONO I SUOI DESCRITTORI: sono quelli del gruppo, e chi
         * resta li sta ancora usando. Li chiude il capogruppo uscendo. */
        p->state     = PROC_ZOMBIE;
        p->exit_code = 0;
        runq_remove(p);
        klog(LOG_DEBUG, "SCHED: filo %u terminato col suo gruppo", p->pid);
    }
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

    /* ! CHI ESCE PORTA CON SE' TUTTO IL GRUPPO, ed e' l'unica semantica
     * sicura: gli altri fili vivono nella MEMORIA DI QUESTO PROCESSO, e
     * lasciarli correre mentre lo spazio di indirizzamento se ne va vuol dire
     * codice che gira sopra pagine liberate. E' la stessa scelta di exit_group
     * su Linux, presa per la stessa ragione.
     *
     * Un FILO che esce invece non porta via nessuno: e' il caso normale di
     * thread_esci(), e da qui in poi questa funzione non deve fare quasi
     * niente — i descrittori sono del gruppo, la memoria e' del gruppo. */
    if (g_current->pid == g_current->tgid) {
        proc_gruppo_termina(g_current->tgid, g_current->pid);
    } else {
        g_current->state = PROC_ZOMBIE;
        g_proc_count--;
        runq_remove(g_current);
        /* Sveglia chi lo stesse aspettando con thread_attendi(). */
        if (g_current->ppid != 0) sched_unblock_locked(g_current->ppid);
        {
            Process *dopo = sched_pick_next();
            sched_switch_to(dopo);
        }
        return;                     /* non si torna */
    }

    /* I descrittori vanno via ADESSO, non al waitpid del padre: vedi
     * proc_chiudi_fd(). Con una pipe aperta, ritardarlo e' uno stallo. */
    proc_chiudi_fd(g_current);

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

    /* L'eseguibile tenuto aperto per il caricamento su richiesta. Va
     * chiuso qui e non prima: finche' il processo esiste puo' faultare su
     * una pagina che non ha ancora toccato, e la sorgente e' quella. Un
     * handle non chiuso e' uno slot del VFS perso per sempre — dopo
     * VFS_MAX_OPEN processi terminati non si aprirebbe piu' niente. */
    if (p->exe_handle >= 0) {
        vfs_close(p->exe_handle);
        p->exe_handle = -1;
    }
    p->n_vma = 0;

    /* E i file che il processo aveva ancora aperti.
     *
     * Non lo faceva nessuno: chi usciva senza chiudere — un programma che
     * termina per un errore, o semplicemente uno che si fida dell'uscita —
     * lasciava lo slot VFS occupato per sempre. Il conto arriva dopo
     * VFS_MAX_OPEN volte, con un `open()` che risponde EMFILE senza che
     * nessuno stia tenendo aperto niente, e non e' un guasto su cui sia
     * facile risalire alla causa.
     *
     * Si vedeva poco finche' i programmi aprivano un file per volta e lo
     * chiudevano; un compilatore ne tiene aperti sei o sette, e ogni
     * invocazione che finisce male ne perde altrettanti.
     *
     * Qui e non in sys_exit: un processo terminato da un fault non passa
     * da sys_exit, e sono proprio quelli che i file li lasciano aperti. */
    /* ! UN FILO NON CHIUDE LA TABELLA DEI DESCRITTORI: non e' sua, e chi resta
     * nel gruppo la sta ancora usando. Si riporta il puntatore sulla propria —
     * che e' vuota, perche' il PCB nasce azzerato — cosi' la spazzata qui
     * sotto passa a vuoto invece di chiudere i file dei fratelli. */
    if (p->fdt != p->fds) p->fdt = p->fds;

    /* ! SPAZZATA DI SICUREZZA, non la chiusura vera. I descrittori li
     * rilascia proc_chiudi_fd() nel momento in cui il processo muore
     * (proc_exit / proc_kill), perche' con una pipe aperta aspettare il
     * waitpid del padre e' uno stallo — vedi il commento su
     * proc_chiudi_fd(). Qui resta il giro di controllo per un processo
     * arrivato a ZOMBIE per una strada che un giorno dimenticasse di
     * chiamarla: proc_chiudi_fd e' idempotente, quindi non costa niente
     * e non chiude niente due volte. */
    proc_chiudi_fd(p);

    /* ! LO SPAZIO DI INDIRIZZAMENTO E' DEL GRUPPO, non del PCB, e si libera
     * quando non resta nessuno a usarlo. Un filo che finisce non deve portarsi
     * via la memoria dei suoi fratelli; e il capogruppo, che quando esce li
     * termina tutti, potrebbe comunque essere raccolto PRIMA che i loro PCB
     * siano stati raccolti. Contare i vivi e' l'unico modo di sapere se questa
     * page directory serve ancora a qualcuno. */
    if (p->page_directory && p->page_directory != paging_get_kernel_directory() &&
        proc_gruppo_vivi(p->tgid) == 0) {
        paging_destroy_directory(p->page_directory);
    }
    p->page_directory = NULL;
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

    /* Chiude le zone di memoria condivisa che teneva aperte.
     *
     * ! LE PAGINE SONO GIA' A POSTO: paging_destroy_directory qui sopra le ha
     * smontate passando dal PMM, che ha tolto questo processo dal conteggio
     * dei proprietari. Quello che manca e' il NOME — una zona il cui ultimo
     * utente e' morto deve sparire, o il nome resta occupato da nessuno e chi
     * riparte non puo' piu' usarlo. E' lo stesso guasto che il commento qui
     * sopra descrive per i servizi IPC. */
    shm_cleanup_process(p);

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
            /* Anche qui, e per lo stesso motivo di proc_exit(): un
             * processo ucciso da un fault non passa da sys_exit, ed e'
             * proprio quello che lascerebbe una pipe aperta per sempre. */
            proc_chiudi_fd(&g_process_pool[i]);
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
    klog(LOG_INFO, "SCHED: avvio - sblocco IRQ0, %u processi pronti",
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
