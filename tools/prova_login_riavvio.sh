#!/bin/bash
# =============================================================================
# tools/prova_login_riavvio.sh - the keyboard at the login after a reboot
#
#     tools/prova_login_riavvio.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_login_riavvio.sh [dir]    64 bit
#
# System disk ext4 with `login = /bin/login`, QEMU WITHOUT -no-reboot: login,
# reboot, and at the second login the same keys must get in. Written for the
# laptop's report of 1 November 2026 (login frozen after a reboot, telnet fine).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/login-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"; DATI="$D/dati.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
P1_SETT=524288                                 # 256 MB, ExWin included

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    KERNEL=build-64/kernel-intero.bin; STAGE2=build-64/avvio/stage2-cd.bin
    RD=build-64/rdboot.img; RDZ=build-64/rdboot.lz4
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
    ALBERO=build-64/iso-exos
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    KERNEL=build/kernel.bin; STAGE2=build/stage2.bin; RD=build/rdboot.img; RDZ=build/rdboot.lz4
    export EXOS_RAM=128M EXTRA=""
    ALBERO=build/iso-exos
fi
for f in "$ISO" "$KERNEL" "$STAGE2" "$RD" "$RDZ"; do [ -f "$f" ] || { echo "manca $f" >&2; exit 1; }; done

T="$D/albero"
mkdir -p "$T/boot" "$T/da"
$GCC -O2 tools/prove/avvioprova.c -o "$T/avvioprova" || exit 1
cp "$STAGE2" "$T/da/stage2.bin"
cp "$KERNEL" "$T/da/kernel.bin"
cp "$RDZ"    "$T/da/rdboot.lz4"
cp boot/kernel.cfg "$T/boot/kernel.cfg"
[ -f boot/avvio.sh ] && cp boot/avvio.sh "$T/boot/avvio.sh"
for d in bin lib dev; do $SBIN/debugfs -R "rdump /rdboot/$d $T" "$RD" > /dev/null 2>&1; done
# The desktop is not in the base system: it comes from the tree of the CD.
for d in bin lib dev exwin; do [ -d "$ALBERO/$d" ] && cp -r "$ALBERO/$d" "$T/"; done

P="$D/p1.img"
fakeroot sh -c "chown -R 0:0 '$T'; chmod 755 '$T'/bin/* '$T'/avvioprova;
    $SBIN/mke2fs -q -F -t ext4 -b 1024 -m 0 -L sistema -d '$T' '$P' $((P1_SETT / 2))" \
    > "$D/mke2fs.log" 2>&1 || { cat "$D/mke2fs.log"; exit 1; }
qemu-img create -f raw "$IMG" 260M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83, bootable\n' $P1_SETT \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$P" of="$IMG" bs=512 seek=2048 conv=notrunc status=none
$SBIN/mke2fs -q -F -t ext4 -b 1024 -m 0 -L dati "$DATI" 32768 > /dev/null 2>&1

esito=0
echo "=== $ARCH ==="

EXOS_ISTANZA=sm0$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
    timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "cp /a/da/stage2.bin /a/boot/stage2.bin@3" "cp /a/da/kernel.bin /a/boot/kernel.bin@4" \
    "cp /a/da/rdboot.lz4 /a/boot/rdboot.lz4@4" "/a/avvioprova /a@10" \
    "umount /a@3" > "$D/qemu0.log" 2>&1
grep -aq "^AVVIO installa 0" "$D/qemu0.log" || { echo "  [NO] l'installazione dell'avvio"; exit 1; }


EXOS_MARCA="nome utente:" EXOS_RIAVVIO=1 EXOS_ISTANZA=lg$ARCH EXOS_NO_FLOPPY=1 \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide" \
    timeout 300 python3 tools/qemu_drive.py \
    "prova@2" "sale12345@2" "sale12345@4" "prova@2" "sale12345@8" "echo PRIMOX@3" "reboot@70" \
    "prova@2" "sale12345@8" "echo SECONDOX@3" > "$D/qemu.log" 2>&1
tr -d '\000' < "/tmp/exos/seriallg$ARCH.txt" > "$D/seriale.txt"
for m in PRIMOX SECONDOX; do
    if grep -aq "^$m" "$D/seriale.txt" "$D/qemu.log"; then echo "  [OK] $m"
    else echo "  [NO] $m non e' arrivato"; esito=1; fi
done
[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
