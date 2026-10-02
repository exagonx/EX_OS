/* =============================================================================
 * lib/include/syslog.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under the GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <syslog.h> — il registro di sistema (@EXILLA-NSS, 30 settembre 2026).
 *
 * EX-OS non ha un demone syslog: ogni messaggio va nel registro del kernel
 * con log_seriale(), preceduto dal nome dato a openlog. Solo libc.a.
 * ============================================================================= */
#ifndef EXOS_SYSLOG_H
#define EXOS_SYSLOG_H

#include "libc.h"

#define LOG_EMERG    0
#define LOG_ALERT    1
#define LOG_CRIT     2
#define LOG_ERR      3
#define LOG_WARNING  4
#define LOG_NOTICE   5
#define LOG_INFO     6
#define LOG_DEBUG    7

#define LOG_PID      0x01
#define LOG_CONS     0x02
#define LOG_NDELAY   0x08
#define LOG_USER     (1 << 3)
#define LOG_DAEMON   (3 << 3)
#define LOG_AUTH     (4 << 3)
#define LOG_LOCAL0   (16 << 3)

#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
void openlog(const char *nome, int opzioni, int servizio);
void syslog(int priorita, const char *fmt, ...);
void vsyslog(int priorita, const char *fmt, va_list ap);
void closelog(void);
#ifdef __cplusplus
}
#endif

#endif /* EXOS_SYSLOG_H */
