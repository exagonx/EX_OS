/* =============================================================================
 * kernel/include/inotify.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * inotify (kernel 0.373, voce 85: Qt's QFileSystemWatcher, the file manager
 * that sees a file appear without looking every second).
 *
 * An FD_INOTIFY is an object of kernel/fs/inotify.c: the watches (a path, a
 * mask, a wd) and a queue of events read as Linux's struct inotify_event.
 * The VFS tells it what changed (inotify_avvisa, inotify_rinomina) after
 * every operation that worked; poll sees it ready when the queue has
 * something; FIONREAD gives the bytes waiting.
 *
 * ! A WATCH IS A PATH, NOT AN INODE. EX-OS has no hard links that programs
 * make (ext2 can hold them, nothing here makes them) and no symlinks, so
 * the path names the file. A rename moves the watches with it (and those
 * below a renamed directory); a file deleted takes its watch away with
 * IN_DELETE_SELF and IN_IGNORED, as on Linux.
 *
 * ! NOTHING TO PAY WHEN NOBODY WATCHES: the VFS asks inotify_attivo()
 * (one global count) before it builds a path.
 *
 * ! THE QUEUE HAS THE CEILING THE CONFIGURATION SAYS: inotify_eventi in
 * kernel.cfg (default 16384, Linux's max_queued_events). Past it one
 * IN_Q_OVERFLOW and the rest dropped, as on Linux: a program that never
 * reads cannot eat the kernel's memory.
 * ============================================================================= */
#ifndef INOTIFY_H
#define INOTIFY_H

#include "kernel.h"

/* Linux's numbers: programs built for <sys/inotify.h> use them */
#define IN_ACCESS        0x00000001u
#define IN_MODIFY        0x00000002u
#define IN_ATTRIB        0x00000004u
#define IN_CLOSE_WRITE   0x00000008u
#define IN_CLOSE_NOWRITE 0x00000010u
#define IN_OPEN          0x00000020u
#define IN_MOVED_FROM    0x00000040u
#define IN_MOVED_TO      0x00000080u
#define IN_CREATE        0x00000100u
#define IN_DELETE        0x00000200u
#define IN_DELETE_SELF   0x00000400u
#define IN_MOVE_SELF     0x00000800u
#define IN_UNMOUNT       0x00002000u
#define IN_Q_OVERFLOW    0x00004000u
#define IN_IGNORED       0x00008000u
#define IN_ALL_EVENTS    0x00000FFFu
#define IN_ONLYDIR       0x01000000u
#define IN_DONT_FOLLOW   0x02000000u
#define IN_EXCL_UNLINK   0x04000000u
#define IN_MASK_CREATE   0x10000000u
#define IN_MASK_ADD      0x20000000u
#define IN_ISDIR         0x40000000u
#define IN_ONESHOT       0x80000000u

extern volatile uint32_t g_inotify_sorv;    /* watches in the whole system */
#define inotify_attivo() (g_inotify_sorv != 0)

int      inotify_crea(void);                /* a handle, or -errno */
void     inotify_apri(int h);               /* one more descriptor */
void     inotify_chiudi(int h);
void     inotify_chiudi_locked(int h);      /* interrupts off (process exit) */

/* abs absolute, existing (the caller checked, and IN_ONLYDIR): the wd, or
 * -errno. The same path again changes that watch's mask (IN_MASK_ADD adds,
 * IN_MASK_CREATE says EEXIST). */
int32_t  inotify_guarda(int h, const char *abs, uint32_t mask);
int32_t  inotify_togli(int h, int32_t wd);  /* 0 or -EINVAL; queues IN_IGNORED */

/* read(): whole events into ubuf (verified by the caller), -EINVAL if the
 * first does not fit, -EAGAIN, -EINTR. Interrupts on. */
int32_t  inotify_leggi(int h, char *ubuf, uint32_t n, int nonblocca);
uint32_t inotify_byte(int h);               /* FIONREAD */

/* poll, interrupts off */
int      inotify_pronto_locked(int h);
int      inotify_attesa_registra_locked(int h, uint32_t pid);
void     inotify_attesa_togli_locked(int h, uint32_t pid);

/* From the VFS, after it worked, interrupts on, no VFS lock needed.
 * `mask` is the event as the parent directory sees it (IN_CREATE,
 * IN_DELETE, IN_MODIFY, IN_ATTRIB, IN_OPEN, IN_CLOSE_*), with IN_ISDIR for
 * a directory; the watch of abs itself gets its own form (IN_DELETE ->
 * IN_DELETE_SELF, IN_CREATE nothing, the rest the same). */
void     inotify_avvisa(const char *abs, uint32_t mask);
void     inotify_rinomina(const char *da, const char *a, int cartella);

#endif /* INOTIFY_H */
