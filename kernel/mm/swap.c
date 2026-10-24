/* =============================================================================
 * kernel/mm/swap.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * L'AREA DI SCAMBIO — il perche' di tutto sta in kernel/include/swap.h
 *
 * Qui c'e' il come: l'intestazione da riconoscere, l'elenco degli slot liberi,
 * le due mezze operazioni (scrivi una pagina, rileggila) e la scelta di CHI
 * mandare via.
 * ============================================================================= */

#include "kernel.h"
#include "swap.h"
#include "blk.h"
#include "pmm.h"
#include "paging.h"
#include "sched.h"
#include "kmalloc.h"
#include "ata.h"

static int         g_dev = -1;      /* indice del dispositivo a blocchi */
static SwapTesta   g_testa;
static uint8_t    *g_mappa = NULL;  /* un bit per slot: 1 = occupato */
static uint32_t    g_mappa_byte = 0;
static uint32_t    g_usati = 0;
static uint32_t    g_prossimo = 0;  /* da dove ricominciare a cercare */

/* =============================================================================
 * ! IL RIMBALZO E' UN VETTORE DEL KERNEL, e non e' pigrizia.
 *
 * Per scrivere una pagina fisica sul disco bisogna leggerla, e per leggerla
 * bisogna vederla: c'e' paging_finestra_apri(), che la mappa per un momento —
 * ma tiene gli INTERRUPT CHIUSI per tutta la durata della finestra, ed e'
 * giusto che li tenga, perche' e' una risorsa sola per tutto il kernel.
 *
 * Fare l'I/O del disco dentro quella finestra vorrebbe dire aspettare l'ATA a
 * interrupt chiusi, cioe' millisecondi con il timer fermo. Quindi la pagina si
 * copia QUI dentro con la finestra aperta (una memcpy, microsecondi), la
 * finestra si chiude, e poi si parla col disco.
 *
 * ! QUATTRO KILOBYTE DI .bss SEMPRE OCCUPATI, anche senza swap: e' il prezzo
 * dichiarato. Allocarlo alla prima necessita' vorrebbe dire chiedere memoria
 * proprio nel momento in cui non ce n'e' — che e' esattamente il momento in cui
 * questa funzione viene chiamata.
 * ========================================================================== */
static uint8_t g_rimbalzo[PAGE_SIZE];

/* Pages written to and read back from the area since boot, for `mem`
 * (24 October 2026): the used slots say how much is out NOW, these say
 * whether the area was ever touched - a program that swapped and then
 * freed its memory leaves no used slot behind. */
static uint32_t g_scritte = 0, g_lette = 0;

static uintptr_t irq_spegni(void);
static void      irq_rimetti(uintptr_t f);

/* Il kernel non ha una libreria di stringhe: ogni file si tiene i due aiuti
 * che gli servono, come fanno kernel/mm/shm.c e kernel/loader/lib.c. */
static int uguali(const void *a, const void *b, uint32_t n)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}

static void azzera(void *p, uint32_t n)
{
    uint8_t *q = (uint8_t *)p;
    while (n--) *q++ = 0;
}

/* --- l'elenco degli slot ---------------------------------------------------- */
#define BIT_TEST(i)  (g_mappa[(i) >> 3] &   (uint8_t)(1u << ((i) & 7)))
#define BIT_SET(i)   (g_mappa[(i) >> 3] |=  (uint8_t)(1u << ((i) & 7)))
#define BIT_CLR(i)   (g_mappa[(i) >> 3] &= (uint8_t)~(1u << ((i) & 7)))

int swap_attivo(void)
{
    return g_dev >= 0 && g_mappa != NULL;
}

void swap_conta(uint32_t *totali, uint32_t *usati)
{
    if (totali) *totali = swap_attivo() ? g_testa.slot : 0;
    if (usati)  *usati  = swap_attivo() ? g_usati : 0;
}

void swap_traffico(uint32_t *scritte, uint32_t *lette)
{
    if (scritte) *scritte = g_scritte;
    if (lette)   *lette   = g_lette;
}

int32_t swap_slot_prendi(void)
{
    uint32_t  i, n;
    uintptr_t fl;

    if (!swap_attivo()) return -1;
    fl = irq_spegni();

    /* Si riparte da dove si era arrivati invece che da capo: cercare sempre
     * dall'inizio rende O(n) ogni presa quando l'area e' quasi piena, e
     * un'area quasi piena e' proprio la situazione in cui si fanno piu' prese. */
    for (n = 0; n < g_testa.slot; n++) {
        i = (g_prossimo + n) % g_testa.slot;
        if (!BIT_TEST(i)) {
            BIT_SET(i);
            g_usati++;
            g_prossimo = (i + 1) % g_testa.slot;
            irq_rimetti(fl);
            return (int32_t)i;
        }
    }
    irq_rimetti(fl);
    return -1;
}

void swap_slot_molla(uint32_t slot)
{
    uintptr_t fl;

    if (!swap_attivo() || slot >= g_testa.slot) return;
    fl = irq_spegni();
    if (!BIT_TEST(slot)) {
        irq_rimetti(fl);
        /* ! MOLLARE DUE VOLTE LO STESSO SLOT NON E' UN DETTAGLIO: la seconda
         * pagina che lo prende scrive sopra la prima, e chi rilegge la prima
         * si ritrova la memoria di qualcun altro. Si dice, invece di
         * correggere in silenzio un conto che a quel punto e' gia' sbagliato. */
        klog(LOG_ERROR, "SWAP: slot %u mollato due volte", slot);
        return;
    }
    BIT_CLR(slot);
    if (g_usati) g_usati--;
    irq_rimetti(fl);
}

/* --- le due mezze operazioni ------------------------------------------------ */
static uint64_t lba_di(uint32_t slot)
{
    return (uint64_t)g_testa.primo + (uint64_t)slot * SWAP_SETTORI_SLOT;
}

/* Interrupts off and back as they were: the slot list and the PTEs are
 * changed by the exit of a process too (paging_destroy_directory). */
static uintptr_t irq_spegni(void)
{
    uintptr_t f;

    __asm__ volatile ("pushf; pop %0; cli" : "=r"(f) : : "memory");
    return f;
}

static void irq_rimetti(uintptr_t f)
{
    if (f & 0x200u) __asm__ volatile ("sti" : : : "memory");
}

/* 1 if some live process still has this address space */
static int spazio_vivo(PDE *pd)
{
    uint32_t i;

    for (i = 0; i < MAX_PROCESSES; i++)
        if (g_process_pool[i].state != PROC_UNUSED && g_process_pool[i].state != PROC_ZOMBIE &&
            g_process_pool[i].page_directory == pd)
            return 1;
    return 0;
}

static void copia_in_rimbalzo(paddr_t fisico)
{
    const uint8_t *p = (const uint8_t *)paging_finestra_apri(fisico);
    uint32_t       i;

    for (i = 0; i < PAGE_SIZE; i++) g_rimbalzo[i] = p[i];
    paging_finestra_chiudi();
}

static int scrivi_rimbalzo(uint32_t slot)
{
    if (blk_write(g_dev, lba_di(slot), SWAP_SETTORI_SLOT, g_rimbalzo) < 0) {
        klog(LOG_ERROR, "SWAP: scrittura dello slot %u fallita", slot);
        return -1;
    }
    g_scritte++;
    return 0;
}

/* ! THE CALLER HOLDS THE DISK LOCK (ata_prendi): g_rimbalzo is shared. */
int swap_scrivi(uint32_t slot, uint32_t fisico)
{
    if (!swap_attivo() || slot >= g_testa.slot) return -1;
    copia_in_rimbalzo(fisico);
    return scrivi_rimbalzo(slot);
}

/* ! THE CALLER HOLDS THE DISK LOCK (ata_prendi): g_rimbalzo is shared. */
int swap_leggi(uint32_t slot, uint32_t fisico)
{
    uint32_t i;

    if (!swap_attivo() || slot >= g_testa.slot) return -1;

    if (blk_read(g_dev, lba_di(slot), SWAP_SETTORI_SLOT, g_rimbalzo) < 0) {
        klog(LOG_ERROR, "SWAP: lettura dello slot %u fallita", slot);
        return -1;
    }
    g_lette++;

    {
        uint8_t *p = (uint8_t *)paging_finestra_apri(fisico);
        for (i = 0; i < PAGE_SIZE; i++) p[i] = g_rimbalzo[i];
        paging_finestra_chiudi();
    }
    return 0;
}

/* =============================================================================
 * swap_init — riconoscere l'area, non fidarsi che ci sia
 * ========================================================================== */
int swap_init(const char *nome)
{
    SwapTesta   *t;
    int          dev;
    uint32_t     byte;
    const BlkDev *d;

    if (nome == NULL || nome[0] == '\0') return -1;

    dev = blk_trova(nome);
    if (dev < 0) {
        klog(LOG_WARN, "SWAP: '%s' non esiste: niente memoria virtuale", nome);
        return -1;
    }

    d = blk_get(dev);
    if (d == NULL || d->sola_lettura) {
        klog(LOG_WARN, "SWAP: '%s' e' in sola lettura", nome);
        return -1;
    }

    if (blk_read(dev, 0, 1, g_rimbalzo) < 0) {
        klog(LOG_WARN, "SWAP: '%s' non si legge", nome);
        return -1;
    }

    t = (SwapTesta *)g_rimbalzo;

    /* ! LA FIRMA PRIMA DI TUTTO. Senza, una partizione con dentro un
     * filesystem — o niente — verrebbe usata come area di scambio, e il primo
     * sfratto ci scriverebbe sopra. Il tipo scritto nella tabella delle
     * partizioni non e' una prova: lo mette chiunque. */
    if (!uguali(t->firma, SWAP_FIRMA, SWAP_FIRMA_LEN)) {
        klog(LOG_WARN, "SWAP: '%s' non ha la firma: si formatta con  mkswap %s",
             nome, nome);
        return -1;
    }
    if (t->versione != SWAP_VERSIONE) {
        klog(LOG_WARN, "SWAP: '%s' e' di versione %u, questo kernel parla la %u",
             nome, t->versione, (unsigned)SWAP_VERSIONE);
        return -1;
    }
    /* ! E LA MISURA DELLA PAGINA VA CONFRONTATA, non data per buona: un'area
     * fatta da un sistema con pagine diverse leggerebbe meta' pagine. */
    if (t->pagina != PAGE_SIZE) {
        klog(LOG_WARN, "SWAP: '%s' ha pagine da %u byte, qui sono %u",
             nome, t->pagina, (unsigned)PAGE_SIZE);
        return -1;
    }
    if (t->slot == 0 || t->slot > SWAP_SLOT_MAX) {
        klog(LOG_WARN, "SWAP: '%s' dichiara %u slot: fuori misura", nome, t->slot);
        return -1;
    }

    /* ! E CHE CI STIANO DAVVERO. L'intestazione la scrive un programma; se
     * dicesse piu' slot di quanti settori ha la partizione, il primo sfratto
     * oltre il limite verrebbe rifiutato dal livello a blocchi — bene — ma la
     * pagina sarebbe gia' stata tolta dalla memoria. Meglio non accendere. */
    if ((uint64_t)t->primo + (uint64_t)t->slot * SWAP_SETTORI_SLOT > d->settori) {
        klog(LOG_WARN, "SWAP: '%s' dichiara %u slot ma la partizione e' di "
             "%u settori", nome, t->slot, (uint32_t)d->settori);
        return -1;
    }

    g_testa = *t;

    byte = (g_testa.slot + 7) / 8;
    g_mappa = (uint8_t *)kmalloc(byte);
    if (g_mappa == NULL) {
        klog(LOG_ERROR, "SWAP: niente memoria per l'elenco di %u slot",
             g_testa.slot);
        return -1;
    }
    azzera(g_mappa, byte);
    g_mappa_byte = byte;
    g_usati      = 0;
    g_prossimo   = 0;
    g_dev        = dev;

    klog(LOG_INFO, "SWAP: '%s' attiva: %u slot da %u KB (%u MB), elenco di %u byte",
         nome, g_testa.slot, (unsigned)(PAGE_SIZE / 1024),
         (unsigned)(((uint64_t)g_testa.slot * PAGE_SIZE) / (1024 * 1024)), byte);
    return 0;
}

/* =============================================================================
 * swap_sfratta — chi se ne va
 *
 * ! LE TABELLE DELLE PAGINE LE GIRA paging.c, NON QUESTO FILE. Le macro che
 * spezzano un indirizzo in indice di directory e indice di tabella stanno la',
 * e sono private apposta: due copie degli stessi turni di bit sono due modi di
 * sbagliarli. Qui si sceglie COSA farne — uno slot, una scrittura, il
 * segnaposto — e la passeggiata la fa chi le tabelle le possiede.
 *
 * La divisione e' anche il motivo per cui la politica sta scritta in un posto
 * solo: paging_vittima() sa dire «questa non serve a nessuno adesso», e non sa
 * niente di dischi.
 * ========================================================================== */
int swap_sfratta(void)
{
    paddr_t   frame = 0;
    vaddr_t   virt  = 0;
    PDE      *pd = NULL;
    int32_t   slot;
    uintptr_t prima, segno, fl;

    if (!swap_attivo()) return 0;

    /* ! THE DISK LOCK FOR THE WHOLE EVICTION (24 October 2026), and not only
     * around the write. g_rimbalzo is one buffer for the whole kernel: two
     * evictions - or an eviction and a page coming back, swap_leggi - that
     * slept on the disk one after the other wrote each other's page into
     * their slot. The lock is re-entrant (blk_write takes it again) and its
     * waiters sleep. */
    ata_prendi();
    fl = irq_spegni();

    /* ! I DUE MODI DI NON RIUSCIRCI SI DICONO, E UNA VOLTA SOLA. Sono due
     * situazioni opposte — «non c'e' niente da mandare via» e «non c'e' piu'
     * posto dove mandarlo» — e la prima e' quasi sempre un difetto della
     * scelta della vittima, la seconda e' un'area troppo piccola. Senza
     * distinguerle, la memoria virtuale che non funziona si presenta in
     * tutt'e due i casi come un OUT OF MEMORY identico a prima.
     *
     * ! E UNA VOLTA SOLA PERCHE' QUI SI PASSA A OGNI PAGINA. Una riga per
     * tentativo vorrebbe dire migliaia di righe che scorrono via portandosi
     * dietro tutto il resto del registro. */
    if (!paging_vittima(&pd, &virt, &frame)) {
        static int detto = 0;
        irq_rimetti(fl);
        ata_lascia();
        if (!detto) {
            detto = 1;
            klog(LOG_WARN, "SWAP: nessuna pagina da mandare via (tutte "
                 "condivise, o tutte in uso adesso)");
        }
        return 0;
    }

    slot = swap_slot_prendi();
    if (slot < 0) {
        static int pieno = 0;
        irq_rimetti(fl);
        ata_lascia();
        if (!pieno) {
            pieno = 1;
            klog(LOG_WARN, "SWAP: area piena, %u slot tutti occupati",
                 g_testa.slot);
        }
        return 0;
    }

    /* ! FIRST THE PAGE COMES OFF, THEN IT IS COPIED (24 October 2026). Until
     * today it was written while still mapped, and unmapped after: the disk
     * write sleeps, the owner ran meanwhile, and what it wrote in those
     * milliseconds was lost - the swap kept the old copy. With interrupts
     * off from the choice to here nobody on this CPU can touch it; another
     * CPU is checked AFTER the PTE changed: one that loads this space from
     * now on sees the page missing, one that already had it is caught here. */
    prima = paging_pte_leggi(pd, virt);
    paging_marca_swap(pd, virt, (uint32_t)slot);
    segno = paging_pte_leggi(pd, virt);
    if (sched_spazio_altrove(pd)) {
        paging_pte_scrivi(pd, virt, prima);
        swap_slot_molla((uint32_t)slot);
        irq_rimetti(fl);
        ata_lascia();
        return 0;
    }
    copia_in_rimbalzo(frame);
    irq_rimetti(fl);

    /* the frame is nobody's now: the PTE speaks of the slot */
    if (scrivi_rimbalzo((uint32_t)slot) != 0) {
        /* ! THE OWNER MAY HAVE DIED WHILE THE DISK WORKED. Its tables are
         * gone then, and its exit already let go of the slot: the PTE is
         * written back only if the space is still alive and still says
         * «that slot». */
        fl = irq_spegni();
        if (spazio_vivo(pd) && paging_pte_leggi(pd, virt) == segno) {
            paging_pte_scrivi(pd, virt, prima);
            swap_slot_molla((uint32_t)slot);
            frame = 0;
        }
        irq_rimetti(fl);
        if (frame) pmm_free_page(frame);
        ata_lascia();
        return 0;
    }
    ata_lascia();
    pmm_free_page(frame);

    klog(LOG_DEBUG, "SWAP: pagina 0x%08x -> slot %d", virt, slot);
    return 1;
}
