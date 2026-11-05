#!/bin/bash
# =============================================================================
# tools/prova-dl/prova-cxx.sh - C++ with libstdc++.so.6 and libgcc_s.so.1
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova-dl/prova-cxx.sh            64 bit (dist/exos64.iso)
#     tools/prova-dl/prova-cxx.sh 32         32 bit (dist/exos.iso)
#
# (voce 85, Qt 5.) Builds libcxx.so (C++) and prova-cxx linked to it, to
# libstdc++.so.6 and to libgcc_s.so.1 (tools/librerie-exos/cxx-condivise.sh
# first), and runs it in EX-OS with the three libraries on /disk. The point
# is ONE unwinder: an exception of the program that crosses a frame of the
# library, one of the library caught in the program, std::bad_alloc thrown
# inside libstdc++.so. Also std::thread, std::mutex, a guarded static, the
# global constructor of the library. Must print "prova-cxx: 0 NO".
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "${1:-64}" = 32 ]; then
    T=i386-exos; ARCH=""; BIT=32
    export PATH="$RADICE/cross_build/$MACCHINA/exos-cross/bin:$PATH"
else
    T=x86_64-exos; ARCH=x86_64; BIT=64
    export PATH="$RADICE/cross_build/$MACCHINA/exos-cross64/bin:$PATH"
fi
L="$RADICE/cross_build/cxx-exos$BIT/exos/lib"
[ -f "$L/libstdc++.so.6" ] || { echo "manca $L/libstdc++.so.6: tools/librerie-exos/cxx-condivise.sh $BIT"; exit 1; }
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-cxx-$T"
mkdir -p "$D"
set -e
$T-g++ -O2 -fPIC -shared -Wl,-soname,libcxx.so -o "$D/libcxx.so" tools/prova-dl/libcxx.cpp
$T-g++ -O2 -o "$D/prova-cxx" tools/prova-dl/prova-cxx.cpp -L"$D" -lcxx -lpthread
set +e
esito=0
for so in libstdc++.so.6 libgcc_s.so.1 libcxx.so; do
    $T-readelf -d "$D/prova-cxx" "$D/libcxx.so" | grep -q "NEEDED.*\[$so\]" ||
        { echo "  [NO] nessuno chiede $so"; esito=1; }
done
$T-nm -D --defined-only "$D/libcxx.so" | grep -qE " _Unwind_(SjLj_)?RaiseException$" &&
    { echo "  [NO] libcxx.so ha un unwinder suo"; esito=1; }
uscita=$(EXOS_ARCH=$ARCH SECONDI=20 ALTRI="$D/libcxx.so $L/libstdc++.so.6 $L/libgcc_s.so.1" \
         AMBIENTE="LD_LIBRARY_PATH=/disk" tools/exilla/esegui-in-exos.sh "$D/prova-cxx")
echo "$uscita" | grep -a -E "\[OK\]|\[NO\]|prova-cxx|terminate|FAULT|undefined"
echo "$uscita" | grep -aq "prova-cxx: 0 NO" || esito=1
[ $esito = 0 ] && echo "=== [OK] ===" || echo "=== [NO] ==="
exit $esito
