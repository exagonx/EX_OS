/* =============================================================================
 * kernel/include/kmalloc.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#ifndef KMALLOC_H
#define KMALLOC_H

#include "kernel.h"

void  kmalloc_init(void);
void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);
void  kfree(void *ptr);
void  kfree_aligned(void *ptr);
void  kmalloc_stats(void);

/* Alias comodi */
#define kmalloc_page()  kmalloc_aligned(PAGE_SIZE, PAGE_SIZE)

#endif /* KMALLOC_H */
