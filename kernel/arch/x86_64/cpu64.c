/* =============================================================================
 * kernel/arch/x86_64/cpu64.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * LE PRIMITIVE DEL PROCESSORE A 64 BIT (@EXOS-64, tappa 3d — 10 ottobre 2026)
 *
 * Le porte, i registri di controllo, CPUID, i flag: nel kernel a 32 bit sono
 * una ventina di funzioncine in fondo a kernel/arch/x86/entry.asm, scritte in
 * assembly perche' leggono gli argomenti dalla pila. Qui la convenzione di
 * chiamata e' un'altra (gli argomenti arrivano nei registri) e riscriverle a
 * mano in assembly vorrebbe dire rifare a memoria il lavoro del compilatore:
 * sono in C, una riga di assembly ciascuna. I prototipi sono gli stessi, in
 * kernel.h - chi le chiama non cambia.
 *
 * ! I REGISTRI DI CONTROLLO SONO DI 64 BIT E I PROTOTIPI NE RENDONO 32. Per
 * CR0 e CR4 la meta' alta e' riservata e a zero; per CR3 vale finche' le
 * tabelle delle pagine stanno sotto i 4 GB, ed e' cosi' per costruzione
 * (nascono nella fascia kernel). CR2 no: e' un indirizzo qualunque, e
 * read_cr2 rende un vaddr_t.
 * ============================================================================= */
#include "kernel.h"

uint8_t port_inb(uint16_t port)
{
    uint8_t v;

    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void port_outb(uint16_t port, uint8_t val)
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint16_t port_inw(uint16_t port)
{
    uint16_t v;

    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void port_outw(uint16_t port, uint16_t val)
{
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

uint32_t port_inl(uint16_t port)
{
    uint32_t v;

    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

void port_outl(uint16_t port, uint32_t val)
{
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

void port_insw(uint16_t port, void *dst, uint32_t n_word)
{
    uint64_t n = n_word;

    __asm__ volatile("cld; rep insw" : "+D"(dst), "+c"(n) : "d"(port) : "memory");
}

void port_outsw(uint16_t port, const void *src, uint32_t n_word)
{
    uint64_t n = n_word;

    __asm__ volatile("cld; rep outsw" : "+S"(src), "+c"(n) : "d"(port) : "memory");
}

/* La scrittura sulla porta 0x80 (i codici POST): non fa niente, e ci mette
 * circa un microsecondo. E' l'attesa che le periferiche ISA chiedono. */
void io_delay(void)
{
    __asm__ volatile("outb %al, $0x80");
}

uint32_t read_cr0(void)
{
    uint64_t v;

    __asm__ volatile("mov %%cr0, %0" : "=r"(v));
    return (uint32_t)v;
}

void write_cr0(uint32_t val)
{
    uint64_t v = val;

    __asm__ volatile("mov %0, %%cr0" : : "r"(v));
}

vaddr_t read_cr2(void)
{
    uint64_t v;

    __asm__ volatile("mov %%cr2, %0" : "=r"(v));
    return v;
}

uint32_t read_cr3(void)
{
    uint64_t v;

    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return (uint32_t)v;
}

void write_cr3(uint32_t val)
{
    uint64_t v = val;

    __asm__ volatile("mov %0, %%cr3" : : "r"(v) : "memory");
}

uint32_t read_cr4(void)
{
    uint64_t v;

    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return (uint32_t)v;
}

void write_cr4(uint32_t val)
{
    uint64_t v = val;

    __asm__ volatile("mov %0, %%cr4" : : "r"(v));
}

void interrupts_enable(void)  { __asm__ volatile("sti"); }
void interrupts_disable(void) { __asm__ volatile("cli"); }

uint32_t read_eflags(void)
{
    uint64_t f;

    __asm__ volatile("pushfq; pop %0" : "=r"(f));
    return (uint32_t)f;
}

/* Un processore a 64 bit ha CPUID per forza: la domanda ha senso solo sui 486
 * del kernel a 32 bit. */
int cpuid_disponibile(void) { return 1; }

void cpuid(uint32_t code, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    uint32_t a, b, c = 0, d;

    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "+c"(c), "=d"(d) : "a"(code));
    if (eax) *eax = a;
    if (ebx) *ebx = b;
    if (ecx) *ecx = c;
    if (edx) *edx = d;
}

uint32_t cpuid_max(void)
{
    uint32_t a;

    cpuid(0, &a, NULL, NULL, NULL);
    return a;
}

uint32_t cpuid_edx1(void)
{
    uint32_t d;

    cpuid(1, NULL, NULL, NULL, &d);
    return d;
}

uint32_t cpuid_ecx1(void)
{
    uint32_t c;

    cpuid(1, NULL, NULL, &c, NULL);
    return c;
}
