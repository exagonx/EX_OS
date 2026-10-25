# =============================================================================
# tools/librerie-exos/comune.sh — what every third-party library port shares
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Sourced (with ".") by tools/<library>-exos/costruisci.sh: zlib, bzip2, xz,
# lz4, zstd, pcre2, fmt. The rule (the user, 25 October 2026): before a
# program is ported, every library it needs becomes a real EX-OS library, 32
# and 64 bit, the latest public release, with its sources on the tools CD
# and in netinst. So each library ends the same way:
#
#     cross_build/<name>-exos32/exos/      laid out as /exos on EX-OS:
#     cross_build/<name>-exos64/exos/        lib/*.a  include/  bin/
#                                            src/<name>/ (archive, signature,
#                                            the EX-OS build script)
#
# and its libraries and headers are copied into the sysroots of both cross
# toolchains of that architecture (exos-cross* and nasi5deb/exos-cross*), so
# `-l<name>` works in a cross build. tools/librerie-exos/su-cd.sh puts every
# cross_build/*-exos<bit>/exos on the tools CDs.
#
# ! NOTHING IS DOWNLOADED during a build: the archives are in
# cross_build/terze-parti/<name>/, checked (GPG or sha256) when fetched.
#
# The script that sources this sets NOME, VER, ARCHIVIO, then calls
#     lib_per BIT          T, CROSS, CC, AR..., MARCH, OUT, DEST, B (build dir)
#     lib_sorgente         a fresh copy of the sources in $B/src -> SRC
#     lib_fine FILE...     sources into $DEST/src/$NOME, sysroots, summary
# =============================================================================
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
TC=$R/cross_build
TP=$TC/terze-parti

manca() { echo "! manca $1"; echo "    rimedio: $2"; exit 1; }

lib_per() {
    BIT=$1
    case $BIT in
        64) T=x86_64-exos; CROSS=$TC/nasi5deb/exos-cross64; ALTRO=$TC/exos-cross64
            MARCH="" ;;
        32) T=i386-exos;   CROSS=$TC/nasi5deb/exos-cross;   ALTRO=$TC/exos-cross
            # the base CPU, as the Makefile's CPU_BASE: no cmov
            MARCH="-march=pentium-mmx -mtune=pentium-mmx" ;;
        *) echo "BIT=$BIT: 32 oppure 64" >&2; exit 2 ;;
    esac
    [ -x "$CROSS/bin/$T-gcc" ] || manca "$T-gcc ($CROSS)" "tools/exilla/leggimi.md (la toolchain con i fili)"
    [ -f "$ARCHIVIO" ] || manca "$ARCHIVIO" "lo scarica chi aggiorna la libreria: vedi il commento in testa a $0"
    export PATH="$CROSS/bin:$PATH"
    CC=$T-gcc; CXX=$T-g++; AR=$T-ar; RANLIB=$T-ranlib; STRIP=$T-strip
    CFLAGS="-O2 $MARCH"
    B=$TC/costruzione-$NOME/exos$BIT
    OUT=$TC/$NOME-exos$BIT
    DEST=$OUT/exos
    rm -rf "$B" "$OUT"
    mkdir -p "$B" "$DEST/lib" "$DEST/include" "$DEST/bin"
    echo "=== $NOME $VER per $T ==="
}

# a fresh copy for each architecture: zlib, bzip2, lz4 and zstd build in
# their own tree
lib_sorgente() {
    mkdir -p "$B/src"
    case "$ARCHIVIO" in
        *.zip) unzip -q "$ARCHIVIO" -d "$B/src" ;;
        *)     tar -xf "$ARCHIVIO" -C "$B/src" ;;
    esac
    SRC=$(ls -d "$B"/src/*/ | head -1)
    SRC=${SRC%/}
}

# autoconf's config.sub learns "exos" (one line, as for binutils, make,
# nasm, ncurses): every config.sub of the tree, the copy is ours
lib_config_sub() {
    local f
    find "$SRC" -name config.sub | while read -r f; do
        # two layouts: one system per line (config.sub of 2023 and later),
        # or several on one line ending with "| mint* \" (older ones)
        grep -q '| exos\* ' "$f" || sed -i -e 's/^\t| mint\* \\$/\t| mint* \\\n\t| exos* \\/' \
            -e 's/^\(\t *| .*\)| mint\* \\$/\1| mint* | exos* \\/' "$f"
        [ "$("$f" $T)" = "${T%-exos}-pc-exos" ] || { echo "$f non riconosce exos"; exit 1; }
    done
}

# a CMake toolchain file for EX-OS in $B/exos.cmake. ! CMake is told the
# system is "Linux", as for libgit2 and LLVM: it has no idea of EX-OS, and
# "Linux" gets the POSIX paths; every function is still checked at configure
# time. __unix__ and EXOS_SOLO_POSIX (libc.h's own names out of the way).
lib_cmake_toolchain() {
    cat > "$B/exos.cmake" <<FINE
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ${T%-exos})
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_AR $CROSS/bin/$AR)
set(CMAKE_RANLIB $CROSS/bin/$RANLIB)
set(CMAKE_FIND_ROOT_PATH $CROSS/$T)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_C_FLAGS_INIT "$MARCH -D__unix__ -DEXOS_SOLO_POSIX")
set(CMAKE_CXX_FLAGS_INIT "$MARCH -D__unix__ -DEXOS_SOLO_POSIX")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
FINE
}

# lib_fine [extra file for src/]: the sources next to the binaries, the
# sysroots, a line of summary
lib_fine() {
    local S f
    mkdir -p "$DEST/src/$NOME"
    for f in "$ARCHIVIO" "$ARCHIVIO".sig "$ARCHIVIO".asc "$ARCHIVIO".sha256 "$0" "$@"; do
        [ -f "$f" ] && cp -p "$f" "$DEST/src/$NOME/"
    done
    cp -p "$R/tools/librerie-exos/comune.sh" "$DEST/src/$NOME/"
    for S in "$CROSS/$T" "$ALTRO/$T"; do
        [ -d "$S/lib" ] || continue
        ls "$DEST"/lib/*.a > /dev/null 2>&1 && cp -p "$DEST"/lib/*.a "$S/lib/"
        cp -rp "$DEST/include/." "$S/include/"
    done
    rmdir "$DEST/bin" 2>/dev/null || true
    echo "    $(cd "$DEST" && ls lib/*.a bin/* 2>/dev/null | tr '\n' ' ')"
}

# lib_tutte: "costruisci.sh [32|64]", both by default
lib_tutte() {
    case "${1:-tutti}" in
        32|64) costruisci "$1" ;;
        tutti) costruisci 32; costruisci 64 ;;
        *) echo "uso: $0 [32|64]" >&2; exit 2 ;;
    esac
}
