#!/bin/bash
# =============================================================================
# tools/prova_dma_morto.sh — a card outliving its driver writes nowhere (voce 63)
#
#     tools/prova_dma_morto.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_dma_morto.sh [dir]    64 bit
#     EXOS_ISO=old.iso tools/prova_dma_morto.sh [dir]    another CD
#
# ohci.drv is killed and a program holds nearly all the free RAM at zero: the
# controller, still on, writes its frame number there every millisecond.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/dma-$ARCH}"
SBIN=/usr/sbin; [ -x /sbin/mke2fs ] && SBIN=/sbin
X=cross_build
rm -rf "$D"; mkdir -p "$D/albero"
if [ "$ARCH" = x86_64 ]; then
    ISO=${EXOS_ISO:-dist/exos64.iso}; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=256M EXTRA="-cpu qemu64"; MB=200
else
    ISO=${EXOS_ISO:-dist/exos.iso}; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M EXTRA=""; MB=80
fi
$GCC -O2 tools/prove/memsporca.c -o "$D/albero/memsporca" || exit 1
IMG="$D/disco.img"; S=32768
qemu-img create -f raw "$IMG" 20M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83\n' $S | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if=/dev/zero of="$D/p.img" bs=512 count=$S status=none
$SBIN/mke2fs -q -F -t ext2 -b 1024 -d "$D/albero" "$D/p.img" || exit 1
dd if="$D/p.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
export EXOS_ISTANZA=dma$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -device pci-ohci"
timeout 60 python3 tools/qemu_drive.py "ps@3" > "$D/ps.log" 2>&1
PID=$(tr -d '\r' < "$D/ps.log" | sed 's/\x1b\[[0-9;]*m//g' | grep -a 'ohci.drv' | awk '$1 ~ /^[0-9]+$/ {print $1}' | tail -1)
[ -n "$PID" ] || { echo "  [NO] ohci.drv non gira (vedi $D/ps.log)"; exit 1; }
echo "ohci.drv: PID $PID"
timeout 200 python3 tools/qemu_drive.py "mount -r hd0p1 /a@8" "ps@3" "kill -9 $PID@4" "ps@3" \
    "echo R3@1" "/a/memsporca $MB 15@~120" > "$D/a.log" 2>&1
tr -d '\r' < "$D/a.log" | sed 's/\x1b\[[0-9;]*m//g' > "$D/pulito.log"
grep -aE '^memsporca:|^secondo [0-9]|PAGING:.*DMA' "$D/pulito.log" | tail -6 | cut -c1-90
if grep -aq '^memsporca: 0 byte estranei' "$D/pulito.log"; then
    echo "  [OK] il controller senza driver non scrive in memoria d'altri"; exit 0
fi
echo "  [NO] memoria scritta da fuori, o prova non arrivata in fondo ($D/pulito.log)"; exit 1
