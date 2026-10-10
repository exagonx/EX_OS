/* =============================================================================
 * kernel/arch/x86_64/pagine64.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LO SCAMBIO E LE TABELLE A QUATTRO LIVELLI (@EXOS-64, tappa 3d — 10 ott. 2026)
 *
 * Le due funzioni di paging.h che SCORRONO le tabelle di un processo: a
 * 32 bit sono due cicli annidati su directory e tabella (kernel/mm/paging.c),
 * e non si possono dire con le macro TAB_* della politica comune, che parlano
 * di un indirizzo alla volta. Qui lo stesso giro, sulle tabelle di paging64.c.
 * Il criterio e' lo stesso, riga per riga: chi cambia l'uno guardi l'altro.
 *
 * Stanno fuori da paging64.c perche' parlano di processi (g_process_pool,
 * sched_spazio_altrove), e paging64.c deve potersi collegare anche da solo.
 * ============================================================================= */
#include "kernel.h"
#include "pmm.h"
#include "swap.h"
#include "paging.h"
#include "sched.h"
#include "k64.h"

#define PG_HUGE     (1 << 7)
#define PG_ADDR(v)  ((v) & 0x000FFFFFFFFFF000ull)
#define MB2         0x200000ull

uint64_t paging64_voce_pd(PDE *pd, vaddr_t virt);

static inline void tlb_via(vaddr_t v) { __asm__ volatile("invlpg (%0)" : : "r"(v) : "memory"); }

/* Da dove riprende la ricerca: una lancetta che gira sui processi, cosi' non
 * e' sempre lo stesso a pagare (la «seconda possibilita'» dell'orologio). */
static uint32_t g_giro_proc = 0;

int paging_vittima(PDE **out_pd, vaddr_t *out_virt, paddr_t *out_frame)
{
    uint64_t ram = (uint64_t)pmm_get_total_pages() * PAGE_SIZE;
    uint32_t tentativi;

    if (out_pd == NULL || out_virt == NULL || out_frame == NULL) return 0;

    for (tentativi = 0; tentativi < 2 * MAX_PROCESSES; tentativi++) {
        Process *p = &g_process_pool[g_giro_proc % MAX_PROCESSES];
        PDE     *pd;
        vaddr_t  blocco;

        g_giro_proc++;

        if (p->state == PROC_UNUSED || p->state == PROC_ZOMBIE) continue;
        if (p->page_directory == NULL) continue;
        /* uno spazio in uso su un altro processore non si tocca: la' la
         * pagina puo' essere letta mentre la si manda via */
        if (sched_spazio_altrove(p->page_directory)) continue;
        pd = p->page_directory;

        for (blocco = USER_SPACE_BASE; blocco < USER_SPACE_END; blocco += MB2) {
            uint64_t  v2 = paging64_voce_pd(pd, blocco);
            uint64_t *pt;
            uint32_t  i;

            if (!(v2 & PG_PRESENT) || (v2 & PG_HUGE)) continue;
            pt = (uint64_t *)DA_FISICO(PG_ADDR(v2));

            for (i = 0; i < 512; i++) {
                uint64_t pte = pt[i], frame;
                vaddr_t  v   = blocco + (vaddr_t)i * PAGE_SIZE;

                if (!(pte & PG_PRESENT)) continue;
                if (!(pte & PG_USER))    continue;

                frame = PG_ADDR(pte);
                if (frame >= ram) continue;                 /* un dispositivo, non RAM */
                if (pmm_ref_count(frame) != 1) continue;    /* condivisa: non si sfratta */

                if (pte & PG_ACCESSED) {
                    /* usata da poco: le si toglie il segno e si passa oltre;
                     * se al prossimo giro e' ancora senza, tocca a lei */
                    pt[i] = pte & ~(uint64_t)PG_ACCESSED;
                    if (p == proc_get_current()) tlb_via(v);
                    continue;
                }

                *out_pd    = pd;
                *out_virt  = v;
                *out_frame = frame;
                return 1;
            }
        }
    }
    return 0;
}

void paging_marca_swap(PDE *pd, vaddr_t virt, uint32_t slot)
{
    PTE *pte = paging64_pte(pd, virt);

    if (pte == NULL) return;
    *pte = SWAP_PTE(slot, *pte);

    if (proc_get_current() != NULL && proc_get_current()->page_directory == pd) tlb_via(virt);
}
