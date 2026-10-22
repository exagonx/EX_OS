/* =============================================================================
 * kernel/include/rdlz4.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * The compressed base system (@RDBOOT step 1b): kernel/boot/rdlz4.c.
 * ============================================================================= */

#ifndef RDLZ4_H
#define RDLZ4_H

#include "kernel.h"

/* Where the expanded volume goes: 6 MB, below the compressed file that
 * Stage 2 puts at 10 MB (RBDEST in bootloader/stage2/loader.asm) - a 16 MB
 * machine must boot. Below 6 MB: the kernel, its bss, the PMM bitmap and
 * references (pmm_init checks they stay below). Hence at most 4 MB. */
#define RD_ESPANSO      0x00600000u
#define RD_ESPANSO_MAX  (4u * 1024u * 1024u)

/* One LZ4 block: bytes written, or -1 if malformed. Every read and write is
 * bounds-checked against `n` and `cap`. */
int  lz4_blocco(const uint8_t *src, uint32_t n, uint8_t *dst, uint32_t cap);

/* If BootInfo describes a compressed rdboot ('EXLZ'), expand it to
 * RD_ESPANSO and make rb_addr/rb_byte describe the expanded volume. Must run
 * BEFORE pmm_init, which protects the range BootInfo names. On a broken
 * block BootInfo ends with no volume at all. */
void rd_decomprimi(BootInfo *info);

#endif /* RDLZ4_H */
