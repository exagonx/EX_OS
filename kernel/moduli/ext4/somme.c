/* =============================================================================
 * kernel/moduli/ext4/somme.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * THE CHECKSUMS OF ext4 (ext4.mod 0.004, @EXT4-SCRITTURA)
 *
 * An ext4 made by today's mke2fs has metadata_csum: every piece of metadata
 * carries a crc32c of itself, and Linux refuses a block whose sum is wrong
 * ("bad checksum", the volume goes read-only). So everything this module
 * writes on such a volume is summed here, exactly as Linux sums it
 * (fs/ext4/ and fs/jbd2/ name the functions):
 *
 *   superblock         crc32c(~0, the first 0x3FC bytes)          at 0x3FC
 *   group descriptor   crc32c(seed, group, the descriptor) & 0xFFFF   0x1E
 *   bitmaps            crc32c(seed, the bitmap)  in the descriptor 0x18/0x1A
 *   inode              crc32c(inode seed, the whole inode)     0x7C, 0x82
 *   extent tree block  crc32c(inode seed, header + entries)  after them
 *   directory block    crc32c(inode seed, the entries)  in a 12-byte tail
 *   attribute block    crc32c(seed, block number, the block)       0x10
 * where seed = crc32c(~0, uuid) (or s_checksum_seed, metadata_csum_seed)
 * and inode seed = crc32c(crc32c(seed, inode number), i_generation).
 * The older uninit_bg (gdt_csum) sums only the descriptors, with crc16.
 *
 * ! LINUX'S crc32c HAS NO FINAL INVERSION: ext4_chksum(~0, ...) is the raw
 * register, the bitwise NOT of the "standard" crc32c. crc32c_grezzo below
 * is that one.
 * ============================================================================= */

#include "ext4.h"

Ext4Vol g_vol[EXT2_MAX_MOUNT];

static uint32_t g_crc32c[256];
static uint16_t g_crc16[256];
static int      g_tabelle = 0;

static void tabelle(void)
{
    uint32_t i, k, c;

    for (i = 0; i < 256u; i++) {
        c = i;
        for (k = 0; k < 8u; k++) c = (c & 1u) ? (c >> 1) ^ 0x82F63B78u : c >> 1;
        g_crc32c[i] = c;
        c = i;
        for (k = 0; k < 8u; k++) c = (c & 1u) ? (c >> 1) ^ 0xA001u : c >> 1;
        g_crc16[i] = (uint16_t)c;
    }
    g_tabelle = 1;
}

uint32_t crc32c_grezzo(uint32_t crc, const void *dati, uint32_t n)
{
    const uint8_t *p = (const uint8_t *)dati;

    if (!g_tabelle) tabelle();
    while (n--) crc = (crc >> 8) ^ g_crc32c[(crc ^ *p++) & 0xFFu];
    return crc;
}

static uint32_t crc16(uint32_t crc, const void *dati, uint32_t n)
{
    const uint8_t *p = (const uint8_t *)dati;

    if (!g_tabelle) tabelle();
    while (n--) crc = ((crc >> 8) ^ g_crc16[(crc ^ *p++) & 0xFFu]) & 0xFFFFu;
    return crc;
}

/* crc32c over `n` bytes of `p`, with the `quanti` bytes at `buco` counted
 * as zeros - the field the sum goes into. */
static uint32_t crc_con_buco(uint32_t crc, const uint8_t *p, uint32_t n,
                             uint32_t buco, uint32_t quanti)
{
    static const uint8_t zeri[4] = { 0, 0, 0, 0 };

    crc = crc32c_grezzo(crc, p, buco);
    crc = crc32c_grezzo(crc, zeri, quanti);
    return crc32c_grezzo(crc, p + buco + quanti, n - buco - quanti);
}

Ext4Vol *vol_di(const Ext2Mount *m)
{
    int i = (int)(m - K->mnt);

    return (i >= 0 && i < EXT2_MAX_MOUNT) ? &g_vol[i] : NULL;
}

/* =============================================================================
 * AT MOUNT: what this volume needs, and whether its superblock is right
 * ============================================================================= */
int vol_monta(int mnt)
{
    Ext2Mount *m = &K->mnt[mnt];
    Ext4Vol   *v = &g_vol[mnt];
    uint8_t    sb[1024];
    uint32_t   x;

    memset(v, 0, sizeof(*v));
    est_dimentica();
    if (blk_read(m->blkdev, SUPER_OFFSET / 512u, 2u, sb) != 0) return -1;

    memcpy(v->uuid, sb + 0x68, 16);
    v->estesi    = (m->incompat & INCOMPAT_EXTENTS) != 0;
    v->meta_csum = (m->ro_compat & RO_COMPAT_METADATA_CSUM) != 0;
    v->gdt_csum  = !v->meta_csum && (m->ro_compat & RO_COMPAT_GDT_CSUM) != 0;
    v->dir_nlink = (m->ro_compat & RO_COMPAT_DIR_NLINK) != 0;
    v->gdt_riservati = (m->compat & COMPAT_RESIZE_INODE) ? le16(sb + 0xCE) : 0u;
    v->backup[0] = le32(sb + 0x24C);
    v->backup[1] = le32(sb + 0x250);

    /* a new inode's extra space: what the volume wants, 32 if it says nothing */
    v->extra_isize = 0;
    if (m->dim_inode > 128u) {
        x = le16(sb + 0x15E);
        if (x == 0 || 128u + x > m->dim_inode) x = 32u;
        if (128u + x > m->dim_inode) x = m->dim_inode - 128u;
        v->extra_isize = x & ~3u;
    }

    if (v->meta_csum) {
        if (sb[0x175] != 1u) {
            klog(LOG_WARN, "ext4: '%s': checksum type %u is not crc32c: read-only",
                 m->etichetta, sb[0x175]);
            return -1;
        }
        if (le32(sb + 0x3FC) != crc32c_grezzo(~0u, sb, 0x3FCu)) {
            klog(LOG_ERROR, "ext4: '%s': the superblock's checksum is wrong: read-only "
                            "(e2fsck)", m->etichetta);
            return -1;
        }
        v->seme = (m->incompat & INCOMPAT_CSUM_SEED) ? le32(sb + 0x270)
                                                     : crc32c_grezzo(~0u, v->uuid, 16);
    }
    return 0;
}

/* =============================================================================
 * THE SUMS
 * ============================================================================= */
void somma_super(Ext2Mount *m, uint8_t *sb)
{
    Ext4Vol *v = vol_di(m);

    if (v && v->meta_csum) p32(sb + 0x3FC, crc32c_grezzo(~0u, sb, 0x3FCu));
}

void somma_desc(Ext2Mount *m, uint32_t g, uint8_t *d)
{
    Ext4Vol *v = vol_di(m);
    uint8_t  gl[4];
    uint32_t c;

    if (v == NULL || (!v->meta_csum && !v->gdt_csum)) return;
    p32(gl, g);
    if (v->meta_csum) {
        c = crc32c_grezzo(v->seme, gl, 4);
        c = crc_con_buco(c, d, m->dim_desc, 0x1Eu, 2u);
    } else {
        c = crc16(0xFFFFu, v->uuid, 16);
        c = crc16(c, gl, 4);
        c = crc16(c, d, 0x1Eu);
        if (m->dim_desc > 0x20u) c = crc16(c, d + 0x20, m->dim_desc - 0x20u);
    }
    p16(d + 0x1E, c & 0xFFFFu);
}

/* The bitmap's sum, into its descriptor: `inode` 0 = blocks, 1 = inodes. */
void somma_bitmap(Ext2Mount *m, uint8_t *d, int inode, const uint8_t *bmp)
{
    Ext4Vol *v = vol_di(m);
    uint32_t c;

    if (v == NULL || !v->meta_csum) return;
    c = crc32c_grezzo(v->seme, bmp, (inode ? m->inode_per_gruppo : m->blocchi_per_gruppo) / 8u);
    p16(d + (inode ? 0x1Au : 0x18u), c & 0xFFFFu);
    if (m->dim_desc >= (inode ? 0x3Cu : 0x3Au)) p16(d + (inode ? 0x3Au : 0x38u), c >> 16);
}

/* The seed of one inode: its number and i_generation (offset 100). */
uint32_t seme_inode(Ext2Mount *m, uint32_t num, const uint8_t *inode)
{
    Ext4Vol *v = vol_di(m);
    uint8_t  b[4];
    uint32_t c;

    p32(b, num);
    c = crc32c_grezzo(v ? v->seme : 0u, b, 4);
    return crc32c_grezzo(c, inode + 100, 4);
}

/* The whole inode as it lies in the table (m->dim_inode bytes). */
void somma_inode(Ext2Mount *m, uint32_t num, uint8_t *ino)
{
    Ext4Vol *v = vol_di(m);
    uint32_t c, n = m->dim_inode;
    int      alto = n > 128u && le16(ino + 0x80) >= 4u;     /* i_checksum_hi fits */

    if (v == NULL || !v->meta_csum) return;
    p16(ino + 0x7C, 0);
    if (alto) p16(ino + 0x82, 0);
    c = crc32c_grezzo(seme_inode(m, num, ino), ino, n);
    p16(ino + 0x7C, c & 0xFFFFu);
    if (alto) p16(ino + 0x82, c >> 16);
}

/* A block of an extent tree: the sum follows the eh_max entries. */
void somma_estesi(Ext2Mount *m, uint32_t num, const uint8_t *inode, uint8_t *blocco)
{
    Ext4Vol *v = vol_di(m);
    uint32_t fine = 12u + 12u * le16(blocco + 4);

    if (v == NULL || !v->meta_csum || fine + 4u > m->dim_blocco) return;
    p32(blocco + fine, crc32c_grezzo(seme_inode(m, num, inode), blocco, fine));
}

/* Where a directory block's entries end: before the tail on metadata_csum. */
uint32_t dir_fine(Ext2Mount *m)
{
    Ext4Vol *v = vol_di(m);

    return (v && v->meta_csum) ? m->dim_blocco - 12u : m->dim_blocco;
}

static int ha_coda(Ext2Mount *m, const uint8_t *b)
{
    const uint8_t *t = b + m->dim_blocco - 12u;

    return le32(t) == 0 && le16(t + 4) == 12u && t[6] == 0 && t[7] == 0xDEu;
}

/* =============================================================================
 * THE TAIL OF A DIRECTORY BLOCK (metadata_csum)
 *
 * Every leaf block of a directory ends with a fake entry of 12 bytes - inode
 * 0, rec_len 12, file type 0xDE - holding the block's sum. A block without
 * one (an htree node that has just become a plain block, see
 * dir_togli_indice) gets it from its last entry, if that has 12 spare bytes:
 * the entries stop before it. 0, or -1 (no room: the block is left alone).
 * ============================================================================= */
int dir_coda(Ext2Mount *m, uint8_t *b)
{
    uint32_t p = 0, ultimo = 0xFFFFFFFFu, fine = m->dim_blocco - 12u;
    Ext4Vol *v = vol_di(m);

    if (v == NULL || !v->meta_csum || ha_coda(m, b)) return 0;
    while (p < m->dim_blocco) {
        uint32_t rec = le16(b + p + 4);

        if (rec < 8u || p + rec > m->dim_blocco) return -1;
        if (p + rec == m->dim_blocco) { ultimo = p; break; }
        p += rec;
    }
    if (ultimo == 0xFFFFFFFFu) return -1;
    {
        uint32_t usato = le32(b + ultimo) ? ((8u + b[ultimo + 6] + 3u) & ~3u) : 0u;

        if (ultimo + usato > fine) return -1;
        if (ultimo == fine) return -1;      /* an entry of exactly 12 bytes at the end */
        p16(b + ultimo + 4, fine - ultimo);
    }
    memset(b + fine, 0, 12);
    p16(b + fine + 4, 12u);
    b[fine + 7] = 0xDEu;
    return 0;
}

int somma_dir(Ext2Mount *m, uint32_t num, const uint8_t *inode, uint8_t *b)
{
    Ext4Vol *v = vol_di(m);

    if (v == NULL || !v->meta_csum) return 0;
    if (dir_coda(m, b) != 0) {
        klog(LOG_ERROR, "ext4: directory %u: a block with no room for its checksum", num);
        return -1;
    }
    p32(b + m->dim_blocco - 4u,
        crc32c_grezzo(seme_inode(m, num, inode), b, m->dim_blocco - 12u));
    return 0;
}

void somma_attributi(Ext2Mount *m, uint32_t blk, uint8_t *b)
{
    Ext4Vol *v = vol_di(m);
    uint8_t  n[8];
    uint32_t c;

    if (v == NULL || !v->meta_csum) return;
    p32(n, blk);
    p32(n + 4, 0);
    c = crc32c_grezzo(v->seme, n, 8);
    p32(b + 0x10, crc_con_buco(c, b, m->dim_blocco, 0x10u, 4u));
}
