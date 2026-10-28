/* =============================================================================
 * lib/include/limits.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <limits.h> — i limiti del SISTEMA, non quelli dei tipi.
 *
 * ! QUESTO FILE NON DEFINISCE INT_MAX. I limiti dei tipi — CHAR_BIT,
 * INT_MAX, LONG_MIN e compagni — li dichiara il COMPILATORE, che per ogni
 * bersaglio sa quanto e' largo un `int`. GCC installa il proprio
 * <limits.h>, lo trova per primo, e da li' fa `#include_next <limits.h>`
 * per prendere anche QUESTO: e' il meccanismo previsto, ed e' il motivo
 * per cui i due file non si pestano i piedi.
 *
 * Qui ci stanno solo i limiti che il compilatore non puo' conoscere,
 * perche' dipendono dal kernel.
 *
 * ! PATH_MAX DEVE RESTARE UGUALE a VFS_PATH_MAX di
 * kernel/include/vfs.h: un programma che dimensiona un buffer su questa
 * costante e poi riceve un percorso piu' lungo dalla syscall scrive
 * fuori. La stessa costante compare in <sys/param.h> come MAXPATHLEN.
 * ============================================================================= */

#ifndef EXOS_LIMITS_H
#define EXOS_LIMITS_H

#define PATH_MAX        320     /* = VFS_PATH_MAX */
#define NAME_MAX        255     /* il massimo di ext2; su FAT i nomi sono 8.3 */
#define OPEN_MAX         128    /* the first block of the table; since 0.281 the real limit is sysconf(_SC_OPEN_MAX) = RLIMIT_NOFILE */
#define ARG_MAX        4096     /* argv + envp copiati sullo stack del figlio */
#define _POSIX_ARG_MAX 4096     /* the POSIX minimum; = ARG_MAX here */
#ifndef SSIZE_MAX
#define SSIZE_MAX __PTRDIFF_MAX__  /* ssize_t is __PTRDIFF_TYPE__ (libc.h); libgit2 */
#endif
#define LINK_MAX          1     /* non ci sono collegamenti: ogni file ha un nome */
#define PIPE_BUF       4096     /* dichiarato: le pipe non ci sono ancora */
#define SYMLINK_MAX       0     /* nessun collegamento simbolico */
#define HOST_NAME_MAX    64
#define _POSIX_PATH_MAX 256     /* the POSIX minimum, not PATH_MAX (MariaDB) */

/* GCC's own <limits.h> gives the long long limits only to C99 and C++11 with
 * __STDC_VERSION__/__cplusplus checks that an older -std leaves out; MariaDB's
 * C++ uses ULLONG_MAX with whatever it gets. Same values as the compiler's. */
#ifndef LLONG_MAX
#define LLONG_MAX   __LONG_LONG_MAX__
#endif
#ifndef LLONG_MIN
#define LLONG_MIN   (-LLONG_MAX - 1LL)
#endif
#ifndef ULLONG_MAX
#define ULLONG_MAX  (LLONG_MAX * 2ULL + 1ULL)
#endif

#endif /* EXOS_LIMITS_H */
