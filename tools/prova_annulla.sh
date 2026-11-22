#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_annulla.sh - pthread_cancel cuts a wait short, FD_CLOEXEC
# closes at exec (voce 105)
#
#     tools/prova_annulla.sh          32 and 64 bit
#     tools/prova_annulla.sh 64       only one
#
# Builds tools/prove/annullaprova.c against a libc.a made HERE from lib/libc.c
# (as prova_segnali.sh: the test says whether today's code works, not
# whether the toolchain's sysroot was redone), then runs it in QEMU from
# dist/exos.iso / dist/exos64.iso (kernel 0.349) through
# tools/exilla/esegui-in-exos.sh. Must print "annullaprova: 0 NO".
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
tot=0

giro() {    # $1 = 32|64
    local bit=$1 T TCB D CF arch cd uscita
    if [ "$bit" = 64 ]; then
        T=x86_64-exos; TCB="${EXOS_FUORI}/cross_build/$MACCHINA/exos-cross64/bin"; arch=x86_64
        CF="-m64 -ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
            -Wall -O2 -std=c11 -ffunction-sections -fdata-sections -DEXOS_LIBC_PORTATI"
        cd=dist/exos64.iso
    else
        T=i386-exos; TCB="${EXOS_FUORI}/cross_build/$MACCHINA/exos-cross/bin"; arch=""
        CF="-m32 -march=pentium-mmx -mtune=pentium-mmx -ffreestanding -fno-builtin \
            -fno-stack-protector -fno-pic -fno-pie -fno-asynchronous-unwind-tables \
            -Wall -O2 -std=c11 -ffunction-sections -fdata-sections -DEXOS_LIBC_PORTATI"
        cd=dist/exos.iso
    fi
    D="${EXOS_FUORI}/cross_build/$MACCHINA/costruzione-annulla-$T"
    mkdir -p "$D/lib"
    echo "=== $bit bit ==="
    gcc $CF -Ilib/include -c lib/libc.c -o "$D/lib/libc.o" || { tot=$((tot+1)); return; }
    rm -f "$D/lib/libc.a"; ar rcs "$D/lib/libc.a" "$D/lib/libc.o"
    "$TCB/$T-gcc" -O2 -Wall -I lib/include -L "$D/lib" tools/prove/annullaprova.c \
        -o "$D/annullaprova" -lpthread || { tot=$((tot+1)); return; }
    uscita=$(EXOS_ARCH=$arch CD_FORZATO="$PWD/$cd" SECONDI=${SECONDI:-40} \
             bash tools/exilla/esegui-in-exos.sh "$D/annullaprova")
    echo "$uscita" | grep -a -E "\[OK\]|\[NO\]|annullaprova|FAULT|PANIC"
    echo "$uscita" | grep -aq "annullaprova: 0 NO" || tot=$((tot+1))
}

case "${1:-}" in
    32) giro 32 ;;
    64) giro 64 ;;
    *)  giro 32; giro 64 ;;
esac
[ $tot = 0 ] && echo "=== [OK] voce 105 ===" || echo "=== [NO] $tot giri ==="
[ $tot = 0 ]
