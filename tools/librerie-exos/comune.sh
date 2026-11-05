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
# and in netinst. And SHARED (the user, 30 July and 3 November 2026: "tutte
# le librerie devono essere dinamiche"; static only where a program needs it
# and the shared one would cause problems): lib_condivisa makes lib<x>.so.N
# from the archive, everything is compiled -fPIC for it, and programs built
# against the sysroot link the .so (lib/libc_dl.c loads them at start). The
# .a stays next to it for -static. So each library ends the same way:
#
#     cross_build/<name>-exos32/exos/      laid out as /exos on EX-OS:
#     cross_build/<name>-exos64/exos/        lib/*.so* lib/*.a  include/  bin/
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
    # the compiler must know -shared and link programs to .so files: the
    # specs file of tools/gcc-exos/specs-condivise.sh, lost when a toolchain
    # is rebuilt (cross64.sh writes it, the 32-bit one does not)
    local sp
    for sp in "$CROSS/bin/$T-gcc" "$ALTRO/bin/$T-gcc"; do
        [ -x "$sp" ] || continue
        grep -q ld-exos.so "$("$sp" -print-libgcc-file-name | xargs dirname)/specs" 2>/dev/null ||
            "$R/tools/gcc-exos/specs-condivise.sh" "$sp" > /dev/null || exit 1
    done
    CC=$T-gcc; CXX=$T-g++; AR=$T-ar; RANLIB=$T-ranlib; STRIP=$T-strip
    CFLAGS="-O2 $MARCH"
    B=$TC/costruzione-$NOME/exos$BIT
    OUT=$TC/$NOME-exos$BIT
    DEST=$OUT/exos
    rm -rf "$B" "$OUT"
    mkdir -p "$B" "$DEST/lib" "$DEST/include" "$DEST/bin"
    # ! EVERYTHING -fPIC (3 November 2026): the libraries become shared
    # objects (lib_condivisa), and a .so is made of position-independent
    # code. Whatever build system the library has, it calls $T-gcc: a
    # wrapper of that name first in PATH adds the flag.
    mkdir -p "$B/pic"
    local c
    for c in gcc g++ cc c++; do
        [ -x "$CROSS/bin/$T-$c" ] || continue
        printf '#!/bin/sh\nexec "%s" -fPIC "$@"\n' "$CROSS/bin/$T-$c" > "$B/pic/$T-$c"
        chmod +x "$B/pic/$T-$c"
    done
    export PATH="$B/pic:$PATH"
    # ! AT 32 BIT, -fPIC with the stack protector calls __stack_chk_fail_local,
    # a hidden function each shared object must carry (glibc keeps it in
    # libc_nonshared.a): $NONSHARED calls the libc's __stack_chk_fail, and
    # goes into every 32-bit .so (lib_condivisa; FFmpeg's --extra-ldsoflags)
    NONSHARED=""
    if [ "$BIT" = 32 ]; then
        printf 'extern void __stack_chk_fail(void);\n__attribute__((visibility("hidden"))) void __stack_chk_fail_local(void) { __stack_chk_fail(); }\n' > "$B/nonshared.c"
        "$CROSS/bin/$T-gcc" -O2 -fPIC -fno-stack-protector -c "$B/nonshared.c" -o "$B/nonshared.o" || exit 1
        NONSHARED="$B/nonshared.o"
    fi
    lib_libm
    echo "=== $NOME $VER per $T ==="
}

# libm.so (openlibm, from the sysroot's libm.a, whose objects are already
# PIC): a library that needs sin or pow names it, and a program that opens
# that library with dlopen has no math of its own to lend it. Made once per
# architecture, in cross_build/libm-exos<bit>/exos/lib (the tools CD takes it
# from there as any other library) and in both sysroots.
lib_libm() {
    local L=$TC/libm-exos$BIT/exos/lib S
    if [ ! -f "$L/libm.so.4" ] || [ "$CROSS/$T/lib/libm.a" -nt "$L/libm.so.4" ]; then
        mkdir -p "$L"
        "$CROSS/bin/$T-gcc" -shared -Wl,-soname,libm.so.4 -o "$L/libm.so.4" \
            -Wl,--whole-archive "$CROSS/$T/lib/libm.a" -Wl,--no-whole-archive || { echo "libm.so.4 non si collega"; exit 1; }
    fi
    lib_per_ld "$L" m libm.so.4
    # ! --remove-destination: where a libm.so was a symbolic link, plain cp
    # would write through it, onto libm.so.4
    for S in "$CROSS/$T" "$ALTRO/$T"; do
        [ -d "$S/lib" ] && cp -p --remove-destination "$L"/libm.so* "$S/lib/"
    done
}

# lib_per_ld DIR NAME FILE: DIR/libNAME.so for the linker, as a two-line ld
# script, INPUT(FILE), the way glibc makes its libc.so. ! NOT A SYMBOLIC LINK:
# EX-OS has none, and the tools CD carries this directory for the native gcc.
lib_per_ld() {
    rm -f "$1/lib$2.so"
    printf '/* GNU ld script: -l%s is %s (tools/librerie-exos/comune.sh) */\nINPUT(%s)\n' "$2" "$3" "$3" > "$1/lib$2.so"
}

# lib_condivisa NAME MAJOR "DEPENDENCIES": lib/libNAME.so.MAJOR from the
# archive lib/libNAME.a, with that soname and DT_NEEDED for each -lDEP
# (found in the sysroot as .so: those libraries come first). MAJOR is the
# upstream one (libavcodec.so.63, libz.so.1...): a program that looks for a
# library by name - Firefox does, for FFmpeg - finds it. The libc is not
# named: a library takes it from the program (lib/libc_esportati.h).
lib_condivisa() {
    local a=$DEST/lib/lib$1.a so=lib$1.so.$2
    [ -f "$a" ] || { echo "lib_condivisa: manca $a"; exit 1; }
    "$CROSS/bin/$T-gcc" -shared -Wl,-soname,$so -o "$DEST/lib/$so" \
        -Wl,--whole-archive "$a" -Wl,--no-whole-archive $NONSHARED -L"$DEST/lib" -L"$CROSS/$T/lib" $3 \
        > "$B/condivisa-$1.log" 2>&1 || { cat "$B/condivisa-$1.log"; echo "lib_condivisa: $so non si collega"; exit 1; }
    lib_controlla "$1" "$2" "$3"
}

# lib_condivisa_sua NAME MAJOR "DEPENDENCIES": for a library whose own build
# makes the shared object (FFmpeg: its libraries share hidden tables that only
# its own shared build handles). The real file becomes lib/libNAME.so.MAJOR,
# the name its soname gives, the symbolic links go (EX-OS has none), then the
# same checks as lib_condivisa.
lib_condivisa_sua() {
    local so=lib$1.so.$2 f vero=""
    for f in "$DEST/lib/lib$1.so"*; do
        [ -L "$f" ] && { rm -f "$f"; continue; }
        [ -f "$f" ] && vero=$f
    done
    [ -n "$vero" ] || { echo "lib_condivisa_sua: nessun lib$1.so in $DEST/lib"; exit 1; }
    [ "$vero" = "$DEST/lib/$so" ] || mv "$vero" "$DEST/lib/$so"
    "$CROSS/bin/$T-readelf" -d "$DEST/lib/$so" | grep -q "Library soname: \[$so\]" \
        || { echo "lib_condivisa_sua: il soname di $so non e' $so"; exit 1; }
    lib_controlla "$1" "$2" "$3"
}

# the checks of a shared object, and its ld script
lib_controlla() {
    local so=lib$1.so.$2
    # ! A .so THAT EXPORTS NOTHING: built with -fvisibility=hidden and the
    # export marks of its static build (expat, 3 November 2026)
    if [ "$("$CROSS/bin/$T-nm" -D --defined-only "$DEST/lib/$so" | wc -l)" -lt 2 ]; then
        echo "lib_condivisa: $so non esporta niente (visibility hidden?)"; exit 1
    fi
    if "$CROSS/bin/$T-readelf" -d "$DEST/lib/$so" | grep -q TEXTREL; then
        echo "lib_condivisa: $so ha rilocazioni nel codice (oggetti senza -fPIC)"; exit 1
    fi
    # ! EVERY UNDEFINED SYMBOL MUST HAVE AN OWNER: the libc's table (what a
    # program offers, lib/libc_esportati.h), libm.so, or a DEPENDENCY named
    # here. A dependency left out would only show at run time, as "undefined
    # symbol" from whoever loads the library first. Symbol versions
    # (av_free@LIBAVUTIL_61) are left out: lib/libc_dl.c binds by name.
    local d f mancano
    mancano=$( {
        python3 - "$R/lib/libc_esportati.h" "$BIT" <<'PY'
import re, sys
t = open(sys.argv[1]).read()
inizio = "#elif defined(__x86_64__) && defined(EXOS_LIBC_PORTATI)" if sys.argv[2] == "64" else "#elif defined(EXOS_LIBC_PORTATI)"
s = t.split(inizio, 1)[1].split("#elif", 1)[0]
for n in re.findall(r'\.asciz \\"([^\\"]+)\\"', s): print(n)
PY
        "$CROSS/bin/$T-nm" -D --defined-only "$TC/libm-exos$BIT/exos/lib/libm.so.4" | awk '{print $3}'
        for d in $3; do
            case $d in -l*) ;; *) continue ;; esac
            f=$(ls "$DEST/lib/lib${d#-l}.so".[0-9]* "$CROSS/$T/lib/lib${d#-l}.so".[0-9]* 2>/dev/null | head -1)
            [ -n "$f" ] && "$CROSS/bin/$T-nm" -D --defined-only "$f" | awk '{print $3}'
        done
    } | sed 's/@.*//' | sort -u | comm -13 - <("$CROSS/bin/$T-nm" -D --undefined-only "$DEST/lib/$so" \
          | awk '$1 == "U" {print $2}' | sed 's/@.*//' | sort -u) )
    if [ -n "$mancano" ]; then
        echo "lib_condivisa: $so usa simboli che nessuno gli da':"; echo "$mancano" | head -20 | sed 's/^/        /'
        exit 1
    fi
    lib_per_ld "$DEST/lib" "$1" "$so"
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

# a Meson cross file for EX-OS in $B/exos.meson (4 November 2026, HarfBuzz).
# ! system = 'linux', for the same reason as CMake above: Meson knows nothing
# of EX-OS and picks the POSIX ways for "linux"; every function is still
# checked at configure time. The compilers are the -fPIC wrappers in PATH.
# lib_meson_cross [PCDIR]: with PCDIR, pkg-config reads only the .pc files
# there (written by the caller for libraries of the sysroot), never the host's.
lib_meson_cross() {
    local pc=${1:-}
    local cpu=${T%-exos} fam=x86_64
    [ "$cpu" = i386 ] && fam=x86
    local f=""
    for x in $MARCH -D__unix__ -DEXOS_SOLO_POSIX; do f="$f'$x', "; done
    cat > "$B/exos.meson" <<FINE
[binaries]
c = '$B/pic/$CC'
cpp = '$B/pic/$CXX'
ar = '$CROSS/bin/$AR'
strip = '$CROSS/bin/$STRIP'
pkg-config = '$( [ -n "$pc" ] && echo pkg-config || echo false )'

[properties]
pkg_config_libdir = '${pc:-/nessuno}'

[built-in options]
c_args = [${f%, }]
cpp_args = [${f%, }]

[host_machine]
system = 'linux'
cpu_family = '$fam'
cpu = '$cpu'
endian = 'little'
FINE
}

# lib_pc DIR NAME VERSION "CFLAGS" "LIBS": a .pc of a library of the sysroot,
# for a configure that only finds its dependencies through pkg-config
lib_pc() {
    mkdir -p "$1"
    printf 'Name: %s\nDescription: %s (EX-OS sysroot)\nVersion: %s\nCflags: %s\nLibs: %s\n' \
        "$2" "$2" "$3" "$4" "$5" > "$1/$2.pc"
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
        # the .so and their ld scripts: `-lname` then finds the shared one,
        # and -static still the archive
        ls "$DEST"/lib/*.so* > /dev/null 2>&1 && cp -p --remove-destination "$DEST"/lib/*.so* "$S/lib/"
        cp -rp "$DEST/include/." "$S/include/"
    done
    rmdir "$DEST/bin" 2>/dev/null || true
    echo "    $(cd "$DEST" && ls lib/*.so.* lib/*.a bin/* 2>/dev/null | tr '\n' ' ')"
}

# lib_tutte: "costruisci.sh [32|64]", both by default
lib_tutte() {
    case "${1:-tutti}" in
        32|64) costruisci "$1" ;;
        tutti) costruisci 32; costruisci 64 ;;
        *) echo "uso: $0 [32|64]" >&2; exit 2 ;;
    esac
}
