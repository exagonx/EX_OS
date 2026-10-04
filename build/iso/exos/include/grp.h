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
 * Come <pwd.h>: nessun file dei gruppi, una voce sola. getgrgid(getgid())
 * risponde un gruppo che ha il nome dell'utente ($USER) e nessun altro
 * membro; ogni altra domanda risponde NULL. Solo in libc.a.
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
