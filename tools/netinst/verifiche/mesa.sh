# pacchetto = mesa
# =============================================================================
# tools/netinst/verifiche/mesa.sh -- Mesa 26.2.6, OpenGL ES with EGL (voce 88)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The libraries are shared objects that a program loads at its start: they
# need the C++ runtime and libm in /lib (the system brings them) and libz in
# /exos/lib (group zlib). If they are all there, provaegl draws a triangle
# on a 64x64 pbuffer and reads it back: what it prints is the proof that
# OpenGL works on this machine, not only that the files arrived.
# (the EX-OS sh has if, not for: one block per file)
# =============================================================================
!silenced
if [ ! -f /exos/lib/libEGL.so.1 ]; then
    echo "  ! Manca /exos/lib/libEGL.so.1: netupdate -install:mesa"
    exit 1
fi
if [ ! -f /exos/lib/libGLESv2.so.2 ]; then
    echo "  ! Manca /exos/lib/libGLESv2.so.2: netupdate -install:mesa"
    exit 1
fi
if [ ! -f /exos/lib/libz.so.1 ]; then
    echo "  ! Mesa vuole /exos/lib/libz.so.1: netupdate -install:zlib"
    exit 1
fi
if [ ! -f /lib/libstdc++.so.6 ]; then
    echo "  ! Mesa vuole /lib/libstdc++.so.6, che qui non c'e': netupdate -check -yesall"
    exit 1
fi
if [ ! -f /lib/libgcc_s.so.1 ]; then
    echo "  ! Mesa vuole /lib/libgcc_s.so.1, che qui non c'e': netupdate -check -yesall"
    exit 1
fi
if [ ! -f /lib/libm.so.4 ]; then
    echo "  ! Mesa vuole /lib/libm.so.4, che qui non c'e': netupdate -check -yesall"
    exit 1
fi
if [ ! -f /exos/bin/provaegl ]; then
    echo "  ! Manca /exos/bin/provaegl: netupdate -install:mesa"
    exit 1
fi
/exos/bin/provaegl > /dev/null
if [ $? != 0 ]; then
    echo "  ! Mesa c'e' ma provaegl non disegna: lancia /exos/bin/provaegl"
    echo "    e guarda quale controllo dice [NO] (help problemi, Mesa)."
    exit 1
fi
exit 0
