#!/bin/bash
# =============================================================================
# tools/prova_librerie.sh — zlib, bzip2, xz, lz4, zstd, PCRE2, {fmt} in EX-OS
#
#     tools/prova_librerie.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_librerie.sh [dir]    64 bit
#
# The libraries of tools/<library>-exos/costruisci.sh, from the cross
# sysroot, on an ext2 disk next to the CD:
#   1. tools/prove/libreprova.cpp: each compressor packs 64 KB and gets it
#      back, PCRE2 matches (and its POSIX layer), fmt formats;
#   2. the programs: xz, zstd, bzip2 and lz4 compress a file inside EX-OS and
#      Linux checks the archives; they unpack archives Linux made, and Linux
#      compares what came out. Both ways, so a compressor that only reads
#      its own output does not pass.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/librerie-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
OFF="$IMG?offset=1048576"
SBIN=/usr/sbin
X=cross_build/nasi5deb
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GXX=$X/exos-cross64/bin/x86_64-exos-g++; BIT=64
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GXX=$X/exos-cross/bin/i386-exos-g++; BIT=32
    export EXOS_RAM=256M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
T="$D/albero"
mkdir -p "$T/bin"
$GXX -O2 -s tools/prove/libreprova.cpp -o "$T/libreprova" -lfmt -lpcre2-posix -lpcre2-8 \
    -lzstd -llz4 -llzma -lbz2 -lz || { echo "  (mancano librerie: tools/<lib>-exos/costruisci.sh)"; exit 1; }
for p in xz/xz zstd/zstd bzip2/bzip2 lz4/lz4; do
    cp "cross_build/${p%/*}-exos$BIT/exos/bin/${p#*/}" "$T/bin/" || exit 1
done
# the Linux side needs the lz4 program too: if this machine has none, one is
# built for it from the same archive
if ! command -v lz4 > /dev/null; then
    H=cross_build/costruzione-lz4/host
    if [ ! -x "$H/lz4" ]; then
        rm -rf "$H"; mkdir -p "$H"
        tar -xzf cross_build/terze-parti/lz4/lz4-1.10.0.tar.gz -C "$H"
        make -C "$H"/lz4-1.10.0/programs -j3 lz4 > "$H/costruisci.log" 2>&1 \
            && cp "$H"/lz4-1.10.0/programs/lz4 "$H/lz4" || { echo "  lz4 per Linux non si costruisce"; exit 1; }
    fi
    lz4() { "$PWD/$H/lz4" "$@"; }
fi
# 200 KB of text, and the same packed by Linux
for i in $(seq 1 4000); do echo "riga $i: il testo che EX-OS comprime e Linux controlla"; done > "$T/dati"
xz -k -c "$T/dati" > "$T/l.xz"; zstd -q -c "$T/dati" > "$T/l.zst"
bzip2 -k -c "$T/dati" > "$T/l.bz2"; lz4 -q -c "$T/dati" > "$T/l.lz4"
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/libreprova '$T'/bin/*;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L librerie -d '$T' '$D/p1.img' 32768" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 40M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=65536, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
echo "=== $ARCH ==="
EXOS_ISTANZA=lb$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 900 python3 tools/qemu_drive.py "mount hd0p1 /a@5" "cd /a@2" \
    "/a/libreprova@~120" \
    "/a/bin/xz -k dati@~120" "/a/bin/zstd -q -k dati@~60" \
    "/a/bin/bzip2 -k dati@~60" "/a/bin/lz4 -q dati dati.lz4@~60" \
    "/a/bin/xz -d -c l.xz > d.xz@~60" "/a/bin/zstd -q -d -c l.zst > d.zst@~60" \
    "/a/bin/bzip2 -d -c l.bz2 > d.bz2@~60" "/a/bin/lz4 -q -d l.lz4 d.lz4@~60" \
    "ls -l /a@3" "cd /@2" "umount /a@5" > "$D/qemu.log" 2>&1
S="$D/seriale.txt"
cp "/tmp/exos/seriallb$ARCH.txt" "$S" 2>/dev/null
cat "$D/qemu.log" >> "$S"
esito=0
grep -a "^LIBRE " "$S" | sort -u | grep -v "^LIBRE fine" | sed 's/^LIBRE /  /'
grep -aq "^LIBRE fine 0" "$S" || { echo "  [NO] libreprova: non tutte giuste"; esito=1; }
# what EX-OS made, read back from the disk
for f in dati.xz dati.zst dati.bz2 dati.lz4 d.xz d.zst d.bz2 d.lz4; do
    $SBIN/debugfs -R "dump /$f $D/$f" "$OFF" > /dev/null 2>&1
done
controlla() {   # $1 what, $2 command that must succeed
    if eval "$2" > /dev/null 2>&1; then echo "  ok  $1"; else echo "  [NO] $1"; esito=1; fi
}
controlla "xz di EX-OS -> xz di Linux"       "xz -d -c '$D/dati.xz' | cmp - '$T/dati'"
controlla "zstd di EX-OS -> zstd di Linux"   "zstd -q -d -c '$D/dati.zst' | cmp - '$T/dati'"
controlla "bzip2 di EX-OS -> bzip2 di Linux" "bzip2 -d -c '$D/dati.bz2' | cmp - '$T/dati'"
controlla "lz4 di EX-OS -> lz4 di Linux"     "lz4 -q -d -c '$D/dati.lz4' | cmp - '$T/dati'"
controlla "xz di Linux -> xz di EX-OS"       "cmp '$D/d.xz' '$T/dati'"
controlla "zstd di Linux -> zstd di EX-OS"   "cmp '$D/d.zst' '$T/dati'"
controlla "bzip2 di Linux -> bzip2 di EX-OS" "cmp '$D/d.bz2' '$T/dati'"
controlla "lz4 di Linux -> lz4 di EX-OS"     "cmp '$D/d.lz4' '$T/dati'"
if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] || echo "  la seriale: $S"
exit $esito
