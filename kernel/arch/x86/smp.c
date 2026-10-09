/* =============================================================================
 * kernel/arch/x86/smp.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Piu' processori — tappe 1 e 2 (7 ottobre 2026): trovarli, accenderli, e
 * dare a ciascuno il suo APIC locale con un timer e i messaggi dagli altri.
 * Che cosa fa e che cosa NON fa ogni tappa sta in smp.h.
 *
 * -----------------------------------------------------------------------------
 * DOVE STA SCRITTO QUANTI PROCESSORI CI SONO
 *
 * In due posti, di due epoche:
 *
 *   ACPI, tabella MADT    ogni macchina dal 2000 in poi. Si arriva dal «RSD
 *                         PTR » nel BIOS, poi RSDT, poi la tabella «APIC». E'
 *                         l'unica che elenca anche i processori logici
 *                         (HyperThreading) e i nuclei.
 *   tabella MP 1.4        le schede a due e quattro processori degli anni
 *                         Novanta: doppio Pentium, doppio Pentium Pro, doppio
 *                         Pentium II. Si arriva dal «_MP_».
 *
 * Si guarda prima l'ACPI perche' dove ci sono tutte e due e' quella tenuta
 * aggiornata, e la tabella MP di una macchina moderna, quando c'e', elenca un
 * processore per zoccolo e basta.
 *
 * ! LE TABELLE SI LEGGONO ATTRAVERSO fis_copia(), MAI CON UN PUNTATORE. Quelle
 * dell'ACPI stanno in cima alla RAM, fuori da cio' che il kernel ha mappato
 * per identita': dereferenziare l'indirizzo sarebbe un page fault in ring 0.
 *
 * -----------------------------------------------------------------------------
 * COME SI SVEGLIA UN PROCESSORE
 *
 * E' l'algoritmo dell'appendice B.4 della MP Specification, lo stesso che usano
 * tutti: un INIT attraverso l'APIC locale, poi — sugli APIC integrati, cioe'
 * dal Pentium in poi — due messaggi di avvio (SIPI) col numero di pagina da cui
 * cominciare. Per gli APIC esterni 82489DX, che il SIPI non lo conoscono, si
 * prepara anche la strada vecchia: il codice 0x0A nel byte di spegnimento del
 * CMOS e l'indirizzo nel vettore di riavvio a caldo (0x467), cosi' il BIOS del
 * processore appena inizializzato salta li'.
 *
 * Si sveglia UN processore alla volta: il trampolino ha una sola casella per
 * la pila, e un processore che non risponde non deve confondersi con quello
 * dopo.
 * ============================================================================= */

#include "kernel.h"
#include "smp.h"
#include "idt.h"
#include "paging.h"
#include "pmm.h"
#include "sched.h"      /* g_ticks */
#include "gdt.h"        /* gdt_installa_cpu: la tappa 3 */

/* ! UGUALE A SMP_TRAMP_BASE di smp_tramp.asm. E' la pagina 0x8000-0x8FFF, che
 * pmm_init tiene fuori dal giro perche' ci ha lavorato Stage 2: qui la si
 * prende in prestito e la si rimette com'era. */
#define TRAMP_BASE          0x8000u

#define APIC_ID             0x020
#define APIC_VERSIONE       0x030
#define APIC_FATTO          0x0B0   /* conferma di fine interrupt (EOI) */
#define APIC_SPURIO         0x0F0
#define APIC_LVT_TIMER      0x320
#define APIC_TIMER_INIZIO   0x380
#define APIC_TIMER_ADESSO   0x390
#define APIC_TIMER_DIVIDE   0x3E0
#define APIC_ERRORI         0x280
#define APIC_ICR_BASSO      0x300
#define APIC_ICR_ALTO       0x310

#define APIC_SPURIO_ACCESO  0x100u
#define VETTORE_SPURIO      0xFFu
#define VETTORE_BATTITO     0xF0u   /* il timer dell'APIC, sui processori in piu' */
#define VETTORE_MESSAGGIO   0xF1u   /* un messaggio da un altro processore */

#define LVT_MASCHERATO      0x00010000u
#define LVT_PERIODICO       0x00020000u
#define TIMER_DIVIDE_16     0x3u

#define ICR_INIT            0x00000500u
#define ICR_AVVIO           0x00000600u
#define ICR_LIVELLO         0x00008000u
#define ICR_ALZATO          0x00004000u
#define ICR_IN_VOLO         0x00001000u

#define MSR_APIC_BASE       0x1Bu
#define APIC_BASE_ACCESO    (1u << 11)
#define APIC_BASE_X2        (1u << 10)

#define APIC_PREDEFINITO    0xFEE00000u

extern uint8_t  smp_tramp_inizio[], smp_tramp_fine[];
extern uint8_t  smp_tramp_gdt[], smp_tramp_idt[];
extern uint32_t smp_tramp_cr0, smp_tramp_cr3, smp_tramp_cr4;
extern uint32_t smp_tramp_esp, smp_tramp_entra;
extern void     smp_spurio(void);
extern void     smp_isr_battito(void);
extern void     smp_isr_messaggio(void);

static SmpInfo           g_smp;
static volatile uint32_t g_apic;            /* indirizzo dell'APIC locale mappato */
static volatile uint32_t g_ap_risposto;     /* lo alza il processore svegliato */
static SmpCpu           *g_ap_voce;         /* la voce di quello in corso d'avvio */
static uint8_t           g_voce_di[256];    /* numero dell'APIC -> indice in g_smp.cpu */
static uint32_t          g_mp_avvio = 0xFFFFFFFFu;  /* APIC del processore d'avvio secondo la tabella MP */

const SmpInfo *smp_info(void) { g_smp.tick = g_ticks; return &g_smp; }

/* -----------------------------------------------------------------------------
 * Leggere la memoria fisica
 * ----------------------------------------------------------------------------- */

/* Copia `n` byte dall'indirizzo FISICO `fis`. Sotto il megabyte (BIOS, EBDA)
 * la memoria e' mappata per identita' e si legge; sopra si passa dalla
 * finestra, una pagina alla volta. */
static void fis_copia(uint32_t fis, void *dove, uint32_t n)
{
    uint8_t *d = (uint8_t *)dove;

    while (n > 0) {
        uint32_t in_pagina = PAGE_SIZE - (fis & (PAGE_SIZE - 1));
        uint32_t quanti    = n < in_pagina ? n : in_pagina, i;

        if (fis + quanti <= 0x100000u) {
            const uint8_t *s = (const uint8_t *)fis;
            for (i = 0; i < quanti; i++) d[i] = s[i];
        } else {
            const uint8_t *s = (const uint8_t *)paging_finestra_apri(fis);
            for (i = 0; i < quanti; i++) d[i] = s[i];
            paging_finestra_chiudi();
        }
        d += quanti; fis += quanti; n -= quanti;
    }
}

static uint8_t fis_somma(uint32_t fis, uint32_t n)
{
    uint8_t buf[64], somma = 0;

    while (n > 0) {
        uint32_t quanti = n < sizeof(buf) ? n : sizeof(buf), i;

        fis_copia(fis, buf, quanti);
        for (i = 0; i < quanti; i++) somma = (uint8_t)(somma + buf[i]);
        fis += quanti; n -= quanti;
    }
    return somma;
}

/* Un indirizzo della memoria bassa (dati del BIOS). Passa da una variabile
 * volatile perche' al compilatore un puntatore fatto con un numero piccolo
 * sembra un accesso fuori da ogni oggetto, e lo dice a ogni riga. */
static void *bassa(uint32_t indirizzo)
{
    volatile uint32_t a = indirizzo;

    return (void *)a;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int uguali(const uint8_t *a, const char *b, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n; i++) if (a[i] != (uint8_t)b[i]) return 0;
    return 1;
}

/* Cerca una firma ogni 16 byte in [da, a). Rende l'indirizzo o 0. */
static uint32_t cerca_firma(uint32_t da, uint32_t a, const char *firma, uint32_t lung)
{
    uint32_t p;

    if (da == 0 || a > 0x100000u || da >= a) return 0;
    for (p = da & ~15u; p + 16 <= a; p += 16)
        if (uguali((const uint8_t *)p, firma, lung)) return p;
    return 0;
}

/* I tre posti dove il BIOS puo' aver lasciato una tabella: il primo kilobyte
 * dell'EBDA, l'ultimo della memoria base, la ROM. `valida` scarta le firme
 * capitate per caso. */
static uint32_t cerca_nel_bios(const char *firma, uint32_t lung, uint32_t rom_da,
                               int (*valida)(uint32_t))
{
    uint32_t ebda = (uint32_t)(*(const uint16_t *)bassa(0x40E)) << 4;
    uint32_t base = (uint32_t)(*(const uint16_t *)bassa(0x413)) * 1024u;
    uint32_t zone[3][2], i;

    zone[0][0] = (ebda >= 0x80000u && ebda < 0xA0000u) ? ebda : 0;
    zone[0][1] = zone[0][0] + 1024;
    zone[1][0] = (base >= 0x80000u && base <= 0xA0000u) ? base - 1024 : 0;
    zone[1][1] = zone[1][0] + 1024;
    zone[2][0] = rom_da;
    zone[2][1] = 0x100000u;

    for (i = 0; i < 3; i++) {
        uint32_t da = zone[i][0];

        while (da != 0) {
            uint32_t p = cerca_firma(da, zone[i][1], firma, lung);
            if (p == 0) break;
            if (valida(p)) return p;
            da = p + 16;
        }
    }
    return 0;
}

/* -----------------------------------------------------------------------------
 * L'elenco
 * ----------------------------------------------------------------------------- */

static void aggiungi_cpu(uint8_t apic_id, uint8_t ver, uint32_t firma, uint32_t capacita)
{
    uint32_t i;

    for (i = 0; i < g_smp.n; i++)
        if (g_smp.cpu[i].apic_id == apic_id) return;    /* elencato due volte */
    if (g_smp.n >= SMP_CPU_MAX) { g_smp.troppi++; return; }

    g_smp.cpu[g_smp.n].apic_id  = apic_id;
    g_smp.cpu[g_smp.n].stato    = SMP_CPU_LASCIATO;
    g_smp.cpu[g_smp.n].apic_ver = ver;
    g_smp.cpu[g_smp.n].firma    = firma;
    g_smp.cpu[g_smp.n].capacita = capacita;
    g_smp.n++;
}

static void aggiungi_ioapic(uint32_t indirizzo)
{
    if (g_smp.n_ioapic == 0) g_smp.ioapic = indirizzo;
    g_smp.n_ioapic++;
}

static void elenco_vuoto(void)
{
    g_smp.n = 0; g_smp.troppi = 0; g_smp.n_ioapic = 0; g_smp.ioapic = 0;
    g_smp.apic_locale = 0;
}

/* --- ACPI -------------------------------------------------------------------- */

static int rsdp_valido(uint32_t p)
{
    return fis_somma(p, 20) == 0;
}

static int da_acpi(void)
{
    uint8_t  t[44];
    uint32_t rsdp, rsdt, lung, n, i, madt = 0, madt_lung = 0, p;

    rsdp = cerca_nel_bios("RSD PTR ", 8, 0xE0000u, rsdp_valido);
    if (rsdp == 0) return 0;

    rsdt = le32((const uint8_t *)rsdp + 16);
    if (rsdt == 0) return 0;
    fis_copia(rsdt, t, 36);
    lung = le32(t + 4);
    if (!uguali(t, "RSDT", 4) || lung < 36 || lung > 0x4000u) return 0;
    if (fis_somma(rsdt, lung) != 0) {
        klog(LOG_WARN, "SMP: la RSDT dell'ACPI ha la somma di controllo sbagliata");
        return 0;
    }

    n = (lung - 36) / 4;
    for (i = 0; i < n && madt == 0; i++) {
        uint8_t  v[4];
        uint32_t tab;

        fis_copia(rsdt + 36 + i * 4, v, 4);
        tab = le32(v);
        if (tab == 0) continue;
        fis_copia(tab, t, 8);
        if (uguali(t, "APIC", 4)) { madt = tab; madt_lung = le32(t + 4); }
    }
    if (madt == 0 || madt_lung < 44 || madt_lung > 0x10000u) return 0;

    elenco_vuoto();
    fis_copia(madt, t, 44);
    g_smp.apic_locale = le32(t + 36);

    for (p = 44; p + 2 <= madt_lung; ) {
        uint8_t  v[16];
        uint32_t quanti;

        fis_copia(madt + p, v, 2);
        if (v[1] < 2 || p + v[1] > madt_lung) break;
        quanti = v[1] < sizeof(v) ? v[1] : sizeof(v);
        fis_copia(madt + p, v, quanti);

        if (v[0] == 0 && quanti >= 8) {             /* APIC locale di un processore */
            if (le32(v + 4) & 1u) aggiungi_cpu(v[3], 0, 0, 0);
        } else if (v[0] == 1 && quanti >= 8) {      /* I/O APIC */
            aggiungi_ioapic(le32(v + 4));
        } else if (v[0] == 5 && quanti >= 12) {     /* l'APIC locale sta altrove */
            if (le32(v + 8) == 0) g_smp.apic_locale = le32(v + 4);
        }
        p += v[1];
    }

    if (g_smp.n == 0) return 0;
    g_smp.fonte = SMP_FONTE_ACPI;
    return 1;
}

/* --- tabella MP -------------------------------------------------------------- */

static int mp_valido(uint32_t p)
{
    const uint8_t *f = (const uint8_t *)p;

    return f[8] == 1 && fis_somma(p, 16) == 0;
}

static int da_mp(void)
{
    const uint8_t *f;
    uint8_t  t[44];
    uint32_t fp, tab, lung, voci, p, i;

    fp = cerca_nel_bios("_MP_", 4, 0xF0000u, mp_valido);
    if (fp == 0) return 0;
    f = (const uint8_t *)fp;

    elenco_vuoto();

    /* Byte 11 diverso da zero: niente tabella, vale una delle configurazioni
     * predefinite della specifica. Sono tutte «due processori, APIC 0 e 1». */
    if (f[11] != 0) {
        g_smp.apic_locale = APIC_PREDEFINITO;
        aggiungi_cpu(0, 0, 0, 0);
        aggiungi_cpu(1, 0, 0, 0);
        aggiungi_ioapic(0xFEC00000u);
        g_smp.fonte = SMP_FONTE_MP_FISSA;
        return 1;
    }

    tab = le32(f + 4);
    if (tab == 0) return 0;
    fis_copia(tab, t, 44);
    lung = (uint32_t)t[4] | ((uint32_t)t[5] << 8);
    voci = (uint32_t)t[34] | ((uint32_t)t[35] << 8);
    if (!uguali(t, "PCMP", 4) || lung < 44) return 0;
    if (fis_somma(tab, lung) != 0) {
        klog(LOG_WARN, "SMP: la tabella MP ha la somma di controllo sbagliata");
        return 0;
    }
    g_smp.apic_locale = le32(t + 36);

    for (p = 44, i = 0; i < voci && p < lung; i++) {
        uint8_t v[20];

        fis_copia(tab + p, v, 1);
        if (v[0] == 0) {                            /* processore: 20 byte */
            if (p + 20 > lung) break;
            fis_copia(tab + p, v, 20);
            if (v[3] & 1u) aggiungi_cpu(v[1], v[2], le32(v + 4), le32(v + 8));
            if ((v[3] & 3u) == 3u) g_mp_avvio = v[1];
            p += 20;
        } else if (v[0] <= 4) {                     /* bus, I/O APIC, interrupt: 8 */
            if (p + 8 > lung) break;
            fis_copia(tab + p, v, 8);
            if (v[0] == 2 && (v[3] & 1u)) aggiungi_ioapic(le32(v + 4));
            p += 8;
        } else {
            break;                                  /* voce che non conosciamo */
        }
    }

    if (g_smp.n == 0) return 0;
    g_smp.fonte = SMP_FONTE_MP;
    return 1;
}

/* -----------------------------------------------------------------------------
 * L'APIC locale
 * ----------------------------------------------------------------------------- */

static uint32_t apic_leggi(uint32_t reg)
{
    return *(volatile uint32_t *)(g_apic + reg);
}

static void apic_scrivi(uint32_t reg, uint32_t valore)
{
    *(volatile uint32_t *)(g_apic + reg) = valore;
}

static void aspetta_tick(uint32_t n)
{
    uint32_t da = g_ticks;

    while (g_ticks - da < n) __asm__ volatile ("pause");
}

/* Aspetta che l'APIC abbia spedito il messaggio. 0 se e' partito. */
static int icr_spedito(void)
{
    uint32_t da = g_ticks;

    while (apic_leggi(APIC_ICR_BASSO) & ICR_IN_VOLO) {
        if (g_ticks - da >= 3) return -1;
        __asm__ volatile ("pause");
    }
    return 0;
}

static void cmos_scrivi(uint8_t reg, uint8_t valore)
{
    port_outb(0x70, reg);
    io_delay();
    port_outb(0x71, valore);
    io_delay();
}

/* -----------------------------------------------------------------------------
 * I due interrupt dei processori in piu' (da smp_tramp.asm)
 *
 * ! SCRIVONO SOLO NELLA PROPRIA VOCE E NEL PROPRIO APIC. Niente klog, niente
 * scheduler, niente g_ticks: quelle sono cose del processore d'avvio, e senza
 * un lucchetto sul kernel (tappa 3) due processori dentro lo stesso codice
 * sono un guasto che si vede una volta su mille.
 *
 * Chi sono lo dice l'APIC locale: il registro dell'identita' e' diverso su
 * ogni processore pur stando allo stesso indirizzo.
 * ----------------------------------------------------------------------------- */
static SmpCpu *io_sono(void)
{
    return &g_smp.cpu[g_voce_di[apic_leggi(APIC_ID) >> 24]];
}

void smp_ap_battito(void)
{
    io_sono()->battiti++;
    apic_scrivi(APIC_FATTO, 0);
}

void smp_ap_messaggio(void)
{
    io_sono()->messaggi++;
    apic_scrivi(APIC_FATTO, 0);
}

/* =============================================================================
 * Il lucchetto del kernel (tappa 3): la regola sta in smp.h
 *
 * ! SI PRENDE E SI LASCIA A INTERRUPT SPENTI. Fra «ho il lucchetto» e «ho
 * scritto che ce l'ho» non deve passare un interrupt: il suo gestore
 * leggerebbe «non ce l'ho», proverebbe a prenderlo e aspetterebbe se stesso
 * per sempre. Lo stesso lasciandolo, a rovescio. La chiamata di sistema entra
 * a interrupt ACCESI (e' una porta di tipo trappola), quindi qui si spengono
 * e si rimettono come erano.
 * ========================================================================== */
/* ! E' UN LUCCHETTO A NUMERI, COME DAL FORNAIO, E NON «CHI ARRIVA PRIMO».
 * Con un lucchetto semplice (prova a prenderlo, se e' occupato riprova) vince
 * chi riprova piu' in fretta: un programma che entra e esce dal kernel in
 * continuazione - una shell che aspetta un figlio - lo riprendeva sempre un
 * attimo dopo averlo lasciato, e l'altro processore, fermo ad aspettarlo al
 * suo battito con gli interrupt spenti, restava fuori per decine di
 * millisecondi. Misurato con smpprova: un programma solo andava a un decimo.
 * Qui chi arriva prende un numero e si serve in ordine: chi lascia e
 * rientra va in coda dietro chi stava aspettando. */
static volatile uint32_t g_bkl_coda  = 0;   /* il prossimo numero da dare */
static volatile uint32_t g_bkl_turno = 0;   /* il numero che si sta servendo */
static volatile uint8_t  g_bkl_tiene[SMP_CPU_MAX];

void bkl_entra(void)
{
    uint32_t n, eflags;

    if (!g_smp_lavora) return;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags));
    n = cpu_n();
    if (!g_bkl_tiene[n]) {
        uint32_t mio = __sync_fetch_and_add(&g_bkl_coda, 1);

        while (g_bkl_turno != mio) __asm__ volatile ("pause");
        g_bkl_tiene[n] = 1;
    }
    if (eflags & 0x200u) __asm__ volatile ("sti");
}

void bkl_lascia(void)
{
    uint32_t n, eflags;

    if (!g_smp_lavora) return;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(eflags));
    n = cpu_n();
    if (g_bkl_tiene[n]) {
        g_bkl_tiene[n] = 0;
        g_bkl_turno++;                  /* lo scrive solo chi ce l'ha */
    }
    if (eflags & 0x200u) __asm__ volatile ("sti");
}

void bkl_ozio(void) { bkl_lascia(); }

/* All'uscita da ogni gestore, a interrupt spenti (lo stub ha fatto cli e
 * restano spenti fino a iret). Se si torna in ring 3: prima si guarda se
 * qualcuno ha chiesto a questo processo di finire, poi si lascia il kernel. */
void bkl_esci(const void *frame)
{
    const InterruptFrame *f = (const InterruptFrame *)frame;

    if (!g_smp_lavora || (f->cs & 3) != 3) return;
    sched_fine_chiesta();           /* se c'e' da finire, non torna */
    bkl_lascia();
}

/* Quanti processori in piu' eseguono processi, e l'ordine di cominciare dato
 * a uno che era parcheggiato (smp_accendi_lavoro): il suo numero e la pila
 * del suo ozio, per voce di g_smp.cpu. */
static uint32_t          g_lavorano = 0;
static volatile uint32_t g_ordine_n[SMP_CPU_MAX], g_ordine_pila[SMP_CPU_MAX];
extern void smp_ap_salta(uint32_t pila, uint32_t funzione);

/* Timer e messaggi di un processore in piu', dagli stub di isr_stubs.asm:
 * possono arrivare mentre gira un programma, quindi col telaio intero e il
 * lucchetto gia' preso. */
void smp_ap_irq(InterruptFrame *f)
{
    if (f->int_no == VETTORE_BATTITO) {
        io_sono()->battiti++;
        /* ! «FATTO» PRIMA DI SCEGLIERE: se lo scheduler cambia processo non
         * si torna qui per un pezzo, e senza questo il timer non batterebbe
         * piu' su questo processore. */
        apic_scrivi(APIC_FATTO, 0);
        /* Solo se questo processore il kernel ce l'ha davvero: un battito
         * arrivato un attimo prima che il lucchetto si accendesse e' entrato
         * senza prenderlo, e non deve toccare lo scheduler. */
        if (g_smp_lavora && g_bkl_tiene[cpu_n()]) sched_battito_cpu();
    } else {
        SmpCpu  *io = io_sono();
        uint32_t i  = (uint32_t)(io - g_smp.cpu);

        io->messaggi++;
        apic_scrivi(APIC_FATTO, 0);

        /* L'ordine di cominciare a lavorare, a un processore parcheggiato:
         * la sua GDT, la FPU in ordine, e via sulla pila del suo ozio. Il
         * telaio di questo interrupt si abbandona: non si torna. */
        if (g_ordine_n[i] != 0 && io->stato == SMP_CPU_FERMO) {
            gdt_installa_cpu(g_ordine_n[i]);
            __asm__ volatile ("fninit");
            io->stato = SMP_CPU_LAVORA;
            smp_ap_salta(g_ordine_pila[i], (uint32_t)sched_cpu_entra);
        }
    }
}

/* -----------------------------------------------------------------------------
 * Il processore svegliato arriva qui, dal trampolino
 *
 * Scrive chi e' nella propria voce, accende il proprio APIC, fa partire il
 * proprio timer a 100 Hz (se quello d'avvio e' riuscito a misurarne la
 * velocita') e resta in attesa: `hlt` a interrupt ACCESI, da cui lo svegliano
 * il suo timer e i messaggi degli altri.
 *
 * ! IL TIMER PARTE DOPO «CI SONO»: il processore d'avvio aspetta quella
 * risposta per passare al successivo, e la casella della pila nel trampolino
 * e' una sola.
 * ----------------------------------------------------------------------------- */
/* Tappa 3: il numero (1..) e la pila d'ozio del processore che si sta
 * svegliando; 0 = lo si parcheggia come alle tappe 1 e 2. */
static volatile uint32_t g_ap_lavora_n = 0, g_ap_lavora_pila = 0;
extern void smp_irq_battito(void);
extern void smp_irq_messaggio(void);

static void smp_ap_entra(void)
{
    uint32_t a = 0, b = 0, c = 0, d = 0;

    cpuid(1, &a, &b, &c, &d);
    g_ap_voce->firma    = a;
    g_ap_voce->capacita = d;

    apic_scrivi(APIC_SPURIO, APIC_SPURIO_ACCESO | VETTORE_SPURIO);
    g_ap_voce->apic_ver = (uint8_t)apic_leggi(APIC_VERSIONE);
    if (g_smp.timer_per_tick != 0) {
        apic_scrivi(APIC_TIMER_DIVIDE, TIMER_DIVIDE_16);
        apic_scrivi(APIC_LVT_TIMER, VETTORE_BATTITO | LVT_PERIODICO);
        apic_scrivi(APIC_TIMER_INIZIO, g_smp.timer_per_tick);
    }
    if (g_ap_lavora_n != 0) {
        /* Tappa 3: questo processore esegue processi. La sua GDT e il suo TSS,
         * la FPU in ordine, e da qui e' il suo compito d'ozio, sulla pila di
         * quello (smp_ap_salta non torna). «Ci sono» si dice dopo aver
         * caricato la GDT: da quel momento cpu_n() dice il vero. */
        uint32_t n = g_ap_lavora_n, pila = g_ap_lavora_pila;

        gdt_installa_cpu(n);
        __asm__ volatile ("fninit");
        g_ap_voce->stato = SMP_CPU_LAVORA;
        g_ap_risposto = 1;
        smp_ap_salta(pila, (uint32_t)sched_cpu_entra);
    }
    g_ap_risposto = 1;

    for (;;) __asm__ volatile ("sti; hlt");
}

/* Quanti conteggi fa il timer dell'APIC in un tick del PIT (10 ms), col
 * divisore a 16. Il timer dell'APIC conta col clock del bus, che cambia da
 * scheda a scheda: va misurato. Si misura su questo processore e vale per gli
 * altri, che stanno sullo stesso bus. Il timer di QUESTO processore dopo
 * resta spento: lui ha il PIT. */
static uint32_t misura_timer(void)
{
    uint32_t da, fatti;

    apic_scrivi(APIC_TIMER_DIVIDE, TIMER_DIVIDE_16);
    apic_scrivi(APIC_LVT_TIMER, LVT_MASCHERATO | VETTORE_BATTITO);

    for (da = g_ticks; g_ticks == da; ) __asm__ volatile ("pause");   /* un bordo */
    apic_scrivi(APIC_TIMER_INIZIO, 0xFFFFFFFFu);
    for (da = g_ticks; g_ticks - da < 5; ) __asm__ volatile ("pause");
    fatti = 0xFFFFFFFFu - apic_leggi(APIC_TIMER_ADESSO);
    apic_scrivi(APIC_TIMER_INIZIO, 0);

    return fatti / 5;
}

/* Un messaggio a ogni processore in attesa. Le due scritture nel registro
 * (destinatario, poi comando) non devono essere separate da un altro che fa
 * la stessa cosa: su un processore solo basta spegnere gli interrupt. */
void smp_chiama_tutti(void)
{
    uint32_t i, flag;

    if (g_apic == 0) return;
    for (i = 0; i < g_smp.n; i++) {
        if (g_smp.cpu[i].stato != SMP_CPU_FERMO) continue;
        flag = read_eflags();
        interrupts_disable();
        apic_scrivi(APIC_ICR_ALTO, (uint32_t)g_smp.cpu[i].apic_id << 24);
        apic_scrivi(APIC_ICR_BASSO, VETTORE_MESSAGGIO);
        {   /* a giri e non a tick: qui gli interrupt sono spenti */
            uint32_t giri = 100000;
            while ((apic_leggi(APIC_ICR_BASSO) & ICR_IN_VOLO) && --giri)
                __asm__ volatile ("pause");
        }
        if (flag & 0x200u) interrupts_enable();
    }
}

/* Sveglia il processore `c`. 1 se ha risposto. */
static int sveglia(SmpCpu *c, uint32_t pila)
{
    uint32_t id = (uint32_t)c->apic_id << 24, da;
    int      integrato = (apic_leggi(APIC_VERSIONE) & 0xF0u) != 0;
    int      giro;

    g_ap_voce     = c;
    g_ap_risposto = 0;
    *(uint32_t *)(TRAMP_BASE + ((uint8_t *)&smp_tramp_esp - smp_tramp_inizio)) =
        pila + PAGE_SIZE;

    if (integrato) { apic_scrivi(APIC_ERRORI, 0); (void)apic_leggi(APIC_ERRORI); }

    apic_scrivi(APIC_ICR_ALTO, id);
    apic_scrivi(APIC_ICR_BASSO, ICR_INIT | ICR_LIVELLO | ICR_ALZATO);
    (void)icr_spedito();
    aspetta_tick(1);
    apic_scrivi(APIC_ICR_ALTO, id);
    apic_scrivi(APIC_ICR_BASSO, ICR_INIT | ICR_LIVELLO);
    (void)icr_spedito();
    aspetta_tick(2);                    /* la specifica chiede 10 ms */

    for (giro = 0; integrato && giro < 2 && !g_ap_risposto; giro++) {
        apic_scrivi(APIC_ERRORI, 0); (void)apic_leggi(APIC_ERRORI);
        apic_scrivi(APIC_ICR_ALTO, id);
        apic_scrivi(APIC_ICR_BASSO, ICR_AVVIO | (TRAMP_BASE >> 12));
        (void)icr_spedito();
        aspetta_tick(2);                /* ne bastano 200 microsecondi */
    }

    for (da = g_ticks; !g_ap_risposto && g_ticks - da < 50; )
        __asm__ volatile ("pause");

    return g_ap_risposto != 0;
}

/* Prepara la copia del trampolino sotto il megabyte. */
static void trampolino_prepara(void)
{
    uint32_t lung = (uint32_t)(smp_tramp_fine - smp_tramp_inizio), i;
    uint8_t *dove = (uint8_t *)TRAMP_BASE;
#define NELLA_COPIA(x) (TRAMP_BASE + (uint32_t)((uint8_t *)(x) - smp_tramp_inizio))

    for (i = 0; i < lung; i++) dove[i] = smp_tramp_inizio[i];

    __asm__ volatile ("sgdt (%0)" : : "r"(NELLA_COPIA(smp_tramp_gdt)) : "memory");
    __asm__ volatile ("sidt (%0)" : : "r"(NELLA_COPIA(smp_tramp_idt)) : "memory");
    *(uint32_t *)NELLA_COPIA(&smp_tramp_cr0)   = read_cr0();
    *(uint32_t *)NELLA_COPIA(&smp_tramp_cr3)   = (uint32_t)paging_get_kernel_directory();
    *(uint32_t *)NELLA_COPIA(&smp_tramp_cr4)   = read_cr4();
    *(uint32_t *)NELLA_COPIA(&smp_tramp_entra) = (uint32_t)smp_ap_entra;
#undef NELLA_COPIA
}

static const char *nome_fonte(uint32_t f)
{
    switch (f) {
    case SMP_FONTE_ACPI:     return "ACPI (MADT)";
    case SMP_FONTE_MP:       return "tabella MP";
    case SMP_FONTE_MP_FISSA: return "configurazione MP predefinita";
    default:                 return "nessuna tabella";
    }
}

/* =============================================================================
 * smp_init
 * ============================================================================= */
/* =============================================================================
 * smp_accendi_lavoro — i processori parcheggiati cominciano a lavorare, ADESSO
 *
 * ! ESISTE PER PROVARE SENZA RISCHIARE L'AVVIO. `smp = 2` in kernel.cfg li
 * mette al lavoro dall'accensione: se su una scheda vera qualcosa non andasse,
 * la macchina non arriverebbe al prompt, e per togliere la riga servirebbe un
 * altro disco da cui partire. Da qui invece si parte normali, si da' il
 * comando (`smpprova -accendi`), e se la macchina si ferma basta riavviarla:
 * torna com'era. E' la stessa strada del comando `ahci` prima di `ahci = 1`.
 *
 * La chiama una chiamata di sistema, quindi il processore d'avvio, dentro il
 * kernel. Rende quanti processori in piu' lavorano dopo (0 = nessuno).
 * ========================================================================== */
int smp_accendi_lavoro(void)
{
    uint32_t i, da, nuovi = 0;

    if (g_smp_lavora) return (int)g_lavorano;
    if (g_apic == 0 || g_smp.timer_per_tick == 0) return 0;

    for (i = 0; i < g_smp.n && i < SMP_CPU_MAX; i++) {
        uint32_t pila;

        if (g_smp.cpu[i].stato != SMP_CPU_FERMO || g_ordine_n[i] != 0) continue;
        if (g_lavorano + 1 >= SMP_CPU_MAX) break;
        pila = sched_cpu_prepara(g_lavorano + 1);
        if (pila == 0) break;
        g_ordine_pila[i] = pila;
        g_ordine_n[i]    = g_lavorano + 1;
        g_lavorano++;
        nuovi++;
    }
    if (nuovi == 0) return 0;

    smp_chiama_tutti();
    for (da = g_ticks; g_ticks - da < 100; ) {          /* un secondo al piu' */
        uint32_t pronti = 0;

        for (i = 0; i < g_smp.n && i < SMP_CPU_MAX; i++)
            if (g_ordine_n[i] != 0 && g_smp.cpu[i].stato == SMP_CPU_LAVORA) pronti++;
        if (pronti >= nuovi) break;
        __asm__ volatile ("pause");
    }

    /* Come in fondo a smp_init: il kernel e' gia' «preso» da chi scrive. */
    interrupts_disable();
    g_bkl_turno    = 0;
    g_bkl_coda     = 1;
    g_bkl_tiene[0] = 1;
    g_smp_lavora   = 1;
    interrupts_enable();

    klog(LOG_INFO, "SMP: %u processori in piu' messi al lavoro a macchina avviata", g_lavorano);
    return (int)g_lavorano;
}

void smp_init(int accendi)
{
    static uint8_t salvata[PAGE_SIZE];      /* la pagina del trampolino, com'era */
    uint32_t a = 0, b = 0, c = 0, d = 0, i, famiglia, mio;
    uint8_t  cmos_prima;
    uint32_t vettore_prima;

    /* Il processore su cui siamo: c'e' sempre, anche senza tabelle. */
    g_smp.n = 1;
    g_smp.cpu[0].stato = SMP_CPU_AVVIO;

    /* Senza CPUID (386, 486 dei primi) o senza APIC locale non c'e' modo di
     * parlare a un altro processore, e nessuna scheda del genere ne ha due. */
    if (!cpuid_disponibile() || cpuid_max() < 1) return;
    cpuid(1, &a, &b, &c, &d);
    g_smp.cpu[0].firma    = a;
    g_smp.cpu[0].capacita = d;
    if (!(d & (1u << 9))) return;
    famiglia = (a >> 8) & 0xF;

    if (!da_acpi() && !da_mp()) {
        g_smp.n = 1;
        g_smp.fonte = SMP_FONTE_NESSUNA;
        g_smp.cpu[0].apic_id  = (uint8_t)(b >> 24);
        g_smp.cpu[0].stato    = SMP_CPU_AVVIO;
        g_smp.cpu[0].firma    = a;
        g_smp.cpu[0].capacita = d;
        return;
    }
    if (g_smp.apic_locale == 0) g_smp.apic_locale = APIC_PREDEFINITO;

    /* Quale delle voci siamo noi. Lo dice l'APIC, che si legge piu' sotto se
     * si arriva a mapparlo. Senza: la tabella MP segna il processore d'avvio;
     * CPUID porta il numero dell'APIC solo dal Pentium 4 (sul Pentium Pro quel
     * byte e' zero, ed e' per questo che viene per ultimo). */
    mio = (g_smp.fonte == SMP_FONTE_MP && g_mp_avvio != 0xFFFFFFFFu) ? g_mp_avvio
                                                                     : (b >> 24);

    /* Le attese qui sotto contano i tick: a interrupt spenti non finirebbero
     * mai. Non succede (vedi dove kernel_main chiama smp_init), ma un avvio
     * che si ferma in silenzio e' il guasto peggiore che questo file possa
     * dare, e il controllo costa una riga. */
    if (!(read_eflags() & 0x200u)) accendi = 0;

    if (g_smp.n >= 2 && accendi) {
        /* Il registro che dice se l'APIC e' acceso, e come, esiste dal
         * Pentium Pro: sul Pentium leggerlo e' un'eccezione. */
        if (famiglia >= 6 && (d & (1u << 5))) {
            uint32_t basso, alto;

            __asm__ volatile ("rdmsr" : "=a"(basso), "=d"(alto) : "c"(MSR_APIC_BASE));
            (void)alto;
            if (basso & APIC_BASE_X2)           g_smp.motivo = SMP_MOTIVO_X2APIC;
            else if (!(basso & APIC_BASE_ACCESO)) g_smp.motivo = SMP_MOTIVO_SPENTO;
            else g_smp.apic_locale = basso & 0xFFFFF000u;
        }
    } else if (g_smp.n >= 2) {
        g_smp.motivo = SMP_MOTIVO_CFG;
    }

    /* ! L'APIC SI MAPPA SOLO SE CI SONO PROCESSORI DA SVEGLIARE, nella sua
     * pagina fissa: e' in una tabella che ogni spazio di indirizzamento
     * condivide, quindi da qui in poi lo si raggiunge da qualunque processo e
     * da ogni processore. Vedi PAGING_APIC_VIRT in paging.h. */
    if (g_smp.n >= 2 && accendi && g_smp.motivo == SMP_MOTIVO_NESSUNO) {
        g_apic = (uint32_t)paging_mappa_apic(g_smp.apic_locale);
        mio    = apic_leggi(APIC_ID) >> 24;
    }

    for (i = 0; i < g_smp.n; i++) {
        if (g_smp.cpu[i].apic_id == mio) {
            g_smp.cpu[i].stato    = SMP_CPU_AVVIO;
            g_smp.cpu[i].firma    = a;
            g_smp.cpu[i].capacita = d;
            if (g_apic) g_smp.cpu[i].apic_ver = (uint8_t)apic_leggi(APIC_VERSIONE);
        }
    }

    if (g_apic == 0) {
        if (g_smp.n >= 2)
            klog(LOG_INFO, "SMP: %u processori (%s), quelli in piu' non svegliati",
                 g_smp.n, nome_fonte(g_smp.fonte));
        return;
    }

    /* --- si svegliano ------------------------------------------------------ */

    /* L'APIC di questo processore deve essere acceso per spedire. Il vettore
     * degli interrupt spuri punta a un `iret`: non ne devono arrivare, ma se
     * ne arrivasse uno senza voce nella IDT sarebbe un doppio fault. */
    idt_set_gate(VETTORE_SPURIO,    (uint32_t)smp_spurio,        0x08, 0x8E);
    /* Gli stub col telaio intero e il lucchetto (isr_stubs.asm): servono da
     * quando un processore in piu' puo' essere in ring 3 all'arrivo del suo
     * timer. Per uno parcheggiato fanno lo stesso lavoro di prima. */
    idt_set_gate(VETTORE_BATTITO,   (uint32_t)smp_irq_battito,   0x08, 0x8E);
    idt_set_gate(VETTORE_MESSAGGIO, (uint32_t)smp_irq_messaggio, 0x08, 0x8E);
    apic_scrivi(APIC_SPURIO, APIC_SPURIO_ACCESO | VETTORE_SPURIO);

    for (i = 0; i < g_smp.n; i++) g_voce_di[g_smp.cpu[i].apic_id] = (uint8_t)i;
    g_smp.timer_per_tick = misura_timer();
    if (g_smp.timer_per_tick == 0)
        klog(LOG_WARN, "SMP: il timer dell'APIC locale non conta: i processori "
                       "in piu' restano senza il loro timer");

    for (i = 0; i < PAGE_SIZE; i++) salvata[i] = ((uint8_t *)TRAMP_BASE)[i];
    trampolino_prepara();

    port_outb(0x70, 0x0F); io_delay();
    cmos_prima    = port_inb(0x71);
    vettore_prima = *(uint32_t *)bassa(0x467);
    cmos_scrivi(0x0F, 0x0A);
    *(uint16_t *)bassa(0x467) = 0;
    *(uint16_t *)bassa(0x469) = (uint16_t)(TRAMP_BASE >> 4);

    for (i = 0; i < g_smp.n; i++) {
        SmpCpu  *cpu = &g_smp.cpu[i];
        uint32_t pila;

        if (cpu->stato == SMP_CPU_AVVIO) continue;
        pila = pmm_alloc_page_kernel();
        if (pila == 0) { g_smp.motivo = SMP_MOTIVO_MEMORIA; break; }

        /* Tappa 3: gli si prepara l'ozio. Se il timer dell'APIC non conta
         * resta parcheggiato: senza battito non avrebbe uno scheduler. */
        g_ap_lavora_n = 0;
        if (accendi >= 2 && g_smp.timer_per_tick != 0 && g_lavorano + 1 < SMP_CPU_MAX) {
            g_ap_lavora_pila = sched_cpu_prepara(g_lavorano + 1);
            if (g_ap_lavora_pila != 0) g_ap_lavora_n = g_lavorano + 1;
        }

        if (sveglia(cpu, pila)) {
            if (g_ap_lavora_n != 0) g_lavorano++;     /* lo stato l'ha scritto lui */
            else cpu->stato = SMP_CPU_FERMO;
            g_smp.fermi++;
            /* La pila resta sua: e' in attesa li' sopra. */
        } else {
            cpu->stato = SMP_CPU_MUTO;
            /* ! LA PILA NON SI RENDE: un processore che non ha risposto in
             * mezzo secondo puo' rispondere dopo, e la userebbe. Una pagina
             * persa per ogni processore muto e' il prezzo giusto. */
            klog(LOG_WARN, "SMP: il processore con APIC %u non ha risposto",
                 cpu->apic_id);
        }
    }

    cmos_scrivi(0x0F, cmos_prima);
    *(uint32_t *)bassa(0x467) = vettore_prima;

    /* ! IL TRAMPOLINO SI TOGLIE SOLO SE HANNO RISPOSTO TUTTI: un processore
     * muto che partisse piu' tardi troverebbe al suo posto i dati di prima, e
     * li eseguirebbe. */
    if (g_smp.fermi + 1 == g_smp.n)
        for (i = 0; i < PAGE_SIZE; i++) ((uint8_t *)TRAMP_BASE)[i] = salvata[i];

    /* ! IL LUCCHETTO SI ACCENDE QUI, CON IL KERNEL GIA' «PRESO» DA CHI SCRIVE:
     * questo processore e' dentro il kernel in questo momento, e lo lascera'
     * come tutti - andando in ring 3 o fermandosi nell'ozio. I processori in
     * piu' da adesso aspettano il loro turno al primo battito. */
    if (g_lavorano > 0) {
        interrupts_disable();
        g_bkl_turno    = 0;             /* il numero 0 e' di chi scrive */
        g_bkl_coda     = 1;
        g_bkl_tiene[0] = 1;
        g_smp_lavora   = 1;
        interrupts_enable();
    }

    klog(LOG_INFO, "SMP: %u processori (%s), %u in piu' accesi, %u dei quali lavorano",
         g_smp.n, nome_fonte(g_smp.fonte), g_smp.fermi, g_lavorano);
}
