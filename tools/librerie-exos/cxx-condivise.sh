#!/bin/bash
# =============================================================================
# tools/librerie-exos/cxx-condivise.sh - the C++ runtime as shared objects
# EX-OS - Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/librerie-exos/cxx-condivise.sh 64
#     tools/librerie-exos/cxx-condivise.sh 32
#
# Makes, for the threaded toolchain (cross_build/<machine>/exos-cross*, the
# one of tools/exilla/gcc-fili.sh):
#
#     libgcc_s.so.1     the unwinder of the C++ exceptions, ONE for everybody
#     libstdc++.so.6    the C++ standard library, on top of libgcc_s.so.1
#
# in cross_build/cxx-exos<bit>/exos/lib, and in the sysroot with the ld
# scripts (libgcc_s.so, libstdc++.so) so that -lstdc++ finds them. Then it
# writes the specs again (tools/gcc-exos/specs-condivise.sh): with
# libgcc_s.so in the sysroot a link that is not -static takes the unwinder
# from it (--as-needed: only who uses it gets a DT_NEEDED).
#
# (voce 85, 4 November 2026: Qt 5 for the interface of VLC. Qt is a set of
# C++ shared objects, and they need these two below them.)
#
# ! WHY libgcc_s AND NOT THE UNWINDER INSIDE libstdc++.so. The libgcc of
# EX-OS is static (--disable-shared) and builds its unwinder hidden
# (-fvisibility=hidden -DHIDE_EXPORTS): every shared object linked with it
# gets a PRIVATE copy, and each copy knows only the unwind tables that were
# registered with it (crtbeginS.o registers the library's own, see
# tools/gcc-exos/crtstuff-condivise.c). An exception thrown in libstdc++.so
# (std::bad_alloc from operator new) that has to cross the frames of
# libQt5Core.so would find no description of them and end in terminate().
# Here the four unwinder files are compiled again WITHOUT the hidden flags,
# with the same command libgcc used, and linked as libgcc_s.so.1: every
# crtbeginS.o then registers in the same list.
#
# ! THE libstdc++.a OF THE TOOLCHAIN IS NOT PIC (R_X86_64_32S: "recompile with
# -fPIC"). Its build directory is copied next to it as libstdc++-v3-pic,
# every object and .s file removed (cxx11-ios_failure goes through a .s that
# make would otherwise keep), and built again with -fPIC. The libstdc++.a of
# the toolchain stays as it is: -static programs keep using it.
# =============================================================================
set -e
R=$(cd "$(dirname "$0")/../.." && pwd)
TC=$R/cross_build
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
BIT=${1:?uso: $0 32|64}
case $BIT in
    64) T=x86_64-exos; CROSS=$TC/$MACCHINA/exos-cross64; GB=$TC/$MACCHINA/costruzione-gcc-fili64 ;;
    32) T=i386-exos;   CROSS=$TC/$MACCHINA/exos-cross;   GB=$TC/$MACCHINA/costruzione-gcc-fili ;;
    *) echo "BIT=$BIT: 32 oppure 64" >&2; exit 2 ;;
esac
fail() { echo "cxx-condivise: $*" >&2; exit 1; }
[ -x "$CROSS/bin/$T-g++" ] || fail "manca $CROSS/bin/$T-g++"
"$CROSS/bin/$T-gcc" -v 2>&1 | grep -q "Thread model: posix" ||
    fail "$T-gcc non ha i fili (Thread model: single): prima tools/exilla/gcc-fili.sh${BIT/64/ con EXOS_ARCH=x86_64}"
[ -d "$GB/$T/libstdc++-v3" ] && [ -d "$GB/$T/libgcc" ] ||
    fail "manca la cartella di costruzione di GCC coi fili ($GB): tools/exilla/gcc-fili.sh"
export PATH="$CROSS/bin:$PATH"
# -shared needs the specs, and rebuilding a toolchain (gcc-fili.sh) loses them
grep -q ld-exos.so "$("$CROSS/bin/$T-gcc" -print-libgcc-file-name | xargs dirname)/specs" 2>/dev/null ||
    "$R/tools/gcc-exos/specs-condivise.sh" "$CROSS/bin/$T-gcc" > /dev/null || fail "specs"
OUT=$TC/cxx-exos$BIT/exos/lib
SYS=$CROSS/$T/lib
J=$(nproc)
rm -rf "$TC/cxx-exos$BIT"; mkdir -p "$OUT"

# --- 1. libstdc++ again, with -fPIC ----------------------------------------------
P=$GB/$T/libstdc++-v3-pic
echo "=== libstdc++ -fPIC ($P) ==="
rm -rf "$P"
cp -a "$GB/$T/libstdc++-v3" "$P"
# the absolute paths of the copy must point to the copy, not the original
# (abs_top_builddir and friends); toplevel_builddir (.../$T/..) stays
grep -rlF "$GB/$T/libstdc++-v3" "$P" | grep -v '\.\(o\|lo\|a\|la\|so\|gch\)$' |
    xargs -r sed -i "s|$GB/$T/libstdc++-v3/\.\.|$GB/$T|g; s|$GB/$T/libstdc++-v3|$P|g"
find "$P" \( -name '*.o' -o -name '*.lo' -o -name '*.a' -o -name '*.la' -o -name '*.s' \) -delete
(cd "$P" && nice make -j"$J" CFLAGS="-g -O2 -fPIC" CXXFLAGS="-g -O2 -fPIC" > make.log 2>&1) ||
    fail "libstdc++ -fPIC non si costruisce: $P/make.log"
[ -f "$P/src/.libs/libstdc++.a" ] || fail "manca $P/src/.libs/libstdc++.a"

# --- 2. the unwinder, visible, as libgcc_s.so.1 -----------------------------------
L=$GB/$T/libgcc
U=$TC/cxx-exos$BIT/unwind
mkdir -p "$U"
SRC=$(sed -n 's/^srcdir = //p' "$L/Makefile")
echo "=== libgcc_s.so.1 ==="
for f in unwind-dw2 unwind-dw2-fde unwind-sjlj unwind-c; do
    # the command libgcc used for it, without -fvisibility=hidden
    # -DHIDE_EXPORTS and without its dependency file, into $U
    cmd=$(cd "$L" && make -s -n -W "$SRC/$f.c" "$f.o" | tail -1)
    [ -n "$cmd" ] || fail "make -n non da' il comando di $f.o"
    cmd=$(echo "$cmd" | sed -e 's/ -fvisibility=hidden//; s/ -DHIDE_EXPORTS//' \
                            -e 's/ -MT [^ ]*//; s/ -MD//; s/ -MP//; s/ -MF [^ ]*//' \
                            -e "s| -o $f\.o| -o $U/$f.o|")
    (cd "$L" && eval "$cmd") || fail "$f.c non compila"
done
"$CROSS/bin/$T-gcc" -shared -Wl,-soname,libgcc_s.so.1 -o "$OUT/libgcc_s.so.1" \
    "$U"/unwind-dw2.o "$U"/unwind-dw2-fde.o "$U"/unwind-sjlj.o "$U"/unwind-c.o ||
    fail "libgcc_s.so.1 non si collega"
# ! AT 32 BIT THE EXCEPTIONS ARE SJLJ (i386-exos: setjmp/longjmp, a list of
# registered contexts per thread), at 64 DWARF: other names, same rule, one
# copy for everybody
if [ "$BIT" = 32 ]; then SIMB="_Unwind_SjLj_RaiseException _Unwind_SjLj_Resume _Unwind_SjLj_Register __gcc_personality_sj0"
else SIMB="_Unwind_RaiseException _Unwind_Resume __register_frame_info __deregister_frame_info __gcc_personality_v0"; fi
for s in $SIMB; do
    "$CROSS/bin/$T-nm" -D --defined-only "$OUT/libgcc_s.so.1" | grep -q " $s\$" || fail "libgcc_s.so.1 non esporta $s"
done

# --- 3. libstdc++.so.6 on top of it ---------------------------------------------------
# the ld script of libgcc_s first: -lgcc_s must find the .so before libgcc.a
printf '/* GNU ld script: -lgcc_s is libgcc_s.so.1 (tools/librerie-exos/cxx-condivise.sh) */\nINPUT(libgcc_s.so.1)\n' > "$OUT/libgcc_s.so"
echo "=== libstdc++.so.6 ==="
"$CROSS/bin/$T-g++" -shared -nostdlib++ -Wl,-soname,libstdc++.so.6 -o "$OUT/libstdc++.so.6" \
    -Wl,--whole-archive "$P/src/.libs/libstdc++.a" -Wl,--no-whole-archive \
    -L"$OUT" -lgcc_s -L"$SYS" -lm > "$TC/cxx-exos$BIT/libstdcxx.log" 2>&1 ||
    { cat "$TC/cxx-exos$BIT/libstdcxx.log"; fail "libstdc++.so.6 non si collega"; }
"$CROSS/bin/$T-readelf" -d "$OUT/libstdc++.so.6" | grep -q TEXTREL && fail "libstdc++.so.6 ha TEXTREL"
"$CROSS/bin/$T-nm" -D --defined-only "$OUT/libstdc++.so.6" | grep -qE " _Unwind_(SjLj_)?RaiseException\$" &&
    fail "libstdc++.so.6 ha un unwinder suo: deve prenderlo da libgcc_s.so.1"
printf '/* GNU ld script: -lstdc++ is libstdc++.so.6 (tools/librerie-exos/cxx-condivise.sh) */\nINPUT(libstdc++.so.6)\n' > "$OUT/libstdc++.so"
# the symbols they ask for: the libc's table, libm.so.4, each other; weak
# ones (transactional memory, the zoneinfo override) may stay unresolved
python3 - "$R/lib/libc_esportati.h" "$CROSS/bin/$T-nm" "$OUT" "$SYS/libm.so.4" <<'EOF' || fail "simboli senza padrone"
import re, subprocess, sys
libc = set(re.findall(r'asciz \\"([^\\]+)\\"', open(sys.argv[1]).read()))
nm, out, libm = sys.argv[2], sys.argv[3], sys.argv[4]
def simboli(f, quali):
    r = subprocess.run([nm, "-D", quali, f], capture_output=True, text=True, check=True).stdout
    return [l.split() for l in r.splitlines() if l.strip()]
definiti = set(libc)
for f in (libm, out + "/libgcc_s.so.1", out + "/libstdc++.so.6"):
    definiti |= {p[-1] for p in simboli(f, "--defined-only")}
male = 0
for f in ("libgcc_s.so.1", "libstdc++.so.6"):
    for p in simboli(out + "/" + f, "--undefined-only"):
        tipo, nome = p[-2], p[-1]
        if nome not in definiti and tipo not in ("w", "v"):
            print("  %s: %s non ha padrone" % (f, nome)); male = 1
sys.exit(male)
EOF

# --- 4. into the sysroot, and the specs --------------------------------------------
cp -p --remove-destination "$OUT"/libgcc_s.so* "$OUT"/libstdc++.so* "$SYS/"
"$R/tools/gcc-exos/specs-condivise.sh" "$CROSS/bin/$T-gcc" > /dev/null || fail "specs"
grep -q -- "-lgcc_s" "$("$CROSS/bin/$T-gcc" -print-libgcc-file-name | xargs dirname)/specs" ||
    fail "le specs non usano libgcc_s"
"$CROSS/bin/$T-strip" --strip-debug "$OUT/libstdc++.so.6" "$OUT/libgcc_s.so.1"
ls -l "$OUT"
echo "[OK] libgcc_s.so.1 e libstdc++.so.6 per $T"
