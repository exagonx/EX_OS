/* =============================================================================
 * lib/include/rete.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * «Cosa manca per accendere la rete» — in un posto solo.
 *
 * -----------------------------------------------------------------------------
 * PERCHE' ESISTE QUESTO FILE
 *
 * La rete di EX-OS è una CATENA di quattro pezzi, e ognuno serve al
 * successivo:
 *
 *     bus PCI          /dev/pci.drv &     enumera l'hardware
 *     scheda di rete   netdetect -c       trova la scheda e avvia il driver
 *     stack IP         /dev/ip.drv &      ARP, IPv4, ICMP, UDP
 *     indirizzo        dhcp               oppure ipcfg -a ...
 *
 * Ogni comando di rete ne aveva scritte nel proprio codice una parte, e
 * ognuno diceva le sue: `ping` elencava tre passi, `nettest` due, `host`
 * uno solo. Chi si fermava al secondo passo leggeva istruzioni diverse a
 * seconda del comando con cui ci aveva provato — e nessuna delle tre
 * diceva a che punto fosse arrivato.
 *
 * Qui la catena si CONTROLLA, non si recita: si guarda quali servizi sono
 * registrati e si stampa lo stato di ciascuno, indicando il primo che
 * manca. La differenza pratica è fra «prova questi tre comandi» e «sei
 * arrivato al terzo, ti manca questo».
 * ============================================================================= */

#ifndef RETE_H
#define RETE_H

/* Anelli della catena, in ordine. Il numero è anche l'indice del passo. */
#define RETE_PASSO_PCI      0
#define RETE_PASSO_SCHEDA   1
#define RETE_PASSO_STACK    2
#define RETE_PASSO_INDIRIZZO 3
#define RETE_PASSI          4

/* Stampa lo stato della catena e il comando del primo passo mancante.
 * Se è tutto pronto lo dice e basta. */
void rete_istruzioni(void);

/* Ritorna il PID del servizio, oppure un valore <= 0 dopo aver stampato
 * il motivo e le istruzioni. È la forma che serve a un comando che non
 * può proseguire senza:
 *
 *     pid = rete_richiedi(IP_SERVIZIO);
 *     if (pid <= 0) return 1;
 *
 * ! STAMPA DA SÉ. Chi la chiama non deve aggiungere un proprio messaggio
 * d'errore: sarebbero due spiegazioni della stessa cosa, e la seconda
 * quasi sempre meno precisa. */
int  rete_richiedi(const char *servizio);

/* Il primo passo non fatto (RETE_PASSO_*), oppure RETE_PASSI se la catena
 * è completa. Non stampa niente: serve a chi vuole decidere da sé. */
int  rete_primo_mancante(void);

/* =============================================================================
 * QUALE DRIVER VUOLE QUESTA SCHEDA — la tabella, in UN posto solo
 *
 * ! STAVA DENTRO bin/netdetect/netdetect.c, e il commento accanto diceva di
 * non copiarla altrove «perche' due elenchi divergono al primo driver nuovo».
 * E' divenuta vera al primo driver nuovo: /dev/e1000.drv e' stato scritto, la
 * riga 8086:100E della tabella e' rimasta a «driver da scrivere», e su QEMU —
 * dove quella scheda e' la predefinita — `netdetect -c` rispondeva che il
 * driver non c'era mentre stava nel CD accanto.
 *
 * Il difetto non era la copia: era che l'elenco stesse dentro un programma. Da
 * qui lo leggono netdetect, che lo stampa e lo usa per caricare, e `hwconfig`,
 * che deve scrivere il driver giusto nella sezione [modules] di kernel.cfg —
 * cioe' decidere all'installazione cio' che prima si decideva a ogni avvio.
 *
 * ! `driver` NULL VUOL DIRE «MODELLO NOTO, DRIVER DA SCRIVERE», ed e' diverso
 * da scheda sconosciuta: dice che il numero e' stato riconosciuto e che manca
 * il codice, non che l'hardware sia un mistero.
 *
 * ! E DICE COSA SERVIREBBE, NON COSA C'E'. Il file del driver puo' non essere
 * su questo supporto — avviando da floppy non c'e' — e chi usa la tabella deve
 * controllarlo prima di prometterlo.
 * ============================================================================= */
typedef struct {
    unsigned short venditore;
    unsigned short dispositivo;
    const char    *modello;
    const char    *driver;      /* NULL = modello noto, driver da scrivere */
} ReteScheda;

/* La scheda con quei numeri, o NULL se non e' in tabella. */
const ReteScheda *rete_riconosci(unsigned short venditore,
                                 unsigned short dispositivo);

/* La i-esima riga della tabella, NULL oltre la fine: serve a stamparla. */
const ReteScheda *rete_scheda(int i);
int               rete_schede_note(void);

#endif /* RETE_H */
