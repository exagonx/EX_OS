'' =============================================================================
'' crt/exos/stdio.bi — <stdio.h> di EX-OS, per FreeBASIC
''
'' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
'' SPDX-License-Identifier: GPL-2.0-or-later
'' =============================================================================
'' Confrontato con lib/include/libc.h. La parte comune — printf, fgets,
'' fputs, fgetc… — la dichiara gia' crt/stdio.bi: qui c'e' solo cio' che
'' cambia da un sistema all'altro.
'' =============================================================================

#ifndef __crt_exos_stdio_bi__
#define __crt_exos_stdio_bi__

'' !! I NUMERI SONO I NOSTRI, e nessuno coincide con quelli di Linux.
'' Il .bi di Linux dice BUFSIZ 8192, FILENAME_MAX 4096, L_tmpnam 20,
'' TMP_MAX 238328. Chi dimensiona un proprio buffer su BUFSIZ, o un array
'' di nomi su FILENAME_MAX, ottiene una struttura che non combacia con
'' quella che la libc usa davvero.
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define BUFSIZ 4096
#define FILENAME_MAX 256
#define FOPEN_MAX 16
#define L_tmpnam 64
#define TMP_MAX 32
#define P_tmpdir "/tmp"

'' typedef struct _FILE FILE;
''
'' !! IL TIPO E' OPACO, e va tenuto tale. Su Linux il .bi lo alias a
'' _IO_FILE, che e' la struttura della glibc; qui la struttura e' nostra e
'' nessun programma ha motivo di guardarci dentro. `type FILE as any` fa
'' fallire un accesso ai campi in compilazione invece che a video.
type FILE as any

extern stdin alias "stdin" as FILE ptr
extern stdout alias "stdout" as FILE ptr
extern stderr alias "stderr" as FILE ptr

'' typedef long fpos_t;   (su Linux e' longint: 8 byte contro 4)
type fpos_t as clong

extern "C"

declare function snprintf (byval s as zstring ptr, byval n as size_t, byval format as zstring ptr, ...) as long
declare function vsnprintf (byval s as zstring ptr, byval n as size_t, byval format as zstring ptr, byval arg as va_list) as long

'' popen/pclose ci sono dal 5 agosto 2026: passano da /bin/sh -c.
declare function popen (byval as zstring ptr, byval as zstring ptr) as FILE ptr
declare function pclose (byval as FILE ptr) as long

declare function fileno (byval as FILE ptr) as long
declare function fdopen (byval as long, byval as zstring ptr) as FILE ptr

end extern

'' !! NON CI SONO getw e putw. Il .bi di Linux le dichiara; nella nostra
'' libc non esistono, e un programma che le usasse compilerebbe per poi non
'' collegarsi.

#endif '' __crt_exos_stdio_bi__
