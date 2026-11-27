/* =============================================================================
 * <sys/vfs.h> - statfs(), Linux's form (voce 103, 27/11/2026)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The same numbers as statvfs() plus f_type, the kind of filesystem, with
 * Linux's values: Gecko (UnscaledFontFreeType) refuses a font file on
 * tmpfs, /proc or sysfs by it. From kernel 0.373; a 0.372 kernel leaves
 * f_type 0. The layout is glibc's.
 * ============================================================================= */
#ifndef EXOS_SYS_VFS_H
#define EXOS_SYS_VFS_H

#include "../libc.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXOS_FSID_T
#define EXOS_FSID_T
typedef struct { int __val[2]; } fsid_t;
#endif

struct statfs {
    long          f_type;
    long          f_bsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    fsid_t        f_fsid;
    long          f_namelen;
    long          f_frsize;
    long          f_flags;          /* ST_RDONLY when read-only */
    long          f_spare[4];
};

/* The values f_type takes on EX-OS (the names of <linux/magic.h>) */
#define EXT2_SUPER_MAGIC   0xEF53   /* ext2, ext3, ext4 */
#define EXT3_SUPER_MAGIC   0xEF53
#define EXT4_SUPER_MAGIC   0xEF53
#define MSDOS_SUPER_MAGIC  0x4d44   /* FAT12/16/32 */
#define ISOFS_SUPER_MAGIC  0x9660   /* CD and DVD */
#define PROC_SUPER_MAGIC   0x9fa0   /* /proc */
/* not on EX-OS, named for programs that refuse them (Gecko's fonts) */
#define TMPFS_MAGIC        0x01021994
#define SYSFS_MAGIC        0x62656572

int statfs(const char *percorso, struct statfs *v);
int fstatfs(int fd, struct statfs *v);
#define statfs64  statfs
#define fstatfs64 fstatfs

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_VFS_H */
