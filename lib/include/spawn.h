/* =============================================================================
 * lib/include/spawn.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <spawn.h> — posix_spawn (19 November 2026, voce 105). Solo in libc.a.
 *
 * fork, le azioni nel figlio nell'ordine dato, execve: come lo descrive
 * POSIX, adesso che il kernel chiude i descrittori FD_CLOEXEC all'exec
 * (0.349). L'errore dell'exec torna al padre (ENOENT, EACCES...) e allora
 * non resta nessun figlio. Le azioni sono una lista che cresce, senza un
 * numero massimo. Lo spawn() nativo di EX-OS (spawn_abi.h) e' un'altra cosa.
 *
 * ! LE STRUTTURE COINCIDONO con AzioniSp e AttrSp in lib/libc.c.
 *
 * Un solo modo di schedulare: SCHED_OTHER con priorita' 0 (sched.h), le
 * altre politiche sono EINVAL. POSIX_SPAWN_SETPGROUP riesce solo per il
 * proprio gruppo (setpgid), POSIX_SPAWN_SETSID e' setsid. POSIX_SPAWN_USEVFORK
 * si accetta: e' sempre fork.
 * ============================================================================= */
#ifndef EXOS_SPAWN_H
#define EXOS_SPAWN_H

#include "libc.h"
#include "sched.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int   __n, __posti;
    void *__azioni;
} posix_spawn_file_actions_t;

typedef struct {
    short    __flag;
    int      __gruppo;
    sigset_t __predefiniti, __maschera;
    int      __politica;
    int      __priorita;
} posix_spawnattr_t;

#define POSIX_SPAWN_RESETIDS       0x01
#define POSIX_SPAWN_SETPGROUP      0x02
#define POSIX_SPAWN_SETSIGDEF      0x04
#define POSIX_SPAWN_SETSIGMASK     0x08
#define POSIX_SPAWN_SETSCHEDPARAM  0x10
#define POSIX_SPAWN_SETSCHEDULER   0x20
#define POSIX_SPAWN_USEVFORK       0x40
#define POSIX_SPAWN_SETSID         0x80

int posix_spawn(pid_t *pid, const char *percorso,
                const posix_spawn_file_actions_t *azioni,
                const posix_spawnattr_t *attr,
                char *const argv[], char *const envp[]);
int posix_spawnp(pid_t *pid, const char *file,
                 const posix_spawn_file_actions_t *azioni,
                 const posix_spawnattr_t *attr,
                 char *const argv[], char *const envp[]);

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *a);
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *a);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *a, int fd,
                                     const char *percorso, int flag, mode_t modo);
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *a, int fd);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *a, int fd, int nuovo);
int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *a, const char *percorso);

int posix_spawnattr_init(posix_spawnattr_t *a);
int posix_spawnattr_destroy(posix_spawnattr_t *a);
int posix_spawnattr_setflags(posix_spawnattr_t *a, short flag);
int posix_spawnattr_getflags(const posix_spawnattr_t *a, short *flag);
int posix_spawnattr_setpgroup(posix_spawnattr_t *a, pid_t gruppo);
int posix_spawnattr_getpgroup(const posix_spawnattr_t *a, pid_t *gruppo);
int posix_spawnattr_setsigdefault(posix_spawnattr_t *a, const sigset_t *s);
int posix_spawnattr_getsigdefault(const posix_spawnattr_t *a, sigset_t *s);
int posix_spawnattr_setsigmask(posix_spawnattr_t *a, const sigset_t *s);
int posix_spawnattr_getsigmask(const posix_spawnattr_t *a, sigset_t *s);
int posix_spawnattr_setschedpolicy(posix_spawnattr_t *a, int politica);
int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *a, int *politica);
int posix_spawnattr_setschedparam(posix_spawnattr_t *a, const struct sched_param *p);
int posix_spawnattr_getschedparam(const posix_spawnattr_t *a, struct sched_param *p);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SPAWN_H */
