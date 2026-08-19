/* =============================================================================
 * kernel/arch/x86/tsc.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * RDTSC e la sua calibrazione
 *
 * ! IL CONTATORE DA SOLO NON DICE IL TEMPO. RDTSC conta cicli, e nessuno gli
 * ha detto quanti cicli stanno in un secondo: quel numero non e' scritto da
 * nessuna parte nella CPU. Va MISURATO contro un orologio di cui si conosce la
 * frequenza — e l'unico che c'e' all'avvio e' il PIT, che oscilla a
 * 1.193.182 Hz perche' e' un terzo del quarzo dei colori NTSC. Un cristallo da
 * televisione degli anni Ottanta e' il metro con cui si misura il processore.
 *
 * ! E NON SI PUO' USARE g_ticks. Sarebbe la strada ovvia — aspetta N tick, vedi
 * quanti cicli sono passati — ed e' sbagliata QUI: tsc_init() gira durante
 * l'avvio, e in EX-OS l'IRQ0 resta mascherato fino a sched_start(), che e'
 * l'ultima riga di kernel_main. g_ticks vale zero e li' resterebbe: il ciclo di
 * attesa non finirebbe mai e la macchina si pianterebbe all'avvio, prima di
 * scrivere una riga di log che spieghi perche'.
 *
 * Quindi si usa il CANALE 2 del PIT, che e' l'unico dei tre che si puo' leggere
 * senza interrupt: il suo piedino di uscita e' visibile nel bit 5 della porta
 * 0x61, e il suo ingresso di abilitazione nel bit 0. Nato per l'altoparlante,
 * serve qui come cronometro. Nessun altro in EX-OS lo tocca (verificato: nessun
 * uso di 0x42 o 0x61 nel kernel).
 *
 * ! IL CANALE 2 NON E' IL CANALE 0. Il canale 0 e' quello che genera l'IRQ0 e
 * fa avanzare g_ticks: riprogrammarlo per misurare vorrebbe dire cambiare la
 * frequenza dello scheduler per 50 millisecondi e poi rimetterla — e se la
 * calibrazione fallisse a meta', il sistema resterebbe con un quanto di tempo
 * sbagliato per sempre. Il canale 2 non ha nessun compito da cui distrarlo.
 *
 * ! CIO' CHE SI MISURA NON E' STABILE SU OGNI MACCHINA, ed e' dichiarato. Su un
 * Pentium 133 il TSC avanza col clock del core e basta. Su portatili piu'
 * recenti la frequenza cambia col risparmio energetico, e su macchine a piu'
 * processori i contatori possono partire in momenti diversi. Per un sistema che
 * ha come bersaglio dichiarato il Pentium 133 MMX questo non e' un problema
 * oggi; il giorno che EX-OS girera' su piu' core andra' riletto — c'e' un bit
 * di CPUID (foglia 0x80000007) che dice se il TSC e' invariante, e quel giorno
 * si guardera' quello.
 * ============================================================================= */

#include "kernel.h"
#include "tsc.h"
#include "fpu.h"

/* Frequenza del PIT: 1.193.182 Hz. */
#define PIT_HZ          1193182u

/* Porte del canale 2. */
#define PIT_CH2         0x42
#define PIT_COMANDO     0x43
#define PIT_GATE        0x61        /* bit 0 = abilita, bit 1 = altoparlante,
                                       bit 5 = uscita del contatore */

/* Quanto dura la misura. 59659 conteggi del PIT sono 50 ms tondi, e stanno
 * dentro i 16 bit del contatore (il massimo e' 65535, cioe' 54,9 ms).
 *
 * ! PIU' CORTA SAREBBE PIU' IMPRECISA, piu' lunga non ci sta. Con 50 ms
 * l'errore di un singolo conteggio del PIT vale 0,8 milionesimi: irrilevante
 * accanto al rumore della lettura stessa. */
#define CONTEGGI        59659u
#define MICROSEC        50000u

static int      g_c_e = 0;
static uint32_t g_hz  = 0;

uint64_t tsc_leggi(void)
{
    uint32_t basso, alto;

    __asm__ __volatile__("rdtsc" : "=a"(basso), "=d"(alto));
    return ((uint64_t)alto << 32) | (uint64_t)basso;
}

int      tsc_c_e(void) { return g_c_e; }
uint32_t tsc_hz(void)  { return g_hz;  }

void tsc_init(void)
{
    const CpuCapacita *cpu = cpu_capacita();
    uint64_t           t0, t1;
    uint32_t           delta;
    uint8_t            gate;
    uint32_t           guardia;

    if (!cpu->tsc) {
        klog(LOG_INFO, "TSC: assente (CPU pre-Pentium) - le misure fini "
                       "restano indisponibili");
        return;
    }

    /* Abilita il canale 2 (bit 0) e tiene ZITTO l'altoparlante (bit 1 a zero).
     *
     * ! IL BIT 1 VA SPENTO A MANO. E' il bit che collega l'uscita del contatore
     * al diffusore: lasciandolo com'era, una macchina che l'aveva acceso
     * suonerebbe per tutta la calibrazione — un fischio di 50 ms a ogni avvio,
     * e nessuno capirebbe da dove viene. */
    gate = port_inb(PIT_GATE);
    port_outb(PIT_GATE, (uint8_t)((gate & ~0x02u) | 0x01u));

    /* Canale 2, leggi/scrivi due byte, modo 0 (conta e alza l'uscita alla
     * fine), binario. Il modo 0 e' l'unico che si ferma da solo: gli altri
     * ripartono, e una misura che ricomincia da capo non si sa quando finisce. */
    port_outb(PIT_COMANDO, 0xB0);
    port_outb(PIT_CH2, (uint8_t)(CONTEGGI & 0xFFu));
    port_outb(PIT_CH2, (uint8_t)(CONTEGGI >> 8));

    t0 = tsc_leggi();

    /* ! IL CICLO HA UNA GUARDIA, e non e' pignoleria. Se il PIT non risponde —
     * hardware virtuale monco, canale gia' dirottato da un firmware — il bit 5
     * non si alza MAI e questo diventa un blocco all'avvio senza spiegazione.
     * Meglio rinunciare al TSC che non partire: il conteggio e' largo, decine
     * di volte quello che serve davvero. */
    guardia = 200000000u;
    while (!(port_inb(PIT_GATE) & 0x20u)) {
        if (--guardia == 0) {
            port_outb(PIT_GATE, (uint8_t)(gate & ~0x03u));
            klog(LOG_WARN, "TSC: il canale 2 del PIT non risponde - "
                           "calibrazione abbandonata");
            return;
        }
    }

    t1 = tsc_leggi();

    /* Spegne il gate e rimette l'altoparlante come stava. */
    port_outb(PIT_GATE, (uint8_t)(gate & ~0x03u));

    /* ! LA DIFFERENZA STA IN 32 BIT, e vale la pena dire perche'. In 50 ms una
     * CPU a 1 GHz fa 50 milioni di cicli; ce ne vorrebbero 85 di GHz per
     * riempire i 32 bit. Farla a 64 costringerebbe a una divisione a 64 bit,
     * che senza libgcc nel kernel andrebbe scritta a mano. */
    if ((t1 - t0) >> 32) {
        klog(LOG_WARN, "TSC: misura fuori scala - calibrazione scartata");
        return;
    }
    delta = (uint32_t)(t1 - t0);

    /* Da cicli in 50 ms a cicli al secondo: per venti. */
    if (delta < 1000u) {
        klog(LOG_WARN, "TSC: misura implausibile (%u cicli in 50 ms) - "
                       "calibrazione scartata", delta);
        return;
    }

    g_hz  = delta * (1000000u / MICROSEC);
    g_c_e = 1;

    klog(LOG_INFO, "TSC: %u.%03u MHz (%u cicli in 50 ms)",
         g_hz / 1000000u, (g_hz / 1000u) % 1000u, delta);
}

uint32_t tsc_us(uint64_t cicli)
{
    uint32_t c;

    if (!g_c_e) return 0;

    /* Sopra i 32 bit si tronca invece di mentire: un intervallo cosi' lungo
     * (ore, sul bersaglio) non e' quello che questa funzione serve a misurare. */
    if (cicli >> 32) return 0xFFFFFFFFu;
    c = (uint32_t)cicli;

    /* ! SI DIVIDE PRIMA, E SI PERDE PRECISIONE APPOSTA. Moltiplicare per
     * 1.000.000 traboccherebbe gia' a 4295 cicli — cioe' su qualunque misura
     * utile. Dividendo prima per (hz/1.000.000) l'unita' resta il microsecondo
     * e il campo copre piu' di un'ora. */
    {
        uint32_t per_us = g_hz / 1000000u;
        if (per_us == 0) per_us = 1;
        return c / per_us;
    }
}
