/* tools/qt5-exos/mkspecs/exos-g++/qplatformdefs.h - Qt on EX-OS
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The POSIX definitions of Qt over the libc of EX-OS (lib/include). Linux's
 * file starts with <features.h>, glibc's: EX-OS has no such header and
 * needs none. Every header named here is one the libc has. */
#ifndef QPLATFORMDEFS_H
#define QPLATFORMDEFS_H

#include "qglobal.h"

#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>

#include "../common/posix/qplatformdefs.h"

#undef  QT_SOCKLEN_T
#define QT_SOCKLEN_T            socklen_t

#define QT_SNPRINTF             ::snprintf
#define QT_VSNPRINTF            ::vsnprintf

#endif /* QPLATFORMDEFS_H */
