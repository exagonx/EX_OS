/* =============================================================================
 * kernel/loader/condivise.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * THE SHARED OBJECTS IN ONE COPY FOR THE WHOLE MACHINE (kernel 0.351, voce 105)
 *
 * An ET_DYN (/lib/ld-exos.so, every .so of dlopen and DT_NEEDED) is code
 * that does not care where it sits. Up to 0.350 each process read it into
 * its own anonymous pages: the shared libc cost 270 KB per process, a copy
 * of the same bytes in every one. Here the file is read ONCE into a cache,
 * and a process that maps it gets:
 *
 *     read-only pages    THE CACHE'S OWN FRAMES, one owner more (PMM count),
 *                        at whatever base this process has room for: PIC
 *                        code reads its data through the GOT, so the same
 *                        frame works at any address
 *     writable pages     a fresh copy of the cache's frame (.data, the GOT;
 *                        the relocations of this process go there)
 *     pure .bss pages    a promise (PG_PIGRA): zero at the first touch
 *
 * The same idea as kernel/loader/lib.c, the cache of the EX-OS libraries at
 * fixed addresses, without the fixed address. A page where a read-only and a
 * writable segment meet counts as writable: it is copied, never shared.
 *
 * ! NOTHING BECOMES WRITABLE BEHIND THE CACHE'S BACK. mprotect on a shared
 * frame makes it PG_COW (paging_proteggi), so even a TEXTREL object would get
 * its own copy at the first write. None of the 158 .so of the ports has one.
 *
 * ! THE KEY IS THE FILE, NOT ITS NAME: the VFS identity (unique in the whole
 * system, mount included), the size and the mtime. A netupdate that replaces
 * a library makes a new entry; the old one leaves the cache and its frames
 * live on only in the processes that still have them (the PMM counts them).
 *
 * ! NO CEILING. The entries are a list that doubles when it is full, the
 * pages of an entry a vector as long as the object.
 * ============================================================================= */

#include "kernel.h"
#include "elf.h"
#include "vfs.h"
#include "pmm.h"
#include "paging.h"
#include "sched.h"
#include "syscall.h"
#include "kmalloc.h"
#include "condivise.h"

typedef struct {
    paddr_t  fisico;    /* the cache's frame; 0 = no file bytes (a promise) */
    uint32_t pg;        /* PG_USER | maybe PG_WRITABLE; 0 = not covered */
} SoPagina;

#define SO_PRONTA     1
#define SO_IN_CARICO  2

typedef struct {
    int       stato;
    int       usi;          /* processes mapping it right now */
    int       fuori;        /* out of the list: freed when usi is back to 0 */
    uint32_t  ident;
    uint32_t  dimensione;
    int64_t   mtime;
    uint32_t  lo;           /* lowest PT_LOAD page, as in the file */
    uint32_t  entry;        /* e_entry, as in the file */
    uint32_t  n;            /* pages from lo */
    SoPagina *pag;
    char      nome[48];     /* for the log only */
} SoVoce;

static SoVoce  **g_so;
static uint32_t  g_so_n;
static uint32_t  g_so_posti;

static void so_nome(char *dst, const char *src)
{
    uint32_t i = 0, k = 0;

    if (src == NULL) src = "?";
    while (src[i]) i++;
    /* the tail is the interesting part: /exwin/lib/<name> */
    if (i > 47) src += i - 47;
    for (; k < 47 && src[k]; k++) dst[k] = src[k];
    dst[k] = '\0';
}

static void so_libera(SoVoce *v)
{
    uint32_t i;

    for (i = 0; v->pag && i < v->n; i++)
        if (v->pag[i].fisico) pmm_free_page(v->pag[i].fisico);
    if (v->pag) kfree(v->pag);
    kfree(v);
}

/* Takes `v` out of the list. Its frames go back now if nobody is mapping it,
 * otherwise when the last one has finished (so_mappa). */
static void so_togli(SoVoce *v)
{
    uint32_t i;

    for (i = 0; i < g_so_n; i++) {
        if (g_so[i] != v) continue;
        g_so[i] = g_so[--g_so_n];
        break;
    }
    if (v->usi > 0) { v->fuori = 1; return; }
    so_libera(v);
}

static int so_metti(SoVoce *v)
{
    if (g_so_n == g_so_posti) {
        uint32_t  posti = g_so_posti ? g_so_posti * 2 : 16;
        SoVoce  **nuovo = (SoVoce **)kmalloc(posti * sizeof(SoVoce *));
        uint32_t  i;

        if (nuovo == NULL) return -1;
        for (i = 0; i < g_so_n; i++) nuovo[i] = g_so[i];
        if (g_so) kfree(g_so);
        g_so = nuovo;
        g_so_posti = posti;
    }
    g_so[g_so_n++] = v;
    return 0;
}

/* The window is opened and closed around the copy alone, never around a
 * vfs_read: that one sleeps in IPC, and somebody else would move the window
 * under our feet (the same rule as elf.c and lib.c). */
static void so_scrivi_fisica(paddr_t phys, uint32_t off, const uint8_t *src, uint32_t n)
{
    uint8_t *dst = (uint8_t *)paging_finestra_apri(phys);
    uint32_t k;

    for (k = 0; k < n; k++) dst[off + k] = src[k];
    paging_finestra_chiudi();
}

/* Reads the object open on `h` into `v`. 0 or -errno, with a log line. */
static int32_t so_leggi(int h, SoVoce *v)
{
    ElfHeader hdr;
    ElfPhdr  *ph  = NULL;
    uint8_t  *buf = NULL;
    uint32_t  i, lo = 0xFFFFFFFFu, hi = 0, misura;
    int32_t   rc = ERR(ENOEXEC);

    if (vfs_read(h, &hdr, sizeof(hdr), 0) != (int)sizeof(hdr) ||
        hdr.e_ident[EI_MAG0] != ELF_MAGIC0 || hdr.e_ident[EI_MAG1] != ELF_MAGIC1 ||
        hdr.e_ident[EI_MAG2] != ELF_MAGIC2 || hdr.e_ident[EI_MAG3] != ELF_MAGIC3 ||
        hdr.e_ident[EI_CLASS] != ELF_CLASSE || hdr.e_type != ET_DYN ||
        hdr.e_machine != ELF_MACCHINA || hdr.e_phnum == 0 ||
        hdr.e_phentsize != sizeof(ElfPhdr)) {
        klog(LOG_ERROR, "SO: '%s' non e' un " ELF_NOME " ET_DYN", v->nome);
        return rc;
    }
    misura = (uint32_t)hdr.e_phnum * sizeof(ElfPhdr);
    ph  = (ElfPhdr *)kmalloc(misura);
    buf = (uint8_t *)kmalloc(PAGE_SIZE);
    if (ph == NULL || buf == NULL) { rc = ERR(ENOMEM); goto fuori; }
    if (vfs_read(h, ph, misura, (uint32_t)hdr.e_phoff) != (int)misura) {
        klog(LOG_ERROR, "SO: '%s': program header illeggibili", v->nome);
        goto fuori;
    }

    for (i = 0; i < hdr.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) continue;
        if (ph[i].p_filesz > ph[i].p_memsz ||
            ph[i].p_memsz > USER_SPACE_END - USER_SPACE_BASE ||
            ph[i].p_vaddr > USER_SPACE_END - USER_SPACE_BASE - ph[i].p_memsz) {
            klog(LOG_ERROR, "SO: '%s': segmento %u fuori misura", v->nome, i);
            goto fuori;
        }
        if ((uint32_t)ph[i].p_vaddr < lo) lo = (uint32_t)ph[i].p_vaddr;
        if ((uint32_t)(ph[i].p_vaddr + ph[i].p_memsz) > hi) hi = (uint32_t)(ph[i].p_vaddr + ph[i].p_memsz);
    }
    if (hi == 0) { klog(LOG_ERROR, "SO: '%s': nessun PT_LOAD", v->nome); goto fuori; }
    lo = ALIGN_DOWN(lo, PAGE_SIZE);
    hi = ALIGN_UP(hi, PAGE_SIZE);

    v->lo    = lo;
    v->entry = (uint32_t)hdr.e_entry;
    v->n     = (hi - lo) / PAGE_SIZE;
    v->pag   = (SoPagina *)kmalloc(v->n * sizeof(SoPagina));
    if (v->pag == NULL) { rc = ERR(ENOMEM); goto fuori; }
    for (i = 0; i < v->n; i++) { v->pag[i].fisico = 0; v->pag[i].pg = 0; }

    /* 1. which pages, with which rights, and which ones hold file bytes */
    for (i = 0; i < hdr.e_phnum; i++) {
        uint32_t a, z, f, p;

        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) continue;
        a = ALIGN_DOWN((uint32_t)ph[i].p_vaddr, PAGE_SIZE);
        z = ALIGN_UP((uint32_t)(ph[i].p_vaddr + ph[i].p_memsz), PAGE_SIZE);
        f = (uint32_t)(ph[i].p_vaddr + ph[i].p_filesz);
        for (p = a; p < z; p += PAGE_SIZE) {
            SoPagina *P = &v->pag[(p - lo) / PAGE_SIZE];

            P->pg |= PG_USER;
            if (ph[i].p_flags & PF_W) P->pg |= PG_WRITABLE;
            if (ph[i].p_filesz && p < f && P->fisico == 0) {
                /* ! ZEROED BEFORE THE BYTES GO IN: the tail of the last
                 * page is .bss, and it would hold somebody else's memory */
                P->fisico = pmm_alloc_page_utente();
                if (P->fisico == 0) { rc = ERR(ENOMEM); goto fuori; }
                paging_azzera_fisica(P->fisico);
            }
        }
    }

    /* 2. the file bytes, one piece per destination page */
    for (i = 0; i < hdr.e_phnum; i++) {
        uint32_t va, off, resta;

        if (ph[i].p_type != PT_LOAD || ph[i].p_filesz == 0) continue;
        va    = (uint32_t)ph[i].p_vaddr;
        off   = (uint32_t)ph[i].p_offset;
        resta = (uint32_t)ph[i].p_filesz;
        while (resta > 0) {
            uint32_t pezzo = PAGE_SIZE - (va & (PAGE_SIZE - 1));
            int      letti;

            if (pezzo > resta) pezzo = resta;
            letti = vfs_read(h, buf, pezzo, off);
            if (letti != (int)pezzo) {
                klog(LOG_ERROR, "SO: '%s': lettura fallita a 0x%x", v->nome, off);
                rc = ERR(EIO);
                goto fuori;
            }
            so_scrivi_fisica(v->pag[(ALIGN_DOWN(va, PAGE_SIZE) - lo) / PAGE_SIZE].fisico,
                             va & (PAGE_SIZE - 1), buf, pezzo);
            va += pezzo; off += pezzo; resta -= pezzo;
        }
    }
    rc = 0;
fuori:
    if (ph)  kfree(ph);
    if (buf) kfree(buf);
    return rc;
}

/* The entry for the file open on `h`: from the cache, or read now. NULL with
 * *rc set if it cannot be had. The caller holds one `usi`. */
static SoVoce *so_prendi(int h, const char *nome, int32_t *rc)
{
    VfsStat  st;
    SoVoce  *v;
    uint32_t i;

    if (vfs_fstat(h, &st) != 0) { *rc = ERR(EBADF); return NULL; }
cerca:
    for (i = 0; i < g_so_n; i++) {
        v = g_so[i];
        if (v->ident != st.ident) continue;
        if (v->dimensione == st.dimensione && v->mtime == st.mtime) {
            /* ! SOMEBODY IS READING IT FROM THE DISK: wait for them instead of
             * reading a second copy (lib.c learnt it the hard way, @NAV-32MB) */
            if (v->stato == SO_IN_CARICO) { sched_yield(); goto cerca; }
            v->usi++;
            return v;
        }
        /* the same file, changed: the old copy leaves the cache */
        if (v->stato == SO_IN_CARICO) { sched_yield(); goto cerca; }
        so_togli(v);
        goto cerca;
    }

    v = (SoVoce *)kmalloc(sizeof(SoVoce));
    if (v == NULL) { *rc = ERR(ENOMEM); return NULL; }
    v->stato = SO_IN_CARICO;
    v->usi = 1;
    v->fuori = 0;
    v->ident = st.ident;
    v->dimensione = st.dimensione;
    v->mtime = st.mtime;
    v->lo = v->entry = v->n = 0;
    v->pag = NULL;
    so_nome(v->nome, nome);
    /* ! AN IDENTITY OF 0 IS NO IDENTITY: such an object is read for this
     * process alone and never enters the list */
    if (st.ident != 0 && so_metti(v) != 0) { kfree(v); *rc = ERR(ENOMEM); return NULL; }

    *rc = so_leggi(h, v);
    if (*rc != 0) {
        v->usi = 0;
        if (st.ident != 0) so_togli(v); else so_libera(v);
        return NULL;
    }
    if (st.ident == 0) v->fuori = 1;
    v->stato = SO_PRONTA;
    klog(LOG_INFO, "SO: '%s' nella cache: %u pagine", v->nome, v->n);
    return v;
}

static void so_lascia(SoVoce *v)
{
    if (--v->usi == 0 && v->fuori) so_libera(v);
}

/* Maps `v` into `proc` with its lowest page at `inizio`. Nothing here sleeps
 * on the disk: every byte is in the cache already. */
static int32_t so_aggancia(SoVoce *v, Process *proc, uint32_t inizio)
{
    PDE     *pd = proc->page_directory;
    uint32_t i;

    for (i = 0; i < v->n; i++) {
        SoPagina *P  = &v->pag[i];
        uint32_t  va = inizio + i * PAGE_SIZE;
        paddr_t   f;

        if (P->pg == 0) continue;                       /* a hole between segments */
        if (P->pg & PG_WRITABLE) {
            if (P->fisico == 0) {
                if (paging_pigra(pd, va, P->pg) != 0) goto senza;
                continue;
            }
            f = pmm_alloc_page_utente();
            if (f == 0) goto senza;
            paging_copia_fisica(P->fisico, f);
            if (paging_map_page(pd, va, f, PG_PRESENT | P->pg) != 0) { pmm_free_page(f); goto senza; }
            continue;
        }
        if (P->fisico == 0) {
            /* read-only and no file bytes: a zero page of its own, read-only */
            f = pmm_alloc_page_utente();
            if (f == 0) goto senza;
            paging_azzera_fisica(f);
        } else {
            /* ! THE SAME FRAME, one owner more: this is the point of the file */
            f = P->fisico;
            if (pmm_ref_inc(f) != 0) goto senza;
        }
        if (paging_map_page(pd, va, f, PG_PRESENT | P->pg) != 0) { pmm_free_page(f); goto senza; }
    }
    return 0;

senza:
    /* ! WHAT WAS MAPPED COMES DOWN AGAIN: half an object would fault on a
     * function that is not there, far from the real cause */
    while (i-- > 0) {
        uint32_t va = inizio + i * PAGE_SIZE;
        paddr_t  f  = paging_get_physical(pd, va);

        if (v->pag[i].pg == 0) continue;
        paging_unmap_page(pd, va);
        if (f) pmm_free_page(f & ~(paddr_t)(PAGE_SIZE - 1));
    }
    return ERR(ENOMEM);
}

int32_t so_mappa(int h, const char *nome, Process *proc, uint32_t dove, SoEsito *out)
{
    SoVoce  *v;
    uint32_t inizio, misura;
    int32_t  rc;

    if (proc == NULL || out == NULL) return ERR(EINVAL);
    v = so_prendi(h, nome, &rc);
    if (v == NULL) return rc;
    misura = v->n * PAGE_SIZE;

    if (dove != 0) {
        /* the interpreter: under the stack's reserve, never into it */
        uint32_t lim = proc->user_stack_limit ? proc->user_stack_limit : USER_SPACE_END;

        if (dove < USER_SPACE_BASE || dove > lim || v->n > (lim - dove) / PAGE_SIZE) {
            klog(LOG_ERROR, "SO: '%s' (%u pagine) non sta a 0x%08x", v->nome, v->n, dove);
            so_lascia(v);
            return ERR(ENOMEM);
        }
        inizio = dove;
    } else {
        /* the same space as mmap without MAP_FIXED, and the same ceiling */
        inizio = ALIGN_UP(proc->heap_end, PAGE_SIZE);
        if (inizio < USER_SPACE_BASE) inizio = USER_SPACE_BASE;
        if (proc->heap_max == 0 || inizio > proc->heap_max ||
            v->n > (proc->heap_max - inizio) / PAGE_SIZE) {
            klog(LOG_WARN, "SO: PID %u: '%s' (%u pagine) non sta sotto 0x%08x",
                 proc->pid, v->nome, v->n, proc->heap_max);
            so_lascia(v);
            return ERR(ENOMEM);
        }
    }

    rc = so_aggancia(v, proc, inizio);
    if (rc == 0) {
        if (dove == 0) proc->heap_end = inizio + misura;
        out->base   = inizio - v->lo;
        out->inizio = inizio;
        out->misura = misura;
        out->entry  = inizio - v->lo + v->entry;
    }
    so_lascia(v);
    return rc;
}
