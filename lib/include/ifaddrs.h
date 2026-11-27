/* =============================================================================
 * <ifaddrs.h> - getifaddrs, freeifaddrs (0.373, Qt)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The interfaces of the network stack: lo (127.0.0.1/8) and eth0, the card
 * ip.drv drives. For each an AF_PACKET entry with the MAC (struct
 * sockaddr_ll) and, when it has an address, an AF_INET one with mask and,
 * on eth0, broadcast. IFF_RUNNING on eth0 means it has an address: the
 * stack does not say whether the cable is in. No stack: an empty list.
 * ============================================================================= */
#ifndef EXOS_IFADDRS_H
#define EXOS_IFADDRS_H

#include "libc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct ifaddrs {
    struct ifaddrs  *ifa_next;
    char            *ifa_name;
    unsigned int     ifa_flags;     /* IFF_* of <net/if.h> */
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    union {
        struct sockaddr *ifu_broadaddr;
        struct sockaddr *ifu_dstaddr;
    } ifa_ifu;
    void            *ifa_data;
};
#define ifa_broadaddr ifa_ifu.ifu_broadaddr
#define ifa_dstaddr   ifa_ifu.ifu_dstaddr

int  getifaddrs(struct ifaddrs **elenco);
void freeifaddrs(struct ifaddrs *elenco);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_IFADDRS_H */
