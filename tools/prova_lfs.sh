#!/bin/bash
# =============================================================================
# tools/prova_lfs.sh — _FILE_OFFSET_BITS=64 in EX-OS
#
#     tools/prova_lfs.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_lfs.sh [dir]    64 bit
#
# tools/prove/lfsprova.c, built with -D_FILE_OFFSET_BITS=64 as MariaDB is,
# on an ext2 disk next to the CD: a 64-bit off_t, lseek, pread, pwrite,
# ftruncate, mmap, stat, fstat, fseeko/ftello through the *64 functions of
# the libc; and past the kernel's 2 GB per file a refusal (EOVERFLOW, EFBIG,
# end of file), never a position cut down to 32 bits.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/lfs-$ARCH}"
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
$GCC -O2 -s -D_FILE_OFFSET_BITS=64 tools/prove/lfsprova.c -o "$T/lfsprova" || exit 1
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/lfsprova;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L lfs -d '$T' '$D/p1.img' 8192" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 10M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=16384, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
echo "=== $ARCH ==="
EXOS_ISTANZA=lf$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "/a/lfsprova /a/lfs.dat@~60" "umount /a@5" > "$D/qemu.log" 2>&1
S="$D/seriale.txt"
cp "/tmp/exos/seriallf$ARCH.txt" "$S" 2>/dev/null
cat "$D/qemu.log" >> "$S"
esito=0
grep -a "^LFS " "$S" | sort -u | grep -v "^LFS fine" | sed 's/^LFS /  /'
grep -aq "^LFS fine 0" "$S" || { echo "  [NO] lfsprova: non tutte giuste"; esito=1; }
if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] || echo "  la seriale: $S"
exit $esito
