/* =============================================================================
 * kernel/arch/x86/rtc.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Lettura dell'orologio CMOS (MC146818 e compatibili).
 *
 * Tre trappole, tutte e tre già costate a chiunque abbia scritto questo
 * codice almeno una volta:
 *
 * 1. L'AGGIORNAMENTO IN CORSO. Una volta al secondo il chip riscrive i
 *    propri registri, e durante quella finestra i valori sono
 *    incoerenti: si può leggere 10:59:59 e poi 10:00:00 nella stessa
 *    lettura, perché i secondi sono stati presi prima
 *    dell'aggiornamento e le ore dopo. Il bit UIP del registro A dice
 *    che l'aggiornamento sta per arrivare o è in corso.
 *
 * 2. IL FORMATO NON È UNO. I valori possono essere in BCD (0x59 vale
 *    cinquantanove) o in binario, e le ore in formato 12 o 24 con il
 *    bit alto a segnalare il pomeriggio. Lo dice il registro B, e va
 *    letto: non si può assumere, perché dipende dal BIOS.
 *
 * 3. IL SECOLO NON C'È. Il chip conserva due cifre d'anno. Il registro
 *    del secolo esiste ma il suo indirizzo cambia da macchina a
 *    macchina (0x32 o 0x37) e su parecchie non è implementato affatto.
 *    Qui si usa la convenzione consueta: anni sotto 70 sono 2000+, gli
 *    altri 1900+.
 *
 * La difesa contro la 1 è leggere DUE VOLTE e accettare solo se le due
 * letture coincidono. È più robusto che fidarsi del solo bit UIP, che
 * dice "sta per aggiornare" ma lascia comunque una finestra fra il
 * controllo e la lettura dei registri.
 * ============================================================================= */

#include "kernel.h"
#include "rtc.h"

#define CMOS_INDICE     0x70
#define CMOS_DATO       0x71

#define CMOS_SECONDI    0x00
#define CMOS_MINUTI     0x02
#define CMOS_ORE        0x04
#define CMOS_GIORNO     0x07
#define CMOS_MESE       0x08
#define CMOS_ANNO       0x09
#define CMOS_STATO_A    0x0A
#define CMOS_STATO_B    0x0B

#define STATO_A_UIP     0x80    /* Update In Progress */
#define STATO_B_24H     0x02    /* 1 = formato 24 ore */
#define STATO_B_BINARIO 0x04    /* 1 = valori binari, 0 = BCD */

/* Quanto si è disposti ad aspettare che l'aggiornamento finisca. Il
 * chip lo completa in circa 2 ms; il limite serve a non appendere il
 * kernel se l'orologio non c'è e le porte restituiscono 0xFF (che ha il
 * bit UIP sempre alto). */
#define RTC_ATTESA_MAX  100000

static uint8_t cmos_leggi(uint8_t reg)
{
    /* Il bit 7 dell'indice disabilita gli NMI. Va lasciato a zero: qui
     * non si sta facendo niente di così delicato da doverli mascherare,
     * e lasciarli disabilitati per sbaglio è un guaio molto peggiore. */
    port_outb(CMOS_INDICE, reg & 0x7F);
    return port_inb(CMOS_DATO);
}

static int aggiornamento_in_corso(void)
{
    return (cmos_leggi(CMOS_STATO_A) & STATO_A_UIP) != 0;
}

static uint32_t da_bcd(uint8_t v)
{
    return (uint32_t)((v & 0x0F) + ((v >> 4) * 10));
}

/* Una passata di lettura grezza, senza conversioni. */
static void leggi_grezzo(uint8_t *s, uint8_t *m, uint8_t *o,
                         uint8_t *g, uint8_t *me, uint8_t *a)
{
    *s  = cmos_leggi(CMOS_SECONDI);
    *m  = cmos_leggi(CMOS_MINUTI);
    *o  = cmos_leggi(CMOS_ORE);
    *g  = cmos_leggi(CMOS_GIORNO);
    *me = cmos_leggi(CMOS_MESE);
    *a  = cmos_leggi(CMOS_ANNO);
}

int rtc_read(RtcTime *t)
{
    uint8_t  s, m, o, g, me, a;
    uint8_t  s2, m2, o2, g2, me2, a2;
    uint8_t  statoB;
    uint32_t attesa;
    int      tentativi;

    if (t == NULL) return -1;

    /* Aspetta la fine di un eventuale aggiornamento in corso. */
    for (attesa = 0; aggiornamento_in_corso(); attesa++) {
        if (attesa > RTC_ATTESA_MAX) return -1;   /* orologio assente o rotto */
    }

    /* Due letture uguali di fila: vedi la trappola 1 in testa al file.
     * Tre tentativi bastano — se l'aggiornamento cade in mezzo alla
     * prima coppia, la seconda lo trova già finito. */
    leggi_grezzo(&s, &m, &o, &g, &me, &a);

    for (tentativi = 0; tentativi < 3; tentativi++) {
        leggi_grezzo(&s2, &m2, &o2, &g2, &me2, &a2);

        if (s == s2 && m == m2 && o == o2 && g == g2 && me == me2 && a == a2) {
            break;
        }

        s = s2; m = m2; o = o2; g = g2; me = me2; a = a2;
    }
    if (tentativi == 3) return -1;   /* non si stabilizza: meglio dire di no */

    statoB = cmos_leggi(CMOS_STATO_B);

    if (!(statoB & STATO_B_BINARIO)) {
        /* BCD. Le ore vanno convertite conservando il bit 7 (pomeriggio
         * nel formato 12 ore), che non fa parte del numero. */
        uint8_t pomeriggio = (uint8_t)(o & 0x80);

        s  = (uint8_t)da_bcd(s);
        m  = (uint8_t)da_bcd(m);
        o  = (uint8_t)(da_bcd((uint8_t)(o & 0x7F)) | pomeriggio);
        g  = (uint8_t)da_bcd(g);
        me = (uint8_t)da_bcd(me);
        a  = (uint8_t)da_bcd(a);
    }

    if (!(statoB & STATO_B_24H) && (o & 0x80)) {
        /* Formato 12 ore, pomeriggio: le 12 PM restano 12, le altre
         * sommano 12. Senza il caso speciale, mezzogiorno diventerebbe
         * le 24. */
        o = (uint8_t)(((o & 0x7F) % 12) + 12);
    }

    t->secondo = s;
    t->minuto  = m;
    t->ora     = (uint32_t)(o & 0x7F);
    t->giorno  = g;
    t->mese    = me;
    t->anno    = (a < 70) ? (2000u + a) : (1900u + a);

    /* Validazione. Su hardware vecchio con la batteria scarica il CMOS
     * restituisce valori senza senso, e una barra di stato che mostra
     * "il 47 del mese 93" è peggio di una che ammette di non saperlo. */
    if (t->mese  < 1 || t->mese  > 12) return -1;
    if (t->giorno < 1 || t->giorno > 31) return -1;
    if (t->ora    > 23) return -1;
    if (t->minuto > 59) return -1;
    if (t->secondo > 59) return -1;
    if (t->anno < 1980 || t->anno > 2199) return -1;

    return 0;
}
