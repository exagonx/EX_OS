#!/bin/bash
# =============================================================================
# tools/prova-fili-core/prova-fili-core.sh - the threads of one program on
# several processors at once, and the TLB shootdown (kernel 0.321, voce 92 b)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova-fili-core/prova-fili-core.sh         64 bit (dist/exos64.iso)
#     tools/prova-fili-core/prova-fili-core.sh 32      32 bit (dist/exos.iso)
#     CPU=4 tools/prova-fili-core/prova-fili-core.sh   4 processors (2)
#
# Builds prova-fili-core.c with the cross gcc and runs it in EX-OS under QEMU
# with CPU processors (KVM when /dev/kvm is writable). The checks are listed
# at the top of prova-fili-core.c. Must print "prova-fili-core: 0 NO". The
# CD must have kernel 0.321 or later: make iso-exos / make ARCH=x86_64 iso64.
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
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-fili-core-$T"
CD="${CD_FORZATO:-$RADICE/dist/exos.iso}"; [ -n "$ARCH" ] && CD="${CD_FORZATO:-$RADICE/dist/exos64.iso}"
mkdir -p "$D"
$T-gcc -O2 -o "$D/prova-fili-core" tools/prova-fili-core/prova-fili-core.c -lpthread || exit 1
uscita=$(EXOS_ARCH=$ARCH EXOS_RAM=${EXOS_RAM:-512M} CD_FORZATO="$CD" SECONDI=${SECONDI:-90} \
         QEMU_PIU="-smp ${CPU:-2}" tools/exilla/esegui-in-exos.sh "$D/prova-fili-core" "${@:2}")
echo "$uscita" | grep -a -E "\[OK\]|\[NO\]|prova-fili-core|FAULT|panic|PANIC|SMP:"
esito=1
echo "$uscita" | grep -aq "prova-fili-core: 0 NO" && esito=0
echo "$uscita" | grep -aq "\[FAULT\]\|PANIC\|panic" && esito=1
[ $esito = 0 ] && echo "=== [OK] ===" || echo "=== [NO] ==="
exit $esito
