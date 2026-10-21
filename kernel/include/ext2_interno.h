/* =============================================================================
 * kernel/include/ext2_interno.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * What kernel/fs/ext2.c (reading, in the kernel) and /dev/ext4.mod (writing,
 * a module: kernel/moduli/ext4/) share. Nobody else includes this file.
 *
 * ! THE MODULE USES THE KERNEL'S BUFFERS AND ITS INDIRECT-BLOCK CACHE, not
 * copies of its own. The kernel keeps the last block of pointers it read in
 * b_ind: if the module rewrote that block through a buffer of its own, the
 * next read in the kernel would map the file with the old pointers. One
 * state, one cache, invalidated in one place (scrivi_blocco).
 *
 * ! THE LAYOUT IS A CONTRACT. A module built for another layout would read
 * Ext2Mount fields at the wrong offsets and write blocks computed from them.
 * The kernel's interface carries EXT2_IF_ABI and the sizes of Ext2Mount and
 * Ext2Stato; the module compares them with its own before touching
 * anything, and the trailer of the module (kernel/loader/modulo.c) ties it
 * to one kernel version anyway. Change a field here: raise EXT2_IF_ABI.
 * ============================================================================= */
#ifndef EXT2_INTERNO_H
#define EXT2_INTERNO_H

#include "kernel.h"
#include "ext2.h"

#define EXT2_IF_ABI         3u          /* 3: Ext2Ops.chiudi (kernel 0.264) */
#define EXT2_BLOCCO_MAX     4096u   /* the largest block this driver accepts */

/* --- the on-disk format, as both halves need it ----------------------------- */
#define EXT2_MAGIC          0xEF53u
#define SUPER_OFFSET        1024u   /* il superblocco sta SEMPRE qui */
#define INODE_RADICE        2u
#define BLOCCO_MAX          EXT2_BLOCCO_MAX

#define COMPAT_DIR_PREALLOC 0x0001u
#define COMPAT_IMAGIC       0x0002u
#define COMPAT_HAS_JOURNAL  0x0004u
#define COMPAT_EXT_ATTR     0x0008u
#define COMPAT_RESIZE_INODE 0x0010u
#define COMPAT_DIR_INDEX    0x0020u
#define COMPAT_SPARSE_SUPER2 0x0200u
#define COMPAT_FAST_COMMIT  0x0400u
#define COMPAT_ORPHAN_FILE  0x1000u

#define INCOMPAT_FILETYPE   0x0002u
#define INCOMPAT_RECOVER    0x0004u
#define INCOMPAT_JOURNAL_DEV 0x0008u
#define INCOMPAT_META_BG    0x0010u
#define INCOMPAT_EXTENTS    0x0040u
#define INCOMPAT_64BIT      0x0080u
#define INCOMPAT_MMP        0x0100u
#define INCOMPAT_FLEX_BG    0x0200u
#define INCOMPAT_EA_INODE   0x0400u
#define INCOMPAT_DIRDATA    0x1000u
#define INCOMPAT_CSUM_SEED  0x2000u
#define INCOMPAT_LARGEDIR   0x4000u
#define INCOMPAT_INLINE     0x8000u

#define RO_COMPAT_SPARSE    0x0001u
#define RO_COMPAT_LARGEFILE 0x0002u
#define RO_COMPAT_BTREE_DIR 0x0004u
#define RO_COMPAT_HUGE_FILE 0x0008u
#define RO_COMPAT_GDT_CSUM  0x0010u
#define RO_COMPAT_DIR_NLINK 0x0020u
#define RO_COMPAT_EXTRA_ISIZE 0x0040u
#define RO_COMPAT_QUOTA     0x0100u
#define RO_COMPAT_BIGALLOC  0x0200u
#define RO_COMPAT_METADATA_CSUM 0x0400u
#define RO_COMPAT_READONLY  0x1000u
#define RO_COMPAT_PROJECT   0x2000u
#define RO_COMPAT_VERITY    0x8000u
#define RO_COMPAT_ORPHAN_PRESENT 0x10000u

#define INODE_FL_INDEX      0x00001000u     /* i_flags: a hashed-index directory */
#define INODE_FL_EXTENTS    0x00080000u     /* i_flags: i_block is an extent tree */
#define INODE_FL_INLINE     0x10000000u     /* i_flags: the data is in the inode */
#define EXTENT_MAGIC        0xF30Au

/* i_mode: i quattro bit alti sono il tipo */
#define MODE_TIPO           0xF000u
#define MODE_DIR            0x4000u
#define MODE_FILE           0x8000u

/* file_type nelle voci di directory, quando FILETYPE e' attiva */
#define DIRTIPO_FILE        1u
#define DIRTIPO_DIR         2u

/* ! I DATI DI UN FILE A CORSE (0.235, @EXILLA-DISCO): up to this many
 * consecutive blocks in one disk command, read or written. */
#define CORSA_MAX           64u

/* --- lettura e scrittura little-endian non allineata ------------------------- */
static inline uint16_t le16(const uint8_t *p)
{
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

static inline uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void p16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

static inline void p32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;         p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Why a volume is read-only (Ext2Mount.sola_lettura is their OR). */
#define EXT2_RO_FUNZIONI    0x01u   /* features the write module cannot handle */
#define EXT2_RO_MODULO      0x02u   /* no write module registered yet */
#define EXT2_RO_GIORNALE    0x04u   /* journal needs recovery, not replayed */

typedef struct {
    uint8_t  usato;
    int      blkdev;

    uint32_t dim_blocco;
    uint32_t sett_per_blocco;
    uint32_t primo_dato;
    uint32_t blocchi_per_gruppo;
    uint32_t inode_per_gruppo;
    uint32_t dim_inode;
    uint32_t n_gruppi;
    uint32_t n_blocchi;
    uint32_t n_inode;
    uint32_t blocchi_liberi;
    uint32_t inode_liberi;
    uint32_t primo_inode;          /* s_first_ino: sotto sono riservati */
    uint32_t punt_per_blocco;      /* dim_blocco / 4 */
    uint32_t gdt_primo;            /* primo blocco della tabella descrittori */
    uint32_t dim_desc;             /* bytes per group descriptor: 32, or s_desc_size (64BIT) */
    uint32_t compat;               /* the superblock's three feature sets, */
    uint32_t incompat;             /* as read at mount: the module decides */
    uint32_t ro_compat;            /* from these what it can write */
    uint8_t  sola_lettura;         /* EXT2_RO_* bits; 0 = writable */

    char     etichetta[17];
} Ext2Mount;

/* The kernel's working buffers - one per kind of block, see "I BUFFER DI
 * LAVORO" in ext2.c - and the cache of the last block of pointers. */
typedef struct {
    uint8_t  b_dati[EXT2_BLOCCO_MAX];   /* file data, or a directory block */
    uint8_t  b_ind [EXT2_BLOCCO_MAX];   /* a block of pointers (cached) */
    uint8_t  b_ino [EXT2_BLOCCO_MAX];   /* a block of the inode table */
    uint8_t  b_ext [EXT2_BLOCCO_MAX];   /* a block of the extent tree */
    uint8_t  b_desc[EXT2_BLOCCO_MAX];   /* a block of group descriptors */
    uint32_t ind_num;                   /* which block b_ind holds... */
    int      ind_mnt;                   /* ...of which mount; -1 = none */
    int      ind_sporco;                /* b_ind has pointers not yet on disk */
} Ext2Stato;

/* What the write module gives the kernel (Ext2Interfaccia.registra). The
 * three masks are the features it can WRITE; monta() is asked for every
 * volume that has only those, and says yes (0) or no (<0, with a reason in
 * the log) - a journal to replay, a checksum it cannot verify. */
typedef struct {
    uint32_t    abi;                    /* EXT2_IF_ABI */
    const char *nome;

    uint32_t    compat, incompat, ro_compat;

    int  (*monta)   (int mnt);
    void (*smonta)  (int mnt);

    int  (*create)  (int mnt, const char *p, uint32_t uid, uint32_t gid);
    int  (*chown)   (int mnt, const char *p, uint32_t uid, uint32_t gid);
    int  (*chmod)   (int mnt, const char *p, uint32_t modo);
    int  (*mkdir)   (int mnt, const char *p, uint32_t uid, uint32_t gid);
    int  (*rmdir)   (int mnt, const char *p);
    int  (*unlink)  (int mnt, const char *p);
    int  (*rename)  (int mnt, const char *da, const char *a);
    int  (*write)   (int mnt, const char *p, const void *buf, uint32_t size,
                     uint32_t offset);
    int  (*truncate)(int mnt, const char *p, uint32_t nuova_dim);
    int  (*sync)    (int mnt);
    /* A file of this volume was closed (its last descriptor). What the
     * module keeps between write()s - blocks set aside for a file being
     * appended to, the superblock's counters - goes to the disk here. */
    void (*chiudi)  (int mnt);
} Ext2Ops;

/* What the kernel gives the write module: found by name ("ext2") through
 * the module table (modulo.h). Everything a write needs from the read side,
 * so the module does not carry a second copy of the reading code. */
typedef struct {
    uint32_t    abi;                    /* EXT2_IF_ABI */
    uint32_t    dim_mount;              /* sizeof(Ext2Mount) */
    uint32_t    dim_stato;              /* sizeof(Ext2Stato) */
    uint32_t    max_mount;              /* EXT2_MAX_MOUNT */

    Ext2Mount  *mnt;                    /* [max_mount] */
    Ext2Stato  *st;

    int      (*leggi_blocco)   (Ext2Mount *m, uint32_t n, void *dst);
    int      (*scrivi_blocco)  (Ext2Mount *m, uint32_t n, const void *src);
    int      (*scrivi_corsa)   (Ext2Mount *m, uint32_t n, uint32_t quanti,
                                const void *src);
    int      (*leggi_indiretto)(Ext2Mount *m, int mnt, uint32_t n);
    int      (*ind_riversa)    (Ext2Mount *m);
    int      (*leggi_inode)    (Ext2Mount *m, uint32_t numero, uint8_t *dst);
    uint32_t (*mappa_blocco)   (Ext2Mount *m, int mnt, const uint8_t *inode,
                                uint32_t n);
    uint32_t (*cerca_voce)     (Ext2Mount *m, int mnt, const uint8_t *inode_dir,
                                const char *seg, uint32_t seg_len);
    uint32_t (*risolvi)        (Ext2Mount *m, int mnt, const char *percorso,
                                uint8_t *out);

    int      (*blk_read)       (int dev, uint64_t lba, uint32_t n, void *buf);
    int      (*blk_write)      (int dev, uint64_t lba, uint32_t n, const void *buf);
    int      (*blk_flush)      (int dev);
    uint32_t (*ora_unix)       (void);

    int      (*registra)       (const Ext2Ops *ops);
} Ext2Interfaccia;

#endif /* EXT2_INTERNO_H */
