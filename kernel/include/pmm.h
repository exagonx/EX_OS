/* =============================================================================
 * kernel/include/pmm.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef PMM_H
#define PMM_H

#include "kernel.h"

#define PAGE_SIZE   4096

void     pmm_init(BootInfo *info);
uint32_t pmm_alloc_page(void);
uint32_t pmm_alloc_pages(uint32_t count);
void     pmm_free_page(uint32_t addr);
void     pmm_free_pages(uint32_t addr, uint32_t count);
uint32_t pmm_get_free_pages(void);
uint32_t pmm_get_used_pages(void);
uint32_t pmm_get_total_pages(void);
int      pmm_is_page_free(uint32_t addr);
void     pmm_dump(void);

/* =============================================================================
 * pmm_region_stat — quante pagine, di cui libere, in [base, base+len)
 *
 * Serve a SYS_MEMINFO per dare i totali per fascia (convenzionale, area
 * superiore, estesa) invece del solo totale globale. Conta interrogando la
 * bitmap pagina per pagina: la fascia piu' grande su una macchina da 192MB
 * sono ~48000 iterazioni, irrilevanti per un comando interattivo e senza
 * alcuno stato da mantenere aggiornato in tempo reale.
 *
 * Le pagine oltre la RAM realmente presente non vengono contate: una
 * fascia interamente fuori dalla memoria installata risulta di zero
 * pagine, che e' esattamente cio' che si vuole mostrare.
 *
 * out_total e out_free possono essere NULL.
 * ============================================================================= */
void     pmm_region_stat(uint32_t base, uint32_t len,
                         uint32_t *out_total, uint32_t *out_free);

#endif /* PMM_H */
