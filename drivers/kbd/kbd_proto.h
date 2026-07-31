/* =============================================================================
 * drivers/kbd/kbd_proto.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Protocollo IPC del servizio tastiera.
 *
 * Questo header è l'UNICO punto di contatto fra le due sponde della
 * migrazione a ring3 ed è incluso da entrambe:
 *   - drivers/kbd/kbd.c   — il driver, processo ring3 (/dev/kbd.drv)
 *   - drivers/tty/tty.c   — il TTY, compilato dentro il kernel, che fa da
 *                            client per conto del processo che chiama read(0)
 *
 * Deve quindi restare privo di dipendenze: niente header del kernel,
 * niente libc, solo #define. Se un giorno anche il TTY diventerà un
 * processo ring3, questo file non cambia.
 *
 * Modello di interazione (una richiesta, una risposta):
 *
 *   client                              kbd (/dev/kbd.drv)
 *     |  KBD_MSG_READLINE (uint32 max)   |
 *     |--------------------------------->|  registra il richiedente
 *     |                                  |  ... attende gli IRQ1 ...
 *     |         KBD_MSG_LINE (riga)      |
 *     |<---------------------------------|  riga completa, '\n' incluso
 *
 * Il driver serve un solo lettore alla volta: la console è una sola.
 * Una richiesta che arriva mentre un'altra è pendente sostituisce la
 * precedente (l'ultimo che chiede vince) — vedi kbd.c.
 * ============================================================================= */

#ifndef KBD_PROTO_H
#define KBD_PROTO_H

/* Nome con cui il driver si registra (ipc_register) e con cui i client
 * lo cercano (ipc_lookup). Max IPC_NAME_LEN-1 = 15 caratteri. */
#define KBD_SERVICE_NAME    "kbd"

/* client -> kbd: "dammi una riga".
 * Payload: uint32_t = numero massimo di byte accettati nella risposta.
 * Un payload assente o più corto di 4 byte equivale a KBD_LINE_MAX. */
#define KBD_MSG_READLINE    1u

/* kbd -> client: riga completa, terminatore '\n' incluso.
 * Payload: i byte della riga. len = lunghezza effettiva (può essere 1,
 * cioè il solo '\n', se l'utente ha premuto Invio su riga vuota). */
#define KBD_MSG_LINE        2u

/* Lunghezza massima di una riga consegnata in un singolo messaggio.
 * Deve restare <= IPC_MSG_MAX_DATA (512, vedi kernel/include/ipc.h e
 * lib/include/libc.h): oltre quel limite il kernel troncherebbe il
 * payload senza che nessuna delle due sponde se ne accorga. */
#define KBD_LINE_MAX        256

#endif /* KBD_PROTO_H */
