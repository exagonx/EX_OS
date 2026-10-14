/* =============================================================================
 * lib/include/unistd.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <unistd.h> — chiamate di sistema in stile POSIX.
 *
 * PERCHE' QUESTO FILE E' UNA FACCIATA. Tutte le dichiarazioni della libc
 * di EX-OS stanno in un header solo, libc.h, e i programmi di /bin lo
 * includono cosi' com'e'. Questi header con i nomi standard esistono
 * perche' il codice scritto per un sistema POSIX — a cominciare da un
 * compilatore da portare — scrive #include <unistd.h> e si aspetta che
 * funzioni.
 *
 * Sono volutamente sottili: rimandano a libc.h invece di dichiarare un
 * sottoinsieme proprio. Due elenchi della stessa funzione sono due elenchi
 * che prima o poi divergono, e la divergenza si manifesta come un
 * prototipo sbagliato — cioe' argomenti passati storti, non un errore di
 * compilazione.
 *
 * Quando la libc verra' spezzata in un archivio vero (libc.a, un file per
 * area), questi header prenderanno il proprio contenuto e libc.h restera'
 * per compatibilita'. Fino ad allora, la fonte unica e' una sola.
 * ============================================================================= */

#ifndef EXOS_UNISTD_H
#define EXOS_UNISTD_H

#include "libc.h"
#include <limits.h>     /* PATH_MAX: il codice di terzi lo aspetta da qui */

/* (@EXILLA-JS, 30 settembre 2026) */
#define STDIN_FILENO    0
#define STDOUT_FILENO   1
#define STDERR_FILENO   2

/* Le opzioni POSIX che EX-OS ha davvero, col valore di POSIX 2008: il codice
 * di terzi le controlla prima di usare la funzione (l'orologio monotono di
 * ipc/chromium in Gecko non compilava senza _POSIX_MONOTONIC_CLOCK). Solo
 * quelle vere: dichiararne una che manca sarebbe peggio che tacerla. */
#define _POSIX_VERSION          200809L
#define _POSIX_TIMERS           200809L   /* clock_gettime, nanosleep */
#define _POSIX_MONOTONIC_CLOCK  200809L   /* CLOCK_MONOTONIC */
#define _POSIX_THREADS          200809L   /* pthread (tappa 1 di Exilla) */
#define _POSIX_SEMAPHORES       200809L   /* <semaphore.h> */

#define _SC_NPROCESSORS_CONF 83      /* i processori: uno (vedi sysconf) */

/* ! EX-OS NON HA fork(): i processi nascono con spawn (vedi spawn_ex in
 * libc.h). fork esiste perche' il codice portabile la nomina, e risponde -1
 * con ENOSYS: chi la chiama la tratta gia' come un fallimento. Solo libc.a. */

#ifdef __cplusplus
extern "C" {
#endif
pid_t   fork(void);

/* POSIX che EX-OS ha a meta' o non ha (@EXILLA-JS): pread/pwrite passano da
 * lseek e rimettono la posizione; fsync rende 0 (i filesystem scrivono gia'
 * sul disco a ogni write); getppid c'e'. I link simbolici, i proprietari
 * cambiati per descrittore, chroot e le FIFO con nome non esistono: ENOSYS
 * o, per readlink su un file che non e' un link, EINVAL — come vuole POSIX. */
pid_t   getppid(void);
ssize_t pread(int fd, void *buf, size_t n, off_t pos);
ssize_t pwrite(int fd, const void *buf, size_t n, off_t pos);
int     fsync(int fd);
ssize_t readlink(const char *percorso, char *buf, size_t n);
int     symlink(const char *bersaglio, const char *percorso);
int     linkat(int dfd1, const char *p1, int dfd2, const char *p2, int flag);
int     lchown(const char *percorso, unsigned int uid, unsigned int gid);
int     fchown(int fd, unsigned int uid, unsigned int gid);
int     chroot(const char *percorso);
int     mkfifo(const char *percorso, mode_t modo);
#ifdef __cplusplus
}
#endif

#endif /* EXOS_UNISTD_H */
