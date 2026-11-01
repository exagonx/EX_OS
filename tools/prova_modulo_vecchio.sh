#!/bin/bash
# =============================================================================
# tools/prova_modulo_vecchio.sh — a stale /dev/ext4.mod must not leave the disk
# read-only (kernel 0.293), in QEMU
#
#     tools/prova_modulo_vecchio.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_modulo_vecchio.sh [dir]    64 bit
#
# The disk is the one of tools/prova_sistema.sh, with in /dev/ext4.mod the
# right module except for the trailer, which says kernel 0.280: same size,
# as on the laptop on 30 October 2026. The kernel must refuse it, take
# /rdboot/dev/ext4.mod and leave / writable.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/modulo-vecchio-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
P1_SETT=131072

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    KERNEL=build-64/kernel-intero.bin; STAGE2=build-64/avvio/stage2-cd.bin
    RD=build-64/rdboot.img; RDZ=build-64/rdboot.lz4
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    KERNEL=build/kernel.bin; STAGE2=build/stage2.bin; RD=build/rdboot.img; RDZ=build/rdboot.lz4
    export EXOS_RAM=128M EXTRA=""
fi

T="$D/albero"
mkdir -p "$T/boot" "$T/da"
$GCC -O2 tools/prove/avvioprova.c -o "$T/avvioprova" || exit 1
cp "$STAGE2" "$T/da/stage2.bin"
cp "$KERNEL" "$T/da/kernel.bin"
cp "$RDZ"    "$T/da/rdboot.lz4"
grep -v '^login *=' boot/kernel.cfg > "$T/boot/kernel.cfg"
for d in bin lib dev; do $SBIN/debugfs -R "rdump /rdboot/$d $T" "$RD" > /dev/null 2>&1; done

python3 tools/modulo-invecchia.py "$T/dev/ext4.mod" 0.280 || exit 1

P="$D/p1.img"
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/bin/* '$T'/avvioprova;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L sistema -d '$T' '$P' $((P1_SETT / 2))" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 66M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83, bootable\n' $P1_SETT \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$P" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
echo "=== $ARCH ==="
esito=0

EXOS_ISTANZA=mv1$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "cp /a/da/stage2.bin /a/boot/stage2.bin@3" "cp /a/da/kernel.bin /a/boot/kernel.bin@4" \
    "cp /a/da/rdboot.lz4 /a/boot/rdboot.lz4@4" "/a/avvioprova /a@10" \
    "umount /a@3" > "$D/qemu1.log" 2>&1
grep -aq "^AVVIO installa 0" "$D/qemu1.log" || { echo "  [NO] l'installazione dell'avvio"; exit 1; }

EXOS_ISTANZA=mv2$ARCH EXOS_NO_FLOPPY=1 \
    timeout 600 python3 tools/qemu_drive.py "cat /proc/mounts@3" \
    "echo scritto > /prova.txt@3" "cat /prova.txt@2" "poweroff@10" > "$D/qemu2.log" 2>&1
cp "/tmp/exos/serialmv2$ARCH.txt" "$D/seriale2.txt" 2>/dev/null

prova() { if tr -d '\r' < "$3" | grep -aq "$2"; then echo "  [OK] $1"; else echo "  [NO] $1 (vedi $3)"; esito=1; fi; }
prova "il modulo del disco e' rifiutato" "is built for kernel 0.280" "$D/seriale2.txt"
prova "si ripiega su /rdboot/dev/ext4.mod" "using /rdboot/dev/ext4.mod" "$D/seriale2.txt"
prova "la radice e' in scrittura" "^hd0p1 / ext2 rw" "$D/seriale2.txt"
prova "il file scritto si rilegge" "^scritto" "$D/seriale2.txt"
dd if="$IMG" of="$D/p.img" bs=512 skip=2048 count=$P1_SETT status=none
if $SBIN/e2fsck -fn "$D/p.img" > "$D/e2fsck.log" 2>&1; then echo "  [OK] e2fsck pulito"
else echo "  [NO] e2fsck (vedi $D/e2fsck.log)"; esito=1; fi
[ $esito = 0 ] && echo "TUTTO A POSTO" || echo "QUALCOSA NON VA"
exit $esito
