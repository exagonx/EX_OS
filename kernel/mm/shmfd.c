/* =============================================================================
 * kernel/mm/shmfd.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Memory that is a descriptor (FD_SHM). Why, and how it lives and dies, in
 * kernel/include/shmfd.h.
 * ============================================================================= */
#include "kernel.h"
#include "shmfd.h"
#include "sched.h"
#include "pmm.h"
#include "paging.h"
#include "kmalloc.h"
#include "syscall.h"

void *memset(void *dst, int c, uint32_t n);

typedef struct {
    int       usato;
    uint32_t  rif;          /* descriptors open on it */
    uint32_t  byte;         /* what ftruncate said */
    uint32_t  pagine;       /* pages held (byte rounded up) */
    uint32_t  posti;        /* room in fisico */
    paddr_t  *fisico;
} ShmFd;

/* AS MANY AS ARE NEEDED, like the pipes: an object is kmalloc'd when every
 * one is in use and reused afterwards, never freed, so the handle stays an
 * index and a pointer read under the lock stays memory. The directory
 * doubles; the old one is left in place. */
static ShmFd   **volatile g_dir;
static uint32_t          g_dir_posti;
static volatile uint32_t g_n;

static ShmFd *da_handle(int h)
{
    if (h < 0 || (uint32_t)h >= g_n) return NULL;
    if (!g_dir[h]->usato)            return NULL;
    return g_dir[h];
}

int shmfd_crea(void)
{
    for (;;) {
        ShmFd   *nuovo;
        ShmFd  **dir = NULL, **scarto = NULL;
        uint32_t h;

        interrupts_disable();
        for (h = 0; h < g_n; h++) {
            ShmFd *s = g_dir[h];

            if (!s->usato) {
                s->usato  = 1;
                s->rif    = 1;
                s->byte   = 0;
                s->pagine = 0;
                interrupts_enable();
                return (int)h;
            }
        }
        interrupts_enable();

        nuovo = (ShmFd *)kmalloc(sizeof(ShmFd));
        if (nuovo == NULL) return ERR(ENOMEM);
        memset(nuovo, 0, sizeof(ShmFd));
        if (g_n >= g_dir_posti) {
            uint32_t posti = g_dir_posti ? g_dir_posti * 2u : 64u;

            dir = (ShmFd **)kmalloc(posti * sizeof(ShmFd *));
            if (dir == NULL) { kfree(nuovo); return ERR(ENOMEM); }
            interrupts_disable();
            if (g_n >= g_dir_posti) {
                uint32_t i;

                for (i = 0; i < g_n; i++) dir[i] = g_dir[i];
                g_dir       = dir;
                g_dir_posti = posti;
            } else {
                scarto = dir;           /* somebody else grew it */
            }
        } else {
            interrupts_disable();
        }
        if (g_n < g_dir_posti) {
            g_dir[g_n] = nuovo;
            g_n++;
            nuovo = NULL;
        }
        interrupts_enable();
        if (scarto) kfree(scarto);
        if (nuovo)  kfree(nuovo);
    }
}

void shmfd_apri(int h)
{
    ShmFd *s;

    interrupts_disable();
    s = da_handle(h);
    if (s) s->rif++;
    interrupts_enable();
}

/* The last descriptor: the object's own reference to each page goes. A page
 * still mapped somewhere lives on with that mapping's reference. */
void shmfd_chiudi_locked(int h)
{
    ShmFd   *s = da_handle(h);
    uint32_t i;

    if (s == NULL) return;
    if (s->rif > 0) s->rif--;
    if (s->rif > 0) return;
    for (i = 0; i < s->pagine; i++) pmm_free_page(s->fisico[i]);
    s->pagine = 0;
    s->byte   = 0;
    s->usato  = 0;
    /* `fisico` stays, empty: the next object made in this slot reuses it */
}

void shmfd_chiudi(int h)
{
    interrupts_disable();
    shmfd_chiudi_locked(h);
    interrupts_enable();
}

uint32_t shmfd_byte(int h)
{
    ShmFd   *s;
    uint32_t b;

    interrupts_disable();
    s = da_handle(h);
    b = s ? s->byte : 0;
    interrupts_enable();
    return b;
}

/* ! THE PAGES ARE TAKEN WITH INTERRUPTS ON, and put in with them off: if
 * another thread changed the size meanwhile, everything taken goes back and
 * we look again. Two ftruncate on one object at once are rare; a page lost
 * or counted twice would not be. */
int32_t shmfd_misura(int h, uint32_t byte)
{
    uint32_t vuole = (uint32_t)(ALIGN_UP((uint64_t)byte, PAGE_SIZE) / PAGE_SIZE);

    for (;;) {
        ShmFd    *s;
        uint32_t  ha, posti, i, servono;
        paddr_t  *lista = NULL, *vecchia = NULL;

        interrupts_disable();
        s = da_handle(h);
        if (s == NULL) { interrupts_enable(); return ERR(EBADF); }
        ha = s->pagine;
        posti = s->posti;
        if (vuole <= ha) {              /* smaller, or the same */
            for (i = vuole; i < ha; i++) pmm_free_page(s->fisico[i]);
            s->pagine = vuole;
            s->byte   = byte;
            interrupts_enable();
            return 0;
        }
        interrupts_enable();

        servono = vuole - ha;
        if (vuole > posti) {
            uint32_t n = posti * 2u > vuole ? posti * 2u : vuole;

            lista = (paddr_t *)kmalloc(n * sizeof(paddr_t));
            if (lista == NULL) return ERR(ENOMEM);
            posti = n;
        }
        {
            paddr_t *nuove = (paddr_t *)kmalloc(servono * sizeof(paddr_t));

            if (nuove == NULL) { if (lista) kfree(lista); return ERR(ENOMEM); }
            for (i = 0; i < servono; i++) {
                nuove[i] = pmm_alloc_page_utente();
                if (nuove[i] == 0) {
                    while (i-- > 0) pmm_free_page(nuove[i]);
                    kfree(nuove);
                    if (lista) kfree(lista);
                    klog(LOG_WARN, "SHMFD: niente RAM per %u pagine", servono);
                    return ERR(ENOMEM);
                }
                paging_azzera_fisica(nuove[i]);
            }

            interrupts_disable();
            s = da_handle(h);
            if (s == NULL || s->pagine != ha) {     /* changed: again */
                interrupts_enable();
                for (i = 0; i < servono; i++) pmm_free_page(nuove[i]);
                kfree(nuove);
                if (lista) kfree(lista);
                if (s == NULL) return ERR(EBADF);
                continue;
            }
            if (lista) {
                for (i = 0; i < ha; i++) lista[i] = s->fisico[i];
                vecchia   = s->fisico;
                s->fisico = lista;
                s->posti  = posti;
            }
            for (i = 0; i < servono; i++) s->fisico[ha + i] = nuove[i];
            s->pagine = vuole;
            s->byte   = byte;
            interrupts_enable();
            kfree(nuove);
            if (vecchia) kfree(vecchia);
            return 0;
        }
    }
}

int32_t shmfd_mappa(int h, Process *proc, uint32_t vaddr, uint32_t pagine,
                    uint32_t da, uint32_t pg_flags)
{
    ShmFd   *s;
    paddr_t *t;
    uint32_t i;

    if (pagine == 0) return ERR(EINVAL);
    t = (paddr_t *)kmalloc(pagine * sizeof(paddr_t));
    if (t == NULL) return ERR(ENOMEM);

    /* the references first, under the lock: a shrink in another thread
     * cannot give these pages back while we map them */
    interrupts_disable();
    s = da_handle(h);
    if (s == NULL || da > s->pagine || pagine > s->pagine - da) {
        interrupts_enable();
        kfree(t);
        return s ? ERR(EINVAL) : ERR(EBADF);
    }
    for (i = 0; i < pagine; i++) {
        t[i] = s->fisico[da + i];
        pmm_ref_inc(t[i]);
    }
    interrupts_enable();

    for (i = 0; i < pagine; i++) {
        if (paging_map_page(proc->page_directory, vaddr + i * PAGE_SIZE, t[i],
                            pg_flags | PG_CONDIVISA) != 0) {
            uint32_t j;

            for (j = 0; j < i; j++)
                paging_unmap_page(proc->page_directory, vaddr + j * PAGE_SIZE);
            for (j = 0; j < pagine; j++) pmm_free_page(t[j]);
            kfree(t);
            return ERR(ENOMEM);
        }
    }
    kfree(t);
    return 0;
}
