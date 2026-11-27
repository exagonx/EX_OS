/* =============================================================================
 * lib/include/sys/stat.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under la GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/stat.h> — che cos'e' un file.
 *
 * Facciata su libc.h, dove stanno struct stat, stat(), fstat(), le macro
 * S_IS* e statraw() — quest'ultima per chi vuole i campi grezzi del
 * filesystem invece della forma POSIX.
 *
 * I PERMESSI. Dalla 0.339 st_mode, st_uid, st_gid e st_nlink sono quelli
 * del volume: su ext2 e ext4 quelli dell'inode, che chmod() cambia. FAT e
 * ISO 9660 non li hanno: li' st_mode viene dall'attributo di sola lettura
 * e il proprietario e' 0.
 *
 * fchmod() e umask() esistono ma non cambiano niente: le ha fatte entrare
 * bfd, che chiude ogni file eseguibile che produce con umask/chmod.
 * ============================================================================= */

#ifndef EXOS_SYS_STAT_H
#define EXOS_SYS_STAT_H

#include "../libc.h"
#include "../exos_stat.h"

/* Bit dei permessi: su ext2 e ext4 quelli dell'inode (vedi sopra). */
#define S_IRWXU     0700
#define S_IRUSR     0400
#define S_IWUSR     0200
#define S_IXUSR     0100
#define S_IRWXG     0070
#define S_IRGRP     0040
#define S_IWGRP     0020
#define S_IXGRP     0010
#define S_IRWXO     0007
#define S_IROTH     0004
#define S_IWOTH     0002
#define S_IXOTH     0001

/* Tipi che EX-OS non crea: collegamenti simbolici, pipe con un nome,
 * dispositivi nel filesystem. Su un ext2 fatto con Linux stat() riporta
 * il tipo dell'inode, anche un collegamento, che EX-OS non segue. */
#define S_IFCHR     0020000
#define S_IFBLK     0060000
#define S_IFIFO     0010000
#define S_IFLNK     0120000
#define S_IFSOCK    0140000

#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

/* The dates of a file (kernel 0.373): seconds kept, nanoseconds not (ext
 * keeps seconds, FAT two of them, and the access only as a day). In tv_nsec,
 * UTIME_NOW = now, UTIME_OMIT = leave it; ts NULL = both now. utimensat:
 * dirfd AT_FDCWD, or any with an absolute path (no directory descriptors). */
#define UTIME_NOW   ((1l << 30) - 1l)
#define UTIME_OMIT  ((1l << 30) - 2l)
#ifdef __cplusplus
extern "C" {
#endif
int utimensat(int dirfd, const char *path, const struct timespec ts[2], int flag);
int futimens(int fd, const struct timespec ts[2]);
#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_STAT_H */
