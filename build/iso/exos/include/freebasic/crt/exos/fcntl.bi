'' =============================================================================
'' crt/exos/fcntl.bi — <fcntl.h> di EX-OS, per FreeBASIC
''
'' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
'' SPDX-License-Identifier: GPL-2.0-or-later
'' =============================================================================
'' I valori sono quelli di lib/include/libc.h, e coincidono con quelli di
'' Linux: O_CREAT &h40, O_TRUNC &h200, O_APPEND &h400, O_NONBLOCK &h800.
''
'' !! SI DICHIARANO SOLO I FLAG CHE open() ONORA DAVVERO. Il .bi di Linux ne
'' elenca una ventina — O_EXCL, O_DIRECTORY, O_CLOEXEC, O_DIRECT, O_NOATIME
'' — e un flag accettato e ignorato e' una promessa: il programma crede di
'' aver chiesto qualcosa e non se ne accorge. Se un giorno la open di EX-OS
'' ne onora uno in piu', si aggiunge qui.
'' =============================================================================

#ifndef __crt_exos_fcntl_bi__
#define __crt_exos_fcntl_bi__

#include once "crt/sys/types.bi"

extern "C"

#define O_RDONLY   &h0
#define O_WRONLY   &h1
#define O_RDWR     &h2
#define O_ACCMODE  &h3
#define O_CREAT    &h40
#define O_TRUNC    &h200
#define O_APPEND   &h400
#define O_NONBLOCK &h800

declare function open_ alias "open" (byval as zstring ptr, byval as long, ...) as long
declare function creat (byval as zstring ptr, byval as mode_t) as long

end extern

#endif '' __crt_exos_fcntl_bi__
