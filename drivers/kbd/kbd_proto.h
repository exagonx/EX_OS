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
 *
 * -----------------------------------------------------------------------------
 * MODALITÀ RAW (agosto 2026)
 *
 * Il protocollo qui sopra consegna TESTO, una riga alla volta, e non
 * poteva reggere un programma a schermo intero: un editor deve sapere
 * che è stato premuto Freccia-Su nell'istante in cui accade, non dopo
 * che l'utente ha premuto Invio. /bin/textline lo dice apertamente nel
 * proprio commento di testa — è nato lineare per questa mancanza.
 *
 * La modalità raw affianca il modello a righe senza sostituirlo:
 *
 *   client                              kbd (/dev/kbd.drv)
 *     |  KBD_MSG_SETMODE (1 = raw)       |
 *     |--------------------------------->|  svuota riga e type-ahead
 *     |  KBD_MSG_READKEY                 |
 *     |--------------------------------->|
 *     |         KBD_MSG_KEY (uint32)     |
 *     |<---------------------------------|  un tasto, appena premuto
 *     |            ...                   |
 *     |  KBD_MSG_SETMODE (0 = cooked)    |
 *     |--------------------------------->|  si torna alle righe
 *
 * In raw il driver NON fa eco a video (lo schermo appartiene al
 * programma) e NON interpreta Backspace: consegna gli eventi e basta.
 *
 * RIENTRO AUTOMATICO IN COOKED, che non è un dettaglio: se il programma
 * a schermo intero muore per un fault senza rimettere la modalità a
 * posto, la console resterebbe muta per sempre e l'unica via d'uscita
 * sarebbe il reset. Il driver quindi torna da solo in cooked in due
 * casi — quando la consegna di un tasto fallisce perché il client è
 * sparito, e quando arriva una KBD_MSG_READLINE mentre è in raw (la
 * manda solo il TTY, cioè è la shell che ha ripreso il controllo).
 * ============================================================================= */

#ifndef KBD_PROTO_H
#define KBD_PROTO_H

/* Nome con cui il driver si registra (ipc_register) e con cui i client
 * lo cercano (ipc_lookup). Max IPC_NAME_LEN-1 = 15 caratteri. */
#define KBD_SERVICE_NAME    "kbd"

/* client -> kbd: "dammi una riga".
 * Payload: KbdReadLine. Un payload più corto di 4 byte equivale a
 * KBD_LINE_MAX sulla console 0. */
#define KBD_MSG_READLINE    1u

/* Payload di KBD_MSG_READLINE.
 *
 * Il campo 'console' è arrivato con le console virtuali: il driver
 * tiene un lettore in attesa PER CONSOLE e serve solo quello della
 * console in primo piano, così le shell delle altre restano ferme al
 * proprio prompt invece di rubarsi i tasti a vicenda.
 *
 * Chi manda meno di 8 byte finisce sulla console 0 — è la sola
 * interpretazione possibile per un client che non sa che esistano, e
 * mantiene compatibile un eventuale chiamante vecchio. */
typedef struct {
    unsigned int max;       /* byte massimi accettati nella risposta */
    unsigned int console;   /* console del processo che sta leggendo */
} KbdReadLine;

/* kbd -> client: riga completa, terminatore '\n' incluso.
 * Payload: i byte della riga. len = lunghezza effettiva (può essere 1,
 * cioè il solo '\n', se l'utente ha premuto Invio su riga vuota). */
#define KBD_MSG_LINE        2u

/* Lunghezza massima di una riga consegnata in un singolo messaggio.
 * Deve restare <= IPC_MSG_MAX_DATA (512, vedi kernel/include/ipc.h e
 * lib/include/libc.h): oltre quel limite il kernel troncherebbe il
 * payload senza che nessuna delle due sponde se ne accorga. */
/* 512 = il massimo che un singolo messaggio IPC puo' portare. Era 256, e
 * con i nomi 8.3 nessuna riga ci arrivava; da quando un nome ext2 puo'
 * essere di 255 byte, `cp <lungo> <dest>` supera i 256 e la riga arrivava
 * TAGLIATA — con l'eco a schermo completo, quindi senza che niente lo
 * facesse sospettare: il comando spariva a meta' e l'utente vedeva un
 * errore che parlava d'altro. */
#define KBD_LINE_MAX        512

/* Quante console virtuali esistono. DEVE restare uguale a VGA_N_CONSOLE
 * in kernel/include/vga.h: il driver tiene uno stato di input per
 * ciascuna, e se le due costanti divergessero le console in eccesso
 * finirebbero tutte sull'indice 0 — cioè si ruberebbero i tasti a
 * vicenda. Non si può includere vga.h da qui: questo header deve
 * restare privo di dipendenze (vedi la nota in testa al file). */
#define KBD_N_CONSOLE       4

/* Alt+F1..F12 commutano fra le console. Il driver li intercetta PRIMA
 * di qualunque altra elaborazione e non li consegna a nessuno: sono un
 * comando all'interfaccia, non input per il programma in esecuzione —
 * altrimenti basterebbe un editor che usa Alt+F per il menu File per
 * rendere impossibile cambiare schermo. */
#define KBD_ALT_FN_COMMUTA  1

/* =============================================================================
 * Messaggi della modalità raw
 * ============================================================================= */

/* client -> kbd: sceglie la modalità della PROPRIA console.
 * Payload: KbdSetMode. Un payload più corto di 4 byte viene ignorato
 * (la modalità non cambia); uno di soli 4 byte vale per la console 0.
 *
 * La modalità è per console e non globale: mentre un editor a schermo
 * intero tiene la console 2 in raw, la shell della console 1 deve
 * continuare a ricevere righe intere con l'eco e il Backspace.
 *
 * Il cambio di modalità BUTTA VIA la riga in costruzione e il type-ahead
 * accumulato di quella console: sono testo raccolto con regole che non
 * valgono più. */
#define KBD_MSG_SETMODE     3u

typedef struct {
    unsigned int modo;      /* KBD_MODE_COOKED o KBD_MODE_RAW */
    unsigned int console;
} KbdSetMode;

/* client -> kbd: "dammi il prossimo tasto".
 * Payload: uint32_t con la console del richiedente (assente = 0).
 * Valida solo in raw: in cooked il driver risponde con un rifiuto
 * silenzioso (nessun messaggio) perché non ha eventi da consegnare. */
#define KBD_MSG_READKEY     4u

/* kbd -> client: un evento tasto. Payload: uint32_t, codificato come
 * descritto sotto. Emesso solo su richiesta (una READKEY, un KEY). */
#define KBD_MSG_KEY         5u

#define KBD_MODE_COOKED     0u
#define KBD_MODE_RAW        1u

/* =============================================================================
 * Codifica di un evento tasto (uint32_t)
 *
 *   bit  0..15   codice base: un carattere ASCII (1..127) oppure uno dei
 *                KBD_K_* qui sotto, tutti >= 0x100 per non poterlo mai
 *                confondere con un carattere
 *   bit 16..18   modificatori premuti NELL'ISTANTE del tasto
 *
 * Il Ctrl NON viene applicato al carattere come faceva la modalità
 * cooked (che trasforma Ctrl+A in 0x01): là era l'unico modo di far
 * passare la combinazione dentro un flusso di testo, qui i modificatori
 * hanno un posto loro. Ctrl+A arriva quindi come 'a' | KBD_MOD_CTRL, e
 * il programma può distinguerlo da un Ctrl+Q qualsiasi senza tabelle.
 * ============================================================================= */
#define KBD_KEY_MASK        0x0000FFFFu
#define KBD_MOD_SHIFT       0x00010000u
#define KBD_MOD_CTRL        0x00020000u
#define KBD_MOD_ALT         0x00040000u

#define KBD_K_UP            0x0100u
#define KBD_K_DOWN          0x0101u
#define KBD_K_LEFT          0x0102u
#define KBD_K_RIGHT         0x0103u
#define KBD_K_HOME          0x0104u
#define KBD_K_END           0x0105u
#define KBD_K_PGUP          0x0106u
#define KBD_K_PGDN          0x0107u
#define KBD_K_INS           0x0108u
#define KBD_K_DEL           0x0109u

/* F1..F12 consecutivi: KBD_K_F(1) .. KBD_K_F(12) */
#define KBD_K_F1            0x0110u
#define KBD_K_F(n)          (KBD_K_F1 + (unsigned)(n) - 1u)
#define KBD_K_F12           KBD_K_F(12)

#endif /* KBD_PROTO_H */
