/* =============================================================================
 * kernel/include/cpufreq.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The CPU frequency (voce 78, 4 November 2026): P-states and the governor.
 * The code and the reasons are in kernel/arch/x86/cpufreq.c; the program that
 * shows and sets it is /dev/cpufreq.drv (drivers/cpufreq).
 *
 * ! CpuFreqInfo IS COPIED IN lib/include/libc.h, field by field: kernel and
 * user space do not share headers (the same rule as the syscall numbers).
 * Only uint32_t, so it is the same at 32 and at 64 bit; the size travels
 * with the call and a different one is refused.
 * ============================================================================= */

#ifndef CPUFREQ_H
#define CPUFREQ_H

#include "kernel.h"

#define CPUFREQ_STATI_MAX   16      /* P-states kept (AMD has 8, Intel ratios are sampled) */
#define CPUFREQ_CPU_MAX     16      /* = SMP_CPU_MAX */

/* tipo: how this CPU changes frequency */
#define CPUFREQ_NESSUNO     0
#define CPUFREQ_AMD_PSTATE  1       /* AMD family 10h and later, hardware P-states (MSR C001_006x) */
#define CPUFREQ_INTEL_EIST  2       /* Intel SpeedStep, IA32_PERF_CTL, Sandy Bridge and later */

/* perche: why there is no control (tipo = CPUFREQ_NESSUNO) */
#define CPUFREQ_NO_CPUID        1   /* no extended CPUID */
#define CPUFREQ_NO_SUPPORTO     2   /* the CPU does not say it has P-states, or is not one we know */
#define CPUFREQ_NO_VIRTUALE     3   /* under a hypervisor: the MSRs are not real */
#define CPUFREQ_NO_TSC          4   /* TSC not invariant: the kernel's timeouts would change */
#define CPUFREQ_NO_BIOS         5   /* SpeedStep switched off by the BIOS (IA32_MISC_ENABLE) */
#define CPUFREQ_NO_HWP          6   /* Intel HWP active: the processor decides by itself */
#define CPUFREQ_NO_STATI        7   /* fewer than two usable P-states */

/* modo */
#define CPUFREQ_SPENTO      0       /* the kernel does not touch the frequency (at boot) */
#define CPUFREQ_AUTO        1       /* follows the load */
#define CPUFREQ_MAX         2       /* always the fastest allowed */
#define CPUFREQ_MIN         3       /* always the slowest */
#define CPUFREQ_FISSO       4       /* always state `fisso` */

#define CPUFREQ_IGNOTO      0xFFFFFFFFu

typedef struct {
    uint32_t versione;                      /* 1 */
    uint32_t tipo, perche;
    uint32_t famiglia, modello;             /* family with the extended part added */
    uint32_t n;                             /* P-states in mhz[]: 0 the fastest */
    uint32_t mhz[CPUFREQ_STATI_MAX];        /* 0 = the formula for this family is not known */
    uint32_t primo, ultimo;                 /* the allowed range, now (AMD: CurPstateLimit, PstateMaxVal) */
    uint32_t modo, fisso;
    uint32_t su, giu;                       /* AUTO: load % to go to the top / to step down */
    uint32_t ncpu;                          /* processors in cpu_* below */
    uint32_t cpu_stato[CPUFREQ_CPU_MAX];    /* the state each one is in, or CPUFREQ_IGNOTO */
    uint32_t cpu_carico[CPUFREQ_CPU_MAX];   /* load % of the last 100 ms */
    uint32_t cpu_cambi[CPUFREQ_CPU_MAX];    /* changes of state since boot */
} CpuFreqInfo;

/* SYS_CPUFREQ: ebx = operation */
#define CPUFREQ_OP_INFO     0       /* ecx = CpuFreqInfo*, edx = sizeof */
#define CPUFREQ_OP_MODO     1       /* ecx = modo, edx = fisso (root) */

void cpufreq_init(void);                        /* after smp_init: finds what the CPU has */
void cpufreq_battito(uint32_t cpu, int ozio);   /* every tick, on each processor */
int  cpufreq_info(CpuFreqInfo *dst);
int  cpufreq_modo(uint32_t modo, uint32_t fisso);

#endif /* CPUFREQ_H */
