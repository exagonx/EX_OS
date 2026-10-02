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
 * ! EX-OS non numera le interfacce: lo stack IP ne ha una e non ha un nome
 * da dare. if_nametoindex risponde 0 («non c'e'»), if_indextoname NULL,
 * if_nameindex un elenco vuoto: e' la risposta vera, e chi la riceve (libevent,
 * per gli indirizzi IPv6 con zona) sa gia' cosa farne. Solo in libc.a.
 * ============================================================================= */

#ifndef EXOS_NET_IF_H
#define EXOS_NET_IF_H

#include "../libc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IFNAMSIZ   16
#define IF_NAMESIZE IFNAMSIZ

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
