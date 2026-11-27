/* =============================================================================
 * lib/include/sys/ipc.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * System V IPC (kernel 0.373, voce 85): the keys and the flags shared by
 * <sys/shm.h> and <sys/sem.h>, with Linux's values. Qt includes these on
 * every Unix; PostgreSQL, Apache and PHP's sysvsem use them.
 *
 * ! A SEGMENT OR A SET IS A NAMED ZONE OF EX-OS (shm_open), and it lives as
 * the zones live: while some process holds it (kernel/include/shm.h). One
 * made and left by a process that then exits is gone, where on Linux it
 * would stay until IPC_RMID. IPC_RMID takes the name away for good only
 * when the last holder lets go - which is what SysV does with a segment
 * still attached.
 * ============================================================================= */
#ifndef EXOS_SYS_IPC_H
#define EXOS_SYS_IPC_H

#include "../libc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXOS_KEY_T
#define EXOS_KEY_T
typedef int key_t;
#endif

struct ipc_perm {
    key_t          __key;
    uid_t          uid;
    gid_t          gid;
    uid_t          cuid;
    gid_t          cgid;
    unsigned short mode;
    unsigned short __seq;
};

#define IPC_PRIVATE ((key_t)0)
#define IPC_CREAT   01000
#define IPC_EXCL    02000
#define IPC_NOWAIT  04000
#define IPC_RMID    0
#define IPC_SET     1
#define IPC_STAT    2
#define IPC_INFO    3

/* the key of a file: its inode, its device and the low byte of id */
key_t ftok(const char *percorso, int id);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_IPC_H */
