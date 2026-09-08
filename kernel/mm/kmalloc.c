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

/* =============================================================================
 * IL CANARINO — LA PAROLA CHE STA IN CODA A OGNI BLOCCO USATO
 *
 * ! @DIF-PANIC LASCIAVA UNA DOMANDA APERTA, ed e' questa: l'intestazione rotta
 * che kfree trovava («dice di essere lungo N byte, ma la sua regione finisce
 * prima») l'aveva scritta la lettura fuori regione — chiusa il 7 settembre
 * 2026 — oppure QUALCUN ALTRO che scrive oltre il proprio blocco? La firma
 * rotta non lo dice: quando la si trova, chi l'ha rotta e' gia' andato via.
 *
 * Un canarino risponde. Subito dopo i byte CHIESTI si scrive una parola nota;
 * chi sconfina di uno la cancella, e alla liberazione — o al primo controllo —
 * si sa TRE cose invece di una: che qualcuno ha scritto oltre, quale blocco, e
 * SOPRATTUTTO chi l'aveva allocato (`chi` e' l'indirizzo di ritorno di chi ha
 * chiamato kmalloc, e si risolve con build/kernel.elf).
 *
 * ! E STA DOPO I BYTE CHIESTI, NON ALLA FINE DEL BLOCCO. Un'allocazione viene
 * arrotondata a otto byte, e chi scrive uno o due byte oltre cio' che ha
 * chiesto finisce dentro quel riempimento: alla fine del blocco non lo
 * vedrebbe nessuno, ed e' proprio il caso piu' comune.
 *
 * ! IL PREZZO E' QUATTRO BYTE PER ALLOCAZIONE, e vanno detti: su un avvio
 * normale (34 blocchi vivi) sono 136 byte di heap. Il difetto che cercano si
 * vede una volta su quattordici e spegne la macchina: e' comprato bene.
 * ============================================================================= */
#define HEAP_CANARINO   0xC0DA5EED
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
    uint32_t            chiesti;    /* byte CHIESTI da chi ha allocato: dice
                                       dove sta il canarino, e di quanto il
                                       blocco e' piu' grande della richiesta */
    uint32_t            chi;        /* indirizzo di ritorno di chi ha chiamato
                                       kmalloc: e' il nome di chi sconfina */
    struct KHeapBlock  *prev;       /* Blocco precedente nella free list */
    struct KHeapBlock  *next;       /* Blocco successivo nella free list */
} KHeapBlock;

#define BLOCK_HEADER_SIZE   sizeof(KHeapBlock)

/* =============================================================================
 * LE REGIONI: LO HEAP NON E' UN TRATTO SOLO
 *
 * ! E FINO AL 7 SETTEMBRE 2026 IL CODICE CREDEVA DI SI'. heap_expand() chiede
 * le pagine a pmm_alloc_pages_kernel(), che le cerca dove le trova dentro
 * 0..USER_SPACE_BASE: due espansioni danno quasi sempre due tratti LONTANI,
 * con in mezzo roba che heap non e' — stack di kernel, page directory, page
 * table, i buffer dei driver.
 *
 * I blocchi si incastrano uno dietro l'altro DENTRO un tratto, e basta
 * `b + header + b->size` per passare al successivo. Ma il tratto finisce, e
 * quel calcolo non lo sa: sull'ULTIMO blocco di una regione rende un
 * indirizzo che con lo heap non c'entra niente. kfree lo dereferenziava per
 * leggerne la firma — due volte, in avanti e all'indietro — e da li'
 * succedeva una delle due:
 *
 *   - l'indirizzo e' oltre la RAM, che il kernel non mappa (paging_init
 *     mappa in identita' fino a pmm_get_total_pages(), non oltre), e allora
 *     e' un PAGE FAULT IN RING0 dentro kfree: il kernel muore mentre libera
 *     memoria, che e' il posto meno somigliante alla causa;
 *
 *   - l'indirizzo e' dentro la RAM e per caso ci trova 0xDEADBEEF. Allora la
 *     firma "vale", e kfree FONDE un blocco dello heap con memoria che heap
 *     non e': `block->size` diventa una misura inventata, e il guasto non si
 *     vede adesso — si vede alla kfree DOPO, che con quella misura calcola un
 *     indirizzo qualunque e muore li'. E' esattamente la firma registrata in
 *     in_lavorazione.txt (@DIF-PANIC): «kfree ha ricevuto un blocco la cui
 *     INTESTAZIONE dice una misura sballata».
 *
 * La cura e' sapere DOVE FINISCE il tratto. Ogni regione tiene in testa a se'
 * stessa due parole — dove finisce, e la prossima regione — e kfree non
 * dereferenzia mai un indirizzo che sta fuori dalla propria.
 *
 * ! LA LISTA STA DENTRO LE REGIONI, e non in un vettore con un tetto, per la
 * ragione di sempre: un tetto o e' troppo alto (memoria sprecata) o e' troppo
 * basso il giorno che serve, e quel giorno l'errore torna a essere «lo heap
 * non si espande» senza dire perche'. Otto byte per regione non li sente
 * nessuno.
 * ============================================================================= */
typedef struct KHeapRegion {
    struct KHeapRegion *next;   /* la regione successiva, in ordine di nascita */
    uint32_t            fine;   /* primo indirizzo FUORI dalla regione */
} KHeapRegion;

#define REGION_HEADER_SIZE  sizeof(KHeapRegion)

/* =============================================================================
 * Stato heap
 * ============================================================================= */
static KHeapBlock *g_heap_start = NULL;     /* Primo blocco heap */
static KHeapBlock *g_heap_end   = NULL;     /* Puntatore fine heap corrente */
static KHeapBlock *g_free_list  = NULL;     /* Testa della free list */
static KHeapRegion *g_regioni   = NULL;     /* Testa della lista delle regioni */
static KHeapRegion *g_prima_regione = NULL; /* La prima nata: quella di g_heap_start */
static uint32_t    g_heap_base  = 0;        /* Indirizzo fisico base heap */
static uint32_t    g_heap_size  = 0;        /* Dimensione heap corrente in byte */
static uint32_t    g_alloc_count = 0;       /* Totale allocazioni */
static uint32_t    g_free_count  = 0;       /* Totale liberazioni */

/* Quante volte il confine di una regione ha fermato una lettura che il codice
 * di prima del 7 settembre 2026 avrebbe fatto fuori dallo heap.
 *
 * ! SERVE A MISURARE, NON A DECORARE. «Puo' succedere» e «succede 1400 volte
 * in un avvio» sono due frasi diverse, e finche' non si conta la seconda non
 * si sa quale delle due e' vera. kmalloc_stats le stampa. */
static uint32_t    g_stop_avanti   = 0;
static uint32_t    g_stop_indietro = 0;

/* Quante intestazioni dicevano una misura che nella loro regione non ci sta.
 * ! QUESTO NON E' UN CONFINE RISPETTATO, E' UN GUASTO. Un blocco che si
 * dichiara piu' lungo della regione che lo contiene ha l'intestazione
 * sovrascritta: prima del 7 settembre 2026 il kernel ci moriva sopra
 * (@DIF-PANIC), adesso sopravvive — ma il numero va guardato, perche' chi ha
 * scritto quella misura sta ancora scrivendo dove non deve. */
static uint32_t    g_intestazioni_rotte = 0;
static uint32_t    g_canarini_rotti     = 0;   /* chi ha scritto oltre la fine */

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
/* Dove sta il canarino di un blocco usato: subito dopo i byte chiesti. */
static inline uint32_t *canarino_di(KHeapBlock *b)
{
    return (uint32_t *)((uint8_t *)(b + 1) + b->chiesti);
}

/* Vero se il blocco ha spazio per il canarino. kmalloc lo garantisce sempre
 * (chiede size+4), ma un blocco piu' vecchio del canarino, o un'intestazione
 * gia' rotta, potrebbero non averlo: in quel caso non si legge niente. */
static inline int canarino_c_e(KHeapBlock *b)
{
    return (b->flags == BLOCK_USED) && (b->chiesti + 4 <= b->size);
}

static inline void canarino_scrivi(KHeapBlock *b)
{
    if (canarino_c_e(b)) *canarino_di(b) = HEAP_CANARINO;
}

/* Rende 0 se il canarino e' intero (o non c'e'), 1 se qualcuno l'ha scritto
 * sopra — e in quel caso LO DICE, col nome di chi aveva allocato. */
static int canarino_rotto(KHeapBlock *b, const char *quando)
{
    if (!canarino_c_e(b))              return 0;
    if (*canarino_di(b) == HEAP_CANARINO) return 0;

    g_canarini_rotti++;
    klog(LOG_ERROR, "KHEAP: %s — il blocco 0x%08x (chiesti %u byte, blocco "
         "%u) e' stato scritto OLTRE LA FINE: al posto del canarino c'e' "
         "0x%08x. L'aveva allocato chi sta a 0x%08x (risolvilo con "
         "build/kernel.elf).", quando, (uint32_t)(b + 1), b->chiesti, b->size,
         *canarino_di(b), b->chi);
    return 1;
}

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

/* La regione che contiene `p`, oppure NULL se `p` nello heap non c'e'.
 *
 * La lista e' corta — un avvio ne fa una manciata, e le regioni attaccate si
 * fondono qui sotto in heap_expand — e la si percorre solo in kfree, che gia'
 * paga una scansione ben piu' lunga. */
static KHeapRegion *regione_di(const void *p)
{
    KHeapRegion *r;

    for (r = g_regioni; r != NULL; r = r->next)
        if ((uint32_t)p >= (uint32_t)r && (uint32_t)p < r->fine)
            return r;

    return NULL;
}

/* Il primo blocco di una regione: subito dopo le sue due parole di testa.
 * ! ANCHE PER UNA REGIONE ALLUNGATA: quando due tratti si toccano non nasce
 * una regione nuova, si sposta `fine` — quindi la testa resta una sola e i
 * blocchi restano una catena sola dall'inizio alla fine. */
static KHeapBlock *regione_primo(KHeapRegion *r)
{
    return (KHeapBlock *)((uint8_t *)r + REGION_HEADER_SIZE);
}

/* Vero se a `b` ci sta dentro un'intestazione di blocco, tutta, prima della
 * fine di `r`. E' la domanda che va fatta PRIMA di leggere b->magic. */
static inline int dentro_regione(KHeapRegion *r, KHeapBlock *b)
{
    return ((uint32_t)b >= (uint32_t)regione_primo(r) &&
            (uint32_t)b + BLOCK_HEADER_SIZE <= r->fine);
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

    /* Il tratto nuovo attacca esattamente dove ne finisce uno gia' noto?
     * Allora non e' una regione nuova: e' quella che si allunga.
     *
     * ! E NON E' UN'OTTIMIZZAZIONE. Due tratti attaccati tenuti per due
     * regioni distinte sarebbero due mondi separati, e i loro blocchi non si
     * fonderebbero MAI: lo heap si frammenterebbe lungo un confine che nella
     * memoria non esiste. Qui invece l'ultimo blocco del tratto di prima e il
     * primo del tratto nuovo sono adiacenti davvero, e la coalescenza
     * all'indietro di kfree li unisce come qualunque altra coppia. */
    {
        KHeapRegion *r;

        for (r = g_regioni; r != NULL; r = r->next) {
            if (r->fine == phys) {
                r->fine    = phys + size_bytes;
                new_block  = (KHeapBlock *)phys;
                new_block->size  = size_bytes - BLOCK_HEADER_SIZE;
                new_block->magic = HEAP_MAGIC;
                new_block->flags = BLOCK_FREE;
                new_block->prev  = NULL;
                new_block->next  = NULL;

                g_heap_size += size_bytes;
                g_heap_end   = new_block;

                klog(LOG_DEBUG, "KMALLOC: regione allungata a 0x%08x "
                     "(+%u pagine, fine 0x%08x)", (uint32_t)r, pages, r->fine);
                return new_block;
            }
        }
    }

    /* Regione nuova: le sue due parole di testa stanno nella regione stessa,
     * e i blocchi cominciano subito dopo. REGION_HEADER_SIZE e' 8, cioe' un
     * multiplo di HEAP_ALIGN: l'allineamento dei blocchi resta quello che
     * sarebbe stato senza. */
    {
        KHeapRegion *r = (KHeapRegion *)phys;

        r->fine   = phys + size_bytes;
        r->next   = g_regioni;
        g_regioni = r;
        if (g_prima_regione == NULL) g_prima_regione = r;

        new_block = regione_primo(r);
    }

    new_block->size  = size_bytes - REGION_HEADER_SIZE - BLOCK_HEADER_SIZE;
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

    /* ! g_heap_base NON E' DOVE STA LO HEAP, ED E' SEMPRE STATO COSI'. Qui
     * c'era scritto che l'heap parte a KERNEL_HEAP_BASE (4 MB) «subito dopo
     * il kernel + la bitmap»: non e' vero e non lo e' mai stato. Le pagine le
     * sceglie pmm_alloc_pages_kernel(), che cerca dove trova dentro la fascia
     * 0..USER_SPACE_BASE — e ogni espansione le cerca di nuovo, quindi lo
     * heap e' sparso. Il valore resta perche' kmalloc_stats lo stampa da
     * sempre, ma vale quanto un'etichetta: DOVE si e' davvero lo dicono le
     * regioni (vedi il commento in cima), e la riga «Regioni» delle
     * statistiche dice quante sono. */
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

    /* Allinea la dimensione richiesta, PIU' i quattro byte del canarino: e'
     * l'unico prezzo che si paga, e si paga qui. */
    aligned_size = heap_align((uint32_t)size + 4);
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

    block->flags   = BLOCK_USED;
    block->chiesti = (uint32_t)size;
    /* Chi ha chiamato kmalloc. E' l'unica cosa che, il giorno che il canarino
     * suona, dice da dove ripartire. */
    block->chi     = (uint32_t)(uintptr_t)__builtin_return_address(0);
    canarino_scrivi(block);

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
    KHeapBlock  *block;
    KHeapBlock  *next_phys;
    KHeapRegion *regione;

    if (ptr == NULL) return;

    /* Recupera header dal payload */
    block = (KHeapBlock *)ptr - 1;

    /* ! LA REGIONE PRIMA DELLA FIRMA, e non e' pignoleria: un puntatore che
     * nello heap non sta puo' avere per caso 0xDEADBEEF davanti, e allora
     * block_valid dice di si' e da li' in poi si lavora su memoria di
     * qualcun altro. Chiedendo prima la regione, un puntatore estraneo si
     * ferma qui con un messaggio che dice il suo indirizzo, invece di
     * spegnere la macchina tre kfree piu' tardi in un posto che non c'entra. */
    regione = regione_di(block);
    if (regione == NULL) {
        kpanic("KFREE: 0x%08x non sta in nessuna regione dello heap",
               (uint32_t)ptr);
    }

    if (!block_valid(block)) {
        kpanic("KFREE: puntatore non valido o heap corrotto: 0x%08x", (uint32_t)ptr);
    }

    if (block->flags == BLOCK_FREE) {
        klog(LOG_WARN, "KFREE: doppia liberazione a 0x%08x!", (uint32_t)ptr);
        return;
    }

    /* ! SI GUARDA IL CANARINO PRIMA DI LIBERARE, e non e' una formalita': da
     * qui in poi il blocco entra nella free list e i suoi byte diventano
     * `prev` e `next` di qualcun altro. Se qualcuno ci ha scritto oltre, e'
     * l'ultimo istante in cui si puo' ancora dire di CHI era il blocco. */
    canarino_rotto(block, "alla liberazione");

    block->flags = BLOCK_FREE;
    g_free_count++;

    klog(LOG_DEBUG, "KFREE: liberati %u byte da 0x%08x", block->size, (uint32_t)ptr);

    /* Coalescenza con il blocco successivo fisicamente adiacente (forward).
     *
     * ! SE `block` E' L'ULTIMO DELLA REGIONE, next_phys NON E' UN BLOCCO: e'
     * il primo indirizzo fuori. Leggerne la firma vuol dire leggere memoria
     * di qualcun altro, o oltre la RAM — vedi il commento sulle regioni in
     * cima. Qui ci si ferma, e si conta.
     *
     * ! E I DUE MODI DI STARE FUORI NON SONO LA STESSA COSA. Esattamente
     * sulla fine della regione vuol dire «questo blocco e' l'ultimo», ed e'
     * normale: si tace. OLTRE la fine vuol dire che `block->size` dice una
     * misura che nella regione non ci sta — cioe' l'intestazione e' ROTTA, e
     * qualcuno ha scritto dove non doveva. E' il caso di @DIF-PANIC: il
     * kernel moriva proprio su questa riga, a leggere la firma di un
     * indirizzo calcolato con una misura di 66 MB dentro una regione da 64
     * KB. Adesso non ci muore piu', ma tacere sarebbe peggio: la misura
     * sbagliata resta, ed e' l'unico indizio di chi l'ha scritta. */
    next_phys = block_next_phys(block);
    if (!dentro_regione(regione, next_phys)) {
        if ((uint32_t)next_phys > regione->fine) {
            klog(LOG_ERROR, "KFREE: 0x%08x dice di essere lungo %u byte, ma la "
                 "sua regione 0x%08x-0x%08x finisce prima: intestazione rotta",
                 (uint32_t)ptr, block->size, (uint32_t)regione, regione->fine);
            g_intestazioni_rotte++;
        }
        g_stop_avanti++;
        next_phys = NULL;
    }
    if (next_phys != NULL && block_valid(next_phys) && next_phys->flags == BLOCK_FREE) {
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
     * troviamo il predecessore con una scansione lineare.
     * Costo O(n) per numero di blocchi nella REGIONE; accettabile dato che
     * kfree è chiamata raramente in bulk e l'heap kernel è piccolo (<1MB).
     *
     * ! SI PARTE DALLA REGIONE DI `block`, NON DA g_heap_start. Partendo dal
     * primo blocco dello heap la scansione arrivava in fondo alla PRIMA
     * regione e proseguiva oltre — `nx` non e' mai uguale a `block` finche'
     * `block` sta altrove — leggendo la firma di quel che c'era dopo. Che a
     * volte e' un'altra regione (e allora, per puro caso, funzionava), a
     * volte e' memoria di qualcun altro, e a volte non e' mappato affatto.
     * Dentro una regione i blocchi si toccano tutti: partendo dal suo primo,
     * il predecessore o si trova o non c'e'. */
    {
        KHeapBlock *scan = regione_primo(regione);
        KHeapBlock *prev_phys_blk = NULL;

        /* ! QUESTA E' LA MISURA DEL DIFETTO, e conta una cosa sola: liberare
         * un blocco che non sta nella PRIMA regione. Ogni volta che succede,
         * il codice di prima partiva da g_heap_start, arrivava in fondo alla
         * prima regione e proseguiva fuori. Non «poteva»: lo faceva. */
        if (regione != g_prima_regione) g_stop_indietro++;

        while (scan < block) {
            KHeapBlock *nx;

            if (!dentro_regione(regione, scan)) break;   /* non ci si arriva */

            /* Dentro una regione i blocchi si incastrano senza buchi: una
             * firma sbagliata qui vuol dire che qualcuno ha scritto oltre il
             * proprio blocco. Non si va avanti a indovinare — e non si va
             * nemmeno in panic, perche' la coalescenza all'indietro e' un
             * risparmio, non un obbligo: si rinuncia, e lo si dice. */
            if (!block_valid(scan)) {
                klog(LOG_ERROR, "KMALLOC: firma rotta a 0x%08x nella regione "
                     "0x%08x-0x%08x (libero 0x%08x): niente coalescenza "
                     "all'indietro", (uint32_t)scan, (uint32_t)regione,
                     regione->fine, (uint32_t)ptr);
                break;
            }

            nx = block_next_phys(scan);
            if (nx == block) {
                prev_phys_blk = scan;
                break;
            }
            if (nx <= scan) break;      /* size a zero: non si avanza mai */
            scan = nx;
        }

        if (prev_phys_blk != NULL &&
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
/* =============================================================================
 * kmalloc_verifica — cammina TUTTE le regioni e controlla tutto
 *
 * ! IL CANARINO ALLA LIBERAZIONE VEDE SOLO CHI VIENE LIBERATO, e un blocco
 * sconfinato puo' restare in mano a qualcuno per tutta la vita del sistema.
 * Questa passa in rassegna ogni blocco di ogni regione: firma, misura che
 * dentro la regione ci sta, e canarino. Rende quanti guai ha trovato, e li
 * dice tutti — uno per riga, col nome di chi aveva allocato.
 *
 * Costa una scansione di qualche decina di blocchi: si puo' chiamare a mano
 * nei momenti in cui si sospetta qualcosa, e la chiama kmalloc_stats.
 * ============================================================================= */
uint32_t kmalloc_verifica(void)
{
    KHeapRegion *r;
    uint32_t     guai = 0, visti = 0;

    for (r = g_regioni; r != NULL; r = r->next) {
        KHeapBlock *b = regione_primo(r);

        while (dentro_regione(r, b)) {
            if (!block_valid(b)) {
                klog(LOG_ERROR, "KHEAP: a 0x%08x, dentro la regione che "
                     "finisce a 0x%08x, non c'e' nessuna firma: la catena "
                     "dei blocchi si e' rotta qui", (uint32_t)b, r->fine);
                guai++;
                break;      /* da qui in poi non si sa piu' dove sono */
            }

            visti++;

            if ((uint32_t)b + BLOCK_HEADER_SIZE + b->size > r->fine) {
                klog(LOG_ERROR, "KHEAP: il blocco 0x%08x dice di essere lungo "
                     "%u byte, ma la sua regione finisce a 0x%08x: "
                     "intestazione rotta", (uint32_t)b, b->size, r->fine);
                guai++;
                break;
            }

            if (canarino_rotto(b, "al controllo")) guai++;

            b = block_next_phys(b);
        }
    }

    klog(guai ? LOG_ERROR : LOG_DEBUG,
         "KHEAP: controllati %u blocchi, %u guai", visti, guai);
    return guai;
}

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
    klog(LOG_INFO, "  Heap base   : 0x%08x (nominale)", g_heap_base);
    klog(LOG_INFO, "  Heap size   : %u KB", g_heap_size / 1024);
    klog(LOG_INFO, "  Blocchi lib.: %u (%u byte)", free_blocks, free_bytes);
    klog(LOG_INFO, "  Allocazioni : %u totali", g_alloc_count);
    klog(LOG_INFO, "  Liberazioni : %u totali", g_free_count);

    /* Le regioni, e quante volte il loro confine ha fermato una lettura.
     * Una riga sola, ma dice due cose che prima non si sapevano: quanti
     * tratti separati e' davvero lo heap, e quanto spesso kfree arrivava al
     * bordo — cioe' quante occasioni al secondo aveva il difetto di
     * @DIF-PANIC per farsi vivo. */
    {
        KHeapRegion *r;
        uint32_t     n = 0;

        for (r = g_regioni; r != NULL; r = r->next) n++;
        klog(LOG_INFO, "  Regioni     : %u (letture fuori evitate: %u in "
             "coda a una regione, %u fuori dalla prima)",
             n, g_stop_avanti, g_stop_indietro);
        if (g_intestazioni_rotte)
            klog(LOG_ERROR, "  ! intestazioni ROTTE: %u — qualcuno scrive "
                 "oltre il proprio blocco", g_intestazioni_rotte);
        if (g_canarini_rotti)
            klog(LOG_ERROR, "  ! canarini ROTTI: %u — e sopra c'e' scritto "
                 "chi aveva allocato quei blocchi", g_canarini_rotti);
    }

    /* E la passata completa: le statistiche dicono quanto, questa dice se. */
    kmalloc_verifica();
}
