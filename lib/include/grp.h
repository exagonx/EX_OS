/* =============================================================================
 * lib/include/grp.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <grp.h> — i gruppi, come li chiede il codice POSIX.
 *
 * Nessun file dei gruppi: ogni utente di /boot/utenti (vedi <pwd.h>) ha un
 * gruppo col suo nome e il suo gid, e nessun altro membro. Senza il file,
 * solo getgrgid(getgid()), col nome di $USER. Solo in libc.a.
 * ============================================================================= */

#ifndef EXOS_GRP_H
#define EXOS_GRP_H

#include "libc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct group {
    char  *gr_name;
    char  *gr_passwd;
    gid_t  gr_gid;
    char **gr_mem;
};

struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *nome);
int getgroups(int dim, gid_t elenco[]);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_GRP_H */
