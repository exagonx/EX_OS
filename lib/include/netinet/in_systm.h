/* =============================================================================
 * lib/include/netinet/in_systm.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <netinet/in_systm.h> — the old BSD network types, in network byte order.
 *
 * Nothing in EX-OS uses them; the header is here because code written for
 * BSD includes it before <netinet/ip.h> without asking (MariaDB's
 * my_global.h, 132 files of the server). Same definitions as glibc's.
 * ============================================================================= */

#ifndef EXOS_NETINET_IN_SYSTM_H
#define EXOS_NETINET_IN_SYSTM_H

#include <stdint.h>

typedef uint16_t n_short;   /* short as received from the net */
typedef uint32_t n_long;    /* long as received from the net */
typedef uint32_t n_time;    /* ms since 00:00 UTC, byte rev */

#endif /* EXOS_NETINET_IN_SYSTM_H */
