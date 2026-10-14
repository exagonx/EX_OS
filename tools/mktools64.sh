#!/bin/sh
# =============================================================================
# tools/mktools64.sh — the 64-bit tools CD: gcc, g++, binutils, make, nasm
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/mktools64.sh          -> build-64/iso/ and dist/exos64-tools.iso
#
# The tree is build-64/iso, the name of the 32-bit one (build/iso): it is
# from there that tools/mknetinst.sh takes the tools for the repository, so
# `make netinst64` publishes them with the system.
#
# (@EXOS-64.) The twin of the 32-bit tools CD (`make iso`, dist/exos-tools.iso)
# with the same layout, so that `toolinst` installs it the same way:
#
#     /exos/bin/                gcc g++ cpp as ld ar nm ranlib strip objcopy
#                               objdump make nasm ndisasm
#     /exos/libexec/gcc/x86_64-exos/17.0.0/   cc1 cc1plus collect2
#     /exos/lib/gcc/x86_64-exos/17.0.0/       libgcc.a, crt*.o, gcc's headers
#     /exos/lib/                crt0.o libc.a libm.a libstdc++.a libsupc++.a
#                               and the crt*.o again (where the 32-bit CD has them)
#     /exos/include/            the EX-OS headers, and c++/17.0.0
#     /exos/x86_64-exos/bin/    as, ld: where the gcc driver looks for them first
#     /exos/strumenti.txt       the catalogue toolinst reads
#
# WHERE THE PIECES COME FROM - everything built by tools/gcc-exos/cross64.sh:
#   - the programs that RUN on EX-OS: cross_build/exos-native64/build-*
#   - the libraries and headers that programs are LINKED with: the cross
#     toolchain's sysroot, cross_build/exos-cross64/x86_64-exos, and the
#     cross GCC's lib/gcc. They are target code either way; the native
#     compiler just needs them on the disk where it runs.
#
# ! THE CATALOGUE IS THE 32-BIT ONE (tools/iso/strumenti.txt, which `make
# iso` copies unchanged as well), copied as it is. toolinst only offers a group whose `prova`
# file is on the CD, so the groups this CD does not have yet (FreeBASIC,
# OpenSSL) are simply not offered. One catalogue, not two to keep equal.
#
# ! THE PROGRAMS ARE STRIPPED: cc1plus with its debug information is several
# hundred megabytes, and nobody debugs the compiler on EX-OS.
# =============================================================================
set -e
cd "$(dirname "$0")/.." || exit 1

TARGET=x86_64-exos
VER=17.0.0
TC64=cross_build/exos-cross64
N=cross_build/exos-native64
OUT=build-64/iso
E="$OUT/exos"
STRIP="$TC64/bin/$TARGET-strip"

fail() { echo "mktools64: $*" >&2; exit 1; }

for f in "$N/build-gcc/gcc/xgcc" "$N/build-gcc/gcc/cc1" "$N/build-gcc/gcc/cc1plus" \
         "$N/build-binutils/gas/as-new" "$N/build-binutils/ld/ld-new" \
         "$TC64/$TARGET/lib/libc.a" "$TC64/$TARGET/lib/libstdc++.a" \
         "$TC64/lib/gcc/$TARGET/$VER/libgcc.a" "$STRIP"; do
    [ -e "$f" ] || fail "missing $f: run tools/gcc-exos/cross64.sh first"
done

rm -rf "$OUT"
mkdir -p "$E/bin" "$E/libexec/gcc/$TARGET/$VER" "$E/lib/gcc/$TARGET/$VER" \
         "$E/include" "$E/$TARGET/bin"

# --- programs that run on EX-OS -------------------------------------------------
put() {     # $1 = built file, $2 = name under /exos
    "$STRIP" -o "$E/$2" "$1" || fail "cannot strip $1"
    chmod 755 "$E/$2"
}
put "$N/build-gcc/gcc/xgcc"       bin/gcc
put "$N/build-gcc/gcc/xg++"       bin/g++
put "$N/build-gcc/gcc/cpp"        bin/cpp
put "$N/build-gcc/gcc/cc1"        "libexec/gcc/$TARGET/$VER/cc1"
put "$N/build-gcc/gcc/cc1plus"    "libexec/gcc/$TARGET/$VER/cc1plus"
put "$N/build-gcc/gcc/collect2"   "libexec/gcc/$TARGET/$VER/collect2"
B="$N/build-binutils"
put "$B/gas/as-new"               bin/as
put "$B/ld/ld-new"                bin/ld
put "$B/binutils/ar"              bin/ar
put "$B/binutils/ranlib"          bin/ranlib
put "$B/binutils/nm-new"          bin/nm
put "$B/binutils/strip-new"       bin/strip
put "$B/binutils/objcopy"         bin/objcopy
put "$B/binutils/objdump"         bin/objdump
cp "$E/bin/as" "$E/$TARGET/bin/as"
cp "$E/bin/ld" "$E/$TARGET/bin/ld"
[ -x "$N/build-make/make" ] && put "$N/build-make/make" bin/make
[ -x "$N/build-nasm/nasm" ] && put "$N/build-nasm/nasm" bin/nasm
[ -x "$N/build-nasm/ndisasm" ] && put "$N/build-nasm/ndisasm" bin/ndisasm

# --- libraries and headers that programs are linked with ------------------------
G="$TC64/lib/gcc/$TARGET/$VER"
cp -rp "$G/." "$E/lib/gcc/$TARGET/$VER/"
rm -rf "$E/lib/gcc/$TARGET/$VER/plugin" "$E/lib/gcc/$TARGET/$VER/install-tools"
S="$TC64/$TARGET"
for f in crt0.o libc.a libm.a libstdc++.a libsupc++.a libpthread.a libdl.a librt.a; do
    [ -f "$S/lib/$f" ] && cp -p "$S/lib/$f" "$E/lib/"
done
for f in crtbegin.o crtend.o crti.o crtn.o libgcc.a; do
    [ -f "$G/$f" ] && cp -p "$G/$f" "$E/lib/"
done
cp -rp "$S/include/." "$E/include/"
if [ -d "$S/include/c++" ]; then :; elif [ -d "$TC64/include/c++" ]; then
    cp -rp "$TC64/include/c++" "$E/include/"
fi

# --- OpenSSL, if tools/openssl-exos/prepara-openssl.sh built it at 64 bit -------
# (EXOS_ARCH=x86_64 tools/openssl-exos/prepara-openssl.sh, then make
# build_libs in cross_build/openssl-build-exos64). The same layout as the
# 32-bit CD: the archives in /exos/lib, the headers, the CA store, and the two
# test programs in /bin.
O=cross_build/openssl-build-exos64
if [ -f "$O/libcrypto.a" ] && [ -f "$O/libssl.a" ]; then
    cp -p "$O/libcrypto.a" "$O/libssl.a" "$E/lib/"
    mkdir -p "$E/include/openssl" "$E/ssl" "$OUT/bin"
    # the source headers first, then the generated ones over them; -f and
    # u+w because the source tree's headers are read-only
    cp -r openssl/include/openssl/. "$E/include/openssl/"
    chmod -R u+w "$E/include/openssl"
    cp -rf "$O/include/openssl/." "$E/include/openssl/"
    [ -f /etc/ssl/certs/ca-certificates.crt ] && cp /etc/ssl/certs/ca-certificates.crt "$E/ssl/certi.pem"
    CC64="$TC64/bin/$TARGET-gcc"
    "$CC64" -O2 -I "$O/include" -I openssl/include -o "$OUT/bin/provassl" tools/iso/prova-ssl.c \
        "$O/libcrypto.a" && "$STRIP" "$OUT/bin/provassl"
    "$CC64" -O2 -I drivers/net -I drivers/pci -I "$O/include" -I openssl/include \
        -o "$OUT/bin/provatls" tools/iso/prova-tls.c lib/rete.c lib/dns.c \
        "$O/libssl.a" "$O/libcrypto.a" && "$STRIP" "$OUT/bin/provatls"
    echo "mktools64: OpenSSL $(sed -n 's/^MAJOR=//p' openssl/VERSION.dat).$(sed -n 's/^MINOR=//p' openssl/VERSION.dat) - libcrypto, libssl, provassl, provatls"
else
    echo "mktools64: no 64-bit OpenSSL ($O): EXOS_ARCH=x86_64 tools/openssl-exos/prepara-openssl.sh" >&2
fi

# --- the catalogue, the test sources, a note -------------------------------------
cp tools/iso/strumenti.txt "$E/strumenti.txt"     # the Makefile copies it as it is too
for f in tools/iso/prova-gcc.c tools/iso/prova-cpp.cpp tools/iso/prova-mat.c tools/iso/prova-make \
         tools/iso/prova-nasm.asm; do
    [ -f "$f" ] && cp "$f" "$OUT/"
done
printf 'EX-OS 64-bit tools: gcc and g++ %s, binutils 2.44, GNU make, NASM, OpenSSL.\r\nInstall them with  toolinst  (or install -tools).\r\n' "$VER" \
    > "$OUT/leggimi.txt"

mkdir -p dist
python3 tools/mkiso.py dist/exos64-tools.iso --da "$OUT" --etichetta "EXOS64 TOOLS" > /dev/null
echo "mktools64: dist/exos64-tools.iso ($(du -sh "$OUT" | cut -f1) of tools, $(stat -c%s dist/exos64-tools.iso) bytes)"
