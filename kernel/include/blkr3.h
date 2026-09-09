/* =============================================================================
 * kernel/include/blkr3.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * UN DISCO SERVITO DA UN PROCESSO — la cucitura fra il VFS e ring 3
 *
 * ! PERCHE' ESISTE, E PERCHE' STA NEL KERNEL
 *
 * Lo strato a blocchi conosce ATA, floppy e CD: tutta roba che il kernel guida
 * di persona. Un disco USB no — il suo driver e' un processo, come dice la
 * direttiva 1 di DIREZIONE.md — e senza questo file quel driver leggerebbe
 * settori che nessun filesystem potrebbe mai montare. Non manca un driver:
 * manca la STRADA fra il VFS e un processo.
 *
 * Quello che il kernel guadagna qui non e' l'USB: e' una frase generica —
 * «un dispositivo a blocchi puo' essere servito da un processo» — che domani
 * vale per un disco di rete, per un'immagine montata come file, per un
 * filesystem sperimentale scritto in ring 3. Nel kernel resta l'arbitrato, il
 * lavoro sta fuori.
 *
 * -----------------------------------------------------------------------------
 * ! NON PASSA DALLA MAILBOX IPC, e la scelta va spiegata perche' l'IPC c'e'
 * gia' ed era la strada ovvia.
 *
 * Chi legge un file e' un processo QUALUNQUE: puo' essere un client di ExWin
 * nel mezzo di una sua conversazione, o un driver che sta parlando col PCI.
 * Fargli arrivare in mailbox anche le risposte del disco vorrebbe dire
 * mescolare due traffici che non c'entrano niente fra loro, e il difetto
 * salterebbe fuori solo quando le due cose capitano insieme — cioe' tardi, e
 * su una macchina di qualcun altro.
 *
 * Qui il canale e' suo: una richiesta per volta per dispositivo, il chiamante
 * dorme sul dispositivo e non sulla propria mailbox.
 *
 * -----------------------------------------------------------------------------
 * ! LE TRE COSE CHE VANNO PROGETTATE SUBITO, NON DOPO
 *
 * Aprire una strada dal kernel verso un processo vuol dire che il kernel
 * comincia ad ASPETTARE qualcuno che puo' non rispondere mai. Le tre trappole
 * sono queste, e sono chiuse qui dentro:
 *
 *   1. IL SERVENTE MUORE con richieste in volo. Chi aspettava si sveglia con
 *      -EIO, il dispositivo sparisce. Un processo bloccato per sempre su un
 *      driver morto e' il difetto che questa strada porta con se'.
 *   2. LA RICHIESTA SENZA RISPOSTA. C'e' una scadenza: passata quella, il
 *      chiamante rende -EIO invece di dormire per sempre.
 *   3. IL SERVENTE CHE LEGGE SE STESSO. Un driver che apre un file sul
 *      proprio dispositivo aspetterebbe la propria risposta, che non puo'
 *      arrivare perche' e' lui a doverla dare. Si rifiuta con -EDEADLK: e' un
 *      errore di chi scrive il driver, e va detto invece di farlo sembrare
 *      una macchina che si e' piantata.
 * ============================================================================= */

#ifndef BLKR3_H
#define BLKR3_H

#include "kernel.h"

/* Quanti settori al massimo viaggiano in una richiesta sola. Il chiamante che
 * ne chiede di piu' viene servito in piu' giri, dentro blk_read/blk_write: qui
 * il tetto serve a tenere il buffer del kernel di dimensione nota. */
#define BLKR3_SETTORI_MAX   8
#define BLKR3_BYTE_MAX      (BLKR3_SETTORI_MAX * 512)

/* Quanto aspetta il chiamante prima di rinunciare. Una chiavetta lenta puo'
 * metterci qualche secondo su una scrittura; cinque sono un compromesso fra
 * "non rinunciare a chi sta lavorando" e "non dormire per sempre". */
#define BLKR3_SCADENZA_MS   5000

/* Le operazioni, come le vede il servente. */
#define BLKR3_LEGGI     1
#define BLKR3_SCRIVI    2
#define BLKR3_SVUOTA    3   /* riversa cio' che il driver tiene in sospeso */

/* Registra un dispositivo servito da `pid`. Rende l'indice del dispositivo a
 * blocchi (quello di blk_get), o un valore negativo. */
int  blkr3_offri(uint32_t pid, const char *nome, uint64_t settori,
                 uint32_t byte_settore, int sola_lettura);

/* Il servente aspetta la prossima richiesta per i PROPRI dispositivi. Blocca
 * finche' non ne arriva una o finche' non scade. Rende 0, o un valore
 * negativo. */
int  blkr3_attendi(uint32_t pid, uint32_t *op, uint64_t *lba, uint32_t *n,
                   uint32_t *quale, uint32_t scadenza_ms);

/* ! IL BUFFER NON SI COPIA DUE VOLTE. I dati di una richiesta stanno gia' in
 * memoria del kernel: questa rende il puntatore, e chi sta al confine — cioe'
 * syscall_impl.c — ci copia dentro o ne copia fuori verso lo spazio utente,
 * una volta sola. Vale solo fra un blkr3_attendi() e la sua risposta; fuori
 * da li' rende NULL. */
void *blkr3_dati(uint32_t pid, uint32_t *max);

/* L'operazione della richiesta che `pid` ha in mano, o 0. Serve al confine
 * (syscall_impl.c) per sapere in che verso copiare i settori. */
uint32_t blkr3_op_in_corso(uint32_t pid);

/* Il servente dichiara com'e' andata. In lettura i settori sono gia' stati
 * messi nel buffer di blkr3_dati(). */
int  blkr3_risposta(uint32_t pid, int esito);

/* Le tre chiamate dello strato a blocchi. `i` e' l'indice del dispositivo. */
int  blkr3_read (int i, uint64_t lba, uint32_t n, void *buf);
int  blkr3_write(int i, uint64_t lba, uint32_t n, const void *buf);
int  blkr3_flush(int i);

/* Un processo e' morto: porta via i suoi dispositivi e sveglia chi aspettava.
 * La chiama proc_reap_zombie(). */
void blkr3_processo_morto(uint32_t pid);

#endif /* BLKR3_H */
