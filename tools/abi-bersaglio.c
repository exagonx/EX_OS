/* =============================================================================
 * tools/abi-bersaglio.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * La FORMA dei tipi che la libc e i programmi si scambiano, scritta nelle
 * DIMENSIONI DEI SIMBOLI di un file oggetto. L'impronta ABI di
 * tools/ricostruisci-bersaglio.sh e' la sha256 di:
 *
 *     i386-exos-gcc -c -I lib/include tools/abi-bersaglio.c -o abi.o
 *     i386-exos-nm --print-size --defined-only abi.o
 *
 * -----------------------------------------------------------------------------
 * ⚠️ PERCHE' NON L'IMPRONTA DEI SORGENTI
 *
 * La prima versione faceva la sha256 di lib/libc.c e degli header:
 * qualunque modifica — anche AGGIUNGERE una funzione, che non rompe
 * niente — gridava «ricostruisci tutto il bersaglio», cioe' tre ore di
 * macchina per un falso allarme. Un avviso che grida sempre e' un avviso
 * che si impara a ignorare, ed e' peggio di nessun avviso.
 *
 * Cio' che rompe i binari gia' costruiti non e' il codice: e' la forma
 * dei dati. Se `struct stat` passa da 48 a 60 byte, la fstat() della libc
 * nuova scrive 12 byte oltre il campo di chi la chiama, e nessun
 * collegamento se ne accorge — e' successo il 4 agosto 2026 con `time_t`
 * da 32 a 64 bit, ed e' costato due giorni. Questo file lo vede; una
 * funzione in piu' no.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ NON SI ESEGUE, SI GUARDA
 *
 * La via ovvia sarebbe un programma che stampa i sizeof. Non si puo': un
 * binario i386-exos su Linux non gira, e compilarlo con `gcc -m32` qui
 * non si collega nemmeno — questa macchina ha il compilatore a 32 bit ma
 * non le sue librerie.
 *
 * Quindi ogni misura diventa un ARRAY GLOBALE della dimensione voluta, e
 * si legge con `nm --print-size`. Il vantaggio non e' solo di non dover
 * eseguire: le dimensioni le calcola il compilatore VERO del bersaglio,
 * non un suo sostituto che gli somiglia.
 *
 * ⚠️ Gli offset sono scritti come dimensione+1, perche' `char x[0]` non e'
 * C valido e un campo all'offset 0 e' il caso piu' comune che ci sia.
 *
 * -----------------------------------------------------------------------------
 * ⚠️ CHE COSA METTERE QUI: ogni tipo che ATTRAVERSA il confine fra la libc
 * e un programma. Se domani una syscall riempira' una struttura nuova per
 * conto del chiamante, va aggiunta anche qui, o quel confine resta senza
 * guardia. `FILE` non c'e' apposta: e' opaco, i programmi ne maneggiano
 * solo puntatori, e la sua forma non attraversa niente.
 * ============================================================================= */

#include <stddef.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <dirent.h>
#include <setjmp.h>

#define DIM(nome, tipo)         char abi_dim_##nome[sizeof(tipo)];
#define CAMPO(nome, tipo, c)    char abi_off_##nome[offsetof(tipo, c) + 1];

/* --- Scalari. `time_t` e' quello che ha gia' fatto danno. ------------------ */
DIM(size_t,     size_t)
DIM(ssize_t,    ssize_t)
DIM(ptrdiff_t,  ptrdiff_t)
DIM(off_t,      off_t)
DIM(time_t,     time_t)
DIM(mode_t,     mode_t)
DIM(ino_t,      ino_t)
DIM(dev_t,      dev_t)
DIM(pid_t,      pid_t)
DIM(wchar_t,    wchar_t)
DIM(wint_t,     wint_t)
DIM(puntatore,  void *)
DIM(long,       long)
DIM(longlong,   long long)

/* --- Strutture, campo per campo -------------------------------------------
 * La dimensione totale non basta: due strutture della stessa dimensione
 * con i campi in ordine diverso sono altrettanto incompatibili. */
DIM(stat,               struct stat)
CAMPO(stat_dev,         struct stat, st_dev)
CAMPO(stat_ino,         struct stat, st_ino)
CAMPO(stat_mode,        struct stat, st_mode)
CAMPO(stat_nlink,       struct stat, st_nlink)
CAMPO(stat_uid,         struct stat, st_uid)
CAMPO(stat_gid,         struct stat, st_gid)
CAMPO(stat_size,        struct stat, st_size)
CAMPO(stat_blksize,     struct stat, st_blksize)
CAMPO(stat_blocks,      struct stat, st_blocks)
CAMPO(stat_atime,       struct stat, st_atime)
CAMPO(stat_mtime,       struct stat, st_mtime)
CAMPO(stat_ctime,       struct stat, st_ctime)

DIM(timeval,            struct timeval)
CAMPO(timeval_sec,      struct timeval, tv_sec)
CAMPO(timeval_usec,     struct timeval, tv_usec)

DIM(timespec,           struct timespec)
CAMPO(timespec_sec,     struct timespec, tv_sec)
CAMPO(timespec_nsec,    struct timespec, tv_nsec)

DIM(tm,                 struct tm)
CAMPO(tm_sec,           struct tm, tm_sec)
CAMPO(tm_year,          struct tm, tm_year)
CAMPO(tm_isdst,         struct tm, tm_isdst)

DIM(dirent,             struct dirent)
CAMPO(dirent_name,      struct dirent, d_name)

DIM(jmp_buf,            jmp_buf)
