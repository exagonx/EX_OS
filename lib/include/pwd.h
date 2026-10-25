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
 * Gli utenti sono quelli di /boot/utenti (nome:uid:gid, lo stesso file di
 * login e di lib/exuser; dal 25 ottobre 2026, per l'utente mysql di
 * MariaDB). La casa e' /root o /home/<nome>; quella di chi chiede e' $HOME.
 * Senza il file (il sistema avviato dal CD) la risposta e' una voce sola,
 * fatta con l'ambiente: il nome da $USER, la casa da $HOME, l'uid da
 * getuid(). Gecko la usa per trovare la casa (il profilo). Solo in libc.a.
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
/* The whole list is the one entry: getpwent gives it once, then NULL until
 * setpwent starts over (MariaDB's user lookup). */
struct passwd *getpwent(void);
void setpwent(void);
void endpwent(void);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_PWD_H */
