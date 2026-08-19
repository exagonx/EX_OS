/* =============================================================================
 * kernel/loader/dynlink.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Dynamic linker per ELF shared objects (.so) in EX-OS.
 *
 * Supporta:
 *   - ELF32 ET_DYN (shared object / PIE)
 *   - Sezioni .dynamic (DT_NEEDED, DT_SYMTAB, DT_STRTAB, DT_REL, DT_RELA)
 *   - Relocazioni R_386_32, R_386_PC32, R_386_GLOB_DAT, R_386_JMP_SLOT
 *   - Symbol resolution dalla symbol table ELF
 *   - Mapping in spazio utente del processo (PG_USER)
 *   - Cache librerie già caricate (evita duplicati)
 *
 * Flusso:
 *   1. elf_load_dynamic() carica l'ELF ET_DYN in memoria
 *   2. Legge .dynamic per trovare dipendenze (DT_NEEDED)
 *   3. Carica ricorsivamente ogni dipendenza da /lib/
 *   4. Risolve tutte le relocazioni
 *   5. Aggiorna GOT del processo con gli indirizzi reali
 * ============================================================================= */

#include "kernel.h"
#include "dynlink.h"
#include "fat12.h"
#include "vfs.h"
#include "pmm.h"
#include "paging.h"
#include "kmalloc.h"
#include "sched.h"

/* =============================================================================
 * Strutture ELF32 per dynamic linking
 * ============================================================================= */

/* Tipi ELF */
typedef uint32_t Elf32_Addr;
typedef uint32_t Elf32_Off;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Word;
typedef int32_t  Elf32_Sword;

/* ELF Header (ridefinito localmente per chiarezza) */
typedef struct PACKED {
    uint8_t   e_ident[16];
    Elf32_Half e_type;
    Elf32_Half e_machine;
    Elf32_Word e_version;
    Elf32_Addr e_entry;
    Elf32_Off  e_phoff;
    Elf32_Off  e_shoff;
    Elf32_Word e_flags;
    Elf32_Half e_ehsize;
    Elf32_Half e_phentsize;
    Elf32_Half e_phnum;
    Elf32_Half e_shentsize;
    Elf32_Half e_shnum;
    Elf32_Half e_shstrndx;
} Elf32_Ehdr;

/* Program Header */
typedef struct PACKED {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
} Elf32_Phdr;

/* Section Header */
typedef struct PACKED {
    Elf32_Word sh_name;
    Elf32_Word sh_type;
    Elf32_Word sh_flags;
    Elf32_Addr sh_addr;
    Elf32_Off  sh_offset;
    Elf32_Word sh_size;
    Elf32_Word sh_link;
    Elf32_Word sh_info;
    Elf32_Word sh_addralign;
    Elf32_Word sh_entsize;
} Elf32_Shdr;

/* Symbol table entry */
typedef struct PACKED {
    Elf32_Word  st_name;    /* Indice nel string table */
    Elf32_Addr  st_value;   /* Valore (indirizzo virtuale) */
    Elf32_Word  st_size;    /* Dimensione simbolo */
    uint8_t     st_info;    /* Tipo e binding */
    uint8_t     st_other;   /* Visibilità */
    Elf32_Half  st_shndx;   /* Sezione di appartenenza */
} Elf32_Sym;

/* Relocation entry (senza addend) */
typedef struct PACKED {
    Elf32_Addr  r_offset;   /* Indirizzo da aggiornare */
    Elf32_Word  r_info;     /* Simbolo + tipo relocation */
} Elf32_Rel;

/* Relocation entry (con addend) */
typedef struct PACKED {
    Elf32_Addr  r_offset;
    Elf32_Word  r_info;
    Elf32_Sword r_addend;
} Elf32_Rela;

/* Dynamic section entry */
typedef struct PACKED {
    Elf32_Sword d_tag;
    union {
        Elf32_Word d_val;
        Elf32_Addr d_ptr;
    } d_un;
} Elf32_Dyn;

/* Tipi di segmento */
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3

/* Tag .dynamic */
#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_PLTGOT   3
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_RELAENT  9
#define DT_STRSZ    10
#define DT_SYMENT   11
#define DT_INIT     12
#define DT_FINI     13
#define DT_REL      17
#define DT_RELSZ    18
#define DT_RELENT   19
#define DT_PLTREL   20
#define DT_JMPREL   23

/* Tipi di relocation x86 */
#define R_386_NONE      0
#define R_386_32        1   /* S + A */
#define R_386_PC32      2   /* S + A - P */
#define R_386_GOT32     3
#define R_386_PLT32     4
#define R_386_COPY      5
#define R_386_GLOB_DAT  6   /* S */
#define R_386_JMP_SLOT  7   /* S */
#define R_386_RELATIVE  8   /* B + A */

/* Estrai tipo e simbolo da r_info */
#define ELF32_R_SYM(i)   ((i) >> 8)
#define ELF32_R_TYPE(i)  ((uint8_t)(i))

/* Binding simbolo */
#define STB_LOCAL   0
#define STB_GLOBAL  1
#define STB_WEAK    2
#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xF)

/* Sezioni speciali */
#define SHN_UNDEF   0
#define SHN_ABS     0xFFF1

/* =============================================================================
 * Cache librerie caricate
 * Evita di caricare la stessa .so più volte nello stesso processo.
 * ============================================================================= */
#define DL_MAX_LIBS     16
#define DL_NAME_LEN     64

typedef struct {
    char        name[DL_NAME_LEN];  /* Nome file (es. "libc.so") */
    uint32_t    base;               /* Indirizzo virtuale base nel processo */
    uint32_t    size;               /* Dimensione totale mappata */
    /* Symbol table in memoria (per resolution da altri moduli) */
    Elf32_Sym  *symtab;
    uint32_t    symtab_count;
    const char *strtab;
} LoadedLib;

static LoadedLib g_loaded_libs[DL_MAX_LIBS];
static uint32_t  g_lib_count = 0;

/* =============================================================================
 * Helper: copia stringa sicura
 * ============================================================================= */
static void dl_strcpy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int dl_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static uint32_t __attribute__((unused)) dl_strlen(const char *s)
{
    uint32_t n = 0;
    while (s && *s++) n++;
    return n;
}

/* =============================================================================
 * Helper: leggi N byte dal file ELF all'offset dato
 * ============================================================================= */
static int dl_read_at(int handle, void *buf, uint32_t size, uint32_t offset)
{
    return vfs_read(handle, buf, size, offset);
}

/* =============================================================================
 * dl_find_loaded — Cerca una libreria già caricata nella cache
 * ============================================================================= */
static LoadedLib *dl_find_loaded(const char *name)
{
    uint32_t i;
    for (i = 0; i < g_lib_count; i++) {
        if (dl_strcmp(g_loaded_libs[i].name, name) == 0) {
            return &g_loaded_libs[i];
        }
    }
    return NULL;
}

/* =============================================================================
 * dl_lookup_symbol — Cerca un simbolo per nome in tutte le librerie caricate
 *
 * Ritorna l'indirizzo virtuale del simbolo, 0 se non trovato.
 * ============================================================================= */
static uint32_t dl_lookup_symbol(const char *name)
{
    uint32_t i, j;

    for (i = 0; i < g_lib_count; i++) {
        LoadedLib *lib = &g_loaded_libs[i];
        if (!lib->symtab || !lib->strtab) continue;

        for (j = 0; j < lib->symtab_count; j++) {
            Elf32_Sym *sym = &lib->symtab[j];

            /* Solo simboli globali definiti */
            if (ELF32_ST_BIND(sym->st_info) == STB_LOCAL) continue;
            if (sym->st_shndx == SHN_UNDEF) continue;
            if (sym->st_value == 0) continue;

            const char *sym_name = lib->strtab + sym->st_name;
            if (dl_strcmp(sym_name, name) == 0) {
                /* L'indirizzo è base + valore relativo */
                return lib->base + sym->st_value;
            }
        }
    }
    return 0;
}

/* =============================================================================
 * dl_apply_relocations — Applica le relocazioni di un segmento
 *
 * proc:      processo destinazione
 * base:      indirizzo virtuale base della libreria nel processo
 * rel_data:  array di Elf32_Rel
 * rel_count: numero di entry
 * symtab:    symbol table della libreria
 * strtab:    string table della libreria
 * ============================================================================= */
static int dl_apply_relocations(Process *proc, uint32_t base,
                                 Elf32_Rel *rel, uint32_t rel_count,
                                 Elf32_Sym *symtab, const char *strtab)
{
    uint32_t i;

    for (i = 0; i < rel_count; i++) {
        uint32_t  r_offset = rel[i].r_offset + base;
        uint32_t  r_type   = ELF32_R_TYPE(rel[i].r_info);
        uint32_t  r_sym    = ELF32_R_SYM(rel[i].r_info);
        uint32_t  sym_addr = 0;

        /* Risolvi simbolo se necessario */
        if (r_sym != 0 && symtab != NULL) {
            Elf32_Sym *sym = &symtab[r_sym];
            if (sym->st_shndx == SHN_UNDEF) {
                /* Simbolo esterno: cercalo in tutte le librerie */
                const char *sym_name = strtab + sym->st_name;
                sym_addr = dl_lookup_symbol(sym_name);
                if (sym_addr == 0) {
                    klog(LOG_WARN, "DYNLINK: simbolo non risolto: '%s'", sym_name);
                    /* Non è fatale: lascia a 0 (produrrà segfault se usato) */
                }
            } else {
                /* Simbolo locale: base + valore */
                sym_addr = base + sym->st_value;
            }
        }

        /* Traduci indirizzo virtuale della patch → fisico */
        uint32_t phys_patch = paging_get_physical(proc->page_directory, r_offset);
        if (phys_patch == 0) {
            klog(LOG_ERROR, "DYNLINK: indirizzo relocation non mappato: 0x%08x", r_offset);
            continue;
        }

        uint32_t *patch_ptr = (uint32_t *)phys_patch;

        /* Applica relocation in base al tipo */
        switch (r_type) {
            case R_386_NONE:
                break;

            case R_386_32:
                /* S + A: simbolo + addend (valore corrente) */
                *patch_ptr = sym_addr + *patch_ptr;
                break;

            case R_386_PC32:
                /* S + A - P: simbolo + addend - posizione */
                *patch_ptr = sym_addr + *patch_ptr - r_offset;
                break;

            case R_386_GLOB_DAT:
            case R_386_JMP_SLOT:
                /* S: indirizzo assoluto del simbolo */
                *patch_ptr = sym_addr;
                break;

            case R_386_RELATIVE:
                /* B + A: base + addend (per PIE) */
                *patch_ptr = base + *patch_ptr;
                break;

            case R_386_COPY:
                /* Copia dati dal simbolo (per variabili esterne) */
                if (sym_addr != 0 && symtab != NULL) {
                    Elf32_Sym *sym = &symtab[r_sym];
                    uint32_t sz = sym->st_size;
                    uint32_t src_phys = paging_get_physical(
                                            proc->page_directory, sym_addr);
                    if (src_phys && sz > 0) {
                        uint8_t *dst = (uint8_t *)phys_patch;
                        uint8_t *src = (uint8_t *)src_phys;
                        while (sz--) *dst++ = *src++;
                    }
                }
                break;

            default:
                klog(LOG_WARN, "DYNLINK: tipo relocation %u non supportato", r_type);
                break;
        }
    }

    return 0;
}

/* =============================================================================
 * dl_load_so — Carica un ELF shared object in memoria del processo
 *
 * path:        percorso completo del .so (es. "/lib/libc.so")
 * proc:        processo destinazione
 * load_base:   indirizzo virtuale base dove mappare il .so
 * out_lib:     output: info sulla libreria caricata
 *
 * Ritorna: 0 = successo, -1 = errore
 * ============================================================================= */
static int dl_load_so(const char *path, Process *proc,
                       uint32_t load_base, LoadedLib *out_lib)
{
    int          handle;
    Elf32_Ehdr   hdr;
    Elf32_Phdr  *phdrs       = NULL;
    uint8_t     *seg_buf     = NULL;
    Elf32_Dyn   *dyn_section = NULL;
    Elf32_Sym   *symtab_copy = NULL;
    char        *strtab_copy = NULL;
    Elf32_Rel   *rel_buf     = NULL;
    int          ret         = -1;
    uint32_t     i;

    klog(LOG_INFO, "DYNLINK: caricamento '%s' a base 0x%08x", path, load_base);

    /* Apri il file */
    handle = vfs_open(path, 0x0000);
    if (handle < 0) {
        klog(LOG_ERROR, "DYNLINK: '%s' non trovato (err=%d)", path, handle);
        return -1;
    }

    /* Leggi ELF header */
    if (dl_read_at(handle, &hdr, sizeof(hdr), 0) != (int)sizeof(hdr)) {
        klog(LOG_ERROR, "DYNLINK: impossibile leggere header di '%s'", path);
        goto cleanup;
    }

    /* Verifica magic */
    if (hdr.e_ident[0] != 0x7F || hdr.e_ident[1] != 'E' ||
        hdr.e_ident[2] != 'L'  || hdr.e_ident[3] != 'F') {
        klog(LOG_ERROR, "DYNLINK: '%s' non e' un ELF valido", path);
        goto cleanup;
    }

    /* Tipo: ET_DYN (shared object) o ET_EXEC (per driver statici) */
    if (hdr.e_type != 2 /* ET_EXEC */ && hdr.e_type != 3 /* ET_DYN */) {
        klog(LOG_ERROR, "DYNLINK: tipo ELF non supportato: %u", hdr.e_type);
        goto cleanup;
    }

    /* Leggi program headers */
    uint32_t phdrs_sz = hdr.e_phnum * sizeof(Elf32_Phdr);
    phdrs = (Elf32_Phdr *)kmalloc(phdrs_sz);
    if (!phdrs) goto cleanup;

    if (dl_read_at(handle, phdrs, phdrs_sz, hdr.e_phoff) != (int)phdrs_sz) {
        klog(LOG_ERROR, "DYNLINK: impossibile leggere program headers");
        goto cleanup;
    }

    seg_buf = (uint8_t *)kmalloc(PAGE_SIZE);
    if (!seg_buf) goto cleanup;

    /* -------------------------------------------------------------------------
     * Passo 1: Carica tutti i segmenti PT_LOAD
     * Per ET_DYN, gli indirizzi virtuali sono relativi → aggiungi load_base
     * ------------------------------------------------------------------------- */
    for (i = 0; i < hdr.e_phnum; i++) {
        Elf32_Phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue;

        uint32_t vstart   = ALIGN_DOWN(load_base + ph->p_vaddr, PAGE_SIZE);
        uint32_t vend     = ALIGN_UP(load_base + ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
        uint32_t n_pages  = (vend - vstart) / PAGE_SIZE;
        uint32_t pg_flags = PG_PRESENT | PG_USER;
        if (ph->p_flags & 0x2 /* PF_W */) pg_flags |= PG_WRITABLE;

        uint32_t pg;
        for (pg = 0; pg < n_pages; pg++) {
            /* ! FASCIA KERNEL, e qui e' una scelta diversa da elf_load.
             *
             * Il rilocatore qui sotto non copia dall'inizio alla fine: sui
             * simboli R_386_COPY legge da una pagina e scrive in un'altra
             * NELLO STESSO ISTANTE, e la finestra di rimappatura e' una
             * sola. Tenere l'immagine di un driver dove il kernel la
             * indirizza direttamente e' il modo onesto di dirlo — e costa
             * poco: i driver sono due, di una quindicina di KB l'uno,
             * caricati una volta all'avvio. */
            uint32_t phys = pmm_alloc_page_kernel();
            if (phys == 0) goto cleanup;

            /* Azzera pagina */
            {
                uint8_t *p = (uint8_t *)phys;
                uint32_t n = PAGE_SIZE;
                while (n--) *p++ = 0;
            }

            if (paging_map_page(proc->page_directory,
                                 vstart + pg * PAGE_SIZE,
                                 phys, pg_flags) != 0) {
                pmm_free_page(phys);
                goto cleanup;
            }
        }

        /* Copia dati dal file */
        if (ph->p_filesz > 0) {
            uint32_t file_off = ph->p_offset;
            uint32_t vaddr    = load_base + ph->p_vaddr;
            uint32_t remain   = ph->p_filesz;

            while (remain > 0) {
                uint32_t chunk = (remain < PAGE_SIZE) ? remain : PAGE_SIZE;
                int n = vfs_read(handle, seg_buf, chunk, file_off);
                if (n <= 0) goto cleanup;

                uint32_t written = 0;
                while (written < (uint32_t)n) {
                    uint32_t phys_dst = paging_get_physical(
                                            proc->page_directory, vaddr + written);
                    if (phys_dst == 0) goto cleanup;

                    uint32_t pg_off = (vaddr + written) & 0xFFF;
                    uint32_t avail  = PAGE_SIZE - pg_off;
                    uint32_t take   = (uint32_t)n - written;
                    if (take > avail) take = avail;

                    uint8_t *dst = (uint8_t *)(phys_dst - pg_off);
                    uint32_t k;
                    for (k = 0; k < take; k++) dst[pg_off + k] = seg_buf[written + k];
                    written += take;
                }

                file_off += (uint32_t)n;
                vaddr    += (uint32_t)n;
                remain   -= (uint32_t)n;
            }
        }
    }

    /* -------------------------------------------------------------------------
     * Passo 2: Trova la sezione .dynamic e leggi i tag
     * ------------------------------------------------------------------------- */
    uint32_t dyn_vaddr = 0, dyn_size = 0;
    for (i = 0; i < hdr.e_phnum; i++) {
        if (phdrs[i].p_type == PT_DYNAMIC) {
            dyn_vaddr = load_base + phdrs[i].p_vaddr;
            dyn_size  = phdrs[i].p_filesz;
            break;
        }
    }

    /* Tag estratti da .dynamic */
    uint32_t dt_symtab = 0, dt_strtab = 0, dt_strsz = 0;
    uint32_t dt_rel    = 0, dt_relsz  = 0;
    uint32_t dt_jmprel = 0, dt_pltrelsz = 0;

    if (dyn_vaddr && dyn_size) {
        dyn_section = (Elf32_Dyn *)kmalloc(dyn_size);
        if (!dyn_section) goto cleanup;

        /* Leggi .dynamic dal file (offset dal program header) */
        uint32_t dyn_file_off = 0;
        for (i = 0; i < hdr.e_phnum; i++) {
            if (phdrs[i].p_type == PT_DYNAMIC) {
                dyn_file_off = phdrs[i].p_offset;
                break;
            }
        }
        dl_read_at(handle, dyn_section, dyn_size, dyn_file_off);

        uint32_t nd = dyn_size / sizeof(Elf32_Dyn);
        for (i = 0; i < nd; i++) {
            switch (dyn_section[i].d_tag) {
                case DT_NULL:    goto dyn_done;
                case DT_SYMTAB:  dt_symtab = load_base + dyn_section[i].d_un.d_ptr; break;
                case DT_STRTAB:  dt_strtab = load_base + dyn_section[i].d_un.d_ptr; break;
                case DT_STRSZ:   dt_strsz  = dyn_section[i].d_un.d_val;             break;
                case DT_REL:     dt_rel    = load_base + dyn_section[i].d_un.d_ptr; break;
                case DT_RELSZ:   dt_relsz  = dyn_section[i].d_un.d_val;             break;
                case DT_JMPREL:  dt_jmprel = load_base + dyn_section[i].d_un.d_ptr; break;
                case DT_PLTRELSZ:dt_pltrelsz = dyn_section[i].d_un.d_val;           break;
            }
        }
        dyn_done:;
    }

    /* -------------------------------------------------------------------------
     * Passo 3: Carica symtab e strtab in memoria kernel (per resolver)
     * ------------------------------------------------------------------------- */
    if (dt_symtab && dt_strtab && dt_strsz) {
        /* Stima dimensione symtab (approssimazione: spazio fino a strtab) */
        uint32_t sym_size = (dt_strtab > dt_symtab)
                            ? (dt_strtab - dt_symtab) : 256 * sizeof(Elf32_Sym);
        uint32_t sym_count = sym_size / sizeof(Elf32_Sym);

        symtab_copy = (Elf32_Sym *)kmalloc(sym_size);
        strtab_copy = (char *)kmalloc(dt_strsz + 1);

        if (symtab_copy && strtab_copy) {
            /* Copia dalle pagine mappate nel processo */
            for (i = 0; i < sym_count; i++) {
                uint32_t sym_vaddr = dt_symtab + i * sizeof(Elf32_Sym);
                uint32_t sym_phys  = paging_get_physical(proc->page_directory, sym_vaddr);
                if (!sym_phys) break;
                uint8_t *src = (uint8_t *)(sym_phys - (sym_vaddr & 0xFFF)) + (sym_vaddr & 0xFFF);
                uint8_t *dst = (uint8_t *)&symtab_copy[i];
                uint32_t k;
                for (k = 0; k < sizeof(Elf32_Sym); k++) dst[k] = src[k];
            }

            /* Copia strtab */
            for (i = 0; i < dt_strsz; i++) {
                uint32_t sa = dt_strtab + i;
                uint32_t sp = paging_get_physical(proc->page_directory, sa);
                if (!sp) break;
                strtab_copy[i] = *(char *)(sp - (sa & 0xFFF) + (sa & 0xFFF));
            }
            strtab_copy[dt_strsz] = '\0';
        }
    }

    /* -------------------------------------------------------------------------
     * Passo 4: Applica relocazioni .rel
     * ------------------------------------------------------------------------- */
    if (dt_rel && dt_relsz) {
        uint32_t rel_count = dt_relsz / sizeof(Elf32_Rel);
        rel_buf = (Elf32_Rel *)kmalloc(dt_relsz);
        if (rel_buf) {
            /* Copia dalla memoria processo */
            for (i = 0; i < rel_count; i++) {
                uint32_t ra = dt_rel + i * sizeof(Elf32_Rel);
                uint32_t rp = paging_get_physical(proc->page_directory, ra);
                if (!rp) break;
                uint8_t *src = (uint8_t *)(rp - (ra & 0xFFF)) + (ra & 0xFFF);
                uint8_t *dst = (uint8_t *)&rel_buf[i];
                uint32_t k;
                for (k = 0; k < sizeof(Elf32_Rel); k++) dst[k] = src[k];
            }
            dl_apply_relocations(proc, load_base, rel_buf, rel_count,
                                  symtab_copy, strtab_copy);
            kfree(rel_buf);
            rel_buf = NULL;
        }
    }

    /* Applica anche PLT relocazioni (.rel.plt) */
    if (dt_jmprel && dt_pltrelsz) {
        uint32_t plt_count = dt_pltrelsz / sizeof(Elf32_Rel);
        rel_buf = (Elf32_Rel *)kmalloc(dt_pltrelsz);
        if (rel_buf) {
            for (i = 0; i < plt_count; i++) {
                uint32_t ra = dt_jmprel + i * sizeof(Elf32_Rel);
                uint32_t rp = paging_get_physical(proc->page_directory, ra);
                if (!rp) break;
                uint8_t *src = (uint8_t *)(rp - (ra & 0xFFF)) + (ra & 0xFFF);
                uint8_t *dst = (uint8_t *)&rel_buf[i];
                uint32_t k;
                for (k = 0; k < sizeof(Elf32_Rel); k++) dst[k] = src[k];
            }
            dl_apply_relocations(proc, load_base, rel_buf, plt_count,
                                  symtab_copy, strtab_copy);
        }
    }

    /* -------------------------------------------------------------------------
     * Passo 5: Registra la libreria nella cache
     * ------------------------------------------------------------------------- */
    if (g_lib_count < DL_MAX_LIBS) {
        LoadedLib *lib = &g_loaded_libs[g_lib_count++];

        /* Nome = ultima componente del path */
        const char *base_name = path;
        const char *p = path;
        while (*p) { if (*p == '/') base_name = p + 1; p++; }
        dl_strcpy(lib->name, base_name, DL_NAME_LEN);

        lib->base         = load_base;
        lib->size         = 0;   /* Calcolato sommando PT_LOAD */
        lib->symtab       = symtab_copy;
        lib->symtab_count = (symtab_copy && dt_strtab > dt_symtab)
                            ? (dt_strtab - dt_symtab) / sizeof(Elf32_Sym) : 0;
        lib->strtab       = strtab_copy;

        /* Non liberare symtab_copy e strtab_copy: servono per la cache */
        symtab_copy = NULL;
        strtab_copy = NULL;

        if (out_lib) *out_lib = *lib;
    }

    klog(LOG_INFO, "DYNLINK: '%s' caricata OK a 0x%08x", path, load_base);
    ret = 0;

cleanup:
    vfs_close(handle);
    if (phdrs)       kfree(phdrs);
    if (seg_buf)     kfree(seg_buf);
    if (dyn_section) kfree(dyn_section);
    if (symtab_copy) kfree(symtab_copy);
    if (strtab_copy) kfree(strtab_copy);
    if (rel_buf)     kfree(rel_buf);
    return ret;
}

/* =============================================================================
 * dynlink_init — Inizializza il dynamic linker per un processo
 *
 * Deve essere chiamato DOPO elf_load() (che carica l'eseguibile principale).
 * Carica tutte le librerie elencate in PT_INTERP / DT_NEEDED.
 *
 * Per EX-OS: le librerie sono cercate in /lib/.
 * La libreria base libc.so è sempre caricata per prima.
 *
 * load_cursor: indirizzo virtuale dove iniziare a caricare le .so
 *              (deve essere dopo l'eseguibile principale, es. 0x10000000)
 * ============================================================================= */
int dynlink_init(Process *proc, uint32_t load_cursor)
{
    g_lib_count = 0;

    klog(LOG_INFO, "DYNLINK: inizializzazione per PID %u, base 0x%08x",
         proc->pid, load_cursor);

    /* Carica la libc minimale di sistema */
    LoadedLib lib;
    if (dl_load_so("/lib/libc.so", proc, load_cursor, &lib) == 0) {
        load_cursor += ALIGN_UP(lib.size + 0x100000, PAGE_SIZE);
        klog(LOG_INFO, "DYNLINK: libc.so caricata");
    } else {
        klog(LOG_WARN, "DYNLINK: libc.so non trovata in /lib/ - linking ridotto");
    }

    return 0;
}

/* =============================================================================
 * dynlink_load_lib — Carica una libreria specifica per nome
 *
 * name:  nome libreria (es. "libmath.so")
 * proc:  processo destinazione
 * base:  indirizzo base dove mapparla
 * ============================================================================= */
int dynlink_load_lib(const char *name, Process *proc, uint32_t base)
{
    char path[128] = "/lib/";
    uint32_t i = 5;
    const char *n = name;

    /* Controlla se già caricata */
    if (dl_find_loaded(name)) {
        klog(LOG_DEBUG, "DYNLINK: '%s' gia' in cache", name);
        return 0;
    }

    /* Costruisci percorso completo */
    while (*n && i < 127) path[i++] = *n++;
    path[i] = '\0';

    LoadedLib lib;
    return dl_load_so(path, proc, base, &lib);
}

/* =============================================================================
 * dynlink_resolve — Risolve un simbolo per nome (chiamato dalla PLT a runtime)
 *
 * In un dynamic linker completo questo sarebbe il "lazy binding resolver".
 * In EX-OS lo usiamo solo internamente per la risoluzione al caricamento.
 * ============================================================================= */
uint32_t dynlink_resolve(const char *name)
{
    uint32_t addr = dl_lookup_symbol(name);
    if (addr == 0) {
        klog(LOG_WARN, "DYNLINK: simbolo non trovato: '%s'", name);
    }
    return addr;
}

/* =============================================================================
 * dynlink_reset — Resetta la cache librerie (da chiamare a proc_exit)
 * ============================================================================= */
void dynlink_reset(void)
{
    uint32_t i;
    for (i = 0; i < g_lib_count; i++) {
        if (g_loaded_libs[i].symtab) {
            kfree(g_loaded_libs[i].symtab);
            g_loaded_libs[i].symtab = NULL;
        }
        if (g_loaded_libs[i].strtab) {
            kfree((void *)g_loaded_libs[i].strtab);
            g_loaded_libs[i].strtab = NULL;
        }
    }
    g_lib_count = 0;
}
