#!/bin/bash
# =============================================================================
# tools/prova-gnu/prova-gnu.sh - the libc Mesa asks for (voce 88)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova-gnu/prova-gnu.sh         64 bit (dist/exos64.iso)
#     tools/prova-gnu/prova-gnu.sh 32      32 bit (dist/exos.iso)
#
# Builds prova-gnu.c with the cross compiler (the sysroot must have the new
# libc: tools/gcc-exos/prepara-cross.sh and cross64.sh sysroot) and runs it
# in EX-OS under QEMU. The checks are listed at the top of prova-gnu.c. Must
# print "prova-gnu: 0 NO".
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "${1:-64}" = 32 ]; then
    T=i386-exos; ARCH=""
    export PATH="$RADICE/cross_build/$MACCHINA/exos-cross/bin:$PATH"
else
    T=x86_64-exos; ARCH=x86_64
    export PATH="$RADICE/cross_build/$MACCHINA/exos-cross64/bin:$PATH"
fi
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-gnu-$T"
CD="${CD_FORZATO:-$RADICE/dist/exos.iso}"; [ -n "$ARCH" ] && CD="${CD_FORZATO:-$RADICE/dist/exos64.iso}"
mkdir -p "$D"
$T-gcc -O2 -o "$D/prova-gnu" tools/prova-gnu/prova-gnu.c -lpthread || exit 1
uscita=$(AMBIENTE="TMPDIR=/disk" EXOS_ARCH=$ARCH EXOS_RAM=${EXOS_RAM:-512M} CD_FORZATO="$CD" SECONDI=${SECONDI:-60} tools/exilla/esegui-in-exos.sh "$D/prova-gnu" "${@:2}")
echo "$uscita" | grep -a -E "\[OK\]|\[NO\]|prova-gnu|FAULT|panic|PANIC"
esito=1
echo "$uscita" | grep -aq "prova-gnu: 0 NO" && esito=0
[ $esito = 0 ] && echo "=== [OK] ===" || echo "=== [NO] ==="
exit $esito
