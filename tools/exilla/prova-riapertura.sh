#!/bin/bash
# =============================================================================
# tools/exilla/prova-riapertura.sh - Firefox: first start, close, second start
# (voce 70)
#
#     tools/exilla/prova-riapertura.sh                       32 bit
#     EXOS_ARCH=x86_64 tools/exilla/prova-riapertura.sh      64 bit
#
# The laptop's report of 1 November 2026: the profile is created, Firefox is
# closed, and at the second start it no longer works. A fresh ext4 disk with
# 4 KB blocks like the laptop's, the Firefox that is packaged NOW, an empty
# profile. First start, Ctrl+Q, second start on the same profile: each one
# must show the yellow page (file:///disk/prova.html), the process must live
# while open and be gone after Ctrl+Q (exit code 0), and the profile must
# have prefs.js.
# Photos, log and serial in cross_build/<macchina>/costruzione-prova[64]/riapertura/.
#
# ! UNTIL 17/11/2026 THIS WAS NOT A TEST. It took the disk tree left by
# prova-gecko.sh on 9/11, whose Firefox was linked before kernel 0.339 (the
# old stat: "va ricollegato alla libc nuova", exit 255 at both starts); on
# 2/11 it stopped on "Profile Missing". Now the tree is made here, every run.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
R="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "$EXOS_ARCH" = x86_64 ]; then
    SUFF=64; CD="$R/dist/exos64.iso"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
else
    SUFF="";  CD="$R/dist/exos.iso";   CPU=""
fi
O="$R/cross_build/exilla-obj/gecko$SUFF"; P="$O/dist/firefox"
D="$R/cross_build/$MACCHINA/costruzione-prova$SUFF"; OUT="$D/riapertura"
G="$D/riapertura-disco"; IMG="$D/riapertura-hd.img"; PART="$D/riapertura-part.img"
mkdir -p "$D" /tmp/exos; rm -rf "$OUT"; mkdir -p "$OUT"

# the package must hold what dist/bin holds (see prova-gecko.sh)
if [ ! -x "$P/firefox" ] || [ -n "$(find -L "$O/dist/bin" -newer "$P/firefox" -type f -print -quit 2>/dev/null)" ]; then
    tools/exilla/gecko-costruisci.sh package > "$D/gecko-package.log" 2>&1 || {
        echo "mach package fallito: $D/gecko-package.log" >&2; exit 1; }
fi

rm -rf "$G"; mkdir -p "$G/font" "$G/casa" "$G/profilo"
cp -a "$P" "$G/firefox"; cp exwin/font/*.ttf "$G/font/"
# ! NO PRIVACY NOTICE TAB (test copy only): it takes the focus and covers the
# page on a first start (see prova-primotab.sh)
# nor the "default browser?" dialog, which dims the page at the second start
printf 'pref("datareporting.policy.firstRunURL", "");\npref("browser.shell.checkDefaultBrowser", false);\n' \
    > "$G/firefox/defaults/pref/riapertura.js"
printf '<!DOCTYPE html><html><head><meta charset="utf-8"><title>Prova</title></head><body style="background:#fff3c4"><h1>RIAPERTURA</h1></body></html>' > "$G/prova.html"
MB=$(( $(du -sm "$G" | cut -f1) * 13 / 10 + 256 ))
rm -f "$IMG" "$PART"
fakeroot sh -c "chown -R 0:0 '$G' && /usr/sbin/mke2fs -q -F -t ext4 -b 4096 -m 0 -L sistema -d '$G' '$PART' ${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83\n' | /usr/sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"

# ! THE IMAGE GOES TO THE HOST DISK BEFORE QEMU STARTS (17/11, voce 70): the
# guest's FLUSH CACHE is QEMU's fdatasync of this file, and a fresh 1.1 GB
# image took the host 60 s to write. The flush at mount ran out of its 30 s,
# the ext4 journal did not open, the volume went read-only: "Profile Missing".
sync "$IMG"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=riap$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -smp 2 -nic none -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialriap$SUFF.txt; rm -f "$SER"
T=/tmp/exos/riap$SUFF-foto; rm -rf "$T"; mkdir -p "$T"
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }
gialla() {
    python3 - "$1" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
p = d.split(b'\n', 3)
w, h = map(int, p[1].split()); px = p[3]
i = ((h * 3 // 4) * w + w * 3 // 4) * 3   # not the middle: the mouse is there
sys.exit(0 if tuple(px[i:i+3]) == (0xff, 0xf3, 0xc4) else 1)
EOF
}

# the boot jobs first (automount, dhcp), then the commands
A=("mount hd0p1 /disk@5" "echo attendo la fine dei lavori di avvio@30")
for v in MOZ_FORCE_DISABLE_E10S=1 HOME=/disk/casa LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font \
         EXWIN_LOG=/disk/firefox.log; do A+=("export $v@1"); done
FF="/disk/firefox/firefox -no-remote -profile /disk/profilo file:///disk/prova.html"
A+=("ls -l /disk@4" "exwin@14" "key:alt-f1@2" "cat /proc/disco@2"
    "$FF &@3" "key:alt-f5@${ATTESA1:-120}" "foto:$T/1-aperto.ppm@2" "key:alt-f1@2"
    "echo PS""-UNO@1" "ps@4"
    "key:alt-f5@2" "key:ctrl-q@5" "foto:$T/1-ctrl-q.ppm@1" "key:ret@40" "foto:$T/1-chiuso.ppm@1"
    "key:alt-f1@2" "echo PS""-CHIUSO@1" "ps@4" "cat /proc/disco@2"
    "echo PROFILO""-INIZIO@1" "ls -a -l /disk/profilo@4" "echo PROFILO""-FINE@1"
    "$FF &@3" "key:alt-f5@${ATTESA2:-90}" "foto:$T/2-aperto.ppm@2" "key:alt-f1@2"
    "echo PS""-DUE@1" "ps@4" "cat /proc/disco@2"
    "echo FINE""-RIAPERTURA@2")
timeout 1800 python3 tools/qemu_drive.py "${A[@]}" > "$OUT/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$OUT/seriale.txt"
L="$OUT/seriale.txt"
tra() { sed -n "/^$1\$/,/^$2\$/p" "$L"; }   # the output between two markers
# a ps line of firefox: it starts with the pid ("[11] terminato: ..." does not)
ff_vivo() { grep -a "^[0-9][0-9]* .*/disk/firefox/firefox" | grep -avq zombie; }

[ "$(grep -ac '^d.* casa$\|^d.* profilo$' "$L")" -ge 2 ] || echo "  (ls -l /disk non mostra casa e profilo: la prova non vale)"
grep -aq "va ricollegato alla libc nuova" "$L" && no "Firefox collegato a una libc vecchia (stat di prima della 0.339)"
[ -f "$T/1-aperto.ppm" ] && gialla "$T/1-aperto.ppm" && si "primo avvio: la pagina c'e'" || no "primo avvio: niente pagina (vedi $OUT/1-aperto.png)"
tra PS-UNO PS-CHIUSO | ff_vivo && si "primo avvio: firefox gira" || no "primo avvio: firefox non gira"
tra PS-CHIUSO PROFILO-INIZIO | ff_vivo && no "dopo Ctrl+Q firefox gira ancora" || si "dopo Ctrl+Q firefox e' uscito"
tra PROFILO-INIZIO PROFILO-FINE | grep -aq "prefs.js" && si "il profilo ha prefs.js" || no "il profilo non ha prefs.js"
[ -f "$T/2-aperto.ppm" ] && gialla "$T/2-aperto.ppm" && si "secondo avvio: la pagina c'e'" || no "secondo avvio: niente pagina (vedi $OUT/2-aperto.png)"
tra PS-DUE FINE-RIAPERTURA | ff_vivo && si "secondo avvio: firefox gira" || no "secondo avvio: firefox non gira"
grep -aq "terminato: /disk/firefox/firefox.*(codice 0)" "$L" && si "Ctrl+Q: firefox uscito con codice 0" || no "Ctrl+Q: firefox non e' uscito con codice 0"
grep -aq "\[FAULT\]\|PANIC" "$L" && no "FAULT o PANIC" || si "nessun FAULT"
for p in "$T"/*.ppm; do python3 tools/ppm2png.py "$p" "$OUT/$(basename "${p%.ppm}").png" > /dev/null 2>&1 && rm -f "$p"; done
rm -f "$IMG"
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
