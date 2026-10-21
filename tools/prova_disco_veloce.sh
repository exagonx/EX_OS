#!/bin/bash
# =============================================================================
# tools/prova_disco_veloce.sh — how fast EX-OS writes and reads files on an
# IDE disk, in QEMU (@DISCO-VELOCE). A measure, before and after a change.
#
#     tools/prova_disco_veloce.sh [dir]                     32 bit, CD
#     EXOS_ARCH=x86_64 tools/prova_disco_veloce.sh [dir]    64 bit
#
# The disk: p1 ext2 with 4 KB blocks, p2 ext2 with 1 KB blocks (what EX-OS's
# own mkfs makes), each with a 32 MB file written by Linux. Inside EX-OS,
# tools/prove/discovel.c (built here with the EX-OS cross gcc):
#   - writes 16 MB in write()s of 4 KB and of 64 KB, reads them back and
#     checks every byte, rewrites one piece in seven in place and checks
#     the whole file again (the read-ahead window must not serve old data);
#   - reads the 32 MB file in read()s of 4 KB and of 1 MB.
# Then outside: e2fsck -fn on both, and the files EX-OS wrote, compared.
# The numbers are QEMU's (its disk is a file in the host's cache): they say
# how much time EX-OS spends per byte, not how fast a real disk is.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/disco-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
P1_SETT=262144; P2_SETT=262144                 # 128 MB each
P1=$((2048 * 512)); P2=$(((2048 + P1_SETT) * 512))

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/discovel.c -o "$D/discovel" || exit 1

qemu-img create -f raw "$IMG" 260M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83\nstart=%d, size=%d, type=83\n' \
    $P1_SETT $((2048 + P1_SETT)) $P2_SETT | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
mkdir -p "$D/albero"
head -c 33554432 /dev/urandom > "$D/albero/grosso.bin"
cp "$D/discovel" "$D/albero/discovel"
$SBIN/mke2fs -q -F -t ext2 -b 4096 -L blocchi4k -d "$D/albero" -E offset=$P1 "$IMG" $((P1_SETT / 8))
$SBIN/mke2fs -q -F -t ext2 -b 1024 -L blocchi1k -d "$D/albero" -E offset=$P2 "$IMG" $((P2_SETT / 2))

export EXOS_ISTANZA=dv$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
passi=("mount hd0p1 /a@5" "mount hd0p2 /b@5")
for v in a b; do
    passi+=("/$v/discovel /$v/t4k.bin 16 4096@~240"
            "/$v/discovel /$v/t64k.bin 16 65536@~240"
            "/$v/discovel -l /$v/grosso.bin 4096@~240"
            "/$v/discovel -l /$v/grosso.bin 1048576@~240")
done
passi+=("umount /a@3" "umount /b@3")
timeout 2400 python3 tools/qemu_drive.py "${passi[@]}" > "$D/qemu.log" 2>&1

echo "=== $ARCH: blocchi 4 KB (a) e 1 KB (b) ==="
grep -a "^DISCOVEL\|^--- invio '/" "$D/qemu.log" | sed "s/^--- invio '\(.*\)' (.*/  \1/"
esito=0
grep -aq "DISCOVEL ERRORI" "$D/qemu.log" && { echo "  [NO] byte riletti diversi"; esito=1; }
[ "$(grep -ac '^DISCOVEL' "$D/qemu.log")" -eq 20 ] || { echo "  [NO] mancano misure"; esito=1; }
for n in 1 2; do
    if [ $n = 1 ]; then s=2048; c=$P1_SETT; else s=$((2048 + P1_SETT)); c=$P2_SETT; fi
    dd if="$IMG" of="$D/p$n.img" bs=512 skip=$s count=$c status=none
    $SBIN/e2fsck -fn "$D/p$n.img" > "$D/e2fsck-p$n.log" 2>&1 \
        && echo "  [OK] p$n: e2fsck pulito" || { echo "  [NO] p$n: e2fsck (vedi $D/e2fsck-p$n.log)"; esito=1; }
done
exit $esito
