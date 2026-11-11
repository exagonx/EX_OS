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
#include "kmalloc.h"     /* the tables of the extra processors (0.319) */

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
/* =============================================================================
 * ! UNA GDT E UN TSS PER PROCESSORE (tappa 3 dell'SMP, kernel 0.246)
 *
 * Due cose nella GDT sono «del processore che sta girando» e non della
 * macchina: il TSS, da cui la CPU prende la pila del kernel quando arriva un
 * interrupt in ring 3, e il descrittore del TLS, che punta al filo in
 * esecuzione. Con due processori che eseguono due processi servono due
 * copie. La numero 0 e' quella di sempre, del processore d'avvio; le altre
 * le carica ogni processore in piu' quando comincia a lavorare.
 *
 * La voce 7 dice al processore CHI E': un segmento il cui LIMITE e' il suo
 * numero. `lsl` lo legge in un'istruzione, senza toccare l'APIC (che in una
 * macchina virtuale costa un'uscita verso l'ospite): vedi cpu_n() in smp.h.
 * Su una macchina con un processore non la legge nessuno.
 * ========================================================================== */
#define GDT_ENTRIES 8

/* ! AS MANY AS THERE ARE PROCESSORS (0.319, voce 92): up to 0.318 sixteen
 * static copies. The boot processor's is static (it is loaded before the
 * heap exists); the others are allocated by gdt_posti when smp_init has
 * counted the processors, one block each. */
typedef struct {
    GDTDescriptor tabella[GDT_ENTRIES] ALIGNED(8);
    GDTRegister   registro;
    TSS           tss ALIGNED(4);
    /* ! RIGHT AFTER THE TSS, no padding (a byte array follows a packed
     * struct): the TSS's iomap_base says where it starts from the TSS's own
     * base. One byte more of ones, which the processor reads past the last
     * port (0.324, see gdt.h). */
    uint8_t       mappa[GDT_MAPPA_IO_BYTE + 1];
    uint32_t      aperti_da, aperti_a;      /* bytes of mappa not all ones */
} GdtCpu;

_Static_assert(__builtin_offsetof(GdtCpu, mappa) ==
               __builtin_offsetof(GdtCpu, tss) + sizeof(TSS),
               "the I/O bitmap must follow the TSS");

static GdtCpu   gdt_avvio;
static GdtCpu  *gdt_cpu_1[1] = { &gdt_avvio };
static GdtCpu **gdt_cpu = gdt_cpu_1;
static uint32_t gdt_quanti = 1;
#define gdt_table       (gdt_avvio.tabella)
#define gdt_reg         (gdt_avvio.registro)
#define kernel_tss      (gdt_avvio.tss)

volatile uint32_t g_smp_lavora = 0;     /* 1 = piu' processori eseguono processi */

static inline uint32_t gdt_cpu_n(void)
{
    uint32_t n = 0;

    if (!g_smp_lavora) return 0;
    __asm__ volatile ("lsl %1, %0" : "+r"(n) : "r"((uint32_t)0x38));
    return n;
}

/* One block per processor, for `posti` of them (see smp.h). */
int gdt_posti(uint32_t posti)
{
    GdtCpu  **nuovi;
    uint32_t  i, j;

    if (posti <= gdt_quanti) return 0;
    nuovi = (GdtCpu **)kmalloc(posti * sizeof(GdtCpu *));
    if (nuovi == NULL) return -1;
    nuovi[0] = &gdt_avvio;
    for (i = 1; i < posti; i++) {
        uint8_t *b = (uint8_t *)kmalloc_aligned(sizeof(GdtCpu), 64);

        if (b == NULL) {
            while (--i >= 1) kfree_aligned(nuovi[i]);
            kfree(nuovi);
            return -1;
        }
        for (j = 0; j < sizeof(GdtCpu); j++) b[j] = 0;
        nuovi[i] = (GdtCpu *)b;
    }
    gdt_cpu    = nuovi;
    gdt_quanti = posti;
    return 0;
}

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
    /*
     * Descrittore 6: TLS — selettore 0x30 | 0x03 = 0x33
     *
     * Identico a User Data (access 0xF2, granularita' 0xCF) tranne per la
     * BASE, che qui vale zero e viene riscritta a ogni cambio di contesto
     * con il thread pointer del processo che sta per girare. E' il
     * meccanismo con cui `mov %gs:0x0, %ebx` — l'accesso a una variabile
     * __thread nel modello local-exec — legge dal blocco TLS di CHI sta
     * girando invece che da un indirizzo fisso.
     *
     * ! CON BASE ZERO E' INDISTINGUIBILE DA 0x23, ed e' voluto: i
     * processi senza variabili thread-local non pagano niente e non serve
     * un selettore diverso per loro. Un %gs:0 in quel caso legge
     * l'indirizzo lineare 0, che non e' mappato — esattamente come prima
     * che questo descrittore esistesse.
     */
    gdt_set_descriptor(6, 0x00000000, 0xFFFFFFFF, 0xF2, 0xCF);

    /* 7: «chi sono». Un segmento dati del kernel, a byte, col limite uguale
     * al numero del processore: 0 su quello d'avvio. Non lo carica nessuno. */
    gdt_set_descriptor(7, 0x00000000, 0x00000000, 0x92, 0x40);

    uint32_t tss_base  = (uint32_t)&kernel_tss;
    /* the TSS and its I/O bitmap (0.324) */
    uint32_t tss_limit = (uint32_t)(sizeof(TSS) + sizeof(gdt_avvio.mappa)) - 1;
    gdt_set_descriptor(5, tss_base, tss_limit, 0x89, 0x00);

    /* Inizializza TSS */
    uint8_t *tss_ptr = (uint8_t *)&kernel_tss;
    uint32_t i;
    for (i = 0; i < sizeof(TSS); i++) tss_ptr[i] = 0;

    kernel_tss.ss0        = GDT_KERNEL_DATA_SEL;    /* Stack segment ring 0 */
    kernel_tss.esp0       = 0;                       /* Aggiornato dallo scheduler */
    kernel_tss.iomap_base = sizeof(TSS);             /* the bitmap, right after */
    for (i = 0; i < sizeof(gdt_avvio.mappa); i++) gdt_avvio.mappa[i] = 0xFF;
    gdt_avvio.aperti_da = gdt_avvio.aperti_a = 0;

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
void gdt_set_kernel_stack(vaddr_t stack_top)
{
    gdt_cpu[gdt_cpu_n()]->tss.esp0 = stack_top;
}

/* The I/O bitmap of this processor (gdt.h). Interrupts off, from the
 * scheduler or from the system call that asks for it. */
void gdt_porte_apri(uint32_t base, uint32_t quante)
{
    GdtCpu  *c = gdt_cpu[gdt_cpu_n()];
    uint32_t p, fine = base + quante;

    if (fine > 65536u) fine = 65536u;
    if (base >= fine) return;
    for (p = base; p < fine; p++) c->mappa[p >> 3] &= (uint8_t)~(1u << (p & 7));
    if (c->aperti_da == c->aperti_a) { c->aperti_da = base >> 3; c->aperti_a = ((fine - 1) >> 3) + 1; }
    else {
        if ((base >> 3) < c->aperti_da) c->aperti_da = base >> 3;
        if (((fine - 1) >> 3) + 1 > c->aperti_a) c->aperti_a = ((fine - 1) >> 3) + 1;
    }
}

void gdt_porte_chiudi(void)
{
    GdtCpu  *c = gdt_cpu[gdt_cpu_n()];
    uint32_t i;

    for (i = c->aperti_da; i < c->aperti_a; i++) c->mappa[i] = 0xFF;
    c->aperti_da = c->aperti_a = 0;
}

/* La GDT e il TSS del processore numero `n` (da 1 in su): una copia di quella
 * d'avvio col suo TSS e il suo numero, caricata SU CHI CHIAMA. La chiama ogni
 * processore in piu' prima di cominciare a lavorare. */
void gdt_installa_cpu(uint32_t n)
{
    GDTDescriptor *t;
    GdtCpu   *c;
    uint8_t  *b;
    uint32_t  i, base;

    if (n == 0 || n >= gdt_quanti) return;
    c = gdt_cpu[n];
    t = c->tabella;
    for (i = 0; i < GDT_ENTRIES; i++) t[i] = gdt_avvio.tabella[i];

    b = (uint8_t *)&c->tss;
    for (i = 0; i < sizeof(TSS); i++) b[i] = 0;
    c->tss.ss0        = GDT_KERNEL_DATA_SEL;
    c->tss.iomap_base = sizeof(TSS);
    for (i = 0; i < sizeof(c->mappa); i++) c->mappa[i] = 0xFF;
    c->aperti_da = c->aperti_a = 0;

    base = (uint32_t)&c->tss;
    t[5].base_low    = (uint16_t)(base & 0xFFFF);
    t[5].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    t[5].base_high   = (uint8_t)((base >> 24) & 0xFF);
    t[5].access      = 0x89;                    /* TSS libero: `ltr` lo segna occupato */

    t[7].limit_low   = (uint16_t)n;             /* chi sono */

    c->registro.limit = (uint16_t)(sizeof(c->tabella) - 1);
    c->registro.base  = (uint32_t)t;
    gdt_flush((uint32_t)&c->registro);
    tss_flush(GDT_TSS_SEL);
}

/* =============================================================================
 * gdt_set_tls_base — Sposta il descrittore TLS sul processo che sta per girare
 *
 * Chiamata dallo scheduler a ogni cambio di contesto, accanto a
 * gdt_set_kernel_stack: sono le due cose che dipendono dal processo e non
 * stanno nei registri.
 *
 * ! NON SERVE RICARICARE GS. Il selettore che i processi tengono in GS e'
 * sempre lo stesso (0x33); quello che cambia e' il descrittore a cui punta.
 * La CPU rilegge la GDT quando il registro segmento viene caricato, e la
 * parte nascosta (base e limite) resta quella letta allora — ma il
 * processo entrante GS lo ricarica comunque, perche' il suo valore viene
 * ripristinato da context_switch (`pop gs`) subito dopo. E' quel pop a
 * rendere effettiva la base nuova.
 * ============================================================================= */
void gdt_set_tls_base(uint32_t base)
{
    /* Riscrive solo i tre pezzi della base, lasciando access e granularita'
     * come li ha messi gdt_install. */
    GDTDescriptor *t = gdt_cpu[gdt_cpu_n()]->tabella;

    t[6].base_low    = (uint16_t)(base & 0xFFFF);
    t[6].base_mid    = (uint8_t)((base >> 16) & 0xFF);
    t[6].base_high   = (uint8_t)((base >> 24) & 0xFF);
}
