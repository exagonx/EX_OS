/* =============================================================================
 * kernel/include/pipe.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * Le pipe — un tubo di byte fra due processi.
 *
 * COS'E' UNA PIPE, ridotta all'osso: un buffer circolare in memoria del
 * kernel, con due estremita' che sono due file descriptor. Chi scrive
 * dalla parte giusta riempie, chi legge dall'altra svuota, e le due cose
 * possono avvenire in processi diversi.
 *
 * -----------------------------------------------------------------------------
 * ! LE TRE REGOLE CHE FANNO FUNZIONARE UNA PIPE, e che sono tutte
 * situazioni di CONFINE — nessuna riguarda il caso normale:
 *
 *   1. LEGGERE DA UNA PIPE VUOTA CON ANCORA UNO SCRITTORE VIVO
 *      significa ASPETTARE, non "zero byte". Ritornare 0 vorrebbe dire
 *      "fine dei dati", e chi legge chiuderebbe convinto di aver finito
 *      mentre l'altro non ha ancora scritto.
 *
 *   2. LEGGERE DA UNA PIPE VUOTA SENZA PIU' SCRITTORI significa fine dei
 *      dati, e li' 0 e' la risposta giusta. La differenza fra i due casi
 *      e' tutta nel CONTEGGIO DEGLI SCRITTORI: senza quel contatore una
 *      pipe non e' distinguibile da un blocco eterno.
 *
 *   3. SCRIVERE SU UNA PIPE SENZA PIU' LETTORI e' un errore (EPIPE), non
 *      un'attesa: nessuno leggera' mai quei byte. Su Unix arriva anche
 *      SIGPIPE, che EX-OS non ha — quindi qui si vede solo il -EPIPE, e
 *      chi non controlla il valore di ritorno di write() non se ne
 *      accorge. E' un limite noto e dichiarato.
 *
 * -----------------------------------------------------------------------------
 * ! IL BUFFER E' PICCOLO DI PROPOSITO (PIPE_DIM). Una pipe non e' un
 * file: serve a far scorrere dati fra due processi che girano insieme, e
 * un buffer grande nasconderebbe il fatto che il lettore non sta al passo
 * invece di farlo emergere subito. Su Linux sono 64 KB, qui 4: la RAM di
 * questo sistema si misura in decine di MB.
 *
 * ! NON C'E' LA GARANZIA DI ATOMICITA' DI POSIX. Su Unix una write di
 * meno di PIPE_BUF byte non si mescola con quella di un altro scrittore.
 * Qui la scrittura puo' essere parziale e piu' scrittori possono
 * intrecciarsi: con un solo filo per processo e nessun uso reale di piu'
 * scrittori sulla stessa pipe, garantirla costerebbe complessita' per
 * nessun beneficio misurabile. Va detto, perche' chi ci conta sopra
 * scriverebbe codice corretto altrove e rotto qui.
 * ============================================================================= */

#ifndef PIPE_H
#define PIPE_H

#include "kernel.h"

#define PIPE_DIM        4096    /* byte nel buffer circolare */
#define PIPE_MAX        16      /* pipe aperte contemporaneamente nel sistema */
/* Quanto si trasferisce al massimo per chiamata. E' anche la dimensione
 * del buffer di appoggio sullo stack del kernel (128 KB per processo), che
 * serve a non toccare mai la memoria utente con gli interrupt disabilitati
 * ne' l'anello con quelli abilitati — vedi il commento in pipe.c. Una
 * read piu' grande semplicemente torna con meno byte, che e' il contratto
 * normale di read(). */
#define PIPE_TRANCHE    1024

/* Handle di pipe: un indice nella tabella, o < 0 se non valido. */
int  pipe_crea(void);

/* Contatori delle estremita'. Chi duplica un descrittore che punta a una
 * pipe DEVE incrementare, chi lo chiude DEVE decrementare: e' il conteggio
 * su cui si regge la distinzione fra "aspetta" e "fine dei dati". */
void pipe_apri_lettore(int h);
void pipe_apri_scrittore(int h);
void pipe_chiudi_lettore(int h);
void pipe_chiudi_scrittore(int h);

/* ! LE VARIANTI _locked PRESUPPONGONO CHE IL CHIAMANTE ABBIA GIA' `cli`.
 *
 * interrupts_disable()/enable() in questo kernel sono cli/sti grezzi,
 * senza contatore di annidamento: chiamare la versione pubblica da un
 * punto che e' gia' dentro una sezione critica farebbe `sti` troppo
 * presto, riaprendo gli interrupt a meta' di un'operazione non atomica.
 * E' la stessa ragione per cui esiste sched_unblock_locked().
 *
 * Le usa proc_exit()/proc_kill() (kernel/sched/sched.c), che girano
 * entrambe sotto cli. */
void pipe_chiudi_lettore_locked(int h);
void pipe_chiudi_scrittore_locked(int h);

/* Ritornano i byte trasferiti, 0 per fine dei dati (solo in lettura), o un
 * errno negativo. Bloccano il chiamante quando serve. */
int32_t pipe_leggi(int h, void *buf, uint32_t n);
int32_t pipe_scrivi(int h, const void *buf, uint32_t n);

/* Quanti byte si possono leggere subito: serve a ioctl(FIONREAD) e alle
 * diagnostiche. */
int32_t pipe_disponibili(int h);

/* =============================================================================
 * PER poll() — guardare senza leggere, e mettersi in attesa senza bloccarsi
 *
 * Una lettura o una scrittura decidono da sole se bloccare. poll() no: deve
 * guardare N sorgenti, e bloccarsi UNA volta sola su tutte insieme. Servono
 * quindi due cose che il resto di questo file non ha mai avuto bisogno di
 * esporre — lo stato senza consumarlo, e la registrazione senza il blocco.
 *
 * ! SONO TUTTE `_locked`: poll() chiama con gli interrupt gia' disabilitati,
 * perche' fra il «non c'e' niente» e il `sched_block()` non ci puo' stare
 * niente — e' la race del risveglio perduto documentata in testa a pipe.c.
 * Una variante che facesse `sti` da sola riaprirebbe esattamente quella
 * finestra, e la riaprirebbe su N sorgenti invece che su una.
 * ========================================================================== */

/* Stato senza consumare niente. Rende 0, o -EBADF se l'handle non e' valido.
 * Ogni puntatore di uscita puo' essere NULL. */
int32_t pipe_stato_locked(int h, uint32_t *quanti, uint32_t *spazio,
                          uint32_t *lettori, uint32_t *scrittori);

/* Si mette in coda come chi aspetta, SENZA bloccarsi. `per_lettura` sceglie
 * quale dei due slot.
 *
 * ! RENDE 0 SE LO SLOT ERA GIA' DI QUALCUN ALTRO, e chi chiama deve
 * guardarlo. C'e' un solo posto per lato — e' cosi' da sempre, una pipe ha un
 * lettore e uno scrittore per costruzione — quindi sovrascriverlo vorrebbe
 * dire rubare la sveglia a chi era li' prima: quello non verrebbe svegliato
 * mai piu'. Chi non ottiene il posto deve ripiegare su un'attesa a scadenza
 * breve, non fare finta di averlo. */
int pipe_attesa_registra_locked(int h, int per_lettura, uint32_t pid);

/* Toglie il proprio pid dallo slot, e SOLO il proprio: se nel frattempo la
 * sveglia e' arrivata, lo slot e' gia' stato azzerato da chi ha svegliato e
 * potrebbe appartenere a un altro. */
void pipe_attesa_togli_locked(int h, int per_lettura, uint32_t pid);

#endif /* PIPE_H */
