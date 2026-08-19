/* =============================================================================
 * kernel/loader/drvmgr.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Driver Manager — carica e gestisce i moduli driver ELF (.drv) da /dev/.
 *
 * I driver in EX-OS sono ELF shared object (ET_DYN) caricati nel kernel
 * space (ring0) ma in pagine separate dal kernel core.
 * Un crash nel codice driver NON può corrompere il kernel perché:
 *   - Il kernel ha uno stack separato da ogni driver
 *   - Le chiamate al driver avvengono tramite puntatori a funzione
 *     in una tabella protetta (DriverTable)
 *   - Il kernel verifica i return value e gestisce fault via ISR #PF
 *
 * Interfaccia standard driver (esportata da ogni .drv):
 *   int  drv_init(void)                   — inizializzazione
 *   int  drv_read(void *buf, size_t n)    — lettura
 *   int  drv_write(const void *buf, size_t n) — scrittura
 *   int  drv_ioctl(int cmd, void *arg)    — controllo
 *   void drv_exit(void)                   — deinizializzazione
 *
 * Il driver manager:
 *   1. Legge la lista moduli da kernel.cfg [modules]
 *   2. Carica ogni .drv come ELF in memoria kernel (non utente)
 *   3. Risolve i simboli kernel esportati (drv_init, drv_read, ecc.)
 *   4. Chiama drv_init() e registra nella DriverTable
 *   5. Espone drvmgr_get() per accedere ai driver per nome
 * ============================================================================= */

#include "kernel.h"
#include "vga.h"
#include "drvmgr.h"
#include "fat12.h"
#include "vfs.h"
#include "pmm.h"
#include "paging.h"
#include "kmalloc.h"
#include "cfg.h"
#include "idt.h"
#include "isr.h"
#include "sched.h"

/* =============================================================================
 * Strutture ELF32 minime per il caricamento driver
 * (i driver sono kernel-space quindi usiamo indirizzi fisici diretti)
 * ============================================================================= */
typedef struct PACKED {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} DrvElfHdr;

typedef struct PACKED {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} DrvElfPhdr;

typedef struct PACKED {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} DrvElfShdr;

typedef struct PACKED {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} DrvElfSym;

typedef struct PACKED {
    uint32_t r_offset;
    uint32_t r_info;
} DrvElfRel;

typedef struct PACKED {
    int32_t  d_tag;
    uint32_t d_val;
} DrvElfDyn;

#define DRV_PT_LOAD     1
#define DRV_PT_DYNAMIC  2
#define DRV_SHT_SYMTAB  2
#define DRV_SHT_STRTAB  3
#define DRV_SHT_DYNSYM  11
#define DRV_SHT_REL     9
#define DRV_SHT_RELA    4
#define DRV_SHT_DYNAMIC 6

#define DRV_R_SYM(i)    ((i) >> 8)
#define DRV_R_TYPE(i)   ((uint8_t)(i))

#define DRV_R_386_32        1
#define DRV_R_386_PC32      2
#define DRV_R_386_GLOB_DAT  6
#define DRV_R_386_JMP_SLOT  7
#define DRV_R_386_RELATIVE  8

#define DRV_STB_GLOBAL  1
#define DRV_ST_BIND(i)  ((i) >> 4)

/* =============================================================================
 * Tabella dei simboli del kernel esportati ai driver
 *
 * I driver possono chiamare funzioni del kernel tramite questi simboli.
 * Aggiungere qui qualsiasi funzione che i driver devono poter usare.
 * ============================================================================= */
typedef struct {
    const char *name;
    uint32_t    addr;
} KernelSymbol;

/* Forward declarations per le funzioni kernel */
/* Tutte le funzioni kernel sono dichiarate negli header inclusi sopra:
 * kernel.h, vga.h, idt.h, isr.h, sched.h, pmm.h, kmalloc.h */

static const KernelSymbol g_kernel_symbols[] = {
    /* Output */
    { "vga_putchar",           (uint32_t)vga_putchar           },
    { "vga_puts",              (uint32_t)vga_puts              },
    { "vga_setcolor",          (uint32_t)vga_setcolor          },
    { "kprintf",               (uint32_t)kprintf               },
    { "klog",                  (uint32_t)klog                  },
    /* I/O porte */
    { "port_inb",              (uint32_t)port_inb              },
    { "port_outb",             (uint32_t)port_outb             },
    { "port_inw",              (uint32_t)port_inw              },
    { "port_outw",             (uint32_t)port_outw             },
    { "io_delay",              (uint32_t)io_delay              },
    /* Interrupt */
    { "irq_register_handler",  (uint32_t)irq_register_handler  },
    { "pic_mask_irq",          (uint32_t)pic_mask_irq          },
    { "pic_unmask_irq",        (uint32_t)pic_unmask_irq        },
    { "pic_send_eoi",          (uint32_t)pic_send_eoi          },
    { "interrupts_enable",     (uint32_t)interrupts_enable     },
    { "interrupts_disable",    (uint32_t)interrupts_disable    },
    /* Scheduler */
    { "sched_block",           (uint32_t)sched_block           },
    { "sched_unblock",         (uint32_t)sched_unblock         },
    /* Memoria */
    { "kmalloc",               (uint32_t)kmalloc               },
    { "kfree",                 (uint32_t)kfree                 },
    { "pmm_alloc_page",        (uint32_t)pmm_alloc_page        },
    { "pmm_free_page",         (uint32_t)pmm_free_page         },
    /* Sentinella */
    { NULL, 0 }
};

/* =============================================================================
 * Tabella driver caricati
 * ============================================================================= */
#define DRV_MAX     16
#define DRV_NAME_LEN 32

static DriverEntry g_drivers[DRV_MAX];
static uint32_t    g_drv_count = 0;

/* =============================================================================
 * Helper: cerca un simbolo nella tabella kernel
 * ============================================================================= */
static uint32_t ksym_lookup(const char *name)
{
    const KernelSymbol *s = g_kernel_symbols;
    while (s->name) {
        {
            const char *a = s->name, *b = name;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a == *b) return s->addr;
        }
        s++;
    }
    return 0;
}

/* =============================================================================
 * Helper: stringa
 * ============================================================================= */
static void drv_strcpy(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    while (i < max - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int drv_strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* =============================================================================
 * drvmgr_load_driver — Carica un singolo driver ELF in kernel space
 *
 * Il driver viene caricato in pagine fisiche allocate dal PMM,
 * accessibili direttamente con mapping identità (il kernel usa
 * indirizzi fisici = virtuali nella sua PD).
 *
 * Dopo il caricamento risolve i simboli:
 *   drv_init, drv_read, drv_write, drv_ioctl, drv_exit
 *
 * Ritorna: 0 = successo, -1 = errore
 * ============================================================================= */
static int drvmgr_load_driver(const char *name, const char *path,
                                DriverEntry *entry)
{
    int         handle;
    DrvElfHdr   hdr;
    DrvElfPhdr *phdrs     = NULL;
    DrvElfShdr *shdrs     = NULL;
    DrvElfSym  *symtab    = NULL;
    char       *strtab    = NULL;
    DrvElfSym  *dynsym    = NULL;  /* symtab usato dalle relocation (.rel.plt
                                      * indicizza .dynsym, NON .symtab: sono
                                      * due tabelle distinte con ordine e
                                      * dimensione diversi) */
    char       *dynstr    = NULL;
    char       *shstrtab  = NULL;
    uint8_t    *load_base = NULL;
    uint8_t    *seg_buf   = NULL;
    uint32_t    total_size = 0;
    uint32_t    i;
    int         ret = -1;

    klog(LOG_INFO, "DRVMGR: caricamento driver '%s' da '%s'...", name, path);

    /* Apri il file */
    handle = vfs_open(path, 0x0000);
    if (handle < 0) {
        klog(LOG_ERROR, "DRVMGR: '%s' non trovato", path);
        return -1;
    }

    /* Leggi ELF header */
    if (vfs_read(handle, &hdr, sizeof(hdr), 0) != (int)sizeof(hdr)) {
        klog(LOG_ERROR, "DRVMGR: errore lettura header '%s'", path);
        goto cleanup;
    }

    /* Verifica magic */
    if (hdr.e_ident[0] != 0x7F || hdr.e_ident[1] != 'E' ||
        hdr.e_ident[2] != 'L'  || hdr.e_ident[3] != 'F') {
        klog(LOG_ERROR, "DRVMGR: '%s' non e' un ELF", path);
        goto cleanup;
    }

    if (hdr.e_machine != 3) {
        klog(LOG_ERROR, "DRVMGR: architettura non i386: %u", hdr.e_machine);
        goto cleanup;
    }

    /* -------------------------------------------------------------------------
     * Leggi Program Headers e calcola spazio totale
     * ------------------------------------------------------------------------- */
    phdrs = (DrvElfPhdr *)kmalloc(hdr.e_phnum * sizeof(DrvElfPhdr));
    if (!phdrs) goto cleanup;
    vfs_read(handle, phdrs, hdr.e_phnum * sizeof(DrvElfPhdr), hdr.e_phoff);

    /* Calcola dimensione totale mappata (max vaddr + memsz dei PT_LOAD) */
    for (i = 0; i < hdr.e_phnum; i++) {
        if (phdrs[i].p_type != DRV_PT_LOAD) continue;
        uint32_t end = phdrs[i].p_vaddr + phdrs[i].p_memsz;
        if (end > total_size) total_size = end;
    }
    total_size = ALIGN_UP(total_size, PAGE_SIZE);

    /* Alloca memoria contigua per il driver */
    uint32_t pages = total_size / PAGE_SIZE;
    /* Fascia kernel: l'immagine del modulo viene rilocata dal kernel al
     * proprio indirizzo fisico (load_base qui sotto). Stessa ragione di
     * dynlink.c. */
    uint32_t phys_base = pmm_alloc_pages_kernel(pages);
    if (phys_base == 0) {
        klog(LOG_ERROR, "DRVMGR: OOM allocando %u pagine per driver", pages);
        goto cleanup;
    }

    load_base = (uint8_t *)phys_base;

    /* Azzera tutta la zona del driver */
    {
        uint8_t *p = load_base;
        uint32_t n = total_size;
        while (n--) *p++ = 0;
    }

    /* -------------------------------------------------------------------------
     * Copia i segmenti PT_LOAD in memoria
     * Il driver è ET_DYN quindi gli indirizzi virtuali sono relativi.
     * Base fisica = phys_base → virtual driver address = phys_base + p_vaddr
     * ------------------------------------------------------------------------- */
    seg_buf = (uint8_t *)kmalloc(PAGE_SIZE);
    if (!seg_buf) goto cleanup;

    for (i = 0; i < hdr.e_phnum; i++) {
        DrvElfPhdr *ph = &phdrs[i];
        if (ph->p_type != DRV_PT_LOAD) continue;
        if (ph->p_filesz == 0) continue;

        uint8_t  *dst     = load_base + ph->p_vaddr;
        uint32_t  file_off = ph->p_offset;
        uint32_t  remain   = ph->p_filesz;

        while (remain > 0) {
            uint32_t chunk = (remain < PAGE_SIZE) ? remain : PAGE_SIZE;
            int n = vfs_read(handle, seg_buf, chunk, file_off);
            if (n <= 0) {
                klog(LOG_ERROR, "DRVMGR: errore lettura segmento driver");
                goto cleanup;
            }
            uint32_t k;
            for (k = 0; k < (uint32_t)n; k++) dst[k] = seg_buf[k];
            dst      += n;
            file_off += (uint32_t)n;
            remain   -= (uint32_t)n;
        }
    }

    /* -------------------------------------------------------------------------
     * Leggi Section Headers (per symtab, strtab, .rel)
     * ------------------------------------------------------------------------- */
    shdrs = (DrvElfShdr *)kmalloc(hdr.e_shnum * sizeof(DrvElfShdr));
    if (!shdrs) goto cleanup;
    vfs_read(handle, shdrs, hdr.e_shnum * sizeof(DrvElfShdr), hdr.e_shoff);

    /* Leggi shstrtab (nomi sezioni) */
    if (hdr.e_shstrndx < hdr.e_shnum) {
        DrvElfShdr *shstr_sh = &shdrs[hdr.e_shstrndx];
        shstrtab = (char *)kmalloc(shstr_sh->sh_size + 1);
        if (shstrtab) {
            vfs_read(handle, shstrtab, shstr_sh->sh_size, shstr_sh->sh_offset);
            shstrtab[shstr_sh->sh_size] = '\0';
        }
    }

    /* Trova symtab e strtab */
    uint32_t sym_count = 0;
    for (i = 0; i < hdr.e_shnum; i++) {
        if (shdrs[i].sh_type == DRV_SHT_SYMTAB) {
            sym_count = shdrs[i].sh_size / sizeof(DrvElfSym);
            symtab = (DrvElfSym *)kmalloc(shdrs[i].sh_size);
            if (symtab)
                vfs_read(handle, symtab, shdrs[i].sh_size, shdrs[i].sh_offset);

            /* Strtab associato */
            uint32_t stridx = shdrs[i].sh_link;
            if (stridx < hdr.e_shnum) {
                strtab = (char *)kmalloc(shdrs[stridx].sh_size + 1);
                if (strtab) {
                    vfs_read(handle, strtab, shdrs[stridx].sh_size,
                               shdrs[stridx].sh_offset);
                    strtab[shdrs[stridx].sh_size] = '\0';
                }
            }
            break;
        }
    }

    /* Trova dynsym e dynstr: le relocation (.rel.plt, .rel.dyn) indicizzano
     * .dynsym, una symbol table diversa da .symtab (ordine ed entry count
     * differenti). Senza caricarla separatamente, drvmgr risolveva ogni
     * relocation leggendo l'indice sbagliato dentro .symtab, ottenendo
     * simboli/indirizzi casuali — causa del salto a codice invalido
     * (page fault / invalid opcode) osservato all'avvio di drv_init(). */
    uint32_t dynsym_count = 0;
    for (i = 0; i < hdr.e_shnum; i++) {
        if (shdrs[i].sh_type == DRV_SHT_DYNSYM) {
            dynsym_count = shdrs[i].sh_size / sizeof(DrvElfSym);
            dynsym = (DrvElfSym *)kmalloc(shdrs[i].sh_size);
            if (dynsym)
                vfs_read(handle, dynsym, shdrs[i].sh_size, shdrs[i].sh_offset);

            uint32_t stridx = shdrs[i].sh_link;
            if (stridx < hdr.e_shnum) {
                dynstr = (char *)kmalloc(shdrs[stridx].sh_size + 1);
                if (dynstr) {
                    vfs_read(handle, dynstr, shdrs[stridx].sh_size,
                               shdrs[stridx].sh_offset);
                    dynstr[shdrs[stridx].sh_size] = '\0';
                }
            }
            break;
        }
    }
    (void)dynsym_count;

    /* -------------------------------------------------------------------------
     * Applica relocazioni sulle sezioni .rel*
     * ------------------------------------------------------------------------- */
    for (i = 0; i < hdr.e_shnum; i++) {
        if (shdrs[i].sh_type != DRV_SHT_REL) continue;

        uint32_t   rel_count = shdrs[i].sh_size / sizeof(DrvElfRel);
        DrvElfRel *rels      = (DrvElfRel *)kmalloc(shdrs[i].sh_size);
        if (!rels) continue;

        vfs_read(handle, rels, shdrs[i].sh_size, shdrs[i].sh_offset);

        /* NOTA: r_offset in ogni relocation ELF e' GIA' un indirizzo
         * virtuale assoluto (relativo alla base dell'immagine ET_DYN),
         * non un offset relativo alla sezione target indicata da
         * sh_info. Il patch va quindi calcolato come load_base + r_off,
         * NON come (load_base + sh_addr_sezione_target) + r_off: quella
         * doppia applicazione dell'offset di sezione produceva un
         * puntatore di scrittura fuori dalla memoria allocata per il
         * driver, corrompendo memoria del kernel non correlata — causa
         * dei crash osservati (page fault su QEMU, invalid opcode su
         * VMware) all'avvio di drv_init(). */

        uint32_t j;
        for (j = 0; j < rel_count; j++) {
            uint32_t  r_off  = rels[j].r_offset;
            uint32_t  r_sym  = DRV_R_SYM(rels[j].r_info);
            uint8_t   r_type = DRV_R_TYPE(rels[j].r_info);
            uint32_t *patch  = (uint32_t *)(load_base + r_off);
            uint32_t  sym_addr = 0;

            if (r_sym != 0 && dynsym && dynstr) {
                DrvElfSym *sym = &dynsym[r_sym];
                if (sym->st_shndx == 0 /* SHN_UNDEF */) {
                    /* Simbolo esterno: cerca nel kernel */
                    const char *sym_name = dynstr + sym->st_name;
                    sym_addr = ksym_lookup(sym_name);
                    if (sym_addr == 0) {
                        klog(LOG_WARN, "DRVMGR: simbolo kernel non trovato: '%s'",
                             sym_name);
                    }
                } else {
                    /* Simbolo locale: base + valore */
                    sym_addr = phys_base + sym->st_value;
                }
            }

            switch (r_type) {
                case DRV_R_386_32:
                    *patch = sym_addr + *patch;
                    break;
                case DRV_R_386_PC32:
                    *patch = sym_addr + *patch - (uint32_t)patch;
                    break;
                case DRV_R_386_GLOB_DAT:
                case DRV_R_386_JMP_SLOT:
                    *patch = sym_addr;
                    break;
                case DRV_R_386_RELATIVE:
                    *patch = phys_base + *patch;
                    break;
            }
        }
        kfree(rels);
    }

    /* -------------------------------------------------------------------------
     * Risolvi simboli drv_init, drv_read, drv_write, drv_ioctl, drv_exit
     * dalla symbol table del driver
     * ------------------------------------------------------------------------- */
    entry->drv_init  = NULL;
    entry->drv_read  = NULL;
    entry->drv_write = NULL;
    entry->drv_ioctl = NULL;
    entry->drv_exit  = NULL;

    if (symtab && strtab) {
        for (i = 0; i < sym_count; i++) {
            if (DRV_ST_BIND(symtab[i].st_info) == DRV_STB_GLOBAL &&
                symtab[i].st_value != 0) {
                const char *sname = strtab + symtab[i].st_name;
                uint32_t    saddr = phys_base + symtab[i].st_value;

                if (drv_strcmp(sname, "drv_init")  == 0)
                    entry->drv_init  = (DrvInitFn)saddr;
                else if (drv_strcmp(sname, "drv_read")  == 0)
                    entry->drv_read  = (DrvReadFn)saddr;
                else if (drv_strcmp(sname, "drv_write") == 0)
                    entry->drv_write = (DrvWriteFn)saddr;
                else if (drv_strcmp(sname, "drv_ioctl") == 0)
                    entry->drv_ioctl = (DrvIoctlFn)saddr;
                else if (drv_strcmp(sname, "drv_exit")  == 0)
                    entry->drv_exit  = (DrvExitFn)saddr;
            }
        }
    }

    /* Verifica che almeno drv_init sia presente */
    if (!entry->drv_init) {
        klog(LOG_ERROR, "DRVMGR: driver '%s' non esporta drv_init()", name);
        pmm_free_pages(phys_base, pages);
        goto cleanup;
    }

    /* -------------------------------------------------------------------------
     * Chiama drv_init() per inizializzare il driver
     * ------------------------------------------------------------------------- */
    klog(LOG_INFO, "DRVMGR: chiamata drv_init() per '%s'...", name);
    int init_ret = entry->drv_init();
    if (init_ret != 0) {
        klog(LOG_ERROR, "DRVMGR: drv_init() fallita per '%s' (ret=%d)",
             name, init_ret);
        pmm_free_pages(phys_base, pages);
        goto cleanup;
    }

    /* -------------------------------------------------------------------------
     * Registra il driver nella tabella
     * ------------------------------------------------------------------------- */
    drv_strcpy(entry->name, name, DRV_NAME_LEN);
    entry->phys_base   = phys_base;
    entry->pages       = pages;
    entry->loaded      = 1;

    klog(LOG_INFO, "DRVMGR: driver '%s' caricato OK "
         "(base=0x%08x size=%u pagine)",
         name, phys_base, pages);
    klog(LOG_INFO, "DRVMGR:   init=%p read=%p write=%p ioctl=%p exit=%p",
         entry->drv_init, entry->drv_read,
         entry->drv_write, entry->drv_ioctl, entry->drv_exit);

    ret = 0;

cleanup:
    vfs_close(handle);
    if (phdrs)    kfree(phdrs);
    if (shdrs)    kfree(shdrs);
    if (symtab)   kfree(symtab);
    if (strtab)   kfree(strtab);
    if (dynsym)   kfree(dynsym);
    if (dynstr)   kfree(dynstr);
    if (shstrtab) kfree(shstrtab);
    if (seg_buf)  kfree(seg_buf);
    return ret;
}

/* =============================================================================
 * drvmgr_init — Carica tutti i driver elencati in kernel.cfg [modules]
 * ============================================================================= */
void drvmgr_init(void)
{
    KernelConfig *cfg = cfg_get();
    uint32_t      i;

    klog(LOG_INFO, "DRVMGR: inizializzazione driver manager...");
    klog(LOG_INFO, "DRVMGR: %u moduli da caricare", cfg->module_count);

    g_drv_count = 0;

    for (i = 0; i < cfg->module_count && g_drv_count < DRV_MAX; i++) {
        const char *name = cfg->modules[i].name;
        const char *path = cfg->modules[i].path;

        if (!name[0] || !path[0]) continue;

        /* Salta il driver TTY: la vera implementazione e' compilata
         * staticamente nel kernel (drivers/tty/tty.c, inizializzata al
         * PASSO 14 con extern drv_init()) e usata direttamente da
         * sys_read/sys_write per stdin/stdout. Caricare anche il modulo
         * dinamico /dev/tty.drv duplicherebbe l'handler IRQ1 e il
         * buffer di input, causando conflitti. */
        if (drv_strcmp(name, "tty") == 0) {
            klog(LOG_INFO, "DRVMGR: driver '%s' gia' attivo staticamente nel kernel, skip",
                 name);
            continue;
        }

        DriverEntry *entry = &g_drivers[g_drv_count];
        if (drvmgr_load_driver(name, path, entry) == 0) {
            g_drv_count++;
        } else {
            klog(LOG_WARN, "DRVMGR: caricamento driver '%s' fallito", name);
        }
    }

    klog(LOG_INFO, "DRVMGR: %u/%u driver caricati con successo",
         g_drv_count, cfg->module_count);
}

/* =============================================================================
 * drvmgr_get — Ritorna il DriverEntry per nome, NULL se non trovato
 * ============================================================================= */
DriverEntry *drvmgr_get(const char *name)
{
    uint32_t i;
    for (i = 0; i < g_drv_count; i++) {
        if (drv_strcmp(g_drivers[i].name, name) == 0) {
            return &g_drivers[i];
        }
    }
    return NULL;
}

/* =============================================================================
 * drvmgr_unload — Scarica un driver (chiama drv_exit e libera memoria)
 * ============================================================================= */
int drvmgr_unload(const char *name)
{
    uint32_t i;
    for (i = 0; i < g_drv_count; i++) {
        if (drv_strcmp(g_drivers[i].name, name) == 0) {
            DriverEntry *e = &g_drivers[i];
            if (e->drv_exit) e->drv_exit();
            pmm_free_pages(e->phys_base, e->pages);
            e->loaded = 0;
            klog(LOG_INFO, "DRVMGR: driver '%s' scaricato", name);

            /* Compatta la tabella */
            uint32_t j;
            for (j = i; j < g_drv_count - 1; j++) {
                g_drivers[j] = g_drivers[j + 1];
            }
            g_drv_count--;
            return 0;
        }
    }
    return -1;
}

/* =============================================================================
 * drvmgr_dump — Stampa la tabella dei driver caricati
 * ============================================================================= */
void drvmgr_dump(void)
{
    uint32_t i;
    klog(LOG_INFO, "DRVMGR: %u driver caricati:", g_drv_count);
    for (i = 0; i < g_drv_count; i++) {
        DriverEntry *e = &g_drivers[i];
        klog(LOG_INFO, "  [%u] %-16s base=0x%08x pagine=%u",
             i, e->name, e->phys_base, e->pages);
    }
}
