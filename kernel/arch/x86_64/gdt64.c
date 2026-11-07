/* =============================================================================
 * kernel/arch/x86_64/gdt64.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * I SEGMENTI A 64 BIT, E IL TSS (@EXOS-64, tappa 3 — 10 ottobre 2026)
 *
 * In long mode i segmenti non delimitano piu' niente: base e limite sono
 * ignorati, la memoria la protegge solo la paginazione. Ne restano due cose:
 * il LIVELLO (ring 0 o ring 3, i due bit bassi del selettore) e il bit L, che
 * dice «codice a 64 bit». La tabella e' quindi corta:
 *
 *     0x00  nullo
 *     0x08  codice del kernel      0x10  dati del kernel
 *     0x18  dati dei programmi     0x20  codice dei programmi
 *     0x28  il TSS (occupa DUE posti: a 64 bit il suo indirizzo e' di 8 byte)
 *
 * ! I DATI DEI PROGRAMMI VENGONO PRIMA DEL LORO CODICE, e non e' un gusto: e'
 * l'ordine che l'istruzione SYSRET pretende. Oggi le chiamate di sistema
 * entrano con int 0x80 come a 32 bit, ma metterli nell'altro ordine vorrebbe
 * dire rifare la tabella il giorno che si vorra' SYSCALL.
 *
 * ! IL TSS A 64 BIT NON TIENE PIU' I REGISTRI DI NESSUNO. Tiene delle PILE:
 * rsp0, dove il processore atterra quando un interrupt arriva mentre gira un
 * programma, e sette pile di riserva (IST). La prima di quelle e' per il
 * doppio errore: quando la pila del kernel e' rotta, l'eccezione che lo
 * racconta deve avere una pila sua o ne nasce una terza, e la macchina si
 * riavvia senza dire niente.
 * ============================================================================= */
#include "k64.h"
#include "smp.h"
#include "kmalloc.h"     /* the tables of the extra processors (0.319) */

typedef struct PACKED {
    uint32_t riservato0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t riservato1;
    uint64_t ist[7];
    uint64_t riservato2;
    uint16_t riservato3;
    uint16_t mappa_io;              /* oltre la fine: nessuna porta ai programmi */
} Tss64;

/* One of each per processor: see gdt_installa_cpu. Slot 7 (selector 0x38)
 * holds the processor's number in its limit, for cpu_n() in smp.h. */
/* ! AS MANY AS THERE ARE PROCESSORS (0.319, voce 92): up to 0.318 sixteen
 * static copies. The boot processor's block is static (loaded before the
 * heap exists), the others come from gdt_posti once smp_init has counted. */
typedef struct {
    uint64_t gdt[8];
    Tss64    tss;
    uint8_t  pila_doppio[8192] __attribute__((aligned(16)));   /* double fault */
} Gdt64Cpu;

static Gdt64Cpu   g_avvio __attribute__((aligned(16)));
static Gdt64Cpu  *g_cpu_1[1] = { &g_avvio };
static Gdt64Cpu **g_cpu = g_cpu_1;
static uint32_t   g_quanti = 1;
#define g_tss_di(n) (g_cpu[n]->tss)

int gdt_posti(uint32_t posti)
{
    Gdt64Cpu **nuovi;
    uint32_t   i;

    if (posti <= g_quanti) return 0;
    nuovi = (Gdt64Cpu **)kmalloc(posti * sizeof(Gdt64Cpu *));
    if (nuovi == NULL) return -1;
    nuovi[0] = &g_avvio;
    for (i = 1; i < posti; i++) {
        nuovi[i] = (Gdt64Cpu *)kmalloc_aligned(sizeof(Gdt64Cpu), 64);
        if (nuovi[i] == NULL) {
            while (--i >= 1) kfree_aligned(nuovi[i]);
            kfree(nuovi);
            return -1;
        }
        __builtin_memset(nuovi[i], 0, sizeof(Gdt64Cpu));
    }
    g_cpu    = nuovi;
    g_quanti = posti;
    return 0;
}

volatile uint32_t g_smp_lavora = 0;     /* 1 = several processors run programs */

typedef struct PACKED { uint16_t limite; uint64_t base; } Punt64;

void gdt64_pila_kernel(uint64_t rsp0) { g_tss_di(cpu_n()).rsp0 = rsp0; }

static void gdt64_carica(uint32_t n)
{
    Punt64    p;
    Gdt64Cpu *c   = g_cpu[n];
    Tss64    *tss = &c->tss;
    uint64_t *gdt = c->gdt;
    uint64_t  base = (uint64_t)tss, lim = sizeof(*tss) - 1;

    __builtin_memset(tss, 0, sizeof(*tss));
    tss->ist[0]   = (uint64_t)(c->pila_doppio + sizeof(c->pila_doppio));
    tss->mappa_io = sizeof(*tss);

    gdt[0] = 0;
    gdt[1] = 0x00209A0000000000ull;       /* codice kernel: presente, ring 0, L */
    gdt[2] = 0x0000920000000000ull;       /* dati kernel */
    gdt[3] = 0x0000F20000000000ull;       /* dati programmi: ring 3 */
    gdt[4] = 0x0020FA0000000000ull;       /* codice programmi: ring 3, L */
    /* il TSS: tipo 0x9 (TSS a 64 bit libero), in due parole */
    gdt[5] = (lim & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ull << 40) |
               (((lim >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    gdt[6] = base >> 32;

    gdt[7] = 0x0000920000000000ull | (uint64_t)n;     /* who am I: cpu_n() */

    p.limite = sizeof(c->gdt) - 1;
    p.base   = (uint64_t)gdt;
    __asm__ volatile("lgdt %0" : : "m"(p));

    /* I registri di segmento tengono una copia di cio' che c'era nella
     * tabella di prima: vanno ricaricati. CS non si scrive con una mov: ci
     * si arriva con un ritorno lontano. */
    __asm__ volatile(
        /* FS e GS restano NULLI, per sempre: la base di GS (la memoria
         * locale del filo) si scrive da un registro del processore, e un
         * selettore caricato dopo la azzererebbe. Vedi cambio64.asm. */
        "mov %0, %%ds\n mov %0, %%es\n mov %0, %%ss\n xor %%eax, %%eax\n mov %%ax, %%fs\n mov %%ax, %%gs\n"
        "pushq %1\n lea 1f(%%rip), %%rax\n pushq %%rax\n lretq\n 1:\n"
        : : "r"((uint16_t)SEL_K_DATI), "i"(SEL_K_CODICE) : "rax", "memory");
    __asm__ volatile("ltr %0" : : "r"((uint16_t)SEL_TSS));
}

void gdt64_installa(void) { gdt64_carica(0); }

/* -----------------------------------------------------------------------------
 * L'interfaccia di gdt.h, la stessa del kernel a 32 bit
 * --------------------------------------------------------------------------- */
#include "gdt.h"

void gdt_install(void) { gdt64_installa(); }

/* Dove atterra il processore arrivando da ring 3. Le pile del kernel stanno
 * nella fascia (sotto i 64 MB): 32 bit bastano, e il prototipo e' comune. */
void gdt_set_kernel_stack(vaddr_t stack_top) { g_tss_di(cpu_n()).rsp0 = stack_top; }

/* The GDT and TSS of processor `n`, loaded on whoever calls. Every extra
 * processor calls it before it starts working. */
void gdt_installa_cpu(uint32_t n)
{
    if (n != 0 && n < g_quanti) gdt64_carica(n);
}

/* La base della memoria locale del filo. A 32 bit e' un descrittore della GDT
 * raggiunto da GS; in long mode i descrittori non hanno base, e la base di FS
 * e di GS si scrive in un registro del processore (MSR).
 * ! A 64 BIT E' FS, NON GS: e' la convenzione di x86-64, quella per cui GCC (e
 * Rust, e ogni libreria portata) compila le variabili __thread come %fs:...
 * La libc legge il puntatore del filo da %fs:0 (TP_LEGGI in lib/libc.c).
 * Lo scheduler la riscrive a ogni cambio di processo. */
void gdt_set_tls_base(uint32_t base)
{
    __asm__ volatile("wrmsr" : : "c"(0xC0000100u), "a"(base), "d"(0u));
}
