#!/bin/bash
# =============================================================================
# tools/mesa-exos/prova-egl.sh - OpenGL ES 2 through EGL in EX-OS (voce 88)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/mesa-exos/prova-egl.sh         64 bit (dist/exos64.iso)
#     tools/mesa-exos/prova-egl.sh 32      32 bit (dist/exos.iso)
#
# Builds prova-egl.c with the cross compiler, LINKED to libEGL.so.1 and
# libGLESv2.so.2 of the sysroot (tools/mesa-exos/costruisci.sh puts them
# there), and runs it in EX-OS under QEMU with the shared objects it needs
# in /disk and LD_LIBRARY_PATH=/disk (tools/mesa-exos/librerie-prova.sh
# follows DT_NEEDED: no list written here).
# The checks are listed at the top of prova-egl.c. Must print
# "prova-egl: 0 NO".
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "${1:-64}" = 32 ]; then
    T=i386-exos; ARCH=""; BIT=32; X="$RADICE/cross_build/$MACCHINA/exos-cross"
else
    T=x86_64-exos; ARCH=x86_64; BIT=64; X="$RADICE/cross_build/$MACCHINA/exos-cross64"
fi
export PATH="$X/bin:$PATH"
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-egl-$T"
CD="${CD_FORZATO:-$RADICE/dist/exos.iso}"; [ -n "$ARCH" ] && CD="${CD_FORZATO:-$RADICE/dist/exos64.iso}"
mkdir -p "$D"
$T-gcc -O2 -o "$D/prova-egl" tools/mesa-exos/prova-egl.c -lEGL -lGLESv2 -lpthread || exit 1
$T-readelf -d "$D/prova-egl" | grep -q "NEEDED.*libEGL" || { echo "=== [NO] prova-egl non e' collegato a libEGL ==="; exit 1; }

. tools/mesa-exos/librerie-prova.sh
altri=$(librerie_prova "$D/prova-egl" $T "$X" $BIT "$D") || exit 1

# libEGL and libstdc++ keep their debug info: 45 MB, more than the 32 MB disk
# of esegui-in-exos.sh
uscita=$(DISCO_MB=${DISCO_MB:-128} AMBIENTE="LD_LIBRARY_PATH=/disk" ALTRI="$altri" EXOS_ARCH=$ARCH EXOS_RAM=${EXOS_RAM:-512M} \
         CD_FORZATO="$CD" SECONDI=${SECONDI:-90} tools/exilla/esegui-in-exos.sh "$D/prova-egl" "${@:2}")
echo "$uscita" | grep -a -E "\[OK\]|\[NO\]|prova-egl|EGL [0-9]|\| |shader|atteso|eglGetError|cannot open|undefined|FAULT|panic|PANIC"
esito=1
echo "$uscita" | grep -aq "prova-egl: 0 NO" && esito=0
[ $esito = 0 ] && echo "=== [OK] ===" || echo "=== [NO] ==="
exit $esito
