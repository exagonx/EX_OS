/* =============================================================================
 * kernel/mm/kmalloc.c
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
#include "kmalloc.h"

/* =============================================================================
 * Costanti
 * ============================================================================= */
#define HEAP_MAGIC      0xDEADBEEF  /* Firma blocco valido */
#define HEAP_MIN_SIZE   16          /* Dimensione minima blocco payload */
#define HEAP_ALIGN      8           /* Allineamento allocazioni (8 byte) */

#define BLOCK_FREE      0x1         /* Flag: blocco libero */
#define BLOCK_USED      0x0         /* Flag: blocco usato */

/* =============================================================================
 * Header/Footer di ogni blocco heap
 * ============================================================================= */
typedef struct KHeapBlock {
    uint32_t            size;       /* Dimensione payload in byte (senza header) */
    uint32_t            magic;      /* 0xDEADBEEF */
    uint32_t            flags;      /* BLOCK_FREE o BLOCK_USED */
    struct KHeapBlock  *prev;       /* Blocco precedente nella free list */
    struct KHeapBlock  *next;       /* Blocco successivo nella free list */
} KHeapBlock;

#define BLOCK_HEADER_SIZE   sizeof(KHeapBlock)

/* =============================================================================
 * Stato heap
 * ============================================================================= */
static KHeapBlock *g_heap_start = NULL;     /* Primo blocco heap */
static KHeapBlock *g_heap_end   = NULL;     /* Puntatore fine heap corrente */
static KHeapBlock *g_free_list  = NULL;     /* Testa della free list */
static uint32_t    g_heap_base  = 0;        /* Indirizzo fisico base heap */
static uint32_t    g_heap_size  = 0;        /* Dimensione heap corrente in byte */
static uint32_t    g_alloc_count = 0;       /* Totale allocazioni */
static uint32_t    g_free_count  = 0;       /* Totale liberazioni */

/* =============================================================================
 * Funzioni helper
 * ============================================================================= */

/* Allineamento a HEAP_ALIGN (8 byte) */
static inline uint32_t heap_align(uint32_t size)
{
    return ALIGN_UP(size, HEAP_ALIGN);
}

/* Verifica integrità blocco */
static inline int block_valid(KHeapBlock *b)
{
    return (b != NULL && b->magic == HEAP_MAGIC);
}

/* Blocco successivo in memoria (aritmetica puntatori) */
static inline KHeapBlock *block_next_phys(KHeapBlock *b)
{
    return (KHeapBlock *)((uint8_t *)b + BLOCK_HEADER_SIZE + b->size);
}

/* Rimuovi blocco dalla free list */
static void free_list_remove(KHeapBlock *b)
{
    if (b->prev) b->prev->next = b->next;
    else         g_free_list   = b->next;
    if (b->next) b->next->prev = b->prev;
    b->prev = NULL;
    b->next = NULL;
}

/* Aggiungi blocco in testa alla free list */
static void free_list_add(KHeapBlock *b)
{
    b->prev = NULL;
    b->next = g_free_list;
    if (g_free_list) g_free_list->prev = b;
    g_free_list = b;
}

/* =============================================================================
 * heap_expand — Aggiunge pagine al heap dal PMM
 *
 * Richiede 'pages' pagine fisiche contigue e le aggiunge all'heap.
 * Ritorna il puntatore al nuovo blocco libero, NULL se OOM.
 * ============================================================================= */
static KHeapBlock *heap_expand(uint32_t pages)
{
    uint32_t     phys;
    uint32_t     size_bytes;
    KHeapBlock  *new_block;

    /* Fascia kernel: i blocchi dello heap si usano al loro indirizzo
     * fisico (new_block qui sotto), quindi devono restare raggiungibili
     * anche quando gira un processo con la propria page directory. */
    phys = pmm_alloc_pages_kernel(pages);
    if (phys == 0) {
        klog(LOG_ERROR, "KMALLOC: PMM OOM durante espansione heap (%u pagine)", pages);
        return NULL;
    }

    size_bytes = pages * PAGE_SIZE;
    new_block  = (KHeapBlock *)phys;

    new_block->size  = size_bytes - BLOCK_HEADER_SIZE;
    new_block->magic = HEAP_MAGIC;
    new_block->flags = BLOCK_FREE;
    new_block->prev  = NULL;
    new_block->next  = NULL;

    g_heap_size += size_bytes;
    g_heap_end   = new_block;

    klog(LOG_DEBUG, "KMALLOC: heap espanso di %u pagine a 0x%08x (tot: %u KB)",
         pages, phys, g_heap_size / 1024);

    return new_block;
}

/* =============================================================================
 * kmalloc_init — Inizializza l'heap kernel
 *
 * L'heap parte subito dopo la bitmap PMM (già calcolata da pmm_init).
 * Alloca le prime pagine iniziali.
 * ============================================================================= */
void kmalloc_init(void)
{
    uint32_t    heap_pages_initial = 16;    /* 64KB iniziali */
    KHeapBlock *first_block;

    klog(LOG_INFO, "KMALLOC: inizializzazione heap kernel...");

    /* Calcola indirizzo base heap: subito dopo la fine del kernel + bitmap PMM
     * La bitmap occupa (total_pages / 8) byte dopo _kernel_end.
     * Usiamo un indirizzo fisso noto: KERNEL_HEAP_BASE = 0x400000 (4MB)
     * Questo è sicuro perché il kernel+bitmap stanno abbondantemente sotto 4MB. */
    g_heap_base = KERNEL_HEAP_BASE;
    g_heap_size = 0;
    g_free_list = NULL;

    klog(LOG_INFO, "KMALLOC: heap base = 0x%08x", g_heap_base);

    /* Alloca le prime pagine */
    first_block = heap_expand(heap_pages_initial);
    if (first_block == NULL) {
        kpanic("KMALLOC: impossibile inizializzare l'heap kernel!");
    }

    g_heap_start = first_block;
    free_list_add(first_block);

    klog(LOG_INFO, "KMALLOC: heap inizializzato (%u KB iniziali)",
         (heap_pages_initial * PAGE_SIZE) / 1024);
    klog(LOG_INFO, "KMALLOC: blocco iniziale a 0x%08x (%u byte payload)",
         (uint32_t)first_block, first_block->size);
}

/* =============================================================================
 * kmalloc — Alloca 'size' byte nell'heap kernel
 *
 * Algoritmo first-fit sulla free list.
 * Se il blocco trovato è abbastanza grande, lo spezza in due:
 *   - Blocco allocato (size richiesta)
 *   - Blocco libero residuo (se >= HEAP_MIN_SIZE)
 *
 * Ritorna: puntatore al payload, NULL se OOM
 * ============================================================================= */
void *kmalloc(size_t size)
{
    KHeapBlock *block;
    uint32_t    aligned_size;

    if (size == 0) return NULL;

    /* Allinea la dimensione richiesta */
    aligned_size = heap_align((uint32_t)size);
    if (aligned_size < HEAP_MIN_SIZE) aligned_size = HEAP_MIN_SIZE;

    /* Cerca nella free list un blocco abbastanza grande (first-fit) */
    block = g_free_list;
    while (block != NULL) {
        if (!block_valid(block)) {
            kpanic("KMALLOC: heap corrotto! Magic errato a 0x%08x", (uint32_t)block);
        }

        if (block->flags == BLOCK_FREE && block->size >= aligned_size) {
            break;  /* Trovato */
        }
        block = block->next;
    }

    /* Nessun blocco libero abbastanza grande: espandi heap */
    if (block == NULL) {
        uint32_t pages_needed = ALIGN_UP(aligned_size + BLOCK_HEADER_SIZE,
                                          PAGE_SIZE) / PAGE_SIZE;
        if (pages_needed < 4) pages_needed = 4; /* Minimo 4 pagine per volta */

        block = heap_expand(pages_needed);
        if (block == NULL) {
            klog(LOG_ERROR, "KMALLOC: OOM! Richiesta %u byte", size);
            return NULL;
        }
        free_list_add(block);
    }

    /* Rimuovi dalla free list */
    free_list_remove(block);

    /* Split: se il blocco è molto più grande del necessario, spezzalo */
    uint32_t leftover = block->size - aligned_size;
    if (leftover >= BLOCK_HEADER_SIZE + HEAP_MIN_SIZE) {
        /* Crea blocco residuo */
        KHeapBlock *residual = (KHeapBlock *)((uint8_t *)block
                                              + BLOCK_HEADER_SIZE
                                              + aligned_size);
        residual->size  = leftover - BLOCK_HEADER_SIZE;
        residual->magic = HEAP_MAGIC;
        residual->flags = BLOCK_FREE;
        residual->prev  = NULL;
        residual->next  = NULL;
        free_list_add(residual);

        block->size = aligned_size;
    }

    block->flags = BLOCK_USED;
    g_alloc_count++;

    klog(LOG_DEBUG, "KMALLOC: allocati %u byte a 0x%08x (richiesti: %u)",
         block->size, (uint32_t)(block + 1), size);

    /* Ritorna puntatore al payload (subito dopo l'header) */
    return (void *)(block + 1);
}

/* =============================================================================
 * kmalloc_aligned — Alloca 'size' byte allineati a 'alignment' byte
 *
 * Utile per strutture che richiedono allineamento specifico
 * (es. Page Directory/Table a 4KB, buffer DMA a 16 byte).
 * ============================================================================= */
void *kmalloc_aligned(size_t size, size_t alignment)
{
    /* Alloca con spazio extra per l'allineamento + puntatore al blocco reale */
    uint8_t  *raw    = kmalloc(size + alignment + sizeof(void *));
    uint8_t  *aligned;
    void    **ptrref;

    if (raw == NULL) return NULL;

    /* Calcola indirizzo allineato */
    aligned = (uint8_t *)ALIGN_UP((uint32_t)(raw + sizeof(void *)), alignment);

    /* Salva puntatore originale subito prima del blocco allineato */
    ptrref  = (void **)(aligned - sizeof(void *));
    *ptrref = raw;

    return aligned;
}

/* =============================================================================
 * kfree — Libera un blocco allocato con kmalloc
 * ============================================================================= */
void kfree(void *ptr)
{
    KHeapBlock *block;
    KHeapBlock *next_phys;

    if (ptr == NULL) return;

    /* Recupera header dal payload */
    block = (KHeapBlock *)ptr - 1;

    if (!block_valid(block)) {
        kpanic("KFREE: puntatore non valido o heap corrotto: 0x%08x", (uint32_t)ptr);
    }

    if (block->flags == BLOCK_FREE) {
        klog(LOG_WARN, "KFREE: doppia liberazione a 0x%08x!", (uint32_t)ptr);
        return;
    }

    block->flags = BLOCK_FREE;
    g_free_count++;

    klog(LOG_DEBUG, "KFREE: liberati %u byte da 0x%08x", block->size, (uint32_t)ptr);

    /* Coalescenza con il blocco successivo fisicamente adiacente (forward) */
    next_phys = block_next_phys(block);
    if (block_valid(next_phys) && next_phys->flags == BLOCK_FREE) {
        /* Unisci block e next_phys */
        free_list_remove(next_phys);
        block->size += BLOCK_HEADER_SIZE + next_phys->size;
        klog(LOG_DEBUG, "KMALLOC: coalescenza forward, nuovo size=%u", block->size);
    }

    /* Coalescenza con il blocco PRECEDENTE fisicamente adiacente (backward).
     * FIX Struct #3: senza questa coalescenza, sequenze alloc/free alternate
     * producono frammentazione crescente: blocchi liberi piccoli separati da
     * header, che kmalloc non riesce a soddisfare anche con molta RAM libera.
     *
     * La struttura KHeapBlock non ha un campo prev_phys (aggiungerne uno
     * richiederebbe modifiche al layout e ai punti di allocazione), quindi
     * troviamo il predecessore con una scansione lineare dal g_heap_start.
     * Costo O(n) per numero di blocchi nell'heap; accettabile dato che
     * kfree è chiamata raramente in bulk e l'heap kernel è piccolo (<1MB). */
    {
        KHeapBlock *scan = g_heap_start;
        KHeapBlock *prev_phys_blk = NULL;

        while (block_valid(scan) && scan < block) {
            KHeapBlock *nx = block_next_phys(scan);
            if (nx == block) {
                prev_phys_blk = scan;
                break;
            }
            scan = nx;
        }

        if (prev_phys_blk != NULL &&
            block_valid(prev_phys_blk) &&
            prev_phys_blk->flags == BLOCK_FREE) {

            /* Unisci prev_phys_blk con block (che ora include già next_phys) */
            free_list_remove(prev_phys_blk);
            prev_phys_blk->size += BLOCK_HEADER_SIZE + block->size;
            klog(LOG_DEBUG, "KMALLOC: coalescenza backward, nuovo size=%u",
                 prev_phys_blk->size);
            /* Da qui in poi il blocco da aggiungere alla free list è il predecessore */
            block = prev_phys_blk;
        }
    }

    /* Aggiungi alla free list */
    free_list_add(block);
}

/* =============================================================================
 * kfree_aligned — Libera un blocco allocato con kmalloc_aligned
 * ============================================================================= */
void kfree_aligned(void *ptr)
{
    if (ptr == NULL) return;
    /* Recupera il puntatore originale salvato da kmalloc_aligned */
    void *raw = *((void **)((uint8_t *)ptr - sizeof(void *)));
    kfree(raw);
}

/* =============================================================================
 * kmalloc_stats — Stampa statistiche dell'heap
 * ============================================================================= */
void kmalloc_stats(void)
{
    KHeapBlock *b;
    uint32_t free_bytes  = 0;
    uint32_t used_bytes  = 0;
    uint32_t free_blocks = 0;
    uint32_t used_blocks = 0;

    /* Scansione free list */
    b = g_free_list;
    while (b != NULL) {
        if (block_valid(b)) {
            free_bytes  += b->size;
            free_blocks++;
        }
        b = b->next;
    }

    used_bytes  = g_heap_size - free_bytes - (free_blocks + used_blocks) * BLOCK_HEADER_SIZE;
    (void)used_bytes;

    klog(LOG_INFO, "KMALLOC statistiche:");
    klog(LOG_INFO, "  Heap base   : 0x%08x", g_heap_base);
    klog(LOG_INFO, "  Heap size   : %u KB", g_heap_size / 1024);
    klog(LOG_INFO, "  Blocchi lib.: %u (%u byte)", free_blocks, free_bytes);
    klog(LOG_INFO, "  Allocazioni : %u totali", g_alloc_count);
    klog(LOG_INFO, "  Liberazioni : %u totali", g_free_count);
}
