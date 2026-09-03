/* =============================================================================
 * lib/include/sys/stat.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * This file is part of EX-OS, distributed under la GNU GPL v2.
 * See the LICENSE file in the project root for the full license text.
 * =============================================================================
 *
 * <sys/stat.h> — che cos'e' un file.
 *
 * Facciata su libc.h, dove stanno struct stat, stat(), fstat(), le macro
 * S_IS* e statraw() — quest'ultima per chi vuole i campi grezzi del
 * filesystem invece della forma POSIX.
 *
 * ! I PERMESSI SONO UNA FINZIONE. EX-OS non ha utenti, gruppi ne'
 * permessi: st_mode li riporta ricostruiti dall'unico attributo che il
 * filesystem tiene davvero, quello di sola lettura.
 *
 * chmod(), fchmod() e umask() ESISTONO ma non cambiano niente, e fino ad
 * agosto 2026 non esistevano affatto — per non promettere qualcosa che non
 * succede. Le ha fatte entrare bfd, che chiude ogni file eseguibile che
 * produce con umask/chmod: l'alternativa era rattoppare i sorgenti di
 * terzi uno per uno, a ogni loro rilascio. Il nome c'e', il commento dice
 * forte che e' inerte — la stessa convenzione di O_EXCL in <fcntl.h>.
 * ============================================================================= */

#ifndef EXOS_SYS_STAT_H
#define EXOS_SYS_STAT_H

#include "../libc.h"

/* Bit dei permessi, per chi stampa un elenco in stile ls. Ci sono perche'
 * il codice che formatta "rwxr-xr-x" li nomina uno per uno; il valore che
 * ci trova dentro e' quello ricostruito, non un permesso vero. */
#define S_IRWXU     0700
#define S_IRUSR     0400
#define S_IWUSR     0200
#define S_IXUSR     0100
#define S_IRWXG     0070
#define S_IRGRP     0040
#define S_IWGRP     0020
#define S_IXGRP     0010
#define S_IRWXO     0007
#define S_IROTH     0004
#define S_IWOTH     0002
#define S_IXOTH     0001

/* Tipi che EX-OS non puo' avere: nessun collegamento simbolico, nessuna
 * pipe con un nome, nessun dispositivo nel filesystem. Le costanti ci
 * sono perche' uno switch sul tipo le elenca tutte; S_ISLNK e compagne
 * rispondono sempre falso, che e' la verita'. */
#define S_IFCHR     0020000
#define S_IFBLK     0060000
#define S_IFIFO     0010000
#define S_IFLNK     0120000
#define S_IFSOCK    0140000

#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)
#define S_ISBLK(m)  (((m) & S_IFMT) == S_IFBLK)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISLNK(m)  (((m) & S_IFMT) == S_IFLNK)
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)

#endif /* EXOS_SYS_STAT_H */
