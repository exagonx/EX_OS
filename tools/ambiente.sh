#!/bin/bash
# =============================================================================
# tools/ambiente.sh — what the build needs, what is missing, and how to get it
#
#     tools/ambiente.sh                  every part, in the order it is built
#     tools/ambiente.sh -per BERSAGLIO   only the parts BERSAGLIO uses (the
#                                        Makefile calls it at the start of
#                                        the big targets)
#
# BERSAGLIO: all iso iso-exos iso64 utente64 kernel64 tools64 netinst
#            netinst64 usb usb64 (the Makefile targets), prove (the
#            tools/prova_*.sh scripts in QEMU), toolchain (rebuilding the
#            compilers from the sources, tools/ricostruisci-bersaglio.sh)
#
# ! ONE LIST, IN ONE PLACE (25 October 2026, the user: «make sure the Makefile
# warns about every missing part, suggesting which command or script to run
# to fix it»). Every part has: what it is, how to tell it is there, which
# targets use it, whether they CANNOT do without it (obbl) or build a smaller
# result without it (fac), and the command that makes it. A missing obbl part
# stops the target before it starts (exit 1); a missing fac part is a warning
# and the target goes on, as the Makefile always did — but now it says what
# is left out and how to get it, instead of skipping in silence.
#
# ! THE COMMAND IS THE ONE TO RUN, not a hint: copy it and it works from the
# project root. Where no script exists yet (the base i386-exos compiler is
# configured by hand), the remedy names the file with the steps.
#
# ! A NEW PART GOES HERE, not in the recipe that uses it: one `voce` line.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1

# Where the toolchains live: cross_build/ if it is there, else the old place
# in the home (the same choice as `dentro` in the Makefile).
dentro() { if [ -d "cross_build/$1" ]; then echo "cross_build/$1"; else echo "$HOME/$1"; fi; }
C32=$(dentro exos-cross)
C64=cross_build/exos-cross64
N32=$(dentro exos-native)
N64=cross_build/exos-native64

PER=""
case "${1:-}" in
    "") ;;
    -per) PER="${2:-}"; [ -n "$PER" ] || { echo "uso: $0 [-per BERSAGLIO]" >&2; exit 2; } ;;
    -h|--help|-aiuto) sed -n '3,13p' "$0" | sed 's/^# \?//'; exit 0 ;;
    *) echo "uso: $0 [-per BERSAGLIO]" >&2; exit 2 ;;
esac

# --- the tests -----------------------------------------------------------------
c()      { command -v "$1" > /dev/null 2>&1; }
gcc_m()  { echo 'int x;' | gcc "$1" -x c -c -o /dev/null - > /dev/null 2>&1; }
libgcc() { ls "$1"/lib/gcc/"$2"/*/libgcc.a > /dev/null 2>&1; }

MANCA_OBBL=0; MANCA_FAC=0; GRUPPO=""

# voce GRUPPO TIPO "BERSAGLI" "WHAT" "TEST" "REMEDY"
voce() {
    local gruppo="$1" tipo="$2" bers="$3" cosa="$4" prova="$5" rimedio="$6"
    if [ -n "$PER" ]; then
        case " $bers " in *" $PER "*) ;; *) return ;; esac
        eval "$prova" && return
        if [ "$tipo" = obbl ]; then
            MANCA_OBBL=$((MANCA_OBBL + 1))
            echo "! MANCA (serve a 'make $PER'): $cosa"
        else
            MANCA_FAC=$((MANCA_FAC + 1))
            echo "! manca (senza, 'make $PER' fa di meno): $cosa"
        fi
        echo "    rimedio: $rimedio"
        return
    fi
    [ "$gruppo" != "$GRUPPO" ] && { GRUPPO="$gruppo"; echo; echo "== $gruppo"; }
    if eval "$prova"; then
        printf '  ok     %s\n' "$cosa"
    else
        [ "$tipo" = obbl ] && MANCA_OBBL=$((MANCA_OBBL + 1)) || MANCA_FAC=$((MANCA_FAC + 1))
        printf '  MANCA  %s   [%s: %s]\n' "$cosa" "$tipo" "$bers"
        printf '         rimedio: %s\n' "$rimedio"
    fi
}

# --- 1. the host -----------------------------------------------------------------
H="strumenti della macchina (Linux)"
voce "$H" obbl "all iso iso-exos usb netinst prove" "gcc che compila a 32 bit (-m32)" \
    "gcc_m -m32" "sudo apt install gcc gcc-multilib"
voce "$H" obbl "utente64 kernel64 iso64 usb64 netinst64 tools64" "gcc che compila a 64 bit (-m64)" \
    "gcc_m -m64" "sudo apt install gcc"
voce "$H" obbl "all iso iso-exos iso64 kernel64 usb usb64 netinst netinst64" "nasm" \
    "c nasm" "sudo apt install nasm"
voce "$H" obbl "all iso iso-exos iso64 utente64 kernel64 tools64 usb usb64 netinst netinst64 prove toolchain" "python3" \
    "c python3" "sudo apt install python3"
voce "$H" obbl "all iso iso-exos iso64 utente64 kernel64 usb usb64 netinst netinst64" "binutils (ld, objcopy)" \
    "c ld && c objcopy" "sudo apt install binutils"
voce "$H" obbl "all iso iso-exos iso64 usb usb64 netinst netinst64" "mtools (mcopy, mformat: dischetti e chiavette)" \
    "c mcopy && c mformat" "sudo apt install mtools"
voce "$H" fac "prove" "qemu a 32 bit (le prove)" \
    "c qemu-system-i386" "sudo apt install qemu-system-x86"
voce "$H" fac "prove" "qemu a 64 bit (le prove)" \
    "c qemu-system-x86_64" "sudo apt install qemu-system-x86"
voce "$H" obbl "toolchain" "flex, bison, gperf, makeinfo, m4 (per costruire GCC e binutils)" \
    "c flex && c bison && c gperf && c makeinfo && c m4" \
    "./shell.sh   (o a mano: sudo apt install flex bison gperf texinfo m4)"
voce "$H" obbl "toolchain" "GMP, MPFR e MPC della macchina (per costruire GCC)" \
    "[ -f /usr/include/gmp.h ] || ls /usr/include/*/gmp.h > /dev/null 2>&1" \
    "./shell.sh   (o a mano: sudo apt install libgmp-dev libmpfr-dev libmpc-dev)"
voce "$H" fac "toolchain" "cmake (libgit2, MariaDB)" \
    "c cmake" "sudo apt install cmake"

# --- 2. third-party sources (in the project and in cross_build) -------------------
S="sorgenti di terzi (stanno nell'archivio di backup.sh)"
voce "$S" obbl "toolchain" "albero di GCC (gcc/)" \
    "[ -f gcc/gcc/config.gcc ]" \
    "ripristinare gcc/ dall'archivio di backup.sh (tar -xzpf .../ex-os.tar.gz)"
voce "$S" obbl "toolchain" "bersaglio exos dentro l'albero di GCC" \
    "grep -q 'i\[34567\]86-\*-exos\*' gcc/gcc/config.gcc 2>/dev/null" \
    "python3 tools/gcc-exos/applica.py gcc"
voce "$S" obbl "toolchain" "sorgenti di binutils 2.44 ($N32/binutils-2.44)" \
    "[ -f $N32/binutils-2.44/configure ]" \
    "tar -xf $N32/binutils_2.44.orig.tar.xz -C $N32   (l'archivio e' nel backup)"
voce "$S" fac "toolchain" "sorgenti di GMP, MPFR, MPC ($N32)" \
    "ls -d $N32/gmp-* $N32/mpfr-* $N32/mpc-* > /dev/null 2>&1" \
    "cd $N32 && for a in gmp-*.tar.* mpfr-*.tar.* mpc-*.tar.*; do tar -xf \$a; done"
voce "$S" fac "toolchain" "sorgenti di openlibm ($N32/openlibm-*)" \
    "ls -d $N32/openlibm-*/ > /dev/null 2>&1" \
    "cd $N32 && tar -xf openlibm-*.tar.gz"
voce "$S" obbl "all iso iso-exos iso64 utente64 netinst netinst64" "albero di QuickJS (quickjs/)" \
    "[ -f quickjs/quickjs.c ]" \
    "ripristinare quickjs/ dall'archivio di backup.sh; vedi tools/quickjs-exos/leggimi.md"
voce "$S" obbl "all iso iso-exos iso64 utente64 netinst netinst64" "QuickJS adattato a EX-OS" \
    "grep -q __EXOS__ quickjs/cutils.h 2>/dev/null" \
    "python3 tools/quickjs-exos/applica.py quickjs"
voce "$S" fac "iso toolchain" "albero di OpenSSL (openssl/)" \
    "[ -f openssl/Configure ]" "ripristinare openssl/ dall'archivio di backup.sh"
voce "$S" fac "iso toolchain" "albero di FreeBASIC 1.10.1" \
    "ls -d freebasic/FreeBASIC-1.10.1-source-bootstrap FreeBASIC-1.10.1-source-bootstrap > /dev/null 2>&1" \
    "ripristinare FreeBASIC-1.10.1-source-bootstrap/ dall'archivio di backup.sh"

# --- 3. the 32-bit toolchain and what is built with it ----------------------------
T="toolchain i386-exos (32 bit)"
voce "$T" obbl "all iso iso-exos netinst" "compilatore incrociato i386-exos ($C32)" \
    "[ -x $C32/bin/i386-exos-gcc ]" \
    "tools/gcc-exos/prepara-cross.sh, poi il configure di GCC: tools/gcc-exos/leggimi.md, passi 1-4"
voce "$T" obbl "all iso iso-exos netinst" "libgcc di i386-exos" \
    "libgcc $C32 i386-exos" "tools/ricostruisci-bersaglio.sh libgcc"
voce "$T" obbl "all iso iso-exos netinst" "openlibm per i386-exos (libm.a)" \
    "[ -f $C32/i386-exos/lib/libm.a ]" "tools/ricostruisci-bersaglio.sh libm"
voce "$T" fac "iso" "libstdc++ per i386-exos" \
    "[ -f $C32/i386-exos/lib/libstdc++.a ]" "tools/ricostruisci-bersaglio.sh libgcc"
voce "$T" fac "iso" "GMP, MPFR, MPC per i386-exos (nel sysroot)" \
    "[ -f $C32/i386-exos/lib/libmpc.a ]" "tools/ricostruisci-bersaglio.sh gcclibs"
voce "$T" fac "iso" "binutils nativi per EX-OS ($N32/build-nativi)" \
    "[ -x $N32/build-nativi/gas/as-new ]" "tools/ricostruisci-bersaglio.sh binutils"
voce "$T" fac "iso" "GCC nativo per EX-OS (cc1, canadian cross)" \
    "[ -x $(dentro gcc-build-cpp)/gcc/cc1 ] || [ -x $(dentro gcc-build-rel)/gcc/cc1 ]" \
    "tools/ricostruisci-bersaglio.sh cc1"
voce "$T" fac "iso" "OpenSSL per i386-exos" \
    "[ -f $(dentro openssl-build-exos)/libcrypto.a ]" "tools/openssl-exos/costruisci.sh 32"
voce "$T" fac "iso" "GNU make per EX-OS" \
    "[ -x $N32/build-make/make ]" "tools/make-exos/prepara-make.sh"
voce "$T" fac "iso" "NASM per EX-OS" \
    "[ -x $N32/build-nasm/nasm ]" "tools/nasm-exos/prepara-nasm.sh"
voce "$T" fac "iso" "FreeBASIC per EX-OS (fbc)" \
    "[ -x $(dentro fb-build-110)/fbc ]" "tools/freebasic-exos/prepara-fb.sh"
voce "$T" fac "iso" "ncurses per i386-exos (libncurses.a, terminfo, tic)" \
    "[ -f cross_build/ncurses-exos32/exos/lib/libncurses.a ]" "tools/ncurses-exos/costruisci.sh 32"
for l in zlib:libz bzip2:libbz2 xz:liblzma lz4:liblz4 zstd:libzstd pcre2:libpcre2-8 fmt:libfmt; do
    voce "$T" fac "iso" "${l%%:*} per i386-exos" \
        "[ -f cross_build/${l%%:*}-exos32/exos/lib/${l#*:}.a ]" "tools/${l%%:*}-exos/costruisci.sh 32"
done

# --- 4. the 64-bit toolchain ------------------------------------------------------
Q="toolchain x86_64-exos (64 bit)"
voce "$Q" obbl "tools64" "compilatore incrociato x86_64-exos ($C64)" \
    "[ -x $C64/bin/x86_64-exos-gcc ]" "tools/gcc-exos/cross64.sh   (piu' di un'ora)"
voce "$Q" fac "utente64 iso64 netinst64" "libgcc e libm di x86_64-exos (per quickjs.so)" \
    "libgcc $C64 x86_64-exos && [ -f $C64/x86_64-exos/lib/libm.a ]" "tools/gcc-exos/cross64.sh"
voce "$Q" fac "tools64" "GCC, binutils, make e nasm nativi a 64 bit ($N64)" \
    "[ -d $N64/build-gcc ] && [ -x $N64/build-make/make ]" "tools/gcc-exos/cross64.sh"
voce "$Q" fac "tools64" "OpenSSL per x86_64-exos" \
    "[ -f cross_build/openssl-build-exos64/libssl.a ]" "tools/openssl-exos/costruisci.sh 64"
voce "$Q" fac "tools64" "libgit2 per x86_64-exos" \
    "[ -f cross_build/libgit2-exos64/lib/libgit2.a ]" "tools/libgit2-exos/costruisci.sh"
voce "$Q" fac "tools64" "ncurses per x86_64-exos (libncurses.a, terminfo, tic)" \
    "[ -f cross_build/ncurses-exos64/exos/lib/libncurses.a ]" "tools/ncurses-exos/costruisci.sh 64"
for l in zlib:libz bzip2:libbz2 xz:liblzma lz4:liblz4 zstd:libzstd pcre2:libpcre2-8 fmt:libfmt; do
    voce "$Q" fac "tools64" "${l%%:*} per x86_64-exos" \
        "[ -f cross_build/${l%%:*}-exos64/exos/lib/${l#*:}.a ]" "tools/${l%%:*}-exos/costruisci.sh 64"
done
voce "$Q" fac "tools64" "FreeBASIC per EX-OS a 64 bit" \
    "[ -x cross_build/fb64/fb-build-exos64/fbc ]" "tools/freebasic-exos/prepara-fb64.sh"

# --- 5. what one target needs from another -----------------------------------------
B="pezzi fatti da altri bersagli"
voce "$B" obbl "iso64 kernel64" "settori d'avvio fatti dal 32 bit (build/boot/mbr_bin.c, boothd_bin.c)" \
    "[ -f build/boot/mbr_bin.c ] && [ -f build/boot/boothd_bin.c ]" "make"
voce "$B" fac "iso64" "albero del CD a 32 bit (build/iso-exos: kernel.cfg, manuali, caratteri)" \
    "[ -d build/iso-exos ]" "make iso-exos"

# --- the verdict -------------------------------------------------------------------
if [ -n "$PER" ]; then
    [ $MANCA_OBBL -gt 0 ] && { echo "! 'make $PER' non puo' partire: prima i rimedi qui sopra."; exit 1; }
    exit 0
fi
echo
if [ $MANCA_OBBL -eq 0 ] && [ $MANCA_FAC -eq 0 ]; then
    echo "Tutto presente."
else
    echo "Mancano $MANCA_OBBL parti indispensabili e $MANCA_FAC facoltative."
    echo "(obbl: il bersaglio non parte senza; fac: il bersaglio si fa, piu' piccolo)"
fi
exit 0
