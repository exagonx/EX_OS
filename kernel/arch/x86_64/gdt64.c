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

typedef struct PACKED {
    uint32_t riservato0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t riservato1;
    uint64_t ist[7];
    uint64_t riservato2;
    uint16_t riservato3;
    uint16_t mappa_io;              /* oltre la fine: nessuna porta ai programmi */
} Tss64;

static Tss64    g_tss;
static uint64_t g_gdt[7];
static uint8_t  g_pila_doppio[8192] __attribute__((aligned(16)));

typedef struct PACKED { uint16_t limite; uint64_t base; } Punt64;

void gdt64_pila_kernel(uint64_t rsp0) { g_tss.rsp0 = rsp0; }

void gdt64_installa(void)
{
    Punt64   p;
    uint64_t base = (uint64_t)&g_tss, lim = sizeof(g_tss) - 1;

    __builtin_memset(&g_tss, 0, sizeof(g_tss));
    g_tss.ist[0]   = (uint64_t)(g_pila_doppio + sizeof(g_pila_doppio));
    g_tss.mappa_io = sizeof(g_tss);

    g_gdt[0] = 0;
    g_gdt[1] = 0x00209A0000000000ull;       /* codice kernel: presente, ring 0, L */
    g_gdt[2] = 0x0000920000000000ull;       /* dati kernel */
    g_gdt[3] = 0x0000F20000000000ull;       /* dati programmi: ring 3 */
    g_gdt[4] = 0x0020FA0000000000ull;       /* codice programmi: ring 3, L */
    /* il TSS: tipo 0x9 (TSS a 64 bit libero), in due parole */
    g_gdt[5] = (lim & 0xFFFF) | ((base & 0xFFFFFF) << 16) | (0x89ull << 40) |
               (((lim >> 16) & 0xF) << 48) | (((base >> 24) & 0xFF) << 56);
    g_gdt[6] = base >> 32;

    p.limite = sizeof(g_gdt) - 1;
    p.base   = (uint64_t)g_gdt;
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

/* -----------------------------------------------------------------------------
 * L'interfaccia di gdt.h, la stessa del kernel a 32 bit
 * --------------------------------------------------------------------------- */
#include "gdt.h"

void gdt_install(void) { gdt64_installa(); }

/* Dove atterra il processore arrivando da ring 3. Le pile del kernel stanno
 * nella fascia (sotto i 64 MB): 32 bit bastano, e il prototipo e' comune. */
void gdt_set_kernel_stack(uint32_t stack_top) { g_tss.rsp0 = stack_top; }

/* Un processore solo, per ora: vedi smp64.c */
void gdt_installa_cpu(uint32_t n) { (void)n; }

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
