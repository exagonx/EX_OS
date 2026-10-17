#!/bin/bash
# =============================================================================
# tools/prova_attesa.sh — the "loading, please wait" box, and rename across
# directories (kernel 0.260), inside EX-OS in QEMU
#
#     tools/prova_attesa.sh                    32 bit (dist/exos.iso)
#     EXOS_ARCH=x86_64 tools/prova_attesa.sh   64 bit (dist/exos64.iso)
#
# 1. On a scratch ext2 disk: mv of a directory into another directory and
#    back, a file inside it; mv of a directory into itself must be refused.
#    The host checks the disk afterwards with e2fsck.
# 2. ExWin: /exwin/bin/attesa in front of
#    a) ping (no window, about 5 s, ends with 0): the box shows, then goes;
#    b) calctor: the box goes when the calculator's window appears;
#    c) a program that does not exist: the box says it did not start.
#    Screenshots in $D/*.png.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/claude-1004/attesa-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

if [ "$ARCH" = x86_64 ]; then
    export EXOS_QEMU=qemu-system-x86_64 EXOS_CDROM=dist/exos64.iso EXOS_RAM=512M
    CPU="-cpu qemu64"; RETE="e1000"
else
    export EXOS_CDROM=dist/exos.iso EXOS_RAM=128M
    CPU=""; RETE="ne2k_pci"
fi
EXOS_ISTANZA=att$ARCH EXOS_NO_FLOPPY=1 \
EXOS_QEMU_EXTRA="$CPU -drive file=$IMG,format=raw,if=ide -netdev user,id=n1 -device $RETE,netdev=n1" \
    timeout 900 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" "mount hd0p1 /disk@6" \
    "echo MV-INIZIO@1" "mkdir /disk/a@1" "mkdir /disk/a/b@1" "cp /bin/ls /disk/a/b/f@2" \
    "mv /disk/a/b /disk/c@3" "ls /disk/c@2" "ls /disk/a@2" \
    "mv /disk/c /disk/a/c@3" "ls /disk/a/c@2" "mv /disk/a /disk/a/c/x@3" "ls /disk/a/c@2" "ls /disk/a/c/b@2" \
    "echo MV-FINE@1" \
    "exwin@25" "key:alt-f1@2" \
    "/exwin/bin/attesa -n Prova -- /bin/ping 10.0.2.2 -n 6 &@1" "key:alt-f5@1" \
    "foto:$D/a1.ppm@1" "foto:$D/a2.ppm@1" "foto:$D/a3.ppm@12" \
    "key:alt-f1@1" "/exwin/bin/attesa -n Calcolatrice -- /exwin/bin/calctor &@1" "key:alt-f5@1" \
    "foto:$D/b1.ppm@6" \
    "key:alt-f1@1" "/exwin/bin/attesa -n Inesistente -- /bin/nonesiste &@2" "key:alt-f5@2" \
    "foto:$D/c1.ppm@1" > "$D/exos.log" 2>&1

for f in "$D"/*.ppm; do convert "$f" "${f%.ppm}.png" 2>/dev/null; done
dd if="$IMG" of="$D/part.img" bs=1M skip=1 status=none
echo "--- mv (dal registro):"
sed 's/\x1b\[[0-9;]*m//g' "$D/exos.log" | tr -d '\000' | sed -n '/^MV-INIZIO/,/^MV-FINE/p' | grep -v '^ex-os'
echo "--- e2fsck:"
/sbin/e2fsck -fn "$D/part.img" 2>&1 | tail -4
/sbin/debugfs -R "ls -l /a/c" "$D/part.img" 2>/dev/null
echo "--- foto: $D/*.png"
