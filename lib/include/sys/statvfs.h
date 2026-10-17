/* =============================================================================
 * <sys/statvfs.h> — information on a mounted filesystem
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * ! THE NUMBERS ARE NOT KNOWN YET (16 October 2026, LLVM for EX-OS): no
 * system call says how big a volume is or how much is free. statvfs() and
 * fstatvfs() check that the path or descriptor exists and fill in a block
 * size and the name length; the counts are 0, which callers read as
 * "unknown". The layout is glibc's at 64 bit.
 * ============================================================================= */
#ifndef EXOS_SYS_STATVFS_H
#define EXOS_SYS_STATVFS_H

#include "../libc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long fsblkcnt_t;
typedef unsigned long fsfilcnt_t;

struct statvfs {
    unsigned long f_bsize;
    unsigned long f_frsize;
    fsblkcnt_t    f_blocks;
    fsblkcnt_t    f_bfree;
    fsblkcnt_t    f_bavail;
    fsfilcnt_t    f_files;
    fsfilcnt_t    f_ffree;
    fsfilcnt_t    f_favail;
    unsigned long f_fsid;
    /* ! TWO NAMES FOR ONE FIELD: f_flag is POSIX's, f_flags the one LLVM
     * reads on every system it does not know by name (Path.inc). */
    union { unsigned long f_flag; unsigned long f_flags; };
    unsigned long f_namemax;
    int           __riserva[6];
};

#define ST_RDONLY 1
#define ST_NOSUID 2
/* BSD's "this volume is local": every EX-OS volume is (statvfs sets it). */
#define MNT_LOCAL 0x1000

int statvfs(const char *percorso, struct statvfs *v);
int fstatvfs(int fd, struct statvfs *v);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_STATVFS_H */
