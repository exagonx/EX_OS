/* =============================================================================
 * lib/include/resolv.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <resolv.h> — only res_init (18 November 2026, Qt's network includes it).
 *
 * ! THERE IS NO RESOLVER LIBRARY: getaddrinfo and gethostbyname ask the DNS
 * that DHCP gave the stack (IpConfig.dns) themselves, reading it at every
 * question (lib/libc.c, dns_chiedi). So res_init has nothing to read again
 * and says so with 0. res_query, res_search and dn_expand are not here: Qt's
 * QDnsLookup (MX, SRV records) is built without them
 * (tools/qt5-exos/costruisci.sh, -no-feature-dnslookup).
 *
 * ! struct __res_state IS ONLY A SHAPE: the fields Qt's QHostInfo names
 * (glibc's order), for code that hands one to res_ninit of a libresolv it
 * loads with dlopen. EX-OS has no libresolv, the dlopen fails, and Qt goes
 * without the local domain name. Nothing here fills one.
 * ============================================================================= */
#ifndef EXOS_RESOLV_H
#define EXOS_RESOLV_H

#include <netinet/in.h>
#include <netdb.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAXNS      3
#define MAXDNSRCH  6

struct __res_state {
    int                retrans, retry;
    unsigned long      options;
    int                nscount;
    struct sockaddr_in nsaddr_list[MAXNS];
    unsigned short     id;
    char              *dnsrch[MAXDNSRCH + 1];
    char               defdname[256];
};
typedef struct __res_state *res_state;

int res_init(void);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_RESOLV_H */
