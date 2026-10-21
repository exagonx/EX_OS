/* =============================================================================
 * kernel/moduli/ext4/ext4.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * What every file of /dev/ext4.mod sees. The module links with NOTHING
 * (kernel/loader/modulo.c refuses a module with a symbol to resolve), so
 * the names the write code has always used - leggi_blocco, b_dati, klog,
 * blk_write... - are turned here into calls through the kernel's tables:
 * the code moved out of kernel/fs/ext2.c reads as it did there.
 *
 * ! THE BUFFERS AND THE POINTER CACHE ARE THE KERNEL'S (K->st): see
 * kernel/include/ext2_interno.h for why there must be only one of each.
 * ============================================================================= */
#ifndef EXT4_MOD_H
#define EXT4_MOD_H

#include "kernel.h"
#include "ext2_interno.h"
#include "modulo.h"

extern const ModuloKernel    *MK;   /* the module table */
extern const Ext2Interfaccia *K;    /* the read side of ext2, in the kernel */

#define klog                (MK->klog)

#define leggi_blocco        (K->leggi_blocco)
#define scrivi_blocco       (K->scrivi_blocco)
#define scrivi_corsa        (K->scrivi_corsa)
#define leggi_indiretto     (K->leggi_indiretto)
#define ind_riversa         (K->ind_riversa)
#define leggi_inode         (K->leggi_inode)
#define mappa_blocco        (K->mappa_blocco)
#define cerca_voce          (K->cerca_voce)
#define risolvi             (K->risolvi)
#define blk_read            (K->blk_read)
#define blk_write           (K->blk_write)
#define blk_flush           (K->blk_flush)
#define unix_ora_corrente   (K->ora_unix)

#define b_dati              (K->st->b_dati)
#define b_ind               (K->st->b_ind)
#define b_ino               (K->st->b_ino)
#define b_ext               (K->st->b_ext)
#define b_desc              (K->st->b_desc)
#define g_ind_num           (K->st->ind_num)
#define g_ind_mnt           (K->st->ind_mnt)
#define g_ind_sporco        (K->st->ind_sporco)

/* The mount, for a write: NULL unless the kernel has made it writable. The
 * kernel checks first (ext2.c, prendi_scrivibile); this is the second lock. */
Ext2Mount *prendi_scrivibile(int mnt);

/* The write operations (scrittura.c), registered by ext4.c. */
int ext2_create  (int mnt, const char *percorso, uint32_t uid, uint32_t gid);
int ext2_chown   (int mnt, const char *percorso, uint32_t uid, uint32_t gid);
int ext2_chmod   (int mnt, const char *percorso, uint32_t modo);
int ext2_mkdir   (int mnt, const char *percorso, uint32_t uid, uint32_t gid);
int ext2_rmdir   (int mnt, const char *percorso);
int ext2_unlink  (int mnt, const char *percorso);
int ext2_rename  (int mnt, const char *da, const char *a);
int ext2_write   (int mnt, const char *percorso, const void *buf, uint32_t size,
                  uint32_t offset);
int ext2_truncate(int mnt, const char *percorso, uint32_t nuova_dim);

/* Blocks set aside for a file being appended to, and the superblock's
 * counters, to the disk (scrittura.c, «LA PRENOTAZIONE RESTA»). */
void prenotazione_chiudi(void);

/* The ext3 journal (giornale.c). giornale_monta: 0 = writable, <0 =
 * read-only; the others answer 0 at once for a volume with no journal. */
int  giornale_monta  (int mnt);
void giornale_smonta (int mnt);
int  giornale_commit (int mnt);
int  giornale_inizio (int mnt);
int  giornale_fine   (int mnt, int subito);
void giornale_leggi  (Ext2Mount *m, uint32_t n, uint32_t quanti, void *dst);
int  giornale_scrivi (Ext2Mount *m, uint32_t n, const void *src);
int  giornale_dati   (Ext2Mount *m, uint32_t n, uint32_t quanti);

/* gcc may turn a loop or a structure copy into a call to these: the module
 * has its own (ext4.c), since it can call nothing in the kernel by name. */
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

#endif /* EXT4_MOD_H */
