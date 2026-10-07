/* =============================================================================
 * kernel/include/smp.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Piu' processori (SMP) — tappa 1: trovarli, accenderli, parcheggiarli
 *
 * ! A QUESTA TAPPA EX-OS LAVORA ANCORA CON UN PROCESSORE SOLO. Gli altri
 * vengono contati, svegliati uno alla volta, portati in modo protetto con le
 * tabelle del kernel e lasciati in attesa. Non eseguono processi e non
 * toccano nessuna struttura del kernel oltre alla propria voce qui sotto.
 *
 * TAPPA 1 (0.237): trovarli, svegliarli, fermarli.
 * TAPPA 2 (0.238): ogni processore in piu' ha il suo APIC locale acceso, un
 *   timer suo a 100 Hz e riceve i messaggi degli altri (IPI). L'APIC locale
 *   sta in una pagina fissa che ogni spazio di indirizzamento vede
 *   (PAGING_APIC_VIRT), quindi anche una chiamata di sistema puo' mandare un
 *   messaggio. E' l'attrezzatura che serve alla tappa 3: il timer per lo
 *   scheduler di quel processore, i messaggi per dirgli «c'e' lavoro» e
 *   «butta via questa traduzione di pagina».
 * TAPPA 3: un lucchetto unico sul kernel, e i processori in piu' eseguono
 *   processi. Vuole GDT e TSS per processore, il processo corrente per
 *   processore, la finestra di rimappatura per processore.
 * TAPPA 4: lucchetti piu' fini.
 *
 * ! L'I/O APIC NON E' IN QUESTO ELENCO, APPOSTA. Serve a mandare gli
 * interrupt delle periferiche a un processore diverso da quello d'avvio, e
 * finche' il kernel ha un lucchetto solo non c'e' niente da guadagnare: le
 * periferiche restano sul PIC, verso il processore d'avvio, come sempre. E' la
 * parte che piu' dipende dalle tabelle del BIOS (polarita', deviazioni degli
 * IRQ ISA) e quindi quella che sul ferro si sbaglia piu' facilmente.
 *
 * ! NIENTE DI QUESTO COSTA A CHI HA UN PROCESSORE SOLO. Senza CPUID, senza
 * APIC locale o con una tabella che elenca una CPU, smp_init() torna dopo
 * aver letto qualche byte del BIOS: non mappa niente, non scrive in nessun
 * registro, non alloca una pagina.
 * ============================================================================= */
#ifndef SMP_H
#define SMP_H

#include "kernel.h"

#define SMP_CPU_MAX         16

/* Da dove viene l'elenco dei processori. */
#define SMP_FONTE_NESSUNA   0   /* nessuna tabella: un processore solo */
#define SMP_FONTE_ACPI      1   /* ACPI, tabella MADT («APIC») */
#define SMP_FONTE_MP        2   /* tabella della MultiProcessor Specification 1.4 */
#define SMP_FONTE_MP_FISSA  3   /* MP, una delle configurazioni predefinite */

/* Lo stato di un processore. */
#define SMP_CPU_AVVIO       1   /* quello su cui EX-OS e' partito e gira */
#define SMP_CPU_FERMO       2   /* acceso, in attesa: riceve il suo timer e i
                                 * messaggi, non esegue processi */
#define SMP_CPU_MUTO        3   /* svegliato, non ha risposto entro la scadenza */
#define SMP_CPU_LASCIATO    4   /* elencato ma non svegliato (vedi `motivo`) */

/* Perche' i processori in piu' non sono stati svegliati (0 = lo sono stati,
 * o non ce ne sono). */
#define SMP_MOTIVO_NESSUNO  0
#define SMP_MOTIVO_CFG      1   /* kernel.cfg dice smp = 0 */
#define SMP_MOTIVO_X2APIC   2   /* APIC in modo x2APIC: non si pilota dalla memoria */
#define SMP_MOTIVO_SPENTO   3   /* APIC locale spento dal BIOS */
#define SMP_MOTIVO_MEMORIA  4   /* niente pagine per mappare l'APIC o per le pile */

typedef struct {
    uint8_t  apic_id;
    uint8_t  stato;         /* SMP_CPU_* */
    uint8_t  apic_ver;      /* versione dell'APIC locale, 0 se non nota */
    uint8_t  riservato;
    uint32_t firma;         /* CPUID.1 EAX: famiglia, modello, stepping */
    uint32_t capacita;      /* CPUID.1 EDX */
    uint32_t battiti;       /* interrupt del proprio timer ricevuti (100 al secondo) */
    uint32_t messaggi;      /* messaggi da un altro processore (IPI) ricevuti */
} SmpCpu;

/* ! QUESTA STRUTTURA ESCE DAL KERNEL (SYS_CPU_INFO) e lib/include/libc.h ne ha
 * la copia: si cambiano insieme, e la dimensione viaggia con la chiamata come
 * per MemInfo. */
typedef struct {
    uint32_t n;             /* processori elencati e abilitati (almeno 1) */
    uint32_t fermi;         /* quanti, oltre a quello d'avvio, hanno risposto */
    uint32_t fonte;         /* SMP_FONTE_* */
    uint32_t motivo;        /* SMP_MOTIVO_* */
    uint32_t apic_locale;   /* indirizzo fisico dell'APIC locale, 0 se non c'e' */
    uint32_t n_ioapic;      /* I/O APIC elencati */
    uint32_t ioapic;        /* indirizzo fisico del primo, 0 se nessuno */
    uint32_t troppi;        /* processori elencati oltre SMP_CPU_MAX, ignorati */
    uint32_t timer_per_tick;/* conteggi del timer dell'APIC in 10 ms; 0 = non misurato */
    uint32_t tick;          /* g_ticks al momento della chiamata, per confrontare */
    SmpCpu   cpu[SMP_CPU_MAX];
} SmpInfo;

/* Cerca i processori e, se `accendi` non e' zero, sveglia e parcheggia quelli
 * in piu'. Da chiamare una volta, a interrupt accesi (le attese contano i
 * tick) e prima che parta il primo processo. */
void           smp_init(int accendi);

/* Cio' che smp_init() ha trovato. Mai NULL. */
const SmpInfo *smp_info(void);

/* Manda un messaggio (IPI) a ogni processore in attesa: ciascuno lo conta in
 * `messaggi`. Da qualunque contesto di processo; non aspetta la risposta. */
void           smp_chiama_tutti(void);

#endif /* SMP_H */
