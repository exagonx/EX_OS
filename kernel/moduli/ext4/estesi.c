/* =============================================================================
 * kernel/moduli/ext4/estesi.c
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * WRITING THE EXTENT TREE (ext4.mod 0.004, @EXT4-SCRITTURA)
 *
 * An ext4 file does not list its blocks one by one, as ext2 does with its
 * pointer blocks: it lists RUNS ("extents": logical block, length, first
 * physical block). Up to four sit in the inode itself (i_block: a 12-byte
 * header and four 12-byte entries); more need a tree whose nodes are whole
 * blocks - index entries (logical block, child block) above, extents in the
 * leaves. The kernel READS that tree (kernel/fs/ext2.c, extent_map); here it
 * is written:
 *
 *   est_mappa_o_alloca   the physical block of logical block n, allocated
 *                        when there is none: the extent before it grows by
 *                        one (the usual case - a file written from start to
 *                        end stays one extent per contiguous run), or a new
 *                        extent goes in; a full node is split, a full root
 *                        moves into a block of its own and the tree grows
 *                        one level;
 *   est_pota             everything from logical block `primo` on is freed:
 *                        extents removed or shortened, nodes left empty
 *                        freed with them.
 *
 * ! THE RULES e2fsck CHECKS, kept here: the entries of a node sorted and not
 * overlapping; an index entry's logical block EQUAL to the first one of its
 * child; an extent at most 32768 blocks (above that the length means
 * "unwritten"); every block of the tree counted in i_blocks; on
 * metadata_csum every non-root node summed (somma_estesi).
 *
 * ! UNWRITTEN EXTENTS (fallocate, made by Linux) read as zeros and are NOT
 * written into: the write is refused - writing would need splitting them.
 * Blocks above 2^32 are refused too, as the kernel does when it reads.
 * ============================================================================= */

#include "ext4.h"

#define EST_LIVELLI     5u              /* ext4 trees are at most 5 deep */
#define EST_LUNG_MAX    32768u          /* an initialised extent */
#define VOCE(h, i)      ((h) + 12u + (uint32_t)(i) * 12u)

/* One buffer per level of the tree (level 0, the root, is in the inode),
 * and which block each holds: walking the same file again finds them here. */
static uint8_t  g_nodo[EST_LIVELLI][BLOCCO_MAX];
static uint32_t g_nodo_blk[EST_LIVELLI];
static int      g_nodo_mnt = -1;
static uint8_t  g_nuovo[BLOCCO_MAX];    /* a node being made */

void est_dimentica(void)
{
    uint32_t i;

    for (i = 0; i < EST_LIVELLI; i++) g_nodo_blk[i] = 0;
    g_nodo_mnt = -1;
}

void est_dimentica_blocco(uint32_t n)
{
    uint32_t i;

    for (i = 0; i < EST_LIVELLI; i++) if (g_nodo_blk[i] == n) g_nodo_blk[i] = 0;
}

void est_radice_vuota(uint8_t *inode)
{
    memset(inode + 40, 0, 60);
    p16(inode + 40, EXTENT_MAGIC);
    p16(inode + 44, 4u);                        /* eh_max: four in the inode */
    p32(inode + 32, le32(inode + 32) | INODE_FL_EXTENTS);
}

static uint32_t max_blocco(Ext2Mount *m)
{
    return (m->dim_blocco - 12u) / 12u;
}

/* Block `blk` into the buffer of level `liv`; it must be a node `prof` deep. */
static int carica(Ext2Mount *m, int mnt, uint32_t liv, uint32_t blk, uint32_t prof)
{
    uint8_t *h = g_nodo[liv];

    if (g_nodo_mnt != mnt) { est_dimentica(); g_nodo_mnt = mnt; }
    if (g_nodo_blk[liv] == blk) return 0;
    g_nodo_blk[liv] = 0;
    if (blk >= m->n_blocchi || leggi_blocco(m, blk, h) != 0) return -1;
    if (le16(h) != EXTENT_MAGIC || le16(h + 6) != prof || le16(h + 4) > max_blocco(m) ||
        le16(h + 2) > le16(h + 4)) {
        klog(LOG_ERROR, "ext4: block %u is not a valid extent node", blk);
        return -1;
    }
    g_nodo_blk[liv] = blk;
    return 0;
}

typedef struct {
    uint8_t  *h;                        /* the node's header */
    uint32_t  blk;                      /* its block; 0 = the root */
    uint32_t  idx;                      /* the entry followed (index nodes) */
} Passo;

/* From the root down to the leaf that holds - or would hold - logical
 * block n. *prof = the depth of the tree. */
static int cammina(Ext2Mount *m, int mnt, uint8_t *inode, uint32_t n, Passo *p,
                   uint32_t *prof)
{
    uint8_t *h = inode + 40;
    uint32_t liv = 0, d;

    if (le16(h) != EXTENT_MAGIC || le16(h + 2) > le16(h + 4) || le16(h + 4) > 4u) return -1;
    d = le16(h + 6);
    if (d >= EST_LIVELLI) return -1;
    *prof = d;
    p[0].blk = 0;

    for (;;) {
        uint32_t voci = le16(h + 2), i, scelta = 0;

        p[liv].h = h;
        for (i = 0; i < voci; i++) {
            if (le32(VOCE(h, i)) <= n) scelta = i;
            else break;
        }
        p[liv].idx = scelta;
        if (liv == d) return 0;
        {
            const uint8_t *e = VOCE(h, scelta);
            uint32_t       figlio = le32(e + 4);

            if (voci == 0 || le16(e + 8) != 0 || figlio == 0) return -1;
            if (carica(m, mnt, liv + 1u, figlio, d - liv - 1u) != 0) return -1;
            liv++;
            p[liv].blk = figlio;
            h = g_nodo[liv];
        }
    }
}

/* A changed node to the disk; the root is written with its inode, by the
 * caller. */
static int scrivi_nodo(Ext2Mount *m, uint32_t num, const uint8_t *inode, Passo *p,
                       uint32_t liv)
{
    if (liv == 0) return 0;
    somma_estesi(m, num, inode, p[liv].h);
    return scrivi_blocco(m, p[liv].blk, p[liv].h);
}

/* The leaf's first extent is now n: the index entries above it follow
 * (see the rules at the head of the file). */
static int nuovo_inizio(Ext2Mount *m, uint32_t num, const uint8_t *inode, Passo *p,
                        uint32_t d, uint32_t n)
{
    uint32_t l;

    for (l = d; l > 0; l--) {
        uint8_t *e = VOCE(p[l - 1u].h, p[l - 1u].idx);

        if (le32(e) <= n) break;
        p32(e, n);
        if (scrivi_nodo(m, num, inode, p, l - 1u) != 0) return -1;
        if (p[l - 1u].idx != 0) break;
    }
    return 0;
}

/* An index entry {first, child} into node `liv` (which has room), after
 * the entry followed. */
static int metti_indice(Ext2Mount *m, uint32_t num, const uint8_t *inode, Passo *p,
                        uint32_t liv, uint32_t primo, uint32_t figlio)
{
    uint8_t *h = p[liv].h, *e;
    uint32_t voci = le16(h + 2), k = p[liv].idx + 1u;

    if (k > voci) k = voci;
    memmove(VOCE(h, k + 1u), VOCE(h, k), (voci - k) * 12u);
    e = VOCE(h, k);
    p32(e, primo);
    p32(e + 4, figlio);
    p16(e + 8, 0);
    p16(e + 10, 0);
    p16(h + 2, voci + 1u);
    return scrivi_nodo(m, num, inode, p, liv);
}

static void intestazione(Ext2Mount *m, uint8_t *b, uint32_t voci, uint32_t prof)
{
    memset(b, 0, m->dim_blocco);
    p16(b, EXTENT_MAGIC);
    p16(b + 2, voci);
    p16(b + 4, max_blocco(m));
    p16(b + 6, prof);
}

/* =============================================================================
 * ROOM IN A FULL NODE (level `liv`)
 * 1 = the tree changed, walk it again; 0 = the extent {n, fis} is in (a leaf
 * grown at its end); -1 = error.
 * ============================================================================= */
static int fai_posto(Ext2Mount *m, int mnt, uint32_t num, uint8_t *inode, Passo *p,
                     uint32_t liv, uint32_t d, int in_fondo, uint32_t n, uint32_t fis,
                     uint32_t pref, uint32_t *charge)
{
    uint8_t *h = p[liv].h;
    uint32_t voci = le16(h + 2), b2;

    if (liv > 0 && le16(p[liv - 1u].h + 2) >= le16(p[liv - 1u].h + 4)) {
        /* The parent is full too: room there first. */
        return fai_posto(m, mnt, num, inode, p, liv - 1u, d, 0, n, fis, pref, charge) < 0 ? -1 : 1;
    }

    b2 = alloca_blocco_per(m, pref, 0);
    if (b2 == 0) return -1;
    (*charge)++;

    if (liv == 0) {
        /* The root moves into a block of its own and points to it: one
         * level more. */
        if (d + 1u >= EST_LIVELLI) { libera_blocco(m, b2); (*charge)--; return -1; }
        intestazione(m, g_nuovo, voci, d);
        memcpy(VOCE(g_nuovo, 0), VOCE(h, 0), voci * 12u);
        somma_estesi(m, num, inode, g_nuovo);
        if (scrivi_blocco(m, b2, g_nuovo) != 0) return -1;
        p16(h + 2, 1u);
        p16(h + 6, d + 1u);
        memset(VOCE(h, 0), 0, 48);
        p32(VOCE(h, 0), voci ? le32(VOCE(g_nuovo, 0)) : 0u);
        p32(VOCE(h, 0) + 4, b2);
        est_dimentica();
        return 1;
    }

    if (liv == d && in_fondo) {
        /* A leaf full, the new extent after all of its: a new leaf with it
         * alone - a file written in order fills its leaves to the brim. */
        intestazione(m, g_nuovo, 1u, 0);
        p32(VOCE(g_nuovo, 0), n);
        p16(VOCE(g_nuovo, 0) + 4, 1u);
        p32(VOCE(g_nuovo, 0) + 8, fis);
        somma_estesi(m, num, inode, g_nuovo);
        if (scrivi_blocco(m, b2, g_nuovo) != 0) return -1;
        if (metti_indice(m, num, inode, p, liv - 1u, n, b2) != 0) return -1;
        return 0;
    }

    /* Split in two: the upper half into the new node. */
    {
        uint32_t meta = voci / 2u;

        intestazione(m, g_nuovo, voci - meta, d - liv);
        memcpy(VOCE(g_nuovo, 0), VOCE(h, meta), (voci - meta) * 12u);
        somma_estesi(m, num, inode, g_nuovo);
        if (scrivi_blocco(m, b2, g_nuovo) != 0) return -1;
        p16(h + 2, meta);
        if (scrivi_nodo(m, num, inode, p, liv) != 0) return -1;
        if (metti_indice(m, num, inode, p, liv - 1u, le32(VOCE(g_nuovo, 0)), b2) != 0)
            return -1;
        est_dimentica();
        return 1;
    }
}

/* The extent {n, 1, fis} into the leaf of the path: k = how many of its
 * extents start before n. Same return values as fai_posto. */
static int inserisci(Ext2Mount *m, int mnt, uint32_t num, uint8_t *inode, Passo *p,
                     uint32_t d, uint32_t k, uint32_t n, uint32_t fis, uint32_t pref,
                     uint32_t *charge)
{
    uint8_t *h = p[d].h, *e;
    uint32_t voci = le16(h + 2);

    /* after the extent before it */
    if (k > 0) {
        uint32_t s, l;

        e = VOCE(h, k - 1u);
        s = le32(e);
        l = le16(e + 4);
        if (l < EST_LUNG_MAX && le16(e + 6) == 0 && s + l == n && le32(e + 8) + l == fis) {
            p16(e + 4, l + 1u);
            if (k < voci) {                     /* and it now touches the next one */
                uint8_t *f  = VOCE(h, k);
                uint32_t fl = le16(f + 4);

                if (fl <= EST_LUNG_MAX && le16(f + 6) == 0 && le32(f) == n + 1u &&
                    le32(f + 8) == fis + 1u && l + 1u + fl <= EST_LUNG_MAX) {
                    p16(e + 4, l + 1u + fl);
                    memmove(f, f + 12, (voci - k - 1u) * 12u);
                    memset(VOCE(h, voci - 1u), 0, 12);
                    p16(h + 2, voci - 1u);
                }
            }
            return scrivi_nodo(m, num, inode, p, d);
        }
    }
    /* before the extent after it */
    if (k < voci) {
        uint32_t fl;

        e = VOCE(h, k);
        fl = le16(e + 4);
        if (fl < EST_LUNG_MAX && le16(e + 6) == 0 && le32(e) == n + 1u &&
            le32(e + 8) == fis + 1u) {
            p32(e, n);
            p32(e + 8, fis);
            p16(e + 4, fl + 1u);
            if (scrivi_nodo(m, num, inode, p, d) != 0) return -1;
            return (k == 0) ? nuovo_inizio(m, num, inode, p, d, n) : 0;
        }
    }
    /* an extent of its own */
    if (voci < le16(h + 4)) {
        memmove(VOCE(h, k + 1u), VOCE(h, k), (voci - k) * 12u);
        e = VOCE(h, k);
        p32(e, n);
        p16(e + 4, 1u);
        p16(e + 6, 0);
        p32(e + 8, fis);
        p16(h + 2, voci + 1u);
        if (scrivi_nodo(m, num, inode, p, d) != 0) return -1;
        return (k == 0) ? nuovo_inizio(m, num, inode, p, d, n) : 0;
    }
    return fai_posto(m, mnt, num, inode, p, d, d, k == voci, n, fis, pref, charge);
}

uint32_t est_mappa_o_alloca(Ext2Mount *m, int mnt, uint32_t num, uint8_t *inode,
                            uint32_t n, uint32_t pref, uint32_t *charge)
{
    Passo    p[EST_LIVELLI];
    uint32_t d, giro, nuovo = 0;

    for (giro = 0; giro < 16u; giro++) {
        uint8_t *h;
        uint32_t voci, k;
        int      r;

        if (cammina(m, mnt, inode, n, p, &d) != 0) {
            klog(LOG_ERROR, "ext4: inode %u: its extent tree cannot be followed", num);
            break;
        }
        h    = p[d].h;
        voci = le16(h + 2);
        for (k = 0; k < voci && le32(VOCE(h, k)) <= n; k++) ;

        if (k > 0) {
            const uint8_t *e = VOCE(h, k - 1u);
            uint32_t       s = le32(e), l = le16(e + 4);

            if (l > EST_LUNG_MAX) {
                if (n - s < l - EST_LUNG_MAX) {
                    klog(LOG_WARN, "ext4: inode %u: an unwritten extent (fallocate) "
                                   "is not written by this driver", num);
                    break;
                }
            } else if (n - s < l) {
                if (le16(e + 6) != 0) break;
                if (nuovo) { libera_blocco(m, nuovo); (*charge)--; }
                return le32(e + 8) + (n - s);
            }
        }

        if (nuovo == 0) {
            nuovo = alloca_blocco_per(m, pref, 1);
            if (nuovo == 0) return 0;
            (*charge)++;
        }
        r = inserisci(m, mnt, num, inode, p, d, k, n, nuovo, pref, charge);
        if (r == 0) return nuovo;
        if (r < 0) break;
    }
    if (nuovo) { libera_blocco(m, nuovo); (*charge)--; }
    return 0;
}

/* =============================================================================
 * PRUNING: from logical block `primo` on, everything goes.
 * Returns 1 if the node is left empty, 0 if not, -1 on error.
 * ============================================================================= */
static int pota_nodo(Ext2Mount *m, uint32_t num, uint8_t *inode, uint32_t liv,
                     uint32_t blk, uint32_t primo, uint32_t *lib)
{
    uint8_t *h = liv ? g_nodo[liv] : inode + 40;
    uint32_t voci;
    int      cambiato = 0;

    if (liv >= EST_LIVELLI) return -1;
    if (liv && (blk >= m->n_blocchi || leggi_blocco(m, blk, h) != 0)) return -1;
    if (le16(h) != EXTENT_MAGIC) return -1;
    voci = le16(h + 2);

    if (le16(h + 6) == 0) {
        while (voci > 0) {
            uint8_t *e = VOCE(h, voci - 1u);
            uint32_t s = le32(e), l = le16(e + 4), fis = le32(e + 8), non = 0, tieni, k;

            if (l > EST_LUNG_MAX) { non = EST_LUNG_MAX; l -= EST_LUNG_MAX; }
            if (le16(e + 6) != 0) return -1;
            if (s + l <= primo) break;
            tieni = (s >= primo) ? 0u : primo - s;
            for (k = tieni; k < l; k++) {
                if (libera_blocco(m, fis + k) != 0) return -1;
                (*lib)++;
            }
            cambiato = 1;
            if (tieni > 0) { p16(e + 4, tieni + non); break; }
            memset(e, 0, 12);
            voci--;
        }
    } else {
        while (voci > 0) {
            uint8_t *e = VOCE(h, voci - 1u);
            uint32_t figlio = le32(e + 4);
            int      r;

            if (le16(e + 8) != 0) return -1;
            r = pota_nodo(m, num, inode, liv + 1u, figlio, primo, lib);
            if (r < 0) return -1;
            if (r == 0) break;              /* it keeps something: the ones before are whole */
            if (libera_blocco(m, figlio) != 0) return -1;
            (*lib)++;
            memset(e, 0, 12);
            voci--;
            cambiato = 1;
        }
    }
    p16(h + 2, voci);
    if (cambiato && liv && voci > 0) {
        somma_estesi(m, num, inode, h);
        if (scrivi_blocco(m, blk, h) != 0) return -1;
    }
    return voci == 0;
}

int est_pota(Ext2Mount *m, uint32_t num, uint8_t *inode, uint32_t primo, uint32_t *liberati)
{
    est_dimentica();
    if (pota_nodo(m, num, inode, 0, 0, primo, liberati) < 0) {
        klog(LOG_ERROR, "ext4: inode %u: its extent tree could not be cut", num);
        est_dimentica();
        return -1;
    }
    est_dimentica();
    if (le16(inode + 42) == 0) {                /* nothing left: an empty root */
        p16(inode + 44, 4u);
        p16(inode + 46, 0);
    }
    return 0;
}
