#!/bin/sh
# =============================================================================
# tools/gcc-exos/cross64.sh — the x86_64-exos cross toolchain (@EXOS-64)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/gcc-exos/cross64.sh [step...]
#
# Steps, in order (no argument = all of them):
#   the CROSS toolchain, which runs on this Linux host:
#     binutils   binutils 2.44 for x86_64-exos   (cross_build/binutils-build-64)
#     sysroot    crt0.o, libc.a and the headers of EX-OS built at 64 bit
#     gcc        GCC for C: the compiler and libgcc (cross_build/gcc-build-64)
#     libm       openlibm, as libm.a in the sysroot: libstdc++ needs it
#     cxx        GCC again with C++, and libstdc++    (same build directory)
#   the NATIVE tools, which run ON EX-OS 64-bit (make tools64 collects them):
#     gcclibs    GMP, MPFR, MPC for x86_64-exos: GCC itself needs them
#     specs      the specs file that lets `gcc -shared` make a shared object
#                for dlopen (tools/gcc-exos/specs-condivise.sh); after cxx
#     nbinutils  as, ld, ar... built to run on EX-OS (exos-native64/build-binutils)
#     ngcc       gcc, cc1, cc1plus built to run on EX-OS (exos-native64/build-gcc)
#     nmake      GNU make                               (exos-native64/build-make)
#     nnasm      NASM                                   (exos-native64/build-nasm)
#
# Everything is installed in cross_build/exos-cross64 (TC64 moves it). The
# 32-bit toolchain in cross_build/exos-cross is not touched.
#
# It is the 64-bit twin of what tools/gcc-exos/leggimi.md describes for
# i386-exos, written as one script so it can run unattended:
#
#     nohup tools/gcc-exos/cross64.sh > cross_build/cross64.log 2>&1 &
#
# and the last line of the log says FINISHED or FAILED.
#
# ! THE SOURCES MUST ALREADY CARRY THE TARGET: tools/binutils-exos/applica.py
# and tools/gcc-exos/applica.py add x86_64-exos (since 2026-10-14). This
# script runs them again first; they are idempotent.
#
# ! NOT IN /tmp: on this machine /tmp is RAM, and GCC fills it at the very
# last link (see tools/gcc-exos/leggimi.md). Build directories are in
# cross_build/.
#
# ! --enable-initfini-array, IN EVERY GCC CONFIGURE (2026-10-14). For a
# target that is not Linux GCC guesses "no": global constructors then go in
# .ctors and crtbegin.o registers the unwind tables from .init - and the
# EX-OS start-up runs neither, it runs .init_array (lib/libc_avvio.c). The
# first C++ throw stopped on a ud2: the unwinder had no frame description
# even for itself.
#
# ! THE CHECK IS THE FILES, NOT make's EXIT CODE: a partial GCC can exit 0
# with xgcc built and cc1 missing. Each step checks what it was meant to
# produce.
# =============================================================================
set -e
cd "$(dirname "$0")/../.." || exit 1
ROOT=$(pwd)

TC64="${TC64:-$ROOT/cross_build/exos-cross64}"
TARGET=x86_64-exos
BINUTILS_SRC="${BINUTILS_SRC:-$ROOT/cross_build/exos-native/binutils-2.44}"
GCC_SRC="${GCC_SRC:-$ROOT/gcc}"
JOBS="${JOBS:-3}"            # 4 cores here: one is left to the machine

STEPS="${*:-binutils sysroot gcc libm cxx specs gcclibs nbinutils ngcc nmake nnasm}"
NATIVE="$ROOT/cross_build/exos-native64"
SRC="$ROOT/cross_build/exos-native"      # third-party sources, shared with 32 bit
S="$TC64/$TARGET"
PATH="$TC64/bin:$PATH"
export PATH

say() { echo; echo "=== $(date +%H:%M:%S) $* ==="; }
fail() { echo "FAILED: $*"; exit 1; }

for step in $STEPS; do
case "$step" in

binutils)
    say "binutils for $TARGET"
    [ -d "$BINUTILS_SRC" ] || fail "no binutils sources in $BINUTILS_SRC"
    python3 tools/binutils-exos/applica.py "$BINUTILS_SRC" | tail -1
    mkdir -p cross_build/binutils-build-64
    (cd cross_build/binutils-build-64 &&
     "$BINUTILS_SRC/configure" --target=$TARGET --prefix="$TC64" \
         --disable-nls --disable-werror --disable-gdb --disable-gprofng \
         --disable-sim --disable-libdecnumber --disable-readline > configure.log 2>&1 &&
     make -j"$JOBS" > make.log 2>&1 &&
     make install > install.log 2>&1) || fail "binutils (logs in cross_build/binutils-build-64)"
    for t in as ld ar ranlib nm objcopy objdump strip; do
        [ -x "$TC64/bin/$TARGET-$t" ] || fail "binutils: $TARGET-$t was not installed"
    done
    echo "[OK] $("$TC64/bin/$TARGET-ld" --version | head -1)"
    ;;

sysroot)
    # What the compiler finds when it builds a program FOR EX-OS: the
    # start-up, the libc and its headers - the same files the system uses,
    # compiled at 64 bit. EXOS_LIBC_PORTATI turns on the parts of the libc
    # that only ported software needs (see lib/libc.c).
    say "sysroot: crt0.o, libc.a, headers"
    S="$TC64/$TARGET"
    mkdir -p "$S/lib" "$S/include/exos"
    CFLAGS64="-m64 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
              -Wall -O2 -std=c11 -ffunction-sections -fdata-sections -DEXOS_LIBC_PORTATI"
    gcc -m64 -c lib/start.S -o "$S/lib/crt0.o"
    gcc $CFLAGS64 -Ilib/include -c lib/libc.c -o "$S/lib/libc.o" 2> cross_build/sysroot64.log ||
        fail "the libc does not compile at 64 bit (cross_build/sysroot64.log)"
    rm -f "$S/lib/libc.a"
    ar rcs "$S/lib/libc.a" "$S/lib/libc.o"
    rm -f "$S/lib/libc.o"
    for l in pthread dl rt; do rm -f "$S/lib/lib$l.a"; ar rcs "$S/lib/lib$l.a"; done
    cp -rp lib/include/. "$S/include/"
    ln -sfn include "$S/sys-include"
    cp -p drivers/wserver/win_proto.h drivers/kbd/kbd_proto.h "$S/include/exos/"
    echo "[OK] crt0.o, libc.a, $(ls lib/include/*.h | wc -l) headers in $S"
    ;;

gcc)
    say "GCC (C) for $TARGET"
    [ -f "$GCC_SRC/gcc/config.gcc" ] || fail "no GCC sources in $GCC_SRC"
    python3 tools/gcc-exos/applica.py "$GCC_SRC" > /dev/null
    grep -q 'x86_64-\*-exos\*' "$GCC_SRC/gcc/config.gcc" || fail "the GCC tree does not carry x86_64-exos"
    [ -x "$TC64/bin/$TARGET-as" ] || fail "binutils first (step binutils)"
    [ -f "$TC64/$TARGET/lib/libc.a" ] || fail "sysroot first (step sysroot)"
    mkdir -p cross_build/gcc-build-64
    (cd cross_build/gcc-build-64 &&
     "$GCC_SRC/configure" --target=$TARGET --prefix="$TC64" \
         --enable-languages=c --without-headers --with-newlib \
         --disable-nls --disable-shared --disable-threads --disable-multilib \
         --disable-libssp --disable-libgomp --disable-libquadmath \
         --disable-libatomic --disable-libvtv --disable-libstdcxx \
         --disable-bootstrap --disable-lto --disable-plugin --enable-initfini-array > configure.log 2>&1 &&
     make -j"$JOBS" all-gcc > make-gcc.log 2>&1 &&
     make -j"$JOBS" all-target-libgcc > make-libgcc.log 2>&1 &&
     make install-gcc install-target-libgcc > install.log 2>&1) ||
        fail "gcc (logs in cross_build/gcc-build-64)"
    [ -x cross_build/gcc-build-64/gcc/cc1 ] || fail "gcc: cc1 is missing even if make said yes"
    [ -x "$TC64/bin/$TARGET-gcc" ] || fail "gcc: $TARGET-gcc was not installed"
    ls "$TC64"/lib/gcc/$TARGET/*/libgcc.a > /dev/null 2>&1 || fail "gcc: libgcc.a was not installed"
    echo "[OK] $("$TC64/bin/$TARGET-gcc" --version | head -1)"
    ;;

libm)
    # openlibm builds in its own tree: a COPY of it, so the 32-bit build in
    # the same sources is not disturbed.
    say "libm (openlibm) for $TARGET"
    [ -x "$TC64/bin/$TARGET-gcc" ] || fail "gcc first"
    rm -rf cross_build/openlibm-build-64
    cp -a "$(ls -d "$SRC"/openlibm-*/ | head -1)" cross_build/openlibm-build-64
    (cd cross_build/openlibm-build-64 && make clean > /dev/null 2>&1;
     make ARCH=x86_64 OS=Linux USEGCC=1 CC="$TARGET-gcc" AR=$TARGET-ar RANLIB=$TARGET-ranlib \
          libopenlibm.a > build.log 2>&1) || fail "openlibm (cross_build/openlibm-build-64/build.log)"
    cp cross_build/openlibm-build-64/libopenlibm.a "$S/lib/libm.a"
    $TARGET-gcc -O2 -c tools/openlibm-exos/nearbyintl-exos.c -o cross_build/openlibm-build-64/nearbyintl-exos.o &&
        $TARGET-ar rcs "$S/lib/libm.a" cross_build/openlibm-build-64/nearbyintl-exos.o || fail "nearbyintl"
    # the six <fenv.h> functions openlibm's amd64 header keeps static inline
    # (tools/openlibm-exos/fenv-amd64-exos.c; Exilla wanted fegetround)
    $TARGET-gcc -O2 -I cross_build/openlibm-build-64/include -I cross_build/openlibm-build-64/src \
        -c tools/openlibm-exos/fenv-amd64-exos.c -o cross_build/openlibm-build-64/fenv-amd64-exos.o &&
        $TARGET-ar rcs "$S/lib/libm.a" cross_build/openlibm-build-64/fenv-amd64-exos.o || fail "fenv"
    for f in sin cos exp log pow atan2 floor fmod hypot nearbyintl fegetround fesetround; do
        $TARGET-nm --defined-only "$S/lib/libm.a" | grep -qE " [TW] $f\$" || fail "libm.a lacks $f"
    done
    echo "[OK] libm.a: $(wc -c < "$S/lib/libm.a") bytes"
    ;;

cxx)
    say "GCC (C, C++) and libstdc++ for $TARGET"
    [ -f "$S/lib/libm.a" ] || fail "libm first"
    (cd cross_build/gcc-build-64 &&
     "$GCC_SRC/configure" --target=$TARGET --prefix="$TC64" \
         --enable-languages=c,c++ --without-headers --with-newlib \
         --disable-nls --disable-shared --disable-threads --disable-multilib \
         --disable-libssp --disable-libgomp --disable-libquadmath \
         --disable-libatomic --disable-libvtv --disable-bootstrap --disable-lto \
         --disable-plugin --enable-initfini-array --enable-clocale=generic --disable-libstdcxx-pch > configure-cxx.log 2>&1 &&
     make -j"$JOBS" all-gcc > make-cxx.log 2>&1 &&
     make -j"$JOBS" all-target-libgcc all-target-libstdc++-v3 > make-libstdcxx.log 2>&1 &&
     make install-gcc install-target-libgcc install-target-libstdc++-v3 > install-cxx.log 2>&1) ||
        fail "c++ (logs in cross_build/gcc-build-64)"
    [ -x "$TC64/bin/$TARGET-g++" ] || fail "$TARGET-g++ was not installed"
    [ -f "$S/lib/libstdc++.a" ] || fail "libstdc++.a was not installed"
    echo "[OK] $("$TC64/bin/$TARGET-g++" --version | head -1), libstdc++.a"
    ;;

specs)
    tools/gcc-exos/specs-condivise.sh "$TC64/bin/$TARGET-gcc" || fail "specs"
    ;;
gcclibs)
    say "GMP, MPFR, MPC for $TARGET"
    for lib in gmp mpfr mpc; do
        tree=$(ls -d "$SRC"/$lib-*/ | head -1)
        python3 tools/gcclibs-exos/applica.py "$tree" > /dev/null
        b="$NATIVE/build-$lib"; rm -rf "$b"; mkdir -p "$b"
        extra=""
        [ $lib = mpfr ] && extra="--with-gmp=$S"
        [ $lib = mpc ]  && extra="--with-gmp=$S --with-mpfr=$S"
        (cd "$b" && "$tree/configure" --build=x86_64-pc-linux-gnu --host=$TARGET --prefix="$S" \
             --disable-shared --enable-static $extra CC="$TARGET-gcc -std=gnu17" CC_FOR_BUILD=gcc \
             > configure.log 2>&1 && make -j"$JOBS" > make.log 2>&1 && make install > install.log 2>&1) ||
            fail "$lib (logs in $b)"
        [ -f "$S/lib/lib$lib.a" ] || fail "lib$lib.a was not installed"
    done
    echo "[OK] libgmp.a libmpfr.a libmpc.a in $S/lib"
    ;;

nbinutils)
    say "native binutils (they run on EX-OS 64-bit)"
    b="$NATIVE/build-binutils"; rm -rf "$b"; mkdir -p "$b"
    (cd "$b" && "$BINUTILS_SRC/configure" --build=x86_64-pc-linux-gnu --host=$TARGET --target=$TARGET \
         --prefix=/usr --disable-nls --disable-werror --disable-shared --enable-static \
         --disable-plugins --disable-gprofng --disable-gdb --disable-sim --without-zstd \
         --without-msgpack --without-debuginfod > configure.log 2>&1 &&
     make -j"$JOBS" > make.log 2>&1) || fail "native binutils (logs in $b)"
    for t in gas/as-new ld/ld-new binutils/ar binutils/nm-new binutils/objdump binutils/strip-new; do
        [ -x "$b/$t" ] || fail "native binutils: $t is missing"
    done
    echo "[OK] native as, ld, ar, nm, objdump, strip in $b"
    ;;

ngcc)
    # ! ac_cv_c_bigendian=no: x86 is little-endian, and GCC's own probe for it
    # is a C++ test that stumbles on an EX-OS header before reaching the
    # question ("unknown endianness", 2026-10-14). The answer is not in doubt.
    say "native GCC (C, C++; it runs on EX-OS 64-bit)"
    [ -f "$S/lib/libmpc.a" ] || fail "gcclibs first"
    [ -f "$S/lib/libstdc++.a" ] || fail "cxx first: GCC is a C++ program"
    b="$NATIVE/build-gcc"; rm -rf "$b"; mkdir -p "$b"
    # exported, not only given to configure: gcc/configure runs later, from
    # make, and reads it from the environment
    (export ac_cv_c_bigendian=no; cd "$b" && "$GCC_SRC/configure" --build=x86_64-pc-linux-gnu --host=$TARGET --target=$TARGET \
         --prefix=/exos --with-sysroot= --enable-languages=c,c++ --without-headers --with-newlib \
         --disable-nls --disable-shared --disable-threads --disable-multilib --disable-libssp \
         --disable-libgomp --disable-libquadmath --disable-libatomic --disable-libvtv \
         --disable-libstdcxx --disable-bootstrap --disable-lto --disable-plugin --enable-initfini-array \
         --disable-fixincludes --enable-checking=release \
         --with-gmp="$S" --with-mpfr="$S" --with-mpc="$S" \
         ac_cv_c_bigendian=no > configure.log 2>&1 &&
     make -j"$JOBS" all-gcc > make.log 2>&1) || fail "native gcc (logs in $b)"
    for t in xgcc cc1 cc1plus collect2 xg++ cpp; do
        [ -x "$b/gcc/$t" ] || fail "native gcc: gcc/$t is missing"
    done
    echo "[OK] native gcc, cc1, cc1plus in $b/gcc"
    ;;

nmake)
    say "native GNU make"
    python3 tools/make-exos/applica.py "$ROOT/make" > /dev/null
    b="$NATIVE/build-make"; rm -rf "$b"; mkdir -p "$b"
    (cd "$b" && "$ROOT/make/configure" --host=$TARGET --build=x86_64-pc-linux-gnu --prefix=/exos \
         --disable-nls --without-guile CC=$TARGET-gcc CFLAGS="-O2 -std=gnu17" \
         CPPFLAGS=-DNO_OUTPUT_SYNC > configure.log 2>&1 && make -j"$JOBS" > make.log 2>&1) ||
        fail "native make (logs in $b)"
    [ -x "$b/make" ] || fail "native make: the binary is missing"
    echo "[OK] native make in $b"
    ;;

nnasm)
    say "native NASM"
    python3 tools/nasm-exos/applica.py "$ROOT/nasm" > /dev/null
    b="$NATIVE/build-nasm"; rm -rf "$b"; mkdir -p "$b"
    (cd "$b" && "$ROOT/nasm/configure" --host=$TARGET --build=x86_64-pc-linux-gnu --prefix=/exos \
         --disable-werror CC="$TARGET-gcc -std=gnu17" > configure.log 2>&1 &&
     make -j"$JOBS" nasm > make.log 2>&1) || fail "native nasm (logs in $b)"
    [ -x "$b/nasm" ] || fail "native nasm: the binary is missing"
    echo "[OK] native nasm in $b"
    ;;

*) fail "unknown step '$step' (binutils sysroot gcc libm cxx specs gcclibs nbinutils ngcc nmake nnasm)" ;;
esac
done

echo
echo "FINISHED: $STEPS"
