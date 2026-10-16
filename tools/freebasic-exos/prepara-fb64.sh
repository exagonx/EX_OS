#!/bin/bash
# =============================================================================
# tools/freebasic-exos/prepara-fb64.sh — FreeBASIC for 64-bit EX-OS (@FB-64)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/freebasic-exos/prepara-fb64.sh [libfb|prova|fbc|tutto]   (default: tutto)
#
# The 32-bit port (prepara-fb.sh) assembles fbc's own x86 output. At 64 bit
# FreeBASIC has no assembly backend of its own: programs go through C
# (`-gen gcc`), and so will the native fbc. This script uses the tree that
# bersaglio-exos-110.py patched in cross_build/fb64/albero (FreeBASIC 1.10.1,
# whose bin/fbc runs on the HOST and knows `-target exos`):
#
#   libfb    the runtime (src/rtlib, the EX-OS layer in src/rtlib/exos) with
#            x86_64-exos-gcc -> cross_build/fb64/fb-build-exos64/libfb.a and
#            fbrt0.o. x86/cpudetect.s is 32-bit code and is left out.
#   prova    tools/freebasic-exos/prova64.bas: host fbc -> C ->
#            x86_64-exos-gcc, linked with libfb.a, the binary next to libfb.
#   fbc      the NATIVE compiler: the 145 sources of src/compiler through the
#            host fbc (-gen gcc, as the 32-bit bootstrap but in C), compiled
#            with the flags fbc gives gcc for its own output, linked with
#            libfb. No -d ENABLE_STANDALONE, as at 32 bit: it finds its
#            prefix from where it runs (/exos/bin -> /exos), the .bi in
#            include/freebasic and libfb in lib/freebasic/exos-x86_64.
# =============================================================================
set -e
RADICE=$(cd "$(dirname "$0")/../.." && pwd)
ALBERO="$RADICE/cross_build/fb64/albero"
USCITA="$RADICE/cross_build/fb64/fb-build-exos64"
PREFISSO="$RADICE/cross_build/exos-cross64"
export PATH="$PREFISSO/bin:$PATH"
CC=x86_64-exos-gcc
FASE="${1:-tutto}"

[ -f "$ALBERO/src/rtlib/fb_config.h" ] || { echo "prepara-fb64: manca $ALBERO" >&2; exit 1; }
[ -d "$ALBERO/src/rtlib/exos" ] || { echo "prepara-fb64: $ALBERO senza lo strato exos (bersaglio-exos-110.py)" >&2; exit 1; }
command -v $CC > /dev/null || { echo "prepara-fb64: manca $CC (tools/gcc-exos/cross64.sh)" >&2; exit 1; }

libfb() {
    local OGG="$USCITA/obj" n=0 f
    rm -rf "$OGG"; mkdir -p "$OGG"
    echo "=== libfb.a per x86_64-exos ==="
    cd "$ALBERO/src/rtlib"
    for f in *.c; do
        [ "$f" = "thread_call.c" ] && continue          # wants libffi, as at 32 bit
        $CC -O2 -c -I. "$f" -o "$OGG/${f%.c}.o"
        n=$((n + 1))
    done
    for f in exos/*.c; do
        $CC -O2 -c -I. -Iexos "$f" -o "$OGG/exos_$(basename "${f%.c}").o"
    done
    $CC -O2 -c -I. static/fbrt0.c -o "$USCITA/fbrt0.o"
    rm -f "$USCITA/libfb.a"
    x86_64-exos-ar rcs "$USCITA/libfb.a" "$OGG"/*.o
    cp "$ALBERO/lib/fbextra.x" "$USCITA/fbextra.x" 2>/dev/null || true
    echo "  $n file comuni, $(ls exos/*.c | wc -l) dello strato exos"
    echo "  [OK] $USCITA/libfb.a ($(wc -c < "$USCITA/libfb.a") byte)"
}

prova() {
    local P="$USCITA/prova"
    mkdir -p "$P"
    echo "=== prova64.bas: fbc dell'host -> C -> x86_64-exos-gcc ==="
    cp "$RADICE/tools/freebasic-exos/prova64.bas" "$P/"
    (cd "$P" && "$ALBERO/bin/fbc" -i "$ALBERO/inc" -target exos -arch x86_64 -gen gcc -r -R -m prova64 prova64.bas)
    $CC -O2 -w -c "$P/prova64.c" -o "$P/prova64.o"
    $CC -static -o "$USCITA/prova64" "$P/prova64.o" "$USCITA/fbrt0.o" "$USCITA/libfb.a" -lm
    echo "  [OK] $USCITA/prova64 ($(wc -c < "$USCITA/prova64") byte)"
}

# what fbc passes to gcc for its own C (src/compiler/fbc.bas, gcc backend)
FBGCC="-O2 -w -fno-strict-aliasing -frounding-math -fno-math-errno -fwrapv -fno-exceptions"

compilatore() {
    local C="$USCITA/fbc-c" f n=0
    rm -rf "$C"; mkdir -p "$C"
    echo "=== fbc nativo: 145 sorgenti -> C -> x86_64-exos-gcc ==="
    cp "$ALBERO"/src/compiler/*.bas "$ALBERO"/src/compiler/*.bi "$C/"
    (cd "$C" && "$ALBERO/bin/fbc" -i "$ALBERO/inc" -target exos -arch x86_64 -gen gcc \
        -e -r -m fbc *.bas)
    for f in "$C"/*.c; do
        $CC $FBGCC -c "$f" -o "${f%.c}.o"
        n=$((n + 1))
    done
    $CC -static -o "$USCITA/fbc" "$C"/*.o "$USCITA/fbrt0.o" "$USCITA/libfb.a" -lm
    echo "  $n moduli"
    echo "  [OK] $USCITA/fbc ($(wc -c < "$USCITA/fbc") byte)"
}

mkdir -p "$USCITA"
case "$FASE" in
    libfb) libfb ;;
    prova) prova ;;
    fbc)   compilatore ;;
    tutto) libfb; prova; compilatore ;;
    *) echo "uso: prepara-fb64.sh [libfb|prova|fbc|tutto]" >&2; exit 2 ;;
esac
