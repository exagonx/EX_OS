/* =============================================================================
 * lib/include/sys/sem.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * System V semaphores (kernel 0.373, voce 85), Linux's values. A set is the
 * named zone "/sysvsem.<id>": the values, who touched each last, and a turn
 * counter the sleepers wait on in the kernel by physical address
 * (SYS_ATTESA_DORMI_COMUNE). semop applies all its operations or none.
 * SEM_UNDO is given back when the process exits normally (exit, return from
 * main): a process killed keeps what it took. How long a set lives:
 * <sys/ipc.h>. The caller defines union semun, as POSIX says.
 * ============================================================================= */
#ifndef EXOS_SYS_SEM_H
#define EXOS_SYS_SEM_H

#include "ipc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct sembuf {
    unsigned short sem_num;
    short          sem_op;
    short          sem_flg;
};

struct semid_ds {
    struct ipc_perm sem_perm;
    time_t          sem_otime;
    time_t          sem_ctime;
    unsigned long   sem_nsems;
};

#define SEM_UNDO 0x1000
#define GETPID   11
#define GETVAL   12
#define GETALL   13
#define GETNCNT  14
#define GETZCNT  15
#define SETVAL   16
#define SETALL   17
#define SEMVMX   32767

int semget(key_t chiave, int quanti, int flag);
int semop(int id, struct sembuf *op, size_t n);
int semtimedop(int id, struct sembuf *op, size_t n, const struct timespec *t);
int semctl(int id, int num, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_SEM_H */
