/* =============================================================================
 * kernel/moduli/ext4/giornale.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * THE ext3 JOURNAL (ext4.mod 0.003, @EXT3-GIORNALE)
 *
 * ext3 is ext2 plus a journal: a file inside the volume (s_journal_inum,
 * usually 8) where every change to the METADATA - bitmaps, descriptors,
 * inode tables, pointer blocks, directories - is written first, as a whole
 * transaction, before any of it reaches its place. A crash in the middle
 * leaves either the transaction complete in the journal (it is applied
 * again at the next mount: "replay") or not complete (it is ignored, and
 * the volume is as before). Never half an operation.
 *
 * ! THE FORMAT IS LINUX'S (jbd2), NOT OURS. A volume EX-OS was writing when
 * the power went must be repaired by whatever mounts it next: Linux, its
 * e2fsck, or EX-OS. So the journal blocks are exactly jbd2's - big-endian
 * headers (magic 0xC03B3998), a descriptor block with one tag per block,
 * the copies, a commit block - and the replay below is jbd2's three passes
 * (scan, revoke, replay) over journals written by Linux.
 *
 * HOW A TRANSACTION GOES (ordered mode, as ext3's default):
 *   - a metadata write does NOT reach the disk: the kernel's scrivi_blocco
 *     hands it here (giornale_scrivi) and a copy is kept in memory; every
 *     read of that block sees the copy (giornale_leggi) - the operation and
 *     the ones after it see their own changes;
 *   - file DATA goes straight to the disk, as on ext2;
 *   - commit: flush (the data is on the disk before the metadata that
 *     points to it), the journal superblock pointing at the transaction,
 *     the descriptor and the copies; flush; the commit block; flush. From
 *     here the transaction exists: a crash means replay;
 *   - checkpoint: every copy to its place, flush, and the journal is marked
 *     empty again (s_start = 0).
 * A transaction is committed at the end of every operation that is not a
 * write(), at a file's close, at sync and unmount, when it is half full,
 * and after 5 seconds; a write() of many megabytes may commit in the middle
 * (the transaction is full): a crash between two such commits leaves at
 * most blocks marked used and owned by nobody, the same as ext2.
 *
 * ! EVERY TRANSACTION STARTS AT s_first. The journal is emptied at every
 * checkpoint, so only the LAST committed transaction can ever be replayed
 * and no revoke records are needed: a block freed and reused for data after
 * that transaction cannot be overwritten by an older copy of it.
 *
 * WHAT IS REFUSED (the volume stays read-only, with the reason in the log):
 * an external journal, a journal with checksums (CSUM_V2/V3, the v1
 * CHECKSUM, ASYNC_COMMIT - ext4's metadata_csum territory), fast commits,
 * a journal in more than GIO_CORSE pieces, a replay that does not fit.
 * ============================================================================= */

#include "ext4.h"

#define GIO_MAGIA           0xC03B3998u
#define GIO_DESCRITTORE     1u
#define GIO_COMMIT          2u
#define GIO_SUPER_V1        3u
#define GIO_SUPER_V2        4u
#define GIO_REVOCA          5u

#define GIO_INC_REVOCA      0x01u
#define GIO_INC_64BIT       0x02u
#define GIO_INC_NOTI        (GIO_INC_REVOCA | GIO_INC_64BIT)

#define TAG_ESCAPE          0x01u
#define TAG_STESSO_UUID     0x02u
#define TAG_ULTIMO          0x08u

#define GIO_CORSE           32u         /* pieces of the journal file on disk */
#define GIO_BYTE            262144u     /* memory for one transaction's copies */
#define GIO_RISERVA         32u         /* free slots an operation starts with */
#define GIO_SECONDI         5u          /* an open transaction commits after this */
#define GIO_REVOCHE         512u        /* revoke records a replay can hold */

#define SB_JOURNAL_INUM     0xE0u
#define SB_JOURNAL_DEV      0xE4u

typedef struct {
    uint32_t log, fis, n;               /* journal blocks log..log+n-1 are at fis.. */
} Corsa;

typedef struct {
    uint32_t  maxlen, primo;            /* s_maxlen, s_first */
    uint32_t  seq;                      /* the next transaction's number */
    uint32_t  incompat;
    uint32_t  tag;                      /* bytes per descriptor tag: 8 or 12 */
    uint8_t   uuid[16];

    Corsa     corse[GIO_CORSE];
    uint32_t  n_corse;

    uint32_t  max_slot, n_slot;
    uint32_t *slot_blk;                 /* [max_slot] which metadata block... */
    uint8_t  *slot_dati;                /* [max_slot * block] ...and its copy */
    uint8_t  *jsb;                      /* the journal superblock, as on disk */
    uint8_t  *buf;                      /* a descriptor, a commit, a copy */
    uint32_t  aperto;                   /* when the open transaction began */
    int       guasto;                   /* a commit failed: no more writes */
} Giornale;

static Giornale *g_gio[EXT2_MAX_MOUNT];

/* --- big-endian, as jbd2 writes everything ---------------------------------- */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static uint32_t be16(const uint8_t *p)
{
    return ((uint32_t)p[0] << 8) | (uint32_t)p[1];
}

static void pbe32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void pbe16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}

static int indice_di(const Ext2Mount *m)
{
    int i = (int)(m - K->mnt);

    return (i >= 0 && i < EXT2_MAX_MOUNT) ? i : -1;
}

static Giornale *di(const Ext2Mount *m)
{
    int i = indice_di(m);

    return (i >= 0) ? g_gio[i] : NULL;
}

/* --- the journal's own blocks ------------------------------------------------ */
static uint32_t fisico(const Giornale *g, uint32_t log)
{
    uint32_t i;

    for (i = 0; i < g->n_corse; i++)
        if (log >= g->corse[i].log && log < g->corse[i].log + g->corse[i].n)
            return g->corse[i].fis + (log - g->corse[i].log);
    return 0;
}

static int gio_leggi(Ext2Mount *m, const Giornale *g, uint32_t log, void *dst)
{
    uint32_t f = fisico(g, log);

    if (f == 0) return -1;
    return blk_read(m->blkdev, (uint64_t)f * m->sett_per_blocco, m->sett_per_blocco, dst);
}

static int gio_scrivi(Ext2Mount *m, const Giornale *g, uint32_t log, const void *src)
{
    uint32_t f = fisico(g, log);

    if (f == 0) return -1;
    return blk_write(m->blkdev, (uint64_t)f * m->sett_per_blocco, m->sett_per_blocco, src);
}

/* A block of the volume, straight to the disk: replay and checkpoint. Not
 * scrivi_blocco - that would hand it back to the transaction. */
static int al_suo_posto(Ext2Mount *m, uint32_t n, const void *src)
{
    if (n == 0 || n >= m->n_blocchi) {
        klog(LOG_ERROR, "ext3: journal block for %u, outside the volume (%u)",
             n, m->n_blocchi);
        return -1;
    }
    return blk_write(m->blkdev, (uint64_t)n * m->sett_per_blocco, m->sett_per_blocco, src);
}

/* The next journal block after `log`: the journal is a ring from s_first to
 * s_maxlen - 1 (block 0 is its superblock). */
static uint32_t avanti(const Giornale *g, uint32_t log)
{
    log++;
    if (log >= g->maxlen) log = g->primo;
    return log;
}

/* --- the volume's superblock: the RECOVER flag -------------------------------
 * needs_recovery (INCOMPAT_RECOVER) is set while EX-OS has the volume open
 * for writing, as Linux does: whoever finds it set knows the journal may
 * hold a transaction to replay. Raw sectors (offset 1024), as super_aggiorna
 * in scrittura.c: the superblock is not journaled here. */
static int segna_recover(Ext2Mount *m, int acceso)
{
    uint8_t  sb[1024];
    uint32_t inc;

    if (blk_read(m->blkdev, SUPER_OFFSET / 512u, 2u, sb) != 0) return -1;
    inc = le32(sb + 96);
    inc = acceso ? (inc | INCOMPAT_RECOVER) : (inc & ~INCOMPAT_RECOVER);
    p32(sb + 96, inc);
    if (blk_write(m->blkdev, SUPER_OFFSET / 512u, 2u, sb) != 0) return -1;
    m->incompat = inc;
    return blk_flush(m->blkdev);
}

/* =============================================================================
 * REPLAY - jbd2's three passes (fs/jbd2/recovery.c, do_one_pass)
 *
 *   SCAN    from s_start with s_sequence, follow descriptor / commit /
 *           revoke blocks while their sequence is the expected one: the
 *           last committed transaction + 1 is where it ends;
 *   REVOKE  collect the revoke records of the committed transactions: a
 *           block revoked by transaction R is not replayed from any
 *           transaction <= R (it was freed, and may hold new data);
 *   REPLAY  every tagged block of every committed transaction, in order,
 *           to its place - unescaped if its first word was the magic.
 * ============================================================================= */
enum { PASSO_SCAN, PASSO_REVOCA, PASSO_RIPRISTINO };

typedef struct { uint32_t blk, seq; } Revoca;

static Revoca  *g_rev;
static uint32_t g_n_rev;
static int      g_rev_pieno;

static void revoca_segna(uint32_t blk, uint32_t seq)
{
    uint32_t i;

    for (i = 0; i < g_n_rev; i++)
        if (g_rev[i].blk == blk) {
            if ((int32_t)(seq - g_rev[i].seq) > 0) g_rev[i].seq = seq;
            return;
        }
    if (g_n_rev >= GIO_REVOCHE) { g_rev_pieno = 1; return; }
    g_rev[g_n_rev].blk = blk;
    g_rev[g_n_rev].seq = seq;
    g_n_rev++;
}

static int revocato(uint32_t blk, uint32_t seq)
{
    uint32_t i;

    for (i = 0; i < g_n_rev; i++)
        if (g_rev[i].blk == blk) return (int32_t)(seq - g_rev[i].seq) <= 0;
    return 0;
}

/* One pass. `fine`: written by SCAN (the first transaction NOT committed),
 * read by the other two. `copie`: how many blocks REPLAY wrote. */
static int passo(Ext2Mount *m, Giornale *g, int quale, uint32_t inizio,
                 uint32_t seq0, uint32_t *fine, uint32_t *copie)
{
    uint8_t  *b = g->buf, *dato = g->slot_dati;     /* the transaction is empty */
    uint32_t  pos = inizio, seq = seq0, giri = 0;

    for (;;) {
        uint32_t tipo;

        if (quale != PASSO_SCAN && seq == *fine) break;
        if (++giri > g->maxlen) break;              /* a ring with no end in sight */
        if (gio_leggi(m, g, pos, b) != 0) return -1;
        if (be32(b) != GIO_MAGIA || be32(b + 8) != seq) break;
        tipo = be32(b + 4);

        if (tipo == GIO_DESCRITTORE) {
            uint32_t off = 12, qui = avanti(g, pos);

            while (off + g->tag <= m->dim_blocco) {
                uint32_t blk   = be32(b + off);
                uint32_t flags = be16(b + off + 6);

                if ((g->incompat & GIO_INC_64BIT) && be32(b + off + 8) != 0) {
                    klog(LOG_ERROR, "ext3: the journal names a block above 2^32");
                    return -1;
                }
                if (quale == PASSO_RIPRISTINO && !revocato(blk, seq)) {
                    if (gio_leggi(m, g, qui, dato) != 0) return -1;
                    if (flags & TAG_ESCAPE) pbe32(dato, GIO_MAGIA);
                    if (al_suo_posto(m, blk, dato) != 0) return -1;
                    (*copie)++;
                }
                qui = avanti(g, qui);
                off += g->tag;
                if (!(flags & TAG_STESSO_UUID)) off += 16u;
                if (flags & TAG_ULTIMO) break;
            }
            pos = qui;
            continue;
        }
        if (tipo == GIO_COMMIT) {
            seq++;
            pos = avanti(g, pos);
            continue;
        }
        if (tipo == GIO_REVOCA) {
            if (quale == PASSO_REVOCA) {
                uint32_t usati = be32(b + 12), off = 16;
                uint32_t dim = (g->incompat & GIO_INC_64BIT) ? 8u : 4u;

                if (usati > m->dim_blocco) usati = m->dim_blocco;
                while (off + dim <= usati) {
                    uint32_t blk = (dim == 8u) ? be32(b + off + 4) : be32(b + off);

                    revoca_segna(blk, seq);
                    off += dim;
                }
            }
            pos = avanti(g, pos);
            continue;
        }
        break;
    }
    if (quale == PASSO_SCAN) *fine = seq;
    return 0;
}

static int ripristina(Ext2Mount *m, Giornale *g, uint32_t inizio, uint32_t seq0,
                      uint32_t *fine)
{
    uint32_t copie = 0;
    int      r = -1;

    *fine = seq0;
    if (passo(m, g, PASSO_SCAN, inizio, seq0, fine, &copie) != 0) return -1;
    if (*fine == seq0) {
        klog(LOG_INFO, "ext3: '%s': nothing committed in the journal to replay",
             m->etichetta);
        return 0;
    }

    g_rev = (Revoca *)MK->kmalloc(GIO_REVOCHE * sizeof(Revoca));
    if (g_rev == NULL) return -1;
    g_n_rev = 0;
    g_rev_pieno = 0;

    if (passo(m, g, PASSO_REVOCA, inizio, seq0, fine, &copie) != 0) goto via;
    if (g_rev_pieno) {
        klog(LOG_ERROR, "ext3: '%s': more than %u revoked blocks: not replayed",
             m->etichetta, GIO_REVOCHE);
        goto via;
    }
    if (passo(m, g, PASSO_RIPRISTINO, inizio, seq0, fine, &copie) != 0) goto via;
    if (blk_flush(m->blkdev) != 0) goto via;

    klog(LOG_INFO, "ext3: '%s': journal replayed, transactions %u-%u, %u blocks",
         m->etichetta, seq0, *fine - 1u, copie);
    r = 0;
via:
    MK->kfree(g_rev);
    g_rev = NULL;
    return r;
}

/* The volume's two counters, after a replay changed the superblock. */
static void contatori_rileggi(Ext2Mount *m)
{
    uint8_t sb[1024];

    if (blk_read(m->blkdev, SUPER_OFFSET / 512u, 2u, sb) != 0) return;
    m->blocchi_liberi = le32(sb + 12);
    m->inode_liberi   = le32(sb + 16);
}

static void libera(Giornale *g)
{
    if (g == NULL) return;
    if (g->slot_blk)  MK->kfree(g->slot_blk);
    if (g->slot_dati) MK->kfree(g->slot_dati);
    if (g->jsb)       MK->kfree(g->jsb);
    if (g->buf)       MK->kfree(g->buf);
    MK->kfree(g);
}

/* =============================================================================
 * MOUNT: is there a journal, can it be written, does it need a replay?
 * 0 = writable (with or without a journal), <0 = read-only (the reason is
 * in the log).
 * ============================================================================= */
int giornale_monta(int mnt)
{
    Ext2Mount *m = &K->mnt[mnt];
    Giornale  *g;
    uint8_t    sb[1024], ino[128];
    uint32_t   jino, tipo, i, dim_file, per_desc, start, seq, fine;

    g_gio[mnt] = NULL;
    if (!(m->compat & COMPAT_HAS_JOURNAL)) {
        if (m->incompat & INCOMPAT_RECOVER) {
            klog(LOG_ERROR, "ext3: '%s' says needs_recovery but has no journal",
                 m->etichetta);
            return -1;
        }
        return 0;                                   /* plain ext2 */
    }

    if (blk_read(m->blkdev, SUPER_OFFSET / 512u, 2u, sb) != 0) return -1;
    jino = le32(sb + SB_JOURNAL_INUM);
    if (jino == 0 || le32(sb + SB_JOURNAL_DEV) != 0) {
        klog(LOG_WARN, "ext3: '%s': the journal is on another device: read-only",
             m->etichetta);
        return -1;
    }
    if (leggi_inode(m, jino, ino) != 0) return -1;
    dim_file = le32(ino + 4) / m->dim_blocco;

    g = (Giornale *)MK->kmalloc(sizeof(Giornale));
    if (g == NULL) return -1;
    memset(g, 0, sizeof(*g));
    g->jsb = (uint8_t *)MK->kmalloc(m->dim_blocco);
    g->buf = (uint8_t *)MK->kmalloc(m->dim_blocco);
    if (g->jsb == NULL || g->buf == NULL) goto no;

    /* --- where the journal file lies: a few runs, mapped once ------------ */
    {
        uint32_t f0 = mappa_blocco(m, mnt, ino, 0);

        if (f0 == 0 || blk_read(m->blkdev, (uint64_t)f0 * m->sett_per_blocco,
                                m->sett_per_blocco, g->jsb) != 0) goto no;
    }

    /* --- the journal superblock ------------------------------------------ */
    tipo = be32(g->jsb + 4);
    if (be32(g->jsb) != GIO_MAGIA || (tipo != GIO_SUPER_V1 && tipo != GIO_SUPER_V2)) {
        klog(LOG_ERROR, "ext3: '%s': the journal has no valid superblock", m->etichetta);
        goto no;
    }
    g->maxlen = be32(g->jsb + 0x10);
    g->primo  = be32(g->jsb + 0x14);
    if (be32(g->jsb + 0x0C) != m->dim_blocco || g->primo == 0 ||
        g->primo + 3u > g->maxlen || g->maxlen > dim_file) {
        klog(LOG_ERROR, "ext3: '%s': journal superblock inconsistent "
                        "(block %u, first %u, length %u, file %u blocks)",
             m->etichetta, be32(g->jsb + 0x0C), g->primo, g->maxlen, dim_file);
        goto no;
    }
    if (tipo == GIO_SUPER_V2) {
        uint32_t c = be32(g->jsb + 0x24), inc = be32(g->jsb + 0x28), ro = be32(g->jsb + 0x2C);

        if (c != 0 || ro != 0 || (inc & ~GIO_INC_NOTI)) {
            klog(LOG_WARN, "ext3: '%s': journal features compat 0x%x incompat 0x%x "
                           "ro_compat 0x%x not handled (checksums?): read-only",
                 m->etichetta, c, inc, ro);
            goto no;
        }
        g->incompat = inc;
        memcpy(g->uuid, g->jsb + 0x30, 16);
    }
    g->tag = (g->incompat & GIO_INC_64BIT) ? 12u : 8u;

    for (i = 0; i < g->maxlen; i++) {
        uint32_t f = mappa_blocco(m, mnt, ino, i);
        Corsa   *c = g->n_corse ? &g->corse[g->n_corse - 1u] : NULL;

        if (f == 0) {
            klog(LOG_ERROR, "ext3: '%s': journal block %u not mapped", m->etichetta, i);
            goto no;
        }
        if (c != NULL && f == c->fis + c->n) { c->n++; continue; }
        if (g->n_corse >= GIO_CORSE) {
            klog(LOG_WARN, "ext3: '%s': the journal is in more than %u pieces: read-only",
                 m->etichetta, GIO_CORSE);
            goto no;
        }
        g->corse[g->n_corse].log = i;
        g->corse[g->n_corse].fis = f;
        g->corse[g->n_corse].n   = 1;
        g->n_corse++;
    }

    /* --- room for one transaction: one descriptor block's tags, the memory
     * set aside, and the journal itself (descriptor + copies + commit) ---- */
    per_desc = (m->dim_blocco - 12u - 16u) / g->tag;
    g->max_slot = GIO_BYTE / m->dim_blocco;
    if (g->max_slot > per_desc) g->max_slot = per_desc;
    if (g->max_slot > g->maxlen - g->primo - 2u) g->max_slot = g->maxlen - g->primo - 2u;
    if (g->max_slot < GIO_RISERVA + 8u) {
        klog(LOG_WARN, "ext3: '%s': journal too small (%u blocks): read-only",
             m->etichetta, g->maxlen);
        goto no;
    }
    g->slot_blk  = (uint32_t *)MK->kmalloc(g->max_slot * sizeof(uint32_t));
    g->slot_dati = (uint8_t *)MK->kmalloc(g->max_slot * m->dim_blocco);
    if (g->slot_blk == NULL || g->slot_dati == NULL) goto no;

    /* --- a replay, if the volume was not unmounted cleanly ---------------- */
    start = be32(g->jsb + 0x1C);
    seq   = be32(g->jsb + 0x18);
    g->seq = seq;                       /* as jbd2: unchanged without a replay */
    if ((m->incompat & INCOMPAT_RECOVER) && start != 0) {
        if (start < g->primo || start >= g->maxlen ||
            ripristina(m, g, start, seq, &fine) != 0) {
            klog(LOG_ERROR, "ext3: '%s': the journal could not be replayed: read-only",
                 m->etichetta);
            goto no;
        }
        contatori_rileggi(m);
        g_ind_mnt = -1;                     /* a pointer block may have changed */
        g->seq = fine + 1u;                 /* jbd2: past the last one replayed */
    }

    /* --- empty journal, needs_recovery set: the volume is ours ------------ */
    pbe32(g->jsb + 0x18, g->seq);
    pbe32(g->jsb + 0x1C, 0);
    if (gio_scrivi(m, g, 0, g->jsb) != 0 || blk_flush(m->blkdev) != 0 ||
        segna_recover(m, 1) != 0) {
        klog(LOG_ERROR, "ext3: '%s': the journal could not be opened", m->etichetta);
        goto no;
    }
    g_gio[mnt] = g;
    klog(LOG_INFO, "ext3: '%s': journal of %u blocks, %u per transaction",
         m->etichetta, g->maxlen, g->max_slot);
    return 0;
no:
    libera(g);
    return -1;
}

/* =============================================================================
 * COMMIT AND CHECKPOINT - see the head of the file for the order.
 * ============================================================================= */
static int commit(Ext2Mount *m, Giornale *g)
{
    uint32_t S = g->seq, i, off, log;
    uint8_t *d = g->buf;

    if (g->n_slot == 0) return 0;
    if (g->guasto) return -1;

    /* 1. the journal superblock points at this transaction */
    pbe32(g->jsb + 0x18, S);
    pbe32(g->jsb + 0x1C, g->primo);
    if (gio_scrivi(m, g, 0, g->jsb) != 0) goto guasto;

    /* 2. the descriptor: one tag per block, the first followed by the UUID */
    memset(d, 0, m->dim_blocco);
    pbe32(d, GIO_MAGIA); pbe32(d + 4, GIO_DESCRITTORE); pbe32(d + 8, S);
    off = 12;
    for (i = 0; i < g->n_slot; i++) {
        const uint8_t *c = g->slot_dati + i * m->dim_blocco;
        uint32_t flags = 0;

        if (i > 0) flags |= TAG_STESSO_UUID;
        if (i + 1u == g->n_slot) flags |= TAG_ULTIMO;
        if (be32(c) == GIO_MAGIA) flags |= TAG_ESCAPE;
        pbe32(d + off, g->slot_blk[i]);
        pbe16(d + off + 6, flags);
        off += g->tag;
        if (i == 0) { memcpy(d + off, g->uuid, 16); off += 16; }
    }
    log = g->primo;
    if (gio_scrivi(m, g, log, d) != 0) goto guasto;

    /* 3. the copies - an escaped one with its first word zeroed */
    for (i = 0; i < g->n_slot; i++) {
        const uint8_t *c = g->slot_dati + i * m->dim_blocco;

        log = avanti(g, log);
        if (be32(c) == GIO_MAGIA) {
            memcpy(d, c, m->dim_blocco);
            pbe32(d, 0);
            c = d;
        }
        if (gio_scrivi(m, g, log, c) != 0) goto guasto;
    }

    /* 4. everything so far - the files' data too - on the disk, then the
     * commit block, then that too: from here the transaction exists */
    if (blk_flush(m->blkdev) != 0) goto guasto;
    memset(d, 0, m->dim_blocco);
    pbe32(d, GIO_MAGIA); pbe32(d + 4, GIO_COMMIT); pbe32(d + 8, S);
    pbe32(d + 0x34, unix_ora_corrente());           /* h_commit_sec, low half */
    log = avanti(g, log);
    if (gio_scrivi(m, g, log, d) != 0 || blk_flush(m->blkdev) != 0) goto guasto;

    /* 5. checkpoint: each block to its place */
    for (i = 0; i < g->n_slot; i++)
        if (al_suo_posto(m, g->slot_blk[i], g->slot_dati + i * m->dim_blocco) != 0)
            goto guasto;
    if (blk_flush(m->blkdev) != 0) goto guasto;

    /* 6. the journal is empty again (carried to the disk by the next flush) */
    g->seq = S + 1u;
    pbe32(g->jsb + 0x18, g->seq);
    pbe32(g->jsb + 0x1C, 0);
    if (gio_scrivi(m, g, 0, g->jsb) != 0) goto guasto;
    g->n_slot = 0;
    return 0;

guasto:
    /* ! The copies stay in memory and keep answering the reads: what the
     * system sees is still right. What reaches the disk is decided at the
     * next mount, by the replay - and no more writes until then. */
    g->guasto = 1;
    klog(LOG_ERROR, "ext3: '%s': transaction %u not written: the volume takes "
                    "no more writes until it is mounted again", m->etichetta, S);
    return -1;
}

int giornale_commit(int mnt)
{
    Giornale *g = (mnt >= 0 && mnt < EXT2_MAX_MOUNT) ? g_gio[mnt] : NULL;

    return g ? commit(&K->mnt[mnt], g) : 0;
}

/* Before an operation: room for what it may change. */
int giornale_inizio(int mnt)
{
    Giornale *g = (mnt >= 0 && mnt < EXT2_MAX_MOUNT) ? g_gio[mnt] : NULL;

    if (g == NULL) return 0;
    if (g->guasto) return -1;
    if (g->max_slot - g->n_slot < GIO_RISERVA) return commit(&K->mnt[mnt], g);
    return 0;
}

/* After an operation: `subito` for everything but write(). */
int giornale_fine(int mnt, int subito)
{
    Giornale *g = (mnt >= 0 && mnt < EXT2_MAX_MOUNT) ? g_gio[mnt] : NULL;

    if (g == NULL || g->n_slot == 0) return 0;
    if (subito || g->n_slot * 2u >= g->max_slot ||
        unix_ora_corrente() - g->aperto >= GIO_SECONDI)
        return commit(&K->mnt[mnt], g);
    return 0;
}

/* Unmount: the last transaction, then needs_recovery off. */
void giornale_smonta(int mnt)
{
    Giornale *g = (mnt >= 0 && mnt < EXT2_MAX_MOUNT) ? g_gio[mnt] : NULL;

    if (g == NULL) return;
    if (commit(&K->mnt[mnt], g) == 0 && blk_flush(K->mnt[mnt].blkdev) == 0)
        segna_recover(&K->mnt[mnt], 0);
    g_gio[mnt] = NULL;
    libera(g);
}

/* =============================================================================
 * THE HOOKS (Ext2Ops: the kernel calls them from its block functions)
 * ============================================================================= */
void giornale_leggi(Ext2Mount *m, uint32_t n, uint32_t quanti, void *dst)
{
    Giornale *g = di(m);
    uint32_t  i;

    if (g == NULL || g->n_slot == 0) return;
    for (i = 0; i < g->n_slot; i++)
        if (g->slot_blk[i] >= n && g->slot_blk[i] < n + quanti)
            memcpy((uint8_t *)dst + (g->slot_blk[i] - n) * m->dim_blocco,
                   g->slot_dati + i * m->dim_blocco, m->dim_blocco);
}

int giornale_scrivi(Ext2Mount *m, uint32_t n, const void *src)
{
    Giornale *g = di(m);
    uint32_t  i;

    if (g == NULL) return 1;                        /* ext2: the kernel writes it */
    if (g->guasto) return -1;
    for (i = 0; i < g->n_slot; i++)
        if (g->slot_blk[i] == n) {
            memcpy(g->slot_dati + i * m->dim_blocco, src, m->dim_blocco);
            return 0;
        }
    /* Full in the middle of an operation (a long write()): commit what is
     * there - see the head of the file - and start again. */
    if (g->n_slot >= g->max_slot && commit(m, g) != 0) return -1;
    if (g->n_slot == 0) g->aperto = unix_ora_corrente();
    g->slot_blk[g->n_slot] = n;
    memcpy(g->slot_dati + g->n_slot * m->dim_blocco, src, m->dim_blocco);
    g->n_slot++;
    return 0;
}

int giornale_dati(Ext2Mount *m, uint32_t n, uint32_t quanti)
{
    Giornale *g = di(m);
    uint32_t  i;

    if (g == NULL || g->n_slot == 0) return 0;
    for (i = 0; i < g->n_slot; i++)
        if (g->slot_blk[i] >= n && g->slot_blk[i] < n + quanti) {
            /* Data over a block the transaction holds: it was metadata and
             * has just been freed. The transaction goes first, or a replay
             * would put the old block back over the new data. */
            return commit(m, g);
        }
    return 0;
}
