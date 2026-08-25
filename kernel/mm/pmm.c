/* =============================================================================
 * kernel/mm/pmm.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "pmm.h"
#include "paging.h"   /* PAGING_FINESTRA_FISICA: la pagina da non allocare mai */

/* =============================================================================
 * Costanti
 * ============================================================================= */
#define PAGE_SIZE       4096            /* 4KB per pagina */
#define PAGES_PER_BYTE  8               /* 8 pagine per byte di bitmap */
#define BITMAP_BIT_SET  1
#define BITMAP_BIT_CLR  0

/* Indirizzo fisico massimo gestibile (4GB per x86 32-bit senza PAE) */
#define MAX_PHYS_ADDR   0xFFFFFFFFUL

/* =============================================================================
 * Stato interno PMM
 * ============================================================================= */
static uint8_t  *g_bitmap       = NULL; /* Puntatore alla bitmap */
static uint32_t  g_total_pages  = 0;    /* Numero totale pagine fisiche */
static uint32_t  g_free_pages   = 0;    /* Pagine libere correnti */
static uint32_t  g_used_pages   = 0;    /* Pagine usate correnti */
static uint32_t  g_bitmap_size  = 0;    /* Dimensione bitmap in byte */

/* Riferimenti IN PIU' DEL PRIMO, un byte per pagina: 0 = un solo
 * proprietario, cioe' il caso normale. Il perche' sta in pmm.h. Sta subito
 * dopo la bitmap e viene protetto insieme a lei. */
static uint8_t  *g_rif          = NULL;
static uint32_t  g_rif_size     = 0;
#define RIF_MAX  255u
static uint32_t  g_last_alloc   = 0;    /* Ultima pagina allocata (hint next-fit) */
static uint32_t  g_last_alloc_k = 0;    /* Hint separato per la fascia kernel:
                                         * condividerlo con quello generale
                                         * farebbe ripartire ogni ricerca
                                         * kernel da dove l'ha lasciata una
                                         * allocazione utente, cioe' quasi
                                         * sempre fuori fascia. */

/* Prima pagina FUORI dalla fascia kernel (definita piu' sotto, insieme
 * alle allocazioni di fascia: qui serve solo la firma). */
static uint32_t pmm_limite_kernel(void);

/* =============================================================================
 * Macro bitmap
 * ============================================================================= */
#define BITMAP_SET(page)    (g_bitmap[(page) / 8] |=  (uint8_t)(1 << ((page) % 8)))
#define BITMAP_CLR(page)    (g_bitmap[(page) / 8] &= (uint8_t)~(1 << ((page) % 8)))
#define BITMAP_TEST(page)   (g_bitmap[(page) / 8] &   (uint8_t)(1 << ((page) % 8)))

/* =============================================================================
 * Funzioni helper
 * ============================================================================= */

/* Indirizzo fisico → numero pagina */
static inline uint32_t addr_to_page(uint32_t addr)
{
    return addr / PAGE_SIZE;
}

/* Numero pagina → indirizzo fisico (inizio pagina) */
static inline uint32_t page_to_addr(uint32_t page)
{
    return page * PAGE_SIZE;
}

/* Marca un range di pagine come usate */
static void pmm_mark_used(uint32_t base_addr, uint32_t length)
{
    uint32_t page_start = addr_to_page(base_addr);
    uint32_t page_end   = addr_to_page(base_addr + length + PAGE_SIZE - 1);
    uint32_t i;

    if (page_end > g_total_pages) page_end = g_total_pages;

    for (i = page_start; i < page_end; i++) {
        if (!BITMAP_TEST(i)) {
            BITMAP_SET(i);
            g_used_pages++;
            if (g_free_pages > 0) g_free_pages--;
        }
    }
}

/* Marca un range di pagine come libere */
static void pmm_mark_free(uint32_t base_addr, uint32_t length)
{
    uint32_t page_start = addr_to_page(base_addr);
    uint32_t page_end   = addr_to_page(base_addr + length);
    uint32_t i;

    if (page_end > g_total_pages) page_end = g_total_pages;

    for (i = page_start; i < page_end; i++) {
        if (BITMAP_TEST(i)) {
            BITMAP_CLR(i);
            g_free_pages++;
            if (g_used_pages > 0) g_used_pages--;
        }
    }
}

/* =============================================================================
 * pmm_init — Inizializza il Physical Memory Manager
 *
 * Chiamata da kernel_main() con la mappa E820 di Stage 2.
 * Dopo questa chiamata pmm_alloc_page() e pmm_free_page() sono utilizzabili.
 * ============================================================================= */
void pmm_init(BootInfo *info)
{
    E820Entry *map;
    uint32_t   i;
    uint32_t   max_addr   = 0;

    klog(LOG_INFO, "PMM: inizializzazione Physical Memory Manager...");

    /* =========================================================================
     * Passo 1: fin dove arriva la RAM VERA
     *
     * ! SOLO LE REGIONI USABILI, e il commento lo diceva gia' mentre il codice
     * faceva un'altra cosa: il ciclo prendeva il massimo su TUTTE le voci
     * della mappa E820, comprese quelle riservate. Su una macchina con 64 MB
     * il risultato non era 0x04000000 ma 0xFEE01000 — il registro dell'APIC
     * locale, che di RAM non ne ha nemmeno un byte.
     *
     * ! E NON ERA UN NUMERO SBAGLIATO DENTRO UN LOG. g_total_pages e' la
     * risposta del PMM alla domanda «questo indirizzo fisico e' RAM?», e su
     * quella risposta si regge chi libera lo spazio di un processo morto:
     * paging_destroy_directory salta le pagine che stanno OLTRE la RAM,
     * perche' sono finestre MMIO mappate da mmio_map/fb_map e non sono mai
     * state allocate da noi. Con il tetto a 0xFEE01000, il framebuffer di
     * VirtualBox — che sta a 0xE0000000, cioe' SOTTO — non veniva piu'
     * riconosciuto: alla morte del server grafico le sue 469 pagine di
     * memoria video entravano nell'elenco delle pagine libere.
     *
     * Da li' in poi l'allocatore le consegnava come RAM qualunque. Il primo
     * programma avviato dopo aver chiuso la scrivania si ritrovava lo stack o
     * una tabella delle pagine DENTRO la scheda video: sullo schermo
     * comparivano colori e disegni casuali, e il programma moriva di page
     * fault su una pagina che aveva appena ottenuto. Su QEMU il framebuffer
     * sta piu' in alto e la stessa mappa lo salvava per caso.
     *
     * ! COSTAVA ANCHE 1,1 MB DI RAM su una macchina da 64. La bitmap e
     * l'elenco dei riferimenti sono dimensionati su g_total_pages: un byte
     * per pagina per un milione di pagine inesistenti.
     * ========================================================================= */
    if (info->e820_count == 0 || info->e820_addr == 0) {
        /* Fallback: usa mem_upper da Stage 2 */
        max_addr = (info->mem_lower + info->mem_upper) * 1024;
        klog(LOG_WARN, "PMM: nessuna mappa E820, uso fallback: %u KB", 
             info->mem_lower + info->mem_upper);
    } else {
        map = (E820Entry *)info->e820_addr;
        for (i = 0; i < info->e820_count; i++) {
            /* Solo regioni usabili e con base < 4GB */
            if (map[i].type != E820_TYPE_USABLE) continue;
            if (map[i].base_high != 0) continue; /* > 4GB, ignora */
            uint32_t end = map[i].base_low + map[i].length_low;
            if (end > max_addr) max_addr = end;
        }
    }

    if (max_addr == 0) {
        kpanic("PMM: impossibile determinare la dimensione della RAM!");
    }

    /* Allinea max_addr a pagina */
    max_addr = ALIGN_UP(max_addr, PAGE_SIZE);

    g_total_pages = max_addr / PAGE_SIZE;
    g_bitmap_size = ALIGN_UP(g_total_pages / 8, PAGE_SIZE);

    klog(LOG_INFO, "PMM: RAM massima rilevata: 0x%08x (%u MB)",
         max_addr, max_addr / (1024 * 1024));
    klog(LOG_INFO, "PMM: pagine totali: %u (bitmap: %u byte)",
         g_total_pages, g_bitmap_size);

    /* -------------------------------------------------------------------------
     * Passo 2: Posiziona la bitmap subito dopo la fine del kernel
     * Il simbolo _kernel_end è definito nel linker script.
     * ------------------------------------------------------------------------- */
    extern uint32_t _kernel_end;
    g_bitmap = (uint8_t *)ALIGN_UP((uint32_t)&_kernel_end, PAGE_SIZE);

    klog(LOG_INFO, "PMM: bitmap a 0x%08x (dimensione: %u byte)",
         (uint32_t)g_bitmap, g_bitmap_size);

    /* I riferimenti stanno subito dopo la bitmap: un byte per pagina. Su una
     * macchina da 64 MB sono 16 KB, e li paga anche chi la condivisione non la
     * usa — ma il costo dell'alternativa (un conteggio consultato solo da chi
     * sa della condivisione) e' un sito di liberazione dimenticato, e quello
     * si paga con una pagina riusata sotto qualcuno. */
    g_rif      = g_bitmap + g_bitmap_size;
    g_rif_size = ALIGN_UP(g_total_pages, PAGE_SIZE);

    klog(LOG_INFO, "PMM: riferimenti a 0x%08x (%u byte, 1 per pagina)",
         (uint32_t)g_rif, g_rif_size);

    /* -------------------------------------------------------------------------
     * Passo 3: Inizializza bitmap — TUTTO marcato come usato (sicuro di default)
     * ------------------------------------------------------------------------- */
    {
        uint8_t *p = g_bitmap;
        uint32_t sz = g_bitmap_size;
        while (sz--) *p++ = 0xFF;   /* 0xFF = tutte le pagine usate */
    }
    /* ! ZERO E' IL VALORE GIUSTO, non solo un azzeramento di cortesia: vuol
     * dire «un solo proprietario». */
    {
        uint8_t *p = g_rif;
        uint32_t sz = g_rif_size;
        while (sz--) *p++ = 0;
    }
    g_free_pages = 0;
    g_used_pages = g_total_pages;

    /* -------------------------------------------------------------------------
     * Passo 4: Marca come libere SOLO le regioni E820 usabili
     * ------------------------------------------------------------------------- */
    if (info->e820_count > 0 && info->e820_addr != 0) {
        map = (E820Entry *)info->e820_addr;
        for (i = 0; i < info->e820_count; i++) {
            if (map[i].type  != E820_TYPE_USABLE) continue;
            if (map[i].base_high != 0)            continue; /* > 4GB */
            if (map[i].length_low == 0)           continue;

            klog(LOG_DEBUG, "PMM: regione usabile: base=0x%08x len=0x%08x",
                 map[i].base_low, map[i].length_low);

            pmm_mark_free(map[i].base_low, map[i].length_low);
        }
    } else {
        /* Fallback: marca manualmente le regioni standard */
        /* RAM convenzionale: 0x1000 - 0x9FC00 (salta prima pagina e area BIOS) */
        pmm_mark_free(0x00001000, 0x9F000);
        /* RAM estesa: 0x100000 + info->mem_upper * 1024 */
        if (info->mem_upper > 0) {
            pmm_mark_free(0x00100000, (uint32_t)info->mem_upper * 1024);
        }
    }

    /* -------------------------------------------------------------------------
     * Passo 5: Re-marca come usate le regioni critiche
     * Anche se E820 le dichiarasse libere, noi le proteggiamo sempre.
     * ------------------------------------------------------------------------- */

    /* Prima pagina (0x0000-0x0FFF): IVT + BDA — non toccare */
    pmm_mark_used(0x00000000, PAGE_SIZE);

    /* BIOS area bassa (0x9FC00-0xFFFFF): EBDA, VGA, ROM */
    pmm_mark_used(0x0009FC00, 0x00060400);

    /* Kernel (da _kernel_start a fine bitmap) */
    {
        extern uint32_t _kernel_start;
        uint32_t kernel_start = (uint32_t)&_kernel_start;
        /* ! FINO ALLA FINE DEI RIFERIMENTI, non della bitmap. Sono contigui e
         * vanno protetti insieme: lasciare fuori l'array dei riferimenti
         * vorrebbe dire che il PMM lo puo' consegnare a un processo, e i
         * conteggi comincerebbero a cambiare da soli. */
        uint32_t riservato_fine = (uint32_t)g_rif + g_rif_size;
        pmm_mark_used(kernel_start, riservato_fine - kernel_start);
        klog(LOG_INFO, "PMM: kernel+bitmap+riferimenti protetti: 0x%08x - 0x%08x",
             kernel_start, riservato_fine);
    }

    /* Stage 2 ancora in memoria (0x0500-0x7FFF) */
    pmm_mark_used(0x00000500, 0x00007B00);

    /* Root dir FAT12 (0x7E00-0x9A00) e FAT buffer (0xA000-0xB200)
     * + DISK_BUFFER (0xB000-0xB200) usati da Stage 2.
     * FIX BUG #2/#3: FAT ora a 0xA000, BootInfo a 0xC000.
     * Riserviamo tutto il range 0x7E00-0xC0FF per sicurezza. */
    pmm_mark_used(0x00007E00, 0x00004300);

    /* BootInfo (0xC000-0xC0FF) — FIX BUG #3: era 0xB000 */
    pmm_mark_used(0x0000C000, PAGE_SIZE);

    /* La pagina della finestra di rimappatura fisica non deve essere
     * allocata a nessuno: il kernel ne riscrive la PTE a piacere, quindi
     * il suo contenuto non e' affidabile. Vedi paging_finestra_apri(). */
    pmm_mark_used(PAGING_FINESTRA_FISICA, PAGE_SIZE);

    g_last_alloc   = addr_to_page(0x00100000); /* Hint: inizia dalla RAM estesa */
    g_last_alloc_k = addr_to_page(0x00100000);

    /* -------------------------------------------------------------------------
     * Passo 6: Statistiche finali
     * ------------------------------------------------------------------------- */
    klog(LOG_INFO, "PMM: inizializzazione completata");
    klog(LOG_INFO, "PMM: pagine libere  : %u (%u MB)",
         g_free_pages, (g_free_pages * PAGE_SIZE) / (1024*1024));
    klog(LOG_INFO, "PMM: pagine usate   : %u (%u KB)",
         g_used_pages, (g_used_pages * PAGE_SIZE) / 1024);
    klog(LOG_INFO, "PMM: pagine totali  : %u (%u MB)",
         g_total_pages, (g_total_pages * PAGE_SIZE) / (1024*1024));
}

/* =============================================================================
 * pmm_alloc_page — Alloca una singola pagina fisica (4KB)
 *
 * Algoritmo: next-fit con hint g_last_alloc per velocità.
 * Cerca la prima pagina libera a partire dall'ultima allocazione.
 *
 * ! SERVE LA FASCIA KERNEL PER ULTIMA, e non e' un dettaglio di
 * efficienza. La fascia (0 → USER_SPACE_BASE) e' l'unica memoria che il
 * kernel puo' rileggere mentre gira un processo, ma nessuno ne vieta l'uso
 * a una pagina utente: senza questa preferenza, il primo programma che
 * chiede memoria la consuma dal basso, si prende tutta la fascia e poi il
 * kernel non trova piu' una pagina per una page table. Successo apparente
 * del programma, fallimento del kernel poco dopo — con 437 MB liberi.
 * (Visto: 72 MB di heap su una macchina da 512 MB.)
 *
 * Su una macchina piccola, dove la RAM sta tutta dentro la fascia, il
 * ripiego e' immediato e il comportamento torna quello di prima: e' giusto
 * cosi', li' non c'e' niente da preservare.
 *
 * Ritorna: indirizzo fisico della pagina (allineato a 4KB)
 *          0 se non ci sono pagine libere (OUT OF MEMORY)
 * ============================================================================= */
uint32_t pmm_alloc_page(void)
{
    uint32_t i;
    uint32_t start;
    uint32_t fascia = pmm_limite_kernel();   /* prima pagina fuori fascia */

    if (g_free_pages == 0) {
        klog(LOG_ERROR, "PMM: OUT OF MEMORY! Nessuna pagina fisica libera.");
        return 0;
    }

    /* Cerca dal hint g_last_alloc in poi (next-fit), mai sotto la fascia */
    start = (g_last_alloc < fascia) ? fascia : g_last_alloc;

    for (i = start; i < g_total_pages; i++) {
        if (!BITMAP_TEST(i)) {
            BITMAP_SET(i);
            g_free_pages--;
            g_used_pages++;
            g_last_alloc = i + 1;
            return page_to_addr(i);
        }
    }

    /* Wrap-around, sempre sopra la fascia */
    for (i = fascia; i < start; i++) {
        if (!BITMAP_TEST(i)) {
            BITMAP_SET(i);
            g_free_pages--;
            g_used_pages++;
            g_last_alloc = i + 1;
            return page_to_addr(i);
        }
    }

    /* Ultima risorsa: la fascia kernel. Meglio consegnarla che rispondere
     * "memoria esaurita" mentre c'e' — su una macchina piccola e' l'unica
     * memoria che esista. */
    for (i = 0; i < fascia; i++) {
        if (!BITMAP_TEST(i)) {
            BITMAP_SET(i);
            g_free_pages--;
            g_used_pages++;
            return page_to_addr(i);
        }
    }

    klog(LOG_ERROR, "PMM: OUT OF MEMORY (bitmap corrotta?)");
    return 0;
}

/* =============================================================================
 * LA FASCIA KERNEL — pmm_alloc_page_kernel / pmm_alloc_pages_kernel
 *
 * PERCHE' ESISTONO. La page directory di un processo copia dalla PD del
 * kernel solo le PDE che stanno SOTTO USER_SPACE_BASE
 * (paging_create_directory). Tutto cio' che il kernel raggiunge
 * dereferenziando un indirizzo FISICO — lo heap di kmalloc, gli stack
 * kernel dei processi, le page directory e le page table — deve quindi
 * vivere sotto quella soglia, altrimenti diventa invisibile non appena e'
 * caricato il CR3 di un processo.
 *
 * Il guasto, quando capita, non assomiglia alla causa: uno stack kernel
 * allocato sopra la soglia manda in page fault la CPU mentre commuta lo
 * stack all'ingresso di un interrupt — cioe' prima che qualunque
 * istruzione del kernel possa dire cosa e' successo.
 *
 * Le pagine dell'UTENTE (segmenti ELF, sbrk, mmap, stack ring3) non hanno
 * questo vincolo: il kernel le tocca attraverso la finestra di
 * rimappatura (vedi paging_finestra_apri), non al loro indirizzo fisico.
 * Per quelle si continua a usare pmm_alloc_page(), che pesca da tutta la
 * RAM — ed e' importante che sia cosi', se no la fascia kernel diventa il
 * tetto della memoria di sistema.
 * ============================================================================= */

/* Ultima pagina + 1 della fascia kernel. Se la RAM e' meno di
 * USER_SPACE_BASE, il limite e' la RAM. */
static uint32_t pmm_limite_kernel(void)
{
    uint32_t limite = addr_to_page(USER_SPACE_BASE);
    return (limite > g_total_pages) ? g_total_pages : limite;
}

uint32_t pmm_alloc_page_kernel(void)
{
    uint32_t limite = pmm_limite_kernel();
    uint32_t i;

    for (i = g_last_alloc_k; i < limite; i++) {
        if (!BITMAP_TEST(i)) {
            BITMAP_SET(i);
            g_free_pages--;
            g_used_pages++;
            g_last_alloc_k = i + 1;
            return page_to_addr(i);
        }
    }

    /* Wrap-around dentro la sola fascia kernel */
    for (i = 0; i < g_last_alloc_k && i < limite; i++) {
        if (!BITMAP_TEST(i)) {
            BITMAP_SET(i);
            g_free_pages--;
            g_used_pages++;
            g_last_alloc_k = i + 1;
            return page_to_addr(i);
        }
    }

    /* Messaggio esplicito: "la RAM e' finita" e "la fascia kernel e'
     * finita" sono due guasti diversi, e confonderli manda a cercare
     * memoria che c'e'. */
    klog(LOG_ERROR, "PMM: fascia kernel esaurita (0x0-0x%08x): %u pagine "
         "libere altrove, ma il kernel non potrebbe rileggerle",
         USER_SPACE_BASE, g_free_pages);
    return 0;
}

uint32_t pmm_alloc_pages_kernel(uint32_t count)
{
    uint32_t limite = pmm_limite_kernel();
    uint32_t i, j, consecutive;

    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_page_kernel();
    if (limite < count) return 0;

    for (i = 0; i <= limite - count; i++) {
        if (BITMAP_TEST(i)) continue;

        consecutive = 1;
        for (j = i + 1; j < i + count && j < limite; j++) {
            if (BITMAP_TEST(j)) break;
            consecutive++;
        }

        if (consecutive == count) {
            for (j = i; j < i + count; j++) {
                BITMAP_SET(j);
                g_free_pages--;
                g_used_pages++;
            }
            return page_to_addr(i);
        }

        i = j;
    }

    klog(LOG_ERROR, "PMM: %u pagine contigue non disponibili nella fascia "
         "kernel (0x0-0x%08x)", count, USER_SPACE_BASE);
    return 0;
}

/* =============================================================================
 * pmm_alloc_pages — Alloca N pagine fisiche CONTIGUE
 *
 * Necessario per strutture che richiedono memoria contigua (es. page tables,
 * buffer DMA).
 *
 * Algoritmo: first-fit lineare (meno efficiente ma semplice e corretto)
 *
 * Ritorna: indirizzo fisico prima pagina del blocco, 0 se fallito
 * ============================================================================= */
uint32_t pmm_alloc_pages(uint32_t count)
{
    uint32_t i, j;
    uint32_t consecutive;

    if (count == 0) return 0;
    if (count == 1) return pmm_alloc_page();

    if (g_free_pages < count) {
        klog(LOG_ERROR, "PMM: richiesta %u pagine contigue, disponibili solo %u",
             count, g_free_pages);
        return 0;
    }

    /* Cerca 'count' pagine libere consecutive */
    for (i = 0; i <= g_total_pages - count; i++) {
        if (BITMAP_TEST(i)) continue; /* Pagina usata, skip */

        /* Controlla che le successive 'count-1' siano libere */
        consecutive = 1;
        for (j = i + 1; j < i + count && j < g_total_pages; j++) {
            if (BITMAP_TEST(j)) break;
            consecutive++;
        }

        if (consecutive == count) {
            /* Trovato blocco contiguo: marca tutto come usato */
            for (j = i; j < i + count; j++) {
                BITMAP_SET(j);
                g_free_pages--;
                g_used_pages++;
            }
            klog(LOG_DEBUG, "PMM: allocate %u pagine contigue a 0x%08x",
                 count, page_to_addr(i));
            return page_to_addr(i);
        }

        /* Salta avanti al primo set dopo il blocco fallito */
        i = j;
    }

    klog(LOG_ERROR, "PMM: impossibile trovare %u pagine contigue", count);
    return 0;
}

/* =============================================================================
 * pmm_free_page — Libera una singola pagina fisica
 *
 * addr: indirizzo fisico della pagina (DEVE essere allineato a 4KB)
 * ============================================================================= */
void pmm_free_page(uint32_t addr)
{
    uint32_t page;

    if (addr == 0) {
        klog(LOG_WARN, "PMM: tentativo di liberare pagina 0 (NULL)");
        return;
    }

    if (addr % PAGE_SIZE != 0) {
        klog(LOG_ERROR, "PMM: indirizzo non allineato a pagina: 0x%08x", addr);
        return;
    }

    page = addr_to_page(addr);

    if (page >= g_total_pages) {
        klog(LOG_ERROR, "PMM: pagina fuori range: %u (max %u)", page, g_total_pages);
        return;
    }

    if (!BITMAP_TEST(page)) {
        klog(LOG_WARN, "PMM: doppia liberazione pagina 0x%08x (gia' libera)", addr);
        return;
    }

    /* ! LA PAGINA CONDIVISA NON TORNA AL SISTEMA: PERDE UN PROPRIETARIO.
     *
     * Qui passa OGNI liberazione — munmap, la morte di un processo, un errore
     * a meta' di elf_load — e nessuna di quelle sa che la condivisione esiste.
     * E' il punto in cui si spende il conteggio, e per questo il conteggio sta
     * nel PMM: una sola riga da scrivere bene invece di dieci da ricordare. */
    if (g_rif != NULL && g_rif[page] > 0) {
        g_rif[page]--;
        return;
    }

    BITMAP_CLR(page);
    g_free_pages++;
    g_used_pages--;

    /* Aggiorna hint per next-fit */
    if (page < g_last_alloc) g_last_alloc = page;
}

/* =============================================================================
 * pmm_ref_inc / pmm_ref_count — vedi kernel/include/pmm.h
 * ============================================================================= */
int pmm_ref_inc(uint32_t addr)
{
    uint32_t page;

    if (g_rif == NULL) return -1;
    if (addr % PAGE_SIZE != 0) {
        klog(LOG_ERROR, "PMM: ref_inc su indirizzo non allineato: 0x%08x", addr);
        return -1;
    }

    page = addr_to_page(addr);

    /* ! FUORI DALLA RAM NON SI CONTA NIENTE, e non e' un errore da nascondere:
     * una finestra MMIO mappata in un driver sta sopra la RAM, non e' mai
     * stata allocata da noi e non va mai liberata. Chi prova a condividerla
     * sta chiedendo la cosa sbagliata. */
    if (page >= g_total_pages) {
        klog(LOG_ERROR, "PMM: ref_inc su pagina fuori RAM: 0x%08x", addr);
        return -1;
    }

    if (!BITMAP_TEST(page)) {
        klog(LOG_ERROR, "PMM: ref_inc su pagina LIBERA 0x%08x - "
             "qualcuno la condivide senza possederla", addr);
        return -1;
    }

    /* ! IL TETTO E' UN RIFIUTO, NON UNA SATURAZIONE. Un conteggio che si ferma
     * a 255 e poi non sale piu' fa liberare la pagina al 255esimo che se ne
     * va, mentre gli altri ce l'hanno ancora mappata: si perde la memoria di
     * quanti sono, ed e' il guasto peggiore che questo file possa produrre. */
    if (g_rif[page] >= RIF_MAX) {
        klog(LOG_ERROR, "PMM: pagina 0x%08x gia' condivisa da %u, tetto raggiunto",
             addr, RIF_MAX + 1u);
        return -1;
    }

    g_rif[page]++;
    return 0;
}

uint32_t pmm_ref_count(uint32_t addr)
{
    uint32_t page;

    if (g_rif == NULL) return 0;
    page = addr_to_page(addr);
    if (page >= g_total_pages)   return 0;
    if (!BITMAP_TEST(page))      return 0;

    return 1u + (uint32_t)g_rif[page];
}

/* =============================================================================
 * pmm_free_pages — Libera N pagine fisiche contigue
 * ============================================================================= */
void pmm_free_pages(uint32_t addr, uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; i++) {
        pmm_free_page(addr + i * PAGE_SIZE);
    }
}

/* =============================================================================
 * pmm_get_free_pages — Ritorna il numero di pagine fisiche libere
 * ============================================================================= */
uint32_t pmm_get_free_pages(void)  { return g_free_pages; }
uint32_t pmm_get_used_pages(void)  { return g_used_pages; }
uint32_t pmm_get_total_pages(void) { return g_total_pages; }

/* =============================================================================
 * pmm_is_page_free — Controlla se una pagina è libera
 * ============================================================================= */
int pmm_is_page_free(uint32_t addr)
{
    uint32_t page = addr_to_page(addr);
    if (page >= g_total_pages) return 0;
    return !BITMAP_TEST(page);
}

/* =============================================================================
 * pmm_region_stat — pagine totali e libere in un intervallo fisico
 * Vedi pmm.h per il razionale.
 * ============================================================================= */
void pmm_region_stat(uint32_t base, uint32_t len,
                     uint32_t *out_total, uint32_t *out_free)
{
    uint32_t first = addr_to_page(base);
    uint32_t last  = addr_to_page(base + len - 1);   /* inclusivo */
    uint32_t page;
    uint32_t tot = 0, libere = 0;

    /* Fascia vuota, oppure PMM non ancora inizializzato: zero pagine.
     * Tronca inoltre alla RAM realmente presente, cosi' una fascia oltre
     * la memoria installata risulta vuota invece che piena di pagine
     * inesistenti. */
    if (len == 0 || g_total_pages == 0) {
        if (out_total) *out_total = 0;
        if (out_free)  *out_free  = 0;
        return;
    }

    if (last >= g_total_pages) {
        last = g_total_pages - 1;
    }

    for (page = first; page <= last; page++) {
        tot++;
        if (!BITMAP_TEST(page)) libere++;
    }

    if (out_total) *out_total = tot;
    if (out_free)  *out_free  = libere;
}

/* =============================================================================
 * pmm_dump — Stampa statistiche e primi N blocchi liberi (debug)
 * ============================================================================= */
void pmm_dump(void)
{
    uint32_t i;
    uint32_t runs       = 0;
    uint32_t run_start  = 0;
    int      in_run     = 0;
    uint32_t shown      = 0;

    klog(LOG_DEBUG, "PMM dump:");
    klog(LOG_DEBUG, "  Totale : %u pagine (%u MB)",
         g_total_pages, (g_total_pages * PAGE_SIZE) / (1024*1024));
    klog(LOG_DEBUG, "  Libere : %u pagine (%u KB)",
         g_free_pages, (g_free_pages * PAGE_SIZE) / 1024);
    klog(LOG_DEBUG, "  Usate  : %u pagine (%u KB)",
         g_used_pages, (g_used_pages * PAGE_SIZE) / 1024);
    klog(LOG_DEBUG, "  Bitmap : 0x%08x (%u byte)", (uint32_t)g_bitmap, g_bitmap_size);

    /* Stampa i primi 8 blocchi liberi contigui */
    klog(LOG_DEBUG, "  Blocchi liberi (primi 8):");
    for (i = 0; i <= g_total_pages && shown < 8; i++) {
        int free = (i < g_total_pages) && !BITMAP_TEST(i);
        if (free && !in_run) {
            run_start = i;
            in_run    = 1;
        } else if (!free && in_run) {
            klog(LOG_DEBUG, "    [%u] 0x%08x - 0x%08x (%u pagine)",
                 runs,
                 page_to_addr(run_start),
                 page_to_addr(i) - 1,
                 i - run_start);
            runs++;
            shown++;
            in_run = 0;
        }
    }
}
