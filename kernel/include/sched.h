/* =============================================================================
 * kernel/include/sched.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef SCHED_H
#define SCHED_H

#include "kernel.h"
#include "paging.h"
#include "fpu.h"    /* FPU_STATE_SIZE: il PCB porta lo stato del coprocessore */

/* =============================================================================
 * IPC — message passing kernel-mediato
 *
 * Ogni processo ha una mailbox a dimensione fissa nel proprio PCB. Il
 * kernel copia i messaggi da mittente a destinatario durante la syscall
 * SYS_IPC_SEND — nessun driver o programma accede mai alla memoria di
 * un altro processo direttamente. Definita qui (non in ipc.h) per
 * evitare un include circolare con Process, che contiene la mailbox.
 * ============================================================================= */
#define IPC_MSG_MAX_DATA    512     /* payload max — copre un settore FAT12 */
#define IPC_MAILBOX_DEPTH   4       /* messaggi in coda per processo */
#define IPC_NAME_LEN        16      /* nome servizio, es. "tty", "floppy" */

typedef struct IpcMessage {
    uint32_t sender_pid;
    uint32_t type;                  /* significato definito dal servizio */
    uint32_t len;                   /* byte validi in data[] */
    uint8_t  data[IPC_MSG_MAX_DATA];
} IpcMessage;

/* =============================================================================
 * Costanti scheduler
 * ============================================================================= */
#define MAX_PROCESSES       64      /* Massimo numero processi simultanei */
/* 32 e non 16 (0.150): un compilatore tiene aperti insieme il sorgente,
 * l'uscita, gli header della catena di inclusione e uno o due file
 * temporanei, e 16 e' un tetto che si tocca senza fare niente di strano.
 * Costa 16 FileDescriptor in piu' per PCB. */
#define MAX_FD              32      /* File descriptor per processo */
#define KERNEL_STACK_SIZE   131072  /* 128KB stack kernel per processo — RAM estesa via PMM */
/* =============================================================================
 * STACK UTENTE A CRESCITA SU FAULT (kernel 0.124)
 *
 * Prima c'era un solo USER_STACK_SIZE = 65536, e elf_load() allocava
 * quei 64 KB per INTERO a ogni processo — driver e programmi minuscoli
 * compresi — azzerandoli byte per byte. Ora si separano due concetti che
 * prima coincidevano:
 *
 *   MAX   spazio di indirizzamento RISERVATO. Non costa RAM: e' solo il
 *         confine oltre il quale lo stack non puo' crescere. Superarlo
 *         termina il processo, che e' cio' che deve succedere a una
 *         ricorsione infinita.
 *   INIT  RAM davvero IMPEGNATA al caricamento. Tutto il resto viene
 *         mappato una pagina alla volta, e solo se il programma la tocca
 *         davvero (vedi page_fault_handler in kernel/mm/paging.c).
 *
 * Il risultato e' che un programma piccolo costa 8 KB invece di 64, e che
 * il tetto puo' essere alzato senza che nessuno lo paghi: e' la ragione
 * per cui MAX e' 256 KB e non 64 come prima.
 *
 * SLACK e' il margine sotto ESP entro cui un fault viene considerato
 * crescita legittima invece che puntatore impazzito: 'pusha' scrive 32
 * byte sotto ESP prima di aggiornarlo, ed e' il caso peggiore fra le
 * istruzioni che toccano memoria sotto il puntatore di stack.
 * ============================================================================= */
#define USER_STACK_MAX      262144  /* 256KB riservati (spazio, non RAM) */
#define USER_STACK_INIT     8192    /* 8KB impegnati al caricamento */
#define USER_STACK_SLACK    32      /* margine sotto ESP: caso peggiore pusha */
#define PROCESS_NAME_LEN    32      /* Lunghezza massima nome processo */
#define MAX_ENV_VARS        32      /* Variabili d'ambiente per processo */
#define ENV_VAR_LEN         128     /* Lunghezza max chiave=valore */

/* Priorità scheduler */
#define PRIO_IDLE       0   /* Task idle (solo quando nessun altro è pronto) */
#define PRIO_LOW        1   /* Bassa priorità */
#define PRIO_NORMAL     2   /* Priorità normale (default) */
#define PRIO_HIGH       3   /* Alta priorità (driver critici) */
#define PRIO_RT         4   /* Real-time (non preemptible da task normali) */
#define PRIO_MAX        PRIO_RT

/* Quantum di tempo per priorità (in tick a 100Hz) */
#define QUANTUM_IDLE    1
#define QUANTUM_LOW     2
#define QUANTUM_NORMAL  4
#define QUANTUM_HIGH    8
#define QUANTUM_RT      16

/* =============================================================================
 * Stati di un processo
 * ============================================================================= */
typedef enum {
    PROC_UNUSED     = 0,    /* Slot PCB non utilizzato */
    PROC_READY      = 1,    /* Pronto per l'esecuzione */
    PROC_RUNNING    = 2,    /* In esecuzione correntemente */
    PROC_BLOCKED    = 3,    /* In attesa di un evento (I/O, sleep, ecc.) */
    PROC_ZOMBIE     = 4,    /* Terminato, aspetta wait() del parent */
    PROC_SLEEPING   = 5,    /* In sleep(n), sveglio dopo n tick */
} ProcState;

/* =============================================================================
 * Contesto CPU salvato durante il context switch
 *
 * Layout sullo stack al momento della chiamata a sched_switch():
 * (salvato da context_switch.asm via pushad + segmenti)
 * ============================================================================= */
typedef struct {
    /* Registri general-purpose (ordine pushad) */
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;           /* ESP al momento del context switch */
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
    /* Registri segmento */
    uint32_t ds;
    uint32_t es;
    uint32_t fs;
    uint32_t gs;
    /* EIP e EFLAGS (recuperati dallo stack interrupt) */
    uint32_t eip;
    uint32_t eflags;
    /* Selettori CS/SS */
    uint32_t cs;
    uint32_t ss;
} CpuContext;

/* =============================================================================
 * File Descriptor
 * ============================================================================= */
typedef enum {
    FD_UNUSED   = 0,
    FD_STDIN    = 1,    /* 0: standard input */
    FD_STDOUT   = 2,    /* 1: standard output */
    FD_STDERR   = 3,    /* 2: standard error */
    FD_FILE     = 4,    /* file FAT12 */
    FD_DRIVER   = 5,    /* device driver */
} FDType;

typedef struct {
    FDType      type;
    uint32_t    flags;
    uint32_t    offset;         /* Posizione corrente nel file */
    uint32_t    inode;          /* Numero cluster FAT12 o ID device */
    void       *driver_data;    /* Dati privati del driver */
} FileDescriptor;

/* =============================================================================
 * ProcVma — un segmento dell'eseguibile, mappato su richiesta
 *
 * Quattro segmenti bastano con abbondanza: un ELF prodotto da GCC ne ha
 * due o tre (testo, dati di sola lettura, dati+BSS). Se un giorno ne
 * arrivasse uno con piu' PT_LOAD di cosi', elf_load carica in RAM tutto
 * quanto invece di mappare a meta': meglio lento che sbagliato.
 * ============================================================================= */
#define PROC_MAX_VMA    4

typedef struct {
    uint32_t vstart;    /* prima pagina del segmento (allineata) */
    uint32_t vend;      /* prima pagina DOPO il segmento (esclusa) */
    uint32_t file_off;  /* offset nel file che corrisponde a vstart */
    uint32_t file_fine; /* indirizzo virtuale dove finiscono i byte del
                         * file: da qui a vend e' BSS, cioe' solo zeri e
                         * niente da leggere */
    uint32_t pg_flags;  /* PG_PRESENT | PG_USER | eventuale PG_WRITABLE */
} ProcVma;

/* =============================================================================
 * Process Control Block (PCB)
 * ============================================================================= */
typedef struct Process {
    /* --- Identità --- */
    uint32_t        pid;                    /* Process ID (univoco) */
    uint32_t        ppid;                   /* Parent PID */
    char            name[PROCESS_NAME_LEN]; /* Nome processo */

    /* --- Stato --- */
    ProcState       state;                  /* Stato corrente */
    uint32_t        priority;               /* Priorità (PRIO_*) */
    uint32_t        quantum;                /* Tick rimasti nel quantum corrente */
    uint32_t        quantum_total;          /* Quantum iniziale (per reset) */
    int32_t         exit_code;             /* Codice uscita (per wait()) */

    /* --- Contesto CPU --- */
    CpuContext      ctx;                    /* Stato registri salvato */
    uint32_t        kernel_esp;             /* ESP kernel (per context switch) */

    /* Stato del coprocessore x87 (agosto 2026).
     *
     * Non sta in CpuContext perche' quello descrive lo stack costruito da
     * pushad/pushfd, che l'assembly di context_switch legge e scrive per
     * posizione: infilarci 108 byte vorrebbe dire rifare quei conti. Qui
     * e' un campo a se', salvato e ripristinato in C da sched_switch_to.
     *
     * Allineato a 16 byte per non dover ripensare a nulla il giorno che
     * FNSAVE lasciasse il posto a FXSAVE, che l'allineamento lo PRETENDE.
     * Vedi kernel/include/fpu.h per il guasto che tutto questo evita. */
    uint8_t         fpu_state[FPU_STATE_SIZE] ALIGNED(16);

    /* --- Stack --- */
    uint32_t        kernel_stack_base;      /* Indirizzo base stack kernel */
    uint32_t        kernel_stack_top;       /* Top stack kernel */
    /* base = pagina piu' bassa ATTUALMENTE mappata; scende man mano che
     * lo stack cresce. limit = confine invalicabile della riserva: sotto
     * quello il fault non e' crescita ma esaurimento, e il processo va
     * terminato. Vedi USER_STACK_MAX/INIT sopra. */
    uint32_t        user_stack_base;        /* Indirizzo base stack utente */
    uint32_t        user_stack_top;         /* Top stack utente */
    uint32_t        user_stack_limit;       /* Confine della riserva (0 = nessuno) */

    /* --- Memoria --- */
    PDE            *page_directory;         /* Page Directory processo */
    uint32_t        heap_start;             /* Inizio heap utente */
    uint32_t        heap_end;               /* Fine heap utente corrente */

    /* --- Segmenti caricati SU RICHIESTA dall'eseguibile ---
     *
     * Il caricatore non copia piu' i segmenti in RAM al momento dello
     * spawn: annota qui dove ciascuno vive nel file, e le pagine arrivano
     * una alla volta quando il processo le tocca (page_fault_handler ->
     * pf_carica_da_file). Un programma di cui si esegue un decimo occupa
     * un decimo della memoria, e il tempo di avvio non dipende piu' dalla
     * dimensione del binario ma da quanto ne serve subito.
     *
     * exe_handle e' l'eseguibile tenuto APERTO per tutta la vita del
     * processo: senza, non ci sarebbe da dove leggere le pagine mancanti.
     * Si chiude in proc_reap_zombie e all'inizio di un nuovo elf_load
     * (exec sostituisce l'immagine). Vale -1 per i processi che
     * un'immagine su file non ce l'hanno: idle, init, e i driver, che si
     * caricano tutti in RAM apposta (vedi elf_load_residente). */
    ProcVma         vma[PROC_MAX_VMA];
    uint32_t        n_vma;
    int             exe_handle;

    /* --- File descriptors --- */
    FileDescriptor  fds[MAX_FD];

    /* Console virtuale di appartenenza (0..VGA_N_CONSOLE-1).
     *
     * È il terminale del processo: dove finisce ciò che scrive su stdout
     * e da dove arriva ciò che legge da stdin. Si eredita dal padre in
     * sys_spawn, così un programma lanciato dalla shell della console 2
     * resta sulla console 2 — e continua a girare, e a scrivere sul
     * proprio schermo, anche mentre se ne guarda un'altra.
     *
     * Zero è la console di sistema, quella dei messaggi del kernel: è
     * anche il valore giusto per un processo creato prima che esistesse
     * un padre da cui ereditare. */
    uint32_t        console;

    /* --- Environment --- */
    char            env[MAX_ENV_VARS][ENV_VAR_LEN];
    uint32_t        env_count;

    /* --- Timing --- */
    uint32_t        ticks_total;            /* Tick CPU totali consumati */
    uint32_t        sleep_until;            /* Tick di risveglio (PROC_SLEEPING) */

    /* Scadenza di un'attesa BLOCCANTE con timeout (0 = attesa senza
     * scadenza, il comportamento storico). Diversa da sleep_until, che
     * dice "svegliami e basta": qui il processo aspetta un EVENTO e la
     * scadenza è solo il limite oltre il quale rinuncia. Chi si sveglia
     * per scadenza lo scopre ricontrollando la propria condizione e
     * trovandola ancora falsa — vedi ipc_recv_timeout in kernel/ipc/ipc.c. */
    uint32_t        block_until;
    uint32_t        created_tick;           /* Tick alla creazione */

    /* --- Lista scheduler --- */
    struct Process *next;                   /* Prossimo processo nella run queue */
    struct Process *prev;                   /* Precedente nella run queue */

    /* --- IPC: mailbox kernel-mediata --- */
    IpcMessage      ipc_mailbox[IPC_MAILBOX_DEPTH];
    uint32_t        ipc_head;               /* prossimo slot da leggere */
    uint32_t        ipc_tail;               /* prossimo slot da scrivere */
    uint32_t        ipc_count;              /* messaggi attualmente in coda */
    char            ipc_service_name[IPC_NAME_LEN]; /* nome registrato (vuoto se nessuno) */

    /* --- I/O porte: whitelist kernel-mediata per driver ring3 ---
     * Un solo range contiguo per processo (sufficiente per un
     * controller hardware tipico: tastiera 0x60-0x64, FDC 0x3F0-0x3F7).
     * io_port_count == 0 significa nessun accesso I/O consentito. */
    uint32_t        io_port_base;
    uint32_t        io_port_count;

} Process;

/* =============================================================================
 * Interfaccia scheduler
 * ============================================================================= */

/* Inizializzazione */
void     sched_init(void);
void     sched_start(void);   /* sblocca IRQ0, non ritorna — chiamare a fine boot */
void     sched_stop(void);    /* congela i context switch; g_ticks continua ad avanzare */

/* Gestione processi */
Process *proc_create(const char *name, uint32_t entry_point,
                     uint32_t priority, int is_kernel_task);
void     proc_set_entry(Process *proc, uint32_t entry_point, uint32_t user_stack_top);
void     proc_exit(int32_t exit_code);
void     proc_set_ready(Process *proc);   /* BLOCKED → READY, aggiunge alla run queue */
void     proc_kill(uint32_t pid);
void     proc_reap_zombie(Process *p); /* Libera risorse zombie, segna UNUSED */

extern Process *g_init_task;  /* task reaper (PID 2), adotta gli orfani */
Process *proc_get_by_pid(uint32_t pid);
Process *proc_get_current(void);
extern Process g_process_pool[MAX_PROCESSES];   /* usato da ipc.c per il registro nomi */

/* Scheduler */
void     sched_tick(void);              /* Chiamato ogni IRQ0 (100Hz) */
void     sched_yield(void);             /* Cede volontariamente la CPU */
void     sched_block(ProcState reason); /* Blocca il processo corrente */
void     sched_unblock(uint32_t pid);   /* Sblocca un processo */

/* =============================================================================
 * Processo in PRIMO PIANO su una console
 *
 * È il job control: su ogni console un solo processo per volta possiede
 * la tastiera, e gli altri — quelli lanciati con '&' — leggono da stdin
 * la fine dell'input invece di rubargli i tasti.
 *
 * Senza questa nozione il driver tastiera farebbe l'unica cosa che sa
 * fare, cioè servire l'ultimo che ha chiesto: un programma in
 * background che legge stdin sostituirebbe la shell come lettore, e la
 * shell resterebbe bloccata per sempre in attesa di una riga che nessuno
 * le consegnerà più. Il prompt sparirebbe e la console con lui.
 *
 * 0 significa "nessuno dichiarato": in quel caso legge chi vuole, che è
 * il comportamento di prima che i job esistessero — necessario perché il
 * sistema funzioni anche prima che una shell abbia detto la sua.
 * ============================================================================= */
uint32_t sched_console_fg(uint32_t console);
void     sched_set_console_fg(uint32_t console, uint32_t pid);
void     sched_unblock_locked(uint32_t pid); /* Come sopra, ma il chiamante deve già avere cli attivo (uso da contesto IRQ, es. ipc.c) */
void     sched_sleep(uint32_t ms);      /* Dorme per ms millisecondi */

/* Statistiche */
void     sched_dump(void);

/* Funzione ASM context switch */
extern void context_switch(uint32_t *old_esp, uint32_t new_esp,
                            uint32_t new_cr3);
extern void sched_enter_usermode(uint32_t entry, uint32_t user_esp);
extern void pit_configure(uint32_t frequency_hz);

/* Tick counter globale (incrementato da IRQ0) */
extern volatile uint32_t g_ticks;

#endif /* SCHED_H */
