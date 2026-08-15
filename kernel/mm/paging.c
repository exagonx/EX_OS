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
#include "vfs.h"   /* vfs_read: le pagine mancanti arrivano dall'eseguibile */

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
 * FINESTRA DI RIMAPPATURA FISICA
 *
 * IL PROBLEMA. Il kernel deve scrivere dentro pagine che appartengono a un
 * ALTRO spazio di indirizzamento: i segmenti di un ELF che sta caricando
 * per un figlio, la pagina appena data a chi ha chiamato sbrk. Finora lo
 * faceva dereferenziando l'indirizzo FISICO, che funziona solo finche'
 * quella pagina e' mappata per identita' nel CR3 corrente. Ma la PD di un
 * processo copia dalla PD del kernel soltanto le PDE sotto
 * USER_SPACE_BASE: la prima pagina fisica allocata oltre quella soglia
 * dava un page fault in ring0, cioe' un kernel panic, e con esso un tetto
 * a quanto puo' crescere un processo — indipendente da quanta RAM c'e'.
 *
 * LA FINESTRA. Una pagina virtuale dentro kernel_page_table_low, la
 * tabella statica installata in PDE[0] e copiata per valore in ogni PD:
 * riscriverne una PTE ha effetto in TUTTI gli spazi di indirizzamento, che
 * qui e' esattamente cio' che serve. Il kernel la punta alla pagina fisica
 * che gli serve, lavora, e la richiude. E' il kmap_atomic/fixmap di Linux
 * per la highmem, ridotto all'osso.
 *
 * DUE REGOLE, e sono entrambe obbligatorie:
 *
 *   1. Gli interrupt restano SPENTI mentre la finestra e' aperta. La
 *      finestra e' una risorsa sola: una preemption in mezzo e un altro
 *      contesto la ripunta sotto i piedi del primo, che continua a
 *      scrivere all'indirizzo di prima credendo di scrivere altrove.
 *
 *   2. Non si tiene aperta attraverso una chiamata che puo' bloccarsi.
 *      In elf_load fra una pagina e l'altra c'e' una vfs_read, che e' un
 *      messaggio IPC a un driver in ring3: la finestra si apre e si
 *      chiude intorno alla singola copia, mai intorno al ciclo.
 *
 * La pagina fisica omonima e' riservata nel PMM (pmm_init): il suo
 * contenuto non e' affidabile, perche' la PTE che la descrive viene
 * riscritta di continuo.
 * ============================================================================= */
static int      g_finestra_aperta = 0;
static uint32_t g_finestra_eflags = 0;

void *paging_finestra_apri(uint32_t phys)
{
    uint32_t eflags;

    __asm__ volatile ("pushf; pop %0" : "=r"(eflags));
    __asm__ volatile ("cli");

    /* Aprire una finestra gia' aperta significa che due percorsi di codice
     * la stanno usando insieme, e il primo dei due scrivera' nel posto
     * sbagliato senza accorgersene. E' un errore di programmazione del
     * kernel, non una condizione d'errore da riportare: meglio fermarsi
     * qui che consegnare dati corrotti a un processo. */
    if (g_finestra_aperta) {
        kpanic("PAGING: finestra di rimappatura aperta due volte");
    }

    g_finestra_aperta = 1;
    g_finestra_eflags = eflags;

    kernel_page_table_low[PT_INDEX(PAGING_FINESTRA_VIRT)] =
        (phys & 0xFFFFF000) | PG_PRESENT | PG_WRITABLE;   /* mai PG_USER */

    __asm__ volatile ("invlpg (%0)" : : "r"(PAGING_FINESTRA_VIRT) : "memory");

    return (void *)(PAGING_FINESTRA_VIRT + (phys & 0xFFF));
}

void paging_finestra_chiudi(void)
{
    if (!g_finestra_aperta) return;

    /* Si lascia NON PRESENTE invece di rimettere l'identita': se qualcuno
     * conserva il puntatore e lo usa dopo, deve prendere un fault subito e
     * rumorosamente, non leggere di nascosto una pagina che nel frattempo
     * e' di qualcun altro. */
    kernel_page_table_low[PT_INDEX(PAGING_FINESTRA_VIRT)] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(PAGING_FINESTRA_VIRT) : "memory");

    g_finestra_aperta = 0;

    /* Ripristina lo stato degli interrupt di CHI HA APERTO, che non e'
     * detto fosse "abilitati": questa funzione viene chiamata anche da
     * dentro sezioni gia' critiche. */
    if (g_finestra_eflags & (1u << 9)) __asm__ volatile ("sti");
}

/* Azzeramento di una pagina fisica qualunque: e' il caso d'uso piu'
 * frequente (ogni pagina nuova consegnata a un processo va azzerata, se no
 * il processo vede i resti di chi c'era prima) e merita di non far
 * ripetere apri/chiudi a ogni chiamante. */
void paging_azzera_fisica(uint32_t phys)
{
    uint8_t  *p = (uint8_t *)paging_finestra_apri(phys & 0xFFFFF000);
    uint32_t  n = PAGE_SIZE;

    while (n--) *p++ = 0;

    paging_finestra_chiudi();
}

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
 * ! SOVRASCRIVE UNA PTE GIA' PRESENTE SENZA DIRE NIENTE, e non e' una
 * dimenticanza: e' il contratto. Chi chiama sa gia' che quella pagina e'
 * sua, e un rifiuto qui costringerebbe ogni chiamante a distinguere fra
 * "non era mappata" e "era mappata e va bene cosi'".
 *
 * IL PREZZO, e va conosciuto: la vecchia pagina fisica NON viene
 * liberata (chi la teneva la perde e resta occupata per sempre), e
 * soprattutto NON c'E' NESSUN SEGNALE. Un errore di calcolo
 * nell'indirizzo non da' un fault ne' un log: da' due oggetti diversi
 * allo stesso indirizzo virtuale, e il guasto salta fuori molto dopo,
 * altrove.
 *
 * ! PERCIO' IL CONFINE LO METTE CHI CHIAMA. Le due vie per cui lo spazio
 * di un processo cresce — sys_sbrk e sys_mmap — controllano
 * `proc->heap_max` PRIMA di allocare (kernel 0.156). Prima non lo
 * facevano, e uno heap abbastanza grande avrebbe rimappato il blocco TLS
 * del processo stesso proprio in questo modo, in silenzio. Vedi il
 * commento su heap_max in kernel/include/sched.h.
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
        /* Page Table non esiste: alloca una nuova pagina dal PMM.
         *
         * Dalla FASCIA KERNEL: questa tabella verra' letta e scritta al
         * proprio indirizzo fisico (qui sotto, e in paging_get_physical,
         * e in paging_destroy_directory) mentre e' caricato il CR3 di un
         * processo qualunque. Una page table sopra USER_SPACE_BASE
         * sarebbe irraggiungibile proprio nei momenti in cui serve. */
        uint32_t pt_phys = pmm_alloc_page_kernel();
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
/* =============================================================================
 * Il framebuffer VESA nelle page directory dei processi
 *
 * ! MAPPARLO NELLA SOLA PD DEL KERNEL NON BASTA, e il modo in cui non basta
 * e' istruttivo: la console scrive nel framebuffer anche mentre gira un
 * processo utente — dentro una syscall, o da klog — e in quel momento CR3 e'
 * la PD di quel processo. Se li' l'indirizzo non e' mappato si prende un page
 * fault su un indirizzo che nessuno collegherebbe allo schermo. Il sintomo
 * era «PF: RAM esaurita caricando 0xfd00c000 per PID 4»: il gestore lo
 * scambiava per una pagina da caricare su richiesta.
 *
 * Le PD dei processi copiano dal kernel solo le entry sotto USER_SPACE_BASE;
 * il framebuffer sta molto piu' in alto (PDE 1012 per 0xFD000000), fuori da
 * quell'intervallo. Si annota qui quali entry aggiungere alla copia.
 * ============================================================================= */
static uint32_t g_fb_pde_da = 0;
static uint32_t g_fb_pde_a  = 0;   /* estremo escluso; 0 = nessun framebuffer */

int paging_mappa_framebuffer(uint32_t phys, uint32_t byte)
{
    uint32_t a;

    if (phys == 0 || byte == 0) return -1;

    for (a = 0; a < byte; a += PAGE_SIZE) {
        if (paging_map_page(kernel_page_directory, phys + a, phys + a,
                            PG_PRESENT | PG_WRITABLE) != 0) return -1;
    }

    g_fb_pde_da = PD_INDEX(phys);
    g_fb_pde_a  = PD_INDEX(phys + byte - 1) + 1;
    return 0;
}

PDE *paging_create_directory(void)
{
    /* Fascia kernel, per la stessa ragione delle page table: la PD di un
     * processo viene letta e modificata al proprio indirizzo fisico anche
     * mentre gira un ALTRO processo (sys_spawn, elf_load). */
    uint32_t phys = pmm_alloc_page_kernel();
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

    /* E il framebuffer, che sta fuori da quell'intervallo. Vedi sopra. */
    for (i = g_fb_pde_da; i < g_fb_pde_a; i++) {
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
 * ! LA PROPRIETA' ESCLUSIVA NON E' PIU' VERA, e questa funzione non ha
 * dovuto cambiare per questo. Fino ad agosto 2026 qui c'era scritto che ogni
 * pagina utente era di un processo solo, e che con la memoria condivisa
 * sarebbe servito «un conteggio riferimenti per pagina». Il conteggio adesso
 * c'e', e sta nel PMM (vedi kernel/include/pmm.h): pmm_free_page() cala il
 * numero di proprietari e restituisce la pagina solo all'ultimo. Il ciclo qui
 * sotto continua a liberare tutto cio' che trova, ed e' giusto — chiede al
 * PMM di togliere QUESTO processo, non di buttare la pagina.
 *
 * E' il motivo per cui il conteggio e' stato messo li' invece che nel gestore
 * delle zone condivise: la strada che libera piu' pagine di tutte e' questa,
 * e non sa niente della condivisione.
 * ============================================================================= */
void paging_destroy_directory(PDE *pd)
{
    uint32_t i, j;

    if (pd == NULL || pd == kernel_page_directory) return;

    for (i = PD_INDEX(USER_SPACE_BASE); i < 1024; i++) {
        /* ! LE ENTRY DEL FRAMEBUFFER NON SI LIBERANO. Stanno sopra
         * USER_SPACE_BASE come quelle utente, ma non sono roba di questo
         * processo: sono la stessa page table del kernel, copiata qui da
         * paging_create_directory perche' la console deve poter scrivere
         * anche mentre gira un processo. Liberarle vorrebbe dire restituire
         * al PMM pagine che non sono RAM — il sintomo era una raffica di
         * «PMM: pagina fuori range: 1036746» a ogni processo che finiva — e,
         * peggio, liberare la page table condivisa: il primo processo che
         * muore lascerebbe senza schermo tutti gli altri. */
        if (i >= g_fb_pde_da && i < g_fb_pde_a) continue;

        if (pd[i] & PG_PRESENT) {
            PTE *pt = (PTE *)PG_ADDR(pd[i]);

            for (j = 0; j < 1024; j++) {
                if (pt[j] & PG_PRESENT) {
                    uint32_t frame = PG_ADDR(pt[j]);

                    /* ! LE FINESTRE MMIO NON SI LIBERANO, per la stessa
                     * ragione del framebuffer qui sopra: non sono RAM e non
                     * sono mai state allocate da noi. Sono i registri di una
                     * scheda, messi qui da SYS_MMIO_MAP.
                     *
                     * Senza questo controllo la morte di ogni driver stampava
                     * una riga «PMM: pagina fuori range» per pagina mappata —
                     * un errore vero su un'operazione che non ha nessun
                     * errore, e che avrebbe insegnato a ignorare quel
                     * messaggio proprio quando serve. */
                    if (frame >= pmm_get_total_pages() * PAGE_SIZE) continue;

                    pmm_free_page(frame);
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
         * vedrebbe nel proprio stack i resti di un altro processo.
         * Attraverso la finestra, perche' la pagina puo' stare ovunque. */
        paging_azzera_fisica(phys);

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

/* =============================================================================
 * pf_carica_da_file — la pagina che manca sta nell'eseguibile
 *
 * E' la seconda meta' del caricamento su richiesta (la prima e' in elf.c):
 * il processo tocca un indirizzo che appartiene a un segmento annotato ma
 * non ancora presente in RAM, e qui lo si va a prendere dal file.
 *
 * Ritorna 1 se la pagina e' stata portata dentro (l'iret rieseguira'
 * l'istruzione che ha faultato), 0 in ogni caso dubbio — e "caso dubbio"
 * comprende "non e' un indirizzo di un segmento", perche' allora il fault
 * e' un errore vero e va diagnosticato dal chiamante.
 *
 * ! GLI INTERRUPT SI RIACCENDONO PER LA LETTURA. Il gate di #PF e' un
 * interrupt gate, quindi qui dentro IF e' spento; ma leggere dal disco
 * significa parlare con un driver e aspettare il timer, e con IF spento
 * l'attesa non finirebbe mai. Si riaccendono per la sola vfs_read e si
 * ripristina lo stato di prima — lo stesso schema di fdc_delay_ms in
 * fat12.c. La conseguenza da avere presente e' che il processo puo'
 * essere sospeso QUI DENTRO: e' legittimo (siamo nel suo contesto, sul
 * suo stack kernel) ed e' il motivo per cui, al ritorno, si ricontrolla
 * che la pagina non sia arrivata nel frattempo.
 *
 * Il buffer di lettura sta sullo STACK KERNEL e non e' uno statico: due
 * processi possono trovarsi qui insieme, uno fermo in attesa del disco e
 * l'altro appena entrato, e un buffer condiviso significherebbe il
 * contenuto di una pagina scritto dentro quella di un altro processo.
 * ============================================================================= */
static int pf_carica_da_file(Process *p, uint32_t fault_addr)
{
    uint32_t pagina = fault_addr & 0xFFFFF000;
    ProcVma *v      = NULL;
    uint32_t i, phys, letti = 0, da_leggere = 0, off_file = 0;
    uint8_t  buf[PAGE_SIZE];

    if (p == NULL || p->exe_handle < 0 || p->page_directory == NULL) return 0;

    for (i = 0; i < p->n_vma; i++) {
        if (pagina >= p->vma[i].vstart && pagina < p->vma[i].vend) {
            v = &p->vma[i];
            break;
        }
    }
    if (v == NULL) return 0;

    /* Quanti byte di QUESTA pagina vengono dal file. Oltre file_fine c'e'
     * il BSS: non e' un errore e non si legge niente, si lascia azzerato.
     * E' il caso di ogni programma — l'ultima pagina dei dati e' quasi
     * sempre mezza file e mezza BSS. */
    if (pagina < v->file_fine) {
        da_leggere = v->file_fine - pagina;
        if (da_leggere > PAGE_SIZE) da_leggere = PAGE_SIZE;
        off_file = v->file_off + (pagina - v->vstart);
    }

    if (da_leggere > 0) {
        uint32_t eflags;
        int      n;

        __asm__ volatile ("pushf; pop %0" : "=r"(eflags));
        __asm__ volatile ("sti");
        n = vfs_read(p->exe_handle, buf, da_leggere, off_file);
        if (!(eflags & (1u << 9))) __asm__ volatile ("cli");

        if (n < 0) {
            klog(LOG_ERROR, "PF: PID %u, lettura della pagina 0x%08x "
                 "dall'eseguibile fallita (%d)", p->pid, pagina, n);
            return 0;
        }
        letti = (uint32_t)n;
    }

    /* Mentre si aspettava il disco il processo era sospeso: se un altro
     * percorso ha gia' mappato questa pagina, mapparla di nuovo
     * perderebbe la prima (e la sua pagina fisica, per sempre). */
    if (paging_get_physical(p->page_directory, pagina) != 0) return 1;

    phys = pmm_alloc_page();
    if (phys == 0) {
        klog(LOG_ERROR, "PF: RAM esaurita caricando 0x%08x per PID %u",
             pagina, p->pid);
        return 0;
    }

    {
        uint8_t *dst = (uint8_t *)paging_finestra_apri(phys);
        uint32_t k;

        for (k = 0; k < letti; k++)     dst[k] = buf[k];
        for (; k < PAGE_SIZE; k++)      dst[k] = 0;

        paging_finestra_chiudi();
    }

    if (paging_map_page(p->page_directory, pagina, phys, v->pg_flags) != 0) {
        pmm_free_page(phys);
        klog(LOG_ERROR, "PF: mapping fallito per 0x%08x (PID %u)", pagina, p->pid);
        return 0;
    }

    return 1;
}

/* =============================================================================
 * vm_precarica_utente — porta in RAM le pagine di un buffer PRIMA che sia
 * un driver a toccarlo.
 *
 * Serve a una cosa sola, ma indispensabile: rompere il ciclo fra il
 * lucchetto del VFS e il fault-in. Una read() il cui buffer cade in una
 * pagina non ancora presente farebbe scrivere il driver dentro quella
 * pagina, con il lucchetto gia' in mano; il page fault chiamerebbe il VFS
 * per andarla a prendere e resterebbe fermo sul lucchetto di se stesso.
 *
 * Chiamarla PRIMA di entrare nel VFS toglie il caso: qui il lucchetto non
 * ce l'ha nessuno, e le pagine arrivano con la strada libera.
 *
 * Riguarda solo le pagine dell'ESEGUIBILE: heap e mmap sono gia' in RAM
 * quando vengono consegnati, e lo stack cresce da un fault che non tocca
 * il filesystem. Gli errori si ignorano di proposito — se la pagina non
 * si puo' caricare lo scoprira' l'accesso vero, con la sua diagnostica.
 * ============================================================================= */
void vm_precarica_utente(uint32_t addr, uint32_t len)
{
    Process *p = proc_get_current();
    uint32_t pag, fine;

    if (p == NULL || p->exe_handle < 0 || p->n_vma == 0 || len == 0) return;

    fine = addr + len;
    if (fine < addr) return;   /* somma che gira: non e' un buffer valido */

    for (pag = addr & 0xFFFFF000; pag < fine; pag += PAGE_SIZE) {
        if (paging_get_physical(p->page_directory, pag) == 0) {
            pf_carica_da_file(p, pag);
        }
    }
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

        /* CARICAMENTO SU RICHIESTA — dopo lo stack e prima della
         * diagnostica, e anche qui per entrambi i livelli: un fault ring0
         * su un puntatore che il programma ha passato a una syscall (una
         * stringa in .rodata mai letta prima) e' legittimo quanto uno
         * ring3, ed e' cio' che permette a syscall_verify_str di
         * dereferenziare senza sapere niente di paginazione. */
        if (!(err & 0x1) && pf_carica_da_file(p, fault_addr)) {
            return;
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

        /* =====================================================================
         * LA MAPPA DEL PROCESSO, subito sotto il fault
         *
         * ! SERVE A DIRE *DOVE* E' CADUTO, e senza costa una serata. Il
         * nome sta in PROCESS_NAME_LEN byte e i percorsi lunghi ci si
         * troncano dentro: '/cdrom/exos/libexec/gcc/i386-ex' puo' essere
         * cc1 come collect2, che stanno nella stessa directory. Restava
         * da dedurre l'identita' dagli indirizzi confrontandoli a mano con
         * `readelf -l` — cioe' da indovinare, con una prova per ipotesi.
         *
         * Heap e VMA il kernel li ha gia' in mano: stampandoli, un fault
         * si classifica leggendo, in una corsa sola. Sopra heap_end e
         * sotto heap_max e' lo heap che sbrk non ha mai mappato; dentro
         * una VMA e' il caricamento su richiesta che ha rinunciato; fuori
         * da tutto e' un puntatore sbagliato.
         * ===================================================================== */
        if (p != NULL) {
            uint32_t i;

            klog(LOG_ERROR, "PF:   heap 0x%08x..0x%08x (tetto 0x%08x), "
                 "stack 0x%08x..0x%08x",
                 p->heap_start, p->heap_end, p->heap_max,
                 p->user_stack_limit, p->user_stack_top);

            for (i = 0; i < p->n_vma; i++) {
                klog(LOG_ERROR, "PF:   vma[%u] 0x%08x..0x%08x  file+0x%x, "
                     "byte del file fino a 0x%08x%s",
                     i, p->vma[i].vstart, p->vma[i].vend, p->vma[i].file_off,
                     p->vma[i].file_fine,
                     (fault_addr >= p->vma[i].vstart &&
                      fault_addr <  p->vma[i].vend) ? "   <-- QUI" : "");
            }

            if (fault_addr >= p->heap_start && fault_addr < p->heap_max) {
                klog(LOG_ERROR, "PF:   l'indirizzo e' nello HEAP, %s heap_end: "
                     "%s", (fault_addr < p->heap_end) ? "SOTTO" : "SOPRA",
                     (fault_addr < p->heap_end)
                       ? "pagina persa sotto il confine (smappata da un sbrk "
                         "negativo?)"
                       : "il programma ha scritto oltre cio' che ha chiesto");
            }
        }

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
