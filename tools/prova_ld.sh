#!/bin/bash
# =============================================================================
# tools/prova_ld.sh - the shared libc, /lib/ld-exos.so (voce 105)
#
#     tools/prova_ld.sh          32 and 64 bit
#     tools/prova_ld.sh 64       only one
#
# Builds /lib/ld-exos.so and libexos_avvio.a HERE from lib/libc.c
# (tools/costruisci-ld.sh) (the test
# says whether today's code works, not whether the sysroot was redone), links
# tools/prove/ldprova.c to them without -static and runs it in QEMU from
# dist/exos.iso / dist/exos64.iso (kernel 0.350, PT_INTERP, ld-exos.so in
# /lib of the CD: make iso-exos / iso64 again after changing the libc). Must print "ldprova: N/N".
# Since kernel 0.351 it also weighs the cache of the shared objects: six
# copies of ldprova asleep against six of ldprova-statico (the same, -static):
# the shared ones may cost their writable pages more, not ld-exos.so's code.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
tot=0

giro() {    # $1 = 32|64
    local bit=$1 T TCB D arch cd uscita
    if [ "$bit" = 64 ]; then
        T=x86_64-exos; TCB="$PWD/cross_build/$MACCHINA/exos-cross64/bin"; arch=x86_64
        cd=dist/exos64.iso
    else
        T=i386-exos; TCB="$PWD/cross_build/$MACCHINA/exos-cross/bin"; arch=""
        cd=dist/exos.iso
    fi
    D="$PWD/cross_build/$MACCHINA/costruzione-ld-$T"
    mkdir -p "$D/lib"
    echo "=== $bit bit ==="
    bash tools/costruisci-ld.sh $bit "$D/lib" || { tot=$((tot+1)); return; }
    "$TCB/$T-gcc" -O2 -Wall -I lib/include -L "$D/lib" tools/prove/ldprova.c \
        -o "$D/ldprova" || { tot=$((tot+1)); return; }
    readelf -l "$D/ldprova" | grep -q "interpreter: /lib/ld-exos.so" ||
        { echo "[NO] ldprova has no PT_INTERP"; tot=$((tot+1)); return; }
    # the yardstick of the memory check (kernel 0.351): the same, with libc.a
    "$TCB/$T-gcc" -O2 -Wall -static -I lib/include tools/prove/ldprova.c \
        -o "$D/ldprova-statico" || { tot=$((tot+1)); return; }
    uscita=$(EXOS_ARCH=$arch CD_FORZATO="$PWD/$cd" SECONDI=${SECONDI:-40} ALTRI="$D/ldprova-statico" \
             bash tools/exilla/esegui-in-exos.sh "$D/ldprova")
    echo "$uscita" | grep -a -E "\[OK\]|\[NO\]|ldprova|FAULT|PANIC|ld-exos"
    echo "$uscita" | grep -aqE "ldprova: ([0-9]+)/\1 " || tot=$((tot+1))
}

case "${1:-}" in
    32) giro 32 ;;
    64) giro 64 ;;
    *)  giro 32; giro 64 ;;
esac
[ $tot = 0 ] && echo "=== [OK] voce 105, la libc condivisa ===" || echo "=== [NO] $tot giri ==="
[ $tot = 0 ]
