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
 * ! PERCHE' NON L'IMPRONTA DEI SORGENTI
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
 * ! NON SI ESEGUE, SI GUARDA
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
 * ! Gli offset sono scritti come dimensione+1, perche' `char x[0]` non e'
 * C valido e un campo all'offset 0 e' il caso piu' comune che ci sia.
 *
 * -----------------------------------------------------------------------------
 * ! CHE COSA METTERE QUI: ogni tipo che ATTRAVERSA il confine fra la libc
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
CAMPO(dirent_ino,       struct dirent, d_ino)

/* ! DirEntry MANCAVA, ED E' QUELLA CHE ATTRAVERSA LA SYSCALL. `struct
 * dirent` c'era da sempre, ma quella la costruisce la libc per conto suo:
 * la struttura che il KERNEL riempie e il programma legge — con
 * listdir_from(), un blocco di LISTDIR_MAX_BATCH voci allocato dal
 * chiamante — e' questa, ed era fuori dall'impronta.
 *
 * Ci e' entrata ad agosto 2026, quando le si e' aggiunto `ident` per dare
 * a d_ino un valore vero (vedi DirEntry in kernel/include/syscall.h). La
 * struttura e' passata da 264 a 268 byte: senza questa riga, un programma
 * compilato prima e ricollegato dopo avrebbe letto le voci sfalsate di
 * quattro byte a partire dalla seconda, e l'impronta avrebbe detto che
 * andava tutto bene. */
DIM(direntry,           DirEntry)
CAMPO(direntry_name,    DirEntry, name)
CAMPO(direntry_size,    DirEntry, size)
CAMPO(direntry_ident,   DirEntry, ident)
CAMPO(direntry_is_dir,  DirEntry, is_dir)

/* ShmZona: il kernel la riempie per conto del chiamante, esattamente come
 * DirEntry, quindi per la regola scritta in testa a questo file ci deve
 * stare. Entra qui il giorno in cui nasce, invece di essere aggiunta dopo un
 * guasto: il campo `byte` e' insieme richiesta e risposta, e se le tre copie
 * della struttura divergessero un programma leggerebbe la dimensione della
 * zona dal campo sbagliato — cioe' si crederebbe autorizzato a scrivere in
 * una memoria condivisa piu' grande di quella che c'e'. */
/* struct pollfd: il kernel scrive `revents` dentro l'array del chiamante,
 * quindi attraversa il confine come ShmZona e DirEntry. Qui il rischio ha una
 * forma particolare: i campi sono `short` per compatibilita' con POSIX, e uno
 * short che diventasse int porterebbe la struttura da 8 a 12 byte — il kernel
 * scriverebbe i revents della voce N nel campo events della voce N+1, cioe'
 * risposte plausibili sulle domande sbagliate. */
DIM(pollfd,             struct pollfd)
CAMPO(pollfd_fd,        struct pollfd, fd)
CAMPO(pollfd_events,    struct pollfd, events)
CAMPO(pollfd_revents,   struct pollfd, revents)

/* VideoInfo: il kernel la riempie per conto del chiamante, come ShmZona. Il
 * rischio ha una forma sua — sono cinque unsigned int e basta che una diventi
 * di larghezza diversa perche' il server grafico legga la larghezza dello
 * schermo dal campo del passo, cioe' disegni su una geometria plausibile e
 * sbagliata. */
/* SpawnExtra: la riempie il CHIAMANTE e la legge il kernel, quindi attraversa
 * il confine nel verso opposto a ShmZona — ed e' anche peggio, perche' un
 * campo letto storto qui non da' un valore sbagliato: da' una REDIREZIONE
 * sbagliata, cioe' un programma che scrive nel file di un altro. Ha gia'
 * cambiato forma due volte (SPNX -> SPNY -> SPNZ), e ogni volta la magia e'
 * cambiata apposta: questa e' la rete che dice se ce ne si e' dimenticati. */
DIM(spawnextra,         SpawnExtra)
CAMPO(spawnextra_magia, SpawnExtra, magia)
CAMPO(spawnextra_envp,  SpawnExtra, envp)
CAMPO(spawnextra_naz,   SpawnExtra, n_azioni)
CAMPO(spawnextra_az,    SpawnExtra, azioni)
CAMPO(spawnextra_flag,  SpawnExtra, flag)
CAMPO(spawnextra_cons,  SpawnExtra, console)
DIM(spawnazione,        SpawnAzione)

DIM(videoinfo,          VideoInfo)
CAMPO(videoinfo_fisico, VideoInfo, fisico)
CAMPO(videoinfo_passo,  VideoInfo, passo)
CAMPO(videoinfo_largh,  VideoInfo, larghezza)
CAMPO(videoinfo_alt,    VideoInfo, altezza)
CAMPO(videoinfo_bit,    VideoInfo, bit)

DIM(shmzona,            ShmZona)
CAMPO(shmzona_nome,     ShmZona, nome)
CAMPO(shmzona_byte,     ShmZona, byte)
CAMPO(shmzona_flag,     ShmZona, flag)
CAMPO(shmzona_virt,     ShmZona, virt)

DIM(jmp_buf,            jmp_buf)
