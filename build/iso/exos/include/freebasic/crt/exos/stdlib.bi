'' =============================================================================
'' crt/exos/stdlib.bi — <stdlib.h> di EX-OS, per FreeBASIC
''
'' Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
'' SPDX-License-Identifier: GPL-2.0-or-later
'' =============================================================================

#ifndef __crt_exos_stdlib_bi__
#define __crt_exos_stdlib_bi__

extern "C"

'' Verificata in lib/include/libc.h. La usa anche il driver di GCC per i
'' propri file di passaggio: la directory viene da TMPDIR.
declare function mkstemp (byval template_ as zstring ptr) as long

end extern

#endif '' __crt_exos_stdlib_bi__
