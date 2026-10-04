/* =============================================================================
 * lib/include/endian.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <endian.h> — ordine dei byte, alla glibc (@EXILLA-NSS, 30 settembre 2026).
 * x86 e' little endian: le conversioni «le» non fanno niente, le «be» girano.
 * ============================================================================= */
#ifndef EXOS_ENDIAN_H
#define EXOS_ENDIAN_H

#include "libc.h"

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __BYTE_ORDER    __LITTLE_ENDIAN
#define LITTLE_ENDIAN   __LITTLE_ENDIAN
#define BIG_ENDIAN      __BIG_ENDIAN
#define BYTE_ORDER      __BYTE_ORDER

#define htole16(x) ((unsigned short)(x))
#define le16toh(x) ((unsigned short)(x))
#define htole32(x) ((unsigned int)(x))
#define le32toh(x) ((unsigned int)(x))
#define htole64(x) ((unsigned long long)(x))
#define le64toh(x) ((unsigned long long)(x))
#define htobe16(x) __builtin_bswap16(x)
#define be16toh(x) __builtin_bswap16(x)
#define htobe32(x) __builtin_bswap32(x)
#define be32toh(x) __builtin_bswap32(x)
#define htobe64(x) __builtin_bswap64(x)
#define be64toh(x) __builtin_bswap64(x)

#endif /* EXOS_ENDIAN_H */
