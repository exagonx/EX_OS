/* =============================================================================
 * kernel/arch/x86_64/main64.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * IL PRIMO C A 64 BIT (@EXOS-64, tappa 1 — 10 ottobre 2026)
 *
 * ! NON E' ANCORA IL KERNEL: e' la prova che ci si arriva. entry.asm porta il
 * processore in long mode e chiama qui; qui si dice sulla seriale e sullo
 * schermo dove si e', con i numeri che lo dimostrano - un puntatore di otto
 * byte, i registri di controllo letti dal processore - e ci si ferma.
 *
 * Il kernel vero (memoria, interrupt, scheduler) arriva con le tappe dopo, e
 * sara' quello di kernel/, ripulito dove assume i 32 bit: questo file allora
 * diventera' il suo kernel_main per questa architettura.
 * ============================================================================= */
#include "version.h"
#include "k64.h"
#include "pmm.h"
#include "kmalloc.h"
#include "paging.h"

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define COM1 0x3F8
#define outb k64_outb
#define inb  k64_inb

static u16 *g_vga = (u16 *)0xB8000;
static int  g_riga = 0, g_col = 0;

static void metti(char c)
{
    if (c == '\n') metti('\r');
    while ((inb(COM1 + 5) & 0x20) == 0) ;
    outb(COM1, (u8)c);

    /* Lo schermo, se e' in modo testo: Stage 2 puo' averlo lasciato in
     * grafica, e allora questi byte finiscono in una memoria che non mostra
     * niente - la seriale resta la voce che conta. */
    if (c == '\r') { g_col = 0; return; }
    if (c == '\n') { g_riga++; return; }
    if (g_riga < 25 && g_col < 80) g_vga[g_riga * 80 + g_col] = (u16)(0x0F00 | (u8)c);
    g_col++;
}

void k64_scrivi(const char *s) { while (*s) metti(*s++); }
#define scrivi k64_scrivi

void k64_esa(uint64_t v)
{
    int i;

    scrivi("0x");
    for (i = 60; i >= 0; i -= 4) metti("0123456789abcdef"[(v >> i) & 15]);
}
#define esa k64_esa

void k64_numero(uint64_t v)
{
    char b[24];
    int  n = 0;

    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) metti(b[--n]);
}
#define numero k64_numero

static volatile int g_arresti = 0;
static volatile u64 g_pagina_dove = 0;

static int prova_arresto(InterruptFrame *f) { (void)f; g_arresti++; return 1; }

static int prova_pagina(InterruptFrame *f)
{
    u64 cr2;

    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    g_pagina_dove = cr2;
    f->rip += 2;                /* oltre la mov che ha toccato l'indirizzo */
    return 1;
}

extern u32 avvio_reg[6];

void kmain64(void)
{
    u64 cr0, cr3, cr4, rsp;
    u32 efer_basso, efer_alto;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    __asm__ volatile("rdmsr" : "=a"(efer_basso), "=d"(efer_alto) : "c"(0xC0000080u));

    /* si ricomincia dalla riga 12 dello schermo: sopra ci sono i messaggi di
     * Stage 2, se lo schermo e' in testo */
    g_riga = 12;

    scrivi("\nEX-OS " EXOS_VERSION " (x86_64): il kernel e' in modo a 64 bit\n");
    scrivi("  un puntatore e' di "); numero(sizeof(void *));
    scrivi(" byte, un long di "); numero(sizeof(long)); scrivi("\n");
    scrivi("  CR0  "); esa(cr0); scrivi("   (bit 31: paginazione)\n");
    scrivi("  CR3  "); esa(cr3); scrivi("   (le tabelle delle pagine)\n");
    scrivi("  CR4  "); esa(cr4); scrivi("   (bit 5: PAE)\n");
    scrivi("  EFER "); esa(((u64)efer_alto << 32) | efer_basso);
    scrivi("   (bit 8: long mode chiesto, bit 10: attivo)\n");
    scrivi("  RSP  "); esa(rsp); scrivi("\n");
    scrivi("  Stage 2 ha lasciato EBX="); esa(avvio_reg[1]);
    scrivi(" ESI="); esa(avvio_reg[4]); scrivi("\n");

    /* Una moltiplicazione che a 32 bit non sta in un registro: se il
     * risultato e' giusto, i registri sono davvero di 64 bit. */
    {
        volatile u64 a = 0x100000001ull, b = 0x10ull;

        scrivi("  0x100000001 * 16 = "); esa(a * b);
        scrivi((a * b) == 0x1000000010ull ? "   giusto\n" : "   SBAGLIATO\n");
    }
    scrivi("EXOS64-TAPPA1-OK\n");

    /* ------------------------------------------------------------------ *
     * Tappa 3a: segmenti, interrupt, orologio.                            *
     * ------------------------------------------------------------------ */
    gdt64_installa();
    idt64_installa();
    scrivi("\nGDT, TSS e IDT a 64 bit installati\n");

    /* 1. un'eccezione gestita: int3 torna qui, e la riga dopo si esegue */
    idt64_gestore(3, prova_arresto);
    __asm__ volatile("int3");
    scrivi(g_arresti == 1 ? "  punto di arresto: gestito, e si e' ripreso dopo\n"
                          : "  punto di arresto: NON e' arrivato al gestore\n");

    /* 2. l'orologio: venti battiti a 100 Hz, aspettando con hlt */
    idt64_orologio_avvia(100);
    __asm__ volatile("sti");
    while (idt64_battiti() < 20) __asm__ volatile("hlt");
    __asm__ volatile("cli");
    scrivi("  orologio: "); numero(idt64_battiti()); scrivi(" battiti\n");

    /* 3. un errore di pagina gestito: si tocca un indirizzo oltre il primo
     * gigabyte, che non e' mappato; il gestore lo annota e salta la riga. */
    idt64_gestore(14, prova_pagina);
    g_pagina_dove = 0;
    {
        volatile u8 *lontano = (volatile u8 *)0x7654321000ull;
        u8 letto = 0x5A;

        /* la lettura sta in una riga sola di assembly, cosi' il gestore sa
         * di quanto avanzare: e' una mov di 2 byte (0x8a 0x00) */
        __asm__ volatile("movb (%%rax), %%al" : "=a"(letto) : "a"(lontano));
        (void)letto;
    }
    scrivi("  errore di pagina: CR2 = "); esa(g_pagina_dove);
    scrivi(g_pagina_dove == 0x7654321000ull ? "   giusto, e si e' ripreso\n" : "   SBAGLIATO\n");

    if (g_arresti == 1 && idt64_battiti() >= 20 && g_pagina_dove == 0x7654321000ull)
        scrivi("EXOS64-TAPPA3A-OK\n");

    /* ------------------------------------------------------------------ *
     * Tappa 3b: i primi file COMUNI al lavoro a 64 bit - il registro dei  *
     * messaggi, l'allocatore delle pagine, lo heap. Sono gli stessi       *
     * sorgenti del kernel a 32 bit, senza una riga diversa.               *
     * ------------------------------------------------------------------ */
    idt64_gestore(14, 0);               /* da qui un errore di pagina e' un errore */
    scrivi("\n");
    klog(LOG_INFO, "klog a 64 bit: %s, %u, %d, 0x%08x, %c", "una stringa", 4000000000u, -7, 0xCAFE, 'k');
    pmm_init((BootInfo *)DA_FISICO(0xC000));
    {
        uint32_t prima = pmm_get_free_pages();
        paddr_t  a = pmm_alloc_page(), b = pmm_alloc_page_kernel();
        char    *p1, *p2, *p3;
        int      bene = 1, i;

        klog(LOG_INFO, "pmm: %u pagine in tutto, %u libere; prese 0x%08x e 0x%08x",
             pmm_get_total_pages(), prima, (uint32_t)a, (uint32_t)b);
        if (a == 0 || b == 0 || a == b || pmm_get_free_pages() != prima - 2) bene = 0;
        pmm_free_page(a);
        pmm_free_page(b);
        if (pmm_get_free_pages() != prima) bene = 0;

        kmalloc_init();
        p1 = (char *)kmalloc(100);
        p2 = (char *)kmalloc(70000);            /* piu' di una pagina: lo heap deve crescere */
        p3 = (char *)kmalloc(33);
        if (!p1 || !p2 || !p3 || p1 == p2 || p2 == p3) bene = 0;
        if (bene) {
            for (i = 0; i < 100; i++)   p1[i] = (char)i;
            for (i = 0; i < 70000; i++) p2[i] = (char)(i * 7);
            for (i = 0; i < 33; i++)    p3[i] = 'z';
            for (i = 0; i < 100; i++)   if (p1[i] != (char)i) bene = 0;
            for (i = 0; i < 70000; i++) if (p2[i] != (char)(i * 7)) bene = 0;
        }
        klog(LOG_INFO, "kmalloc: 100 byte a 0x%08x, 70000 a 0x%08x, 33 a 0x%08x",
             (uint32_t)IN_NUMERO(p1), (uint32_t)IN_NUMERO(p2), (uint32_t)IN_NUMERO(p3));
        kfree(p2);
        kfree(p1);
        kfree(p3);
        if (kmalloc_verifica() != 0) bene = 0;
        klog(LOG_INFO, "kmalloc: liberati, heap %s", kmalloc_verifica() == 0 ? "integro" : "ROTTO");
        if (bene) scrivi("EXOS64-TAPPA3B-OK\n");
        else      scrivi("EXOS64-TAPPA3B: qualcosa non torna\n");
    }

    /* ------------------------------------------------------------------ *
     * Tappa 3c: la memoria a quattro livelli (paging64.c), dietro la      *
     * stessa interfaccia di paging.h.                                     *
     * ------------------------------------------------------------------ */
    scrivi("\n");
    paging_init();
    {
        PDE     *k = paging_get_kernel_directory(), *sp;
        vaddr_t  alto = (vaddr_t)0x8000201000ull;    /* oltre i 512 GB: un'altra voce della PML4 */
        vaddr_t  ut   = (vaddr_t)USER_SPACE_BASE + 0x5000;
        paddr_t  f, g;
        uint32_t libere;
        volatile uint32_t *p;
        int      bene = 1;

        /* 1. una pagina mappata lontano nello spazio del kernel: cio' che si
         * scrive la' si legge all'indirizzo fisico, e tolta non c'e' piu' */
        f = pmm_alloc_page_kernel();
        libere = pmm_get_free_pages();
        if (paging_map_page(k, alto, f, PG_PRESENT | PG_WRITABLE) != 0) bene = 0;
        if (bene) {
            p = (volatile uint32_t *)alto;
            p[3] = 0xC0FFEE64u;
            if (((volatile uint32_t *)DA_FISICO(f))[3] != 0xC0FFEE64u) bene = 0;
            if (paging_get_physical(k, alto + 0x123) != f + 0x123) bene = 0;
            paging_unmap_page(k, alto);
            if (paging_get_physical(k, alto) != 0) bene = 0;

            idt64_gestore(14, prova_pagina);
            g_pagina_dove = 0;
            {
                u8 letto = 0;

                __asm__ volatile("movb (%%rax), %%al" : "=a"(letto) : "a"(alto));
                (void)letto;
            }
            idt64_gestore(14, 0);
            if (g_pagina_dove != alto) bene = 0;
        }
        klog(LOG_INFO, "paging: pagina a 0x%08x%08x -> 0x%08x, tolta: %s; %u tabelle nuove",
             (uint32_t)((u64)alto >> 32), (uint32_t)alto, (uint32_t)f,
             bene ? "giusto" : "SBAGLIATO", libere - pmm_get_free_pages());

        /* 2. la finestra: una pagina qualunque vista senza mapparla */
        g = pmm_alloc_page();
        {
            volatile uint32_t *w = (volatile uint32_t *)paging_finestra_apri(g + 0x10);

            *w = 0x12345678u;
            paging_finestra_chiudi();
        }
        if (*(volatile uint32_t *)DA_FISICO(g + 0x10) != 0x12345678u) bene = 0;
        paging_azzera_fisica(g);
        if (*(volatile uint32_t *)DA_FISICO(g + 0x10) != 0) bene = 0;
        klog(LOG_INFO, "paging: finestra sulla pagina 0x%08x: %s", (uint32_t)g,
             bene ? "giusto" : "SBAGLIATO");

        /* 3. lo spazio di un processo: la fascia kernel c'e' (si continua a
         * girare), la sua pagina e' sua, e distruggendolo torna tutto */
        libere = pmm_get_free_pages();
        sp = paging_create_directory();
        if (sp == NULL) bene = 0;
        if (bene) {
            paddr_t u = pmm_alloc_page();
            int     fuori;

            paging_azzera_fisica(u);
            if (paging_map_page(sp, ut, u, PG_PRESENT | PG_WRITABLE | PG_USER) != 0) bene = 0;
            if (paging_map_page(sp, alto, pmm_alloc_page(), PG_PRESENT | PG_WRITABLE | PG_USER) != 0) bene = 0;
            if (paging_riserva(sp, ut + 0x1000) != 0) bene = 0;
            if (paging_pigra(sp, ut + 0x2000, PG_USER | PG_WRITABLE) != 0) bene = 0;
            if (!PTE_E_RISERVA(*paging64_pte(sp, ut + 0x1000))) bene = 0;
            if (!PTE_E_PIGRA(*paging64_pte(sp, ut + 0x2000))) bene = 0;
            if (paging_get_physical(sp, ut + 0x1000) != 0) bene = 0;
            /* la RAM oltre la fascia in uno spazio di processo non c'e' */
            fuori = (u >= USER_SPACE_BASE) && paging_get_physical(sp, (vaddr_t)u) == 0;

            paging_switch(sp);
            p = (volatile uint32_t *)ut;
            if (p[0] != 0) bene = 0;                    /* azzerata dalla finestra */
            p[0] = 0x64646464u;
            {
                volatile uint32_t *w = (volatile uint32_t *)paging_finestra_apri(u);

                if (*w != 0x64646464u) bene = 0;        /* la finestra vale in ogni spazio */
                paging_finestra_chiudi();
            }
            paging_switch(k);
            if (*(volatile uint32_t *)DA_FISICO(u) != 0x64646464u) bene = 0;

            klog(LOG_INFO, "paging: spazio di processo a 0x%08x, pagina utente 0x%08x -> 0x%08x%s: %s",
                 (uint32_t)IN_NUMERO(sp), (uint32_t)ut, (uint32_t)u,
                 fuori ? " (fuori dalla fascia, non mappata la')" : "",
                 bene ? "giusto" : "SBAGLIATO");
            paging_destroy_directory(sp);
        }
        if (pmm_get_free_pages() != libere) bene = 0;
        klog(LOG_INFO, "paging: spazio distrutto, pagine libere %u (prima %u)",
             pmm_get_free_pages(), libere);

        pmm_free_page(g);
        pmm_free_page(f);
        if (bene) scrivi("EXOS64-TAPPA3C-OK\n");
        else      scrivi("EXOS64-TAPPA3C: qualcosa non torna\n");
    }
}
