/* =============================================================================
 * lib/include/byteswap.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <byteswap.h> — the glibc names for turning the bytes round (Qt's sha3,
 * 18 November 2026): GCC's builtins, as endian.h uses them.
 * ============================================================================= */
#ifndef EXOS_BYTESWAP_H
#define EXOS_BYTESWAP_H

#define bswap_16(x) __builtin_bswap16(x)
#define bswap_32(x) __builtin_bswap32(x)
#define bswap_64(x) __builtin_bswap64(x)

#endif /* EXOS_BYTESWAP_H */
