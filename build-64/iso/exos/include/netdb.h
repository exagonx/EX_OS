/* =============================================================================
 * lib/include/netdb.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <netdb.h> — facciata su libc.h, sezione «I SOCKET BSD» (@SOCKET-BSD, 29
 * settembre 2026). Tutto sta la', in un posto solo; cosa c'e' e cosa no e'
 * scritto in testa a quella sezione.
 * ============================================================================= */
#ifndef EXOS_NETDB_H
#define EXOS_NETDB_H

#include "libc.h"

/* ! QUI E NON IN libc.h (30 settembre 2026): libc.h e' incluso da tutto, e
 * NO_DATA & c. come macro globali si scontravano con un enum di SpiderMonkey
 * (ArrayBufferObject::NO_DATA). Nelle libc vere stanno in <netdb.h>. */
#ifdef __cplusplus
extern "C" {
#endif
/* (@EXILLA-NSPR, 30 settembre 2026) gethostbyaddr: EX-OS non fa ricerche
 * inverse, e allora rende il solo numero come nome — che e' quello che fa
 * anche un resolver vero quando il DNS non sa rispondere. h_errno e' uno
 * solo per il processo (non per filo): chi vuole la risposta precisa usa
 * getaddrinfo. I protocolli sono i quattro che lo stack conosce. */
struct hostent *gethostbyaddr(const void *ind, socklen_t len, int famiglia);
extern int      h_errno;
#define HOST_NOT_FOUND  1
#define TRY_AGAIN       2
#define NO_RECOVERY     3
#define NO_DATA         4
const char     *hstrerror(int e);

struct protoent {
    char  *p_name;
    char **p_aliases;
    int    p_proto;
};
struct protoent *getprotobyname(const char *nome);
struct protoent *getprotobynumber(int numero);
#ifdef __cplusplus
}
#endif

#endif /* EXOS_NETDB_H */
