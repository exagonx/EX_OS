/* =============================================================================
 * lib/exuser/exuser.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * ExUser — gli utenti, il loro archivio, e le password che non si vedono
 *
 * ! ADESSO GLI UTENTI SONO TRE, ed e' la condizione che questo sistema chiede
 * prima di condividere qualcosa: `login` che autentica, `install` che crea il
 * primo utente e la password di root, e `su` che verifica per alzarsi. Tutto
 * questo codice stava dentro login.c, dove era giusto finche' a usarlo c'era
 * solo lui.
 *
 * ! E TRE COPIE DI UN FORMATO DI ARCHIVIO SONO TRE MODI DI DIVERGERE. Qui
 * dentro c'e' la forma dei due file, il sale, l'impronta e la lettura della
 * password senza mostrarla: chi ne fa una copia sbaglia il giorno che si
 * aggiunge un campo, e su un archivio di password sbagliare vuol dire
 * chiudere fuori qualcuno o far entrare qualcuno.
 *
 * ! E' UN MODULO COMPILATO DENTRO, NON UNA .so, e non e' un ripensamento:
 * `login`, `install` e la shell devono partire anche quando /lib/libc.so non
 * c'e' — sono i programmi con cui si ripara. Una libreria condivisa qui
 * sarebbe la dipendenza che non ci si puo' permettere.
 *
 * ! I DUE FILE SONO SEPARATI APPOSTA:
 *
 *     /boot/utenti   nome:uid:gid            0644, lo legge chiunque
 *     /boot/ombra    nome:sale:impronta      0600, solo root
 *
 * Un file solo avrebbe dovuto scegliere fra due mali: leggibile da tutti, e
 * allora chiunque si porta via le impronte; leggibile solo da root, e allora
 * NESSUNO PUO' PIU' TRADURRE UN uid IN UN NOME.
 * ============================================================================= */
#ifndef EXUSER_H
#define EXUSER_H

#ifdef __cplusplus
extern "C" {
#endif

#define EXUSER_NOME_MAX   32
#define EXUSER_PASS_MAX   64

/* -----------------------------------------------------------------------------
 * La console e la password
 *
 * ! LA PASSWORD SI LEGGE SENZA MOSTRARLA, e non e' una cortesia: e' l'unica
 * ragione per cui questo codice ha bisogno del modo raw della tastiera. Chi
 * non riesce a prenderlo NON deve ripiegare su una lettura normale — mostrare
 * una password perche' il modo raw non si e' potuto prendere e' peggio che
 * rifiutarsi.
 * --------------------------------------------------------------------------- */

/* Prende il primo piano sulla console: chi legge dei tasti deve averlo. */
void exuser_prendi_console(void);

/* Legge una password mostrando asterischi. Rende la lunghezza, <0 se non ce
 * l'ha fatta — e in quel caso NON ha letto niente in chiaro. */
int  exuser_leggi_password(char *dst, int max);

/* Una riga normale, con l'eco. */
int  exuser_leggi_riga(char *dst, int max);

/* -----------------------------------------------------------------------------
 * L'archivio
 *
 * ! `radice` E' IL PREFISSO DEI DUE FILE, e serve a `install`: lui scrive
 * nell'archivio del disco che sta preparando, non nel proprio. Passare "" o 0
 * vuol dire «il sistema di adesso», che e' il caso di login e di su.
 * --------------------------------------------------------------------------- */

/* 1 se il nome e' fatto di lettere, cifre e '_'. Niente ':' — separa i campi —
 * e niente spazi, che renderebbero ambigua la riga. */
int  exuser_nome_valido(const char *n);

/* 1 se in quell'archivio c'e' almeno un utente. */
int  exuser_c_e_qualcuno(const char *radice);

/* Il primo uid libero da 1000 in su; 0 se l'archivio e' vuoto (sara' root). */
unsigned int exuser_prossimo_uid(const char *radice);

/* Aggiunge un utente ai due file. Rende 0, o <0. */
int  exuser_aggiungi(const char *radice, const char *nome, const char *pass,
                     unsigned int uid, unsigned int gid);

/* Verifica una password. Rende 1 se combacia, 0 se no o se l'utente non c'e'.
 *
 * ! NON DICE QUALE DELLE DUE COSE E' ANDATA STORTA, ed e' voluto: «utente
 * sconosciuto» e «password sbagliata» sono due risposte diverse solo per chi
 * sta provando i nomi. */
int  exuser_verifica(const char *radice, const char *nome, const char *pass,
                     unsigned int *uid, unsigned int *gid);

#ifdef __cplusplus
}
#endif

#endif /* EXUSER_H */
