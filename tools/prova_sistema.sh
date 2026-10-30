#!/bin/bash
# =============================================================================
# tools/prova_sistema.sh — the system disk checked and remounted from the base
# system (@RDBOOT step 3), in QEMU
#
#     tools/prova_sistema.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_sistema.sh [dir]    64 bit
#
# The disk: one ext2 partition (the case that has the "not clean" mark), with
# /boot/stage2.bin, kernel.bin, rdboot.lz4 and kernel.cfg (no login: the
# commands are typed at a shell), and bin, lib, dev taken from rdboot.img.
#   1. from the CD: mount it, tools/prove/avvioprova.c installs the boot;
#   2. from the disk: the kernel takes the disk out from under /, the
#      "sistema" task puts it back; then QEMU is killed - the disk stays
#      marked "in use" (dumpe2fs: not clean);
#   3. from the disk again: found not clean, /rdboot/bin/chkdsk -r runs on
#      the unmounted partition, the disk goes back on /; `poweroff`;
#   4. after a poweroff the disk is marked clean;
#   5. e2fsck -fn finds nothing to fix (chkdsk -r included).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/sistema-$ARCH}"
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
# ! IN /da, E LI COPIA EX-OS IN /boot: mke2fs -d fa SPARSI i file con blocchi
# di zeri, e bootinstall un kernel sparso giustamente non lo mappa.
cp "$STAGE2" "$T/da/stage2.bin"
cp "$KERNEL" "$T/da/kernel.bin"
cp "$RDZ"    "$T/da/rdboot.lz4"
grep -v '^login *=' boot/kernel.cfg > "$T/boot/kernel.cfg"
for d in bin lib dev; do $SBIN/debugfs -R "rdump /rdboot/$d $T" "$RD" > /dev/null 2>&1; done

P="$D/p1.img"
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/bin/* '$T'/avvioprova;
    $SBIN/mke2fs -q -F -t ${EXOS_FS:-ext2} -b 1024 -m 0 -L sistema -d '$T' '$P' $((P1_SETT / 2))" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 66M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83, bootable\n' $P1_SETT \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$P" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

stato() {   # "clean" o "not clean"
    dd if="$IMG" of="$D/p.img" bs=512 skip=2048 count=$P1_SETT status=none
    $SBIN/dumpe2fs -h "$D/p.img" 2>/dev/null | sed -n 's/^Filesystem state: *//p'
}

export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
echo "=== $ARCH ==="
esito=0

# --- 1. dal CD: si installa l'avvio -----------------------------------------
EXOS_ISTANZA=si1$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "cp /a/da/stage2.bin /a/boot/stage2.bin@3" "cp /a/da/kernel.bin /a/boot/kernel.bin@4" \
    "cp /a/da/rdboot.lz4 /a/boot/rdboot.lz4@4" "/a/avvioprova /a@10" \
    "umount /a@3" > "$D/qemu1.log" 2>&1
grep -aq "^AVVIO installa 0" "$D/qemu1.log" || { echo "  [NO] l'installazione dell'avvio"; exit 1; }
s=$(stato); echo "  dopo il CD (montato, smontato): $s"
[ "$s" = clean ] || { echo "  [NO] smontato dal CD ma non pulito"; esito=1; }

# --- 2. dal disco, poi QEMU spento di colpo -----------------------------------
EXOS_ISTANZA=si2$ARCH EXOS_NO_FLOPPY=1 \
    timeout 600 python3 tools/qemu_drive.py "ls /boot@3" "ls /rdboot@3" > "$D/qemu2.log" 2>&1
cp "/tmp/exos/serialsi2$ARCH.txt" "$D/seriale2.txt" 2>/dev/null
grep -a "PASSO 13d\] il disco\|di nuovo la radice\|SISTEMA:" "$D/seriale2.txt" | sed 's/^/  /'
if grep -aq "si monta dopo l'avvio" "$D/seriale2.txt" &&
   grep -aq "di nuovo la radice" "$D/seriale2.txt" &&
   grep -aqi "kernel.bin" "$D/qemu2.log" && ! grep -aq "chkdsk -r" "$D/seriale2.txt"; then
    echo "  [OK] base prima, poi il disco su / (pulito: niente chkdsk)"
else
    echo "  [NO] il giro base -> disco (vedi $D/qemu2.log, seriale2.txt)"; esito=1
fi
s=$(stato); echo "  dopo uno spegnimento brusco: $s"
[ "$s" = "not clean" ] || { echo "  [NO] il disco doveva restare segnato in uso"; esito=1; }

# --- 3. dal disco: sporco -> chkdsk, poi poweroff -----------------------------
EXOS_ISTANZA=si3$ARCH EXOS_NO_FLOPPY=1 \
    timeout 600 python3 tools/qemu_drive.py "ls /boot@3" "poweroff@10" > "$D/qemu3.log" 2>&1
cp "/tmp/exos/serialsi3$ARCH.txt" "$D/seriale3.txt" 2>/dev/null
grep -a "smontato pulito\|SISTEMA:\|di nuovo la radice" "$D/seriale3.txt" | sed 's/^/  /'
if grep -aq "non e' stato smontato pulito" "$D/seriale3.txt" &&
   grep -aq "SISTEMA: chkdsk -r hd0p1 finito" "$D/seriale3.txt" &&
   grep -aq "di nuovo la radice" "$D/seriale3.txt" &&
   grep -aqi "kernel.bin" "$D/seriale3.txt"; then
    echo "  [OK] sporco: chkdsk -r sul disco smontato, poi il disco su /"
else
    echo "  [NO] il controllo automatico (vedi $D/qemu3.log, seriale3.txt)"; esito=1
fi

# --- 4. dopo poweroff: pulito --------------------------------------------------
s=$(stato); echo "  dopo poweroff: $s"
[ "$s" = clean ] && echo "  [OK] poweroff lascia il disco pulito" || { echo "  [NO] poweroff non ha segnato pulito"; esito=1; }

# --- 5. e2fsck: dopo chkdsk -r e i giri di EX-OS il volume e' sano ------------
# ! LA RIPARAZIONE AUTOMATICA DEVE LASCIARE UN VOLUME CHE e2fsck NON TOCCA: un
# blocco per crescere dichiarato libero (resize_inode) non lo vede nessun'altra
# riga di questa prova, e il primo file scritto dopo ci finirebbe sopra.
if $SBIN/e2fsck -fn "$D/p.img" > "$D/e2fsck.log" 2>&1; then
    echo "  [OK] e2fsck -fn: nessun errore"
else
    grep -v "^Pass\|^e2fsck " "$D/e2fsck.log" | head -6 | sed 's/^/  /'
    echo "  [NO] e2fsck trova errori (vedi $D/e2fsck.log)"; esito=1
fi
exit $esito
