/* =============================================================================
 * lib/include/utime.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <utime.h> — the dates of a file.
 *
 * Since kernel 0.373 utime() really writes them (SYS_TEMPI): ext keeps
 * seconds, FAT two of them and the access only as a day. With a kernel
 * before it, 0 if the file is there and nothing changed, as until then.
 * ============================================================================= */

#ifndef EXOS_UTIME_H
#define EXOS_UTIME_H

#include "libc.h"

/* What POSIX passes to utime(). */
struct utimbuf {
    time_t actime;      /* ultimo accesso */
    time_t modtime;     /* ultima modifica */
};

#endif /* EXOS_UTIME_H */
