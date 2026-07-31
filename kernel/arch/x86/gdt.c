/* =============================================================================
 * kernel/arch/x86/gdt.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * ============================================================================= */

#include "kernel.h"
#include "gdt.h"

/* =============================================================================
 * Struttura di un descrittore GDT (8 byte — formato hardware x86)
 *
 * Layout fisico (bit):
 *  63       56 55   52 51  48 47      40 39      16 15      0
 *  base[31:24] flags limit  access_byte base[23:0] limit[15:0]
 *              [19:16]
 *
 * Byte di accesso (offset 5):
 *  Bit 7: Present (P)         — 1 = descrittore valido
 *  Bit 6-5: DPL               — 0=ring0, 3=ring3
 *  Bit 4: Descriptor Type (S) — 1=code/data, 0=system
 *  Bit 3: Executable (E)      — 1=code, 0=data
 *  Bit 2: Direction/Conforming (DC)
 *  Bit 1: Read/Write (RW)     — code: readable; data: writable
 *  Bit 0: Accessed (A)        — CPU lo imposta a 1 al primo accesso
 *
 * Byte granularity (offset 6, nibble alto):
 *  Bit 7: Granularity (G)     — 0=byte, 1=4KB (page)
 *  Bit 6: Size (D/B)          — 0=16bit, 1=32bit
 *  Bit 5: Long mode (L)       — 0 (non usiamo 64-bit)
 *  Bit 4: Available (AVL)     — uso libero OS
 *  Bit 3-0: Limit[19:16]
 * ============================================================================= */

typedef struct PACKED {
    uint16_t    limit_low;      /* Limite [15:0] */
    uint16_t    base_low;       /* Base [15:0] */
    uint8_t     base_mid;       /* Base [23:16] */
    uint8_t     access;         /* Byte di accesso */
    uint8_t     granularity;    /* Flags (nibble alto) + Limite [19:16] (nibble basso) */
    uint8_t     base_high;      /* Base [31:24] */
} GDTDescriptor;

/* Struttura GDTR per istruzione lgdt */
typedef struct PACKED {
    uint16_t    limit;          /* Dimensione GDT in byte - 1 */
    uint32_t    base;           /* Indirizzo lineare GDT */
} GDTRegister;

/* =============================================================================
 * TSS — Task State Segment (x86 hardware)
 *
 * Il TSS è usato dalla CPU durante le transizioni di privilegio (ring3→ring0)
 * per caricare ESP e SS del ring 0. Noi lo usiamo SOLO per questo scopo.
 * Il context switch software salva/ripristina i registri manualmente.
 * ============================================================================= */
typedef struct PACKED {
    uint32_t    prev_tss;       /* TSS precedente (non usato) */
    uint32_t    esp0;           /* ESP per ring 0 (IMPORTANTE: usato dal CPU) */
    uint32_t    ss0;            /* SS per ring 0 */
    uint32_t    esp1;           /* Non usato */
    uint32_t    ss1;
    uint32_t    esp2;
    uint32_t    ss2;
    uint32_t    cr3;            /* Page directory (non usato — gestiamo noi) */
    uint32_t    eip;
    uint32_t    eflags;
    uint32_t    eax, ecx, edx, ebx;
    uint32_t    esp, ebp, esi, edi;
    uint32_t    es, cs, ss, ds, fs, gs;
    uint32_t    ldt;
    uint16_t    trap;
    uint16_t    iomap_base;     /* Offset della I/O permission bitmap */
} TSS;

/* =============================================================================
 * Dati GDT (statici, allocati nel kernel .data)
 * ============================================================================= */
#define GDT_ENTRIES 6

static GDTDescriptor    gdt_table[GDT_ENTRIES] ALIGNED(8);
static GDTRegister      gdt_reg;
static TSS              kernel_tss ALIGNED(4);

/* =============================================================================
 * gdt_set_descriptor — Imposta un descrittore GDT
 *
 * index:   indice nella tabella GDT (0-5)
 * base:    indirizzo base segmento
 * limit:   limite segmento
 * access:  byte di accesso
 * gran:    byte granularity (nibble alto = flags, nibble basso = limit[19:16])
 * ============================================================================= */
static void gdt_set_descriptor(uint32_t index, uint32_t base, uint32_t limit,
                                 uint8_t access, uint8_t gran)
{
    GDTDescriptor *d = &gdt_table[index];

    d->base_low    = (uint16_t)(base & 0xFFFF);
    d->base_mid    = (uint8_t)((base >> 16) & 0xFF);
    d->base_high   = (uint8_t)((base >> 24) & 0xFF);

    d->limit_low   = (uint16_t)(limit & 0xFFFF);
    d->granularity = (uint8_t)((gran & 0xF0) | ((limit >> 16) & 0x0F));

    d->access      = access;
}

/* =============================================================================
 * gdt_install — Installa la GDT definitiva del kernel
 * Chiamata da kernel_main() prima di idt_install()
 * ============================================================================= */
void gdt_install(void)
{
    klog(LOG_INFO, "GDT: installazione...");

    /*
     * Descrittore 0: Null (obbligatorio)
     * Qualsiasi accesso con selettore 0 genera #GP (General Protection Fault)
     */
    gdt_set_descriptor(0, 0, 0, 0, 0);

    /*
     * Descrittore 1: Kernel Code — selettore 0x08
     * Access: 0x9A = 1001 1010b
     *   P=1 (present), DPL=0 (ring0), S=1 (code/data),
     *   E=1 (executable), DC=0, RW=1 (readable), A=0
     * Gran: 0xCF = 1100 1111b
     *   G=1 (4KB), D=1 (32-bit), L=0, AVL=0, Limit[19:16]=F
     * Base=0, Limit=0xFFFFF → 0xFFFFF * 4KB = 4GB
     */
    gdt_set_descriptor(1, 0x00000000, 0xFFFFFFFF, 0x9A, 0xCF);

    /*
     * Descrittore 2: Kernel Data — selettore 0x10
     * Access: 0x92 = 1001 0010b
     *   P=1, DPL=0, S=1, E=0 (data), DC=0, RW=1 (writable), A=0
     */
    gdt_set_descriptor(2, 0x00000000, 0xFFFFFFFF, 0x92, 0xCF);

    /*
     * Descrittore 3: User Code — selettore 0x18 | 0x03 = 0x1B
     * Access: 0xFA = 1111 1010b
     *   P=1, DPL=3 (ring3), S=1, E=1, DC=0, RW=1, A=0
     */
    gdt_set_descriptor(3, 0x00000000, 0xFFFFFFFF, 0xFA, 0xCF);

    /*
     * Descrittore 4: User Data — selettore 0x20 | 0x03 = 0x23
     * Access: 0xF2 = 1111 0010b
     *   P=1, DPL=3, S=1, E=0, DC=0, RW=1, A=0
     */
    gdt_set_descriptor(4, 0x00000000, 0xFFFFFFFF, 0xF2, 0xCF);

    /*
     * Descrittore 5: TSS — selettore 0x28
     * Il TSS è un descrittore di sistema (S=0).
     * Access: 0x89 = 1000 1001b
     *   P=1, DPL=0, S=0 (system), Type=9 (32-bit TSS available)
     * Gran: 0x00 (byte granularity per TSS, non paginato)
     * Base = indirizzo fisico del nostro TSS
     * Limit = sizeof(TSS) - 1
     */
    uint32_t tss_base  = (uint32_t)&kernel_tss;
    uint32_t tss_limit = (uint32_t)sizeof(TSS) - 1;
    gdt_set_descriptor(5, tss_base, tss_limit, 0x89, 0x00);

    /* Inizializza TSS */
    uint8_t *tss_ptr = (uint8_t *)&kernel_tss;
    uint32_t i;
    for (i = 0; i < sizeof(TSS); i++) tss_ptr[i] = 0;

    kernel_tss.ss0        = GDT_KERNEL_DATA_SEL;    /* Stack segment ring 0 */
    kernel_tss.esp0       = 0;                       /* Aggiornato dallo scheduler */
    kernel_tss.iomap_base = sizeof(TSS);             /* Nessuna I/O permission map */

    /* Carica GDTR */
    gdt_reg.limit = (uint16_t)(sizeof(gdt_table) - 1);
    gdt_reg.base  = (uint32_t)gdt_table;

    klog(LOG_INFO, "GDT: base=0x%x limit=%d (%d descrittori)",
         gdt_reg.base, gdt_reg.limit, GDT_ENTRIES);

    /* Flush: carica GDTR e ricarica tutti i registri segmento */
    gdt_flush((uint32_t)&gdt_reg);

    /* Carica TSS nel Task Register */
    tss_flush(GDT_TSS_SEL);

    klog(LOG_INFO, "GDT: installata (code=0x%x data=0x%x tss=0x%x)",
         GDT_KERNEL_CODE_SEL, GDT_KERNEL_DATA_SEL, GDT_TSS_SEL);
}

/* =============================================================================
 * gdt_set_kernel_stack — Aggiorna ESP0 nel TSS
 * Chiamata dallo scheduler ad ogni context switch verso ring3
 * per impostare lo stack kernel del task corrente.
 * ============================================================================= */
void gdt_set_kernel_stack(uint32_t stack_top)
{
    kernel_tss.esp0 = stack_top;
}
