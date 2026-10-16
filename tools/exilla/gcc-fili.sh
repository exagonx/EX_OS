#!/bin/sh
# =============================================================================
# tools/exilla/gcc-fili.sh — GCC, libgcc e libstdc++ di EX-OS CON I FILI
# EX-OS — Extensible Operating System
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#     tools/exilla/gcc-fili.sh
#     EXOS_ARCH=x86_64 tools/exilla/gcc-fili.sh      (Exilla at 64 bit)
#
# ! AT 64 BIT (16 October 2026) the shared cross_build/exos-cross64 is copied
# once to cross_build/<machine>/exos-cross64 and the threaded GCC goes into
# the copy: the shared one, which builds the 64-bit system programs, is left
# as it is - the same split as at 32 bit. 64-bit GCC also wants
# --enable-initfini-array (see tools/gcc-exos/cross64.sh: without it the
# unwind tables are registered from .init, which EX-OS does not run).
#
# ! PERCHE' ESISTE (30 settembre 2026, tappa 6 di Exilla). La toolchain di
# EX-OS e' costruita con --disable-threads: «Thread model: single». La
# libstdc++ non ha _GLIBCXX_HAS_GTHREADS, cioe' niente std::mutex,
# std::thread, std::condition_variable — e, meno visibile e peggio, le
# statiche locali di funzione si inizializzano senza lucchetto: due fili che
# ci arrivano insieme le costruiscono due volte. Gecko e i suoi crate di terzi
# usano std::mutex dappertutto, e sono pieni di fili.
#
# Qui si rifanno compilatore, libgcc e libstdc++ con --enable-threads=posix,
# sopra i pthread della libc (tappa 1), e si installano nella toolchain DI
# QUESTA MACCHINA (cross_build/<macchina>/exos-cross). Quella condivisa non si
# tocca: i programmi di EX-OS continuano come prima finche' non si decide di
# passare anche lei ai fili.
#
# ! LA libgcc USA I pthread CON RIFERIMENTI FORTI: vedi libgcc/gthr.h
# (tools/gcc-exos/applica.py). Con i deboli, collegando staticamente, si
# sarebbe creduta a filo unico.
#
# ! --enable-checking=release: l'albero di GCC e' un'istantanea di sviluppo, e
# senza questa opzione il compilatore tiene accesi i verificatori interni. Uno
# di loro fermava harfbuzz con «internal compiler error: verify_cgraph_node
# failed», e tutti insieme rendevano ogni compilazione di Gecko lenta il doppio.
# La cartella di compilazione e' costruzione-gcc-fili (fuori da MEGA).
# Le opzioni sono quelle di toolchain-questa-macchina.sh, tranne i fili.
# =============================================================================

set -e

RADICE=$(cd "$(dirname "$0")/../.." && pwd)
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
DEST="$RADICE/cross_build/$MACCHINA"
GCC_SRC="${GCC_SRC:-$RADICE/gcc}"
if [ "$EXOS_ARCH" = x86_64 ]; then
    T=x86_64-exos
    PREFISSO="$DEST/exos-cross64"
    B="$DEST/costruzione-gcc-fili64"
    PIU="--enable-initfini-array"
    ORIGINE="${ORIGINE:-$RADICE/cross_build/exos-cross64}"
    if [ ! -x "$PREFISSO/bin/$T-as" ]; then
        [ -x "$ORIGINE/bin/$T-as" ] || { echo "manca $ORIGINE: tools/gcc-exos/cross64.sh" >&2; exit 1; }
        echo "copio $ORIGINE in $PREFISSO (una volta sola)"
        cp -a "$ORIGINE" "$PREFISSO"
    fi
else
    T=i386-exos
    PREFISSO="$DEST/exos-cross"
    B="$DEST/costruzione-gcc-fili"
    PIU=""
    ORIGINE="${ORIGINE:-$RADICE/cross_build/exos-cross}"
fi

[ -x "$PREFISSO/bin/$T-as" ] || { echo "manca la toolchain di questa macchina: tools/exilla/toolchain-questa-macchina.sh" >&2; exit 1; }
python3 "$RADICE/tools/gcc-exos/applica.py" "$GCC_SRC" > /dev/null

mkdir -p "$B"
cd "$B"
export PATH="$PREFISSO/bin:$PATH"
if [ ! -f config.status ]; then
    "$GCC_SRC/configure" \
        --target=$T --prefix="$PREFISSO" $PIU \
        --without-headers --with-newlib --disable-nls --disable-shared \
        --enable-threads=posix --enable-checking=release \
        --disable-libssp --disable-libgomp \
        --disable-libquadmath --disable-libatomic --disable-libvtv \
        --disable-bootstrap --enable-clocale=generic \
        --disable-libstdcxx-pch --enable-languages=c,c++,lto \
        > configure.log 2>&1 || { echo "configure fallito: $B/configure.log" >&2; exit 1; }
fi
make -j"$(nproc)" all-gcc > make-gcc.log 2>&1 || { echo "all-gcc fallito: $B/make-gcc.log" >&2; exit 1; }
make -j"$(nproc)" all-target-libgcc > make-libgcc.log 2>&1 || { echo "libgcc fallita: $B/make-libgcc.log" >&2; exit 1; }
make -j"$(nproc)" all-target-libstdc++-v3 > make-libstdcxx.log 2>&1 || { echo "libstdc++ fallita: $B/make-libstdcxx.log" >&2; exit 1; }
make install-gcc install-target-libgcc install-target-libstdc++-v3 > install.log 2>&1

# ! IL limits.h DI GCC VA RIPRESO DALL'ORIGINE, come in
# toolchain-questa-macchina.sh: costruito --without-headers, GCC installa la
# versione «senza libc sotto» e PATH_MAX non arriva piu' a nessuno.
VERSIONE=$(cat "$GCC_SRC/gcc/BASE-VER")
cp "$ORIGINE/lib/gcc/$T/$VERSIONE/include/limits.h" \
   "$PREFISSO/lib/gcc/$T/$VERSIONE/include/limits.h"

# La verifica: il modello dei fili e la libstdc++ che li ha.
"$PREFISSO/bin/$T-gcc" -v 2>&1 | grep "Thread model"
grep -q "^#define _GLIBCXX_HAS_GTHREADS 1" \
    "$PREFISSO"/$T/include/c++/*/$T/bits/c++config.h \
    && echo "[OK] libstdc++ con _GLIBCXX_HAS_GTHREADS" \
    || { echo "[NO] la libstdc++ installata non ha i fili" >&2; exit 1; }
