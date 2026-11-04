/* =============================================================================
 * drivers/cpufreq/cpufreq.c - /dev/cpufreq.drv, the CPU frequency
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 *     cpufreq.drv               what the processor has and where it is now
 *     cpufreq.drv -auto         the frequency follows the load      (root)
 *     cpufreq.drv -max          always the fastest                   (root)
 *     cpufreq.drv -min          always the slowest                   (root)
 *     cpufreq.drv -fisso:N      always state N (0 = the fastest)     (root)
 *     cpufreq.drv -spento       the kernel lets go: back to the state found
 *     cpufreq.drv -guarda [S]   one line a second for S seconds (10)
 *     cpufreq.drv -install      -auto at every boot (/boot/avvio.sh)
 *     cpufreq.drv -remove       takes that away
 *     cpufreq.drv -i            0 if this processor can be controlled (hwconfig)
 *
 * (voce 78, 4 November 2026.) THE WORK IS IN THE KERNEL, not here:
 * kernel/arch/x86/cpufreq.c finds the P-states and decides at the tick of
 * each processor, which is the only place where both the load and the MSR
 * of THAT processor are at hand. This program reads and sets the mode with
 * SYS_CPUFREQ and ends: there is no process left running, and nothing to
 * restart if it dies.
 *
 * ! IT IS A .drv ALL THE SAME: it is the piece of the system that belongs to
 * a part of the hardware, it lives in /dev with the others, and it has
 * -install / -remove like every driver (avvio_blocco, libc).
 * ============================================================================= */

#include "libc.h"

EX_VERSIONE("cpufreq.drv", "0.001");

static const char *nome_modo(unsigned int m)
{
    switch (m) {
    case CPUFREQ_SPENTO: return "spento (decide il BIOS)";
    case CPUFREQ_AUTO:   return "auto (segue il carico)";
    case CPUFREQ_MAX:    return "max";
    case CPUFREQ_MIN:    return "min";
    case CPUFREQ_FISSO:  return "fisso";
    default:             return "?";
    }
}

static const char *nome_perche(unsigned int p)
{
    switch (p) {
    case CPUFREQ_NO_CPUID:    return "il processore non ha il CPUID esteso";
    case CPUFREQ_NO_SUPPORTO: return "il processore non dichiara P-state, o e' uno che non conosco\n"
                                     "  (AMD prima della famiglia 10h, Intel prima di Sandy Bridge)";
    case CPUFREQ_NO_VIRTUALE: return "macchina virtuale: gli MSR della frequenza non sono veri";
    case CPUFREQ_NO_TSC:      return "il TSC non e' invariante: le scadenze del kernel cambierebbero\n"
                                     "  con la frequenza";
    case CPUFREQ_NO_BIOS:     return "SpeedStep e' spento dal BIOS: si accende dal menu del BIOS";
    case CPUFREQ_NO_HWP:      return "HWP e' acceso: la frequenza la decide il processore da solo";
    case CPUFREQ_NO_STATI:    return "il processore dichiara meno di due P-state";
    default:                  return "motivo sconosciuto";
    }
}

static void mhz(unsigned int v, char *b, int n)
{
    if (v) snprintf(b, (size_t)n, "%u MHz", v);
    else   snprintf(b, (size_t)n, "MHz ignoti");
}

static int leggi(CpuFreqInfo *i)
{
    if (cpufreq_info(i) == 0) return 0;
    if (errno == ENOSYS || errno == EINVAL)
        printf("cpufreq: il kernel non ha SYS_CPUFREQ (serve il kernel 0.311 o piu')\n");
    else
        printf("cpufreq: non riesco a leggere lo stato (errno %d)\n", errno);
    return -1;
}

static int stato(void)
{
    CpuFreqInfo i;
    unsigned int k;
    char b[32];

    if (leggi(&i) < 0) return 1;
    if (i.tipo == CPUFREQ_NESSUNO) {
        printf("cpufreq: nessun controllo della frequenza su questa macchina.\n");
        printf("  %s.\n", nome_perche(i.perche));
        printf("  (famiglia %xh, modello %xh)\n", i.famiglia, i.modello);
        return 0;
    }
    printf("processore: %s, famiglia %xh, modello %xh\n",
           i.tipo == CPUFREQ_AMD_PSTATE ? "AMD, P-state hardware" : "Intel, SpeedStep",
           i.famiglia, i.modello);
    printf("modo:       %s", nome_modo(i.modo));
    if (i.modo == CPUFREQ_FISSO) printf(" sullo stato %u", i.fisso);
    printf("\n");
    if (i.modo == CPUFREQ_AUTO)
        printf("            sopra il %u%% di carico va al massimo, sotto il %u%% scende\n"
               "            di uno stato ogni 100 ms\n", i.su, i.giu);
    printf("stati:\n");
    for (k = 0; k < i.n; k++) {
        mhz(i.mhz[k], b, sizeof(b));
        printf("  %2u  %-10s%s\n", k, b,
               k < i.primo || k > i.ultimo ? "  (non permesso adesso)" : "");
    }
    for (k = 0; k < i.ncpu; k++) {
        if (i.cpu_stato[k] == CPUFREQ_IGNOTO) {
            printf("cpu %u: stato ignoto\n", k);
            continue;
        }
        mhz(i.cpu_stato[k] < i.n ? i.mhz[i.cpu_stato[k]] : 0, b, sizeof(b));
        printf("cpu %u: stato %u (%s), carico %u%%, %u cambi dall'avvio\n",
               k, i.cpu_stato[k], b, i.cpu_carico[k], i.cpu_cambi[k]);
    }
    return 0;
}

static int guarda(unsigned int secondi)
{
    CpuFreqInfo i;
    unsigned int s, k;

    for (s = 0; s < secondi; s++) {
        if (leggi(&i) < 0) return 1;
        if (i.tipo == CPUFREQ_NESSUNO) { printf("cpufreq: nessun controllo (%s)\n", nome_perche(i.perche)); return 0; }
        printf("%3u s", s);
        for (k = 0; k < i.ncpu; k++) {
            unsigned int st = i.cpu_stato[k];
            printf("   cpu %u: %u MHz %3u%%", k,
                   st < i.n ? i.mhz[st] : 0, i.cpu_carico[k]);
        }
        printf("\n");
        sleep(1);
    }
    return 0;
}

static int imposta(unsigned int modo, unsigned int fisso)
{
    CpuFreqInfo i;

    if (getuid() != 0) { printf("cpufreq: cambiare il modo solo da root\n"); return 1; }
    if (cpufreq_modo(modo, fisso) < 0) {
        if (errno == ENODEV) {
            if (leggi(&i) == 0) printf("cpufreq: nessun controllo: %s.\n", nome_perche(i.perche));
            return 0;   /* at boot on a machine without it: not an error */
        }
        if (errno == EINVAL && modo == CPUFREQ_FISSO)
            printf("cpufreq: lo stato %u non c'e' (`cpufreq.drv` li elenca)\n", fisso);
        else
            printf("cpufreq: il kernel dice di no (errno %d)\n", errno);
        return 1;
    }
    printf("cpufreq: %s\n", nome_modo(modo));
    return 0;
}

/* -install / -remove: the block in /boot/avvio.sh. In the foreground: it is
 * one system call and it ends. On a machine without control it says why in
 * one line and goes on. */
static int installa(int togli)
{
    int rc;

    if (getuid() != 0) { printf("cpufreq: -install e -remove solo da root\n"); return 1; }
    rc = avvio_blocco("cpufreq.drv", NULL);
    if (rc >= 0 && !togli)
        rc = avvio_blocco("cpufreq.drv",
                          "# La frequenza del processore segue il carico: piu' bassa,\n"
                          "# e con meno tensione, quando c'e' poco da fare.\n"
                          "# `cpufreq.drv -remove` toglie questo blocco.\n"
                          "cpufreq.drv -auto\n");
    if (rc < 0) { printf("cpufreq: non riesco a scrivere /boot/avvio.sh (errno %d)\n", errno); return 1; }
    printf(togli ? "cpufreq: tolto da /boot/avvio.sh; adesso: `cpufreq.drv -spento`\n"
                 : "cpufreq: in /boot/avvio.sh, dal prossimo avvio la frequenza segue il carico\n");
    return 0;
}

static void aiuto(void)
{
    printf("cpufreq.drv - la frequenza del processore (kernel 0.311)\n\n");
    printf("  cpufreq.drv              cosa ha il processore e dove sta adesso\n");
    printf("  cpufreq.drv -auto        la frequenza segue il carico         (root)\n");
    printf("  cpufreq.drv -max         sempre la piu' alta                  (root)\n");
    printf("  cpufreq.drv -min         sempre la piu' bassa                 (root)\n");
    printf("  cpufreq.drv -fisso:N     sempre lo stato N (0 = il piu' veloce)\n");
    printf("  cpufreq.drv -spento      il kernel lascia stare: torna lo stato trovato\n");
    printf("  cpufreq.drv -guarda [S]  una riga al secondo per S secondi (10)\n");
    printf("  cpufreq.drv -install     -auto a ogni accensione (/boot/avvio.sh)\n");
    printf("  cpufreq.drv -remove      lo toglie\n");
}

int main(int argc, char **argv)
{
    const char *a = argc >= 2 ? argv[1] : "";

    if (argc < 2 || strcmp(a, "-stato") == 0) return stato();
    if (strcmp(a, "-i") == 0) {
        CpuFreqInfo i;
        return cpufreq_info(&i) == 0 && i.tipo != CPUFREQ_NESSUNO ? 0 : 1;
    }
    if (strcmp(a, "-auto") == 0)   return imposta(CPUFREQ_AUTO, 0);
    if (strcmp(a, "-max") == 0)    return imposta(CPUFREQ_MAX, 0);
    if (strcmp(a, "-min") == 0)    return imposta(CPUFREQ_MIN, 0);
    if (strcmp(a, "-spento") == 0) return imposta(CPUFREQ_SPENTO, 0);
    if (strncmp(a, "-fisso:", 7) == 0) {
        const char *p = a + 7;
        unsigned int n = 0;

        if (*p < '0' || *p > '9') { printf("cpufreq: -fisso:N vuole un numero\n"); return 1; }
        while (*p >= '0' && *p <= '9') n = n * 10u + (unsigned int)(*p++ - '0');
        return imposta(CPUFREQ_FISSO, n);
    }
    if (strcmp(a, "-guarda") == 0) {
        int s = argc >= 3 ? atoi(argv[2]) : 10;
        return guarda(s > 0 ? (unsigned int)s : 10u);
    }
    if (strcmp(a, "-install") == 0) return installa(0);
    if (strcmp(a, "-remove") == 0)  return installa(1);
    if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) { aiuto(); return 0; }
    printf("cpufreq: opzione sconosciuta '%s'\n\n", a);
    aiuto();
    return 1;
}
