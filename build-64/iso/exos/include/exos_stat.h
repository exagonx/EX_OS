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
    mode_t          st_mode;    /* tipo | permessi ricostruiti */
    nlink_t         st_nlink;   /* sempre 1 */
    uid_t           st_uid;     /* sempre 0: non ci sono utenti */
    gid_t           st_gid;     /* sempre 0 */
    off_t           st_size;
    blksize_t       st_blksize; /* 512: il settore, l'unita' vera dei nostri fs */
    blkcnt_t        st_blocks;  /* settori da 512 occupati, arrotondati per eccesso */
    time_t          st_atime;   /* = st_mtime: non si tiene l'ultimo accesso */
    time_t          st_mtime;
    time_t          st_ctime;   /* = st_mtime */
};

#endif /* EXOS_STAT_H */
