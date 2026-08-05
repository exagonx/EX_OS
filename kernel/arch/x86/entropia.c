/* =============================================================================
 * kernel/arch/x86/entropia.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Raccolta di ENTROPIA, cioe' di byte che nessuno puo' prevedere.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ QUESTO FILE NON E' UN GENERATORE DI NUMERI CASUALI, E NON DEVE
 * DIVENTARLO
 *
 * La divisione dei compiti e' deliberata e vale la pena dirla per intero,
 * perche' e' la stessa ragione per cui in EX-OS non c'e' una `sqrt`
 * scritta in casa:
 *
 *     una `sqrt` quasi giusta e' peggio di nessuna `sqrt`, perche'
 *     sbaglia in silenzio.
 *
 * Un generatore crittografico quasi giusto e' molto peggio: non sbaglia
 * nemmeno in silenzio — sembra funzionare benissimo. I byte escono, sono
 * diversi ogni volta, passano qualunque occhiata. E la chiave che ne esce
 * si indovina.
 *
 * Quindi il kernel fa la sola cosa che il kernel puo' fare e nessun altro
 * puo' fare al posto suo: RACCOGLIE eventi imprevedibili. L'espansione
 * crittografica — trasformare un pugno di byte imprevedibili in un flusso
 * infinito — la fa chi ha un DRBG vero e rivisto da qualcuno, cioe'
 * OpenSSL. Noi gli passiamo il seme, non il flusso.
 *
 * -----------------------------------------------------------------------------
 * DA DOVE VIENE L'IMPREVEDIBILITA'
 *
 *   1. RDRAND, se la CPU ce l'ha. E' una sorgente hardware vera, e dove
 *      c'e' e' la migliore. Non c'e' su nulla di piu' vecchio di Ivy
 *      Bridge, cioe' su quasi tutto l'hardware che questo sistema punta a
 *      far girare.
 *
 *   2. L'ISTANTE ESATTO IN CUI ARRIVANO GLI INTERRUPT. Fra due pressioni
 *      di un tasto passano milioni di cicli, e le ultime cifre di quel
 *      numero dipendono da quando esattamente un dito ha toccato la
 *      plastica. Lo stesso vale, in misura minore, per il disco e per la
 *      rete.
 *
 * ⚠️ IL TIMER NON CONTA, e va detto perche' e' la sorgente piu'
 * abbondante e sarebbe la piu' comoda: IRQ0 arriva ogni 10 ms per
 * costruzione. Contare quegli eventi come entropia vorrebbe dire
 * dichiarare imprevedibile un orologio.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ SI CONTA QUANTA SE NE E' RACCOLTA, E SI RIFIUTA DI DARNE SENZA
 *
 * E' la parte che rende questo file onesto. Un `getentropy()` che
 * risponde comunque, con quello che ha, e' esattamente il difetto che
 * genera chiavi indovinabili: il programma che chiama non ha modo di
 * accorgersene, perche' i byte ci sono e sembrano buoni.
 *
 * Qui si tiene una stima CONSERVATIVA — un bit per evento, e non di piu'
 * — e finche' non se ne sono accumulati abbastanza la syscall risponde
 * -EAGAIN. Meglio un programma che dice «non ho abbastanza casualita',
 * muovi il mouse» di uno che genera una chiave debole senza dirlo.
 * ============================================================================= */

#include "kernel.h"
#include "entropia.h"
#include "fpu.h"

/* =============================================================================
 * Il serbatoio
 *
 * 32 byte: e' la dimensione di un seme a 256 bit, cioe' quanto serve a un
 * DRBG serio. Tenerne di piu' non aggiunge sicurezza, tenerne meno vuol
 * dire non poter servire una richiesta intera.
 * ============================================================================= */
#define POOL_BYTE       32
#define POOL_BIT        (POOL_BYTE * 8)

/* Quanti bit servono prima di dare qualcosa. 128 e' il minimo sotto cui
 * una chiave non ha piu' senso; si potrebbe pretendere 256, ma su una
 * macchina appena accesa e senza tastiera vorrebbe dire non partire mai. */
#define BIT_MINIMI      128

static uint8_t  g_pool[POOL_BYTE];
static uint32_t g_scritto = 0;      /* dove assorbire il prossimo byte */
static uint32_t g_bit = 0;          /* stima conservativa, in bit */
static uint32_t g_eventi = 0;       /* quanti ne sono arrivati in tutto */
static uint32_t g_prec = 0;         /* istante dell'evento precedente */
static uint8_t  g_rdrand = 0;

/* =============================================================================
 * Il tempo, con la risoluzione piu' fine che questa macchina abbia
 *
 * ⚠️ NON SI PUO' USARE SOLO RDTSC: sul 486 non esiste, e questo kernel
 * deve partire anche li'. Il ripiego e' il contatore del PIT, che si
 * legge dalla porta 0x40 e scende a 1,19 MHz: molto meno fine del TSC ma
 * pur sempre una misura piu' precisa del tick da 10 ms.
 * ============================================================================= */
static uint32_t istante(void)
{
    const CpuCapacita *c = cpu_capacita();
    uint32_t basso;

    if (c->cpuid) {
        /* RDTSC esiste da tutti i Pentium in poi; CPUID e' un buon
         * indicatore, e comunque i 486 che hanno CPUID hanno anche il
         * TSC nei modelli tardi. Il caso dubbio costa un'istruzione
         * illegale, quindi si resta prudenti e si usa il PIT quando
         * CPUID manca. */
        uint32_t alto;

        __asm__ volatile("rdtsc" : "=a"(basso), "=d"(alto));
        return basso;
    }

    /* PIT canale 0: comando di latch, poi i due byte. */
    port_outb(0x43, 0x00);
    basso  = port_inb(0x40);
    basso |= (uint32_t)port_inb(0x40) << 8;
    return basso;
}

static int rdrand32(uint32_t *out)
{
    uint32_t v = 0, ok = 0;
    int      giri;

    if (!g_rdrand) return 0;

    /* ⚠️ RDRAND PUO' FALLIRE, e il modo di fallire e' mettere CF a zero
     * lasciando il registro invariato. Chi non guarda il flag ottiene lo
     * stesso valore ogni volta e non se ne accorge: e' il difetto piu'
     * documentato di questa istruzione. Dieci tentativi e poi si
     * rinuncia, come raccomanda AMD. */
    for (giri = 0; giri < 10; giri++) {
        __asm__ volatile("rdrand %0; setc %b1" : "=r"(v), "=q"(ok) : : "cc");
        if (ok & 1) { *out = v; return 1; }
    }
    return 0;
}

/* =============================================================================
 * Assorbimento
 *
 * ⚠️ SI MESCOLA, NON SI SOVRASCRIVE. Scrivere il valore nuovo sopra il
 * vecchio butterebbe via l'imprevedibilita' gia' raccolta ogni volta che
 * il serbatoio si riempie: dopo trentadue eventi resterebbero solo gli
 * ultimi trentadue. Con lo XOR e la rotazione ogni evento contribuisce
 * per sempre.
 *
 * ⚠️ QUESTO NON E' UN HASH E NON PRETENDE DI ESSERLO. Non ha nessuna
 * delle proprieta' che si chiedono a SHA-256: serve solo a non perdere
 * quello che entra e a spargerlo su tutto il serbatoio. La robustezza
 * crittografica la mette il DRBG che riceve questi byte — vedi il
 * commento di testa.
 * ============================================================================= */
static void assorbi(uint32_t v)
{
    int i;

    for (i = 0; i < 4; i++) {
        uint32_t p = g_scritto % POOL_BYTE;

        g_pool[p] = (uint8_t)(((g_pool[p] << 1) | (g_pool[p] >> 7))
                              ^ (uint8_t)(v >> (i * 8)));
        g_scritto++;
    }
}

void entropia_init(void)
{
    const CpuCapacita *c = cpu_capacita();
    uint32_t v;
    int i;

    for (i = 0; i < POOL_BYTE; i++) g_pool[i] = 0;
    g_scritto = 0;
    g_bit     = 0;
    g_eventi  = 0;

    /* CPUID.1:ECX bit 30 = RDRAND. Senza CPUID non c'e' di sicuro. */
    g_rdrand = 0;
    if (c->cpuid) {
        extern uint32_t cpuid_ecx1(void);
        extern uint32_t cpuid_max(void);

        if (cpuid_max() >= 1u && (cpuid_ecx1() & (1u << 30))) g_rdrand = 1;
    }

    if (g_rdrand) {
        /* Otto parole da RDRAND riempiono il serbatoio di sorgente vera:
         * il sistema e' pronto subito, senza aspettare che qualcuno
         * tocchi la tastiera. */
        int prese = 0;

        for (i = 0; i < POOL_BYTE / 4; i++)
            if (rdrand32(&v)) { assorbi(v); prese++; }

        /* 32 bit di stima per parola: qui la fonte e' hardware, non una
         * misura di tempo, e contarla come il resto sarebbe inutilmente
         * pessimista. */
        g_bit = (uint32_t)prese * 32u;
        if (g_bit > POOL_BIT) g_bit = POOL_BIT;
    }

    klog(LOG_INFO, "Entropia: RDRAND %s, %u bit all'avvio",
         g_rdrand ? "presente" : "assente", g_bit);
}

/* =============================================================================
 * entropia_evento — chiamata dagli interrupt che valgono
 *
 * ⚠️ DEVE COSTARE POCHISSIMO: gira in contesto interrupt, prima
 * dell'handler vero. Una lettura del tempo, quattro XOR, due
 * incrementi — nient'altro, e soprattutto nessun klog.
 * ============================================================================= */
void entropia_evento(uint8_t irq)
{
    uint32_t t, d;

    /* ⚠️ IRQ0 (timer) NON CONTA: arriva ogni 10 ms per costruzione, e
     * dichiararlo imprevedibile sarebbe dichiarare imprevedibile un
     * orologio. Vedi il commento di testa. */
    if (irq == 0) return;

    t = istante();
    d = t - g_prec;
    g_prec = t;

    assorbi(t);
    assorbi(d);
    g_eventi++;

    /* Un bit per evento, e non di piu'. La stima vera sarebbe piu' alta
     * per la tastiera e piu' bassa per il disco; contarne uno solo e'
     * il modo di sbagliare dalla parte giusta. */
    if (g_bit < POOL_BIT) g_bit++;
}

/* =============================================================================
 * entropia_preleva — l'unica uscita
 *
 * Ritorna quanti byte ha scritto, oppure -EAGAIN se non ne ha abbastanza.
 *
 * ⚠️ QUELLO CHE ESCE VIENE TOLTO DAL SERBATOIO. La stima cala di quanto
 * si e' consegnato: due chiamate di fila non possono ottenere entrambe
 * dell'entropia che c'era una volta sola. Senza questo, un programma in
 * ciclo svuoterebbe la casa di casualita' continuando a ricevere byte
 * che sembrano nuovi.
 * ============================================================================= */
int entropia_preleva(uint8_t *dst, uint32_t n)
{
    uint32_t i;

    if (n == 0) return 0;
    if (n > POOL_BYTE) n = POOL_BYTE;

    /* Se c'e' RDRAND si serve direttamente da li': e' hardware, non si
     * consuma, e non c'e' motivo di attingere al serbatoio. */
    if (g_rdrand) {
        uint32_t v;

        for (i = 0; i < n; i += 4) {
            uint32_t k, resto = n - i;

            if (!rdrand32(&v)) break;          /* fallita: si ripiega sotto */
            if (resto > 4) resto = 4;
            for (k = 0; k < resto; k++) dst[i + k] = (uint8_t)(v >> (k * 8));
        }
        if (i >= n) return (int)n;
    }

    if (g_bit < BIT_MINIMI) return -11;        /* -EAGAIN */

    for (i = 0; i < n; i++) {
        uint32_t p = (g_scritto + i) % POOL_BYTE;

        dst[i] = g_pool[p];
        /* Il byte consegnato non deve restare uguale nel serbatoio: chi
         * lo ha letto lo conosce. */
        g_pool[p] = (uint8_t)(g_pool[p] ^ (uint8_t)istante());
    }

    g_bit = (g_bit > n * 8) ? (g_bit - n * 8) : 0;
    return (int)n;
}

void entropia_stato(uint32_t *bit, uint32_t *eventi, int *rdrand)
{
    if (bit)    *bit    = g_bit;
    if (eventi) *eventi = g_eventi;
    if (rdrand) *rdrand = g_rdrand;
}
