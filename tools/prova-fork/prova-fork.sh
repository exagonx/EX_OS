#!/bin/bash
# =============================================================================
# tools/prova-fork/prova-fork.sh - fork() in EX-OS (kernel 0.313, voce 33)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova-fork/prova-fork.sh         64 bit (dist/exos64.iso)
#     tools/prova-fork/prova-fork.sh 32      32 bit (dist/exos.iso)
#     tools/prova-fork/prova-fork.sh 64 --figli=2000   more children in check 8
#
# Builds prova-fork.c and runs it in EX-OS under QEMU. The checks
# are listed at the top of prova-fork.c. Must print "prova-fork: 0 NO".
# The CD is dist/exos.iso or dist/exos64.iso (CD_FORZATO to change it), with
# kernel 0.313 or later: make iso-exos / make ARCH=x86_64 iso64. EXOS_RAM is
# 512M: 300 children with a 128 KB kernel stack each do not fit in 32 MB.
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
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-fork-$T"
CD="${CD_FORZATO:-$RADICE/dist/exos.iso}"; [ -n "$ARCH" ] && CD="${CD_FORZATO:-$RADICE/dist/exos64.iso}"
mkdir -p "$D"
$T-gcc -O2 -o "$D/prova-fork" tools/prova-fork/prova-fork.c -lpthread || exit 1
uscita=$(EXOS_ARCH=$ARCH EXOS_RAM=${EXOS_RAM:-512M} CD_FORZATO="$CD" SECONDI=${SECONDI:-60} tools/exilla/esegui-in-exos.sh "$D/prova-fork" "${@:2}")
echo "$uscita" | grep -a -E "\[OK\]|\[NO\]|prova-fork|FAULT|panic|PANIC|WP"
esito=1
echo "$uscita" | grep -aq "prova-fork: 0 NO" && esito=0
[ $esito = 0 ] && echo "=== [OK] ===" || echo "=== [NO] ==="
exit $esito
