/* =============================================================================
 * kernel/mm/filecache.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * THE PAGES OF A FILE IN ONE COPY FOR THE WHOLE MACHINE (kernel 0.369,
 * debug/bug_FIREFOX001 3.2, voce 34)
 *
 * Until 0.368 every page of an executable loaded on demand (pf_carica_da_file)
 * was read into a frame of that process alone. Firefox is ONE executable of
 * 277 MB with libxul inside, 8-11 processes of it: 16-20 MB of the same code
 * copied in every one, and every new process read it again from the disk
 * with the VFS lock in hand. condivise.c does this for the .so since 0.351,
 * but reads the whole object at once and only for ET_DYN.
 *
 * Here a page is read the first time somebody touches it, kept with one
 * reference of the cache, and every later fault of any process on the same
 * page of the same file gets the same frame, one owner more (PMM count).
 *
 * ! ONLY WHOLE PAGES OF READ-ONLY MAPPINGS. The caller decides the second;
 * the first is here: the last partial page of a file has zeros past the end
 * that belong to nobody's file, and goes private.
 *
 * ! THE KEY IS THE FILE, NOT ITS NAME (the rule of condivise.c): VFS identity,
 * size and mtime. A netupdate that replaces the file makes a new entry; the
 * old one leaves at once, and its frames live on only in whoever maps them.
 *
 * ! NO READ AHEAD (FC_AVANTI = 1), MEASURED. 16 pages per vfs_read made
 * Firefox take 90 MB MORE than one page at a time (tre-schede, 25/11:
 * 1269/1400 MB against 1203/1341): the pages read ahead and never touched
 * sat twice in RAM, in this cache and in the disk cache under it. The disk
 * cache already reads ahead for the disk's sake; this one keeps only what a
 * program has touched. FC_AVANTI > 1 still works, for whoever measures it
 * better (a machine without disk cache, cachedisco = 0).
 *
 * ! THE CACHE GIVES BACK. A frame only the cache holds (count 1: nobody maps
 * it) can be read again from the file; fc_cedi frees those when the RAM runs
 * out (pagina_utente, after the disk cache). An entry left with no frame and
 * no user goes too.
 *
 * ! NO CEILING (the user's rule): the entries are a list that doubles when
 * full, the pages of an entry a vector as long as the file.
 *
 * Locking: the list and the vectors change with interrupts off; vfs_read and
 * pagina_utente run with them on (they sleep). An entry being worked on has
 * `usi` > 0 and is never freed under its user: taken out of the list it is
 * only marked `fuori`, and the last user frees it.
 * ============================================================================= */

#include "kernel.h"
#include "vfs.h"
#include "pmm.h"
#include "paging.h"
#include "kmalloc.h"
#include "filecache.h"

#define FC_AVANTI  1u           /* pages read at once: see above */

typedef struct {
    uint32_t  ident, dim;
    int64_t   mtime;
    uint32_t  n;                /* whole pages of the file */
    paddr_t  *f;                /* frame per page, 0 = not read */
    uint32_t  tenute;           /* frames in f */
    int       usi;              /* calls working on it right now */
    int       fuori;            /* out of the list: freed at usi == 0 */
} FcVoce;

static FcVoce  **g_fc;
static uint32_t  g_fc_n, g_fc_posti;
static uint32_t  g_fc_presi, g_fc_letti, g_fc_detti, g_fc_ceduti;

/* Why fc_pagina gave up, said the first time of each (0.369 tests) */
static uint32_t g_fc_perche;
static void fc_rinuncia(uint32_t motivo, const char *perche, int h, uint32_t idx, int v)
{
    if (g_fc_perche & (1u << motivo)) return;
    g_fc_perche |= 1u << motivo;
    klog(LOG_WARN, "FC: rinuncio (%s): handle %d pagina %u valore %d", perche, h, idx, v);
}

static uintptr_t fc_chiudi(void)
{
    uintptr_t fl;

    __asm__ volatile ("pushf; pop %0" : "=r"(fl));
    __asm__ volatile ("cli");
    return fl;
}

static void fc_riapri(uintptr_t fl)
{
    if (fl & (1u << 9)) __asm__ volatile ("sti");
}

static void fc_libera(FcVoce *v)
{
    uint32_t i;

    for (i = 0; i < v->n; i++)
        if (v->f[i]) pmm_free_page(v->f[i]);
    kfree(v->f);
    kfree(v);
}

/* Out of the list (interrupts off). Freed now if nobody works on it. */
static void fc_togli(uint32_t i)
{
    FcVoce *v = g_fc[i];

    g_fc[i] = g_fc[--g_fc_n];
    if (v->usi > 0) { v->fuori = 1; return; }
    fc_libera(v);
}

static FcVoce *fc_cerca(const FcChiave *st)
{
    uint32_t i = 0;

    while (i < g_fc_n) {
        FcVoce *v = g_fc[i];

        if (v->ident != st->ident) { i++; continue; }
        if (v->dim == st->dim && v->mtime == st->mtime) return v;
        fc_togli(i);            /* the same file, changed */
    }
    return NULL;
}

/* The entry for `st`, made if missing, with usi + 1. NULL if no memory. */
static FcVoce *fc_prendi(const FcChiave *st)
{
    uintptr_t fl = fc_chiudi();
    FcVoce   *v  = fc_cerca(st), *nuova;
    FcVoce  **lista = NULL;
    uint32_t  n = st->dim / PAGE_SIZE, i, posti = 0;

    if (v) { v->usi++; fc_riapri(fl); return v; }
    fc_riapri(fl);

    /* kmalloc outside: the list may have changed when we are back */
    nuova = (FcVoce *)kmalloc(sizeof(FcVoce));
    if (nuova == NULL) return NULL;
    nuova->f = (paddr_t *)kmalloc((n ? n : 1) * sizeof(paddr_t));
    if (nuova->f == NULL) { kfree(nuova); return NULL; }
    for (i = 0; i < n; i++) nuova->f[i] = 0;
    nuova->ident = st->ident; nuova->dim = st->dim; nuova->mtime = st->mtime;
    nuova->n = n; nuova->tenute = 0; nuova->usi = 1; nuova->fuori = 0;
    if (g_fc_n + 1 > g_fc_posti) {
        posti = g_fc_posti ? g_fc_posti * 2 : 16;
        lista = (FcVoce **)kmalloc(posti * sizeof(FcVoce *));
        if (lista == NULL) { kfree(nuova->f); kfree(nuova); return NULL; }
    }

    fl = fc_chiudi();
    v = fc_cerca(st);
    if (v) {                    /* somebody else made it meanwhile */
        v->usi++;
        fc_riapri(fl);
        kfree(nuova->f); kfree(nuova);
        if (lista) kfree(lista);
        return v;
    }
    if (g_fc_n + 1 > g_fc_posti) {
        if (lista == NULL || posti < g_fc_n + 1) {      /* grew meanwhile */
            fc_riapri(fl);
            kfree(nuova->f); kfree(nuova);
            if (lista) kfree(lista);
            return NULL;
        }
        for (i = 0; i < g_fc_n; i++) lista[i] = g_fc[i];
        if (g_fc) kfree(g_fc);
        g_fc = lista; g_fc_posti = posti; lista = NULL;
    }
    g_fc[g_fc_n++] = nuova;
    fc_riapri(fl);
    if (lista) kfree(lista);
    return nuova;
}

static void fc_lascia(FcVoce *v)
{
    uintptr_t fl = fc_chiudi();

    if (--v->usi == 0 && v->fuori) fc_libera(v);
    fc_riapri(fl);
}

static void fc_conta(void)
{
    if (g_fc_presi + g_fc_letti - g_fc_detti < 4096u) return;
    g_fc_detti = g_fc_presi + g_fc_letti;
    klog(LOG_INFO, "FC: pagine di file riusate %u (%u MB), lette %u (%u MB), "
         "rese alla RAM %u", g_fc_presi, g_fc_presi / 256u, g_fc_letti,
         g_fc_letti / 256u, g_fc_ceduti);
}

int fc_chiave(int h, FcChiave *k)
{
    VfsStat st;
    int     r;

    if (h < 0 || k == NULL) return -1;
    r = vfs_fstat(h, &st);
    if (r != 0) { fc_rinuncia(1, "fstat", h, 0, r); return r; }
    if (st.ident == 0) { fc_rinuncia(2, "identita' 0", h, 0, 0); return -1; }
    k->ident = st.ident; k->dim = st.dimensione; k->mtime = st.mtime;
    return 0;
}

/* ! NO VFS HERE (0.371): a page already in the cache is a lookup with the
 * interrupts off. Until 0.370 every fault went through vfs_fstat, and the
 * VFS lock: a fault on a page in RAM waited behind somebody's write to the
 * disk (Firefox's profile, about a second each). */
paddr_t fc_c_e(const FcChiave *k, uint32_t idx)
{
    uintptr_t fl = fc_chiudi();
    FcVoce   *v  = fc_cerca(k);
    paddr_t   fr = 0;

    if (v && idx < v->n && v->f[idx] && pmm_ref_inc(v->f[idx]) == 0) {
        fr = v->f[idx];
        g_fc_presi++;
    }
    fc_riapri(fl);
    return fr;
}

paddr_t fc_leggi(const FcChiave *k, int h, uint32_t idx, uint32_t a, uint32_t z)
{
    FcVoce   *v;
    paddr_t   fr = 0;
    uintptr_t fl;
    uint8_t  *buf;
    uint32_t  da, fino, j, pagine;
    int       letti;

    if (!(g_fc_perche & (1u << 31))) {
        g_fc_perche |= 1u << 31;
        klog(LOG_WARN, "FC: prima pagina chiesta: handle %d pagina %u", h, idx);
    }
    if (h < 0) { fc_rinuncia(0, "handle", h, idx, 0); return 0; }
    if (idx >= k->dim / PAGE_SIZE) return 0;    /* not a whole page: the normal case */
    v = fc_prendi(k);
    if (v == NULL) { fc_rinuncia(4, "voce", h, idx, (int)k->dim); return 0; }

    fl = fc_chiudi();
    if (v->f[idx]) {
        fr = v->f[idx];
        if (pmm_ref_inc(fr) != 0) { fc_rinuncia(7, "ref_inc", h, idx, 0); fr = 0; }
        else g_fc_presi++;
    }
    fc_riapri(fl);
    if (fr) { fc_lascia(v); fc_conta(); return fr; }

    /* the run of missing pages around idx, inside [a, z): one disk request */
    if (a > idx) a = idx;
    if (z > v->n) z = v->n;
    if (z <= idx) z = idx + 1;
    for (da = idx; da > a && v->f[da - 1] == 0; da--) ;
    for (fino = idx + 1; fino < z && v->f[fino] == 0; fino++) ;
    buf = (uint8_t *)kmalloc((fino - da) * PAGE_SIZE);
    if (buf == NULL && fino - da > 1) { da = idx; fino = idx + 1; buf = (uint8_t *)kmalloc(PAGE_SIZE); }
    if (buf == NULL) { fc_rinuncia(5, "buffer", h, idx, 0); fc_lascia(v); return 0; }

    fl = fc_chiudi();
    __asm__ volatile ("sti");
    letti = vfs_read(h, buf, (fino - da) * PAGE_SIZE, da * PAGE_SIZE);
    pagine = letti > 0 ? (uint32_t)letti / PAGE_SIZE : 0;

    for (j = 0; j < pagine; j++) {
        paddr_t  p = paging_pagina_utente();
        uint8_t *dst;
        uint32_t b;

        if (p == 0) break;      /* the RAM: what we have is enough */
        dst = (uint8_t *)paging_finestra_apri(p);
        for (b = 0; b < PAGE_SIZE; b++) dst[b] = buf[j * PAGE_SIZE + b];
        paging_finestra_chiudi();

        __asm__ volatile ("cli");
        if (v->f[da + j] == 0) { v->f[da + j] = p; v->tenute++; g_fc_letti++; p = 0; }
        __asm__ volatile ("sti");
        if (p) pmm_free_page(p);        /* another fault put it there first */
    }
    __asm__ volatile ("cli");
    if (v->f[idx] && pmm_ref_inc(v->f[idx]) == 0) fr = v->f[idx];
    if (!(fl & (1u << 9))) __asm__ volatile ("cli"); else __asm__ volatile ("sti");
    if (fr == 0) fc_rinuncia(6, "lettura o RAM", h, idx, letti);
    kfree(buf);
    fc_lascia(v);
    fc_conta();
    return fr;
}

paddr_t fc_pagina(int h, uint32_t idx)
{
    FcChiave k;

    if (h < 0) { fc_rinuncia(0, "handle", h, idx, 0); return 0; }
    if (fc_chiave(h, &k) != 0) return 0;
    return fc_leggi(&k, h, idx, idx, idx + FC_AVANTI);
}

paddr_t fc_pagina_privata(int h, uint32_t idx)
{
    VfsStat   st;
    uint8_t  *buf;
    uint8_t  *dst;
    paddr_t   p;
    uint32_t  n = 0, b;
    uintptr_t fl;
    int       letti = 0;

    if (h < 0 || vfs_fstat(h, &st) != 0) return 0;
    if ((uint64_t)idx * PAGE_SIZE < st.dimensione) {
        n = st.dimensione - idx * PAGE_SIZE;
        if (n > PAGE_SIZE) n = PAGE_SIZE;
    }
    buf = (uint8_t *)kmalloc(PAGE_SIZE);
    if (buf == NULL) return 0;
    if (n) {
        fl = fc_chiudi();
        __asm__ volatile ("sti");
        letti = vfs_read(h, buf, n, idx * PAGE_SIZE);
        if (!(fl & (1u << 9))) __asm__ volatile ("cli");
        /* ! A SHORT READ IS A FAILURE (pf_carica_da_file, voce 59) */
        if (letti < 0 || (uint32_t)letti < n) { kfree(buf); return 0; }
    }
    p = paging_pagina_utente();
    if (p != 0) {
        dst = (uint8_t *)paging_finestra_apri(p);
        for (b = 0; b < n; b++) dst[b] = buf[b];
        for (; b < PAGE_SIZE; b++) dst[b] = 0;
        paging_finestra_chiudi();
    }
    kfree(buf);
    return p;
}

uint32_t fc_cedi(uint32_t pagine)
{
    uintptr_t fl = fc_chiudi();
    uint32_t  dati = 0, i = 0, k;

    while (i < g_fc_n && dati < pagine) {
        FcVoce *v = g_fc[i];

        for (k = 0; k < v->n && dati < pagine; k++) {
            if (v->f[k] == 0 || pmm_ref_count(v->f[k]) != 1) continue;
            pmm_free_page(v->f[k]);
            v->f[k] = 0;
            v->tenute--;
            dati++;
        }
        if (v->tenute == 0 && v->usi == 0) { fc_togli(i); continue; }
        i++;
    }
    g_fc_ceduti += dati;
    fc_riapri(fl);
    return dati;
}
