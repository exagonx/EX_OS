/* =============================================================================
 * bin/mkfs/crc32c.h
 * EX-OS - Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * crc32c as ext4 uses it: no final inversion, the seed is passed by the
 * caller. mkfs writes these sums and chkdsk checks and rewrites them, so the
 * function lives in one place; the kernel's copy is in
 * kernel/moduli/ext4/somme.c.
 * ============================================================================= */
#ifndef MKFS_CRC32C_H
#define MKFS_CRC32C_H

static unsigned int crc32c_tab[256];

static unsigned int crc32c(unsigned int crc, const void *dati, unsigned int n)
{
    const unsigned char *p = (const unsigned char *)dati;
    unsigned int i;

    if (crc32c_tab[1] == 0) {
        for (i = 0; i < 256u; i++) {
            unsigned int c = i, k;

            for (k = 0; k < 8u; k++)
                c = (c & 1u) ? (c >> 1) ^ 0x82F63B78u : c >> 1;
            crc32c_tab[i] = c;
        }
    }
    for (i = 0; i < n; i++)
        crc = crc32c_tab[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    return crc;
}

#endif /* MKFS_CRC32C_H */
