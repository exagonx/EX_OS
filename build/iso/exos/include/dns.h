/* =============================================================================
 * lib/include/dns.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Risoluzione dei nomi: da "www.esempio.it" a quattro byte.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ PERCHE' UN MODULO DA COMPILARE DENTRO I PROGRAMMI, E NON UN SERVIZIO
 *
 * DNS è un protocollo APPLICATIVO: sta sopra UDP esattamente come DHCP.
 * Metterlo dentro /dev/ip.drv vorrebbe dire che lo stack — il processo che
 * deve restare acceso — contiene anche l'analisi di risposte scritte da
 * un server di cui non sappiamo niente, con la compressione dei nomi e i
 * puntatori che possono puntare all'indietro. Un errore lì spegne la rete;
 * qui dentro fa fallire un comando.
 *
 * E non è nemmeno un servizio a sé, perché non ha stato da conservare fra
 * una richiesta e l'altra: una cache ci starebbe, ma ci starebbe anche
 * senza un processo dedicato, e un processo in più va avviato, sorvegliato
 * e riavviato. Quando servirà una cache condivisa si farà un servizio; per
 * ora è una funzione che chiunque può chiamare.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ NON E' gethostbyname() E NON DEVE SEMBRARLO
 *
 * Non c'è nessuna struttura statica riusata fra le chiamate, nessun
 * h_errno, nessuna lista di indirizzi. Si passa il nome e un buffer di
 * quattro byte, e si riceve un codice. Le vecchie API di risoluzione sono
 * scomode proprio perché restituiscono memoria che appartiene alla
 * libreria, e questo è il momento buono per non ereditarne i difetti.
 * ============================================================================= */

#ifndef DNS_H
#define DNS_H

/* Risolve `nome` in un indirizzo IPv4.
 *
 * Se `nome` è già un indirizzo in cifre ("10.0.2.2") lo converte e basta,
 * senza chiedere niente a nessuno: è il caso in cui una risposta esiste
 * già, e andarla a chiedere sarebbe solo un modo di poter fallire.
 *
 * Ritorna 0 e riempie `ip[4]`, oppure un valore negativo:
 *
 *   -ENOENT      il nome non esiste (il server ha detto proprio così)
 *   -ETIMEDOUT   il server non ha risposto
 *   -ENODEV      lo stack IP non è attivo
 *   -ENETDOWN    nessun server DNS configurato (fallo dare al DHCP)
 *   -EIO         risposta malformata o errore del server
 *
 * ⚠️ È SINCRONA E PUÒ BLOCCARE PER QUALCHE SECONDO. Un programma che deve
 * restare reattivo non la chiama nel proprio ciclo principale. */
int dns_risolvi(const char *nome, unsigned char *ip);

/* Come sopra, ma dice anche quale server ha risposto. `da` può essere
 * NULL. Serve a chi deve stampare da dove è arrivata la risposta —
 * cioè agli strumenti di diagnosi, non ai programmi normali. */
int dns_risolvi_da(const char *nome, unsigned char *ip, unsigned char *da);

/* Converte "10.0.2.2" in quattro byte. Ritorna 1 se la stringa è un
 * indirizzo IPv4 ben formato, 0 altrimenti.
 *
 * Sta qui e non in ogni programma perché finora ogni comando di rete se
 * n'era scritta una copia: cinque copie della stessa funzione sono cinque
 * occasioni di accettare "999.1.2.3" in modo diverso. */
int ip_da_stringa(const char *s, unsigned char *out);

#endif /* DNS_H */
