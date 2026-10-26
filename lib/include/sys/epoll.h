/* =============================================================================
 * lib/include/sys/epoll.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/epoll.h> (26 October 2026), with the values and the layout of Linux.
 *
 * ! ONLY TCP SOCKETS GO IN A SET. The set lives in the network stack (ip.drv
 * 0.016, IP_MSG_INS_* in drivers/net/ip_proto.h), which wakes epoll_wait
 * itself when a connection is ready: a thousand connections cost nothing
 * while they are quiet. A file, a pipe or anything else gives EPERM, as a
 * regular file does on Linux. EPOLLIN, EPOLLOUT, EPOLLRDHUP, EPOLLHUP,
 * EPOLLERR, EPOLLET and EPOLLONESHOT work; EPOLLEXCLUSIVE, EPOLLWAKEUP and
 * EPOLLPRI are accepted and change nothing. One thread at a time waits on a
 * set: a second one takes turns with it (10 ms).
 *
 * Only in the static libc, like the sockets.
 * ============================================================================= */
#ifndef EXOS_SYS_EPOLL_H
#define EXOS_SYS_EPOLL_H

#include "../libc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EPOLL_CLOEXEC   02000000

#define EPOLL_CTL_ADD   1
#define EPOLL_CTL_DEL   2
#define EPOLL_CTL_MOD   3

#define EPOLLIN         0x001u
#define EPOLLPRI        0x002u
#define EPOLLOUT        0x004u
#define EPOLLERR        0x008u
#define EPOLLHUP        0x010u
#define EPOLLRDNORM     0x040u
#define EPOLLRDBAND     0x080u
#define EPOLLWRNORM     0x100u
#define EPOLLWRBAND     0x200u
#define EPOLLMSG        0x400u
#define EPOLLRDHUP      0x2000u
#define EPOLLEXCLUSIVE  (1u << 28)
#define EPOLLWAKEUP     (1u << 29)
#define EPOLLONESHOT    (1u << 30)
#define EPOLLET         (1u << 31)

typedef union epoll_data {
    void              *ptr;
    int                fd;
    unsigned int       u32;
    unsigned long long u64;
} epoll_data_t;

/* 12 bytes on both architectures: packed on x86_64, as glibc has it */
struct epoll_event {
    unsigned int events;
    epoll_data_t data;
}
#ifdef __x86_64__
__attribute__((__packed__))
#endif
;

int epoll_create(int n);
int epoll_create1(int flag);
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *evento);
int epoll_wait(int epfd, struct epoll_event *eventi, int max, int timeout);
int epoll_pwait(int epfd, struct epoll_event *eventi, int max, int timeout,
                const sigset_t *maschera);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_EPOLL_H */
