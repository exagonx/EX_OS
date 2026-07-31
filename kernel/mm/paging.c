/* =============================================================================
 * kernel/mm/paging.c
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
#include "pmm.h"
#include "paging.h"
#include "idt.h"
#include "sched.h"

/* =============================================================================
 * Struttura Page Directory Entry (PDE) — formato hardware x86
 *
 * Bit  0: Present (P)         — 1 = page table presente
 * Bit  1: Read/Write (R/W)    — 0 = solo lettura, 1 = lettura+scrittura
 * Bit  2: User/Supervisor (U) — 0 = ring0 only, 1 = ring0+ring3
 * Bit  3: Write-Through (WT)
 * Bit  4: Cache Disable (CD)
 * Bit  5: Accessed (A)        — settato dalla CPU al primo accesso
 * Bit  6: riservato (0)
 * Bit  7: Page Size (PS)      — 0 = 4KB pages, 1 = 4MB pages (non usato)
 * Bit  8: riservato
 * Bit  9-11: AVL               — uso OS
 * Bit 12-31: Page Table Base   — indirizzo fisico page table / 4096
 * ============================================================================= */
typedef uint32_t PDE;   /* Page Directory Entry */
typedef uint32_t PTE;   /* Page Table Entry */

/* Flag PDE/PTE */
#define PG_PRESENT      (1 << 0)    /* Pagina presente in memoria */
#define PG_WRITABLE     (1 << 1)    /* Pagina scrivibile */
#define PG_USER         (1 << 2)    /* Accessibile da ring3 */
#define PG_WRITE_THRU   (1 << 3)    /* Write-through caching */
#define PG_CACHE_DIS    (1 << 4)    /* Cache disabilitata */
#define PG_ACCESSED     (1 << 5)    /* Acceduta (settata dalla CPU) */
#define PG_DIRTY        (1 << 6)    /* Modificata (solo PTE) */
#define PG_HUGE         (1 << 7)    /* 4MB page (solo PDE, non usiamo) */
#define PG_GLOBAL       (1 << 8)    /* Non invalidare dal TLB su CR3 switch */

/* Estrae l'indirizzo fisico da un PDE/PTE (maschera i 12 flag bit bassi) */
#define PG_ADDR(entry)  ((entry) & 0xFFFFF000)

/* Indici nella PD/PT da indirizzo virtuale */
#define PD_INDEX(vaddr) (((vaddr) >> 22) & 0x3FF)  /* Bit 31-22 */
#define PT_INDEX(vaddr) (((vaddr) >> 12) & 0x3FF)  /* Bit 21-12 */

/* =============================================================================
 * Kernel Page Directory (statica — allocata nel BSS del kernel)
 *
 * Allineata a 4KB (obbligatorio per CR3).
 * Questa è la PD usata dal kernel durante il boot e per tutti i contesti
 * che non hanno ancora una PD propria.
 * ============================================================================= */
static PDE kernel_page_directory[1024] ALIGNED(4096);

/* Page Table per il primo 1MB + zona kernel (mapping identità 0x0 - 0x400000)
 * Copre: 4MB = 1024 pagine × 4KB */
static PTE kernel_page_table_low[1024]  ALIGNED(4096);  /* 0x000000 - 0x3FFFFF */

/* Paginazione attiva? */
static int g_paging_enabled = 0;

/* Page directory corrente (usata dallo scheduler per context switch) */
static PDE *g_current_pd = NULL;

/* =============================================================================
 * paging_map_page — Mappa una pagina virtuale → fisica in una Page Directory
 *
 * pd:         Page Directory del processo (o kernel_page_directory)
 * virt_addr:  indirizzo virtuale da mappare (allineato a 4KB)
 * phys_addr:  indirizzo fisico destinazione (allineato a 4KB)
 * flags:      PG_PRESENT | PG_WRITABLE | PG_USER | ...
 *
 * Se la Page Table per questo indirizzo non esiste ancora, ne alloca una
 * nuova dal PMM.
 *
 * Ritorna: 0 = successo, -1 = errore (OOM)
 * ============================================================================= */
int paging_map_page(PDE *pd, uint32_t virt_addr, uint32_t phys_addr, uint32_t flags)
{
    uint32_t  pd_idx = PD_INDEX(virt_addr);
    uint32_t  pt_idx = PT_INDEX(virt_addr);
    PTE      *pt;

    /* Allineamento obbligatorio */
    virt_addr &= 0xFFFFF000;
    phys_addr &= 0xFFFFF000;

    if (pd[pd_idx] & PG_PRESENT) {
        /* Page Table già esistente: usa quella */
        pt = (PTE *)PG_ADDR(pd[pd_idx]);
    } else {
        /* Page Table non esiste: alloca una nuova pagina dal PMM */
        uint32_t pt_phys = pmm_alloc_page();
        if (pt_phys == 0) {
            klog(LOG_ERROR, "PAGING: OOM durante map_page virt=0x%08x", virt_addr);
            return -1;
        }

        /* Azzera la nuova Page Table */
        pt = (PTE *)pt_phys;
        {
            uint32_t *p = (uint32_t *)pt;
            uint32_t  n = 1024;
            while (n--) *p++ = 0;
        }

        /* Inserisci nella Page Directory */
        pd[pd_idx] = pt_phys | PG_PRESENT | PG_WRITABLE | (flags & PG_USER);

        klog(LOG_DEBUG, "PAGING: nuova PT a 0x%08x per PD[%u]", pt_phys, pd_idx);
    }

    /* Scrivi l'entry nella Page Table */
    pt[pt_idx] = phys_addr | (flags & 0xFFF) | PG_PRESENT;

    /* Invalida TLB per questo indirizzo virtuale */
    if (g_paging_enabled) {
        __asm__ volatile ("invlpg (%0)" :: "r"(virt_addr) : "memory");
    }

    return 0;
}

/* =============================================================================
 * paging_unmap_page — Rimuove il mapping di una pagina virtuale
 *
 * NON libera la pagina fisica (quello lo fa il chiamante se necessario).
 * ============================================================================= */
void paging_unmap_page(PDE *pd, uint32_t virt_addr)
{
    uint32_t pd_idx = PD_INDEX(virt_addr);
    uint32_t pt_idx = PT_INDEX(virt_addr);
    PTE     *pt;

    if (!(pd[pd_idx] & PG_PRESENT)) return; /* PD entry non presente */

    pt = (PTE *)PG_ADDR(pd[pd_idx]);
    pt[pt_idx] = 0;    /* Rimuovi entry */

    if (g_paging_enabled) {
        __asm__ volatile ("invlpg (%0)" :: "r"(virt_addr) : "memory");
    }
}

/* =============================================================================
 * paging_get_physical — Traduce un indirizzo virtuale → fisico
 *
 * Ritorna l'indirizzo fisico, o 0 se non mappato.
 * ============================================================================= */
uint32_t paging_get_physical(PDE *pd, uint32_t virt_addr)
{
    uint32_t pd_idx = PD_INDEX(virt_addr);
    uint32_t pt_idx = PT_INDEX(virt_addr);
    uint32_t offset = virt_addr & 0xFFF;
    PTE     *pt;

    if (!(pd[pd_idx] & PG_PRESENT)) return 0;

    pt = (PTE *)PG_ADDR(pd[pd_idx]);
    if (!(pt[pt_idx] & PG_PRESENT)) return 0;

    return PG_ADDR(pt[pt_idx]) | offset;
}

/* =============================================================================
 * paging_map_range — Mappa un range di pagine con mapping identità
 *
 * Mappa [base, base+size) con virt == phys.
 * Usato per mappare il kernel stesso nella PD.
 * ============================================================================= */
static int paging_map_range_identity(PDE *pd, uint32_t base, uint32_t size,
                                      uint32_t flags)
{
    uint32_t addr;
    uint32_t end = ALIGN_UP(base + size, PAGE_SIZE);
    base = ALIGN_DOWN(base, PAGE_SIZE);

    for (addr = base; addr < end; addr += PAGE_SIZE) {
        if (paging_map_page(pd, addr, addr, flags) != 0) {
            return -1;
        }
    }
    return 0;
}

/* =============================================================================
 * paging_init — Inizializza la paginazione del kernel
 *
 * Chiamata da kernel_main() dopo pmm_init().
 * Al termine CR0.PG = 1 e il kernel gira con paginazione attiva.
 * ============================================================================= */
void paging_init(void)
{
    uint32_t i;

    klog(LOG_INFO, "PAGING: inizializzazione...");

    /* -------------------------------------------------------------------------
     * Passo 1: Azzera la Page Directory del kernel
     * ------------------------------------------------------------------------- */
    for (i = 0; i < 1024; i++) {
        kernel_page_directory[i] = 0;
    }

    /* -------------------------------------------------------------------------
     * Passo 2: Mapping identità del primo 1MB (0x0 - 0xFFFFF)
     *
     * Necessario per:
     *   - Tabella IVT (0x0000)
     *   - BDA (0x0400)
     *   - Memoria VGA (0xB8000)
     *   - BIOS ROM (0xF0000)
     *   - Buffer Stage 2 (0x0500-0xC0FF): code, FAT@0xA000, BootInfo@0xC000
     *
     * Usiamo la page table statica kernel_page_table_low per evitare
     * chiamate al PMM durante l'init (PMM potrebbe non essere ancora pronto).
     * ------------------------------------------------------------------------- */
    klog(LOG_INFO, "PAGING: mapping primo 1MB (identita')...");

    /* Azzera la page table */
    for (i = 0; i < 1024; i++) kernel_page_table_low[i] = 0;

    /* Mappa 0x0 - 0x3FFFFF (4MB) con mapping identità, kernel+write */
    for (i = 0; i < 1024; i++) {
        kernel_page_table_low[i] = (i * PAGE_SIZE) | PG_PRESENT | PG_WRITABLE;
    }

    /* Installa PT nella PD (indice 0 → copre 0x0 - 0x3FFFFF) */
    kernel_page_directory[0] = (uint32_t)kernel_page_table_low
                                | PG_PRESENT | PG_WRITABLE;

    /* -------------------------------------------------------------------------
     * Passo 3: Mapping identità del kernel (0x100000 - fine kernel+bitmap)
     *
     * Il kernel è caricato a 0x100000 (1MB).
     * Mappiamo tutto il range kernel + bitmap PMM con mapping identità.
     * ------------------------------------------------------------------------- */
    {
        extern uint32_t _kernel_start;
        extern uint32_t _kernel_end;

        /* PMM è già inizializzato, possiamo usare pmm_alloc_page() per le PT */
        uint32_t kstart = ALIGN_DOWN((uint32_t)&_kernel_start, PAGE_SIZE);

        /* FIX CRITICO: mappa TUTTA la RAM fisica disponibile, non solo
         * kernel+bitmap. paging_create_directory() copia questo mapping
         * UNA VOLTA, al momento in cui la PD di un processo viene creata
         * — non è un riferimento condiviso live a kernel_page_directory.
         * Se il PMM alloca pagine oltre il limite qui mappato (es. lo
         * stack kernel da 128KB di un processo creato più tardi, come
         * "hello" lanciato dalla shell via sys_spawn), quelle pagine
         * restano invisibili in QUALUNQUE page directory creata PRIMA di
         * quel momento — inclusa quella della shell stessa, che è ancora
         * il CR3 attivo mentre esegue la syscall che crea "hello".
         * paging_get_physical() cammina quella PD stantia dereferenziando
         * puntatori fisici: un #PF silenzioso a metà syscall, con "cd"
         * (nessuna creazione processo) che continua a funzionare mentre
         * ogni spawn di un programma esterno fallisce o si blocca.
         * Mappare qui tutta la RAM elimina l'intera classe di bug: da
         * questo momento in poi kernel_page_directory non cresce più
         * nello spazio kernel, quindi ogni copia successiva (per ogni
         * nuovo processo) è già completa e resta valida per sempre. */
        uint32_t total_ram = pmm_get_total_pages() * PAGE_SIZE;
        uint32_t kend      = ALIGN_UP(total_ram, PAGE_SIZE);

        /* Non scendere sotto la vecchia soglia (kernel+bitmap) nel caso
         * pmm_get_total_pages() riporti meno RAM del previsto */
        uint32_t kend_min  = ALIGN_UP((uint32_t)&_kernel_end +
                              (pmm_get_total_pages() / 8) + PAGE_SIZE,
                              PAGE_SIZE);
        if (kend < kend_min) kend = kend_min;

        klog(LOG_INFO, "PAGING: mapping RAM completa 0x%08x - 0x%08x (%u MB)",
             kstart, kend, (kend - kstart) / (1024 * 1024));

        if (paging_map_range_identity(kernel_page_directory, kstart,
                                       kend - kstart,
                                       PG_PRESENT | PG_WRITABLE) != 0) {
            kpanic("PAGING: impossibile mappare il kernel!");
        }
    }

    /* -------------------------------------------------------------------------
     * Passo 4: Mapping identità area VGA (0xB8000 - 0xBFFFF)
     * Già coperta dal mapping del primo 1MB (indice 0), ma lo esplicitiamo
     * per chiarezza e per future modifiche di permessi.
     * ------------------------------------------------------------------------- */
    /* Già mappata sopra nella PT index 0 */

    /* -------------------------------------------------------------------------
     * Passo 5: Carica CR3 con l'indirizzo fisico della Page Directory
     * e abilita paginazione (CR0 bit 31 = PG)
     * ------------------------------------------------------------------------- */
    klog(LOG_INFO, "PAGING: carico CR3 = 0x%08x", (uint32_t)kernel_page_directory);

    g_current_pd    = kernel_page_directory;
    g_paging_enabled = 0; /* Non ancora attiva */

    /* Carica CR3 */
    write_cr3((uint32_t)kernel_page_directory);

    /* Abilita paginazione: CR0 |= PG (bit 31) */
    uint32_t cr0 = read_cr0();
    cr0 |= 0x80000000;
    write_cr0(cr0);

    g_paging_enabled = 1;

    klog(LOG_INFO, "PAGING: paginazione attiva (CR0.PG=1)");
    klog(LOG_INFO, "PAGING: mapping identita' 0x0-0x3FFFFF + kernel");
}

/* =============================================================================
 * paging_create_directory — Crea una nuova Page Directory per un processo
 *
 * Copia il mapping del kernel nella nuova PD (così ogni processo può
 * eseguire syscall e usare il kernel).
 * Le aree utente (virt >= USER_SPACE_BASE) sono inizialmente non mappate.
 *
 * Ritorna: puntatore alla nuova PD, NULL se OOM
 * ============================================================================= */
PDE *paging_create_directory(void)
{
    uint32_t phys = pmm_alloc_page();
    PDE     *pd;
    uint32_t i;

    if (phys == 0) {
        klog(LOG_ERROR, "PAGING: OOM creando Page Directory processo");
        return NULL;
    }

    pd = (PDE *)phys;

    /* Azzera la nuova PD */
    for (i = 0; i < 1024; i++) pd[i] = 0;

    /* Copia il mapping del kernel dalla kernel_page_directory
     * Solo le entry che coprono lo spazio kernel (non user) */
    for (i = 0; i < PD_INDEX(USER_SPACE_BASE); i++) {
        pd[i] = kernel_page_directory[i];
    }

    klog(LOG_DEBUG, "PAGING: nuova PD processo a 0x%08x", phys);
    return pd;
}

/* =============================================================================
 * paging_destroy_directory — Distrugge la PD di un processo
 *
 * Libera TUTTA la memoria utente del processo: per ogni page table
 * presente nello spazio utente, libera prima le pagine fisiche di dati
 * che mappa (codice, dati, heap, stack — tutto cio' che elf_load() e
 * sys_mmap() hanno allocato con pmm_alloc_page() e mappato qui dentro),
 * poi la page table stessa, infine la PD.
 *
 * Nota: questo presuppone che ogni pagina fisica mappata nello spazio
 * utente di un processo sia di proprieta' ESCLUSIVA di quel processo
 * (vero oggi: niente fork() con pagine condivise, niente mmap MAP_SHARED
 * tra processi diversi — ogni pmm_alloc_page() in elf_load()/sys_mmap()
 * e' privato). Se in futuro si introduce memoria condivisa tra processi,
 * questa funzione andra' rivista per non liberare pagine ancora in uso
 * altrove (serve un conteggio riferimenti per pagina).
 * ============================================================================= */
void paging_destroy_directory(PDE *pd)
{
    uint32_t i, j;

    if (pd == NULL || pd == kernel_page_directory) return;

    for (i = PD_INDEX(USER_SPACE_BASE); i < 1024; i++) {
        if (pd[i] & PG_PRESENT) {
            PTE *pt = (PTE *)PG_ADDR(pd[i]);

            for (j = 0; j < 1024; j++) {
                if (pt[j] & PG_PRESENT) {
                    pmm_free_page(PG_ADDR(pt[j]));
                }
            }

            /* Libera la page table stessa */
            pmm_free_page(PG_ADDR(pd[i]));
        }
    }

    /* Libera la PD stessa */
    pmm_free_page((uint32_t)pd);

    klog(LOG_DEBUG, "PAGING: PD processo 0x%08x distrutta (dati utente inclusi)",
         (uint32_t)pd);
}

/* =============================================================================
 * paging_switch — Cambia la Page Directory attiva (context switch)
 *
 * Chiamato dallo scheduler ad ogni switch di processo.
 * Aggiorna CR3 → il TLB viene automaticamente invalidato dalla CPU.
 * ============================================================================= */
void paging_switch(PDE *pd)
{
    if (pd == g_current_pd) return;  /* Stessa PD: nessun switch necessario */

    g_current_pd = pd;
    write_cr3((uint32_t)pd);
}

/* =============================================================================
 * paging_get_kernel_directory — Ritorna la PD del kernel
 * ============================================================================= */
PDE *paging_get_kernel_directory(void)
{
    return kernel_page_directory;
}

/* =============================================================================
 * paging_get_current_directory — Ritorna la PD corrente
 * ============================================================================= */
PDE *paging_get_current_directory(void)
{
    return g_current_pd;
}

/* =============================================================================
 * page_fault_handler — Handler per #PF Page Fault (INT 14)
 *
 * Registrato in isr.c tramite isr_register_handler(14, page_fault_handler).
 *
 * Un fault generato da codice in ring3 (bit U dell'error code) NON deve
 * abbattere l'intero kernel: significa solo che QUEL programma ha un bug
 * (puntatore nullo, overflow, accesso a memoria non sua, ecc.). In
 * un'architettura dove driver/servizi/comandi gireranno come processi
 * userspace, questo e' essenziale — un driver buggy deve poter "morire"
 * da solo senza portarsi dietro il sistema. Solo un fault originato in
 * ring0 (bug reale nel kernel) giustifica un panic.
 *
 * In futuro: copy-on-write, demand paging, stack growth (gestiti PRIMA
 * di decidere se e' un fault "vero" o uno di questi casi legittimi).
 * ============================================================================= */
/* =============================================================================
 * pf_cresci_stack — il fault e' una crescita legittima dello stack utente?
 *
 * Ritorna 1 se il fault e' stato SODDISFATTO (il chiamante deve tornare, e
 * l'iret rieseguira' l'istruzione che ha faultato), 0 se non era un caso di
 * crescita e va trattato come un errore vero.
 *
 * QUESTA FUNZIONE PUO' TRASFORMARE UN BUG IN CORRUZIONE SILENZIOSA, ed e'
 * scritta per non farlo. Il gestore dei fault ha oggi una proprieta'
 * preziosa: qualunque accesso inatteso termina il processo con una
 * diagnostica esatta. Ogni condizione qui sotto esiste per NON perderla —
 * in caso di dubbio si ritorna 0 e il processo muore come prima.
 *
 * LE CONDIZIONI, e cosa scartano:
 *
 *   1. pagina ASSENTE (P=0). Una violazione di protezione su una pagina
 *      gia' mappata non e' mai crescita: e' una scrittura su sola lettura
 *      o un accesso ring3 a memoria kernel. Resta un errore.
 *   2. il processo deve avere una riserva (limit != 0), cioe' essere
 *      passato da elf_load. Un task kernel non ha stack utente.
 *   3. l'indirizzo deve stare SOTTO la parte gia' impegnata: sopra e'
 *      gia' mappato, quindi un fault li' e' un'altra cosa.
 *   4. l'indirizzo deve stare SOPRA il confine della riserva. Sotto non e'
 *      crescita ma ESAURIMENTO: e' il caso della ricorsione infinita, e il
 *      processo deve morire. E' il motivo per cui la riserva e' limitata.
 *   5. solo per i fault da ring3: l'indirizzo deve essere VICINO A ESP.
 *      E' la condizione che distingue "lo stack sta crescendo" da "un
 *      puntatore impazzito e' finito per caso dentro la finestra dello
 *      stack". Un accesso molto piu' in basso di ESP non e' uno stack che
 *      cresce, e va ucciso.
 *
 * PERCHE' SI SERVONO ANCHE I FAULT DA RING0 (senza la condizione 5).
 * syscall_verify_ptr() controlla solo l'INTERVALLO di un puntatore utente,
 * non che le sue pagine siano mappate. Un programma che fa
 *     char buf[8192];  read(0, buf, sizeof buf);
 * passa un buffer perfettamente legittimo di cui pero' non ha mai toccato
 * le pagine: e' il KERNEL a scriverci per primo, con CPL=0, quindi il
 * fault arriva con U=0. Senza questo ramo sarebbe un kernel panic — un
 * guasto che l'allocazione ansiosa di prima nascondeva perche' le pagine
 * c'erano comunque tutte. La condizione 5 li' non e' applicabile: in un
 * fault ring0->ring0 la CPU non impila SS:ESP, quindi frame->user_esp non
 * contiene l'ESP utente e leggerlo darebbe un valore arbitrario.
 * ============================================================================= */
static int pf_cresci_stack(Process *p, InterruptFrame *frame,
                           uint32_t fault_addr, uint32_t err, int from_user)
{
    uint32_t pagina, ind;

    if (p == NULL)                          return 0;
    if (err & 0x1)                          return 0;   /* 1: non "assente" */
    if (p->user_stack_limit == 0)           return 0;   /* 2: nessuna riserva */
    if (p->user_stack_base  == 0)           return 0;
    if (fault_addr >= p->user_stack_base)   return 0;   /* 3: gia' mappato */
    if (fault_addr <  p->user_stack_limit)  return 0;   /* 4: esaurito */

    /* 5: vicinanza a ESP, solo dove ESP e' davvero disponibile.
     * Nessun rischio di overflow nella somma: fault_addr e' gia' stato
     * confinato fra limit e base, entrambi ampiamente sotto 0xC0000000. */
    if (from_user && fault_addr + USER_STACK_SLACK < frame->user_esp) {
        return 0;
    }

    /* Impegna TUTTE le pagine da quella che ha faultato fino alla base
     * attuale, non solo quella. Un programma puo' scendere di parecchie
     * pagine in un colpo solo ("sub esp, N" seguito da una scrittura), e
     * lasciare buchi non mappati in mezzo significherebbe un fault per
     * ognuno, con il rischio che uno di essi arrivi in un contesto dove
     * non possiamo servirlo. Il ciclo e' limitato dalla riserva. */
    pagina = fault_addr & 0xFFFFF000;

    for (ind = pagina; ind < p->user_stack_base; ind += PAGE_SIZE) {
        uint32_t phys = pmm_alloc_page();

        if (phys == 0) {
            /* Memoria esaurita: non e' un errore del programma, ma non
             * possiamo soddisfarlo. Si torna 0 e il chiamante lo termina
             * con la diagnostica normale — meglio un processo morto e un
             * messaggio che un sistema che prosegue con un mapping a meta'. */
            klog(LOG_ERROR, "PF: RAM esaurita crescendo lo stack di PID %u a 0x%08x",
                 p->pid, ind);
            return 0;
        }

        /* Azzerare e' obbligatorio, non igiene: senza, il processo
         * vedrebbe nel proprio stack i resti di un altro processo. */
        {
            uint8_t *z = (uint8_t *)phys;
            uint32_t n = PAGE_SIZE;
            while (n--) *z++ = 0;
        }

        if (paging_map_page(p->page_directory, ind, phys,
                            PG_PRESENT | PG_WRITABLE | PG_USER) != 0) {
            pmm_free_page(phys);
            klog(LOG_ERROR, "PF: mapping fallito crescendo lo stack di PID %u a 0x%08x",
                 p->pid, ind);
            return 0;
        }
    }

    p->user_stack_base = pagina;

    /* LOG_INFO e non DEBUG: con verboseboot=1 si vede quando e quanto gli
     * stack crescono davvero — serve a capire se USER_STACK_INIT e' tarato
     * bene — mentre con verboseboot=0 sparisce e non disturba l'uso
     * normale. Riga rimovibile senza conseguenze. */
    klog(LOG_INFO, "PF: stack di PID %u '%s' cresciuto a 0x%08x (%u KB, %s)",
         p->pid, p->name, pagina,
         (p->user_stack_top - pagina) / 1024,
         from_user ? "ring3" : "ring0");

    return 1;
}

void page_fault_handler(InterruptFrame *frame)
{
    uint32_t fault_addr = read_cr2();
    uint32_t err        = frame->err_code;

    /* Decodifica error code:
     *   Bit 0: P — 0=not present, 1=protection violation
     *   Bit 1: W — 0=read, 1=write
     *   Bit 2: U — 0=supervisor, 1=user
     *   Bit 3: R — reserved bit violation
     *   Bit 4: I — instruction fetch */
    const char *reason    = (err & 0x1) ? "protezione" : "pagina assente";
    const char *access    = (err & 0x2) ? "scrittura"  : "lettura";
    int         from_user = (err & 0x4) != 0;
    const char *ring      = from_user ? "user" : "kernel";

    /* CRESCITA DELLO STACK — va provata PRIMA di decidere che il fault sia
     * un errore, e per entrambi i livelli: un fault ring0 su un buffer
     * utente non ancora impegnato e' legittimo quanto uno ring3. Vedi
     * pf_cresci_stack, che ritorna 0 in ogni caso dubbio. */
    {
        Process *p = proc_get_current();

        if (pf_cresci_stack(p, frame, fault_addr, err, from_user)) {
            return;   /* l'iret rieseguira' l'istruzione che ha faultato */
        }
    }

    if (from_user) {
        Process *p = proc_get_current();

        /* Distingue l'esaurimento dello stack da un fault qualunque: e' la
         * differenza fra "ricorsione infinita" e "puntatore sbagliato", e
         * senza questo dettaglio le due si presentano identiche. */
        if (p != NULL && p->user_stack_limit != 0 &&
            fault_addr <  p->user_stack_limit &&
            fault_addr >= p->user_stack_limit - USER_STACK_MAX) {
            klog(LOG_ERROR, "PF: PID %u '%s' ha esaurito lo stack "
                 "(riserva di %u KB fino a 0x%08x, richiesto 0x%08x)",
                 p->pid, p->name, USER_STACK_MAX / 1024,
                 p->user_stack_limit, fault_addr);
        }

        vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
        kprintf("\n[FAULT] PID %u '%s': page fault a 0x%08x (%s, %s, EIP=0x%08x) — processo terminato\n",
                p ? p->pid : 0, p ? p->name : "?", fault_addr, reason, access, frame->eip);
        vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_BLACK);

        klog(LOG_ERROR, "PF: PID %u '%s' terminato per fault a 0x%08x EIP=0x%08x (%s/%s)",
             p ? p->pid : 0, p ? p->name : "?", fault_addr, frame->eip, reason, access);

        /* proc_exit() non ritorna: fa il context switch verso il
         * prossimo processo pronto da qui stesso, esattamente come fa
         * sys_exit() per un'uscita volontaria. */
        proc_exit(-11); /* convenzione stile SIGSEGV */
        /* mai raggiunto */
    }

    /* Fault in ring0: bug reale del kernel, qui ha senso fermare tutto
     * per non proseguire con stato potenzialmente corrotto. */
    vga_setcolor(VGA_COLOR_WHITE, VGA_COLOR_RED);
    kprintf("\n");
    kprintf("╔══════════════════════════════════════════════╗\n");
    kprintf("║            PAGE FAULT (KERNEL)               ║\n");
    kprintf("╠══════════════════════════════════════════════╣\n");
    kprintf("║  Indirizzo : 0x%08x                    ║\n", fault_addr);
    kprintf("║  Causa     : %-32s  ║\n", reason);
    kprintf("║  Accesso   : %-32s  ║\n", access);
    kprintf("║  Livello   : %-32s  ║\n", ring);
    kprintf("║  EIP       : 0x%08x                    ║\n", frame->eip);
    kprintf("║  Err code  : 0x%08x                    ║\n", err);
    kprintf("╚══════════════════════════════════════════════╝\n");

    kpanic("Page Fault non gestito in ring0 a 0x%08x (EIP=0x%08x)",
           fault_addr, frame->eip);
}
