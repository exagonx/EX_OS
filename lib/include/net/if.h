/* =============================================================================
 * lib/include/net/if.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <net/if.h> — le interfacce di rete per nome e per indice.
 *
 * (0.373, Qt's QNetworkInterface) The stack's interfaces have names: lo
 * (index 1, 127.0.0.1/8) and eth0 (index 2, the card ip.drv drives).
 * if_nametoindex, if_indextoname, if_nameindex, SIOCGIFCONF (the ones with
 * an address) and SIOCGIFNAME, FLAGS, ADDR, NETMASK, BRDADDR, MTU, INDEX,
 * HWADDR on any socket; getifaddrs in <ifaddrs.h>. IFF_RUNNING on eth0 means
 * "it has an address" (the stack does not say whether the cable is in).
 * Without a running stack: none. Until 0.373 every list was empty.
 * struct ifreq, struct ifconf, IFF_*, SIOCGIF*: Linux's layout and numbers.
 * More than one card: voce 101. Not in the floppy's libc.so.
 * ============================================================================= */

#ifndef EXOS_NET_IF_H
#define EXOS_NET_IF_H

#include "../libc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IFNAMSIZ   16
#define IF_NAMESIZE IFNAMSIZ

#define IFF_UP          0x1
#define IFF_BROADCAST   0x2
#define IFF_DEBUG       0x4
#define IFF_LOOPBACK    0x8
#define IFF_POINTOPOINT 0x10
#define IFF_NOTRAILERS  0x20
#define IFF_RUNNING     0x40
#define IFF_NOARP       0x80
#define IFF_PROMISC     0x100
#define IFF_ALLMULTI    0x200
#define IFF_MASTER      0x400
#define IFF_SLAVE       0x800
#define IFF_MULTICAST   0x1000

#define SIOCGIFNAME     0x8910
#define SIOCGIFCONF     0x8912
#define SIOCGIFFLAGS    0x8913
#define SIOCGIFADDR     0x8915
#define SIOCGIFDSTADDR  0x8917
#define SIOCGIFBRDADDR  0x8919
#define SIOCGIFNETMASK  0x891b
#define SIOCGIFMTU      0x8921
#define SIOCGIFHWADDR   0x8927
#define SIOCGIFINDEX    0x8933

struct ifmap {
    unsigned long  mem_start, mem_end;
    unsigned short base_addr;
    unsigned char  irq, dma, port;
};

struct ifreq {
    union { char ifrn_name[IFNAMSIZ]; } ifr_ifrn;
    union {
        struct sockaddr ifru_addr, ifru_dstaddr, ifru_broadaddr, ifru_netmask, ifru_hwaddr;
        short           ifru_flags;
        int             ifru_ivalue;
        int             ifru_mtu;
        struct ifmap    ifru_map;
        char            ifru_slave[IFNAMSIZ], ifru_newname[IFNAMSIZ];
        char           *ifru_data;
    } ifr_ifru;
};
#define ifr_name      ifr_ifrn.ifrn_name
#define ifr_addr      ifr_ifru.ifru_addr
#define ifr_dstaddr   ifr_ifru.ifru_dstaddr
#define ifr_broadaddr ifr_ifru.ifru_broadaddr
#define ifr_netmask   ifr_ifru.ifru_netmask
#define ifr_hwaddr    ifr_ifru.ifru_hwaddr
#define ifr_flags     ifr_ifru.ifru_flags
#define ifr_ifindex   ifr_ifru.ifru_ivalue
#define ifr_metric    ifr_ifru.ifru_ivalue
#define ifr_mtu       ifr_ifru.ifru_mtu
#define ifr_map       ifr_ifru.ifru_map
#define ifr_slave     ifr_ifru.ifru_slave
#define ifr_newname   ifr_ifru.ifru_newname
#define ifr_data      ifr_ifru.ifru_data

struct ifconf {
    int ifc_len;                /* in: room in ifc_buf; out: bytes used */
    union { char *ifcu_buf; struct ifreq *ifcu_req; } ifc_ifcu;
};
#define ifc_buf ifc_ifcu.ifcu_buf
#define ifc_req ifc_ifcu.ifcu_req

struct if_nameindex {
    unsigned int if_index;
    char        *if_name;
};

unsigned int         if_nametoindex(const char *nome);
char                *if_indextoname(unsigned int indice, char *nome);
struct if_nameindex *if_nameindex(void);
void                 if_freenameindex(struct if_nameindex *elenco);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_NET_IF_H */
