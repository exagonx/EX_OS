/* =============================================================================
 * lib/include/pwd.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <pwd.h> — l'utente, come lo chiede il codice POSIX.
 *
 * EX-OS non ha un file degli utenti da consultare: la risposta e' una sola
 * voce, fatta con l'ambiente di chi chiede — il nome da $USER, la casa da
 * $HOME, l'uid da getuid(). Gecko la usa per trovare la casa (il profilo).
 * Solo in libc.a.
 * ============================================================================= */

#ifndef EXOS_PWD_H
#define EXOS_PWD_H

#include "libc.h"

#ifdef __cplusplus
extern "C" {
#endif

struct passwd {
    char  *pw_name;
    char  *pw_passwd;
    uid_t  pw_uid;
    gid_t  pw_gid;
    char  *pw_gecos;
    char  *pw_dir;
    char  *pw_shell;
};

struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *nome);
int getpwuid_r(uid_t uid, struct passwd *pw, char *buf, size_t dim,
               struct passwd **risultato);
int getpwnam_r(const char *nome, struct passwd *pw, char *buf, size_t dim,
               struct passwd **risultato);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_PWD_H */
