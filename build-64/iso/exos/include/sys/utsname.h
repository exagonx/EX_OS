/* =============================================================================
 * lib/include/sys/utsname.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/utsname.h> — come si chiama il sistema (@EXILLA-NSPR, 30 settembre 2026).
 *
 * Le stesse risposte del comando uname (bin/uname/uname.c): «EX-OS», il nome
 * della macchina da HOSTNAME e la versione da OSVER (le mette kernel.cfg),
 * «i386». Solo libc.a.
 * ============================================================================= */
#ifndef EXOS_SYS_UTSNAME_H
#define EXOS_SYS_UTSNAME_H

#include "../libc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define _UTSNAME_LENGTH 65

struct utsname {
    char sysname[_UTSNAME_LENGTH];
    char nodename[_UTSNAME_LENGTH];
    char release[_UTSNAME_LENGTH];
    char version[_UTSNAME_LENGTH];
    char machine[_UTSNAME_LENGTH];
};

int uname(struct utsname *u);

#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYS_UTSNAME_H */
