/* =============================================================================
 * kernel/arch/x86_64/paging64.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LA PAGINAZIONE A 64 BIT (@EXOS-64, tappa 3c — 10 ottobre 2026)
 *
 * E' la stessa interfaccia di kernel/include/paging.h: chi chiama
 * paging_map_page o paging_create_directory non sa quale dei due file c'e'
 * sotto. Cambia solo cio' che il processore impone: le tabelle sono a QUATTRO
 * livelli (PML4, PDPT, PD, PT) con voci di otto byte e 512 voci per tabella,
 * dove a 32 bit erano due livelli da 1024 voci di quattro byte.
 *
 * ! LA DISPOSIZIONE DELLA MEMORIA E' QUELLA DEL KERNEL A 32 BIT, ed e' una
 * scelta: sched, exec, mmap, lo scambio su disco la danno per buona, e
 * cambiarla insieme alla larghezza dei registri vorrebbe dire non sapere mai
 * quale delle due cose ha rotto.
 *   - la FASCIA KERNEL: i primi 64 MB (0 -> USER_SPACE_BASE) mappati su se
 *     stessi, uguali in ogni processo. Le tabelle delle pagine nascono li'
 *     (pmm_alloc_page_kernel), e per questo si leggono col loro indirizzo
 *     fisico da qualunque spazio;
 *   - i programmi da USER_SPACE_BASE in su;
 *   - la directory del KERNEL mappa su se stessa tutta la RAM (sotto i 4 GB);
 *     quella di un processo solo la fascia, e una pagina fuori dalla fascia
 *     si raggiunge dalla FINESTRA (paging_finestra_apri).
 *
 * ! «PDE *» RESTA IL NOME DI UNO SPAZIO DI INDIRIZZI. A 32 bit e' la page
 * directory, qui e' la PML4: per gli altri file e' una maniglia opaca, e
 * nessuno fuori da qui ci guarda dentro.
 *
 * COME LA FASCIA E' CONDIVISA. Sta tutta nella prima PD (il primo gigabyte):
 * le sue prime 32 voci, 2 MB l'una. Ogni processo ha PML4, PDPT e prima PD
 * sue, e in quelle 32 voci una COPIA di quelle del kernel. I primi 4 MB sono
 * a pagine da 4 KB (due tabelle, k_pt_bassa): dentro ci sono la finestra e
 * l'APIC, che si cambiano una pagina alla volta - e siccome le tabelle sono
 * le stesse per tutti, chi le cambia le cambia per tutti. Il resto della
 * fascia e' a pagine da 2 MB.
 *
 * ! QUI C'E' SOLO LA MECCANICA: tabelle, finestra, spazi di indirizzi. La
 * POLITICA - il gestore degli errori di pagina, lo stack che cresce, le
 * pagine caricate dal file, lo scambio, mprotect - parla di processi e non
 * dipende dal processore: e' la seconda meta' di kernel/mm/paging.c, lo
 * STESSO file per i due kernel. Guarda nelle tabelle solo attraverso le macro
 * TAB_*, che qui arrivano a paging64_voce_pd() e paging64_spezza(). Le due
 * funzioni che scorrono le tabelle per intero (la scelta della pagina da
 * mandare sullo scambio) hanno la loro versione in pagine64.c.
 * ============================================================================= */
#include "kernel.h"
#include "pmm.h"
#include "swap.h"
#include "paging.h"
#include "k64.h"

#define PG_HUGE         (1 << 7)                    /* nella PD: pagina da 2 MB */
#define PG_ADDR(v)      ((v) & 0x000FFFFFFFFFF000ull)
#define PG_ADDR_2MB(v)  ((v) & 0x000FFFFFFFE00000ull)

#define MB2             0x200000ull
#define GB1             0x40000000ull
#define GB4             0x100000000ull
#define VOCI            512u

#define I4(v)   (((uint64_t)(v) >> 39) & 511)       /* nella PML4 */
#define I3(v)   (((uint64_t)(v) >> 30) & 511)       /* nella PDPT */
#define I2(v)   (((uint64_t)(v) >> 21) & 511)       /* nella PD   */
#define I1(v)   (((uint64_t)(v) >> 12) & 511)       /* nella PT   */

/* quante voci della prima PD sono la fascia kernel: 64 MB / 2 MB */
#define FASCIA_VOCI     ((uint32_t)(USER_SPACE_BASE / MB2))

/* Una tabella, dal valore di una voce che la indica: sta nella fascia, il suo
 * indirizzo fisico e' anche quello a cui si legge. */
#define TAB(voce)       ((uint64_t *)DA_FISICO(PG_ADDR(voce)))

/* -----------------------------------------------------------------------------
 * Lo spazio del kernel
 * --------------------------------------------------------------------------- */
static uint64_t k_pml4[VOCI]        ALIGNED(4096);
static uint64_t k_pdpt[VOCI]        ALIGNED(4096);
static uint64_t k_pd[4][VOCI]       ALIGNED(4096);  /* i primi 4 GB */
static uint64_t k_pt_bassa[2][VOCI] ALIGNED(4096);  /* i primi 4 MB, a 4 KB */

/* le quattro PD in fila: la voce n governa i 2 MB che cominciano a n * 2 MB */
#define K_PD(n)         (((uint64_t *)k_pd)[n])

static PDE *g_corrente = NULL;

/* dove sta il framebuffer, in voci di PD contate dall'indirizzo 0 */
static uint32_t g_fb_da = 0, g_fb_a = 0;

static inline uint64_t cr3_leggi(void)
{
    uint64_t v;

    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void cr3_scrivi(uint64_t v) { __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory"); }
static inline void tlb_via(vaddr_t v)     { __asm__ volatile("invlpg (%0)" : : "r"(v) : "memory"); }
static inline void tlb_tutto(void)        { cr3_scrivi(cr3_leggi()); }

static inline uint64_t flag_leggi(void)
{
    uint64_t f;

    __asm__ volatile("pushfq; pop %0" : "=r"(f));
    return f;
}

/* la voce dei primi 4 MB che governa `virt` */
static inline uint64_t *voce_bassa(vaddr_t virt)
{
    return &((uint64_t *)k_pt_bassa)[(I2(virt) & 1) * VOCI + I1(virt)];
}

/* -----------------------------------------------------------------------------
 * La finestra, l'APIC, l'MMIO basso: pagine singole dentro i primi 4 MB
 * --------------------------------------------------------------------------- */
static int      g_finestra_aperta = 0;
static uint64_t g_finestra_flag   = 0;

void *paging_finestra_apri(paddr_t phys)
{
    uint64_t flag = flag_leggi();

    __asm__ volatile("cli");
    if (g_finestra_aperta) {
        kpanic("PAGING: finestra di rimappatura aperta due volte");
    }
    g_finestra_aperta = 1;
    g_finestra_flag   = flag;

    *voce_bassa(PAGING_FINESTRA_VIRT) = PG_ADDR(phys) | PG_PRESENT | PG_WRITABLE;
    tlb_via(PAGING_FINESTRA_VIRT);

    return (void *)(uintptr_t)(PAGING_FINESTRA_VIRT + (phys & 0xFFF));
}

void paging_finestra_chiudi(void)
{
    if (!g_finestra_aperta) return;

    *voce_bassa(PAGING_FINESTRA_VIRT) = 0;
    tlb_via(PAGING_FINESTRA_VIRT);
    g_finestra_aperta = 0;

    if (g_finestra_flag & (1u << 9)) __asm__ volatile("sti");
}

void *paging_mappa_apic(paddr_t phys)
{
    *voce_bassa(PAGING_APIC_VIRT) =
        PG_ADDR(phys) | PG_PRESENT | PG_WRITABLE | PG_CACHE_DIS | PG_WRITE_THRU;
    tlb_via(PAGING_APIC_VIRT);
    return (void *)(uintptr_t)(PAGING_APIC_VIRT + (phys & 0xFFF));
}

void *paging_mappa_mmio_basso(vaddr_t virt, paddr_t phys, uint32_t pagine)
{
    uint32_t i;

    for (i = 0; i < pagine; i++) {
        vaddr_t v = virt + (vaddr_t)i * PAGE_SIZE;

        if (v >= 2 * MB2) return NULL;          /* «basso» vuol dire nei primi 4 MB */
        *voce_bassa(v) = PG_ADDR(phys + (paddr_t)i * PAGE_SIZE) | PG_PRESENT |
                         PG_WRITABLE | PG_CACHE_DIS | PG_WRITE_THRU;
        tlb_via(v);
    }
    return (void *)(uintptr_t)(virt + (phys & 0xFFF));
}

void paging_azzera_fisica(paddr_t phys)
{
    uint64_t *p = (uint64_t *)paging_finestra_apri(PG_ADDR(phys));
    uint32_t  n = PAGE_SIZE / 8;

    while (n--) *p++ = 0;
    paging_finestra_chiudi();
}

/* -----------------------------------------------------------------------------
 * Scendere per le tabelle
 * --------------------------------------------------------------------------- */

/* Una tabella nuova, azzerata, nella fascia. 0 se la memoria e' finita anche
 * dopo aver mandato via qualche pagina. */
static paddr_t tabella_nuova(void)
{
    paddr_t   t = pmm_alloc_page_kernel();
    uint64_t *p;
    uint32_t  n;
    int       giri;

    for (giri = 0; giri < 64 && t == 0; giri++) {
        if (!swap_sfratta()) break;
        t = pmm_alloc_page_kernel();
    }
    if (t == 0) return 0;

    p = (uint64_t *)DA_FISICO(t);
    for (n = 0; n < VOCI; n++) p[n] = 0;
    return t;
}

/* Fa in modo che `voce` indichi una tabella, creandola se manca; `utente`
 * (PG_USER o 0) si aggiunge alla voce, anche se c'era gia'.
 *
 * ! LA TABELLA SI PRENDE PRIMA, E SI ATTACCA A INTERRUPT SPENTI ricontrollando
 * la voce: prenderla puo' voler dire mandare una pagina sullo scambio, cioe'
 * dormire, e intanto un altro puo' aver fatto la stessa strada. E' la stessa
 * cura di paging_map_page a 32 bit. */
static int tabella_sotto(uint64_t *voce, uint64_t utente)
{
    paddr_t  t;
    uint64_t flag;
    int      doppia = 0;

    if (!(*voce & PG_PRESENT)) {
        t = tabella_nuova();
        if (t == 0) return -1;

        flag = flag_leggi();
        __asm__ volatile("cli");
        if (*voce & PG_PRESENT) doppia = 1;
        else                    *voce = t | PG_PRESENT | PG_WRITABLE | utente;
        if (flag & 0x200u) __asm__ volatile("sti");
        if (doppia) pmm_free_page(t);
    }
    if (utente && !(*voce & PG_USER)) {
        *voce |= PG_USER;
        tlb_tutto();
    }
    return 0;
}

/* La voce della PD che governa `virt` nello spazio `pml4`. Con `crea` fa
 * nascere PDPT e PD che mancano; senza, rende NULL se non ci sono. */
static uint64_t *voce_pd(PDE *pml4, vaddr_t virt, int crea)
{
    uint64_t *v4 = &pml4[I4(virt)], *v3;
    uint64_t  utente = (virt >= USER_SPACE_BASE) ? PG_USER : 0;

    if (crea) { if (tabella_sotto(v4, utente) != 0) return NULL; }
    else if (!(*v4 & PG_PRESENT)) return NULL;

    v3 = &TAB(*v4)[I3(virt)];
    if (crea) { if (tabella_sotto(v3, utente) != 0) return NULL; }
    else if (!(*v3 & PG_PRESENT)) return NULL;

    return &TAB(*v3)[I2(virt)];
}

/* Una pagina da 2 MB diventa una tabella di 512 pagine da 4 KB che dicono la
 * stessa cosa: serve quando di quei 2 MB se ne vuole cambiare una sola. */
static int spezza_2mb(uint64_t *v2)
{
    uint64_t  base = PG_ADDR_2MB(*v2);
    uint64_t  flag = (*v2 & 0xFFFu) & ~(uint64_t)PG_HUGE;
    paddr_t   t    = pmm_alloc_page_kernel();
    uint64_t *pt;
    uint32_t  i;

    if (t == 0) {
        klog(LOG_ERROR, "PAGING: OOM spezzando una pagina da 2 MB (0x%08x)", (uint32_t)base);
        return -1;
    }
    pt = (uint64_t *)DA_FISICO(t);
    for (i = 0; i < VOCI; i++) pt[i] = (base + (uint64_t)i * PAGE_SIZE) | flag;

    *v2 = t | (flag & (PG_PRESENT | PG_WRITABLE | PG_USER));
    tlb_tutto();
    return 0;
}

/* La PTE di `virt` nello spazio `pd`, o NULL se la sua tabella non c'e' (o se
 * li' c'e' una pagina da 2 MB). Non crea niente. E' la porta da cui passera'
 * la politica delle pagine (vedi in testa al file). */
PTE *paging64_pte(PDE *pd, vaddr_t virt)
{
    uint64_t *v2;

    if (pd == NULL) return NULL;
    v2 = voce_pd(pd, virt, 0);
    if (v2 == NULL || !(*v2 & PG_PRESENT) || (*v2 & PG_HUGE)) return NULL;
    return &TAB(*v2)[I1(virt)];
}

/* Le due porte per la POLITICA delle pagine, che sta in kernel/mm/paging.c ed
 * e' la stessa dei due kernel (le macro TAB_* la' in mezzo): il valore della
 * voce di PD che governa `virt` (0 se sopra non c'e' niente), e spezzare la
 * pagina da 2 MB che c'e' li'. */
uint64_t paging64_voce_pd(PDE *pd, vaddr_t virt)
{
    uint64_t *v2 = (pd != NULL) ? voce_pd(pd, virt, 0) : NULL;

    return (v2 != NULL) ? *v2 : 0;
}

int paging64_spezza(PDE *pd, vaddr_t virt)
{
    uint64_t *v2 = (pd != NULL) ? voce_pd(pd, virt, 0) : NULL;

    if (v2 == NULL || !(*v2 & PG_PRESENT)) return -1;
    if (!(*v2 & PG_HUGE)) return 0;
    return spezza_2mb(v2);
}

/* -----------------------------------------------------------------------------
 * Mappare, togliere, tradurre
 * --------------------------------------------------------------------------- */
int paging_map_page(PDE *pd, vaddr_t virt, paddr_t phys, uint32_t flags)
{
    uint64_t *v2 = voce_pd(pd, virt, 1);

    if (v2 == NULL) {
        klog(LOG_ERROR, "PAGING: OOM durante map_page virt=0x%08x%08x",
             (uint32_t)((uint64_t)virt >> 32), (uint32_t)virt);
        return -1;
    }
    if ((*v2 & PG_PRESENT) && (*v2 & PG_HUGE)) {
        if (spezza_2mb(v2) != 0) return -1;
    }
    /* sopra la fascia la voce e' di un programma, e a dire chi puo' toccare
     * la pagina e' la PTE; nella fascia e' del kernel, a meno che la tabella
     * nasca adesso per chi la chiede sua (come a 32 bit) */
    if (tabella_sotto(v2, (virt >= USER_SPACE_BASE ||
                           (!(*v2 & PG_PRESENT) && (flags & PG_USER))) ? PG_USER : 0) != 0) {
        klog(LOG_ERROR, "PAGING: OOM durante map_page virt=0x%08x%08x",
             (uint32_t)((uint64_t)virt >> 32), (uint32_t)virt);
        return -1;
    }

    TAB(*v2)[I1(virt)] = PG_ADDR(phys) | (flags & 0xFFF) | PG_PRESENT;
    tlb_via(virt & ~(vaddr_t)0xFFF);
    return 0;
}

void paging_unmap_page(PDE *pd, vaddr_t virt)
{
    uint64_t *v2 = voce_pd(pd, virt, 0);

    if (v2 == NULL || !(*v2 & PG_PRESENT)) return;
    if (*v2 & PG_HUGE) {
        if (spezza_2mb(v2) != 0) return;
    }
    TAB(*v2)[I1(virt)] = 0;
    tlb_via(virt & ~(vaddr_t)0xFFF);
}

paddr_t paging_get_physical(PDE *pd, vaddr_t virt)
{
    uint64_t *v2 = voce_pd(pd, virt, 0);
    uint64_t  pte;

    if (v2 == NULL || !(*v2 & PG_PRESENT)) return 0;
    if (*v2 & PG_HUGE) return PG_ADDR_2MB(*v2) | (virt & (MB2 - 1));

    pte = TAB(*v2)[I1(virt)];
    if (!(pte & PG_PRESENT)) return 0;
    return PG_ADDR(pte) | (virt & 0xFFF);
}

/* Una voce NON presente con un segno: la pagina e' del processo ma la memoria
 * non c'e' (vedi PG_RISERVA e PG_PIGRA in paging.h). La tabella deve esserci:
 * la si fa nascere mappando e poi si riscrive la voce. */
static int segna(PDE *pd, vaddr_t virt, uint64_t segno)
{
    PTE *pte;

    virt &= ~(vaddr_t)0xFFF;
    if (paging_map_page(pd, virt, 0, 0) != 0) return -1;
    pte = paging64_pte(pd, virt);
    if (pte == NULL) return -1;
    *pte = segno;
    tlb_via(virt);
    return 0;
}

int paging_riserva(PDE *pd, vaddr_t virt)
{
    return segna(pd, virt, PG_RISERVA);
}

int paging_pigra(PDE *pd, vaddr_t virt, uint32_t flags)
{
    return segna(pd, virt, PG_PIGRA | (flags & (PG_USER | PG_WRITABLE)));
}

/* -----------------------------------------------------------------------------
 * L'avvio
 * --------------------------------------------------------------------------- */

/* In long mode le pagine da 2 MB ci sono sempre: non e' una capacita' da
 * chiedere al processore, come il PSE a 32 bit. */
int paging_pse_attivo(void) { return 1; }

/* La prova che spezzare non cambia le traduzioni, fatta una volta all'avvio
 * su un blocco vero e poi disfatta: come a 32 bit, perche' un errore qui si
 * vedrebbe solo il giorno che qualcuno mappa una pagina dentro un blocco. */
static void prova_spezzamento(vaddr_t base)
{
    vaddr_t   campione[4];
    paddr_t   prima[4];
    uint64_t *v2 = voce_pd(k_pml4, base, 0), salvata;
    uint32_t  i;

    if (v2 == NULL || !(*v2 & PG_HUGE)) return;
    salvata = *v2;

    campione[0] = base;
    campione[1] = base + PAGE_SIZE + 0x123;
    campione[2] = base + MB2 / 2;
    campione[3] = base + MB2 - 1;
    for (i = 0; i < 4; i++) prima[i] = paging_get_physical(k_pml4, campione[i]);

    if (spezza_2mb(v2) != 0) {
        klog(LOG_WARN, "PAGING: prova dello spezzamento saltata (niente memoria)");
        return;
    }
    for (i = 0; i < 4; i++) {
        if (paging_get_physical(k_pml4, campione[i]) != prima[i]) {
            kpanic("PAGING: spezzando un blocco da 2 MB la traduzione cambia");
        }
    }
    pmm_free_page(PG_ADDR(*v2));
    *v2 = salvata;
    tlb_tutto();

    klog(LOG_INFO, "PAGING: spezzamento provato su 0x%08x - le traduzioni coincidono",
         (uint32_t)base);
}

void paging_init(void)
{
    uint64_t ram = (uint64_t)pmm_get_total_pages() * PAGE_SIZE;
    uint64_t fine, a;
    uint32_t i, blocchi = 0;

    klog(LOG_INFO, "PAGING: inizializzazione (quattro livelli)...");

    for (i = 0; i < VOCI; i++) { k_pml4[i] = 0; k_pdpt[i] = 0; }
    for (i = 0; i < 4 * VOCI; i++) K_PD(i) = 0;

    k_pml4[0] = IN_NUMERO(k_pdpt) | PG_PRESENT | PG_WRITABLE;
    for (i = 0; i < 4; i++) k_pdpt[i] = IN_NUMERO(k_pd[i]) | PG_PRESENT | PG_WRITABLE;

    /* i primi 4 MB, una pagina alla volta */
    for (i = 0; i < 2 * VOCI; i++) {
        ((uint64_t *)k_pt_bassa)[i] = ((uint64_t)i * PAGE_SIZE) | PG_PRESENT | PG_WRITABLE;
    }
    k_pd[0][0] = IN_NUMERO(k_pt_bassa[0]) | PG_PRESENT | PG_WRITABLE;
    k_pd[0][1] = IN_NUMERO(k_pt_bassa[1]) | PG_PRESENT | PG_WRITABLE;

    /* ! IL SISTEMA NASCE SOTTO I 4 GB: la RAM oltre non e' usata (pmm non la
     * conta), e quattro PD bastano. Almeno la fascia intera, anche su una
     * macchina che ha meno di 64 MB: le tabelle di ogni processo ne copiano
     * le voci, e una voce vuota non fa danno. */
    fine = ALIGN_UP(ram, MB2);
    if (fine < USER_SPACE_BASE) fine = USER_SPACE_BASE;
    if (fine > GB4)             fine = GB4;

    for (a = 2 * MB2; a < fine; a += MB2) {
        K_PD(a / MB2) = a | PG_PRESENT | PG_WRITABLE | PG_HUGE;
        blocchi++;
    }

    klog(LOG_INFO, "PAGING: RAM 0x0 - 0x%08x su se stessa (%u MB): 4 MB a pagine "
         "da 4 KB, %u blocchi da 2 MB", (uint32_t)(fine - 1), (uint32_t)(fine >> 20), blocchi);

    g_corrente = k_pml4;
    cr3_scrivi(IN_NUMERO(k_pml4));
    klog(LOG_INFO, "PAGING: CR3 = 0x%08x, tabelle del kernel in uso", (uint32_t)IN_NUMERO(k_pml4));

    if (blocchi) prova_spezzamento((vaddr_t)(fine - MB2));
}

int paging_mappa_framebuffer(paddr_t phys, uint32_t byte)
{
    uint64_t a;

    if (phys == 0 || byte == 0) return -1;
    if (phys + byte > GB4) {
        /* EXOS64-DAFARE: un framebuffer oltre i 4 GB vuole un posto suo nello
         * spazio del kernel; finche' il sistema nasce sotto i 4 GB lo si dice */
        klog(LOG_ERROR, "PAGING: framebuffer oltre i 4 GB, non mappato");
        return -1;
    }
    for (a = 0; a < byte; a += PAGE_SIZE) {
        if (paging_map_page(k_pml4, (vaddr_t)(phys + a), phys + a,
                            PG_PRESENT | PG_WRITABLE) != 0) return -1;
    }
    g_fb_da = (uint32_t)(phys / MB2);
    g_fb_a  = (uint32_t)((phys + byte - 1) / MB2) + 1;
    return 0;
}

/* -----------------------------------------------------------------------------
 * Gli spazi dei processi
 * --------------------------------------------------------------------------- */
PDE *paging_create_directory(void)
{
    paddr_t   f4 = tabella_nuova(), f3 = tabella_nuova(), f2 = tabella_nuova();
    uint64_t *pml4, *pdpt, *pd;
    uint32_t  i;

    if (f4 == 0 || f3 == 0 || f2 == 0) {
        if (f4) pmm_free_page(f4);
        if (f3) pmm_free_page(f3);
        if (f2) pmm_free_page(f2);
        klog(LOG_ERROR, "PAGING: OOM creando lo spazio di un processo");
        return NULL;
    }
    pml4 = (uint64_t *)DA_FISICO(f4);
    pdpt = (uint64_t *)DA_FISICO(f3);
    pd   = (uint64_t *)DA_FISICO(f2);

    /* ! PG_USER SUI DUE LIVELLI ALTI: sotto la stessa voce stanno la fascia e
     * il programma. Il permesso vero lo danno le voci piu' in basso - quelle
     * della fascia non lo hanno, e un programma li' non arriva. */
    pml4[0] = f3 | PG_PRESENT | PG_WRITABLE | PG_USER;
    pdpt[0] = f2 | PG_PRESENT | PG_WRITABLE | PG_USER;

    for (i = 0; i < FASCIA_VOCI; i++) pd[i] = K_PD(i);

    /* il framebuffer, se c'e': le stesse tabelle del kernel, come a 32 bit */
    for (i = g_fb_da; i < g_fb_a; i++) {
        vaddr_t   v  = (vaddr_t)i * MB2;
        uint64_t *v2 = voce_pd(pml4, v, 1);

        if (v2 == NULL) {
            paging_destroy_directory(pml4);
            return NULL;
        }
        *v2 = K_PD(i);
    }

    klog(LOG_DEBUG, "PAGING: nuovo spazio di processo, PML4 a 0x%08x", (uint32_t)f4);
    return pml4;
}

void paging_destroy_directory(PDE *pml4)
{
    uint64_t ram = (uint64_t)pmm_get_total_pages() * PAGE_SIZE;
    uint32_t i4, i3, i2, i1;

    if (pml4 == NULL || pml4 == k_pml4) return;

    for (i4 = 0; i4 < VOCI; i4++) {
        uint64_t *pdpt;

        if (!(pml4[i4] & PG_PRESENT)) continue;
        pdpt = TAB(pml4[i4]);

        for (i3 = 0; i3 < VOCI; i3++) {
            uint64_t *pd;

            if (!(pdpt[i3] & PG_PRESENT)) continue;
            pd = TAB(pdpt[i3]);

            for (i2 = 0; i2 < VOCI; i2++) {
                uint64_t *pt;

                /* la fascia e il framebuffer sono tabelle del kernel: la
                 * copia della voce se ne va con la PD, la tabella resta */
                if (i4 == 0 && i3 < 4) {
                    uint32_t g = i3 * VOCI + i2;

                    if (g < FASCIA_VOCI) continue;
                    if (g >= g_fb_da && g < g_fb_a) continue;
                }
                if (!(pd[i2] & PG_PRESENT) || (pd[i2] & PG_HUGE)) continue;
                pt = TAB(pd[i2]);

                for (i1 = 0; i1 < VOCI; i1++) {
                    if (SWAP_PTE_E_SWAP(pt[i1])) {
                        swap_slot_molla((uint32_t)SWAP_PTE_SLOT(pt[i1]));
                        continue;
                    }
                    if (pt[i1] & PG_PRESENT) {
                        uint64_t frame = PG_ADDR(pt[i1]);

                        /* fuori dalla RAM e' un dispositivo (MMIO), non
                         * una pagina da rendere */
                        if (frame >= ram) continue;
                        pmm_free_page(frame);
                    }
                }
                pmm_free_page(PG_ADDR(pd[i2]));
            }
            pmm_free_page(PG_ADDR(pdpt[i3]));
        }
        pmm_free_page(PG_ADDR(pml4[i4]));
    }
    pmm_free_page(IN_NUMERO(pml4));

    klog(LOG_DEBUG, "PAGING: spazio di processo 0x%08x distrutto (dati utente inclusi)",
         (uint32_t)IN_NUMERO(pml4));
}

void paging_switch(PDE *pd)
{
    if (pd == g_corrente) return;
    g_corrente = pd;
    cr3_scrivi(IN_NUMERO(pd));
}

PDE *paging_get_kernel_directory(void)  { return k_pml4; }
PDE *paging_get_current_directory(void) { return g_corrente; }
