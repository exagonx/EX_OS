#!/bin/bash
# =============================================================================
# tools/prova_schede_rete.sh — the rings of the network drivers (voce 41)
#
#     tools/prova_schede_rete.sh                 32 bit
#     EXOS_ARCH=x86_64 tools/prova_schede_rete.sh  64 bit
#     SCHEDE="pcnet" tools/prova_schede_rete.sh    only some cards
#
# For every card QEMU has a driver for (e1000, pcnet, ne2k_pci): boots the CD
# on that card (the CD starts the network and dhcp by itself), downloads 16 MB of random bytes from a server
# on the host onto a disk and compares sha256. Says the rings the driver chose.
# nforce, rtl8169, sis900 and ar5k cannot be tried here: QEMU has no such card.
#
# Wants dist/exos.iso / dist/exos64.iso (make iso-exos / make iso64).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/schede-$ARCH}"
SCHEDE="${SCHEDE:-e1000 pcnet ne2k_pci}"
PORTA="${PORTA:-18090}"
SBIN=/usr/sbin
mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=1024M EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; export EXOS_RAM=512M EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }

head -c 16M /dev/urandom > "$D/dati.bin"
ATTESO=$(sha256sum "$D/dati.bin" | cut -d' ' -f1)
python3 -I -m http.server "$PORTA" --bind 0.0.0.0 --directory "$D" > "$D/http.log" 2>&1 &
P_HTTP=$!
trap 'kill $P_HTTP 2>/dev/null' EXIT
sleep 1

esito=0
for m in $SCHEDE; do
    IMG="$D/disco-$m.img"
    rm -f "$IMG"
    qemu-img create -f raw "$IMG" 40M > /dev/null
    printf 'label: dos\nunit: sectors\n\nstart=2048, size=79872, type=83\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
    I="sc$ARCH$(echo "$m" | tr -d '_0-9')"
    EXOS_ISTANZA=$I EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -nic user,model=$m" \
        timeout 500 python3 tools/qemu_drive.py \
        "mkfs -t ext2 -L prova hd0p1@4" "si@25" "mount hd0p1 /a@6" \
        "scarica http://10.0.2.2:$PORTA/dati.bin /a/dati.bin@~300" \
        "sha256sum /a/dati.bin@60" "ipcfg@4" "umount /a@6" > "$D/qemu-$m.log" 2>&1
    S="$D/seriale-$m.txt"
    tr -d '\r' < "/tmp/exos/serial$I.txt" > "$S" 2>/dev/null
    echo "=== $ARCH $m ==="
    grep -a -i -m2 'anelli \|coda [0-9]* posti' "$S" | sed 's/^ */  /'
    grep -a 'scarica: ' "$S" | sed 's/^/  /'
    if grep -aq "KERNEL PANIC\|terminato per fault" "$S"; then
        grep -a "KERNEL PANIC\|terminato per fault" "$S" | head -3 | sed 's/^/  /'; esito=1
    fi
    if grep -aq "$ATTESO" "$S"; then echo "  [ok] 16 MB arrivati, sha256 giusto"
    else echo "  [NO] sha256 assente o sbagliato ($S)"; esito=1; fi
done
[ $esito -eq 0 ] && echo "TUTTO A POSTO" || echo "CI SONO DIFETTI"
exit $esito
