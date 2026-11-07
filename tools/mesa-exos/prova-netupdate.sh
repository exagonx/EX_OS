#!/bin/bash
# =============================================================================
# tools/mesa-exos/prova-netupdate.sh - Mesa through netupdate, in QEMU (voce 88)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/mesa-exos/prova-netupdate.sh                  64 bit
#     EXOS_ARCH=i386 tools/mesa-exos/prova-netupdate.sh   32 bit
#
# What a user does: a disk installed from the CD, then `netupdate
# -install:mesa -yes` from the repository (dist/netinst_64 or dist/netinst,
# made by make netinst / make ARCH=x86_64 netinst64), served at 10.0.2.100
# by tools/prova_netupdate_http.py as in tools/prova_verifiche.sh.
#   1. the group arrives with zlib, and its check (verifiche/mesa.sh) runs
#      provaegl: "mesa  a posto";
#   2. /exos/bin/provaegl, started by hand on the installed disk: 0 NO;
#   3. libz.so.1 taken away: -verifica:mesa says what is missing and that
#      it is to be done by hand.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE=$PWD
ARCH="${EXOS_ARCH:-x86_64}"
D="${1:-/tmp/exos-prove/mesa-netupdate-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; Q=qemu-system-x86_64; EXTRA="-cpu qemu64"; REPO=dist/netinst_64
else
    ISO=dist/exos.iso;   Q=qemu-system-i386;   EXTRA="";            REPO=dist/netinst
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
grep -q '^\[mesa\]' "$REPO/catalogo.txt" 2>/dev/null || { echo "$REPO non ha il pacchetto mesa: make netinst" >&2; exit 1; }
[ -f "$REPO/verifiche/mesa.sh" ] || { echo "$REPO non ha verifiche/mesa.sh" >&2; exit 1; }
[ -w /dev/kvm ] && EXTRA="$EXTRA -enable-kvm"

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

echo "=== disco installato dal CD ($ARCH) ==="
IMG="$D/hd.img"
sed "s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
# 512 MB: the group with its sources is about 90 MB
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=me$ARCH EXOS_SUPPORTO=cd EXOS_HD="$IMG" bash "$D/mkhd.sh" 512 > "$D/install.log" 2>&1 || {
    no "l'installazione dal CD non e' riuscita (vedi $D/install.log)"
    echo "=== $ok riusciti, $no no ==="; exit 1; }

# the server: a copy of the repository made of links (see prova_verifiche.sh)
NOME=$(basename "$REPO")
mkdir -p "$D/srv"
cp -al "$RADICE/$REPO" "$D/srv/$NOME" 2>/dev/null || {
    rm -rf "$D/srv/$NOME"; cp -as "$RADICE/$REPO" "$D/srv/$NOME"; } || {
    echo "non riesco a copiare $REPO in $D/srv" >&2; exit 1; }
printf 'exec python3 %s/tools/prova_netupdate_http.py %s\n' "$RADICE" "$D/srv" > "$D/http.sh"
chmod +x "$D/http.sh"

EXOS_QEMU=$Q EXOS_ISTANZA=me$ARCH EXOS_NO_FLOPPY=1 EXOS_RAM=512M EXOS_MARCA="utente:" \
EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -nic user,model=e1000,guestfwd=tcp:10.0.2.100:80-cmd:$D/http.sh" \
    timeout 2400 python3 tools/qemu_drive.py "root@2" "root@6" "keymap us@3" \
    "echo tipo = HTTP > /boot/netupdate.cnf@2" "echo url = 10.0.2.100/netinst >> /boot/netupdate.cnf@2" \
    "netupdate -install:mesa -yes@~900" \
    "/exos/bin/provaegl@~60" \
    "rm /exos/lib/libz.so.1@4" \
    "netupdate -verifica:mesa@~120" \
    "sync@5" > "$D/giro.log" 2>&1
tr -d '\r' < "$D/giro.log" | sed 's/\x1b\[[0-9;]*m//g' | sed -n '/^=== seriale dal prompt/,$p' > "$D/giro.txt"
T="$D/giro.txt"

sed -n '/> netupdate -install:mesa/,/> \/exos\/bin\/provaegl/p' "$T" > "$D/1.txt"
grep -aq -- "-- mesa.sh (mesa)" "$D/1.txt" && grep -aq "^  mesa *a posto" "$D/1.txt" \
    && si "1. -install:mesa: la verifica lancia provaegl e dice 'a posto'" || no "1. dopo -install:mesa niente 'mesa a posto' ($D/1.txt)"
sed -n '/> \/exos\/bin\/provaegl/,/> rm /p' "$T" > "$D/2.txt"
grep -a "\[OK\]\|\[NO\]\|softpipe" "$D/2.txt" | sed 's/^/      /'
grep -aq "prova-egl: 0 NO" "$D/2.txt" \
    && si "2. provaegl dal disco installato: 0 NO" || no "2. provaegl dal disco ($D/2.txt)"
sed -n '/> netupdate -verifica:mesa/,/> sync/p' "$T" > "$D/3.txt"
grep -aq "Mesa vuole /exos/lib/libz.so.1" "$D/3.txt" && grep -aq "^  mesa *C'E' DA FARE A MANO" "$D/3.txt" \
    && si "3. senza libz.so.1 la verifica lo dice, da fare a mano" || no "3. senza libz la verifica non lo dice ($D/3.txt)"
grep -a "\[FAULT\]\|KERNEL PANIC" /tmp/exos/serialme$ARCH.txt > /dev/null 2>&1 && no "un [FAULT] sulla seriale"
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
