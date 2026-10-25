/* =============================================================================
 * lib/include/sys/types.h
 * EX-OS — Extensible Operating System
 *
 * Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * =============================================================================
 *
 * <sys/types.h> — i nomi POSIX dei tipi.
 *
 * Facciata sottile su libc.h, come gli altri header con i nomi standard:
 * i typedef stanno tutti li', in un posto solo, e qui c'e' soltanto il
 * nome che il codice di terzi include. Vedi lib/include/errno.h per il
 * ragionamento esteso sul perche' non si duplica niente.
 *
 * E' l'header che il codice scritto per POSIX include piu' spesso senza
 * chiedersi se c'e': cento inclusioni non condizionate nei soli sorgenti
 * di binutils. Mancava, e mancava in un modo che si vede solo quando si
 * prova a compilare qualcosa che non abbiamo scritto noi.
 * ============================================================================= */

#ifndef EXOS_SYS_TYPES_H
#define EXOS_SYS_TYPES_H

#include "../libc.h"

/* int8_t ... int64_t: glibc li definisce QUI (bits/stdint-intn.h), e il codice
 * di terzi li usa includendo solo <sys/types.h> o <stdlib.h> — parakeet.cpp di
 * Firefox scrive int64_t con dentro soltanto <string> e <vector>. Sono gli
 * stessi typedef di <stdint.h> (dalle macro del compilatore), quindi non
 * possono contraddirlo. */
#include <stdint.h>

/* The BSD names, which glibc gives here too (__USE_MISC): MariaDB's
 * safe_process and mariadb_rpl.c write uint and u_char with nothing else. */
typedef unsigned char       u_char;
typedef unsigned short      u_short;
typedef unsigned int        u_int;
typedef unsigned long       u_long;
typedef unsigned short      ushort;
typedef unsigned int        uint;
typedef unsigned long       ulong;
typedef uint8_t             u_int8_t;
typedef uint16_t            u_int16_t;
typedef uint32_t            u_int32_t;
typedef uint64_t            u_int64_t;

#endif /* EXOS_SYS_TYPES_H */
