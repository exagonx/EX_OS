#!/bin/bash
# =============================================================================
# tools/prova_iso_letture.sh - files of an ISO 9660 CD read in pieces
# (analisi_kernel voce 75)
#
#     tools/prova_iso_letture.sh [dir]                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_iso_letture.sh [dir]   64 bit
#     ISOL_ISO=old.iso tools/prova_iso_letture.sh         against another kernel
#
# xorriso writes a CD (Joliet) with grande.bin (8 MB + 777 bytes, byte i is a
# formula of i), files of 1, 2047, 2048 and 2049 bytes, a/b/c/d/e/profondo.txt.
# It goes in as the second CD (cd1); tools/prove/isoletture.c reads it in
# sequence with pieces of 1 byte to 300 KB, at 1500 random places, and says
# how fast. Since 0.337 a path is resolved once per mount and whole blocks go
# straight into the program's buffer.
# PASS: every "ISOL" line with 0, "ISOL fine 0", no FAULT in the log.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/isol-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero" "$D/cd/a/b/c/d/e"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"

if [ "$ARCH" = x86_64 ]; then
    ISO=${ISOL_ISO:-dist/exos64.iso}; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64 $KVM"
else
    ISO=${ISOL_ISO:-dist/exos.iso}; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=256M EXTRA="$KVM"
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/isoletture.c -o "$D/albero/isoletture" || exit 1
chmod 755 "$D/albero/isoletture"

python3 - "$D/cd" <<'PY'
import sys
d = sys.argv[1]
def dati(n):
    return bytes((((i * 2654435761) & 0xffffffff) >> 13 ^ (i >> 11)) & 0xff for i in range(n))
open(d + "/grande.bin", "wb").write(dati(8 * 1024 * 1024 + 777))
for nome, n in (("uno.bin", 1), ("b2047.bin", 2047), ("b2048.bin", 2048), ("b2049.bin", 2049)):
    open(d + "/" + nome, "wb").write(dati(n))
open(d + "/a/b/c/d/e/profondo.txt", "wb").write(b"profondo")
PY
xorriso -as mkisofs -quiet -J -o "$D/cd.iso" "$D/cd" || exit 1

qemu-img create -f raw "$IMG" 20M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=38912, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
fakeroot $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -d "$D/albero" "$D/p1.img" 19456 || exit 1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

echo "=== $ARCH ${KVM:+(KVM)}, $ISO ==="
EXOS_ISTANZA=isol$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide,index=0 -drive file=$D/cd.iso,if=ide,index=3,media=cdrom" \
    timeout 900 python3 tools/qemu_drive.py \
    "mount hd0p1 /a@5" "mount cd1 /c@5" "/a/isoletture /c@90" "umount /c@5" "umount /a@5" "echo FINE@1" > "$D/q.log" 2>&1

esito=0
righe=$(grep -a '^ISOL [a-z]* [01] ' "$D/q.log")
[ -n "$righe" ] || { echo "  [NO]  nessuna riga ISOL (vedi $D/q.log)"; esito=1; }
while read -r r; do
    [ -z "$r" ] && continue
    set -- $r
    if [ "$3" = 0 ]; then echo "  [OK]  $r"; else echo "  [NO]  $r"; esito=1; fi
done <<< "$righe"
grep -aq '^ISOL fine 0' "$D/q.log" || { echo "  [NO]  manca ISOL fine 0"; esito=1; }
grep -aq 'FAULT\|PANIC' "$D/q.log" && { echo "  [NO]  FAULT o PANIC nel log"; esito=1; }
[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
