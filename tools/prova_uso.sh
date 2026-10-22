#!/bin/bash
# =============================================================================
# tools/prova_uso.sh — who uses a mount: ps, ps -m, kill, umount (@RDBOOT
# step 4), in QEMU
#
#     tools/prova_uso.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_uso.sh [dir]    64 bit
#
# From the CD, with a FAT32 disk holding tools/prove/usoprova.c:
#   1. mount hd0p1 /a; /a/usoprova keeps /a/tieni.txt open in the background
#      and writes /rdboot/tmp/uccidi.sh = "kill <its pid>";
#   2. ps lists it, ps -m /a says why it uses /a (the program, one open file);
#   3. umount /a fails and names it;
#   4. the script kills it with /bin/kill; umount works;
#   5. kill of a pid that does not exist says so.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/uso-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
SBIN=/usr/sbin
X=cross_build/nasi5deb
P1_SETT=131072                                 # 64 MB

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; GCC=$X/exos-cross64/bin/x86_64-exos-gcc
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; GCC=$X/exos-cross/bin/i386-exos-gcc
    export EXOS_RAM=128M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
$GCC -O2 tools/prove/usoprova.c -o "$D/usoprova" || exit 1

qemu-img create -f raw "$IMG" 66M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=0c\n' $P1_SETT \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
P="$D/p1.img"
dd if=/dev/zero of="$P" bs=512 count=$P1_SETT status=none
$SBIN/mkfs.fat -F 32 -n EXOSUSO "$P" > /dev/null
mcopy -i "$P" "$D/usoprova" ::/usoprova
dd if="$P" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

export EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide"
EXOS_ISTANZA=us1$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 900 python3 tools/qemu_drive.py "mount hd0p1 /a@5" \
    "/a/usoprova /a/tieni.txt /rdboot/tmp/uccidi.sh &@4" \
    "ps@3" "ps -m /a@3" "umount /a@3" \
    "sh /rdboot/tmp/uccidi.sh@4" "ps -m /a@3" "umount /a@3" \
    "kill 9999@3" > "$D/qemu.log" 2>&1

L="$D/qemu.log"
echo "=== $ARCH ==="
esito=0
pid=$(grep -ao "USO pid [0-9]*" "$L" | head -1 | awk '{print $3}')
[ -n "$pid" ] || { echo "  [NO] usoprova non e' partito (vedi $L)"; exit 1; }
echo "  usoprova: pid $pid"
grep -aE "^ *$pid +[0-9]+ .*usoprova" "$L" | head -1 | sed 's/^/  ps: /'
if grep -aqE "^ *$pid +[0-9]+ .*usoprova" "$L"; then
    echo "  [OK] ps lo elenca"
else
    echo "  [NO] ps non lo elenca"; esito=1
fi
if grep -aqE "^$pid +[^ ]+ +/a/usoprova +il programma 1 file aperto" "$L"; then
    echo "  [OK] ps -m /a: il programma e 1 file aperto"
else
    echo "  [NO] ps -m /a non dice perche' (vedi $L)"; esito=1
fi
if grep -aq "lo usano:" "$L" && grep -aqE "^ +$pid +/a/usoprova" "$L"; then
    echo "  [OK] umount /a occupato: lo nomina"
else
    echo "  [NO] umount non dice chi usa /a"; esito=1
fi
# Subito dopo kill puo' restare uno zombie (tiene il programma finche' la
# shell non lo raccoglie): ps -m lo dice; conta che umount poi riesca.
if grep -aq "^smontato /a" "$L"; then
    echo "  [OK] kill lo chiude, poi umount /a riesce"
else
    echo "  [NO] dopo kill /a resta occupato"; esito=1
fi
grep -aq "kill: nessun processo 9999" "$L" && echo "  [OK] kill 9999: nessun processo" \
    || { echo "  [NO] kill di un pid inesistente"; esito=1; }
exit $esito
