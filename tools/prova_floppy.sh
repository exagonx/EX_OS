#!/bin/bash
# =============================================================================
# tools/prova_floppy.sh — two handles of one floppy file, a file deleted or
# renamed while open (analisi voce 70), in QEMU
#
#     tools/prova_floppy.sh [dir]                     32 bit, CD
#     EXOS_ARCH=x86_64 tools/prova_floppy.sh [dir]    64 bit
#
# Boots from the CD with a COPY of dist/floppy.img (with a floppy in, '/' is
# the floppy; an empty one leaves the system without its files), runs
# tools/prove/floppyprova.c in '/', then fsck.fat -n on the copy: no lost
# clusters, no cluster in two files.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/floppy-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero"
SBIN=/usr/sbin
X=cross_build/nasi5deb

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/floppyprova.c -o "$D/albero/floppyprova" || exit 1

qemu-img create -f raw "$D/hd.img" 16M > /dev/null
$SBIN/mke2fs -q -F -t ext2 -b 1024 -d "$D/albero" "$D/hd.img"
cp dist/floppy.img "$D/fd.img" || exit 1

export EXOS_ISTANZA=fd$ARCH EXOS_CDROM="$ISO" EXOS_IMG="$D/fd.img"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$D/hd.img,format=raw,if=ide"
timeout 600 python3 tools/qemu_drive.py "mount hd0 /a@5" \
    "/a/floppyprova /@~40" "ls /@3" > "$D/qemu.log" 2>&1

echo "=== $ARCH: il floppy ==="
grep -a "^FLOPPY" "$D/qemu.log" | sed 's/^/  /'
esito=0
grep -aq '^FLOPPY FINE 0' "$D/qemu.log" || { echo "  [NO] casi falliti o mancanti"; esito=1; }
# dist/floppy.img has the volume label different in boot sector and root
# (EX-OS / EXOS): fsck.fat exits 1 for that alone, so the clusters are read
$SBIN/fsck.fat -n "$D/fd.img" > "$D/fsck.log" 2>&1
if grep -qiE "reclaim|cross-link|lost|chain|truncat|bad cluster" "$D/fsck.log"; then
    echo "  [NO] fsck.fat: $(grep -iE 'reclaim|cross|lost|chain|truncat|bad cluster' "$D/fsck.log" | head -2 | tr '\n' ' ')"; esito=1
else
    echo "  [OK] fsck.fat: nessun cluster perso o in comune"
fi
exit $esito
