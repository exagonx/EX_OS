#!/bin/bash
# =============================================================================
# tools/prova_zona_rossa.sh — the stack grows for a write in the red zone
#
#     tools/prova_zona_rossa.sh [dir]                     64 bit only
#     EXOS_ISO=old.iso tools/prova_zona_rossa.sh [dir]    another CD
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-prove/zonarossa}"
SBIN=/usr/sbin; [ -x /sbin/mke2fs ] && SBIN=/sbin
rm -rf "$D"; mkdir -p "$D/albero"
ISO=${EXOS_ISO:-dist/exos64.iso}
cross_build/exos-cross64/bin/x86_64-exos-gcc -O1 tools/prove/zonarossa.c -o "$D/albero/zonarossa" -lpthread || exit 1
IMG="$D/disco.img"; S=32768
qemu-img create -f raw "$IMG" 20M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83\n' $S | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if=/dev/zero of="$D/p.img" bs=512 count=$S status=none
$SBIN/mke2fs -q -F -t ext2 -b 1024 -d "$D/albero" "$D/p.img" || exit 1
dd if="$D/p.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=256M EXOS_ISTANZA=zr EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="-cpu qemu64 -drive file=$IMG,format=raw,if=ide"
timeout 90 python3 tools/qemu_drive.py "mount -r hd0p1 /a@8" "/a/zonarossa@6" > "$D/a.log" 2>&1
tr -d '\r' < "$D/a.log" | sed 's/\x1b\[[0-9;]*m//g' > "$D/pulito.log"
grep -aE 'zonarossa:|FAULT' "$D/pulito.log" | grep -av 'invio' | cut -c1-100
if grep -aq 'secondo filo a posto' "$D/pulito.log" && grep -aq 'filo principale a posto' "$D/pulito.log"; then
    echo "  [OK] la pila cresce per una scrittura nella zona rossa"; exit 0
fi
echo "  [NO] vedi $D/pulito.log"; exit 1
