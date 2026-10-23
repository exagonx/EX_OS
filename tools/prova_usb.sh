#!/bin/bash
# =============================================================================
# tools/prova_usb.sh — il comando usb: -status, -unplug, -refresh, in QEMU
#
#     tools/prova_usb.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_usb.sh [dir]    64 bit
#
# Dal CD, con una chiavetta FAT32 su EHCI e un mouse su OHCI:
#   1. usb -status: il mouse, la chiavetta (usb0) e dove l'ha montata
#      automount;
#   2. usb -unplug usb0: smonta, ritira, «Puoi scollegare usb0»;
#   3. usb -status: la porta dice «tolta con -unplug, ancora infilata»;
#   4. usb -refresh: la chiavetta torna e automount la rimonta;
#   5. usb -unplug senza chiavette: lo dice.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/usb-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/chiave.img"
SBIN=/usr/sbin
P1_SETT=131072                                 # 64 MB: sotto, FAT32 ha troppo pochi cluster

if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso
    export EXOS_RAM=128M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }

qemu-img create -f raw "$IMG" 66M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=0c\n' $P1_SETT \
    | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
P="$D/p1.img"
dd if=/dev/zero of="$P" bs=512 count=$P1_SETT status=none
$SBIN/mkfs.fat -F 32 -n EXOSUSB "$P" > /dev/null
echo "ciao dalla chiavetta" > "$D/leggimi.txt"
mcopy -i "$P" "$D/leggimi.txt" ::/leggimi.txt
dd if="$P" of="$IMG" bs=512 seek=2048 conv=notrunc status=none

export EXOS_QEMU_EXTRA="$EXTRA -device usb-ehci,id=e -drive if=none,id=s,file=$IMG,format=raw \
-device usb-storage,bus=e.0,drive=s -device pci-ohci,id=o -device usb-mouse,bus=o.0"
EXOS_ISTANZA=ub1$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 900 python3 tools/qemu_drive.py "usb -status@12" \
    "usb -unplug usb0@4" "usb -status@3" "usb -refresh@3" "usb -status@3" \
    "usb -unplug usb0@4" "usb -unplug@3" > "$D/qemu.log" 2>&1

L="$D/qemu.log"
echo "=== $ARCH ==="
esito=0
prova() {   # prova <descrizione> <regex>
    if grep -aqE -e "$2" "$L"; then echo "  [OK] $1"; else echo "  [NO] $1"; esito=1; fi
}
prova "status: la chiavetta su ehci" "porta [0-9]+: memoria di massa"
prova "status: usb0 montata"         "-> usb0, /USB/USB[0-9]"
prova "status: il mouse su ohci"     "porta [0-9]+: mouse"
prova "unplug: smonta"               "smontato /USB/USB[0-9]"
prova "unplug: si puo' staccare"     "Puoi scollegare usb0"
prova "status: tolta e infilata"     "tolta con -unplug, ancora infilata"
prova "refresh: riguarda la porta"   "Riguardo 1 porta"
prova "refresh: torna montata"       "usb0 di nuovo presente: /USB/USB[0-9]"
prova "unplug senza chiavette"       "Non c'e' nessuna chiavetta da togliere"
n=$(grep -ac "Puoi scollegare usb0" "$L")
[ "$n" = 2 ] && echo "  [OK] la seconda volta si toglie di nuovo" \
    || { echo "  [NO] la seconda unplug ($n)"; esito=1; }
[ $esito = 0 ] || echo "  (vedi $L)"
exit $esito
