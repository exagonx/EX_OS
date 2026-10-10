/* =============================================================================
 * kernel/arch/x86_64/k64.h — quel che i file di questa directory si dicono
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * @EXOS-64, tappa 3. Finche' il kernel comune non e' collegato, questi file
 * parlano sulla seriale con le quattro funzioni di main64.c; quando lo sara'
 * useranno klog come tutti gli altri, e queste spariranno.
 * ============================================================================= */
#ifndef K64_H
#define K64_H

#include "kernel.h"
#include "idt.h"
#include "paging.h"

/* main64.c: la voce provvisoria */
void k64_scrivi(const char *s);
void k64_esa(uint64_t v);
void k64_numero(uint64_t v);

/* gdt64.c */
void gdt64_installa(void);
void gdt64_pila_kernel(uint64_t rsp0);          /* dove si atterra arrivando da ring 3 */

#define SEL_K_CODICE  0x08
#define SEL_K_DATI    0x10
#define SEL_U_DATI    0x1B                       /* 0x18 | ring 3 */
#define SEL_U_CODICE  0x23                       /* 0x20 | ring 3 */
#define SEL_TSS       0x28

/* paging64.c: la PTE di un indirizzo in uno spazio, NULL se la tabella non
 * c'e'. Non crea niente. */
PTE     *paging64_pte(PDE *pd, vaddr_t virt);

/* idt64.c */
void     idt64_installa(void);
void     idt64_orologio_avvia(uint32_t hz);
uint64_t idt64_battiti(void);
/* Un gestore per un vettore: 1 = l'ho gestito, si riprende; 0 = non era mio.
 * Senza un gestore, un'eccezione ferma il kernel e dice dove. */
typedef int (*Gestore64)(InterruptFrame *f);
void     idt64_gestore(uint8_t vettore, Gestore64 g);

static inline void    k64_outb(uint16_t porta, uint8_t v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(porta)); }
static inline uint8_t k64_inb(uint16_t porta) { uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(porta)); return v; }

#endif /* K64_H */
