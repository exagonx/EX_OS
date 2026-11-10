# pacchetto = exilla
# =============================================================================
# tools/netinst/verifiche/exilla.sh -- Exilla and its CJK fonts (voce 6)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# From 9 November 2026 Exilla wants the package [font-cjk] (Noto Sans CJK and
# Noto Serif CJK, 90 MB in /exwin/font): -install:exilla brings it, but a
# machine that had Exilla before gets only the new catalogue from -check, not
# a package it never installed. Without the fonts the Chinese, Japanese and
# Korean pages come out as boxes: say how to get them.
# (the EX-OS sh has if, not for: one block per file)
# =============================================================================
!silenced
if [ ! -f /exwin/app/exilla/firefox ]; then
    echo "  ! Manca /exwin/app/exilla/firefox: netupdate -install:exilla"
    exit 1
fi
if [ ! -f /exwin/font/NotoSansCJK-Regular.ttc ]; then
    echo "  ! Exilla scrive a quadratini le pagine cinesi, giapponesi e coreane:"
    echo "    mancano i loro caratteri (90 MB). netupdate -install:font-cjk"
    exit 1
fi
if [ ! -f /exwin/font/NotoSerifCJK-Regular.ttc ]; then
    echo "  ! Mancano i caratteri CJK con le grazie: netupdate -install:font-cjk"
    exit 1
fi
exit 0
