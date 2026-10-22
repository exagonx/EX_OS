#!/bin/bash
# =============================================================================
# tools/prova_rdboot.sh — the base system archive reaches RAM, in QEMU (@RDBOOT)
#
#     tools/prova_rdboot.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_rdboot.sh [dir]    64 bit
#
# The disk: one FAT32 partition, filled from outside with the system (the
# contents of rdboot.img itself, at the root), /BOOT/STAGE2.BIN,
# /BOOT/KERNEL.BIN and /BOOT/RDBOOT.IMG. FAT and not ext2 on purpose: a FAT
# root has no login, and the boot map on FAT is the same as on ext2 for what
# is tested here (ext2 is the laptop's, step 6).
#   1. from the CD: mount the disk, tools/prove/avvioprova.c installs the boot
#      (bootinstall writes the rdboot map into Stage 2);
#   2. from the disk: Stage 2 loads the archive, the kernel expands it,
#      registers it as rdb0 and mounts its /rdboot on /rdboot (step 2);
#   3. the shell from /rdboot (kernel.cfg changed on the disk): `umount /`
#      leaves the base as the root, `mount hd0p1 /` puts the disk back;
#   4. one byte spoiled in the .lz4: noticed, and the system boots without.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/rdboot-$ARCH}"
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
$GCC -O2 tools/prove/avvioprova.c -o "$D/avvioprova" || exit 1

qemu-img create -f raw "$IMG" 66M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=0c\n' $P1_SETT \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
P="$D/p1.img"
dd if=/dev/zero of="$P" bs=512 count=$P1_SETT status=none
$SBIN/mkfs.fat -F 32 -n EXOSRD "$P" > /dev/null
mmd -i "$P" ::/boot
mcopy -i "$P" "$STAGE2" ::/boot/STAGE2.BIN
mcopy -i "$P" "$KERNEL" ::/boot/KERNEL.BIN
mcopy -i "$P" "$RDZ"    ::/boot/RDBOOT.LZ4
mcopy -i "$P" boot/kernel.cfg ::/boot/KERNEL.CFG
for d in bin lib dev; do $SBIN/debugfs -R "rdump /rdboot/$d $D" "$RD" > /dev/null 2>&1; done
mcopy -s -i "$P" "$D/bin" "$D/lib" "$D/dev" ::/ 2>/dev/null
mcopy -i "$P" "$D/avvioprova" ::/avvioprova
dd if="$P" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"

# --- 1. dal CD: si installa l'avvio -----------------------------------------
EXOS_ISTANZA=rb1$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" "/a/avvioprova /a@10" \
    "umount /a@3" > "$D/qemu1.log" 2>&1

# --- 2. dal disco: l'archivio dev'essere in RAM -----------------------------
EXOS_ISTANZA=rb2$ARCH EXOS_NO_FLOPPY=1 \
    timeout 600 python3 tools/qemu_drive.py "ls /rdboot/bin@3" "/rdboot/bin/ls /rdboot/dev@3" \
    > "$D/qemu2.log" 2>&1

# Stage 2 e l'avvio del kernel stanno nella seriale INTERA, non in quella
# «dal prompt in poi» che qemu_drive mette nel registro.
cp "/tmp/exos/serialrb2$ARCH.txt" "$D/seriale2.txt" 2>/dev/null
cat "$D/seriale2.txt" >> "$D/qemu2.log"
echo "=== $ARCH ==="
grep -a "^AVVIO" "$D/qemu1.log" | sed 's/^/  /'
grep -a "Sistema di base\|BLK: volume in RAM\|RD: \|VFS: sistema" "$D/seriale2.txt" | sed 's/^/  /' | head -8
esito=0
grep -aq "^AVVIO installa 0" "$D/qemu1.log" || { echo "  [NO] l'installazione dell'avvio"; esito=1; }
grep -aq "Sistema di base in RAM" "$D/qemu2.log" || { echo "  [NO] Stage 2 non ha caricato l'archivio"; esito=1; }
grep -aq "BLK: volume in RAM" "$D/qemu2.log" || { echo "  [NO] il kernel non ha registrato rdb0"; esito=1; }
grep -aq "VFS: sistema di base su /rdboot" "$D/qemu2.log" || { echo "  [NO] /rdboot non montato"; esito=1; }
grep -aq "volume in RAM ROTTO" "$D/qemu2.log" && { echo "  [NO] somma EXRD sbagliata"; esito=1; }
grep -aq "RD: rdboot decompresso" "$D/qemu2.log" || { echo "  [NO] il kernel non ha decompresso rdboot.lz4"; esito=1; }
grep -aqi "login" "$D/qemu2.log" || { echo "  [NO] /r/rdboot/bin non elenca login"; esito=1; }
grep -aqi "svga" "$D/qemu2.log" || { echo "  [NO] /r/rdboot/dev non elenca svga.drv"; esito=1; }
[ $esito = 0 ] && echo "  [OK] archivio caricato, decompresso, montato su /rdboot"

# --- 3. il disco si toglie da sotto / e torna -------------------------------
# La shell e il driver della tastiera vengono da /rdboot: sul disco non resta
# niente di aperto, e `umount /` deve riuscire.
sed -e 's|^shell *=.*|shell       = /rdboot/bin/sh|' \
    -e 's|^kbd *=.*|kbd         = /rdboot/dev/kbd.drv|' \
    -e 's|^PATH *=.*|PATH        = /rdboot/bin:/bin|' boot/kernel.cfg > "$D/kernel-base.cfg"
mcopy -o -i "$IMG@@1M" "$D/kernel-base.cfg" ::/boot/KERNEL.CFG
EXOS_ISTANZA=rb4$ARCH EXOS_NO_FLOPPY=1 \
    timeout 600 python3 tools/qemu_drive.py "umount /@4" "ls /@3" "ls /rdboot/dev@3" \
    "mount hd0p1 /@5" "ls /boot@3" > "$D/qemu4.log" 2>&1
cp "/tmp/exos/serialrb4$ARCH.txt" "$D/seriale4.txt" 2>/dev/null
mcopy -o -i "$IMG@@1M" boot/kernel.cfg ::/boot/KERNEL.CFG
if grep -aq "la radice e' il sistema di base" "$D/seriale4.txt" &&
   grep -aq "di nuovo la radice" "$D/seriale4.txt" &&
   grep -aqi "kernel.bin" "$D/qemu4.log"; then
    echo "  [OK] umount / lascia la base come radice, mount hd0p1 / rimette il disco"
else
    echo "  [NO] il giro umount / e mount / (vedi $D/qemu4.log, seriale4.txt)"; esito=1
fi

# --- 3. un byte rovinato nel file compresso: si parte SENZA, e lo si dice ----
python3 - "$IMG" <<'PY'
import struct, sys
b = bytearray(open(sys.argv[1], 'rb').read())
# ! LA FIRMA C'E' ANCHE DENTRO IL KERNEL (e' una costante di rdlz4.c): si
# cerca quella seguita dalla misura del volume, 4 MB.
i = b.find(b'EXLZ')
while i >= 0 and struct.unpack('<I', b[i + 4:i + 8])[0] != 4 * 1024 * 1024:
    i = b.find(b'EXLZ', i + 1)
comp = struct.unpack('<I', b[i + 8:i + 12])[0]
b[i + 16 + comp // 2] ^= 0x5A
open(sys.argv[1], 'wb').write(b)
PY
EXOS_ISTANZA=rb3$ARCH EXOS_NO_FLOPPY=1 \
    timeout 600 python3 tools/qemu_drive.py "ls /boot@3" > "$D/qemu3.log" 2>&1
cp "/tmp/exos/serialrb3$ARCH.txt" "$D/seriale3.txt" 2>/dev/null
if grep -aq "rdboot compresso ROTTO" "$D/seriale3.txt" && grep -aqi "kernel.bin" "$D/qemu3.log"; then
    echo "  [OK] rovinato: riconosciuto, e il sistema parte senza"
else
    echo "  [NO] il file rovinato non e' stato riconosciuto o l'avvio si e' fermato"; esito=1
fi
exit $esito
