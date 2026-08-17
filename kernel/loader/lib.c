/* =============================================================================
 * kernel/loader/lib.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LIBRERIE CONDIVISE — caricate una volta, agganciate a molti processi
 *
 * ! PERCHE' NON UN VERO COLLEGAMENTO DINAMICO. Un .so con ld.so, codice PIC,
 * GOT e PLT e' la strada standard, ed e' mesi di lavoro: caricatore ELF da
 * riscrivere, un linker dinamico da scrivere, la libc ricompilata PIC. E ogni
 * difetto li' dentro sarebbe un difetto di TUTTE le applicazioni insieme.
 *
 * Qui la libreria e' un ELF normalissimo — ET_EXEC, non PIC — collegato a un
 * indirizzo RISERVATO. Sta sempre li', quindi non c'e' niente da rilocare e
 * non serve nessuna GOT. Cio' che serviva davvero — aggiornare la libreria
 * senza ricompilare le applicazioni — si ottiene con la RISOLUZIONE PER NOME:
 * la libreria esporta una tabella {nome, indirizzo}, l'applicazione chiede i
 * nomi che le servono. Aggiungere funzioni, riordinarle, riscriverne il corpo:
 * le applicazioni continuano a funzionare. Solo TOGLIERE un nome le rompe —
 * esattamente come una DLL.
 *
 * ! LO SPAZIO ERA GIA' LI', VUOTO: 0x04000000-0x08000000, i 64 MB fra
 * USER_SPACE_BASE e l'indirizzo dove si caricano i programmi. Non tocca ne'
 * lo heap (che comincia sopra il programma) ne' la riserva dello stack.
 *
 * ! COSA SI CONDIVIDE E COSA NO, ed e' tutta la differenza fra una libreria
 * condivisa e una copia:
 *
 *     .text/.rodata   sola lettura  -> LA STESSA PAGINA FISICA in ogni
 *                                      processo, con un riferimento in piu'
 *     .data/.bss      scrivibili    -> una copia FRESCA per processo, presa
 *                                      dall'originale tenuto qui
 *
 * Condividere anche i dati scrivibili vorrebbe dire che due applicazioni si
 * scrivono addosso le variabili: la tabella delle finestre di ExWin e' in
 * .bss, e sarebbe la stessa per tutti.
 *
 * ! LE PAGINE CONDIVISE NON HANNO BISOGNO DI CONTABILITA' NEL PCB. Quando il
 * processo muore, paging_destroy_directory() le restituisce passando dal PMM,
 * che cala il conteggio dei riferimenti: e' lo stesso meccanismo della memoria
 * condivisa (kernel/mm/shm.c). L'originale tenuto qui e' un riferimento in
 * piu' che non muore mai, ed e' cio' che rende questa una CACHE.
 * ============================================================================= */

#include "kernel.h"
#include "elf.h"
#include "vfs.h"
#include "pmm.h"
#include "paging.h"
#include "sched.h"
#include "syscall.h"

/* -----------------------------------------------------------------------------
 * I confini, e sono controllati
 *
 * ! UNA LIBRERIA CHE CHIEDESSE UN INDIRIZZO FUORI DA QUI SI RIFIUTA. Sopra
 * 0x08000000 ci sono i programmi: una libreria mappata li' sovrascriverebbe il
 * codice di chi l'ha aperta, e paging_map_page() rimpiazza una PTE gia'
 * presente senza dire niente.
 * --------------------------------------------------------------------------- */
#define LIB_SPAZIO_BASE   0x04000000u
#define LIB_SPAZIO_FINE   0x08000000u

#define LIB_MAX           4     /* librerie diverse tenute insieme */
#define LIB_PAGINE_MAX    96    /* 384 KB per libreria */
#define LIB_PERC_MAX      96

typedef struct {
    uint32_t virt;      /* dove va mappata nel processo */
    uint32_t fisico;    /* l'originale: condiviso se sola lettura, altrimenti
                         * il modello da cui si copia */
    uint32_t flags;     /* PG_PRESENT | PG_USER | eventuale PG_WRITABLE */
    int      privata;   /* 1 = ogni processo ne vuole una sua */
} LibPagina;

typedef struct {
    int        usata;
    char       percorso[LIB_PERC_MAX];
    uint32_t   tabella;                     /* e_entry: dov'e' la tabella */
    uint32_t   n_pagine;
    LibPagina  pag[LIB_PAGINE_MAX];
} Libreria;

static Libreria g_lib[LIB_MAX];

/* Il kernel non ha una libreria di stringhe: ogni file si tiene i due aiuti
 * che gli servono, come fa kernel/mm/shm.c con i nomi delle zone. */
static int perc_uguale(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < LIB_PERC_MAX; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == '\0') return 1;
    }
    return 1;
}

static void perc_copia(char *dst, const char *src)
{
    uint32_t i;
    for (i = 0; i + 1 < LIB_PERC_MAX && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* -----------------------------------------------------------------------------
 * Scrivere in una pagina fisica che non e' mappata in nessuno spazio
 *
 * Stessa tecnica di elf.c: la finestra di rimappatura si apre e si chiude
 * intorno alla SINGOLA copia, mai intorno a un ciclo che contiene una
 * vfs_read() — quella si blocca in IPC verso un driver in ring 3, e una
 * finestra tenuta aperta attraverso un blocco e' una finestra che qualcun
 * altro ripunta sotto i piedi.
 * --------------------------------------------------------------------------- */
static void scrivi_in_pagina(uint32_t phys, uint32_t off, const uint8_t *src,
                             uint32_t n)
{
    uint8_t *dst = (uint8_t *)paging_finestra_apri(phys);
    uint32_t k;

    for (k = 0; k < n; k++) dst[off + k] = src[k];
    paging_finestra_chiudi();
}

static void copia_pagina(uint32_t dst_phys, uint32_t src_phys)
{
    /* ! DUE FINESTRE INSIEME NON CI SONO, quindi si passa da un buffer sullo
     * stack del kernel. 4 KB su uno stack da 128 KB stanno; aprire due volte
     * la stessa finestra darebbe due puntatori che indicano lo stesso posto. */
    static uint8_t ponte[PAGE_SIZE];        /* static: non sullo stack */
    uint8_t *p;
    uint32_t k;

    p = (uint8_t *)paging_finestra_apri(src_phys);
    for (k = 0; k < PAGE_SIZE; k++) ponte[k] = p[k];
    paging_finestra_chiudi();

    p = (uint8_t *)paging_finestra_apri(dst_phys);
    for (k = 0; k < PAGE_SIZE; k++) p[k] = ponte[k];
    paging_finestra_chiudi();
}

/* Butta via una libreria caricata a meta': le pagine originali tornano al PMM. */
static void disfa(Libreria *L)
{
    uint32_t i;

    for (i = 0; i < L->n_pagine; i++)
        if (L->pag[i].fisico) pmm_free_page(L->pag[i].fisico);

    L->n_pagine = 0;
    L->usata    = 0;
    L->percorso[0] = '\0';
}

/* -----------------------------------------------------------------------------
 * Leggere la libreria dal disco nella cache
 * --------------------------------------------------------------------------- */
static int leggi_libreria(const char *percorso, Libreria *L)
{
    int         handle;
    Elf32Header hdr;
    Elf32Phdr   ph;
    uint32_t    i;
    int         rc = ERR(EINVAL);

    handle = vfs_open(percorso, 0x0000);
    if (handle < 0) return handle;

    if (vfs_read(handle, &hdr, sizeof hdr, 0) != (int)sizeof hdr) {
        klog(LOG_ERROR, "LIB: '%s': header illeggibile", percorso);
        goto fine;
    }

    if (hdr.e_ident[0] != 0x7F || hdr.e_ident[1] != 'E' ||
        hdr.e_ident[2] != 'L'  || hdr.e_ident[3] != 'F' ||
        hdr.e_ident[4] != 1    || hdr.e_ident[5] != 1 ||
        hdr.e_machine != EM_386) {
        klog(LOG_ERROR, "LIB: '%s': non e' un ELF32 i386", percorso);
        goto fine;
    }

    /* ! ANCHE UNA LIBRERIA E' ET_EXEC, e non e' una svista: e' collegata a un
     * indirizzo fisso, quindi e' un eseguibile a tutti gli effetti — solo che
     * il suo punto d'ingresso non e' codice, e' la tabella dei nomi. */
    if (hdr.e_type != ET_EXEC) {
        klog(LOG_ERROR, "LIB: '%s': tipo %u, atteso ET_EXEC", percorso, hdr.e_type);
        goto fine;
    }

    if (hdr.e_entry < LIB_SPAZIO_BASE || hdr.e_entry >= LIB_SPAZIO_FINE) {
        klog(LOG_ERROR, "LIB: '%s': la tabella e' a 0x%08x, fuori dalla fascia "
             "delle librerie 0x%08x-0x%08x",
             percorso, hdr.e_entry, LIB_SPAZIO_BASE, LIB_SPAZIO_FINE);
        goto fine;
    }

    L->tabella  = hdr.e_entry;
    L->n_pagine = 0;

    for (i = 0; i < hdr.e_phnum; i++) {
        uint32_t off = hdr.e_phoff + i * sizeof(Elf32Phdr);
        uint32_t vstart, vend, pagine, pg, flags;

        if (vfs_read(handle, &ph, sizeof ph, off) != (int)sizeof ph) {
            klog(LOG_ERROR, "LIB: '%s': program header %u illeggibile", percorso, i);
            goto fine;
        }
        if (ph.p_type != PT_LOAD || ph.p_memsz == 0) continue;

        if (ph.p_vaddr < LIB_SPAZIO_BASE ||
            ph.p_vaddr + ph.p_memsz > LIB_SPAZIO_FINE) {
            klog(LOG_ERROR, "LIB: '%s': segmento 0x%08x+0x%x fuori dalla fascia "
                 "delle librerie", percorso, ph.p_vaddr, ph.p_memsz);
            goto fine;
        }

        vstart = ALIGN_DOWN(ph.p_vaddr, PAGE_SIZE);
        vend   = ALIGN_UP(ph.p_vaddr + ph.p_memsz, PAGE_SIZE);
        pagine = (vend - vstart) / PAGE_SIZE;

        flags = PG_PRESENT | PG_USER;
        if (ph.p_flags & PF_W) flags |= PG_WRITABLE;

        if (L->n_pagine + pagine > LIB_PAGINE_MAX) {
            klog(LOG_ERROR, "LIB: '%s' non ci sta: piu' di %u pagine",
                 percorso, (unsigned)LIB_PAGINE_MAX);
            goto fine;
        }

        for (pg = 0; pg < pagine; pg++) {
            LibPagina *P = &L->pag[L->n_pagine];
            uint32_t   vpage = vstart + pg * PAGE_SIZE;
            uint32_t   phys;

            phys = pmm_alloc_page();
            if (phys == 0) { rc = ERR(ENOMEM); goto fine; }

            /* ! AZZERATA PRIMA DI COPIARCI DENTRO, e non e' prudenza: la parte
             * di .bss che sta nell'ultima pagina di .data non e' nel file, e
             * senza azzerare conterrebbe quello che c'era prima in RAM — cioe'
             * la memoria di un altro processo, letta da questo. */
            paging_azzera_fisica(phys);

            P->virt    = vpage;
            P->fisico  = phys;
            P->flags   = flags;
            P->privata = (ph.p_flags & PF_W) ? 1 : 0;
            L->n_pagine++;
        }

        /* Il contenuto: solo p_filesz byte, il resto e' .bss e resta a zero. */
        if (ph.p_filesz > 0) {
            static uint8_t buf[512];
            uint32_t resta = ph.p_filesz;
            uint32_t fpos  = ph.p_offset;
            uint32_t vpos  = ph.p_vaddr;

            while (resta > 0) {
                uint32_t pezzo = resta > sizeof buf ? (uint32_t)sizeof buf : resta;
                uint32_t scritti = 0;
                int n = vfs_read(handle, buf, pezzo, fpos);

                if (n <= 0) {
                    klog(LOG_ERROR, "LIB: '%s': lettura fallita a 0x%x",
                         percorso, fpos);
                    goto fine;
                }

                while (scritti < (uint32_t)n) {
                    uint32_t v    = vpos + scritti;
                    uint32_t base = ALIGN_DOWN(v, PAGE_SIZE);
                    uint32_t poff = v - base;
                    uint32_t sp   = PAGE_SIZE - poff;
                    uint32_t take = (uint32_t)n - scritti;
                    uint32_t k;

                    if (take > sp) take = sp;

                    for (k = 0; k < L->n_pagine; k++)
                        if (L->pag[k].virt == base) break;
                    if (k == L->n_pagine) {
                        klog(LOG_ERROR, "LIB: '%s': pagina 0x%08x non allocata",
                             percorso, base);
                        goto fine;
                    }

                    scrivi_in_pagina(L->pag[k].fisico, poff, buf + scritti, take);
                    scritti += take;
                }

                resta -= (uint32_t)n;
                fpos  += (uint32_t)n;
                vpos  += (uint32_t)n;
            }
        }
    }

    if (L->n_pagine == 0) {
        klog(LOG_ERROR, "LIB: '%s': nessun segmento da caricare", percorso);
        goto fine;
    }

    rc = 0;

fine:
    vfs_close(handle);
    return rc;
}

/* -----------------------------------------------------------------------------
 * Agganciare una libreria gia' in cache al processo che la chiede
 * --------------------------------------------------------------------------- */
static int32_t aggancia(Libreria *L, Process *proc)
{
    uint32_t i;

    for (i = 0; i < L->n_pagine; i++) {
        LibPagina *P = &L->pag[i];
        uint32_t   phys;

        if (P->privata) {
            /* Una copia fresca: .data con i suoi valori iniziali, .bss a zero. */
            phys = pmm_alloc_page();
            if (phys == 0) goto senza_memoria;
            copia_pagina(phys, P->fisico);
        } else {
            /* ! LA STESSA PAGINA, con un proprietario in piu'. E' questo il
             * punto di tutto il file: dieci applicazioni grafiche useranno le
             * stesse pagine di codice di ExWin. */
            phys = P->fisico;
            if (pmm_ref_inc(phys) != 0) goto senza_memoria;
        }

        if (paging_map_page(proc->page_directory, P->virt, phys, P->flags) != 0) {
            pmm_free_page(phys);
            goto senza_memoria;
        }
    }
    return 0;

senza_memoria:
    /* ! CIO' CHE E' STATO MAPPATO SI SMONTA. Lasciarlo darebbe un processo con
     * mezza libreria: chiamerebbe una funzione che c'e' e cadrebbe su quella
     * dopo, e il fault indicherebbe la libreria invece della memoria finita. */
    {
        uint32_t j;
        for (j = 0; j < i; j++) {
            paging_unmap_page(proc->page_directory, L->pag[j].virt);
            pmm_free_page(L->pag[j].fisico);
        }
    }
    return ERR(ENOMEM);
}

/* -----------------------------------------------------------------------------
 * lib_apri — l'unica porta d'ingresso
 *
 * Rende l'indirizzo della tabella di esportazione (sempre positivo, sta a
 * 0x04xxxxxx), oppure un -errno.
 * --------------------------------------------------------------------------- */
int32_t lib_apri(const char *percorso, Process *proc, uint32_t *out_tabella)
{
    Libreria *L = NULL;
    uint32_t  i;
    int32_t   rc;

    if (percorso == NULL || proc == NULL || out_tabella == NULL)
        return ERR(EINVAL);

    /* Gia' in cache? Allora si aggancia e basta: il disco non si tocca. */
    for (i = 0; i < LIB_MAX; i++) {
        if (!g_lib[i].usata) continue;
        if (perc_uguale(g_lib[i].percorso, percorso)) { L = &g_lib[i]; break; }
    }

    if (L == NULL) {
        for (i = 0; i < LIB_MAX; i++)
            if (!g_lib[i].usata) { L = &g_lib[i]; break; }

        /* ! NON SI BUTTA FUORI NIENTE PER FARE POSTO. Una libreria in cache e'
         * mappata dentro processi vivi: toglierla dalla cache non la toglie da
         * loro, e la prossima apertura ne caricherebbe una SECONDA copia agli
         * stessi indirizzi. Meglio dire di no. */
        if (L == NULL) {
            klog(LOG_ERROR, "LIB: gia' %u librerie caricate, '%s' non entra",
                 (unsigned)LIB_MAX, percorso);
            return ERR(ENOMEM);
        }

        perc_copia(L->percorso, percorso);
        rc = leggi_libreria(percorso, L);
        if (rc != 0) { disfa(L); return rc; }
        L->usata = 1;

        klog(LOG_INFO, "LIB: '%s' caricata: %u pagine, tabella a 0x%08x",
             percorso, L->n_pagine, L->tabella);
    }

    rc = aggancia(L, proc);
    if (rc != 0) return rc;

    *out_tabella = L->tabella;
    return 0;
}
