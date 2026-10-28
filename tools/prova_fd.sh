#!/bin/bash
# =============================================================================
# tools/prova_fd.sh — the descriptor table of kernel 0.281 (files and sockets
# in one table, limits from the system, thousands of connections)
#
#     tools/prova_fd.sh [dir]                        32 bit
#     EXOS_ARCH=x86_64 tools/prova_fd.sh [dir]       64 bit
#     FD_N=4000 tools/prova_fd.sh                    more connections at once
#
# tools/prove/fdprova.c on an ext2 disk next to the CD (kernel 0.281, ip.drv
# 0.017, libc of 28 October 2026), all on 127.0.0.1: see the head of
# fdprova.c.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
N="${FD_N:-2000}"
D="${1:-/tmp/exos-prove/fd-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=1024M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=512M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
T="$D/albero"
mkdir -p "$T"
$GCC -O2 -Wall -s tools/prove/fdprova.c -o "$T/fdprova" -lpthread || exit 1
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/fdprova;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L epoll -d '$T' '$D/p1.img' 8192" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 10M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=16384, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
# the CD starts the network by itself on the e1000 (ip.drv, and 127.0.0.1)
export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -nic user,model=e1000"
echo "=== $ARCH ==="
EXOS_ISTANZA=fd$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 1200 python3 tools/qemu_drive.py "ipcfg@5" "mount hd0p1 /a@5" \
    "/a/fdprova $N@~600" "ipcfg -tcp@5" "umount /a@5" > "$D/qemu.log" 2>&1
S="$D/seriale.txt"
tr -d '\r' < "/tmp/exos/serialfd$ARCH.txt" > "$S" 2>/dev/null
cat "$D/qemu.log" >> "$S"
esito=0
grep -a "^FD " "$S" | grep -v "^FD fine" | awk '!visto[$0]++' | sed 's/^FD /  /'
grep -aq "^FD fine 0" "$S" || { echo "  [NO] fdprova: non tutte giuste"; esito=1; }
if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
fi
[ $esito = 0 ] || echo "  la seriale: $S"
exit $esito
