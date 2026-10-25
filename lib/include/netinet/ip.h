/* =============================================================================
 * lib/include/netinet/ip.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <netinet/ip.h> — the IPv4 header and the type-of-service values.
 *
 * There are no raw sockets in EX-OS, so nobody builds these headers by hand:
 * the file is for code that includes it out of habit (MariaDB's my_net.h,
 * 152 files) and for IPTOS_THROUGHPUT, which vio passes to setsockopt(IP_TOS)
 * and the stack accepts without effect (see setsockopt in lib/libc.c).
 * Layouts and values are glibc's (RFC 791, RFC 1349).
 * ============================================================================= */

#ifndef EXOS_NETINET_IP_H
#define EXOS_NETINET_IP_H

#include <stdint.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>

struct iphdr {
    uint8_t  ihl:4, version:4;  /* little endian: the low nibble first */
    uint8_t  tos;
    uint16_t tot_len;
    uint16_t id;
    uint16_t frag_off;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t check;
    uint32_t saddr;
    uint32_t daddr;
};

struct ip {
    unsigned int   ip_hl:4, ip_v:4;
    uint8_t        ip_tos;
    unsigned short ip_len;
    unsigned short ip_id;
    unsigned short ip_off;
    uint8_t        ip_ttl;
    uint8_t        ip_p;
    unsigned short ip_sum;
    struct in_addr ip_src, ip_dst;
};

#define IP_RF           0x8000  /* reserved fragment flag */
#define IP_DF           0x4000  /* don't fragment */
#define IP_MF           0x2000  /* more fragments */
#define IP_OFFMASK      0x1fff
#define IP_MAXPACKET    65535
#define IPVERSION       4
#define MAXTTL          255
#define IPDEFTTL        64

#define IPTOS_TOS_MASK      0x1E
#define IPTOS_TOS(tos)      ((tos) & IPTOS_TOS_MASK)
#define IPTOS_LOWDELAY      0x10
#define IPTOS_THROUGHPUT    0x08
#define IPTOS_RELIABILITY   0x04
#define IPTOS_LOWCOST       0x02
#define IPTOS_MINCOST       IPTOS_LOWCOST

#endif /* EXOS_NETINET_IP_H */
