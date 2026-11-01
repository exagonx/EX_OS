#!/bin/bash
# =============================================================================
# tools/prova_smonta.sh - poweroff, reboot and halt unmount the volumes
#
#     tools/prova_smonta.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_smonta.sh [dir]    64 bit
#
# Two disks with a journal (ext4): hd0p1 is the system disk, hd1 is a data
# disk mounted on /dati, with a file kept open by a program in the background
# (for reboot, one that ignores SIGTERM: only SIGKILL frees the volume). After each of
# poweroff, reboot and halt the images are looked at FROM OUTSIDE, with
# dumpe2fs and e2fsck: state clean, no needs_recovery, nothing to fix, and the
# file written just before stopping is there. QEMU runs with -no-reboot, so
# that after `reboot` nothing else touches the disk.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/smonta-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"; DATI="$D/dati.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
P1_SETT=262144                                 # 128 MB

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    KERNEL=build-64/kernel-intero.bin; STAGE2=build-64/avvio/stage2-cd.bin
    RD=build-64/rdboot.img; RDZ=build-64/rdboot.lz4
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    KERNEL=build/kernel.bin; STAGE2=build/stage2.bin; RD=build/rdboot.img; RDZ=build/rdboot.lz4
    export EXOS_RAM=128M EXTRA=""
fi
for f in "$ISO" "$KERNEL" "$STAGE2" "$RD" "$RDZ"; do [ -f "$f" ] || { echo "manca $f" >&2; exit 1; }; done

T="$D/albero"
mkdir -p "$T/boot" "$T/da"
$GCC -O2 tools/prove/avvioprova.c -o "$T/avvioprova" || exit 1
$GCC -O2 tools/prove/ignoraterm.c -o "$T/ignoraterm" || exit 1
cp "$STAGE2" "$T/da/stage2.bin"
cp "$KERNEL" "$T/da/kernel.bin"
cp "$RDZ"    "$T/da/rdboot.lz4"
grep -v '^login *=' boot/kernel.cfg > "$T/boot/kernel.cfg"
for d in bin lib dev; do $SBIN/debugfs -R "rdump /rdboot/$d $T" "$RD" > /dev/null 2>&1; done

P="$D/p1.img"
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/bin/* '$T'/avvioprova '$T'/ignoraterm;
    $SBIN/mke2fs -q -F -t ext4 -b 1024 -m 0 -L sistema -d '$T' '$P' $((P1_SETT / 2))" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 130M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83, bootable\n' $P1_SETT \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$P" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
$SBIN/mke2fs -q -F -t ext4 -b 1024 -m 0 -L dati "$DATI" 32768 > /dev/null 2>&1

guarda() {   # $1 = name, $2 = image, $3 = sectors to skip, $4 = file expected
    local f="$D/g.img" s feat
    dd if="$2" of="$f" bs=512 skip="$3" status=none
    s=$($SBIN/dumpe2fs -h "$f" 2>/dev/null | sed -n 's/^Filesystem state: *//p')
    feat=$($SBIN/dumpe2fs -h "$f" 2>/dev/null | grep -c needs_recovery)
    if [ "$s" = clean ] && [ "$feat" = 0 ]; then
        echo "  [OK] $1: pulito, giornale chiuso"
    else
        echo "  [NO] $1: stato '$s', needs_recovery $feat"; esito=1
    fi
    if $SBIN/e2fsck -fn "$f" > "$D/e2fsck.log" 2>&1; then
        echo "  [OK] $1: e2fsck niente da riparare"
    else
        grep -v "^Pass\|^e2fsck " "$D/e2fsck.log" | head -4 | sed 's/^/  /'
        echo "  [NO] $1: e2fsck trova errori"; esito=1
    fi
    if [ -n "$4" ]; then
        if $SBIN/debugfs -R "cat /$4" "$f" 2>/dev/null | grep -q "^$4$"; then
            echo "  [OK] $1: $4 c'e'"
        else
            echo "  [NO] $1: $4 perso"; esito=1
        fi
    fi
}

esito=0
echo "=== $ARCH ==="

EXOS_ISTANZA=sm0$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
    timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "cp /a/da/stage2.bin /a/boot/stage2.bin@3" "cp /a/da/kernel.bin /a/boot/kernel.bin@4" \
    "cp /a/da/rdboot.lz4 /a/boot/rdboot.lz4@4" "/a/avvioprova /a@10" \
    "umount /a@3" > "$D/qemu0.log" 2>&1
grep -aq "^AVVIO installa 0" "$D/qemu0.log" || { echo "  [NO] l'installazione dell'avvio"; exit 1; }

for cmd in poweroff reboot halt; do
    echo "--- $cmd ---"
    # reboot: a program that ignores SIGTERM, 30 s and then SIGKILL
    prog=/bin/sleep\ 3000\ \<; att=25
    [ $cmd = reboot ] && { prog=/ignoraterm; att=50; }
    EXOS_ISTANZA=sm$cmd$ARCH EXOS_NO_FLOPPY=1 \
        EXOS_QEMU_EXTRA="$EXTRA -no-reboot -drive file=$IMG,format=raw,if=ide -drive file=$DATI,format=raw,if=ide" \
        timeout ${EXOS_TEMPO:-120} python3 tools/qemu_drive.py \
        "mount hd1 /dati@3" \
        "echo su$cmd > /su$cmd@2" "echo da$cmd > /dati/da$cmd@2" \
        "$prog /dati/da$cmd &@2" "sync@3" "echo tardi$cmd > /tardi$cmd@1" "$cmd@$att" \
        > "$D/qemu-$cmd.log" 2>&1
    cp "/tmp/exos/serialsm$cmd$ARCH.txt" "$D/seriale-$cmd.txt" 2>/dev/null
    grep -a "Smonto\|li chiudo a forza\|occupato\|non e' stato smontato\|not unmounted\|SISTEMA:" "$D/seriale-$cmd.txt" | sed 's/^/    /'
    n=$(grep -ac "ancora in uso" "$D/seriale-$cmd.txt")
    n2=$(grep -ac "Smonto " "$D/seriale-$cmd.txt")
    if [ "$n" = 0 ] && [ "$n2" = 2 ]; then echo "  [OK] $cmd: smontati davvero tutti e due"
    else echo "  [NO] $cmd: $n2 smontaggi, $n ancora in uso"; esito=1; fi
    guarda "$cmd sistema" "$IMG" 2048 "tardi$cmd"
    guarda "$cmd dati" "$DATI" 0 "da$cmd"
done
[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
