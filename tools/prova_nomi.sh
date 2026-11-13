#!/bin/bash
# =============================================================================
# tools/prova_nomi.sh - names, sizes and files in pieces on ext2, ext4 and FAT
# (kernel 0.333, analisi_kernel voci 56 and 75)
#
#     tools/prova_nomi.sh [dir]                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_nomi.sh [dir]   64 bit
#
# One disk, three partitions made by Linux: p1 ext2, p2 ext4 (with its
# journal), p3 FAT32. tools/prove/nomiprova.c runs on each: renames, unlinks
# and names made again, each looked at at once through the old and the new
# name; sizes after an append, a truncate, a chmod; a 3 MB file read back in
# 4 KB pieces forward, backward and jumping. Then umount, and from outside
# e2fsck -fn and fsck.fat -n must find nothing.
# PASS: "nomiprova: 0 NO" three times, clean checks, no FAULT in the log.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/nomi-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
S1=131072; S2=131072; S3=262144                 # sectors: 64, 64, 128 MB
O1=2048; O2=$((O1 + S1)); O3=$((O2 + S2))

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
[ -x $SBIN/mkfs.fat ] || { echo "manca mkfs.fat (dosfstools)" >&2; exit 1; }
$GCC -O2 tools/prove/nomiprova.c -o "$D/albero/nomiprova" || exit 1
chmod 755 "$D/albero/nomiprova"

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

echo "=== disco: ext2, ext4, FAT32 fatti da Linux ($ARCH) ==="
qemu-img create -f raw "$IMG" $(((O3 + S3) / 2048 + 1))M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=83\nstart=%d, size=%d, type=c\n' \
    $O1 $S1 $O2 $S2 $O3 $S3 | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
fakeroot $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -d "$D/albero" "$D/p1.img" $((S1 / 2)) || exit 1
fakeroot $SBIN/mke2fs -q -F -t ext4 -b 4096 -m 0 -d "$D/albero" "$D/p2.img" $((S2 / 8)) || exit 1
dd if=/dev/zero of="$D/p3.img" bs=512 count=$S3 status=none
$SBIN/mkfs.fat -F 32 -n NOMI "$D/p3.img" > /dev/null || exit 1
dd if="$D/p1.img" of="$IMG" bs=512 seek=$O1 conv=notrunc status=none
dd if="$D/p2.img" of="$IMG" bs=512 seek=$O2 conv=notrunc status=none
dd if="$D/p3.img" of="$IMG" bs=512 seek=$O3 conv=notrunc status=none

EXOS_ISTANZA=nomi$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
    timeout 900 python3 tools/qemu_drive.py \
    "mount hd0p1 /a@5" "mount hd0p2 /b@5" "mount hd0p3 /c@5" \
    "/a/nomiprova /a@60" "/a/nomiprova /b@60" "/a/nomiprova /c fat@90" \
    "umount /c@5" "umount /b@5" "umount /a@5" "echo FINE@1" > "$D/q.log" 2>&1

n=$(grep -ac 'nomiprova: 0 NO' "$D/q.log")
[ "$n" = 3 ] && ok "nomiprova: 0 NO su ext2, ext4 e FAT32" \
    || { no "nomiprova: $n volte 0 NO su 3"; grep -a '^NO \|nomiprova:' "$D/q.log" | head -20; }
grep -aq 'FAULT\|PANIC' "$D/q.log" && no "FAULT o PANIC nel log" || ok "niente FAULT nel log"

dd if="$IMG" of="$D/p1.img" bs=512 skip=$O1 count=$S1 status=none
dd if="$IMG" of="$D/p2.img" bs=512 skip=$O2 count=$S2 status=none
dd if="$IMG" of="$D/p3.img" bs=512 skip=$O3 count=$S3 status=none
$SBIN/e2fsck -fn "$D/p1.img" > "$D/e2fsck1.log" 2>&1 && ok "p1 ext2: e2fsck pulito" || no "p1 ext2: e2fsck (vedi $D/e2fsck1.log)"
$SBIN/e2fsck -fn "$D/p2.img" > "$D/e2fsck2.log" 2>&1 && ok "p2 ext4: e2fsck pulito" || no "p2 ext4: e2fsck (vedi $D/e2fsck2.log)"
$SBIN/fsck.fat -n "$D/p3.img" > "$D/fsckfat.log" 2>&1 && ok "p3 FAT32: fsck.fat pulito" || no "p3 FAT32: fsck.fat (vedi $D/fsckfat.log)"

[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
