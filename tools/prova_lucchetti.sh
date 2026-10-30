#!/bin/bash
# =============================================================================
# tools/prova_lucchetti.sh — file locks between processes (kernel 0.291)
#
#     tools/prova_lucchetti.sh [dir]                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_lucchetti.sh [dir]   64 bit
#
# tools/prove/lucchettiprova.c on an ext2 disk, built twice at 32 bit: with
# off_t of 32 bit and with _FILE_OFFSET_BITS=64, the way SQLite is. The waiter's
# case is also the proof that a lock goes when its process ends.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/lucchetti-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero"
M=$(hostname | tr 'A-Z' 'a-z'); X=cross_build/$M; [ -d "$X/exos-cross" ] || X=cross_build
SBIN=/usr/sbin
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=256M; EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M; EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/lucchettiprova.c -o "$D/albero/lp" || exit 1
$GCC -O2 -D_FILE_OFFSET_BITS=64 tools/prove/lucchettiprova.c -o "$D/albero/lp64" || exit 1

IMG="$D/hd.img"
qemu-img create -f raw "$IMG" 40M > /dev/null
$SBIN/mke2fs -q -F -t ext2 -b 4096 -d "$D/albero" "$IMG"

export EXOS_ISTANZA=luc$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
A=("mount hd0 /a@5")
for p in lp lp64; do
    A+=("/a/$p tutti /a/f$p@4"
        "/a/$p tieni /a/f$p 0 10 40 &@2"
        "/a/$p tieni /a/f$p 100 200 40 r &@3"
        "/a/$p altro /a/f$p@~50")
done
timeout 500 python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
tr -d '\r' < "$D/qemu.log" | grep -a '^LUCCHI' | grep -av 'tieni [0-9]' | sed 's/^/  /' | sed -n '1,60p'
esito=0
[ "$(grep -ac '^LUCCHI FINE 0' "$D/qemu.log")" -eq 4 ] || { echo "  [NO] casi falliti o mancanti"; esito=1; }
[ "$esito" = 0 ] && echo "TUTTO A POSTO" || echo "QUALCOSA NON VA"
exit $esito
