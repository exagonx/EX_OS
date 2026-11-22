#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/librerie-exos/aggiorna-sysroot.sh - today's libc into the cross sysroots
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/librerie-exos/aggiorna-sysroot.sh
#
# The four toolchains (cross_build/exos-cross, exos-cross64 and the ones of
# cross_build/<this machine>): libc.a and crt0.o made from lib/libc.c and
# lib/start.S, all of lib/include, the protocol headers of ExWin and of the
# keyboard in include/exos. C++ ports cannot put lib/include before the
# compiler's own headers (libstdc++'s include_next), so what a port needs
# from the libc must be in the sysroot: after a change to the libc, this,
# then tools/librerie-exos/ricostruisci-tutto.sh for what depends on it.
# The first libc.a replaced is kept once as libc.a.prima-<day>-<month>.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
set -e
C32="-m32 -march=pentium-mmx -mtune=pentium-mmx -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables -Wall -O2 -std=c11 -ffunction-sections -fdata-sections -DEXOS_LIBC_PORTATI"
C64="-m64 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie -Wall -O2 -std=c11 -ffunction-sections -fdata-sections -DEXOS_LIBC_PORTATI"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
gcc -m32 -c lib/start.S -o "$W/crt0-32.o"
gcc $C32 -Ilib/include -c lib/libc.c -o "$W/libc32.o"; ar rcs "$W/libc32.a" "$W/libc32.o"
gcc -m64 -c lib/start.S -o "$W/crt0-64.o"
gcc $C64 -Ilib/include -c lib/libc.c -o "$W/libc64.o"; ar rcs "$W/libc64.a" "$W/libc64.o"
GIORNO=$(date +%d-%m)
# this machine's toolchains only: cross_build/salvati-* are kept copies
M="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
for coppia in ${EXOS_FUORI}/cross_build/exos-cross/i386-exos:32 ${EXOS_FUORI}/cross_build/exos-cross64/x86_64-exos:64 \
              ${EXOS_FUORI}/cross_build/$M/exos-cross/i386-exos:32 ${EXOS_FUORI}/cross_build/$M/exos-cross64/x86_64-exos:64; do
    S=${coppia%:*}; b=${coppia#*:}
    [ -d "$S/lib" ] || continue
    ls "$S"/lib/libc.a.prima-* > /dev/null 2>&1 || cp -p "$S/lib/libc.a" "$S/lib/libc.a.prima-$GIORNO" 2>/dev/null || true
    # ! A NEW FILE AND A RENAME: a port being linked now keeps the old one
    cp "$W/libc$b.a" "$S/lib/.libc.a.nuova"; mv -f "$S/lib/.libc.a.nuova" "$S/lib/libc.a"
    cp "$W/crt0-$b.o" "$S/lib/.crt0.o.nuovo"; mv -f "$S/lib/.crt0.o.nuovo" "$S/lib/crt0.o"
    cp -rp lib/include/. "$S/include/"
    mkdir -p "$S/include/exos"
    cp -p drivers/wserver/win_proto.h drivers/wserver/egl_exwin.h drivers/kbd/kbd_proto.h "$S/include/exos/"
    echo "[OK] $S ($b bit)"
done
