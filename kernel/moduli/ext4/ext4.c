/* =============================================================================
 * kernel/moduli/ext4/ext4.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * /dev/ext4.mod: the module that WRITES ext2/ext3/ext4 (kernel 0.263). The
 * kernel reads those volumes by itself (kernel/fs/ext2.c); this module is
 * loaded right after the root is mounted (kernel/loader/modulo.c) and
 * registers the write operations. Here: the entry point, the checks that
 * the kernel is the one it was built for, and what it can write.
 *
 *     EXT4MOD_VERSIONE 0.004 (+0.001 a ogni modifica)
 *     0.004: ext4 - extents (estesi.c), metadata_csum and uninit_bg
 *            (somme.c), 64-byte descriptors, flex_bg, groups never
 *            initialised, extra_isize, dir_nlink, the journal's CSUM_V3;
 *            the superblock goes through the journal and a long write()
 *            is cut into transactions of its own (@EXT4-SCRITTURA).
 *     0.003: ext3 - the journal (giornale.c): replayed at mount, every
 *            change to the metadata written there first (@EXT3-GIORNALE).
 *     0.002: the blocks set aside for a file being appended to stay between
 *            write()s; the superblock's counters are written when the file
 *            is closed (scrittura.c, «LA PRENOTAZIONE RESTA»).
 * ============================================================================= */

#include "ext4.h"

#define EXT4MOD_VERSIONE "0.004"

const ModuloKernel    *MK = NULL;
const Ext2Interfaccia *K  = NULL;

/* =============================================================================
 * WHAT THIS MODULE CAN WRITE
 *
 * The kernel compares every bit of a volume's three feature sets with these
 * (ext2.c, valuta): one bit outside them and the volume stays read-only.
 * A compat bit is listed only when writing past it is safe:
 *   DIR_PREALLOC, IMAGIC   hints nobody acts on;
 *   EXT_ATTR               attributes are not written, and the block of a
 *                          deleted file's attributes is released (scrittura.c);
 *   RESIZE_INODE           the reserved descriptor blocks are already marked
 *                          in use: the allocator never hands them out;
 *   DIR_INDEX              a directory with a hashed index loses it before it
 *                          is changed (the INDEX flag cleared: the blocks read
 *                          as an ordinary directory), as Linux does;
 *   SPARSE_SUPER2          only says where the backups are;
 *   HAS_JOURNAL            ext3: giornale.c writes it (0.003), and monta()
 *                          refuses a journal it cannot write;
 *   ORPHAN_FILE            ext4's list of files deleted while open: this
 *                          module never puts one there, and a volume whose
 *                          list is not empty (ro_compat ORPHAN_PRESENT) stays
 *                          read-only - nothing below lets that bit through.
 * And incompat RECOVER (needs_recovery): the journal is replayed at mount.
 *
 * ext4 (0.004): EXTENTS (estesi.c), 64BIT (with fewer than 2^32 blocks: the
 * kernel refuses more), FLEX_BG (bitmaps and tables of a group elsewhere:
 * every address is read from the descriptor), CSUM_SEED; ro_compat
 * HUGE_FILE (an inode marked huge_file is not written), GDT_CSUM,
 * METADATA_CSUM (somme.c), DIR_NLINK, EXTRA_ISIZE. Not: inline data,
 * bigalloc, quota, encryption, casefold, large directories, MMP, meta_bg.
 * ============================================================================= */
#define COMPAT_SCRIVE    (COMPAT_DIR_PREALLOC | COMPAT_IMAGIC | COMPAT_EXT_ATTR | \
                          COMPAT_RESIZE_INODE | COMPAT_DIR_INDEX | COMPAT_SPARSE_SUPER2 | \
                          COMPAT_HAS_JOURNAL | COMPAT_ORPHAN_FILE)
#define INCOMPAT_SCRIVE  (INCOMPAT_FILETYPE | INCOMPAT_RECOVER | INCOMPAT_EXTENTS | \
                          INCOMPAT_64BIT | INCOMPAT_FLEX_BG | INCOMPAT_CSUM_SEED)
#define RO_COMPAT_SCRIVE (RO_COMPAT_SPARSE | RO_COMPAT_LARGEFILE | RO_COMPAT_HUGE_FILE | \
                          RO_COMPAT_GDT_CSUM | RO_COMPAT_DIR_NLINK | \
                          RO_COMPAT_EXTRA_ISIZE | RO_COMPAT_METADATA_CSUM)

static int monta(int mnt)
{
    if (mnt < 0 || (uint32_t)mnt >= K->max_mount) return -1;
    if (vol_monta(mnt) != 0) return -1;
    return giornale_monta(mnt);
}

/* Blocks set aside for an append go back first: their bitmap is a change
 * like the others, and on ext3 it goes into the transaction committed here. */
static int sincronizza(int mnt)
{
    if (mnt < 0 || (uint32_t)mnt >= K->max_mount) return -1;
    prenotazione_chiudi();
    if (giornale_commit(mnt) != 0) return -1;
    return blk_flush(K->mnt[mnt].blkdev);
}

static void smonta(int mnt)
{
    prenotazione_chiudi();
    giornale_smonta(mnt);
}

static void chiudi(int mnt)
{
    prenotazione_chiudi();
    giornale_commit(mnt);
}

/* Every operation but write() starts by giving back the blocks set aside
 * for an append: alloca_blocco would hand them to a directory, or truncate
 * would cut a file whose next blocks are still marked as its own.
 * ! ON ext3 EACH ONE IS A TRANSACTION, committed before it returns: an
 * operation that frees blocks must be on the disk before anything can
 * write data into them (see giornale.c). write() alone leaves it open. */
#define OPERAZIONE(mnt, chiamata)                                   \
    do {                                                            \
        int r_;                                                     \
        prenotazione_chiudi();                                      \
        if (giornale_inizio(mnt) != 0) return -1;                   \
        r_ = (chiamata);                                            \
        if (giornale_fine(mnt, 1) != 0) return -1;                  \
        return r_;                                                  \
    } while (0)

static int op_create(int mnt, const char *p, uint32_t uid, uint32_t gid)
{ OPERAZIONE(mnt, ext2_create(mnt, p, uid, gid)); }

static int op_chown(int mnt, const char *p, uint32_t uid, uint32_t gid)
{ OPERAZIONE(mnt, ext2_chown(mnt, p, uid, gid)); }

static int op_chmod(int mnt, const char *p, uint32_t modo)
{ OPERAZIONE(mnt, ext2_chmod(mnt, p, modo)); }

static int op_mkdir(int mnt, const char *p, uint32_t uid, uint32_t gid)
{ OPERAZIONE(mnt, ext2_mkdir(mnt, p, uid, gid)); }

static int op_rmdir(int mnt, const char *p)
{ OPERAZIONE(mnt, ext2_rmdir(mnt, p)); }

static int op_unlink(int mnt, const char *p)
{ OPERAZIONE(mnt, ext2_unlink(mnt, p)); }

static int op_rename(int mnt, const char *da, const char *a)
{ OPERAZIONE(mnt, ext2_rename(mnt, da, a)); }

static int op_truncate(int mnt, const char *p, uint32_t nuova_dim)
{ OPERAZIONE(mnt, ext2_truncate(mnt, p, nuova_dim)); }

/* write(): the transaction stays open for the next one - committed at the
 * file's close, when half full, after 5 seconds (giornale_fine).
 *
 * ! ON A JOURNAL, IN PIECES OF PEZZO_GIORNALE (0.004). A write() of many
 * megabytes changed more metadata than one transaction holds, and the
 * journal committed in the middle of it - a crash between two such commits
 * left blocks marked used and owned by nobody. Each piece is one ext2_write
 * now, and giornale_inizio before it makes room (committing what is there):
 * every transaction holds whole pieces. 256 KB change at most a dozen
 * blocks of metadata (bitmaps, descriptors, pointer or extent blocks, the
 * inode, the superblock), well inside GIO_RISERVA. */
#define PEZZO_GIORNALE 262144u

static int op_write(int mnt, const char *p, const void *buf, uint32_t size,
                    uint32_t offset)
{
    uint32_t fatti = 0;
    int      r;

    if (!giornale_attivo(mnt)) return ext2_write(mnt, p, buf, size, offset);

    do {
        uint32_t q = size - fatti;

        if (q > PEZZO_GIORNALE) q = PEZZO_GIORNALE;
        if (giornale_inizio(mnt) != 0) return fatti ? (int)fatti : -1;
        r = ext2_write(mnt, p, (const uint8_t *)buf + fatti, q, offset + fatti);
        if (giornale_fine(mnt, 0) != 0) return -1;
        if (r <= 0) return fatti ? (int)fatti : r;
        fatti += (uint32_t)r;
        if ((uint32_t)r < q) break;
    } while (fatti < size);
    return (int)fatti;
}

static const Ext2Ops g_ops = {
    EXT2_IF_ABI,
    "ext4.mod",
    COMPAT_SCRIVE,
    INCOMPAT_SCRIVE,
    RO_COMPAT_SCRIVE,
    monta,
    smonta,
    op_create,
    op_chown,
    op_chmod,
    op_mkdir,
    op_rmdir,
    op_unlink,
    op_rename,
    op_write,
    op_truncate,
    sincronizza,
    chiudi,
    giornale_leggi,
    giornale_scrivi,
    giornale_dati,
};

/* =============================================================================
 * The entry point (the linker's -e). Nothing is registered unless every
 * check has passed: a module that refuses leaves the kernel as it was.
 * ============================================================================= */
int modulo_ingresso(const ModuloKernel *k)
{
    if (k == NULL || k->abi != MODULO_ABI || k->dim < sizeof(ModuloKernel)) return -1;
    MK = k;

    K = (const Ext2Interfaccia *)k->interfaccia("ext2", EXT2_IF_ABI);
    if (K == NULL || K->dim_mount != sizeof(Ext2Mount) ||
        K->dim_stato != sizeof(Ext2Stato) || K->max_mount != EXT2_MAX_MOUNT) {
        klog(LOG_ERROR, "ext4.mod: the kernel's ext2 interface is not the one this "
                        "module was built for (abi %u, Ext2Mount %u bytes; expected "
                        "%u, %u)", K ? K->abi : 0u, K ? K->dim_mount : 0u,
             EXT2_IF_ABI, (uint32_t)sizeof(Ext2Mount));
        K = NULL;
        return -2;
    }

    klog(LOG_INFO, "ext4.mod %s: ext2, ext3 and ext4 writing", EXT4MOD_VERSIONE);
    return K->registra(&g_ops);
}

/* =============================================================================
 * memcpy & co. Plain loops: the module is small, and these are called on
 * one block at a time.
 * ============================================================================= */
void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    if (d < s) while (n--) *d++ = *s++;
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}

void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;

    while (n--) *d++ = (uint8_t)c;
    return dst;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;

    for (; n; n--, x++, y++) if (*x != *y) return (int)*x - (int)*y;
    return 0;
}
