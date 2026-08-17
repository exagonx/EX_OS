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
#include "vfs.h"    /* VFS_PATH_MAX: il PCB porta la directory corrente.
                     * Nessun ciclo: vfs.h non include sched.h. */

/* =============================================================================
 * IPC — message passing kernel-mediato
 *
 * Ogni processo ha una mailbox a dimensione fissa nel proprio PCB. Il
 * kernel copia i messaggi da mittente a destinatario durante la syscall
 * SYS_IPC_SEND — nessun driver o programma accede mai alla memoria di
 * un altro processo direttamente. Definita qui (non in ipc.h) per
 * evitare un include circolare con Process, che contiene la mailbox.
 * ============================================================================= */
/* =============================================================================
 * ! 1536 E NON 512: UN FRAME ETHERNET NON SI PUO' SPEZZARE A META'
 *
 * Fino ad agosto 2026 il limite era 512 byte, scelto perché copre un
 * settore FAT12. Un frame Ethernet arriva a 1514 byte (1500 di payload +
 * 14 di intestazione), e un driver di rete deve poterlo consegnare
 * INTERO: quello che entra dal cavo lo decide chi sta dall'altra parte,
 * non noi.
 *
 * L'alternativa era spezzare i frame in quattro messaggi con un numero
 * di sequenza. Sarebbe stato più economico in RAM e molto peggio in
 * tutto il resto: la mailbox è profonda 4, quindi un frame solo la
 * riempirebbe, e basterebbe un secondo mittente che si intromette per
 * lasciare mezzo frame in attesa di un pezzo che non arriva mai. La
 * logica di riassemblaggio finirebbe in ogni client del driver di rete —
 * cioè scritta più volte, e ogni volta in modo un po' diverso.
 *
 * IL COSTO, DETTO PER INTERO: la mailbox sta dentro il PCB, quindi
 * 64 processi x 4 messaggi x 1536 byte = 384 KB di BSS del kernel,
 * contro i 128 di prima. Sono 256 KB in più, azzerati all'avvio e mai
 * scritti su disco (il BSS non sta in kernel.bin).
 *
 * E NON LO PAGA LO SPAZIO UTENTE: sys_ipc_recv copia nella IpcMessage
 * del chiamante SOLO i tre campi di intestazione, mai l'array. La copia
 * userspace della struttura (lib/include/libc.h) è infatti di 12 byte
 * e non ha nessun data[] — vedi il commento lì.
 * ============================================================================= */
#define IPC_MSG_MAX_DATA    1536    /* payload max — copre un frame Ethernet */
#define IPC_MAILBOX_DEPTH   4       /* messaggi in coda per processo */
#define IPC_NAME_LEN        16      /* nome servizio, es. "tty", "floppy" */

typedef struct IpcMessage {
    uint32_t sender_pid;
    /* ! `tipo` e non `type`: la copia di questa struttura in
     * lib/include/libc.h ha dovuto cambiare nome perche' `type` e' una
     * parola che il codice di terzi definisce come macro (openlibm lo fa),
     * e un header pubblico non puo' permetterselo. Le due copie devono
     * restare identiche — e' la convenzione di questo progetto — quindi il
     * nome cambia anche qui, dove non servirebbe. Vedi il commento esteso
     * in libc.h. */
    uint32_t tipo;                  /* significato definito dal servizio */
    uint32_t len;                   /* byte validi in data[] */
    uint8_t  data[IPC_MSG_MAX_DATA];
} IpcMessage;

/* =============================================================================
 * Costanti scheduler
 * ============================================================================= */
#define MAX_PROCESSES       64      /* Massimo numero processi simultanei */

/* Quante zone di memoria condivisa puo' tenere aperte un processo. Quattro
 * perche' un client grafico ne usa una o due (il proprio buffer, e al piu'
 * una coda di eventi); il server ne apre una per client e per lui il tetto
 * vero e' SHM_MAX_ZONE, non questo. */
#define SHM_PER_PROC        4
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

/* =============================================================================
 * Blocco TLS — le variabili __thread di un processo
 *
 * TLS_TCB_SIZE: quanto sta SOPRA il thread pointer. L'ABI di i386 chiede
 * che a `tp + 0` ci sia un puntatore a `tp` stesso; il resto del TCB e'
 * roba della libreria dei thread, che qui non c'e'. Otto byte invece di
 * quattro per lasciare il blocco allineato a 8, che e' l'allineamento
 * naturale di un `double` in una variabile thread-local.
 *
 * TLS_MAX: un tetto dichiarato invece di un'allocazione che cresce a
 * sorpresa. 64 KB sono due ordini di grandezza sopra a cio' che usa il
 * codice reale — bfd ne dichiara una manciata di byte — e un binario che
 * ne chiede di piu' quasi certamente ha un program header sbagliato.
 * ============================================================================= */
#define TLS_TCB_SIZE        8
#define TLS_MAX             65536
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
    /* Le due estremita' di una pipe. ! SONO DUE TIPI E NON UNO CON UN
     * FLAG: la direzione non e' un dettaglio del descrittore, e' cio' che
     * decide se una read blocca o e' un errore. Tenerle distinte fa
     * sbagliare il compilatore invece del programma. `inode` contiene
     * l'handle della pipe (vedi kernel/include/pipe.h). */
    FD_PIPE_R   = 6,    /* estremita' di LETTURA  */
    FD_PIPE_W   = 7,    /* estremita' di SCRITTURA */
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
    /* =========================================================================
     * heap_max — IL TETTO, e perche' prima non c'era (kernel 0.156)
     *
     * heap_end cresceva finche' il PMM aveva pagine da dare: l'unico
     * limite era la RAM FISICA, non lo spazio di indirizzamento. Su una
     * macchina piccola non si notava — la memoria finiva prima — ma la
     * cosa che stava sopra lo heap non era il vuoto:
     *
     *      heap_start ... heap_end -->        <-- tls_base   riserva stack
     *      0x08xxxxxx                          ~0xBFFB....   0xBFFC0000
     *
     * ! E paging_map_page() SOVRASCRIVE una PTE gia' presente senza dire
     * niente (vedi kernel/mm/paging.c). Uno sbrk che avesse superato
     * tls_base avrebbe rimappato il blocco TLS su pagine nuove azzerate:
     * il thread pointer sarebbe diventato zero e ogni variabile __thread
     * avrebbe cominciato a leggere memoria altrui — in silenzio, senza un
     * fault, senza un log. Piu' sopra c'e' la riserva dello stack, dove il
     * danno sarebbe stato lo stesso al contrario: pagine gia' mappate dove
     * page_fault_handler si aspetta di poterle mappare lui.
     *
     * Ora il confine e' esplicito e sta nel PCB: una pagina di guardia
     * sotto il blocco TLS se c'e', sotto la riserva dello stack se non
     * c'e'. Chi lo supera si prende ENOMEM, che e' un errore che malloc()
     * sa gia' trattare.
     *
     * Vale 0 solo per un processo il cui spazio di indirizzamento non e'
     * stato preparato da elf_load: in quel caso lo heap NON si apre
     * affatto, invece di indovinare un indirizzo. Vedi sys_sbrk.
     * ========================================================================= */
    uint32_t        heap_max;               /* Tetto invalicabile dello heap */

    /* =========================================================================
     * IL THREAD POINTER — dove vivono le variabili __thread
     *
     * Vale 0 per chi non ne ha, che e' quasi tutto: un processo di EX-OS ha
     * un filo solo, quindi una variabile thread-local e' una variabile
     * globale con un nome piu' lungo. Serve al CODICE DI TERZI, che le usa
     * e le fa usare al compilatore senza chiedere il permesso — bfd
     * dichiara `static TLS bfd_error_type bfd_error` e GCC emette
     * `mov %gs:0x0, %ebx`.
     *
     * MODELLO local-exec, VARIANTE II (quella di i386):
     *
     *      indirizzi bassi                          indirizzi alti
     *      +----------------------+-------------------+
     *      |  blocco TLS (memsz)  |  TCB (8 byte)     |
     *      +----------------------+-------------------+
     *      ^ tls_base             ^ tls_tp
     *
     * `tls_tp` e' cio' che finisce nella base del descrittore GDT_TLS_SEL,
     * quindi `%gs:0` legge la prima parola del TCB — che per convenzione
     * contiene tls_tp stesso, ed e' l'unica cosa che ci sia dentro. Le
     * variabili stanno a offset NEGATIVI da li', risolti da `ld` al momento
     * del link (rilocazioni R_386_TLS_LE): a runtime non c'e' niente da
     * rilocare, ed e' il motivo per cui questo modello costa cosi' poco.
     *
     * ! NON C'E' IL TLS DINAMICO. __tls_get_addr, i modelli general-dynamic
     * e local-dynamic e le variabili __thread dentro una libreria condivisa
     * NON funzionano. Servono a chi carica codice a runtime; qui i binari
     * sono statici, e il giorno che non lo fossero questo e' il punto da
     * cui ripartire.
     * ========================================================================= */
    uint32_t        tls_tp;                 /* Thread pointer (0 = nessun TLS) */
    uint32_t        tls_base;               /* Prima pagina del blocco, per il log */
    /* ! Il blocco NON si libera a mano: sta nella page directory del
     * processo, e paging_destroy_directory() se lo porta via insieme a
     * tutto il resto quando il processo viene riassorbito. */

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

    /* =========================================================================
     * L'IDENTITA' DI CHI STA GIRANDO — uid e gid, dal 17 agosto 2026
     *
     * ! SI EREDITANO DAL PADRE, come `console` e `cwd` qui accanto, e per la
     * stessa ragione: un programma lanciato dalla shell di un utente e' quello
     * stesso utente. Senza eredita' ogni processo ripartirebbe da root, e i
     * permessi sarebbero una decorazione.
     *
     * ! uid 0 E' root, E PASSA DAPPERTUTTO. Non e' un caso particolare
     * infilato nei controlli: e' la definizione di root, e sta in UN posto
     * solo — vfs_permesso() in kernel/fs/vfs.c. Sparso nei singoli controlli
     * sarebbe la cosa piu' facile da dimenticare in uno di essi.
     *
     * ! IL PRIMO PROCESSO NASCE root, E DEVE. E' il kernel a crearlo, non c'e'
     * nessuno da cui ereditare, e da li' passa o per `login` — che scende a un
     * utente vero — o direttamente per la shell, quando si avvia da floppy o
     * da CD e l'installazione dev'essere possibile.
     * ========================================================================= */
    uint32_t        uid;
    uint32_t        gid;

/* =============================================================================
 * La directory corrente — UNA PER PROCESSO
 *
 * ! ERA UNA SOLA PER TUTTO IL SISTEMA: `static char g_cwd[]` dentro
 * kernel/syscall/syscall_impl.c. Funzionava finche' a usarla era una shell
 * sola, e sotto quella soglia il difetto non si vede — che e' esattamente
 * cio' che lo rendeva pericoloso.
 *
 * Si vede appena due processi lavorano in posti diversi, e sono casi
 * normali, non di laboratorio:
 *
 *   make -C sotto          make entra nella directory e sposta ANCHE la
 *                          shell che l'ha lanciato, e ogni altro processo
 *   ( cd altrove; ... )    un subshell che torna indietro non torna
 *                          indietro per se': muove tutti
 *   make -j2               due compilatori in directory diverse: il
 *                          secondo risolve i propri percorsi relativi
 *                          contro la directory del primo
 *
 * Il terzo e' il peggiore, perche' non da' un errore: da' un file APERTO
 * NEL POSTO SBAGLIATO. E su un sistema con piu' CPU sarebbe una corsa vera.
 *
 * ! SI EREDITA DAL PADRE, come `console` qui sopra e per la stessa ragione:
 * un programma lanciato dalla shell deve partire dalla directory in cui si
 * trovava chi l'ha lanciato, o `gcc prova.c` compilerebbe un altro file.
 *
 * ! NON E' UN CAMBIO DI ABI: il PCB e' memoria del kernel e nessun
 * programma lo vede. Costa PERCORSO_MAX byte per processo — 320 x 64
 * PCB = 20 KB in tutto, dentro un pool che il kernel ha gia'.
 * ============================================================================= */
    char            cwd[VFS_PATH_MAX];

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

    /* --- E' un driver? Il varco verso l'hardware, in un campo solo ---
     *
     * Lo mette il CARICATORE (percorso_di_driver in kernel/loader/elf.c)
     * guardando il nome dell'eseguibile: <qualcosa>.drv. Nessuna syscall lo
     * concede, quindi un programma non puo' dichiararsi driver mentre gira.
     *
     * Ci si appoggiano ioport_bind, dma_alloc e mmio_map.
     *
     * ! PRIMA IL CRITERIO ERA `io_port_count != 0`, e non era un confine:
     * ioport_bind non ha mai controllato niente, quindi qualunque programma
     * diventava «un driver» con una chiamata e un range di porte inventato.
     * E dalla parte opposta escludeva chi driver lo e' davvero: un
     * framebuffer non ha NESSUNA porta I/O, quindi il server grafico non
     * avrebbe potuto mappare i propri registri.
     *
     * ! NON E' UNA BARRIERA, e chi legge questo campo deve saperlo: senza
     * proprietari dei file, un programma puo' copiarsi in «x.drv». E' la
     * definizione di COS'E' un driver, fatta dal kernel su un fatto fissato
     * prima che il programma parta, invece che su cio' che il programma
     * dichiara di se' mentre gira. Il motivo per esteso sta su
     * percorso_di_driver(). */
    uint32_t        is_driver;

    /* --- Zone di memoria condivisa aperte da questo processo ---
     *
     * ! SERVE PERCHE' LA MORTE DEL PROCESSO NON PASSA DAL GESTORE DELLE ZONE.
     * Le PAGINE le rilascia paging_destroy_directory attraverso il PMM, che
     * conta i proprietari e non ha bisogno di sapere altro. Ma il NOME della
     * zona vive finche' un processo la tiene aperta, e per calare quel
     * conteggio bisogna sapere quali zone erano di chi e' appena morto.
     * Senza questo elenco il nome resterebbe occupato per sempre da un
     * processo che non c'e' piu'.
     *
     * `zona` e' l'indice nella tabella PIU' UNO: zero vuol dire slot libero,
     * cosi' un PCB azzerato nasce senza agganci e non serve inizializzare
     * niente in proc_create. */
    struct {
        uint32_t    zona;                   /* indice+1, 0 = slot libero */
        uint32_t    virt;                   /* dove il processo la vede */
        uint32_t    pagine;
    }               shm[SHM_PER_PROC];

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
