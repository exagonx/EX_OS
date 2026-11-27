/* =============================================================================
 * <netpacket/packet.h> - struct sockaddr_ll (0.373)
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Only as getifaddrs gives it: the AF_PACKET entry of each interface carries
 * its hardware address (Qt's QNetworkInterface reads the MAC there). EX-OS
 * opens no AF_PACKET socket. The layout is Linux's.
 * ============================================================================= */
#ifndef EXOS_NETPACKET_PACKET_H
#define EXOS_NETPACKET_PACKET_H

#include "../libc.h"

struct sockaddr_ll {
    unsigned short sll_family;      /* AF_PACKET */
    unsigned short sll_protocol;
    int            sll_ifindex;
    unsigned short sll_hatype;      /* ARPHRD_ETHER 1, ARPHRD_LOOPBACK 772 */
    unsigned char  sll_pkttype;
    unsigned char  sll_halen;
    unsigned char  sll_addr[8];
};

#define ARPHRD_ETHER     1
#define ARPHRD_LOOPBACK  772

#endif /* EXOS_NETPACKET_PACKET_H */
