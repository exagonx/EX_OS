/* =============================================================================
 * kernel/include/ipc.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * IPC — comunicazione kernel-mediata tra task ring3.
 *
 * Nessun task accede mai alla memoria di un altro: ogni messaggio passa
 * per una copia kernel->kernel (mailbox del mittente in ring3 -> buffer
 * kernel -> mailbox del destinatario in ring3). Un driver che crasha o
 * invia dati corrotti può al massimo danneggiare se stesso o i client
 * che scelgono di fidarsi del contenuto applicativo del messaggio — mai
 * la memoria del kernel o di processi terzi.
 *
 * Modello: send bloccante se la mailbox del destinatario è piena, recv
 * bloccante se la propria mailbox è vuota. Un registro nomi separato
 * (IPC_REGISTER/IPC_LOOKUP) permette ai client di trovare un servizio
 * per nome ("tty", "floppy", "kbd") senza conoscere il PID del driver,
 * che è assegnato dinamicamente all'avvio.
 * ============================================================================= */

#ifndef IPC_H
#define IPC_H

#include "kernel.h"
#include "sched.h"   /* IpcMessage, Process */

/* =============================================================================
 * API kernel-side, usata dai syscall handler (syscall_impl.c)
 * ============================================================================= */

/* Consegna un messaggio a dest_pid. Se la mailbox del destinatario è
 * piena, blocca il chiamante finché non si libera spazio (o il
 * destinatario termina, nel qual caso ritorna errore).
 * Ritorna 0 su successo, <0 su errore (ESRCH se dest_pid non esiste). */
int32_t ipc_send(uint32_t dest_pid, uint32_t tipo,
                  const void *data, uint32_t len);

/* Riceve il prossimo messaggio nella mailbox del chiamante. Blocca se
 * vuota. Scrive mittente/tipo/lunghezza in *out e copia i dati in buf
 * (fino a buf_len byte; il messaggio viene troncato se più grande —
 * len nel messaggio riporta comunque la dimensione originale).
 * Ritorna 0 su successo. */
int32_t ipc_recv(IpcMessage *out, void *buf, uint32_t buf_len);

/* Come ipc_recv, ma rinuncia dopo timeout_ms e ritorna -ETIMEDOUT.
 * timeout_ms == 0 = attesa senza scadenza (identica a ipc_recv). */
int32_t ipc_recv_timeout(IpcMessage *out, void *buf, uint32_t buf_len,
                         uint32_t timeout_ms);

/* Registra il processo chiamante come fornitore del servizio 'name'.
 * Un solo processo alla volta può registrare un dato nome. */
int32_t ipc_register(const char *name);

/* Cerca il PID del processo che ha registrato 'name'.
 * Ritorna il PID (>0) su successo, <0 se non trovato. */
int32_t ipc_lookup(const char *name);

/* Rimuove ogni traccia IPC di un processo che termina (deregistra il
 * nome se presente, sblocca eventuali mittenti in attesa verso di lui
 * segnalando errore). Chiamata da proc_reap_zombie(). */
void    ipc_cleanup_process(uint32_t pid);

/* Consegna una notifica di IRQ hardware alla mailbox di dest_pid.
 * Interrupt-safe: chiamabile SOLO da dentro il dispatcher IRQ (isr.c),
 * mai da codice ring3 o da un contesto con IF=1. Non bloccante: se la
 * mailbox è piena la notifica viene scartata silenziosamente. */
int     ipc_notify_irq(uint32_t dest_pid, uint8_t irq_num);

/* =============================================================================
 * IPC_SENZA_ATTESA — un bit nel `tipo` di ipc_send (28 settembre 2026)
 *
 * Su una casella piena ipc_send RIPROVA ogni 5 ms fino a dieci secondi, e
 * per un driver temporaneamente indietro e' la cosa giusta. Per il server
 * grafico no: col mouse mosso in fretta mandava a Pennello un movimento per
 * pacchetto, la casella di Pennello (4 posti) si riempiva, e il SERVER restava
 * fermo dentro ipc_send — puntatore immobile, schermo congelato — mentre
 * Pennello a sua volta aspettava posto nella casella del server. Il commento
 * di wserver diceva «se il client non raccoglie, non si insiste»: era vero
 * nell'intenzione e falso nel kernel.
 *
 * Con questo bit acceso nel tipo, una casella piena rende subito -EBUSY; il
 * bit si toglie prima di consegnare, quindi chi riceve vede il tipo vero. Si
 * usa per cio' che, perso, e' rimpiazzato dal prossimo (un movimento), mai
 * per cio' che non si ripete (un clic).
 *
 * ! DUPLICATO A MANO in drivers/wserver/wserver.c, come le altre costanti
 * che attraversano la syscall.
 * ============================================================================= */
#define IPC_SENZA_ATTESA  0x80000000u

#endif /* IPC_H */
