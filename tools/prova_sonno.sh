#!/bin/bash
# =============================================================================
# tools/prova_sonno.sh - a sleep lasts at least the time asked (analisi_kernel
# voce 27)
#
#     tools/prova_sonno.sh [dir]                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_sonno.sh [dir]   64 bit
#     SONNO_ISO=old.iso tools/prova_sonno.sh        against another kernel
#
# tools/prove/sonnoprova.c measures nanosleep and usleep with rdtsc, each one
# started at a different point between two beats. Up to 0.335 a 10 ms sleep
# asked just before the beat came back almost at once, and sleep(1) could
# last 990 ms.
# PASS: every "SONNO" line with 0, "SONNO fine 0", no FAULT in the log.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/sonno-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"

if [ "$ARCH" = x86_64 ]; then
    ISO=${SONNO_ISO:-dist/exos64.iso}; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64 $KVM"
else
    ISO=${SONNO_ISO:-dist/exos.iso}; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=256M EXTRA="$KVM"
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/sonnoprova.c -o "$D/albero/sonnoprova" || exit 1
chmod 755 "$D/albero/sonnoprova"

qemu-img create -f raw "$IMG" 20M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=38912, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
fakeroot $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -d "$D/albero" "$D/p1.img" 19456 || exit 1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

echo "=== $ARCH ${KVM:+(KVM)}, $ISO ==="
EXOS_ISTANZA=sonno$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
    timeout 600 python3 tools/qemu_drive.py \
    "mount hd0p1 /a@5" "/a/sonnoprova@20" "umount /a@5" "echo FINE@1" > "$D/q.log" 2>&1

esito=0
righe=$(grep -a '^SONNO [a-z0-9]* [01] ' "$D/q.log")
[ -n "$righe" ] || { echo "  [NO]  nessuna riga SONNO (vedi $D/q.log)"; esito=1; }
while read -r r; do
    [ -z "$r" ] && continue
    set -- $r
    if [ "$3" = 0 ]; then echo "  [OK]  $r"; else echo "  [NO]  $r"; esito=1; fi
done <<< "$righe"
grep -aq '^SONNO fine 0' "$D/q.log" || { echo "  [NO]  manca SONNO fine 0"; esito=1; }
grep -aq 'FAULT\|PANIC' "$D/q.log" && { echo "  [NO]  FAULT o PANIC nel log"; esito=1; }
[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
