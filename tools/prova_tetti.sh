#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_tetti.sh - the fixed ceilings of voce 35 are gone (kernel 0.347)
#
#     tools/prova_tetti.sh          32 and 64 bit
#     tools/prova_tetti.sh 64       only one
#
# One QEMU run per architecture, from the CD (tools/exilla/esegui-in-exos.sh),
# with a second disk made here with sfdisk: a primary and 22 logical
# partitions (hd1p5..hd1p26), the last one ext2 with a file in it.
#   - tools/prove/tettiprova.c: spawn_ex and fork+execve with 3000 arguments
#     (~70 KB) and 200 variables (up to 0.346: 512, 32 KB, and the variables
#     after the 32nd dropped without a word);
#   - 22 logical partitions: hd1p26 mounts and its file reads (up to 0.346
#     16 partitions per disk and 16 block devices in all);
#   - `disk` says it shows only part of them (DiskInfo keeps 16: ABI);
#   - no [FAULT] or PANIC on the serial line.
# The kernel.cfg part (more than 8 KB, 16 modules, 32 variables) needs a
# disk installation: tools/prova_tetti_cfg.sh.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
SBIN=/usr/sbin
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

disco_partizioni() {    # $1 = image
    local img=$1 i t p
    rm -f "$img"; qemu-img create -f raw "$img" 128M > /dev/null
    # ! the logicals by name (img5...): without, sfdisk counts them as primaries
    { printf 'label: dos\nunit: sectors\n\n%s1 : start=2048, size=4096, type=83\n%s2 : start=8192, type=5\n' "$img" "$img"
      for i in $(seq 5 26); do printf '%s%d : size=2048, type=83\n' "$img" $i; done; } | $SBIN/sfdisk "$img" > /dev/null 2>&1
    # the last logical: its start from sfdisk, an ext2 with a file
    p=$($SBIN/sfdisk -d "$img" | grep -a "${img}26 " | sed 's/.*start= *\([0-9]*\).*/\1/')
    [ -n "$p" ] || return 1
    t=$(dirname "$img")/p26.img
    dd if=/dev/zero of="$t" bs=512 count=2048 status=none
    $SBIN/mke2fs -q -t ext2 -L ventidue "$t"
    echo "la partizione 26" > "$(dirname "$img")/ventidue.txt"
    $SBIN/debugfs -w -R "write $(dirname "$img")/ventidue.txt ventidue.txt" "$t" > /dev/null 2>&1
    dd if="$t" of="$img" bs=512 seek="$p" conv=notrunc status=none
}

giro() {    # $1 = 32|64
    local bit=$1 T TCB D CF arch cd uscita ser=/tmp/exos/serialexilla.txt
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
    D="${EXOS_FUORI}/cross_build/$MACCHINA/costruzione-tetti-$T"
    mkdir -p "$D/lib"
    echo "=== $bit bit ==="
    gcc $CF -Ilib/include -c lib/libc.c -o "$D/lib/libc.o" 2> "$D/libc.log" || { no "$bit: la libc non compila"; return; }
    rm -f "$D/lib/libc.a"; ar rcs "$D/lib/libc.a" "$D/lib/libc.o"
    "$TCB/$T-gcc" -O2 -Wall -I lib/include -L "$D/lib" tools/prove/tettiprova.c \
        -o "$D/tettiprova" || { no "$bit: tettiprova non compila"; return; }
    disco_partizioni "$D/part.img" || { no "$bit: il disco con 22 logiche non si fa"; return; }
    uscita=$(EXOS_ARCH=$arch CD_FORZATO="$PWD/$cd" SECONDI=30 EXOS_RAM=512M \
             QEMU_PIU="-drive file=$D/part.img,format=raw,if=ide,index=1" \
             DOPO="mount:hd1p26:/disk/p26@4 cat:/disk/p26/ventidue.txt@2 disk@4" \
             bash tools/exilla/esegui-in-exos.sh "$D/tettiprova")
    echo "$uscita" | grep -a "\[OK\]\|\[NO\]\|figlio\|execve" | sed 's/^/    /'
    echo "$uscita" | grep -aq "tettiprova: 0 NO" && si "$bit: spawn ed exec senza tetti" || no "$bit: tettiprova"
    echo "$uscita" | grep -aqx "la partizione 26" && si "$bit: hd1p26 (ventiduesima logica) si monta e si legge" \
        || no "$bit: hd1p26 non si legge"
    echo "$uscita" | grep -aq "piu' partizioni di quante ne possa mostrare" && si "$bit: disk dice che ne mostra solo una parte" \
        || no "$bit: disk non avvisa delle partizioni in piu'"
    grep -aq "\[FAULT\]\|PANIC" "$ser" 2>/dev/null && no "$bit: [FAULT] o PANIC sulla seriale" || si "$bit: nessun FAULT"
    echo "$uscita" > "$D/uscita.txt"
}

case "${1:-}" in
    32) giro 32 ;;
    64) giro 64 ;;
    *)  giro 32; giro 64 ;;
esac
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
