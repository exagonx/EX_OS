#!/bin/bash
# =============================================================================
# tools/prova_moltifili.sh - hundreds of threads in one program (kernel 0.282)
#
#     tools/prova_moltifili.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_moltifili.sh [dir]    64 bit
#     MOLTI_N=400 tools/prova_moltifili.sh               threads alive together
#
# From the CD, an ext2 disk with tools/prove/moltifili.c on it. Until kernel
# 0.281 a program had 128 threads (FILO_MAX) and the system 192 tasks
# (MAX_PROCESSES): Apache's worker MPM stopped at "unable to create worker
# thread". Now both grow; the test starts MOLTI_N threads (default 300), then
# more until the system refuses with EAGAIN, joins them, and starts 50 more.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/moltifili-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=1G EXTRA="-cpu qemu64 -smp 2"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=512M EXTRA="-smp 2"
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }

T="$D/albero"
mkdir -p "$T"
$GCC -O2 $PROVA_CFLAGS tools/prove/moltifili.c -o "$T/moltifili" -lpthread 2>/dev/null ||
    $GCC -O2 $PROVA_CFLAGS tools/prove/moltifili.c -o "$T/moltifili" || exit 1
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/moltifili;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L molti -d '$T' '$D/p1.img' 32768" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 34M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=65536, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$D/p1.img" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
echo "=== $ARCH ==="
EXOS_ISTANZA=mf$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 900 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "/a/moltifili ${MOLTI_N:-300} /a@300" "umount /a@3" > "$D/qemu.log" 2>&1
S="$D/seriale.txt"
cp "/tmp/exos/serialmf$ARCH.txt" "$S" 2>/dev/null
cat "$D/qemu.log" >> "$S"
esito=0
for p in crea vivi tetto join errno chiave pila ancora; do
    r=$(grep -a "^MOLTI $p " "$S" | head -1)
    if [ -z "$r" ]; then echo "  [NO] $p: nessuna risposta"; esito=1
    elif echo "$r" | grep -q "^MOLTI $p 0"; then echo "  [OK] $r"
    else echo "  [NO] $r"; esito=1; fi
done
grep -aq "^MOLTI fine" "$S" || { echo "  [NO] moltifili non e' arrivato in fondo"; esito=1; }
if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
    grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
fi
exit $esito
