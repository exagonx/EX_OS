/* =============================================================================
 * kernel/include/paging.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef PAGING_H
#define PAGING_H

#include "kernel.h"
#include "idt.h"

typedef uint32_t PDE;
typedef uint32_t PTE;

/* Flag pagine */
#define PG_PRESENT      (1 << 0)
#define PG_WRITABLE     (1 << 1)
#define PG_USER         (1 << 2)
#define PG_WRITE_THRU   (1 << 3)
#define PG_CACHE_DIS    (1 << 4)
#define PG_ACCESSED     (1 << 5)
#define PG_DIRTY        (1 << 6)
#define PG_GLOBAL       (1 << 8)

void     paging_init(void);
int      paging_map_page(PDE *pd, uint32_t virt, uint32_t phys, uint32_t flags);
void     paging_unmap_page(PDE *pd, uint32_t virt);
uint32_t paging_get_physical(PDE *pd, uint32_t virt);
PDE     *paging_create_directory(void);
void     paging_destroy_directory(PDE *pd);
void     paging_switch(PDE *pd);
PDE     *paging_get_kernel_directory(void);
PDE     *paging_get_current_directory(void);
void     page_fault_handler(InterruptFrame *frame);

#endif /* PAGING_H */
