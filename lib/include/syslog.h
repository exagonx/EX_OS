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
#define LOG_ODELAY   0x04
#define LOG_NDELAY   0x08
#define LOG_NOWAIT   0x10
#define LOG_PERROR   0x20

/* The facilities, with Linux's numbers (Apache's ErrorLog syslog:<name>
 * names them all, 28 October 2026): EX-OS keeps one log, so openlog and
 * syslog accept the facility and do not use it. */
#define LOG_KERN     (0 << 3)
#define LOG_USER     (1 << 3)
#define LOG_MAIL     (2 << 3)
#define LOG_DAEMON   (3 << 3)
#define LOG_AUTH     (4 << 3)
#define LOG_SYSLOG   (5 << 3)
#define LOG_LPR      (6 << 3)
#define LOG_NEWS     (7 << 3)
#define LOG_UUCP     (8 << 3)
#define LOG_CRON     (9 << 3)
#define LOG_AUTHPRIV (10 << 3)
#define LOG_FTP      (11 << 3)
#define LOG_LOCAL0   (16 << 3)
#define LOG_LOCAL1   (17 << 3)
#define LOG_LOCAL2   (18 << 3)
#define LOG_LOCAL3   (19 << 3)
#define LOG_LOCAL4   (20 << 3)
#define LOG_LOCAL5   (21 << 3)
#define LOG_LOCAL6   (22 << 3)
#define LOG_LOCAL7   (23 << 3)

#define LOG_PRIMASK  0x07
#define LOG_FACMASK  0x03f8
#define LOG_PRI(p)   ((p) & LOG_PRIMASK)
#define LOG_MAKEPRI(fac, pri) ((fac) | (pri))
#define LOG_MASK(pri) (1 << (pri))
#define LOG_UPTO(pri) ((1 << ((pri) + 1)) - 1)

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
