/* =============================================================================
 * kernel/arch/x86/mtrr.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * MTRR — dire alla CPU che la memoria video si puo' scrivere a raffica
 *
 * -----------------------------------------------------------------------------
 * ! IL PROBLEMA, IN UNA RIGA: SCRIVERE NEL FRAMEBUFFER NON E' SCRIVERE IN RAM.
 *
 * Il framebuffer e' mappato con PG_PRESENT | PG_WRITABLE e basta
 * (paging_mappa_framebuffer). Che tipo di memoria sia lo decidono gli MTRR, e
 * quelli li ha lasciati il BIOS: fuori dalla RAM il tipo predefinito e'
 * praticamente sempre UC — «non cacheabile, e nemmeno combinabile».
 *
 * Su memoria UC ogni store e' una transazione sul bus per conto suo. Un
 * `mov [edi], eax` di quattro byte attraversa il chipset da solo; i buffer di
 * combinazione della CPU — quelli che raccolgono 64 byte e li mandano in un
 * colpo — non entrano in gioco. E siccome non entrano in gioco, MMX e SSE non
 * servono a niente: otto o sedici byte per istruzione restano otto o sedici
 * byte per transazione. E' il motivo per cui un compositore ottimizzato a mano
 * puo' non andare piu' veloce di uno scritto in C.
 *
 * Con il tipo WC — write combining — la CPU raccoglie le scritture vicine in un
 * buffer e le consegna in blocchi. Sulle schede di quell'epoca il salto
 * misurato e' fra cinque e dieci volte. Non e' una micro-ottimizzazione: e' la
 * differenza fra un'interfaccia che scatta e una che no.
 *
 * -----------------------------------------------------------------------------
 * ! PERCHE' GLI MTRR E NON IL PAT
 *
 * Il PAT permetterebbe di dire «questa pagina e' WC» senza toccare registri
 * globali, e sarebbe piu' fine. Ma il tipo che vale e' la COMBINAZIONE dei due,
 * e nella tabella di Intel (SDM, «Effective Memory Type») MTRR=UC insieme a
 * PAT=WC da' UC: il PAT non puo' ammorbidire quel che l'MTRR ha dichiarato
 * incacheabile. Chi comanda qui e' l'MTRR, e quindi e' li' che si scrive.
 *
 * -----------------------------------------------------------------------------
 * ! UNA SOLA FASCIA, E SI RINUNCIA INVECE DI ARRANGIARSI
 *
 * Una fascia variabile MTRR copre un intervallo di misura POTENZA DI DUE e
 * ALLINEATO alla propria misura. Un framebuffer 800x600x32 sono 1,83 MB: per
 * coprirli esattamente servirebbero cinque o sei fasce (1 MB + 512 KB + ...),
 * e le fasce sono otto in tutto, con il BIOS che ne usa gia' parecchie.
 *
 * Percio' si ARROTONDA PER ECCESSO alla potenza di due successiva — 2 MB — e si
 * usa UNA fascia sola. Il di piu' non e' terra di nessuno: e' l'apertura della
 * scheda video, cioe' altra memoria video, e dichiararla combinabile e'
 * esattamente cio' che si vuole.
 *
 * E se l'indirizzo non e' allineato alla misura arrotondata, si prova a
 * crescere; se nemmeno cosi' torna, SI LASCIA PERDERE e si scrive nel log. Una
 * fascia MTRR sbagliata non rallenta: puo' dichiarare cacheabile un registro di
 * periferica, e allora la macchina fa cose che nessuno spiega piu'.
 *
 * -----------------------------------------------------------------------------
 * ! E NON SI TOCCA NIENTE SE QUALCOSA NON TORNA
 *
 * Si rinuncia — dicendolo — se: la CPU non ha gli MTRR, non dichiara di saper
 * fare WC, non sono abilitati, non c'e' una fascia libera, l'intervallo si
 * sovrappone alla RAM, o e' gia' coperto da una fascia che dice altro. L'unico
 * caso in cui si scrive e' quello in cui si sa esattamente cosa si sta facendo.
 *
 * ! LA SEQUENZA DI SCRITTURA E' QUELLA DEL MANUALE, E OGNI PASSO SERVE. Fra
 * l'istante in cui si spegne la cache e quello in cui la si riaccende, la CPU
 * lavora senza: e' lentissima, ed e' per questo che si fa una volta sola
 * all'avvio e mai piu'. Saltare il wbinvd vorrebbe dire lasciare in cache righe
 * catalogate col tipo vecchio.
 * ============================================================================= */

#include "kernel.h"
#include "pmm.h"

#define MSR_MTRRCAP         0x0FEu
#define MSR_MTRR_PHYSBASE0  0x200u
#define MSR_MTRR_PHYSMASK0  0x201u
#define MSR_MTRR_DEF_TYPE   0x2FFu

#define MTRR_TIPO_UC        0x00u
#define MTRR_TIPO_WC        0x01u
#define MTRR_TIPO_WB        0x06u

#define MASK_VALIDA         (1u << 11)   /* bit V di PHYSMASK        */
#define DEF_ABILITATI       (1u << 11)   /* bit E di MTRRdefType     */
#define DEF_FISSE_ATTIVE    (1u << 10)   /* bit FE                   */

#define CR0_CD              (1u << 30)
#define CR0_NW              (1u << 29)
#define CR4_PGE_BIT         (1u << 7)

/* -----------------------------------------------------------------------------
 * Gli attrezzi: MSR e CPUID
 *
 * ! rdmsr/wrmsr SONO ISTRUZIONI PRIVILEGIATE E NON ESISTONO PRIMA DEL PENTIUM.
 * Chi le esegue su un 486 prende una #UD. Qui si arriva solo dopo aver
 * verificato CPUID, che a sua volta non esiste prima del 486 tardo — ma il
 * kernel gia' lo usa altrove, quindi il terreno e' noto.
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

/* Quanti bit di indirizzo FISICO ha questa CPU.
 *
 * ! SERVE PER SAPERE FIN DOVE ARRIVA LA MASCHERA, e sbagliarlo costa una #GP:
 * scrivere un bit riservato sopra MAXPHYADDR fa fallire wrmsr. CPUID
 * 0x80000008 lo dice; se quella foglia non c'e' si guarda PSE-36, che promette
 * 36 bit, e in mancanza d'altro si resta a 32 — che e' sempre sicuro, perche'
 * lascia i bit alti a zero. */
static uint32_t bit_fisici(void)
{
    uint32_t a, b, c, d;

    cpuid_di(0x80000000u, &a, &b, &c, &d);
    if (a >= 0x80000008u) {
        cpuid_di(0x80000008u, &a, &b, &c, &d);
        if ((a & 0xFFu) >= 32u && (a & 0xFFu) <= 52u) return a & 0xFFu;
    }

    cpuid_di(1u, &a, &b, &c, &d);
    if (d & (1u << 17)) return 36u;      /* PSE-36 */
    return 32u;
}

static int cpu_ha_mtrr(void)
{
    uint32_t a, b, c, d;

    cpuid_di(0u, &a, &b, &c, &d);
    if (a < 1u) return 0;
    cpuid_di(1u, &a, &b, &c, &d);
    return (d & (1u << 12)) != 0;        /* MTRR */
}

static const char *nome_tipo(uint32_t t)
{
    switch (t) {
    case 0x00: return "UC (non cacheabile)";
    case 0x01: return "WC (combinabile)";
    case 0x04: return "WT (write-through)";
    case 0x05: return "WP (write-protect)";
    case 0x06: return "WB (write-back)";
    default:   return "riservato";
    }
}

/* =============================================================================
 * Cosa c'e' scritto adesso
 *
 * ! SI STAMPA SEMPRE, ANCHE QUANDO NON SI CAMBIA NIENTE. La tabella degli MTRR
 * e' il documento che spiega perche' una macchina va piano, e non si puo'
 * leggere da ring 3: se non la scrive il kernel all'avvio, non la vede nessuno.
 * ========================================================================== */
static uint32_t g_fasce = 0;            /* quante fasce variabili ha la CPU */

void mtrr_racconta(void)
{
    uint32_t cap_b, cap_a, def_b, def_a, i;

    if (!cpu_ha_mtrr()) {
        klog(LOG_INFO, "MTRR: questa CPU non li ha");
        return;
    }

    msr_leggi(MSR_MTRRCAP, &cap_b, &cap_a);
    msr_leggi(MSR_MTRR_DEF_TYPE, &def_b, &def_a);
    g_fasce = cap_b & 0xFFu;

    klog(LOG_INFO, "MTRR: %u fasce variabili, WC %s, %s, predefinito %s",
         g_fasce,
         (cap_b & (1u << 10)) ? "si" : "NO",
         (def_b & DEF_ABILITATI) ? "abilitati" : "SPENTI",
         nome_tipo(def_b & 0xFFu));

    for (i = 0; i < g_fasce && i < 16u; i++) {
        uint32_t bb, ba, mb, ma;

        msr_leggi(MSR_MTRR_PHYSBASE0 + i * 2u, &bb, &ba);
        msr_leggi(MSR_MTRR_PHYSMASK0 + i * 2u, &mb, &ma);
        if (!(mb & MASK_VALIDA)) continue;

        /* La misura si ricava dalla maschera: e' il primo bit acceso. */
        {
            uint32_t m = mb & 0xFFFFF000u;
            uint32_t misura = m ? (uint32_t)(~m + 1u) : 0u;

            klog(LOG_INFO, "MTRR:   [%u] 0x%08x  %u KB  %s",
                 i, bb & 0xFFFFF000u, misura / 1024u, nome_tipo(bb & 0xFFu));
        }
    }
}

/* Il tipo dichiarato per un indirizzo, o -1 se nessuna fascia lo copre. */
static int tipo_di(uint32_t indirizzo)
{
    uint32_t i;

    for (i = 0; i < g_fasce && i < 16u; i++) {
        uint32_t bb, ba, mb, ma;

        msr_leggi(MSR_MTRR_PHYSBASE0 + i * 2u, &bb, &ba);
        msr_leggi(MSR_MTRR_PHYSMASK0 + i * 2u, &mb, &ma);
        if (!(mb & MASK_VALIDA)) continue;

        if ((indirizzo & mb & 0xFFFFF000u) == (bb & mb & 0xFFFFF000u))
            return (int)(bb & 0xFFu);
    }
    return -1;
}

/* La prima fascia libera, o -1. */
static int fascia_libera(void)
{
    uint32_t i;

    for (i = 0; i < g_fasce && i < 16u; i++) {
        uint32_t mb, ma;

        msr_leggi(MSR_MTRR_PHYSMASK0 + i * 2u, &mb, &ma);
        if (!(mb & MASK_VALIDA)) return (int)i;
    }
    return -1;
}

/* =============================================================================
 * La scrittura vera, con la cache spenta intorno
 *
 * La sequenza e' quella dell'SDM di Intel, «Procedure for Changing the Memory
 * Type»: si spegne la cache, si svuota, si spengono gli MTRR, si scrive, si
 * riaccende tutto. Con gli interrupt chiusi, perche' un'interruzione presa a
 * cache spenta gira a un decimo della velocita' e tocca memoria che in quel
 * momento e' catalogata male.
 * ========================================================================== */
static void scrivi_fascia(uint32_t n, uint32_t base, uint32_t misura,
                          uint32_t tipo, uint32_t maschera_alta)
{
    uint32_t cr0, cr4 = 0, def_b, def_a;
    uint32_t flags;
    int      pge;

    __asm__ __volatile__("pushfl; popl %0" : "=r"(flags));
    __asm__ __volatile__("cli");

    cr0 = read_cr0();
    write_cr0((cr0 | CR0_CD) & ~CR0_NW);
    __asm__ __volatile__("wbinvd");

    cr4 = read_cr4();
    pge = (cr4 & CR4_PGE_BIT) != 0;
    if (pge) write_cr4(cr4 & ~CR4_PGE_BIT);
    write_cr3(read_cr3());                       /* svuota il TLB */

    msr_leggi(MSR_MTRR_DEF_TYPE, &def_b, &def_a);
    msr_scrivi(MSR_MTRR_DEF_TYPE, def_b & ~DEF_ABILITATI, def_a);

    msr_scrivi(MSR_MTRR_PHYSBASE0 + n * 2u, (base & 0xFFFFF000u) | tipo, 0u);
    msr_scrivi(MSR_MTRR_PHYSMASK0 + n * 2u,
               ((~(misura - 1u)) & 0xFFFFF000u) | MASK_VALIDA, maschera_alta);

    msr_scrivi(MSR_MTRR_DEF_TYPE, def_b | DEF_ABILITATI, def_a);

    __asm__ __volatile__("wbinvd");
    write_cr3(read_cr3());
    if (pge) write_cr4(cr4);
    write_cr0(cr0);

    __asm__ __volatile__("pushl %0; popfl" : : "r"(flags));
}

/* =============================================================================
 * mtrr_framebuffer_wc — l'unica porta d'ingresso
 *
 * Rende 0 se da adesso quell'intervallo e' combinabile in scrittura, -1 se si
 * e' rinunciato (e nel log c'e' scritto perche').
 * ========================================================================== */
int mtrr_framebuffer_wc(uint32_t base, uint32_t byte)
{
    uint32_t cap_b, cap_a, def_b, def_a;
    uint32_t misura, alta, bit;
    int      n, gia;

    if (base == 0u || byte == 0u) return -1;

    if (!cpu_ha_mtrr()) {
        klog(LOG_INFO, "MTRR: la CPU non li ha, il framebuffer resta com'e'");
        return -1;
    }

    msr_leggi(MSR_MTRRCAP, &cap_b, &cap_a);
    msr_leggi(MSR_MTRR_DEF_TYPE, &def_b, &def_a);
    g_fasce = cap_b & 0xFFu;

    if (!(cap_b & (1u << 10))) {
        klog(LOG_WARN, "MTRR: questa CPU non sa fare write-combining");
        return -1;
    }
    if (!(def_b & DEF_ABILITATI)) {
        klog(LOG_WARN, "MTRR: sono spenti: non li accendo io");
        return -1;
    }

    /* ! NON SI TOCCA NIENTE CHE POSSA ESSERE RAM. Un intervallo dichiarato WC
     * sopra della memoria normale toglie la cache a quella memoria: la
     * macchina non si guasta, rallenta di dieci volte e nessuno capisce
     * perche'. Il framebuffer sta sempre molto sopra la RAM installata. */
    {
        uint32_t ram = pmm_get_total_pages() * 4096u;

        if (base < ram) {
            klog(LOG_WARN, "MTRR: 0x%08x sta dentro la RAM (%u KB): rinuncio",
                 base, ram / 1024u);
            return -1;
        }
    }

    /* La misura: potenza di due, arrotondata per ECCESSO, e almeno 4 KB. */
    misura = 4096u;
    while (misura < byte && misura < 0x40000000u) misura <<= 1;

    /* E l'allineamento: se non torna, si cresce; se non torna comunque, si
     * lascia perdere invece di spezzare l'intervallo in sei fasce. */
    while ((base & (misura - 1u)) != 0u && misura < 0x40000000u) misura <<= 1;
    if ((base & (misura - 1u)) != 0u) {
        klog(LOG_WARN, "MTRR: 0x%08x non si allinea a nessuna misura: rinuncio",
             base);
        return -1;
    }

    gia = tipo_di(base);
    if (gia == (int)MTRR_TIPO_WC) {
        klog(LOG_INFO, "MTRR: il framebuffer era gia' combinabile, non tocco");
        return 0;
    }
    if (gia >= 0 && gia != (int)MTRR_TIPO_UC) {
        klog(LOG_WARN, "MTRR: 0x%08x e' gia' dichiarato %s da una fascia: "
                       "non la sovrascrivo", base, nome_tipo((uint32_t)gia));
        return -1;
    }

    n = fascia_libera();
    if (n < 0) {
        klog(LOG_WARN, "MTRR: tutte le %u fasce sono occupate dal BIOS",
             g_fasce);
        return -1;
    }

    /* I bit alti della maschera: tutti quelli che esistono sopra il 31esimo,
     * perche' il nostro intervallo sta sotto i 4 GB e quei bit devono essere
     * confrontati e valere zero. */
    bit  = bit_fisici();
    alta = (bit > 32u) ? ((1u << (bit - 32u)) - 1u) : 0u;

    scrivi_fascia((uint32_t)n, base, misura, MTRR_TIPO_WC, alta);

    if (tipo_di(base) != (int)MTRR_TIPO_WC) {
        klog(LOG_ERROR, "MTRR: la fascia %d non ha preso: il framebuffer "
                        "resta UC", n);
        return -1;
    }

    klog(LOG_INFO, "MTRR: framebuffer 0x%08x, %u KB combinabili in scrittura "
                   "(fascia %d di %u)",
         base, misura / 1024u, n, g_fasce);
    return 0;
}
