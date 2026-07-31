/* =============================================================================
 * kernel/loader/elf.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "elf.h"
#include "fat12.h"
#include "vfs.h"
#include "pmm.h"
#include "paging.h"
#include "sched.h"
#include "kmalloc.h"

/* =============================================================================
 * Strutture ELF32 (formato file)
 * ============================================================================= */

/* ELF Magic */
#define ELF_MAGIC0      0x7F
#define ELF_MAGIC1      'E'
#define ELF_MAGIC2      'L'
#define ELF_MAGIC3      'F'

/* e_ident indici */
#define EI_MAG0         0
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4   /* 1=32bit, 2=64bit */
#define EI_DATA         5   /* 1=little-endian, 2=big-endian */
#define EI_VERSION      6   /* Deve essere 1 */

/* Valori e_type */
#define ET_EXEC         2   /* Eseguibile */
#define ET_DYN          3   /* Shared object / PIE */

/* Valore e_machine */
#define EM_386          3   /* Intel i386 */

/* Tipi Program Header (p_type) */
#define PT_NULL         0
#define PT_LOAD         1   /* Segmento da caricare */
#define PT_DYNAMIC      2   /* Informazioni dynamic linker */
#define PT_INTERP       3   /* Percorso dynamic linker */
#define PT_NOTE         4
#define PT_SHLIB        5
#define PT_PHDR         6

/* Flag Program Header (p_flags) */
#define PF_X            0x1     /* Execute */
#define PF_W            0x2     /* Write */
#define PF_R            0x4     /* Read */

/* ELF Header (52 byte) */
typedef struct PACKED {
    uint8_t  e_ident[16];   /* Magic + class + data + version + padding */
    uint16_t e_type;        /* ET_EXEC o ET_DYN */
    uint16_t e_machine;     /* EM_386 */
    uint32_t e_version;     /* 1 */
    uint32_t e_entry;       /* Entry point virtuale */
    uint32_t e_phoff;       /* Offset program header table */
    uint32_t e_shoff;       /* Offset section header table (non usato) */
    uint32_t e_flags;       /* Flags specifici architettura */
    uint16_t e_ehsize;      /* Dimensione ELF header (52) */
    uint16_t e_phentsize;   /* Dimensione ogni program header (32) */
    uint16_t e_phnum;       /* Numero program headers */
    uint16_t e_shentsize;   /* Dimensione ogni section header */
    uint16_t e_shnum;       /* Numero section headers */
    uint16_t e_shstrndx;    /* Indice section header con nomi sezioni */
} Elf32Header;

/* Program Header (32 byte) */
typedef struct PACKED {
    uint32_t p_type;        /* Tipo segmento (PT_LOAD, ecc.) */
    uint32_t p_offset;      /* Offset nel file */
    uint32_t p_vaddr;       /* Indirizzo virtuale destinazione */
    uint32_t p_paddr;       /* Indirizzo fisico (ignorato) */
    uint32_t p_filesz;      /* Dimensione nel file */
    uint32_t p_memsz;       /* Dimensione in memoria (>= p_filesz, diff = BSS) */
    uint32_t p_flags;       /* Permessi: PF_R, PF_W, PF_X */
    uint32_t p_align;       /* Allineamento (deve essere potenza di 2) */
} Elf32Phdr;

/* =============================================================================
 * elf_verify_header — Controlla che l'ELF header sia valido per ExOS
 * ============================================================================= */
static int elf_verify_header(const Elf32Header *hdr)
{
    /* Magic number */
    if (hdr->e_ident[EI_MAG0] != ELF_MAGIC0 ||
        hdr->e_ident[EI_MAG1] != ELF_MAGIC1 ||
        hdr->e_ident[EI_MAG2] != ELF_MAGIC2 ||
        hdr->e_ident[EI_MAG3] != ELF_MAGIC3) {
        klog(LOG_ERROR, "ELF: magic non valido");
        return -1;
    }

    /* Classe 32-bit */
    if (hdr->e_ident[EI_CLASS] != 1) {
        klog(LOG_ERROR, "ELF: non e' ELF32 (class=%u)", hdr->e_ident[EI_CLASS]);
        return -1;
    }

    /* Little-endian */
    if (hdr->e_ident[EI_DATA] != 1) {
        klog(LOG_ERROR, "ELF: non e' little-endian");
        return -1;
    }

    /* Tipo: eseguibile */
    if (hdr->e_type != ET_EXEC) {
        klog(LOG_ERROR, "ELF: tipo non supportato (type=%u, atteso ET_EXEC=%u)",
             hdr->e_type, ET_EXEC);
        return -1;
    }

    /* Architettura i386 */
    if (hdr->e_machine != EM_386) {
        klog(LOG_ERROR, "ELF: architettura non supportata (machine=%u)", hdr->e_machine);
        return -1;
    }

    /* Versione */
    if (hdr->e_version != 1) {
        klog(LOG_ERROR, "ELF: versione non valida (%u)", hdr->e_version);
        return -1;
    }

    /* Entry point nel range utente */
    if (hdr->e_entry < USER_SPACE_BASE || hdr->e_entry >= USER_SPACE_END) {
        klog(LOG_ERROR, "ELF: entry point fuori spazio utente: 0x%08x", hdr->e_entry);
        return -1;
    }

    /* Program headers presenti */
    if (hdr->e_phnum == 0 || hdr->e_phoff == 0) {
        klog(LOG_ERROR, "ELF: nessun program header");
        return -1;
    }

    return 0;
}

/* =============================================================================
 * elf_load — Carica un ELF da FAT12 in un processo
 *
 * path:    percorso del file ELF (es. "/bin/sh")
 * proc:    processo destinazione (PD già creata)
 * result:  struttura con entry point e stack top da passare a sched_enter_usermode
 *
 * Ritorna: 0 = successo, -1 = errore
 * ============================================================================= */
int elf_load(const char *path, Process *proc, ElfLoadResult *result)
{
    int           handle;
    Elf32Header   hdr;
    Elf32Phdr    *phdrs = NULL;
    uint8_t      *seg_buf = NULL;
    uint32_t      i;
    int           ret = -1;

    klog(LOG_INFO, "ELF: caricamento '%s'...", path);

    /* ==========================================================================
     * Passo 1: Apri il file ELF dal FAT12
     * ========================================================================== */

handle = vfs_open(path, 0x0000);  /* O_RDONLY */

if (handle < 0) {
        klog(LOG_ERROR, "ELF: file non trovato: '%s' (err=%d)", path, handle);
        return -1;
    }

    /* ==========================================================================
     * Passo 2: Leggi e verifica ELF header
     * ========================================================================== */

if (vfs_read(handle, &hdr, sizeof(Elf32Header), 0) != (int)sizeof(Elf32Header)) {
        klog(LOG_ERROR, "ELF: impossibile leggere header");
        goto cleanup;
    }

    if (elf_verify_header(&hdr) != 0) goto cleanup;

klog(LOG_INFO, "ELF: header valido, entry=0x%08x, phnum=%u",
         hdr.e_entry, hdr.e_phnum);

    /* ==========================================================================
     * Passo 3: Leggi tutti i Program Headers
     * ========================================================================== */
    uint32_t phdrs_size = hdr.e_phnum * sizeof(Elf32Phdr);
    phdrs = (Elf32Phdr *)kmalloc(phdrs_size);
    if (!phdrs) {
        klog(LOG_ERROR, "ELF: OOM allocando program headers");
        goto cleanup;
    }

    if (vfs_read(handle, phdrs, phdrs_size, hdr.e_phoff) != (int)phdrs_size) {
        klog(LOG_ERROR, "ELF: impossibile leggere program headers");
        goto cleanup;
    }

    /* ==========================================================================
     * Passo 4: Alloca buffer temporaneo per i segmenti
     * Useremo un buffer di 1 pagina alla volta per non sprecare RAM.
     * ========================================================================== */

seg_buf = (uint8_t *)kmalloc(PAGE_SIZE);
    if (!seg_buf) {
        klog(LOG_ERROR, "ELF: OOM allocando buffer segmento");
        goto cleanup;
    }

    /* ==========================================================================
     * Passo 5: Processa ogni PT_LOAD
     * ========================================================================== */

for (i = 0; i < hdr.e_phnum; i++) {
        Elf32Phdr *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0)      continue;

        klog(LOG_INFO, "ELF: PT_LOAD[%u] vaddr=0x%08x memsz=0x%x filesz=0x%x flags=0x%x",
             i, ph->p_vaddr, ph->p_memsz, ph->p_filesz, ph->p_flags);

        /* Verifica range virtuale */
        if (ph->p_vaddr < USER_SPACE_BASE || ph->p_vaddr >= USER_SPACE_END) {
            klog(LOG_ERROR, "ELF: segmento fuori spazio utente: 0x%08x", ph->p_vaddr);
            goto cleanup;
        }

        /* Calcola pagine necessarie */
        uint32_t vstart  = ALIGN_DOWN(ph->p_vaddr, PAGE_SIZE);
        uint32_t vend    = ALIGN_UP(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
        uint32_t pages   = (vend - vstart) / PAGE_SIZE;
        uint32_t pg_flags = PG_PRESENT | PG_USER;
        if (ph->p_flags & PF_W) pg_flags |= PG_WRITABLE;

/* Alloca e mappa le pagine */
        uint32_t pg;
        for (pg = 0; pg < pages; pg++) {
            uint32_t phys = pmm_alloc_page();
            if (phys == 0) {
                klog(LOG_ERROR, "ELF: OOM allocando pagina per segmento %u", i);
                goto cleanup;
            }

            /* Azzera la pagina */
            {
                uint8_t *p = (uint8_t *)phys;
                uint32_t n = PAGE_SIZE;
                while (n--) *p++ = 0;
            }

            uint32_t vpage = vstart + pg * PAGE_SIZE;
            if (paging_map_page(proc->page_directory, vpage, phys, pg_flags) != 0) {
                pmm_free_page(phys);
                klog(LOG_ERROR, "ELF: errore mapping pagina 0x%08x", vpage);
                goto cleanup;
            }
        }

/* Copia dati dal file nella memoria virtuale del processo
         * Dobbiamo scrivere attraverso il mapping: traduciamo ogni
         * indirizzo virtuale → fisico e scriviamo direttamente. */
        if (ph->p_filesz > 0) {
            uint32_t file_offset = ph->p_offset;
            uint32_t vaddr       = ph->p_vaddr;
            uint32_t remaining   = ph->p_filesz;

            while (remaining > 0) {
                uint32_t chunk = remaining;
                if (chunk > PAGE_SIZE) chunk = PAGE_SIZE;

                /* Leggi chunk dal file ELF */
                int n = vfs_read(handle, seg_buf, chunk, file_offset);

if (n <= 0) {
                    klog(LOG_ERROR, "ELF: errore lettura segmento (offset=%u)", file_offset);
                    goto cleanup;
                }

                /* Traduci indirizzo virtuale → fisico e copia */
                uint32_t written = 0;
                while (written < (uint32_t)n) {
                    uint32_t phys_dst = paging_get_physical(proc->page_directory,
                                                             vaddr + written);
                    if (phys_dst == 0) {
                        klog(LOG_ERROR, "ELF: indirizzo virtuale non mappato 0x%08x",
                             vaddr + written);
                        goto cleanup;
                    }

                    /* Copia byte per byte (potremmo ottimizzare con memcpy) */
                    uint32_t page_off = (vaddr + written) & 0xFFF;
                    uint32_t avail    = PAGE_SIZE - page_off;
                    uint32_t take     = (uint32_t)n - written;
                    if (take > avail) take = avail;

                    uint8_t *dst_ptr = (uint8_t *)(phys_dst - page_off);
                    uint32_t k;
                    for (k = 0; k < take; k++) {
                        dst_ptr[page_off + k] = seg_buf[written + k];
                    }
                    written += take;
                }

                file_offset += (uint32_t)n;
                vaddr       += (uint32_t)n;
                remaining   -= (uint32_t)n;
            }
        }

/* BSS: la zona tra filesz e memsz è già azzerata (pagine azzerate sopra) */
        klog(LOG_DEBUG, "ELF: segmento %u caricato OK", i);
    }

/* ==========================================================================
     * Passo 6: Alloca stack utente
     *
     * Lo stack cresce verso il basso. Mettiamo il top a USER_SPACE_END - 4KB
     * e allochiamo USER_STACK_SIZE byte.
     * ========================================================================== */
    {
        uint32_t stack_top   = USER_SPACE_END - PAGE_SIZE;
        uint32_t stack_limit = stack_top - USER_STACK_MAX;   /* riserva */
        uint32_t stack_base  = stack_top - USER_STACK_INIT;  /* impegno */
        uint32_t stack_pages = USER_STACK_INIT / PAGE_SIZE;
        uint32_t pg;

        /* SI IMPEGNANO SOLO LE PRIME PAGINE (kernel 0.124).
         *
         * Prima qui si allocavano e azzeravano tutti i 64 KB, byte per
         * byte, per OGNI processo — anche per un programma che ne usa
         * duecento. Erano ~65000 iterazioni di azzeramento a ogni
         * caricamento, spese quasi interamente su pagine mai toccate.
         *
         * Ora si impegnano USER_STACK_INIT byte partendo dal TOP (lo
         * stack cresce verso il basso, quindi le pagine subito utili sono
         * quelle in cima) e il resto della riserva viene mappato su
         * richiesta da page_fault_handler. Il caricamento e' piu' veloce e
         * il processo occupa meno RAM; il tetto resta USER_STACK_MAX. */
        for (pg = 0; pg < stack_pages; pg++) {
            uint32_t phys = pmm_alloc_page();
            if (phys == 0) {
                klog(LOG_ERROR, "ELF: OOM allocando stack utente");
                goto cleanup;
            }
            /* Azzera */
            {
                uint8_t *p = (uint8_t *)phys;
                uint32_t n = PAGE_SIZE;
                while (n--) *p++ = 0;
            }

            uint32_t vpage = stack_base + pg * PAGE_SIZE;
            if (paging_map_page(proc->page_directory, vpage, phys,
                                 PG_PRESENT | PG_WRITABLE | PG_USER) != 0) {
                pmm_free_page(phys);
                goto cleanup;
            }
        }

        proc->user_stack_base  = stack_base;
        proc->user_stack_top   = stack_top;
        proc->user_stack_limit = stack_limit;
        result->user_stack_top = stack_top - 16;   /* Allineamento 16 byte */

        klog(LOG_INFO, "ELF: stack utente 0x%08x - 0x%08x impegnato "
             "(riserva fino a 0x%08x, %u KB)",
             stack_base, stack_top, stack_limit, USER_STACK_MAX / 1024);
    }

    /* ==========================================================================
     * Passo 7: Aggiorna heap del processo
     * ========================================================================== */
    {
        /* L'heap inizia subito dopo l'ultimo segmento caricato */
        uint32_t heap_start = 0;
        for (i = 0; i < hdr.e_phnum; i++) {
            if (phdrs[i].p_type != PT_LOAD) continue;
            uint32_t end = ALIGN_UP(phdrs[i].p_vaddr + phdrs[i].p_memsz, PAGE_SIZE);
            if (end > heap_start) heap_start = end;
        }
        proc->heap_start = heap_start;
        proc->heap_end   = heap_start;
        klog(LOG_INFO, "ELF: heap utente inizia a 0x%08x", heap_start);
    }

    /* Successo */
    result->entry_point = hdr.e_entry;
    klog(LOG_INFO, "ELF: '%s' caricato con successo, entry=0x%08x", path, hdr.e_entry);
    ret = 0;

cleanup:
    vfs_close(handle);
    if (phdrs)   kfree(phdrs);
    if (seg_buf) kfree(seg_buf);
    return ret;
}
