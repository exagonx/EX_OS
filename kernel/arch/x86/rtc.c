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

/* =============================================================================
 * SCRITTURA — dal kernel 0.218, 16 settembre 2026
 *
 * Fino a oggi l'orologio di EX-OS si LEGGEVA e basta, e la conseguenza si
 * vedeva sulla macchina vera: l'Acer segnava il 2005, e ogni file che
 * caricava sul server arrivava datato «Feb 9 2005». Non c'era modo di
 * rimetterlo a posto da EX-OS — bisognava entrare nel BIOS.
 *
 * ! LE TRE TRAPPOLE DELLA LETTURA VALGONO TUTTE, PIU' UNA QUARTA.
 *
 * 4. NON SI SCRIVE MENTRE IL CHIP CONTA. Se l'aggiornamento cade in mezzo
 *    ai sei registri, il chip riscrive sopra a quel che si e' appena messo:
 *    si ottiene una data mezza vecchia e mezza nuova, e il minuto che si
 *    voleva mettere puo' sparire del tutto. Il bit SET del registro B
 *    (0x80) FERMA il conteggio: si alza, si scrivono i sei registri, lo si
 *    riabbassa. Finche' e' alto il chip non tocca niente.
 *
 * ! E IL FORMATO LO DECIDE IL CHIP, NON NOI. Il registro B dice se i
 * valori sono BCD o binari e se le ore sono a 12 o a 24: si scrive NEL
 * FORMATO CHE C'E' GIA'. Cambiare il formato sarebbe piu' comodo da
 * programmare e romperebbe la lettura del BIOS, che quel formato lo
 * conosce dall'accensione.
 *
 * ! IL SECOLO SI SCRIVE SOLO SE C'E' GIA'. rtc_read non lo legge affatto
 * (usa la convenzione «sotto 70 = 2000+»), quindi per EX-OS sarebbe
 * inutile; ma un BIOS che invece lo usa, e che lo trovasse fermo al 20
 * mentre le due cifre dicono 26, ripartirebbe da un anno sbagliato. Si
 * aggiorna quindi il registro 0x32 — ma SOLO se quel che c'e' dentro
 * adesso somiglia gia' a un secolo (19 o 20). Su una macchina dove 0x32
 * serve ad altro, quel controllo lo lascia in pace.
 * ============================================================================= */

#define CMOS_SECOLO     0x32    /* dove c'e': vedi la trappola 3 */
#define STATO_B_SET     0x80    /* 1 = conteggio fermo, si puo' scrivere */

static void cmos_scrivi(uint8_t reg, uint8_t val)
{
    port_outb(CMOS_INDICE, reg & 0x7F);   /* bit 7 = NMI: vedi cmos_leggi */
    port_outb(CMOS_DATO, val);
}

static uint8_t a_bcd(uint32_t v)
{
    return (uint8_t)(((v / 10) << 4) | (v % 10));
}

/* Quanti giorni ha il mese. Serve a rifiutare il 31 di febbraio PRIMA di
 * scriverlo: il chip lo accetterebbe, e poi rtc_read troverebbe una data
 * che il suo stesso controllo di validita' lascia passare (il giorno e'
 * <= 31) ma che non esiste. */
static uint32_t giorni_del_mese(uint32_t mese, uint32_t anno)
{
    static const uint32_t g[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };

    if (mese < 1 || mese > 12) return 0;
    if (mese == 2 && ((anno % 4 == 0 && anno % 100 != 0) || anno % 400 == 0))
        return 29;
    return g[mese - 1];
}

int rtc_write(const RtcTime *t)
{
    uint8_t  statoB, secolo_vecchio, sec_bcd;
    uint32_t attesa;
    uint32_t ore;
    int      interrupt_erano_attivi;

    if (t == NULL) return -1;

    /* Validazione PRIMA di toccare il chip. Vedi giorni_del_mese. */
    if (t->anno < 1980 || t->anno > 2099) return -1;
    if (t->mese < 1 || t->mese > 12) return -1;
    if (t->giorno < 1 || t->giorno > giorni_del_mese(t->mese, t->anno)) return -1;
    if (t->ora > 23 || t->minuto > 59 || t->secondo > 59) return -1;

    /* ! IL LIMITE E' 2099 E NON 2199 COME IN LETTURA, e non e' una svista:
     * il chip tiene due cifre e rtc_read le rilegge con la convenzione
     * «sotto 70 = 2000+». Scrivere il 2150 vorrebbe dire mettere 50 nel
     * registro e rileggere 2050 — cioe' accettare in silenzio una data
     * diversa da quella chiesta. Meglio dire di no. */

    statoB = cmos_leggi(CMOS_STATO_B);

    /* Aspetta la fine dell'aggiornamento in corso, come in lettura: alzare
     * SET a meta' di un aggiornamento lo lascia a meta'. */
    for (attesa = 0; aggiornamento_in_corso(); attesa++) {
        if (attesa > RTC_ATTESA_MAX) return -1;   /* orologio assente o rotto */
    }

    /* ! `interrupts_disable()` QUI E' cli GREZZO, senza contatore: vedi il
     * commento sulle varianti _locked in kernel/include/pipe.h. Si guarda
     * il flag IF prima, e si riaccende solo se era acceso — una syscall
     * gira con gli interrupt attivi, ma questo codice deve poter essere
     * chiamato anche da dove non lo sono. */
    interrupt_erano_attivi = (read_eflags() & 0x200) != 0;
    interrupts_disable();

    cmos_scrivi(CMOS_STATO_B, (uint8_t)(statoB | STATO_B_SET));

    ore = t->ora;
    if (!(statoB & STATO_B_24H)) {
        /* Formato 12 ore: mezzanotte e mezzogiorno sono i due casi che si
         * sbagliano sempre. 0 diventa 12 AM, 12 resta 12 PM. */
        uint8_t pomeriggio = (ore >= 12) ? 0x80 : 0x00;

        ore = ore % 12;
        if (ore == 0) ore = 12;
        if (statoB & STATO_B_BINARIO) ore = (uint32_t)((uint8_t)ore | pomeriggio);
        else                          ore = (uint32_t)(a_bcd(ore) | pomeriggio);
    } else if (!(statoB & STATO_B_BINARIO)) {
        ore = a_bcd(ore);
    }

    if (statoB & STATO_B_BINARIO) {
        cmos_scrivi(CMOS_SECONDI, (uint8_t)t->secondo);
        cmos_scrivi(CMOS_MINUTI,  (uint8_t)t->minuto);
        cmos_scrivi(CMOS_ORE,     (uint8_t)ore);
        cmos_scrivi(CMOS_GIORNO,  (uint8_t)t->giorno);
        cmos_scrivi(CMOS_MESE,    (uint8_t)t->mese);
        cmos_scrivi(CMOS_ANNO,    (uint8_t)(t->anno % 100u));
    } else {
        cmos_scrivi(CMOS_SECONDI, a_bcd(t->secondo));
        cmos_scrivi(CMOS_MINUTI,  a_bcd(t->minuto));
        cmos_scrivi(CMOS_ORE,     (uint8_t)ore);
        cmos_scrivi(CMOS_GIORNO,  a_bcd(t->giorno));
        cmos_scrivi(CMOS_MESE,    a_bcd(t->mese));
        cmos_scrivi(CMOS_ANNO,    a_bcd(t->anno % 100u));
    }

    /* Il secolo, solo dove c'e' gia'. Vedi il commento in testa. */
    secolo_vecchio = cmos_leggi(CMOS_SECOLO);
    sec_bcd        = a_bcd(t->anno / 100u);
    if (statoB & STATO_B_BINARIO) {
        if (secolo_vecchio == 19 || secolo_vecchio == 20)
            cmos_scrivi(CMOS_SECOLO, (uint8_t)(t->anno / 100u));
    } else {
        if (secolo_vecchio == 0x19 || secolo_vecchio == 0x20)
            cmos_scrivi(CMOS_SECOLO, sec_bcd);
    }

    /* SET giu': il chip riprende a contare dal valore appena messo. */
    cmos_scrivi(CMOS_STATO_B, (uint8_t)(statoB & (uint8_t)~STATO_B_SET));

    if (interrupt_erano_attivi) interrupts_enable();

    return 0;
}
