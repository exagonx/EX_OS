#!/bin/bash
# =============================================================================
# tools/prova_giornale_tempo.sh - commits that wait (kernel 0.307)
#
#     tools/prova_giornale_tempo.sh [dir]                  32 bit
#     EXOS_ARCH=x86_64 tools/prova_giornale_tempo.sh [dir] 64 bit
#
# QEMU is stopped without umount. A: a file created and left alone for 8 s is on
# the disk, with no operation after it: only the kernel's task can have
# committed it. B: fsync at once. C: an unlinked file whose blocks are taken at
# once by a new file: after the crash the volume is clean and the old file does
# not come back with the new data (tools/prove/riuso.c). D (ext4.mod 0.006,
# analisi_kernel voce 74): a synced file unlinked, a new one written with no
# fsync, and QEMU stopped 1 s later (monitor "stop"), before any commit: the unlinked file may
# come back, but only with its own bytes - its blocks were held, not reused.
# From outside e2fsck replays the journal and must find nothing else.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/giornale-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero"
X=cross_build/nasi5deb
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M EXTRA=""
fi
# ! KVM WHEN THERE IS ONE: without it the guest is so slow that case D took
# more than the 5 s of a transaction, the timer committed first and the case
# tested nothing (seen 13 November 2026, 64 blocks held at that commit).
[ -w /dev/kvm ] && EXTRA="$EXTRA -enable-kvm"
$GCC -O2 tools/prove/riuso.c -o "$D/albero/riuso" || exit 1
chmod 755 "$D/albero/riuso"
IMG="$D/hd.img"
fakeroot /usr/sbin/mke2fs -q -F -t ext4 -b 4096 -m 0 -d "$D/albero" "$IMG" 64M > /dev/null 2>&1
EXOS_ISTANZA=gt$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
    timeout 200 python3 tools/qemu_drive.py "mount hd0 /a@6" \
    "/a/riuso /a@8" "echo tempo > /a/atempo@9" "/a/riuso /a 2@1" "mon:stop@0" > "$D/q.log" 2>&1
grep -aq 'RIUSO FATTO' "$D/q.log" || { echo "  [NO] riuso non ha finito"; tail -3 "$D/q.log"; exit 1; }
grep -aq 'RIUSO2 FATTO' "$D/q.log" || { echo "  [NO] riuso 2 non ha finito"; tail -3 "$D/q.log"; exit 1; }
esito=0
v() { if [ "$1" = 0 ]; then echo "  [OK] $2"; else echo "  [NO] $2"; esito=1; fi; }
n=$(/usr/sbin/dumpe2fs -h "$IMG" 2>/dev/null | grep -c needs_recovery)
v $([ "$n" -ge 1 ] && echo 0 || echo 1) "fermato senza umount: needs_recovery acceso"
/usr/sbin/e2fsck -fy "$IMG" > "$D/e2fsck.log" 2>&1; r=$?
v $([ $r -le 1 ] && echo 0 || echo 1) "e2fsck ripara col giornale (codice $r)"
/usr/sbin/e2fsck -fn "$IMG" > "$D/e2fsck2.log" 2>&1
v $? "e2fsck di nuovo: niente da riparare"
f() { /usr/sbin/debugfs -R "cat /$1" "$IMG" 2>/dev/null; }
v $([ "$(f atempo)" = tempo ] && echo 0 || echo 1) "A: il file lasciato 8 s c'e' senza altre operazioni"
v $([ "$(f sincrono | head -c 5)" = dentr ] && echo 0 || echo 1) "B: il file con fsync c'e'"
/usr/sbin/debugfs -R "stat /vecchio" "$IMG" 2>&1 | grep -q 'not found'
v $? "C: il file cancellato non e' tornato"
if /usr/sbin/debugfs -R "stat /vecchio2" "$IMG" 2>&1 | grep -q 'not found'; then
    echo "  [OK] D: la cancellazione era arrivata al disco prima dello spegnimento"
else
    altri=$(f vecchio2 | tr -d V | wc -c); tutti=$(f vecchio2 | wc -c)
    v $([ "$altri" = 0 ] && [ "$tutti" = 262144 ] && echo 0 || echo 1) \
        "D: il file cancellato e' tornato con i suoi byte ($tutti, $altri non suoi)"
fi
[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
