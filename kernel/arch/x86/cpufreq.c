/* =============================================================================
 * kernel/arch/x86/cpufreq.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * The CPU frequency: P-states and a governor that follows the load
 * (voce 78, asked on 4 November 2026: "prima la frequenza della CPU").
 *
 * Until kernel 0.310 the only saving was the `hlt` of the idle task: the
 * processor stopped between two ticks, but always at full clock and voltage.
 * Here the kernel lowers the clock (and with it the voltage, which the
 * processor chooses with the P-state) when there is little to do.
 *
 * ! TWO FAMILIES OF PROCESSORS, BOTH WITHOUT ACPI. The universal way is the
 * _PSS object of the ACPI tables, and reading it needs an AML interpreter
 * that EX-OS does not have (acpi.c only finds S5 with a byte search). So the
 * table comes from the processor itself:
 *   - AMD, family 10h and later, with hardware P-states (CPUID 8000_0007
 *     EDX bit 7): MSR C001_0064+i describe each P-state, C001_0061 says which
 *     are allowed now, C001_0062 chooses. The laptop's Athlon QL-64 is
 *     family 11h (Griffin).
 *   - Intel, Sandy Bridge and later, SpeedStep: MSR_PLATFORM_INFO (CEh) gives
 *     the highest and lowest ratio, IA32_PERF_CTL (199h) chooses. Only the
 *     models in the list below: on a Core 2 MSR CEh does not exist and rdmsr
 *     would stop the kernel with a #GP.
 * Everything else (AMD K8 with the old FID/VID PowerNow!, Pentium M, Core 2,
 * Atom) says "non supportato" and is left as the BIOS set it.
 *
 * ! THE TSC MUST BE INVARIANT, OR NOTHING IS TOUCHED. The kernel measures its
 * timeouts with the TSC (blk.c, ahci.c, the guardia in power.c), calibrated
 * once at boot. Where the TSC follows the clock, a machine booted slow and
 * then sped up would see every timeout come too early. CPUID 8000_0007 EDX
 * bit 8 says it does not follow; without it, no control.
 *
 * ! NOTHING UNDER A HYPERVISOR (CPUID 1 ECX bit 31). QEMU, VirtualBox and KVM
 * may pass the CPUID bits of the host and then answer the MSRs with a #GP.
 *
 * ! NO IPI: EACH PROCESSOR CHANGES ITS OWN. The P-state is per core, and the
 * MSR is written by the core it belongs to. cpufreq_battito is called at the
 * tick of every working processor (sched_tick on the boot one, the APIC
 * timer on the others), counts how many of the last ten ticks were idle and
 * decides there, with no message between processors. A parked processor
 * stays as the BIOS left it.
 *
 * ! AT BOOT THE MODE IS SPENTO: the kernel finds and describes, but writes
 * nothing until /dev/cpufreq.drv asks (`cpufreq.drv -auto`, written in
 * avvio.sh by `cpufreq.drv -install`). Going back to SPENTO puts each
 * processor back in the state it was found in.
 * ============================================================================= */

#include "kernel.h"
#include "cpufreq.h"
#include "syscall.h"      /* EINVAL, ENODEV */

/* -----------------------------------------------------------------------------
 * MSR and CPUID
 * --------------------------------------------------------------------------- */
static void msr_leggi(uint32_t msr, uint32_t *basso, uint32_t *alto)
{
    uint32_t a, d;

    __asm__ __volatile__("rdmsr" : "=a"(a), "=d"(d) : "c"(msr));
    *basso = a;
    *alto  = d;
}

static void msr_scrivi(uint32_t msr, uint32_t basso, uint32_t alto)
{
    __asm__ __volatile__("wrmsr" : : "c"(msr), "a"(basso), "d"(alto));
}

static void cpuid_di(uint32_t foglia, uint32_t *eax, uint32_t *ebx,
                     uint32_t *ecx, uint32_t *edx)
{
    uint32_t a, b, c, d;

    __asm__ __volatile__("cpuid"
                         : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                         : "a"(foglia), "c"(0));
    *eax = a; *ebx = b; *ecx = c; *edx = d;
}

#define AMD_PSTATE_LIMITE   0xC0010061u     /* [2:0] CurPstateLimit, [6:4] PstateMaxVal */
#define AMD_PSTATE_COMANDO  0xC0010062u     /* [2:0] PstateCmd */
#define AMD_PSTATE_STATO    0xC0010063u     /* [2:0] CurPstate */
#define AMD_PSTATE_DEF      0xC0010064u     /* +i, bit 63 PstateEn */

#define INTEL_PLATFORM_INFO 0x000000CEu     /* [15:8] max non-turbo ratio, [47:40] min */
#define INTEL_PERF_STATO    0x00000198u     /* [15:8] the ratio now */
#define INTEL_PERF_CTL      0x00000199u     /* [15:8] the ratio asked */
#define INTEL_MISC_ENABLE   0x000001A0u     /* bit 16: SpeedStep on */
#define INTEL_PM_ENABLE     0x00000770u     /* bit 0: HWP on */

/* AUTO: at or above `su` % the processor goes to the top at once; below
 * `giu` % it steps down one state per 100 ms. Up at once and down slowly:
 * a program that starts must not wait, and a pause of a few hundred
 * milliseconds is not a reason to slow down. */
#define SOGLIA_SU       80u
#define SOGLIA_GIU      30u
#define FINESTRA        10u     /* ticks per decision: 100 ms at 100 Hz */

typedef struct {
    uint32_t tick, ozio;        /* in the current window */
    uint32_t stato;             /* index in g_cf.mhz, or CPUFREQ_IGNOTO */
    uint32_t iniziale;          /* the state found, for SPENTO */
    uint32_t carico;
    uint32_t cambi;
    uint32_t visto;             /* this processor has had its first tick */
} CpuStato;

static struct {
    uint32_t tipo, perche, famiglia, modello;
    uint32_t n;
    uint32_t mhz[CPUFREQ_STATI_MAX];
    uint32_t codice[CPUFREQ_STATI_MAX];     /* AMD: P-state number; Intel: ratio */
    uint32_t primo, ultimo;
    volatile uint32_t modo, fisso;
    CpuStato cpu[CPUFREQ_CPU_MAX];
} g_cf;

/* -----------------------------------------------------------------------------
 * AMD
 * --------------------------------------------------------------------------- */

/* The frequency of a P-state, from its definition (BKDG of each family).
 * 0 when the family's formula is not known: the state is still usable. */
static uint32_t amd_mhz(uint32_t famiglia, uint32_t basso)
{
    uint32_t fid, did;

    switch (famiglia) {
    case 0x10: case 0x15: case 0x16:
        fid = basso & 0x3Fu; did = (basso >> 6) & 0x7u;
        return (100u * (fid + 0x10u)) >> did;
    case 0x11:
        fid = basso & 0x3Fu; did = (basso >> 6) & 0x7u;
        return (100u * (fid + 0x08u)) >> did;
    case 0x12: {
        /* divisor 1, 1.5, 2, 3, 4, 6, 8, 12, 16: twice it, to stay integer */
        static const uint8_t div2[9] = { 2, 3, 4, 6, 8, 12, 16, 24, 32 };
        fid = (basso >> 4) & 0x1Fu; did = basso & 0xFu;
        return did < 9 ? 200u * (fid + 0x10u) / div2[did] : 0;
    }
    case 0x17: case 0x18: case 0x19: case 0x1A:
        fid = basso & 0xFFu; did = (basso >> 8) & 0x3Fu;
        return did ? 200u * fid / did : 0;
    default:
        return 0;               /* 14h (Bobcat) has a formula of its own */
    }
}

static void amd_limiti(void)
{
    uint32_t b, a, lim, max;

    msr_leggi(AMD_PSTATE_LIMITE, &b, &a);
    lim = b & 0x7u;
    max = (b >> 4) & 0x7u;
    if (max >= g_cf.n) max = g_cf.n - 1;
    if (lim > max) lim = max;
    g_cf.primo  = lim;
    g_cf.ultimo = max;
}

static int amd_trova(void)
{
    uint32_t b, a, i, max;

    msr_leggi(AMD_PSTATE_LIMITE, &b, &a);
    max = (b >> 4) & 0x7u;
    g_cf.n = 0;
    for (i = 0; i <= max && i < 8; i++) {
        msr_leggi(AMD_PSTATE_DEF + i, &b, &a);
        if (!(a & 0x80000000u)) break;      /* PstateEn: the list ends here */
        g_cf.codice[g_cf.n] = i;
        g_cf.mhz[g_cf.n]    = amd_mhz(g_cf.famiglia, b);
        g_cf.n++;
    }
    if (g_cf.n < 2) return CPUFREQ_NO_STATI;
    amd_limiti();
    return 0;
}

static uint32_t amd_ora(void)
{
    uint32_t b, a;

    msr_leggi(AMD_PSTATE_STATO, &b, &a);
    b &= 0x7u;
    return b < g_cf.n ? b : CPUFREQ_IGNOTO;
}

/* -----------------------------------------------------------------------------
 * Intel
 * --------------------------------------------------------------------------- */

/* Family 6 models with MSR_PLATFORM_INFO, Sandy Bridge to Raptor Lake. */
static int intel_noto(uint32_t modello)
{
    static const uint8_t noti[] = {
        0x2A, 0x2D, 0x3A, 0x3E, 0x3C, 0x3F, 0x45, 0x46, 0x3D, 0x47, 0x4F,
        0x56, 0x4E, 0x5E, 0x55, 0x8E, 0x9E, 0xA5, 0xA6, 0x66, 0x6A, 0x6C,
        0x7D, 0x7E, 0x8C, 0x8D, 0x97, 0x9A, 0xA7, 0xB7, 0xBA, 0xBF, 0xAA,
        0xAC, 0x8F, 0xCF
    };
    uint32_t i;

    for (i = 0; i < sizeof(noti); i++) if (noti[i] == modello) return 1;
    return 0;
}

static int intel_trova(void)
{
    uint32_t b, a, c, d, alto, basso, passi, i;

    msr_leggi(INTEL_MISC_ENABLE, &b, &a);
    if (!(b & (1u << 16))) return CPUFREQ_NO_BIOS;

    cpuid_di(0, &a, &b, &c, &d);
    if (a >= 6) {
        cpuid_di(6, &a, &b, &c, &d);
        if (a & (1u << 7)) {                /* HWP exists: is it on? */
            msr_leggi(INTEL_PM_ENABLE, &b, &a);
            if (b & 1u) return CPUFREQ_NO_HWP;
        }
    }

    msr_leggi(INTEL_PLATFORM_INFO, &b, &a);
    alto  = (b >> 8) & 0xFFu;
    basso = (a >> 8) & 0xFFu;
    if (alto == 0 || basso == 0 || basso >= alto) return CPUFREQ_NO_STATI;

    /* from the highest ratio down to the lowest; more than 16 are sampled */
    passi = alto - basso;
    g_cf.n = passi + 1 <= CPUFREQ_STATI_MAX ? passi + 1 : CPUFREQ_STATI_MAX;
    for (i = 0; i < g_cf.n; i++) {
        uint32_t r = alto - (passi * i) / (g_cf.n - 1);

        g_cf.codice[i] = r;
        g_cf.mhz[i]    = r * 100u;
    }
    g_cf.primo  = 0;
    g_cf.ultimo = g_cf.n - 1;
    return 0;
}

static uint32_t intel_ora(void)
{
    uint32_t b, a, r, i, meglio = CPUFREQ_IGNOTO, dist = 0xFFFFFFFFu;

    msr_leggi(INTEL_PERF_STATO, &b, &a);
    r = (b >> 8) & 0xFFu;
    for (i = 0; i < g_cf.n; i++) {
        uint32_t d = g_cf.codice[i] > r ? g_cf.codice[i] - r : r - g_cf.codice[i];
        if (d < dist) { dist = d; meglio = i; }
    }
    return meglio;
}

/* -----------------------------------------------------------------------------
 * Common
 * --------------------------------------------------------------------------- */
static uint32_t stato_ora(void)
{
    if (g_cf.tipo == CPUFREQ_AMD_PSTATE) return amd_ora();
    if (g_cf.tipo == CPUFREQ_INTEL_EIST) return intel_ora();
    return CPUFREQ_IGNOTO;
}

/* Writes state `i` on THIS processor. */
static void metti(CpuStato *c, uint32_t i)
{
    uint32_t b, a;

    if (i >= g_cf.n || i == c->stato) return;
    if (g_cf.tipo == CPUFREQ_AMD_PSTATE) {
        msr_scrivi(AMD_PSTATE_COMANDO, g_cf.codice[i], 0);
    } else if (g_cf.tipo == CPUFREQ_INTEL_EIST) {
        msr_leggi(INTEL_PERF_CTL, &b, &a);
        b = (b & ~0xFFFFu) | (g_cf.codice[i] << 8);
        msr_scrivi(INTEL_PERF_CTL, b, a);
    } else {
        return;
    }
    c->stato = i;
    c->cambi++;
}

static const char *nome_perche(uint32_t p)
{
    switch (p) {
    case CPUFREQ_NO_CPUID:    return "niente CPUID esteso";
    case CPUFREQ_NO_SUPPORTO: return "P-state non dichiarati o processore che non conosco";
    case CPUFREQ_NO_VIRTUALE: return "macchina virtuale: gli MSR non sono veri";
    case CPUFREQ_NO_TSC:      return "TSC non invariante: le scadenze del kernel cambierebbero";
    case CPUFREQ_NO_BIOS:     return "SpeedStep spento dal BIOS";
    case CPUFREQ_NO_HWP:      return "HWP acceso: decide il processore";
    case CPUFREQ_NO_STATI:    return "meno di due P-state";
    default:                  return "?";
    }
}

void cpufreq_init(void)
{
    uint32_t a, b, c, d, max_est, venditore[3], fam_base;
    int      amd, intel, r;
    uint32_t i;

    g_cf.tipo = CPUFREQ_NESSUNO;
    g_cf.modo = CPUFREQ_SPENTO;
    for (i = 0; i < CPUFREQ_CPU_MAX; i++) {
        g_cf.cpu[i].stato    = CPUFREQ_IGNOTO;
        g_cf.cpu[i].iniziale = CPUFREQ_IGNOTO;
    }

    if (!cpuid_disponibile()) { g_cf.perche = CPUFREQ_NO_CPUID; goto fine; }

    cpuid_di(0, &a, &venditore[0], &venditore[2], &venditore[1]);
    amd   = venditore[0] == 0x68747541u && venditore[1] == 0x69746E65u && venditore[2] == 0x444D4163u;
    intel = venditore[0] == 0x756E6547u && venditore[1] == 0x49656E69u && venditore[2] == 0x6C65746Eu;

    cpuid_di(1, &a, &b, &c, &d);
    fam_base       = (a >> 8) & 0xFu;
    g_cf.famiglia  = fam_base + (fam_base == 0xFu ? ((a >> 20) & 0xFFu) : 0);
    g_cf.modello   = ((a >> 4) & 0xFu) |
                     ((fam_base == 0xFu || fam_base == 0x6u) ? ((a >> 12) & 0xF0u) : 0);
    if (c & (1u << 31)) { g_cf.perche = CPUFREQ_NO_VIRTUALE; goto fine; }

    cpuid_di(0x80000000u, &max_est, &b, &c, &d);
    if (max_est < 0x80000007u) { g_cf.perche = CPUFREQ_NO_CPUID; goto fine; }
    cpuid_di(0x80000007u, &a, &b, &c, &d);

    if (amd && g_cf.famiglia >= 0x10 && (d & (1u << 7))) {
        if (!(d & (1u << 8))) { g_cf.perche = CPUFREQ_NO_TSC; goto fine; }
        r = amd_trova();
        if (r) { g_cf.perche = (uint32_t)r; goto fine; }
        g_cf.tipo = CPUFREQ_AMD_PSTATE;
    } else if (intel && g_cf.famiglia == 6 && intel_noto(g_cf.modello) &&
               (cpuid_ecx1() & (1u << 7))) {
        if (!(d & (1u << 8))) { g_cf.perche = CPUFREQ_NO_TSC; goto fine; }
        r = intel_trova();
        if (r) { g_cf.perche = (uint32_t)r; goto fine; }
        g_cf.tipo = CPUFREQ_INTEL_EIST;
    } else {
        g_cf.perche = CPUFREQ_NO_SUPPORTO;
        goto fine;
    }

    g_cf.perche = 0;
    g_cf.cpu[0].iniziale = g_cf.cpu[0].stato = stato_ora();
    g_cf.cpu[0].visto    = 1;
    klog(LOG_INFO, "CPUFREQ: %s, famiglia %xh modello %xh, %u stati: %u %u %u %u MHz (0 = formula ignota), ora %u",
         g_cf.tipo == CPUFREQ_AMD_PSTATE ? "AMD P-state" : "Intel SpeedStep",
         g_cf.famiglia, g_cf.modello, g_cf.n,
         g_cf.mhz[0], g_cf.mhz[1], g_cf.n > 2 ? g_cf.mhz[2] : 0, g_cf.n > 3 ? g_cf.mhz[3] : 0,
         g_cf.cpu[0].stato);
    return;
fine:
    klog(LOG_INFO, "CPUFREQ: nessun controllo della frequenza (%s), famiglia %xh modello %xh",
         nome_perche(g_cf.perche), g_cf.famiglia, g_cf.modello);
}

/* Every tick (100 Hz) of a working processor, with interrupts off. */
void cpufreq_battito(uint32_t cpu, int ozio)
{
    CpuStato *c;
    uint32_t  modo, voluto;

    if (g_cf.tipo == CPUFREQ_NESSUNO || cpu >= CPUFREQ_CPU_MAX) return;
    c = &g_cf.cpu[cpu];
    if (!c->visto) {                        /* first tick here: what did the BIOS leave? */
        c->iniziale = c->stato = stato_ora();
        c->visto = 1;
    }
    c->tick++;
    if (ozio) c->ozio++;
    if (c->tick < FINESTRA) return;

    c->carico = 100u * (c->tick - c->ozio) / c->tick;
    c->tick = c->ozio = 0;

    modo = g_cf.modo;
    if (modo == CPUFREQ_SPENTO) {
        if (c->iniziale != CPUFREQ_IGNOTO && c->stato != c->iniziale) metti(c, c->iniziale);
        return;
    }
    if (g_cf.tipo == CPUFREQ_AMD_PSTATE) amd_limiti();     /* the limit moves with the heat */

    switch (modo) {
    case CPUFREQ_MAX:   voluto = g_cf.primo;  break;
    case CPUFREQ_MIN:   voluto = g_cf.ultimo; break;
    case CPUFREQ_FISSO: voluto = g_cf.fisso;  break;
    default:
        if (c->stato == CPUFREQ_IGNOTO)        voluto = g_cf.primo;
        else if (c->carico >= SOGLIA_SU)       voluto = g_cf.primo;
        else if (c->carico <  SOGLIA_GIU)      voluto = c->stato + 1;
        else                                   voluto = c->stato;
        break;
    }
    if (voluto < g_cf.primo)  voluto = g_cf.primo;
    if (voluto > g_cf.ultimo) voluto = g_cf.ultimo;
    metti(c, voluto);
}

int cpufreq_info(CpuFreqInfo *dst)
{
    uint32_t i;

    for (i = 0; i < sizeof(*dst); i++) ((uint8_t *)dst)[i] = 0;
    dst->versione = 1;
    dst->tipo     = g_cf.tipo;
    dst->perche   = g_cf.perche;
    dst->famiglia = g_cf.famiglia;
    dst->modello  = g_cf.modello;
    dst->n        = g_cf.n;
    for (i = 0; i < g_cf.n; i++) dst->mhz[i] = g_cf.mhz[i];
    dst->primo  = g_cf.primo;
    dst->ultimo = g_cf.ultimo;
    dst->modo   = g_cf.modo;
    dst->fisso  = g_cf.fisso;
    dst->su     = SOGLIA_SU;
    dst->giu    = SOGLIA_GIU;
    for (i = 0; i < CPUFREQ_CPU_MAX; i++) {
        if (!g_cf.cpu[i].visto) continue;
        dst->ncpu = i + 1;
        dst->cpu_stato[i]  = g_cf.cpu[i].stato;
        dst->cpu_carico[i] = g_cf.cpu[i].carico;
        dst->cpu_cambi[i]  = g_cf.cpu[i].cambi;
    }
    return 0;
}

int cpufreq_modo(uint32_t modo, uint32_t fisso)
{
    if (modo > CPUFREQ_FISSO) return -EINVAL;
    if (g_cf.tipo == CPUFREQ_NESSUNO) return modo == CPUFREQ_SPENTO ? 0 : -ENODEV;
    if (modo == CPUFREQ_FISSO && fisso >= g_cf.n) return -EINVAL;
    g_cf.fisso = fisso;
    g_cf.modo  = modo;
    klog(LOG_INFO, "CPUFREQ: modo %u%s", modo, modo == CPUFREQ_FISSO ? " (stato fisso)" : "");
    return 0;
}
