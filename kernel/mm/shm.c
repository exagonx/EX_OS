/* =============================================================================
 * kernel/mm/shm.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Memoria condivisa fra processi. Il perche', la forma dell'interfaccia e cio'
 * che questo file NON fa stanno in kernel/include/shm.h: qui c'e' come.
 * ============================================================================= */

#include "shm.h"
#include "pmm.h"
#include "paging.h"
#include "kmalloc.h"
#include "syscall.h"    /* i codici di errore e la macro ERR, in un posto solo */

static ShmZonaK g_zone[SHM_MAX_ZONE];

/* -----------------------------------------------------------------------------
 * Nomi
 * --------------------------------------------------------------------------- */
static int nome_uguale(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < SHM_NOME_LEN; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == '\0') return 1;
    }
    return 1;
}

/* Rende la lunghezza, o 0 se il nome non e' utilizzabile. */
static uint32_t nome_valido(const char *n)
{
    uint32_t i;

    if (n == NULL || n[0] == '\0') return 0;

    for (i = 0; i < SHM_NOME_LEN; i++) {
        if (n[i] == '\0') return i;
        /* Niente caratteri di controllo: un nome che contiene un ritorno a
         * capo esce nei log spezzando la riga, e diventa un modo di scrivere
         * righe di log false. */
        if ((unsigned char)n[i] < 0x20) return 0;
    }
    return 0;                       /* non terminato entro SHM_NOME_LEN */
}

static int32_t cerca_nome(const char *nome)
{
    uint32_t i;
    for (i = 0; i < SHM_MAX_ZONE; i++)
        if (g_zone[i].proc > 0 && nome_uguale(g_zone[i].nome, nome))
            return (int32_t)i;
    return -1;
}

/* -----------------------------------------------------------------------------
 * Creazione e distruzione di una zona
 * --------------------------------------------------------------------------- */

/* ! LE PAGINE SI AZZERANO, e non e' cortesia. Una pagina appena tolta al PMM
 * contiene quello che ci aveva scritto il processo di prima: consegnarla a due
 * processi che si parlano vuol dire consegnare loro i resti di un terzo. */
static int32_t zona_crea(uint32_t idx, const char *nome, uint32_t pagine)
{
    ShmZonaK *z = &g_zone[idx];
    uint32_t  i;

    z->fisico = (uint32_t *)kmalloc(pagine * sizeof(uint32_t));
    if (z->fisico == NULL) return ERR(ENOMEM);

    for (i = 0; i < pagine; i++) {
        uint32_t f = pmm_alloc_page();
        if (f == 0) {
            uint32_t j;
            for (j = 0; j < i; j++) pmm_free_page(z->fisico[j]);
            kfree(z->fisico);
            z->fisico = NULL;
            return ERR(ENOMEM);
        }
        paging_azzera_fisica(f);
        z->fisico[i] = f;
    }

    for (i = 0; i < SHM_NOME_LEN; i++) {
        z->nome[i] = nome[i];
        if (nome[i] == '\0') break;
    }
    z->nome[SHM_NOME_LEN - 1] = '\0';
    z->pagine = pagine;
    z->proc   = 0;                  /* lo alza chi si aggancia */
    return 0;
}

/* ! QUI LE PAGINE TORNANO DAVVERO AL SISTEMA. Ogni processo agganciato aveva
 * aggiunto un proprietario con pmm_ref_inc e l'ha tolto staccandosi; questa
 * liberazione e' quella del PRIMO proprietario, quello che la zona stessa
 * teneva dal momento in cui e' stata creata. */
static void zona_distruggi(uint32_t idx)
{
    ShmZonaK *z = &g_zone[idx];
    uint32_t  i;

    for (i = 0; i < z->pagine; i++) pmm_free_page(z->fisico[i]);
    kfree(z->fisico);

    klog(LOG_INFO, "SHM: zona '%s' chiusa dall'ultimo processo, %u pagine "
         "restituite", z->nome, z->pagine);

    z->fisico = NULL;
    z->pagine = 0;
    z->nome[0] = '\0';
    z->proc   = 0;
}

/* -----------------------------------------------------------------------------
 * Aggancio e sgancio nello spazio di un processo
 * --------------------------------------------------------------------------- */
static int32_t aggancia(Process *proc, uint32_t idx, uint32_t *out_virt)
{
    ShmZonaK *z = &g_zone[idx];
    uint32_t  vaddr, i, slot;

    /* Uno slot nel PCB. Si prende PRIMA di mappare: fallire dopo aver mappato
     * vorrebbe dire smontare tutto per un motivo che si conosceva gia'. */
    for (slot = 0; slot < SHM_PER_PROC; slot++)
        if (proc->shm[slot].zona == 0) break;
    if (slot == SHM_PER_PROC) return ERR(EMFILE);

    if (proc->heap_max == 0) return ERR(ENOMEM);

    vaddr = ALIGN_UP(proc->heap_end, PAGE_SIZE);
    if (vaddr < USER_SPACE_BASE) vaddr = USER_SPACE_BASE;
    if (vaddr > proc->heap_max ||
        z->pagine > (proc->heap_max - vaddr) / PAGE_SIZE) return ERR(ENOMEM);

    for (i = 0; i < z->pagine; i++) {
        /* ! IL CONTEGGIO PRIMA DELLA MAPPATURA. Se si mappasse per prima e il
         * conteggio fallisse, resterebbe una pagina raggiungibile dal
         * processo e non contata: la prima morte la restituirebbe al PMM
         * mentre l'altro ce l'ha ancora sotto gli occhi. */
        if (pmm_ref_inc(z->fisico[i]) != 0 ||
            paging_map_page(proc->page_directory,
                            vaddr + i * PAGE_SIZE,
                            z->fisico[i],
                            PG_PRESENT | PG_USER | PG_WRITABLE) != 0) {
            uint32_t j;
            for (j = 0; j < i; j++) {
                paging_unmap_page(proc->page_directory, vaddr + j * PAGE_SIZE);
                pmm_free_page(z->fisico[j]);    /* cala il conteggio, non libera */
            }
            return ERR(ENOMEM);
        }
    }

    proc->heap_end = vaddr + z->pagine * PAGE_SIZE;

    proc->shm[slot].zona   = idx + 1;
    proc->shm[slot].virt   = vaddr;
    proc->shm[slot].pagine = z->pagine;
    z->proc++;

    *out_virt = vaddr;
    return 0;
}

/* Stacca lo slot: smonta le pagine dal processo e cala i due conteggi. Se era
 * l'ultimo processo, la zona muore. */
static void sgancia(Process *proc, uint32_t slot, int smonta)
{
    uint32_t  idx = proc->shm[slot].zona - 1;
    ShmZonaK *z   = &g_zone[idx];
    uint32_t  i;

    /* ! `smonta` E' FALSO QUANDO IL PROCESSO E' MORTO, e la differenza non e'
     * un'ottimizzazione: paging_destroy_directory ha gia' smontato ogni cosa e
     * ha gia' chiamato pmm_free_page per ogni pagina presente. Rifarlo qui
     * calerebbe il conteggio DUE volte, e la pagina tornerebbe al PMM mentre
     * un altro processo ce l'ha ancora mappata. */
    if (smonta) {
        for (i = 0; i < proc->shm[slot].pagine; i++) {
            paging_unmap_page(proc->page_directory,
                              proc->shm[slot].virt + i * PAGE_SIZE);
            pmm_free_page(z->fisico[i]);
        }

        /* Se la zona stava in cima, il confine dello heap torna giu'. Stessa
         * regola di munmap, e per la stessa ragione: senza, un ciclo di
         * apri/chiudi si mangia spazio di indirizzamento che non torna. Un
         * buco in mezzo resta un buco, perche' heap_end e' un confine e non
         * una mappa. */
        {
            uint32_t fine = proc->shm[slot].virt +
                            proc->shm[slot].pagine * PAGE_SIZE;
            if (fine == proc->heap_end &&
                proc->shm[slot].virt >= proc->heap_start)
                proc->heap_end = proc->shm[slot].virt;
        }
    }

    proc->shm[slot].zona   = 0;
    proc->shm[slot].virt   = 0;
    proc->shm[slot].pagine = 0;

    if (z->proc > 0) z->proc--;
    if (z->proc == 0) zona_distruggi(idx);
}

/* -----------------------------------------------------------------------------
 * Interfaccia
 * --------------------------------------------------------------------------- */
int32_t shm_apri(Process *proc, const char *nome, uint32_t byte, uint32_t flag,
                 uint32_t *out_virt, uint32_t *out_byte)
{
    int32_t  idx;
    uint32_t pagine, slot;
    int      creata = 0;
    int32_t  rc;

    if (proc == NULL || out_virt == NULL || out_byte == NULL) return ERR(EINVAL);
    if (nome_valido(nome) == 0) return ERR(EINVAL);

    idx = cerca_nome(nome);

    /* Gia' aperta da QUESTO processo: e' un errore, non una richiesta
     * idempotente. Rendere lo stesso indirizzo vorrebbe dire che una apri e
     * una chiudi non si corrispondono piu' una a una, e chi chiude una volta
     * sola lascerebbe la zona aperta per sempre. */
    if (idx >= 0) {
        for (slot = 0; slot < SHM_PER_PROC; slot++)
            if (proc->shm[slot].zona == (uint32_t)idx + 1) return ERR(EEXIST);
    }

    if (idx < 0) {
        uint32_t i;

        if (!(flag & SHM_CREA)) return ERR(ENOENT);
        if (byte == 0) return ERR(EINVAL);

        pagine = ALIGN_UP(byte, PAGE_SIZE) / PAGE_SIZE;
        if (pagine > SHM_PAGINE_MAX) return ERR(ENOMEM);

        for (i = 0; i < SHM_MAX_ZONE; i++)
            if (g_zone[i].proc == 0 && g_zone[i].fisico == NULL) break;
        if (i == SHM_MAX_ZONE) return ERR(ENOSPC);

        rc = zona_crea(i, nome, pagine);
        if (rc != 0) return rc;

        idx    = (int32_t)i;
        creata = 1;
    }

    rc = aggancia(proc, (uint32_t)idx, out_virt);
    if (rc != 0) {
        /* ! UNA ZONA CREATA E MAI AGGANCIATA NON DEVE RESTARE. Il suo
         * conteggio di processi e' zero, quindi nessuno la chiudera' mai e il
         * nome sarebbe occupato da qualcosa che non esiste. */
        if (creata) zona_distruggi((uint32_t)idx);
        return rc;
    }

    *out_byte = g_zone[idx].pagine * PAGE_SIZE;

    klog(LOG_INFO, "SHM: PID %u %s '%s' (%u KB) a 0x%08x - %u process%s",
         proc->pid, creata ? "crea" : "apre", g_zone[idx].nome,
         *out_byte / 1024, *out_virt, g_zone[idx].proc,
         g_zone[idx].proc == 1 ? "o" : "i");
    return 0;
}

int32_t shm_chiudi(Process *proc, uint32_t virt)
{
    uint32_t slot;

    if (proc == NULL) return ERR(EINVAL);

    for (slot = 0; slot < SHM_PER_PROC; slot++)
        if (proc->shm[slot].zona != 0 && proc->shm[slot].virt == virt) {
            sgancia(proc, slot, 1);
            return 0;
        }

    return ERR(EINVAL);
}

void shm_cleanup_process(Process *proc)
{
    uint32_t slot;

    if (proc == NULL) return;

    for (slot = 0; slot < SHM_PER_PROC; slot++)
        if (proc->shm[slot].zona != 0)
            sgancia(proc, slot, 0);
}

void shm_stato(uint32_t *out_zone, uint32_t *out_pagine)
{
    uint32_t i, n = 0, p = 0;

    for (i = 0; i < SHM_MAX_ZONE; i++)
        if (g_zone[i].proc > 0) { n++; p += g_zone[i].pagine; }

    if (out_zone)   *out_zone   = n;
    if (out_pagine) *out_pagine = p;
}
