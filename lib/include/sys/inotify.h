/* =============================================================================
 * lib/include/sys/inotify.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/inotify.h> (kernel 0.373, voce 85), with Linux's values and layout:
 * Qt's QFileSystemWatcher, a file manager that sees a file appear.
 *
 * The descriptor is the kernel's: read() gives whole struct inotify_event
 * (EINVAL if the first does not fit), poll() POLLIN when one waits,
 * ioctl(FIONREAD) the bytes waiting. Events: IN_CREATE, IN_DELETE,
 * IN_MODIFY, IN_ATTRIB, IN_OPEN, IN_CLOSE_WRITE, IN_CLOSE_NOWRITE,
 * IN_MOVED_FROM/IN_MOVED_TO (one cookie), IN_DELETE_SELF, IN_MOVE_SELF,
 * IN_IGNORED, IN_Q_OVERFLOW; IN_ONESHOT, IN_ONLYDIR, IN_MASK_ADD,
 * IN_MASK_CREATE. IN_ACCESS is accepted and never comes (a read changes
 * nothing worth watching, and would cost on every read).
 *
 * ! A WATCH IS A PATH: a rename carries it along, a delete ends it
 * (IN_DELETE_SELF, IN_IGNORED). EX-OS has no symlinks, and nothing makes
 * hard links, so the path names the file. The queue holds inotify_eventi
 * events (kernel.cfg, default 16384), then one IN_Q_OVERFLOW.
 * ============================================================================= */
#ifndef EXOS_SYS_INOTIFY_H
#define EXOS_SYS_INOTIFY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct inotify_event {
    int      wd;
    uint32_t mask;
    uint32_t cookie;
    uint32_t len;           /* the name with its padding, 0 = no name */
    char     name[];
};

#define IN_ACCESS        0x00000001
#define IN_MODIFY        0x00000002
#define IN_ATTRIB        0x00000004
#define IN_CLOSE_WRITE   0x00000008
#define IN_CLOSE_NOWRITE 0x00000010
#define IN_CLOSE         (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)
#define IN_OPEN          0x00000020
#define IN_MOVED_FROM    0x00000040
#define IN_MOVED_TO      0x00000080
#define IN_MOVE          (IN_MOVED_FROM | IN_MOVED_TO)
#define IN_CREATE        0x00000100
#define IN_DELETE        0x00000200
#define IN_DELETE_SELF   0x00000400
#define IN_MOVE_SELF     0x00000800
#define IN_UNMOUNT       0x00002000
#define IN_Q_OVERFLOW    0x00004000
#define IN_IGNORED       0x00008000
#define IN_ONLYDIR       0x01000000
#define IN_DONT_FOLLOW   0x02000000
#define IN_EXCL_UNLINK   0x04000000
#define IN_MASK_CREATE   0x10000000
#define IN_MASK_ADD      0x20000000
#define IN_ISDIR         0x40000000
#define IN_ONESHOT       0x80000000
#define IN_ALL_EVENTS    0x00000FFF

#define IN_NONBLOCK      0x00000800     /* O_NONBLOCK */
#define IN_CLOEXEC       0x00080000     /* O_CLOEXEC */

int inotify_init(void);
int inotify_init1(int flag);
int inotify_add_watch(int fd, const char *percorso, uint32_t mask);
int inotify_rm_watch(int fd, int wd);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_INOTIFY_H */
