#!/bin/bash
# =============================================================================
# tools/prova_swapmem.sh — the swap area under pressure, and `mem` telling it
#
#     tools/prova_swapmem.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_swapmem.sh [dir]    64 bit
#
# The disk of tools/prova_system.sh (ext2 with the boot and the base) plus a
# second partition for the swap, and `swap = hd0p2` in its kernel.cfg.
#   1. from the CD: the boot is installed and `mkswap -f hd0p2`;
#   2. from the disk, with little RAM: `mem` shows the area with both
#      counters at 0; `swaptest` asks for more than the free RAM and must get
#      every page back identical; `mem` again must show pages gone out and
#      come back; then two swaptest at once, each asking for more than half
#      the RAM - the races between processes (24 October 2026: on the CQ60 nothing said whether Firefox
#      ever reached the swap).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/swapmem-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
P1_SETT=131072                                 # 64 MB of system
P2_SETT=524288                                 # 256 MB of swap

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso
    KERNEL=build-64/kernel-intero.bin; STAGE2=build-64/avvio/stage2-cd.bin
    RD=build-64/rdboot.img; RDZ=build-64/rdboot.lz4
    MEM=build-64/bin/mem; SWT=build-64/bin/swaptest; GCC=cross_build/nasi5deb/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=256M EXTRA="-cpu qemu64"
    CHIEDI=300; META=170
else
    ISO=dist/exos.iso
    KERNEL=build/kernel.bin; STAGE2=build/stage2.bin; RD=build/rdboot.img; RDZ=build/rdboot.lz4
    MEM=build/bin/mem; SWT=build/bin-cd/swaptest; GCC=cross_build/nasi5deb/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M EXTRA=""
    CHIEDI=160; META=85
fi
for f in "$ISO" "$KERNEL" "$STAGE2" "$RD" "$RDZ" "$MEM" "$SWT"; do
    [ -f "$f" ] || { echo "manca $f" >&2; exit 1; }
done

T="$D/albero"
mkdir -p "$T/boot" "$T/da"
$GCC -O2 tools/prove/avvioprova.c -o "$T/avvioprova" || exit 1
cp "$STAGE2" "$T/da/stage2.bin"
cp "$KERNEL" "$T/da/kernel.bin"
cp "$RDZ"    "$T/da/rdboot.lz4"
{ grep -v '^login *=' boot/kernel.cfg | sed '/^\[kernel\]/a swap = hd0p2'; } > "$T/boot/kernel.cfg"
for d in bin lib dev; do $SBIN/debugfs -R "rdump /rdboot/$d $T" "$RD" > /dev/null 2>&1; done
cp "$MEM" "$T/bin/mem"
cp "$SWT" "$T/bin/swaptest"

P="$D/p1.img"
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/bin/* '$T'/avvioprova;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L sistema -d '$T' '$P' $((P1_SETT / 2))" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" $(( (2048 + P1_SETT + P2_SETT) / 2048 + 1 ))M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83, bootable\nstart=%d, size=%d, type=82\n' \
    $P1_SETT $((2048 + P1_SETT)) $P2_SETT | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$P" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
echo "=== $ARCH ==="
esito=0
ok() { if grep -aq "$1" "$S"; then echo "  [OK] $2"; else echo "  [NO] $2 (manca: $1)"; esito=1; fi; }

# --- 1. from the CD: the boot and the swap signature ---------------------------
EXOS_ISTANZA=sw1$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "cp /a/da/stage2.bin /a/boot/stage2.bin@3" "cp /a/da/kernel.bin /a/boot/kernel.bin@4" \
    "cp /a/da/rdboot.lz4 /a/boot/rdboot.lz4@4" "/a/avvioprova /a@10" \
    "umount /a@3" "mkswap -f hd0p2@5" > "$D/qemu1.log" 2>&1
grep -aq "^AVVIO installa 0" "$D/qemu1.log" || { echo "  [NO] l'installazione dell'avvio"; exit 1; }

# --- 2. from the disk: mem, swaptest, mem ----------------------------------------
EXOS_ISTANZA=sw2$ARCH EXOS_NO_FLOPPY=1 \
    timeout 1200 python3 tools/qemu_drive.py "dmesg SWAP@3" "mem@3" \
    "swaptest $CHIEDI@240" "mem@3" "mem -b@3" \
    "swaptest $META &@2" "swaptest $META@300" "sleep 30@35" "mem@3" "shutdown@10" > "$D/qemu2.log" 2>&1
S="$D/seriale2.txt"
cp "/tmp/exos/serialsw2$ARCH.txt" "$S" 2>/dev/null
cat "$D/qemu2.log" >> "$S"
grep -a "scambio (swap)\|mandate sul disco\|^swaptest" "$S" | sort -u | sed 's/^/  /' | head -20
ok "SWAP: 'hd0p2' attiva" "l'area si accende all'avvio"
ok "mandate sul disco dall'avvio: 0, tornate in RAM: 0" "mem prima: contatori a zero"
ok "le pagine sono tornate identiche" "swaptest: ogni pagina torna uguale"
n=$(grep -ac "le pagine sono tornate identiche" "/tmp/exos/serialsw2$ARCH.txt")
if [ "$n" -ge 3 ]; then echo "  [OK] due swaptest insieme: tutti e due giusti"
else echo "  [NO] due swaptest insieme: $n prove giuste su 3"; esito=1; fi
if grep -a "mandate sul disco dall'avvio" "$S" | grep -avq ": 0, tornate in RAM: 0"; then
    echo "  [OK] mem dopo: pagine andate e tornate"
else echo "  [NO] mem dopo: i contatori sono ancora a zero"; esito=1; fi
# and nobody died for lack of memory on the way (kbd did, at 64 bit, until
# the fault learned to wait for a page)
if grep -aq "RAM esaurita\|terminato per fault\|KERNEL PANIC" "$S"; then
    grep -a "RAM esaurita\|terminato per fault\|KERNEL PANIC" "$S" | sort -u | head -5 | sed 's/^/  /'
    echo "  [NO] qualcuno e' morto per la memoria"; esito=1
else echo "  [OK] nessun processo morto, nessun panico"; fi
exit $esito
