#!/bin/bash
# =============================================================================
# tools/prova_sigwait.sh — sigwait, sigtimedwait, sigsuspend in EX-OS
#
#     tools/prova_sigwait.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_sigwait.sh [dir]    64 bit
#
# tools/prove/sigwaitprova.c on an ext2 disk next to the CD (kernel 0.279):
# a pending signal taken by sigwait, sigtimedwait with and without a signal,
# SIGTERM blocked in every thread and taken by the one in sigwait (MariaDB's
# signal thread), sigsuspend, and a SIGTERM from another process to a
# program that blocks it (and, as a control, to one that does not).
# The older signal test (handlers, mprotect, faults) is tools/prova_segnali.sh.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/sigwait-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=256M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
T="$D/albero"
mkdir -p "$T"
$GCC -O2 -s tools/prove/sigwaitprova.c -o "$T/sigwaitprova" -lpthread || exit 1
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/sigwaitprova;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L sigwait -d '$T' '$D/p1.img' 8192" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 10M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=16384, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
echo "=== $ARCH ==="
EXOS_ISTANZA=sg$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "/a/sigwaitprova@~60" "umount /a@5" > "$D/qemu.log" 2>&1
S="$D/seriale.txt"
cp "/tmp/exos/serialsg$ARCH.txt" "$S" 2>/dev/null
cat "$D/qemu.log" >> "$S"
esito=0
grep -a "^SEGNALI " "$S" | sort -u | grep -v "^SEGNALI fine" | sed 's/^SEGNALI /  /'
grep -aq "^SEGNALI fine 0" "$S" || { echo "  [NO] sigwaitprova: non tutte giuste"; esito=1; }
if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] || echo "  la seriale: $S"
exit $esito
