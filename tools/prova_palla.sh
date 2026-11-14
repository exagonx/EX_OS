#!/bin/bash
# =============================================================================
# tools/prova_palla.sh - a woken thread on an idle processor (analisi_kernel
# voce 24)
#
# ! IT FAILS ON KERNEL 0.335, ON PURPOSE: voce 24 was measured and taken out
# (fuori_uso/voce24-ipi.patch, see fuori_uso/LEGGIMI.txt). With the patch it
# gave 47729 rounds a second at 64 bit and 131808 at 32, without it 117.
#
#     tools/prova_palla.sh [dir]                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_palla.sh [dir]   64 bit
#     PALLA_ISO=old.iso tools/prova_palla.sh        against another kernel
#
# Two processors (KVM when /dev/kvm can be opened). tools/prove/pallaprova.c:
# one thread spins and wakes the other through a condition variable, which
# answers from the processor that was in hlt. Up to 0.334 about 100 rounds a
# second (the idle processor's tick); with the message to the idle
# processor, thousands.
# PASS: at least 1000 rounds a second, no FAULT in the log.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/palla-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"

if [ "$ARCH" = x86_64 ]; then
    ISO=${PALLA_ISO:-dist/exos64.iso}; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64 -smp 2 $KVM"
else
    ISO=${PALLA_ISO:-dist/exos.iso}; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=256M EXTRA="-smp 2 $KVM"
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/pallaprova.c -o "$D/albero/pallaprova" -lpthread || exit 1
chmod 755 "$D/albero/pallaprova"

qemu-img create -f raw "$IMG" 20M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=38912, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
fakeroot $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -d "$D/albero" "$D/p1.img" 19456 || exit 1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

echo "=== $ARCH, 2 processori ${KVM:+(KVM)}, $ISO ==="
EXOS_ISTANZA=palla$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
    timeout 600 python3 tools/qemu_drive.py \
    "mount hd0p1 /a@5" "/a/pallaprova 3@15" "umount /a@5" "echo FINE@1" > "$D/q.log" 2>&1

esito=0
riga=$(grep -a '^PALLA' "$D/q.log" | tail -1)
echo "  $riga"
n=$(echo "$riga" | sed -n 's/.*, \([0-9]*\) al secondo.*/\1/p')
if [ -n "$n" ] && [ "$n" -ge 1000 ]; then echo "  [OK]  $n giri al secondo (almeno 1000)"
else echo "  [NO]  ${n:-nessun} giri al secondo (almeno 1000; vedi $D/q.log)"; esito=1; fi
grep -aq 'FAULT\|PANIC' "$D/q.log" && { echo "  [NO]  FAULT o PANIC nel log"; esito=1; }
[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
