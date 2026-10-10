/* =============================================================================
 * kernel/arch/x86_64/smp64.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * PIU' PROCESSORI A 64 BIT: NON ANCORA (@EXOS-64, tappa 3d — 10 ottobre 2026)
 *
 * ! A 64 BIT EX-OS NASCE CON UN PROCESSORE SOLO. Svegliare gli altri vuole un
 * trampolino che li porti da 16 bit fino in long mode (a 32 bit si ferma al
 * modo protetto: kernel/arch/x86/smp_tramp.asm), una GDT e una TSS per
 * ciascuno, e il lucchetto del kernel provato su questa architettura. E' il
 * lavoro di kernel/arch/x86/smp.c rifatto, e viene dopo il primo programma.
 *
 * Fino ad allora questo file risponde a smp.h dicendo la verita': c'e' un
 * processore, quello d'avvio. Il lucchetto del kernel non serve - con un
 * processore solo nel kernel ce n'e' uno per forza - e le sue quattro
 * funzioni non fanno niente, come quelle a 32 bit quando «smp» e' spento.
 * ============================================================================= */
#include "kernel.h"
#include "smp.h"

volatile uint32_t g_smp_lavora = 0;

static SmpInfo g_smp;

void smp_init(int accendi)
{
    uint32_t a = 0, d = 0;

    (void)accendi;
    g_smp.n = 1;
    g_smp.cpu[0].stato = SMP_CPU_AVVIO;
    cpuid(1, &a, NULL, NULL, &d);
    g_smp.cpu[0].firma    = a;
    g_smp.cpu[0].capacita = d;
    klog(LOG_INFO, "SMP: a 64 bit un processore solo, per ora (EXOS64-DAFARE)");
}

const SmpInfo *smp_info(void)
{
    extern volatile uint32_t g_ticks;

    g_smp.tick = g_ticks;
    return &g_smp;
}

void smp_chiama_tutti(void)     { }
int  smp_accendi_lavoro(void)   { return 0; }

void bkl_entra(void)            { }
void bkl_esci(const void *f)    { (void)f; }
void bkl_lascia(void)           { }
void bkl_ozio(void)             { }
