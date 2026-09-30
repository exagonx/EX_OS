/* =============================================================================
 * lib/include/sys/mman.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <sys/mman.h> — mmap, munmap, mprotect and the PROT_/MAP_ flags
 *
 * A thin facade over libc.h, like the other standard headers: see
 * lib/include/errno.h for why there is one list and not two.
 * ============================================================================= */

#ifndef EXOS_SYS_MMAN_H
#define EXOS_SYS_MMAN_H

#include "../libc.h"

/* madvise: un CONSIGLIO, e su EX-OS rende 0 senza fare niente — tenere le
 * pagine e' una risposta lecita, e chi chiede MADV_DONTNEED (il GC di
 * SpiderMonkey) non conta che tornino a zero (@EXILLA-JS, 30 settembre 2026). */
#define MADV_NORMAL      0
#define MADV_RANDOM      1
#define MADV_SEQUENTIAL  2
#define MADV_WILLNEED    3
#define MADV_DONTNEED    4
#define MADV_FREE        8
#define POSIX_MADV_NORMAL     MADV_NORMAL
#define POSIX_MADV_RANDOM     MADV_RANDOM
#define POSIX_MADV_SEQUENTIAL MADV_SEQUENTIAL
#define POSIX_MADV_WILLNEED   MADV_WILLNEED
#define POSIX_MADV_DONTNEED   MADV_DONTNEED

#ifdef __cplusplus
extern "C" {
#endif
int madvise(void *addr, size_t lung, int consiglio);
int posix_madvise(void *addr, size_t lung, int consiglio);
#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_MMAN_H */
