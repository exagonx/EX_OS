#!/bin/bash
# =============================================================================
# tools/prova_fatbuchi.sh — FAT holes read as zeros, not old data (voce 71)
#
#     tools/prova_fatbuchi.sh [dir]                     32 bit, CD
#     EXOS_ARCH=x86_64 tools/prova_fatbuchi.sh [dir]    64 bit
#
# p1 ext2 with tools/prove/fatbuchiprova.c, p2 FAT16 with 4 KB clusters (64 MB is too small for a FAT32 the kernel accepts) made
# on an area full of 'X' (mkfs.fat does not clear the data): the free
# clusters are the "deleted files". Then fsck.fat -n on p2.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/fatbuchi-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
P1_SETT=65536; P2_SETT=131072
P1=$((2048 * 512))

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/fatbuchiprova.c -o "$D/fatbuchiprova" || exit 1

qemu-img create -f raw "$IMG" 100M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83\nstart=%d, size=%d, type=06\n' \
    $P1_SETT $((2048 + P1_SETT)) $P2_SETT | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
mkdir -p "$D/albero"
cp "$D/fatbuchiprova" "$D/albero/"
$SBIN/mke2fs -q -F -t ext2 -b 4096 -L fatbuchi -d "$D/albero" -E offset=$P1 "$IMG" $((P1_SETT / 8))
python3 -c "import sys; sys.stdout.buffer.write(b'X' * ($P2_SETT * 512))" > "$D/p2.img"
$SBIN/mkfs.fat -F 16 -s 8 -n BUCHI "$D/p2.img" > /dev/null
dd if="$D/p2.img" of="$IMG" bs=512 seek=$((2048 + P1_SETT)) conv=notrunc status=none

export EXOS_ISTANZA=fb$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" "mount hd0p2 /b@5" \
    "/a/fatbuchiprova /b@~30" "umount /b@3" "umount /a@3" > "$D/qemu.log" 2>&1

echo "=== $ARCH: buchi su FAT16 ==="
grep -a "^FATBUCHI" "$D/qemu.log" | sed 's/^/  /'
esito=0
grep -aq '^FATBUCHI FINE 0' "$D/qemu.log" || { echo "  [NO] casi falliti o mancanti"; esito=1; }
dd if="$IMG" of="$D/p2.img" bs=512 skip=$((2048 + P1_SETT)) count=$P2_SETT status=none
$SBIN/fsck.fat -n "$D/p2.img" > "$D/fsckfat.log" 2>&1 \
    && echo "  [OK] p2: fsck.fat pulito" || { echo "  [NO] p2: fsck.fat (vedi $D/fsckfat.log)"; esito=1; }
exit $esito
