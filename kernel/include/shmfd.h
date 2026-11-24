/* =============================================================================
 * kernel/include/shmfd.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * MEMORY THAT IS A DESCRIPTOR (kernel 0.358, voce 115): shm_open(SHM_ANON)
 * and memfd_create.
 *
 * ! WHY NOT THE ZONES OF shm.h. Those are the windows' memory: a name, a
 * size fixed when made and no bigger than the screen, mapped at the top of
 * the heap and nowhere else. Firefox with more processes makes thousands of
 * these, of any size, grows them with ftruncate, maps them where its
 * allocator says, and hands them to its children over a unix pair
 * (SCM_RIGHTS). Until 0.357 the libc kept them as memory of the process
 * alone: in another process the number meant nothing.
 *
 * The object holds its pages, one reference each (pmm_ref_inc). A mapping
 * adds one per page and munmap, exit and paging_destroy_directory take it
 * away as they do for every page: so the memory lives as long as a
 * descriptor or a mapping holds it, and dies with the last. The pages are
 * mapped PG_CONDIVISA: fork keeps them the same memory.
 *
 * `rif` counts the descriptors (dup, fork, spawn, SCM_RIGHTS add one, close
 * takes one), like the readers of a pipe. Interrupts off guard it, as for the
 * pipes, because a process's exit closes its descriptors with interrupts off
 * (proc_chiudi_fd): hence the _locked versions.
 * ============================================================================= */
#ifndef SHMFD_H
#define SHMFD_H

#include "kernel.h"
#include "sched.h"

int      shmfd_crea(void);                 /* handle, rif = 1; or -errno */
void     shmfd_apri(int h);                /* one more descriptor */
void     shmfd_chiudi(int h);              /* one less; the last frees */
void     shmfd_chiudi_locked(int h);
/* ftruncate: grows with zeroed pages, shrinks giving back the ones past the
 * end (a mapping keeps its own until munmap). 0 or -errno. */
int32_t  shmfd_misura(int h, uint32_t byte);
uint32_t shmfd_byte(int h);
/* `pagine` pages from page `da` of the object at `vaddr` in proc, with
 * pg_flags (PG_CONDIVISA added). The object must reach that far: -EINVAL
 * otherwise (ftruncate first, as POSIX programs do). 0 or -errno. */
int32_t  shmfd_mappa(int h, Process *proc, uint32_t vaddr, uint32_t pagine,
                     uint32_t da, uint32_t pg_flags);

#endif /* SHMFD_H */
