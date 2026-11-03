#!/bin/bash
# =============================================================================
# tools/prova_mappa_giornale.sh - does the boot map survive the journal? (voce 75)
#
#     tools/prova_mappa_giornale.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_mappa_giornale.sh [dir]    64 bit
#
# On ext4 with 4 KB blocks block 0 holds both the boot sector and the
# superblock. A transaction open while the boot is installed, with the
# superblock in it, must not put the old boot sector back when it reaches the
# disk. Files are written and the map installed with no pause in between, as
# netupdate does; then the first sector of the partition is read from outside.
# Two ends: umount, and a crash (no umount) so that the replay is tested too.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/mappa-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
SBIN=/usr/sbin; X=cross_build/nasi5deb; P1_SETT=262144
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    KERNEL=build-64/kernel-intero.bin; STAGE2=build-64/avvio/stage2-cd.bin; RDZ=build-64/rdboot.lz4
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M; EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    KERNEL=build/kernel.bin; STAGE2=build/stage2.bin; RDZ=build/rdboot.lz4
    export EXOS_RAM=128M; EXTRA=""
fi
T="$D/albero"; mkdir -p "$T/boot"
$GCC -O2 tools/prove/avvioprova.c -o "$T/avvioprova" || exit 1
cp "$STAGE2" "$T/boot/stage2.bin"; cp "$KERNEL" "$T/boot/kernel.bin"; cp "$RDZ" "$T/boot/rdboot.lz4"
cp "$KERNEL" "$T/carico"
printf 'cp /a/carico /a/copia1\ncp /a/carico /a/copia2\n/a/avvioprova /a\n' > "$T/fa.sh"
chmod 755 "$T/avvioprova"

esito=0
prova() {  # $1 = name, $2 = last command for the drive ("" = power cut)
    local IMG="$D/hd-$1.img" P="$D/p.img" fine=()
    fakeroot sh -c "chown -R 0:0 '$T'; $SBIN/mke2fs -q -F -t ext4 -b 4096 -m 0 -L sistema -d '$T' '$P' $((P1_SETT / 2))k" || exit 1
    qemu-img create -f raw "$IMG" 134M > /dev/null
    printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83, bootable\n' $P1_SETT \
        | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
    dd if="$P" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
    [ -n "$2" ] && fine=("$2")
    EXOS_ISTANZA=mg$1$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
        EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
        timeout 300 python3 tools/qemu_drive.py "mount hd0p1 /a@5" "echo attendo@25" "/bin/sh /a/fa.sh@${ATTESA_FA:-12}" "${fine[@]}" \
        > "$D/qemu-$1.log" 2>&1
    grep -aq "^AVVIO installa 0" "$D/qemu-$1.log" || { echo "  [NO] $1: avvio non installato"; esito=1; return; }
    if [ "$1" = taglio ]; then    # the replay is done by the next mount
        EXOS_ISTANZA=mgr$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
            EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
            timeout 200 python3 tools/qemu_drive.py "mount hd0p1 /a@5" "umount /a@4" > "$D/qemu-r.log" 2>&1
    fi
    local firma
    firma=$(dd if="$IMG" bs=1 skip=$((2048 * 512 + 510)) count=2 status=none | od -An -tx1 | tr -d ' ')
    if [ "$firma" = 55aa ]; then echo "  [OK] $1: settore di avvio al suo posto"
    else echo "  [NO] $1: settore di avvio cancellato ($firma)"; esito=1; fi
    dd if="$IMG" of="$P" bs=512 skip=2048 status=none
    if $SBIN/e2fsck -fn "$P" > "$D/e2fsck-$1.log" 2>&1; then echo "  [OK] $1: e2fsck niente da riparare"
    else grep -v "^Pass\|^e2fsck " "$D/e2fsck-$1.log" | head -4 | sed 's/^/  /'; echo "  [NO] $1: e2fsck trova errori"; esito=1; fi
}
echo "=== $ARCH ==="
prova smonta "umount /a@5"
ATTESA_FA=1 prova taglio ""
[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
