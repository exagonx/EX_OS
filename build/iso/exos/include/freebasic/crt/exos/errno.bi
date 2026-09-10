'' =============================================================================
'' crt/exos/errno.bi — i valori di errno di EX-OS, per FreeBASIC
''
'' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
'' SPDX-License-Identifier: GPL-2.0-or-later
'' =============================================================================
''
'' !! I VALORI DI crt/errno.bi NON VANNO BENE QUI, e la differenza e' muta.
'' Quel file usa la numerazione di MSVC; EX-OS usa quella di Linux, che per
'' sei codici e' un'altra:
''
''            crt/errno.bi   EX-OS
''     EDEADLK          36      35
''     ENAMETOOLONG     38      36
''     ENOLCK           39      37
''     ENOSYS           40      38
''     ENOTEMPTY        41      39
''     EILSEQ           42      84
''
'' Un programma che confronta `errno = ENOSYS` compilerebbe benissimo e non
'' entrerebbe mai nel ramo giusto: nessun avviso, nessun errore, solo un
'' controllo che non scatta.
''
'' Presi da lib/include/libc.h. errno stesso e' una variabile globale
'' (`extern int errno`), non una funzione come nella glibc: se ne occupa il
'' ramo #else di crt/errno.bi.
'' =============================================================================

#ifndef __crt_exos_errno_bi__
#define __crt_exos_errno_bi__

#define EPERM            1
#define ENOENT           2
#define ESRCH            3
#define EINTR            4
#define EIO              5
#define ENXIO            6
#define E2BIG            7
#define ENOEXEC          8
#define EBADF            9
#define ECHILD           10
#define EAGAIN           11
#define ENOMEM           12
#define EACCES           13
#define EFAULT           14
#define EBUSY            16
#define EEXIST           17
#define EXDEV            18
#define ENODEV           19
#define ENOTDIR          20
#define EISDIR           21
#define EINVAL           22
#define ENFILE           23
#define EMFILE           24
#define ENOTTY           25
#define EFBIG            27
#define ENOSPC           28
#define ESPIPE           29
#define EROFS            30
#define EMLINK           31
#define EPIPE            32
#define EDOM             33
#define ERANGE           34
#define ENAMETOOLONG     36
#define ENOSYS           38
#define ENOTEMPTY        39
#define ELOOP            40
#define EILSEQ           84
#define ETIMEDOUT        110
#define ENOMEDIUM        123
#define EDEADLK          35
#define ENOLCK           37
#define ENOMSG           42
#define ENOTSOCK         88
#define EDESTADDRREQ     89
#define EMSGSIZE         90
#define EPROTOTYPE       91
#define ENOPROTOOPT      92
#define EPROTONOSUPPORT  93
#define EOPNOTSUPP       95
#define EAFNOSUPPORT     97
#define EADDRINUSE       98
#define EADDRNOTAVAIL    99
#define ENETDOWN         100
#define ENETUNREACH      101
#define ENETRESET        102
#define ECONNABORTED     103
#define ECONNRESET       104
#define ENOBUFS          105
#define EISCONN          106
#define ENOTCONN         107
#define ECONNREFUSED     111
#define EHOSTUNREACH     113
#define EALREADY         114
#define EINPROGRESS      115
#define EIDRM            43
#define ETXTBSY          26
#define EOVERFLOW        75
#define ECANCELED        125
#define EOWNERDEAD       130
#define ENOTRECOVERABLE  131
#define ENODATA          61
#define ENOSR            63
#define ENOSTR           60
#define ETIME            62
#define EBADMSG          74
#define ENOLINK          67
#define EPROTO           71

#endif '' __crt_exos_errno_bi__
