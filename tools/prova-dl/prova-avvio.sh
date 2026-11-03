#!/bin/bash
# =============================================================================
# tools/prova-dl/prova-avvio.sh — a program linked to shared objects starts
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova-dl/prova-avvio.sh            64 bit (dist/exos64.iso)
#     tools/prova-dl/prova-avvio.sh 32         32 bit (dist/exos.iso)
#
# Builds libdue.so and libuno.so (DT_NEEDED libdue.so) as tools/prova-dl/
# prova-dl.sh does, then prova-avvio LINKED to them: `gcc ... -luno -ldue`,
# no -static and no dlopen. Runs it in EX-OS twice:
#   1. with LD_LIBRARY_PATH=/disk: must print "prova-avvio: 0 NO";
#   2. without it, so the libraries are not found: must stop before main
#      with the name of the missing library.
# Needs the sysroot libc with __exos_dl_avvio_programma and the specs of
# tools/gcc-exos/specs-condivise.sh in the compiler used.
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
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-avvio-$T"
mkdir -p "$D"
set -e
$T-gcc -O2 -fPIC -shared -Wl,-soname,libdue.so -o "$D/libdue.so" tools/prova-dl/libdue.c
$T-gcc -O2 -fPIC -shared -Wl,-soname,libuno.so -o "$D/libuno.so" tools/prova-dl/libuno.c -L"$D" -ldue
$T-gcc -O2 -o "$D/prova-avvio" tools/prova-dl/prova-avvio.c -L"$D" -luno -ldue -lpthread
set +e
$T-readelf -d "$D/prova-avvio" | grep -q NEEDED || { echo "=== [NO] prova-avvio non e' dinamico ==="; exit 1; }
esito=0
uscita=$(EXOS_ARCH=$ARCH SECONDI=15 ALTRI="$D/libuno.so $D/libdue.so" AMBIENTE="LD_LIBRARY_PATH=/disk" \
         tools/exilla/esegui-in-exos.sh "$D/prova-avvio")
echo "$uscita" | grep -a -E "\[OK\]|\[NO\]|prova-avvio"
if echo "$uscita" | grep -q "prova-avvio: 0 NO"; then echo "  [OK] 1. con le librerie"
else echo "  [NO] 1. con le librerie"; esito=1; fi
uscita=$(EXOS_ARCH=$ARCH SECONDI=15 tools/exilla/esegui-in-exos.sh "$D/prova-avvio")
if echo "$uscita" | grep -a -q "libuno.so: cannot open shared object file"; then
    echo "  [OK] 2. senza librerie si ferma prima di main e dice quale manca"
else
    echo "  [NO] 2. senza librerie:"; echo "$uscita" | tail -5; esito=1
fi
[ $esito = 0 ] && echo "=== [OK] ===" || echo "=== [NO] ==="
exit $esito
