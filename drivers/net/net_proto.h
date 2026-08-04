/* =============================================================================
 * drivers/net/net_proto.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Protocollo IPC dei driver di rete.
 *
 * Non è il protocollo della NE2000: è quello di QUALUNQUE scheda. Il
 * driver PCnet che verrà dopo parlerà esattamente questa lingua, così lo
 * stack TCP/IP non dovrà sapere che scheda ha sotto — che è poi l'unico
 * motivo per cui esiste un livello di driver.
 *
 * Nessuna dipendenza: solo #define e struct. Lo includono il driver e i
 * suoi client.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ IL DRIVER NON SPINGE MAI UN FRAME NON RICHIESTO
 *
 * La forma ovvia sarebbe: il client si iscrive, il driver gli manda i
 * frame appena arrivano. È sbagliata in questo sistema, e il modo in cui
 * è sbagliata è un blocco a due, non un rallentamento.
 *
 * `ipc_send` BLOCCA il mittente se la mailbox del destinatario è piena
 * (mailbox profonda 4). Se il driver spinge frame verso uno stack che
 * intanto sta mandando al driver un pacchetto da trasmettere, si arriva
 * facilmente allo stato in cui ognuno dei due è fermo dentro `ipc_send`
 * ad aspettare che l'altro svuoti: nessuno dei due tornerà mai a
 * chiamare `ipc_recv`. Il sistema non va in panic, la rete smette e
 * basta.
 *
 * Quindi: domanda e risposta, come fa il driver di tastiera con
 * KBD_MSG_READLINE. Il client chiede NET_MSG_RICEVI e resta in
 * `ipc_recv`; il driver risponde solo a chi sta già aspettando. Se al
 * momento della richiesta non c'è niente, il driver se ne ricorda e
 * risponde quando arriva — non fa attendere il chiamante a vuoto e non
 * spinge mai di sua iniziativa.
 * ============================================================================= */

#ifndef NET_PROTO_H
#define NET_PROTO_H

/* Nome del servizio. Il numero è l'indice dell'interfaccia: una seconda
 * scheda si registrerà come "rete1". */
#define NET_SERVIZIO_0   "rete0"

/* Frame Ethernet massimo senza FCS: 14 di intestazione + 1500 di payload.
 * Il messaggio IPC arriva a 1536 apposta per contenerlo intero — vedi
 * kernel/include/sched.h. */
#define NET_FRAME_MAX    1514
#define NET_MTU          1500

/* Frame Ethernet MINIMO. Sotto i 60 byte (FCS escluso) il frame non è
 * valido e uno switch lo scarta: chi trasmette meno deve riempire di
 * zeri fino a qui. Lo fa il driver, perché è un vincolo del mezzo e non
 * di chi scrive il pacchetto. */
#define NET_FRAME_MIN    60

/* --- Richieste (client -> driver) --------------------------------------- */
#define NET_MSG_INFO       1   /* -> NET_MSG_STATO */
#define NET_MSG_INVIA      2   /* payload = frame grezzo -> NET_MSG_ESITO */
#define NET_MSG_RICEVI     3   /* -> NET_MSG_FRAME quando ce n'è uno */
#define NET_MSG_CONTATORI  4   /* -> NET_MSG_CONTEGGI */
#define NET_MSG_ANNULLA    5   /* ritira una NET_MSG_RICEVI pendente */

/* --- Risposte (driver -> client) ---------------------------------------- */
#define NET_MSG_STATO    128
#define NET_MSG_FRAME    129   /* payload = frame grezzo ricevuto */
#define NET_MSG_ESITO    130
#define NET_MSG_CONTEGGI 131

/* Stato dell'interfaccia */
typedef struct {
    unsigned char  mac[6];
    unsigned short mtu;
    unsigned int   porta_base;
    unsigned int   irq;
    unsigned int   bus;          /* 0xFFFFFFFF se la scheda non è su PCI */
    unsigned int   slot;
    char           modello[32];
} NetStato;

/* Contatori. Servono a distinguere «la rete non funziona» da «la rete
 * funziona e nessuno risponde», che sono due guasti diversi e vengono
 * confusi ogni volta che non si può guardare. */
typedef struct {
    unsigned int inviati;
    unsigned int ricevuti;
    unsigned int errori_tx;
    unsigned int errori_rx;
    unsigned int troppo_grandi;  /* frame più lunghi di NET_FRAME_MAX */
    unsigned int persi_coda;     /* arrivati con la coda interna piena */
    unsigned int overflow;       /* l'anello della scheda è traboccato */

    /* ⚠️ QUESTI DUE SEPARANO «funziona» DA «funziona per il motivo
     * giusto». Il driver guarda la scheda quando arriva un interrupt,
     * ma anche a ogni battito e dopo ogni richiesta di un client: se
     * l'interrupt non arrivasse mai, la rete continuerebbe a funzionare
     * — solo con centinaia di millisecondi di ritardo, e nessuno
     * saprebbe dire perché. Con questi due numeri la domanda ha una
     * risposta invece che un'ipotesi. */
    unsigned int notifiche_irq;  /* interrupt consegnati dal kernel */
    unsigned int battiti;        /* risvegli per scadenza, senza messaggi */
} NetContatori;

/* Risposta a NET_MSG_INVIA e NET_MSG_ANNULLA: 0 = fatto, <0 = -errno */
typedef struct {
    int codice;
} NetEsito;

#endif /* NET_PROTO_H */
