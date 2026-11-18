# pacchetto = nano
# =============================================================================
# tools/netinst/verifiche/nano.sh -- GNU nano 9.3 (18 November 2026)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# nano reads /exos/nano/nanorc, which includes the syntax files of
# /exos/nano/share/nano: without them it still edits, in black and white,
# and says "Error reading /exos/nano/nanorc" at every start. A TERM it does
# not know gives "Error opening terminal": the console is vga, telnet brings
# the client's (xterm, linux, vt100 are inside the program; the others want
# the terminfo of [curses]).
# =============================================================================
!silenced
if [ ! -f /exos/bin/nano ]; then
    echo "  ! nano: /exos/bin/nano non c'e': netupdate -install:nano"
    exit 1
fi
if [ ! -f /exos/nano/nanorc ]; then
    echo "  ! nano: manca /exos/nano/nanorc: netupdate -install:nano (rimette anche la sintassi)"
    exit 1
fi
if [ ! -f /exos/nano/share/nano/c.nanorc ]; then
    echo "  ! nano: manca la sintassi in /exos/nano/share/nano: netupdate -install:nano"
    exit 1
fi
exit 0
