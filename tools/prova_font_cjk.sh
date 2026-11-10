#!/bin/bash
# =============================================================================
# tools/prova_font_cjk.sh - netupdate -install:exilla brings [font-cjk] too
# (voce 6, netupdate 0.047), in QEMU
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_font_cjk.sh                  64 bit
#     EXOS_ARCH=i386 tools/prova_font_cjk.sh   32 bit
#
# Exilla wants [font-cjk], and both have their own list of files
# (pacchetti/exilla.txt, pacchetti/font-cjk.txt). Up to netupdate 0.046 only
# the list of the package asked for was downloaded, and the dependency came
# without files. On a disk installed from the CD:
#   -  netupdate -check -yes -noverifica (brings netupdate 0.047 itself);
#   1. netupdate -install:exilla -yes: the four .ttc and the licence are in
#      /exwin/font with the digests of pacchetti/font-cjk.txt;
#   2. the register has [font-cjk];
#   3. netupdate -verifica:exilla says "a posto";
#   4. with NotoSansCJK-Regular.ttc gone, it says -install:font-cjk.
# Served at 10.0.2.100 by tools/prova_netupdate_http.py.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
RADICE=$PWD
ARCH="${EXOS_ARCH:-x86_64}"
D="${1:-/tmp/exos-prove/font-cjk-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; Q=qemu-system-x86_64; EXTRA="-cpu qemu64"; REPO=dist/netinst_64
else
    ISO=dist/exos.iso;   Q=qemu-system-i386;   EXTRA="";            REPO=dist/netinst
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
[ -f "$REPO/pacchetti/font-cjk.txt" ] ||
    { echo "$REPO non ha [font-cjk]: tools/font-cjk/prepara.sh, poi make netinst / netinst64" >&2; exit 1; }
[ -w /dev/kvm ] && EXTRA="$EXTRA -enable-kvm"

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

echo "=== disco installato dal CD ($ARCH) ==="
IMG="$D/hd.img"
sed "s|dist/exos.iso|$ISO|g; s|/tmp/exos-mkhd.log|$D/mkhd.log|g" tools/mkhd.sh > "$D/mkhd.sh"
env -u EXOS_MARCA -u EXOS_NO_FLOPPY -u EXOS_QEMU_EXTRA -u EXOS_RAM \
    EXOS_QEMU=$Q EXOS_ISTANZA=fc$ARCH EXOS_SUPPORTO=cd EXOS_HD="$IMG" bash "$D/mkhd.sh" 1536 > "$D/install.log" 2>&1 || {
    no "l'installazione dal CD non e' riuscita (vedi $D/install.log)"
    echo "=== $ok riusciti, $no no ==="; exit 1; }

NOME=$(basename "$REPO")
mkdir -p "$D/srv"
# ! SYMBOLIC LINKS: /tmp is another filesystem (see prova_menu_netupdate.sh)
cp -as "$RADICE/$REPO" "$D/srv/$NOME" || exit 1
printf 'exec python3 %s/tools/prova_netupdate_http.py %s\n' "$RADICE" "$D/srv" > "$D/http.sh"
chmod +x "$D/http.sh"

EXOS_QEMU=$Q EXOS_ISTANZA=fc$ARCH EXOS_NO_FLOPPY=1 EXOS_RAM=1024M EXOS_MARCA="utente:" \
EXOS_QEMU_EXTRA="$EXTRA -drive file=$IMG,format=raw,if=ide -nic user,model=e1000,guestfwd=tcp:10.0.2.100:80-cmd:$D/http.sh" \
    timeout 5400 python3 tools/qemu_drive.py "root@2" "root@6" "keymap us@3" \
    "echo tipo = HTTP > /boot/netupdate.cnf@2" "echo url = 10.0.2.100/$NOME >> /boot/netupdate.cnf@2" \
    "netupdate -check -yes -noverifica@~1500" \
    "netupdate -version@3" \
    "echo INIZIO-INSTALLA@2" \
    "netupdate -install:exilla -yes@~3000" \
    "echo INIZIO-SHA@2" \
    "sha256sum /exwin/font/NotoSansCJK-Regular.ttc /exwin/font/NotoSansCJK-Bold.ttc /exwin/font/NotoSerifCJK-Regular.ttc /exwin/font/NotoSerifCJK-Bold.ttc /exwin/font/NotoCJK-LICENSE.txt@~120" \
    "echo INIZIO-REGISTRO@2" "grep font-cjk /boot/netupdate.reg@5" \
    "echo INIZIO-VERIFICA@2" "netupdate -verifica:exilla@~60" \
    "rm /exwin/font/NotoSansCJK-Regular.ttc@5" \
    "echo INIZIO-SENZA@2" "netupdate -verifica:exilla@~60" \
    "echo FINE-CJK@2" "sync@5" > "$D/giro.log" 2>&1
tr -d '\r' < "$D/giro.log" | sed 's/\x1b\[[0-9;]*m//g' | sed -n '/^=== seriale dal prompt/,$p' > "$D/giro.txt"
T="$D/giro.txt"
tratto() { sed -n "/> echo $1/,/> echo $2/p" "$T"; }

tratto INIZIO-INSTALLA INIZIO-SHA > "$D/installa.txt"
tratto INIZIO-SHA INIZIO-REGISTRO > "$D/sha.txt"
tratto INIZIO-REGISTRO INIZIO-VERIFICA > "$D/registro.txt"
tratto INIZIO-VERIFICA INIZIO-SENZA > "$D/verifica.txt"
tratto INIZIO-SENZA FINE-CJK > "$D/senza.txt"
grep -a "netupdate 0\.\|font-cjk\|Installo\|installat" "$T" | sed 's/^/      /' | head -12

giusti=0
while IFS='	' read -r p b h resto; do
    case "$p" in '#'*|'') continue ;; esac
    grep -aq "$h" "$D/sha.txt" && giusti=$((giusti+1))
done < "$REPO/pacchetti/font-cjk.txt"
[ "$giusti" = 5 ] && si "1. i 5 file di font-cjk in /exwin/font, con le impronte giuste" \
    || no "1. $giusti file di font-cjk su 5 con l'impronta giusta ($D/sha.txt, $D/installa.txt)"
grep -aq "^\[font-cjk\]" "$D/registro.txt" && si "2. [font-cjk] nel registro" \
    || no "2. [font-cjk] non e' nel registro ($D/registro.txt)"
grep -aq "exilla *a posto" "$D/verifica.txt" && si "3. la verifica di Exilla: a posto" \
    || no "3. la verifica di Exilla non dice a posto ($D/verifica.txt)"
grep -aq "install:font-cjk" "$D/senza.txt" && si "4. senza un font, la verifica dice -install:font-cjk" \
    || no "4. senza un font la verifica non lo dice ($D/senza.txt)"
grep -a "\[FAULT\]\|KERNEL PANIC" /tmp/exos/serialfc$ARCH.txt > /dev/null 2>&1 && no "un [FAULT] sulla seriale"
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
