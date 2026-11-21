#!/bin/bash
# =============================================================================
# tools/prova_riga_lunga.sh [32|64] - a typed line up to 32 KB (kernel 0.353,
# sh 0.014, kbd.drv), in QEMU
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# A line stopped at 255 characters over telnet (the pty), at 511 at the
# console (sh). Every line here writes a file on a test disk mounted on /a,
# and the files are read from the image after the machine stops: the bytes
# that arrived, not what the screen shows.
#   console  echo <1200 y> FINE > /a/c1.txt, typed key by key; then the
#            history: arrow up, five Backspace, "2.txt": /a/c2.txt the same
#   telnet   300, 4000, 20000 and 32000 x (tools/riga_lunga_prova.py), and
#            a line of 40000: cut at the limit, no file, the session lives
# One machine at a time: the telnet port of the host is fixed.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
TN_PORTA="${TN_PORTA:-2391}"
if ss -Hltn "sport = :$TN_PORTA" | grep -q .; then
    echo "NON RIUSCITO: la porta $TN_PORTA dell'host e' gia' occupata"
    exit 2
fi
BIT="${1:-32}"
case "$BIT" in
    64) iso=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M ;;
    *)  iso=dist/exos.iso;   export EXOS_RAM=128M ;;
esac
[ -f "$iso" ] || { echo "manca $iso" >&2; exit 1; }
D="${2:-/tmp/exos-prove/riga-lunga-$BIT}"
rm -rf "$D"; mkdir -p "$D"
IMG="$D/hd.img"
qemu-img create -f raw "$IMG" 40M > /dev/null
/usr/sbin/mke2fs -q -F -t ext2 -b 4096 "$IMG" || exit 1

log=/tmp/exos/serialrl$BIT.txt
rm -f "$log"
Y=$(printf 'y%.0s' $(seq 1 1200))
export EXOS_ISTANZA=rl$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$iso" EXOS_ATTESA_FINALE=400
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide -netdev user,id=n1,net=10.0.0.0/24,host=10.0.0.2,dhcpstart=10.0.0.15,hostfwd=tcp::$TN_PORTA-:23 -device ne2k_pci,netdev=n1"
timeout 900 python3 tools/qemu_drive.py "mount hd0 /a@5" \
    "echo $Y FINE > /a/c1.txt@4" \
    "mon:sendkey up" \
    "mon:sendkey backspace;sendkey backspace;sendkey backspace;sendkey backspace;sendkey backspace" \
    "2.txt@4" "sync@3" \
    "netdetect -c@12" "dhcp@8" "telnetd -s -v@420" > "$D/qemu.log" 2>&1 &
QPID=$!
timeout 880 python3 tools/riga_lunga_prova.py "$TN_PORTA" "$log" | tee "$D/telnet.txt"
rc=${PIPESTATUS[0]}
# ! QEMU IS ENDED BY ITS MONITOR PATH, not through qemu_drive: killed,
# qemu_drive leaves QEMU running with the image open and the port taken,
# and the monitor takes one client, qemu_drive itself
pkill -f "unix:/tmp/exos/monrl$BIT.sock" 2>/dev/null
sleep 2
wait $QPID 2>/dev/null
cp "$log" "$D/seriale.txt" 2>/dev/null

esito=$rc
si() { echo "  [OK] $*"; }
no() { echo "  [NO] $*"; esito=1; }
dump() { /usr/sbin/debugfs -R "dump $1 $2" "$IMG" > /dev/null 2>&1; }

for n in 300 4000 20000 32000; do
    rm -f "$D/t$n.txt"; dump "/t$n.txt" "$D/t$n.txt"
    atteso="$(printf 'x%.0s' $(seq 1 $n)) FINE"
    if [ -f "$D/t$n.txt" ] && [ "$(cat "$D/t$n.txt")" = "$atteso" ]; then
        si "telnet: la riga di $n x e' arrivata intera ($(stat -c %s "$D/t$n.txt") byte nel file)"
    else
        no "telnet: la riga di $n x non e' arrivata intera ($(stat -c %s "$D/t$n.txt" 2>/dev/null || echo 0) byte, vedi $D)"
    fi
done
rm -f "$D/t40000.txt"; dump /t40000.txt "$D/t40000.txt"
[ -f "$D/t40000.txt" ] && no "telnet: la riga di 40000 ha scritto un file: non e' stata tagliata" \
                       || si "telnet: la riga di 40000 e' tagliata al limite, nessun file"
for f in c1 c2; do
    rm -f "$D/$f.txt"; dump "/$f.txt" "$D/$f.txt"
    if [ -f "$D/$f.txt" ] && [ "$(cat "$D/$f.txt")" = "$Y FINE" ]; then
        si "console: /a/$f.txt ha la riga di 1200 y intera"
    else
        no "console: /a/$f.txt non ha la riga di 1200 y ($(stat -c %s "$D/$f.txt" 2>/dev/null || echo 0) byte)"
    fi
done
grep -aq '\[FAULT\]' "$D/seriale.txt" && no "un [FAULT] sulla seriale" || si "nessun [FAULT]"
exit $esito
