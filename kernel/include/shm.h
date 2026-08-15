/* =============================================================================
 * kernel/include/shm.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * MEMORIA CONDIVISA FRA PROCESSI — le stesse pagine fisiche in due spazi
 *
 * Gradino 0 punto 2 di DIREZIONE.md. Serve al server grafico e non solo a
 * lui: fino ad ora due processi potevano scambiarsi dati soltanto con i
 * messaggi IPC, che sono da 1536 byte. Una finestra 640x480x32 sono 1,2 MB,
 * cioe' circa 800 messaggi PER FOTOGRAMMA — non e' lentezza, e' la struttura
 * sbagliata.
 *
 * -----------------------------------------------------------------------------
 * SI APRONO PER NOME, come i servizi IPC
 *
 *     shm_apri("schermo", 1228800, SHM_CREA)   il primo la crea e la apre
 *     shm_apri("schermo", 0,       0)          il secondo si attacca a quella
 *
 * Il nome e' l'unica cosa che i due processi devono essersi detti, ed e' la
 * stessa convenzione che gia' regge ipc_register()/ipc_lookup(). Un
 * identificatore numerico avrebbe richiesto un modo di passarselo, cioe'
 * l'IPC, cioe' la cosa che si sta cercando di non usare.
 *
 * ! CHI SI ATTACCA NON SCEGLIE LA DIMENSIONE, LA RICEVE. `byte` in ingresso
 * conta solo per chi crea; agli altri viene reso quanto la zona misura
 * davvero. Chi si attacca chiedendo meno e crede di aver ottenuto meno
 * scriverebbe dentro i limiti che ha chiesto e leggerebbe fuori da quelli
 * dell'altro: due idee diverse della stessa memoria sono il difetto che
 * questa interfaccia deve rendere impossibile.
 *
 * -----------------------------------------------------------------------------
 * ! LA ZONA VIVE FINCHE' QUALCUNO LA TIENE APERTA, e non un istante di piu'
 *
 * Non c'e' un shm_unlink come su POSIX, e non e' una semplificazione: una
 * zona che sopravvive a tutti i suoi utenti e' memoria che nessuno liberera'
 * mai piu', su un sistema dove un processo che muore non lascia niente
 * dietro. Quando l'ultimo processo la chiude — o muore — le pagine tornano al
 * PMM e il nome torna libero.
 *
 * Conseguenza da sapere: NON si puo' creare una zona, riempirla e uscire
 * perche' qualcun altro la trovi dopo. Il creatore deve restare vivo fino a
 * quando il secondo si e' attaccato.
 *
 * -----------------------------------------------------------------------------
 * ! IL CONTEGGIO DEI RIFERIMENTI NON STA QUI. Sta nel PMM, per pagina fisica
 * (kernel/include/pmm.h). Questo file conta i PROCESSI che tengono aperta una
 * zona, che e' un'altra cosa e serve a sapere quando il nome muore. Le pagine
 * le protegge il PMM, perche' la strada che ne libera di piu' —
 * paging_destroy_directory(), quando un processo muore — non passa di qui e
 * non deve doverlo sapere.
 *
 * -----------------------------------------------------------------------------
 * ! IL BLOCCO TEMPORANEO PER IL PAGEFILE NON C'E' ANCORA, e la direttiva 3 di
 * DIREZIONE.md chiede di pensarci adesso. Ci si e' pensato: cio' che non si
 * puo' aggiungere dopo e' SAPERE CHE UNA PAGINA HA PIU' DI UN PROPRIETARIO, e
 * quello e' fatto. Un contatore di blocco che nessuno consulta sarebbe codice
 * morto oggi e non renderebbe piu' facile lo swap domani: quando ci sara' uno
 * swapper si aggiunge un campo a ShmZonaK e le due chiamate che lo muovono,
 * senza toccare niente di cio' che c'e' qui.
 * ============================================================================= */

#ifndef SHM_H
#define SHM_H

#include "kernel.h"
#include "sched.h"

#define SHM_NOME_LEN     16     /* come IPC_NAME_LEN: e' lo stesso genere di nome */
#define SHM_MAX_ZONE      8     /* zone vive nel sistema */
#define SHM_PAGINE_MAX  512     /* 2 MB per zona: una finestra 640x480x32 ne usa 300 */

/* Flag di shm_apri */
#define SHM_CREA     0x0001     /* creala se non c'e' (senza: solo attacco) */

/* Zona vista dal kernel. Quella vista dai processi sta in libc.h. */
typedef struct {
    char      nome[SHM_NOME_LEN];
    uint32_t  pagine;
    uint32_t *fisico;       /* elenco delle pagine: NON sono contigue, e non
                             * devono esserlo — nessun dispositivo le legge,
                             * quindi pretendere contiguita' costerebbe
                             * frammentazione in cambio di niente */
    uint32_t  proc;         /* quanti processi la tengono aperta. 0 = slot libero */
} ShmZonaK;

/* Apre o crea. Rende 0 e riempie *out_virt / *out_byte, oppure un -errno.
 * `byte` conta solo quando la zona viene creata. */
int32_t shm_apri(Process *proc, const char *nome, uint32_t byte, uint32_t flag,
                 uint32_t *out_virt, uint32_t *out_byte);

/* Chiude la zona mappata a `virt` nel processo. Rende 0 o -errno. */
int32_t shm_chiudi(Process *proc, uint32_t virt);

/* Chiamata da proc_reap_zombie(): chiude tutto cio' che il processo teneva
 * aperto. Le PAGINE le ha gia' rilasciate paging_destroy_directory passando
 * dal PMM; qui si cala il conteggio dei processi e, se era l'ultimo, muore
 * il nome. */
void shm_cleanup_process(Process *proc);

/* Per la diagnostica: quante zone vive e quante pagine in tutto. */
void shm_stato(uint32_t *out_zone, uint32_t *out_pagine);

#endif /* SHM_H */
