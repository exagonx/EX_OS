#!/bin/bash
# =============================================================================
# tools/prova_system.sh — system -unload / -load (@RDBOOT step 5), in QEMU
#
#     tools/prova_system.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_system.sh [dir]    64 bit
#
# The disk of tools/prova_sistema.sh (one ext2 partition with the boot, the
# base copied in bin/lib/dev, no login) plus tools/prove/usoprova.c in /da.
#   1. from the CD: the boot is installed;
#   2. from the disk: usoprova (a program ON THE DISK) holds /boot/tienimi
#      open, the shell sits in /boot; `system -unload -yes` must close it,
#      unmount the disk, and leave the shell in "/" (the base has no /boot);
#      `system -status` says base; `system -load` mounts it back and runs
#      the configuration; `ls /boot` sees the disk again; poweroff;
#   3. e2fsck -fn finds nothing to fix.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/system-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
P1_SETT=131072                                 # 64 MB

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
for f in "$ISO" "$KERNEL" "$STAGE2" "$RD" "$RDZ"; do [ -f "$f" ] || { echo "manca $f" >&2; exit 1; }; done

T="$D/albero"
mkdir -p "$T/boot" "$T/da"
$GCC -O2 tools/prove/avvioprova.c -o "$T/avvioprova" || exit 1
$GCC -O2 tools/prove/usoprova.c -o "$T/da/usoprova" || exit 1
cp "$STAGE2" "$T/da/stage2.bin"
cp "$KERNEL" "$T/da/kernel.bin"
cp "$RDZ"    "$T/da/rdboot.lz4"
grep -v '^login *=' boot/kernel.cfg > "$T/boot/kernel.cfg"
for d in bin lib dev; do $SBIN/debugfs -R "rdump /rdboot/$d $T" "$RD" > /dev/null 2>&1; done

P="$D/p1.img"
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/bin/* '$T'/avvioprova '$T'/da/usoprova;
    $SBIN/mke2fs -q -F -t ext2 -b 1024 -m 0 -L sistema -d '$T' '$P' $((P1_SETT / 2))" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 66M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83, bootable\n' $P1_SETT \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$P" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
echo "=== $ARCH ==="
esito=0
ok() { if grep -aq "$1" "$S"; then echo "  [OK] $2"; else echo "  [NO] $2 (manca: $1)"; esito=1; fi; }

# --- 1. dal CD: l'avvio --------------------------------------------------------
EXOS_ISTANZA=sy1$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "cp /a/da/stage2.bin /a/boot/stage2.bin@3" "cp /a/da/kernel.bin /a/boot/kernel.bin@4" \
    "cp /a/da/rdboot.lz4 /a/boot/rdboot.lz4@4" "/a/avvioprova /a@10" \
    "umount /a@3" > "$D/qemu1.log" 2>&1
grep -aq "^AVVIO installa 0" "$D/qemu1.log" || { echo "  [NO] l'installazione dell'avvio"; exit 1; }

# --- 2. dal disco: unload e load ------------------------------------------------
EXOS_ISTANZA=sy2$ARCH EXOS_NO_FLOPPY=1 \
    timeout 900 python3 tools/qemu_drive.py "system -status@3" \
    "/da/usoprova /boot/tienimi /rdboot/tmp/k.sh &@3" "cd /boot@2" \
    "ps -m /@3" "system -unload -yes@20" "system -status@3" "pwd@2" "ls /@3" \
    "system -load@20" "system -status@3" "ls /boot@3" "poweroff@10" > "$D/qemu2.log" 2>&1
S="$D/seriale2.txt"
cp "/tmp/exos/serialsy2$ARCH.txt" "$S" 2>/dev/null
cat "$D/qemu2.log" >> "$S"
grep -a "^system\|Fatto\|non si smonta\|e' il disco\|e' il sistema di base\|usoprova" "$S" | sort -u | sed 's/^/  /' | head -20
ok "/ e' il disco di sistema (hd0p1)" "-status col disco"
ok "usoprova" "un programma del disco lo tiene"
ok "Fatto: hd0p1 non e' piu' montato" "-unload chiude usoprova e smonta"
ok "/ e' il sistema di base: il disco" "-status senza disco"
if [ "$(grep -a -A1 '> pwd' "$S" | sed -n 2p | tr -d '\r')" = "/" ]; then
    echo "  [OK] la shell che stava in /boot e' finita in /"
else echo "  [NO] la shell che stava in /boot e' finita in /"; esito=1; fi
ok "hd0p1 di nuovo su /" "-load lo rimonta"
ok "kernel.bin" "ls /boot vede di nuovo il disco"

# --- 3. e2fsck ---------------------------------------------------------------------
dd if="$IMG" of="$D/p.img" bs=512 skip=2048 count=$P1_SETT status=none
if $SBIN/e2fsck -fn "$D/p.img" > "$D/e2fsck.log" 2>&1; then
    echo "  [OK] e2fsck -fn: nessun errore"
else
    grep -v "^Pass\|^e2fsck " "$D/e2fsck.log" | head -6 | sed 's/^/  /'
    echo "  [NO] e2fsck trova errori (vedi $D/e2fsck.log)"; esito=1
fi
exit $esito
