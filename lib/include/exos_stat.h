/* =============================================================================
 * lib/include/exos_stat.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * struct stat, da sola. La includono libc.h (per i programmi di EX-OS) e
 * <sys/stat.h> (per tutti): vedi il commento in libc.h sul perche' il codice
 * di terzi la vede solo da <sys/stat.h>. Il commento sui campi sta in libc.h,
 * accanto a stat().
 * ============================================================================= */

#ifndef EXOS_STAT_H
#define EXOS_STAT_H

struct stat {
    dev_t           st_dev;     /* sempre 0: EX-OS non numera i volumi */
    ino_t           st_ino;     /* primo cluster/inode, 0 dove non si applica */
    mode_t          st_mode;    /* tipo | permessi del volume (0.339) */
    nlink_t         st_nlink;   /* ext2/ext4 i_links_count, 1 altrove */
    uid_t           st_uid;     /* ext2/ext4 e /proc, 0 su FAT e ISO */
    gid_t           st_gid;
    off_t           st_size;
    blksize_t       st_blksize; /* 512: il settore, l'unita' vera dei nostri fs */
    blkcnt_t        st_blocks;  /* settori da 512 occupati, arrotondati per eccesso */
    time_t          st_atime;   /* ext2/ext4 i_atime, = st_mtime altrove */
    time_t          st_mtime;
    time_t          st_ctime;   /* ext2/ext4 i_ctime, = st_mtime altrove */
    /* (7/11/2026, Mesa) at the end, so the fields before keep their place:
     * the identity of a character device (st_ino), 0 for every other file */
    dev_t           st_rdev;
    /* (18/11/2026, GNU nano) the times as POSIX 2008 has them, after
     * st_rdev for the same reason; st_atime & c. stay and say the same
     * seconds. tv_nsec is 0: no filesystem here keeps nanoseconds. */
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
};

/* The same with a 64-bit st_size: stat64() and the others fill it; with
 * _FILE_OFFSET_BITS=64 on the 32-bit system struct stat is laid out the
 * same (off_t is 64 bits there), and stat() is stat64(). */
struct stat64 {
    dev_t           st_dev;
    ino_t           st_ino;
    mode_t          st_mode;
    nlink_t         st_nlink;
    uid_t           st_uid;
    gid_t           st_gid;
    off64_t         st_size;
    blksize_t       st_blksize;
    blkcnt_t        st_blocks;
    time_t          st_atime;
    time_t          st_mtime;
    time_t          st_ctime;
    dev_t           st_rdev;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;
};

#endif /* EXOS_STAT_H */
