/* =============================================================================
 * lib/include/sys/shm.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * System V shared memory (kernel 0.373, voce 85), Linux's values. A
 * segment is the named zone "/sysvshm.<id>" (shm_open), so any process that
 * knows the id attaches it, a forked child included. How long it lives:
 * <sys/ipc.h>. shmctl knows IPC_STAT, IPC_SET (the mode) and IPC_RMID.
 * ============================================================================= */
#ifndef EXOS_SYS_SHM_H
#define EXOS_SYS_SHM_H

#include "ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct shmid_ds {
    struct ipc_perm shm_perm;
    size_t          shm_segsz;
    time_t          shm_atime;
    time_t          shm_dtime;
    time_t          shm_ctime;
    pid_t           shm_cpid;
    pid_t           shm_lpid;
    unsigned long   shm_nattch;
};

#define SHM_RDONLY 010000
#define SHM_RND    020000
#define SHMLBA     4096

int   shmget(key_t chiave, size_t dim, int flag);
void *shmat(int id, const void *dove, int flag);
int   shmdt(const void *dove);
int   shmctl(int id, int cmd, struct shmid_ds *buf);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_SHM_H */
