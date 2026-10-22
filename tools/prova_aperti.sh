#!/bin/bash
# =============================================================================
# tools/prova_aperti.sh — an open file follows the file, not its name, in QEMU
#
#     tools/prova_aperti.sh [dir]                     32 bit, CD
#     EXOS_ARCH=x86_64 tools/prova_aperti.sh [dir]    64 bit
#
# The disk: p1 ext2 with 4 KB blocks, p2 FAT32. tools/prove/apertiprova.c
# (built here with the EX-OS cross gcc) runs its five cases on both; then
# e2fsck -fn on p1 and fsck.fat -n on p2, and no ~EXV file may be left.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/aperti-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
P1_SETT=131072; P2_SETT=131072                 # 64 MB each
P1=$((2048 * 512)); P2=$(((2048 + P1_SETT) * 512))

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/apertiprova.c -o "$D/apertiprova" || exit 1

qemu-img create -f raw "$IMG" 130M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83\nstart=%d, size=%d, type=0c\n' \
    $P1_SETT $((2048 + P1_SETT)) $P2_SETT | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
mkdir -p "$D/albero"
cp "$D/apertiprova" "$D/albero/apertiprova"
$SBIN/mke2fs -q -F -t ext2 -b 4096 -L aperti -d "$D/albero" -E offset=$P1 "$IMG" $((P1_SETT / 8))
dd if=/dev/zero of="$D/p2.img" bs=512 count=$P2_SETT status=none
$SBIN/mkfs.fat -F 32 -n APERTI "$D/p2.img" > /dev/null
dd if="$D/p2.img" of="$IMG" bs=512 seek=$((2048 + P1_SETT)) conv=notrunc status=none

export EXOS_ISTANZA=ap$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
timeout 900 python3 tools/qemu_drive.py "mount hd0p1 /a@5" "mount hd0p2 /b@5" \
    "/a/apertiprova /a@~60" "/a/apertiprova /b@~60" "umount /a@3" "umount /b@3" \
    > "$D/qemu.log" 2>&1

echo "=== $ARCH: ext2 (a) e FAT32 (b) ==="
grep -a "^APERTI" "$D/qemu.log" | sed 's/^/  /'
esito=0
[ "$(grep -ac '^APERTI FINE 0' "$D/qemu.log")" -eq 2 ] || { echo "  [NO] casi falliti o mancanti"; esito=1; }
dd if="$IMG" of="$D/p1.img" bs=512 skip=2048 count=$P1_SETT status=none
dd if="$IMG" of="$D/p2.img" bs=512 skip=$((2048 + P1_SETT)) count=$P2_SETT status=none
$SBIN/e2fsck -fn "$D/p1.img" > "$D/e2fsck.log" 2>&1 \
    && echo "  [OK] p1: e2fsck pulito" || { echo "  [NO] p1: e2fsck (vedi $D/e2fsck.log)"; esito=1; }
$SBIN/fsck.fat -n "$D/p2.img" > "$D/fsckfat.log" 2>&1 \
    && echo "  [OK] p2: fsck.fat pulito" || { echo "  [NO] p2: fsck.fat (vedi $D/fsckfat.log)"; esito=1; }
if /usr/sbin/debugfs -R "ls -l /" "$D/p1.img" 2>/dev/null | grep -q "~EXV"; then
    echo "  [NO] p1: un ~EXV e' rimasto"; esito=1
fi
exit $esito
