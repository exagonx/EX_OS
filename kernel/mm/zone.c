/* =============================================================================
 * kernel/mm/zone.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ZONES OF FILES, LOADED AT THE FIRST TOUCH (kernel 0.371, voce 120)
 *
 * Until 0.370 only the executable's pages came when touched (pf_carica_da_file,
 * the VMAs of elf.c). A file mapped without writing was read whole at mmap
 * (omni.ja, 106 MB, at Firefox's first process), and the read-only pages of a
 * .so whole at its first mapping (condivise.c). The disk read what nobody
 * would touch, before the program could do anything.
 *
 * Now both are ZONES: pages [vstart, vend) of the space are pages of a file
 * from file_off, and a fault there takes the page from filecache.c - the same
 * frame for the whole machine - as the executable's pages do. This is the
 * "list of the zones of a process" the comment at mmap waited for (voce 16).
 *
 * ! READ-ONLY ONLY. A write to a zone page is an mprotect away, and then
 *   paging_proteggi brings the page in first and makes it copy on write.
 * ! THE LEADER OF THE GROUP KEEPS THEM (proc_spazio_capo): threads share one
 *   space, as for heap_end. They change inside a syscall of the space
 *   (mmap, munmap, SYS_SO_MAPPA under proc_spazio_prendi), at fork, exec and
 *   exit.
 * ! A FAULT SLEEPS ON THE DISK with the zone copied, the file held by a
 *   vfs_dup of its own: meanwhile another thread may munmap it. zone_gen
 *   says the list moved, and then the zone is looked for again before the
 *   frame goes in.
 * ! NO CEILING (the user's rule): a list that doubles when full.
 *
 * ! A FAULT AS ON LINUX (0.371, measured): a page already in filecache is
 *   mapped with no VFS at all (fc_c_e), and with it the neighbours the cache
 *   has (fault-around, ZONA_INTORNO). A page that is missing is read with
 *   the missing pages around it in one request (read-around, the same
 *   window): page by page, Firefox's first start compiling omni.ja took
 *   158 s against 111 for the whole file read at mmap.
 * ============================================================================= */

#include "kernel.h"
#include "sched.h"
#include "paging.h"
#include "pmm.h"
#include "kmalloc.h"
#include "vfs.h"
#include "syscall.h"     /* ENOMEM, EINVAL, EBADF */
#include "filecache.h"
#include "zone.h"

/* pages around a fault: 32 = 128 KB, Linux's ra_pages */
#define ZONA_INTORNO 32u

static int posto(Process *p, uint32_t piu)
{
    ProcZona *nuovo;
    uint32_t  posti, i;

    if (p->n_zone + piu <= p->zone_posti) return 0;
    posti = p->zone_posti ? p->zone_posti * 2u : 8u;
    while (posti < p->n_zone + piu) posti *= 2u;
    nuovo = (ProcZona *)kmalloc(posti * sizeof(ProcZona));
    if (nuovo == NULL) return -ENOMEM;
    for (i = 0; i < p->n_zone; i++) nuovo[i] = p->zone[i];
    if (p->zone) kfree(p->zone);
    p->zone = nuovo;
    p->zone_posti = posti;
    return 0;
}

void zona_togli(Process *p, uint32_t va, uint32_t pagine)
{
    uint64_t fine;
    uint32_t i;

    p = proc_spazio_capo(p);
    if (p == NULL || p->n_zone == 0 || pagine == 0) return;
    va  &= ~(uint32_t)(PAGE_SIZE - 1);
    fine = (uint64_t)va + (uint64_t)pagine * PAGE_SIZE;

ancora:
    for (i = 0; i < p->n_zone; i++) {
        ProcZona *z = &p->zone[i];

        if ((uint64_t)z->vstart >= fine || z->vend <= va) continue;
        if (z->vstart < va && (uint64_t)z->vend > fine) {
            /* across both edges: the right part becomes a zone of its own */
            ProcZona destra = *z;

            if (posto(p, 1) != 0 || vfs_dup(p->zone[i].handle) < 0) {
                klog(LOG_WARN, "ZONE: PID %u, zona 0x%08x-0x%08x tagliata a 0x%08x "
                     "(niente memoria per il pezzo dopo 0x%08x)", p->pid,
                     z->vstart, z->vend, va, (uint32_t)fine);
                p->zone[i].vend = va;
                p->zone_gen++;
                continue;
            }
            destra.file_off += (uint32_t)fine - destra.vstart;
            destra.vstart    = (uint32_t)fine;
            p->zone[i].vend  = va;
            p->zone[p->n_zone++] = destra;
            p->zone_gen++;
            continue;
        }
        if (z->vstart < va) { z->vend = va; p->zone_gen++; continue; }
        if ((uint64_t)z->vend > fine) {
            z->file_off += (uint32_t)fine - z->vstart;
            z->vstart    = (uint32_t)fine;
            p->zone_gen++;
            continue;
        }
        /* inside: it goes. vfs_close may sleep: the list is out of our hands
         * until it returns, so from the top again */
        {
            int h = z->handle;

            *z = p->zone[--p->n_zone];
            p->zone_gen++;
            vfs_close(h);
            goto ancora;
        }
    }
}

int zona_aggiungi(Process *p, uint32_t va, uint32_t pagine, int h, uint32_t off,
                  uint32_t pg_flags)
{
    ProcZona z;

    p = proc_spazio_capo(p);
    if (p == NULL || pagine == 0 || h < 0 || (va & (PAGE_SIZE - 1)) || (off & (PAGE_SIZE - 1)))
        return -EINVAL;
    if (pagine > (0xFFFFFFFFu - va) / PAGE_SIZE + 1u) return -EINVAL;
    zona_togli(p, va, pagine);
    if (posto(p, 1) != 0) return -ENOMEM;
    if (fc_chiave(h, &z.chiave) != 0) return -EBADF;
    if (vfs_dup(h) < 0) return -EBADF;
    z.vstart   = va;
    z.vend     = va + pagine * PAGE_SIZE;
    z.file_off = off;
    z.pg_flags = (pg_flags | PG_PRESENT) & ~(uint32_t)PG_WRITABLE;
    z.handle   = h;
    p->zone[p->n_zone++] = z;
    p->zone_gen++;
    return 0;
}

void zone_tutte_via(Process *p)
{
    if (p == NULL) return;
    while (p->n_zone > 0) {
        int h = p->zone[--p->n_zone].handle;

        p->zone_gen++;
        vfs_close(h);
    }
}

void zone_libera(Process *p)
{
    if (p == NULL) return;
    zone_tutte_via(p);
    if (p->zone) kfree(p->zone);
    p->zone = NULL;
    p->zone_posti = 0;
}

int zone_copia(Process *figlio, Process *padre)
{
    uint32_t i;

    padre = proc_spazio_capo(padre);
    if (figlio == NULL || padre == NULL || padre->n_zone == 0) return 0;
    if (posto(figlio, padre->n_zone) != 0) return -ENOMEM;
    for (i = 0; i < padre->n_zone; i++) {
        if (vfs_dup(padre->zone[i].handle) < 0) continue;      /* closed: lost */
        figlio->zone[figlio->n_zone++] = padre->zone[i];
    }
    figlio->zone_gen++;
    return 0;
}

static int trova(const Process *c, uint32_t pagina, ProcZona *out)
{
    uint32_t i;

    for (i = 0; i < c->n_zone; i++) {
        if (pagina >= c->zone[i].vstart && pagina < c->zone[i].vend) {
            *out = c->zone[i];
            return 1;
        }
    }
    return 0;
}

/* The neighbours of `pagina` in [da, a) of the zone that the cache already
 * has, mapped too: no disk, no sleep. */
static void intorno(Process *p, const ProcZona *z, uint32_t pagina, uint32_t da, uint32_t a)
{
    uint32_t q;

    for (q = da; q < a; q += PAGE_SIZE) {
        paddr_t fr;

        if (q == pagina || paging_get_physical(p->page_directory, q) != 0) continue;
        fr = fc_c_e(&z->chiave, (z->file_off + (q - z->vstart)) / PAGE_SIZE);
        if (fr == 0) continue;
        if (paging_map_page(p->page_directory, q, fr, z->pg_flags) != 0) { pmm_free_page(fr); return; }
    }
}

int zona_carica(Process *p, uint32_t va)
{
    Process *c = proc_spazio_capo(p);
    uint32_t pagina = va & ~(uint32_t)(PAGE_SIZE - 1), gen, idx, da, a, mezzo;
    ProcZona z, z2;
    paddr_t  fr;
    int      letto = 0;

    if (p == NULL || c == NULL || p->page_directory == NULL || c->n_zone == 0) return 0;
    if (!trova(c, pagina, &z)) return 0;
    if (paging_get_physical(p->page_directory, pagina) != 0) return 1;

    /* the window: ZONA_INTORNO pages around the fault, inside the zone */
    mezzo = (ZONA_INTORNO / 2u) * PAGE_SIZE;
    da = pagina - z.vstart > mezzo ? pagina - mezzo : z.vstart;
    a  = z.vend - pagina > ZONA_INTORNO * PAGE_SIZE - (pagina - da) ?
         da + ZONA_INTORNO * PAGE_SIZE : z.vend;

    gen = c->zone_gen;
    idx = (z.file_off + (pagina - z.vstart)) / PAGE_SIZE;
    fr  = fc_c_e(&z.chiave, idx);
    if (fr == 0) {
        if (vfs_dup(z.handle) < 0) return 0;
        fr = fc_leggi(&z.chiave, z.handle, idx,
                      (z.file_off + (da - z.vstart)) / PAGE_SIZE,
                      (z.file_off + (a - z.vstart)) / PAGE_SIZE);
        if (fr == 0) fr = fc_pagina_privata(z.handle, idx);     /* the last piece, past the end */
        vfs_close(z.handle);
        letto = 1;
    }
    if (fr == 0) {
        klog(LOG_ERROR, "ZONE: PID %u, pagina 0x%08x del file (pagina %u): niente frame",
             p->pid, pagina, idx);
        return 0;
    }

    /* the list moved while we read: still the same zone page? */
    if (letto && c->zone_gen != gen &&
        (!trova(c, pagina, &z2) || z2.handle != z.handle ||
         z2.file_off + (pagina - z2.vstart) != idx * PAGE_SIZE)) {
        pmm_free_page(fr);
        return 1;               /* the fault comes again, and decides */
    }
    if (paging_get_physical(p->page_directory, pagina) != 0) {
        pmm_free_page(fr);      /* another thread mapped it meanwhile */
        return 1;
    }
    if (paging_map_page(p->page_directory, pagina, fr, z.pg_flags) != 0) {
        pmm_free_page(fr);
        klog(LOG_ERROR, "ZONE: PID %u, mapping fallito per 0x%08x", p->pid, pagina);
        return 0;
    }
    if (c->zone_gen == gen) intorno(p, &z, pagina, da, a);
    return 1;
}
