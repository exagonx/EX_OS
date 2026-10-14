#!/bin/bash
# =============================================================================
# tools/prova-dl/prova-dl.sh — the ELF loader (lib/libc_dl.c) inside EX-OS
#
#     tools/prova-dl/prova-dl.sh               64 bit (dist/exos64.iso)
#     EXOS_ARCH=i386 tools/prova-dl/prova-dl.sh 32 bit (the private CD, i386-exos)
#
# Builds libdue.so, libuno.so (with DT_NEEDED libdue.so) and prova-dl with the
# x86_64-exos cross compiler, puts them on a test disk and runs prova-dl in
# EX-OS (tools/exilla/esegui-in-exos.sh) with LD_LIBRARY_PATH=/disk.
# The libraries are linked with `gcc -shared`, which the EX-OS compilers
# accept once tools/gcc-exos/specs-condivise.sh has written their specs file.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "$EXOS_ARCH" = i386 ]; then
    T=i386-exos; ARCH=""
    export PATH="$RADICE/cross_build/$MACCHINA/exos-cross/bin:$RADICE/cross_build/exos-cross/bin:$PATH"
else
    T=x86_64-exos; ARCH=x86_64
    export PATH="$RADICE/cross_build/exos-cross64/bin:$PATH"
fi
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-dl-$T"
mkdir -p "$D"
set -e
$T-gcc -O2 -fPIC -shared -Wl,-soname,libdue.so -o "$D/libdue.so" tools/prova-dl/libdue.c
$T-gcc -O2 -fPIC -shared -Wl,-soname,libuno.so -o "$D/libuno.so" tools/prova-dl/libuno.c -L"$D" -ldue
$T-gcc -O2 -o "$D/prova-dl" tools/prova-dl/prova-dl.c -lpthread
set +e
uscita=$(EXOS_ARCH=$ARCH SECONDI=15 ALTRI="$D/libuno.so $D/libdue.so" AMBIENTE="LD_LIBRARY_PATH=/disk" \
         tools/exilla/esegui-in-exos.sh "$D/prova-dl")
echo "$uscita"
echo "$uscita" | grep -q "prova-dl: 0 NO" && { echo "=== [OK] ==="; exit 0; }
echo "=== [NO] ==="; exit 1
