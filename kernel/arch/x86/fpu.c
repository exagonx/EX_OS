/* =============================================================================
 * kernel/arch/x86/fpu.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Coprocessore matematico x87: rilevamento, azzeramento e salvataggio
 * dello stato. Il perche' di tutto questo sta in kernel/include/fpu.h.
 * ============================================================================= */

#include "fpu.h"
#include "kernel.h"

/* Bit di CR0 che riguardano il coprocessore. Gli altri (PG, PE...) li
 * tocca chi di dovere: qui si legge, si cambiano SOLO questi e si
 * riscrive, perche' CR0 e' un registro condiviso e sovrascriverlo per
 * intero spegnerebbe la paginazione. */
#define CR0_MP  (1u << 1)   /* Monitor coProcessor: WAIT rispetta TS */
#define CR0_EM  (1u << 2)   /* EMulation: le istruzioni x87 danno #NM */
#define CR0_TS  (1u << 3)   /* Task Switched: usato dallo switch pigro */
#define CR0_NE  (1u << 5)   /* Numeric Error: eccezioni via #MF, non IRQ13 */

static int      g_fpu_present = 0;
static uint8_t  g_stato_pulito[FPU_STATE_SIZE] ALIGNED(16);

/* =============================================================================
 * fpu_init — rileva la FPU, la azzera, prepara il modello di stato pulito
 *
 * IL RILEVAMENTO. Si azzera EM (altrimenti ogni istruzione x87 diventa
 * un'eccezione #NM: e' il modo di dire "coprocessore emulato via
 * software", che qui non abbiamo) e si esegue FNINIT seguita da FNSTSW su
 * una variabile precaricata con un valore riconoscibile. Se un
 * coprocessore c'e', FNSTSW ci scrive la parola di stato, che dopo FNINIT
 * vale zero; se non c'e', nessuno scrive e il valore riconoscibile resta.
 *
 * E' la prova classica, quella di Linux delle origini, e funziona perche'
 * FNINIT/FNSTSW sono le uniche due istruzioni x87 che NON aspettano il
 * coprocessore (la N sta per "no wait"): su una macchina senza 387 non
 * restano appese.
 *
 * NE=1 dice alla CPU di segnalare gli errori di virgola mobile come
 * eccezione #MF invece che con l'IRQ13 del PC originale. #MF ha gia' un
 * gate nell'IDT (vedi idt.c), l'IRQ13 arriverebbe a un handler che non
 * c'e'.
 * ============================================================================= */
/* =============================================================================
 * Che CPU abbiamo sotto
 *
 * ⚠️ TUTTO A TEMPO DI ESECUZIONE, MAI A TEMPO DI COMPILAZIONE. Lo stesso
 * kernel.bin deve partire su un 486 senza CPUID e su una macchina con
 * SSE3: una #ifdef qui produrrebbe due kernel diversi, e quello con SSE
 * morirebbe alla prima istruzione sul 486.
 *
 * ⚠️ L'ORDINE DELLE DOMANDE E' OBBLIGATO. Non si puo' chiedere a CPUID se
 * CPUID esiste (lo si scopre col bit ID di EFLAGS), e non si puo'
 * chiedere la foglia 1 senza aver prima chiesto alla foglia 0 fin dove si
 * puo' arrivare: su una CPU che si ferma a 0, leggere la 1 restituisce i
 * valori di un'altra foglia, cioe' numeri plausibili e falsi.
 * ============================================================================= */
#define CPUID_EDX_CMOV  (1u << 15)
#define CPUID_EDX_MMX   (1u << 23)
#define CPUID_EDX_FXSR  (1u << 24)
#define CPUID_EDX_SSE   (1u << 25)
#define CPUID_EDX_SSE2  (1u << 26)
#define CPUID_ECX_SSE3  (1u << 0)

#define CR4_OSFXSR      (1u << 9)
#define CR4_OSXMMEXCPT  (1u << 10)

static CpuCapacita g_cpu;
static int         g_sse_attivo = 0;

static void rileva_cpu(void)
{
    uint32_t edx, ecx;

    g_cpu.cpuid = cpuid_disponibile() ? 1 : 0;
    if (!g_cpu.cpuid) return;       /* 386 o 486 primo: niente altro da sapere */

    if (cpuid_max() < 1u) return;   /* CPUID c'e' ma non arriva alla foglia 1 */

    edx = cpuid_edx1();
    ecx = cpuid_ecx1();

    g_cpu.cmov = (edx & CPUID_EDX_CMOV) ? 1 : 0;
    g_cpu.mmx  = (edx & CPUID_EDX_MMX)  ? 1 : 0;
    g_cpu.fxsr = (edx & CPUID_EDX_FXSR) ? 1 : 0;
    g_cpu.sse  = (edx & CPUID_EDX_SSE)  ? 1 : 0;
    g_cpu.sse2 = (edx & CPUID_EDX_SSE2) ? 1 : 0;
    g_cpu.sse3 = (ecx & CPUID_ECX_SSE3) ? 1 : 0;
}

int fpu_init(void)
{
    uint32_t cr0 = read_cr0();


    cr0 &= ~(CR0_EM | CR0_TS);
    cr0 |=  (CR0_MP | CR0_NE);
    write_cr0(cr0);

    rileva_cpu();

    uint16_t stato = 0x55AA;    /* valore riconoscibile: nessuna FPU lo scrive */
    __asm__ __volatile__("fninit; fnstsw %0" : "=m"(stato));

    if (stato != 0) {
        g_fpu_present = 0;
        klog(LOG_WARN, "FPU: nessun coprocessore (fnstsw=0x%04x)", stato);
        /* Senza coprocessore le istruzioni x87 vanno fatte fallire in
         * modo riconoscibile invece di produrre numeri inventati: EM=1
         * le trasforma in #NM. */
        write_cr0(read_cr0() | CR0_EM);
        return 0;
    }

    g_fpu_present = 1;

    /* =====================================================================
     * SSE, se la CPU ce l'ha
     *
     * ⚠️ DUE BIT, E SERVONO ENTRAMBI.
     *
     *   CR4.OSFXSR (9)     dice alla CPU che il sistema operativo sa
     *                      salvare lo stato con FXSAVE. Senza, ogni
     *                      istruzione SSE e' #UD — ed e' esattamente il
     *                      guasto con cui cc1 moriva dentro
     *                      search_line_sse2() di libcpp.
     *   CR4.OSXMMEXCPT(10) dice che sappiamo gestire l'eccezione #XF.
     *                      Senza, un errore SIMD non mascherato arriva
     *                      come #UD, cioe' come "istruzione inesistente"
     *                      su un'istruzione che esiste benissimo: la
     *                      diagnosi peggiore possibile.
     *
     * ⚠️ E SERVE FXSR, NON SOLO SSE. Sono due bit distinti di CPUID
     * proprio perche' esistono CPU con l'uno e non con l'altro. Accendere
     * OSFXSR senza FXSAVE vorrebbe dire promettere alla CPU un
     * salvataggio che non sappiamo fare.
     * ===================================================================== */
    if (g_cpu.fxsr && g_cpu.sse) {
        uint32_t cr4 = read_cr4();

        write_cr4(cr4 | CR4_OSFXSR | CR4_OSXMMEXCPT);
        g_sse_attivo = 1;
    }

    /* Il modello: la FPU e' appena stata azzerata da FNINIT, quindi lo
     * stato che salviamo ORA e' per definizione quello pulito. */
    if (g_sse_attivo) {
        /* ⚠️ MXCSR VA MESSA A MANO. FNINIT azzera l'x87 ma non tocca
         * MXCSR, che dopo il reset vale gia' 0x1F80 — tutte le eccezioni
         * SIMD mascherate. Il punto e' che FXRSTOR RIFIUTA con #GP un'area
         * il cui MXCSR abbia bit riservati accesi: se il modello pulito
         * uscisse da memoria mai scritta, ogni processo nuovo morirebbe al
         * primo cambio di contesto. Si scrive il valore buono e poi lo si
         * salva insieme al resto. */
        uint32_t mxcsr = 0x1F80u;

        __asm__ __volatile__("ldmxcsr %0" : : "m"(mxcsr));
        __asm__ __volatile__("fxsave (%0)" : : "r"(g_stato_pulito) : "memory");
    } else {
        __asm__ __volatile__("fnsave (%0)" : : "r"(g_stato_pulito) : "memory");
        /* FNSAVE azzera la FPU dopo aver salvato: rimettiamola come
         * l'abbiamo trovata, cosi' il kernel non lascia il coprocessore in
         * uno stato diverso da quello che il modello descrive. */
        __asm__ __volatile__("frstor (%0)" : : "r"(g_stato_pulito));
    }

    klog(LOG_INFO, "FPU: x87 presente, salvataggio con %s (%u byte per processo)",
         g_sse_attivo ? "FXSAVE" : "FNSAVE",
         (unsigned)(g_sse_attivo ? 512 : 108));
    klog(LOG_INFO, "CPU: %s%s%s%s%s%s",
         g_cpu.cpuid ? "cpuid " : "nessun cpuid (386/486 primo) ",
         g_cpu.mmx  ? "mmx "  : "",
         g_cpu.sse  ? "sse "  : "",
         g_cpu.sse2 ? "sse2 " : "",
         g_cpu.sse3 ? "sse3 " : "",
         g_cpu.cmov ? "cmov"  : "");

    if (g_cpu.sse && !g_sse_attivo)
        klog(LOG_WARN, "CPU: SSE presente ma NON acceso (manca FXSAVE)");

    return 1;
}

int fpu_present(void)
{
    return g_fpu_present;
}

/* Copia a byte come nel resto del kernel, che una memcpy non ce l'ha:
 * la libc sta in ring3, e questi 108 byte non giustificano una libreria. */
void fpu_init_state(void *dst)
{
    if (!dst) return;

    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = g_stato_pulito;

    for (uint32_t i = 0; i < FPU_STATE_SIZE; i++) {
        /* Senza coprocessore lo stato non verra' mai ne' salvato ne'
         * ripristinato, ma azzerarlo lo stesso evita che un dump del PCB
         * mostri byte casuali. */
        d[i] = g_fpu_present ? s[i] : 0;
    }
}

/* ⚠️ LE DUE VIE NON SONO INTERCAMBIABILI. FNSAVE scrive 108 byte in un
 * formato, FXSAVE 512 in un altro: ripristinare con l'una cio' che ha
 * salvato l'altra non da' un errore, da' registri con dentro pezzi di
 * intestazione. La scelta e' fatta una volta all'avvio e non cambia mai,
 * quindi non c'e' modo che un processo salvi in un formato e venga
 * ripristinato nell'altro.
 *
 * ⚠️ FXSAVE PRETENDE L'ALLINEAMENTO A 16. Il campo fpu_state del PCB e'
 * dichiarato ALIGNED(16) apposta (vedi kernel/include/sched.h): un
 * indirizzo disallineato qui non e' lento, e' una #GP dentro il cambio di
 * contesto — cioe' un kernel che muore mentre cambia processo. */
void fpu_save(void *dst)
{
    if (!g_fpu_present || !dst) return;

    if (g_sse_attivo) __asm__ __volatile__("fxsave (%0)"  : : "r"(dst) : "memory");
    else              __asm__ __volatile__("fnsave (%0)"  : : "r"(dst) : "memory");
}

void fpu_restore(const void *src)
{
    if (!g_fpu_present || !src) return;

    if (g_sse_attivo) __asm__ __volatile__("fxrstor (%0)" : : "r"(src));
    else              __asm__ __volatile__("frstor (%0)"  : : "r"(src));
}

int sse_attivo(void)
{
    return g_sse_attivo;
}

const CpuCapacita *cpu_capacita(void)
{
    return &g_cpu;
}
