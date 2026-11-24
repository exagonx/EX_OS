#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/exilla/prova-processi.sh - Firefox with more processes (voce 115, step 5)
#
#     tools/exilla/prova-processi.sh                     32 bit (dist/exos.iso)
#     EXOS_ARCH=x86_64 tools/exilla/prova-processi.sh    64 bit
#
# Firefox started WITHOUT MOZ_FORCE_DISABLE_E10S, as on Linux: the parent
# launches its content processes (fork + exec of firefox -contentproc) and
# talks to them over unix pairs (socketpair, SCM_RIGHTS) and memory that is
# a descriptor (FD_SHM), kernel 0.358.
#
#   1  ps shows more than one firefox (the parent and a child);
#   2  file:///disk/seconda.html typed in the first tab loads (the page is
#      yellow, #fff3c4: the photo after Enter says it);
#   3  no FAULT and no PANIC on the serial line.
#
# Photos and serial in cross_build/<macchina>/costruzione-prova[64]/processi/.
# Built like tools/exilla/prova-primotab.sh (one start: the profile is new).
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
R="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "$EXOS_ARCH" = x86_64 ]; then
    SUFF=64; CD="$R/dist/exos64.iso"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
else
    SUFF="";  CD="$R/dist/exos.iso";   CPU=""
fi
O="${EXOS_FUORI}/cross_build/exilla-obj/gecko$SUFF"; P="$O/dist/firefox"
D="${EXOS_FUORI}/cross_build/$MACCHINA/costruzione-prova$SUFF"; OUT="$D/processi"
G="$D/processi-disco"; IMG="$D/processi-hd.img"; PART="$D/processi-part.img"
mkdir -p "$D" /tmp/exos; rm -rf "$OUT"; mkdir -p "$OUT"

# the package must hold what dist/bin holds (see prova-gecko.sh)
if [ ! -x "$P/firefox" ] || [ -n "$(find -L "$O/dist/bin" -newer "$P/firefox" -type f -print -quit 2>/dev/null)" ]; then
    tools/exilla/gecko-costruisci.sh package > "$D/gecko-package.log" 2>&1 || {
        echo "mach package fallito: $D/gecko-package.log" >&2; exit 1; }
fi

rm -rf "$G"; mkdir -p "$G/font"
cp -a "$P" "$G/firefox"; cp exwin/font/*.ttf "$G/font/"
# no Privacy Notice tab (test copy only): see prova-primotab.sh
echo 'pref("datareporting.policy.firstRunURL", "");' > "$G/firefox/defaults/pref/processi.js"
printf '<!DOCTYPE html><html><head><meta charset="utf-8"><title>Seconda</title></head><body style="background:#fff3c4"><h1>SECONDA PAGINA</h1></body></html>' > "$G/seconda.html"
MB=$(( $(du -sm "$G" | cut -f1) * 13 / 10 + 128 ))
rm -f "$IMG" "$PART"
fakeroot /sbin/mke2fs -q -t ext2 -b 4096 -L gecko -d "$G" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"
sync "$IMG"                         # see prova-primotab.sh (17/11, voce 70)

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=processi$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -smp 2 -nic none -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialprocessi$SUFF.txt
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }
gialla() {
    python3 - "$1" <<'EOF'
import sys
d = open(sys.argv[1], 'rb').read()
p = d.split(b'\n', 3)
w, h = map(int, p[1].split()); px = p[3]
i = ((h // 2) * w + w // 2) * 3
sys.exit(0 if tuple(px[i:i+3]) == (0xff, 0xf3, 0xc4) else 1)
EOF
}

T=/tmp/exos/processi$SUFF-foto; rm -rf "$T"; mkdir -p "$T"
rm -f "$SER"
A=("mount hd0p1 /disk@5" "echo attendo la fine dei lavori di avvio@30")
# ! NO MOZ_FORCE_DISABLE_E10S: that is the whole test
for v in HOME=/disk/c2 LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font EXILLA_DIAG=1; do A+=("export $v@1"); done
A+=("mkdir /disk/p2@3" "mkdir /disk/c2@3")
A+=("exwin@14" "key:alt-f1@2"
    "/disk/firefox/firefox -no-remote -profile /disk/p2 &@3" "key:alt-f5@3"
    "foto:$T/a1.ppm@${ATTESA1:-90}" "foto:$T/a2.ppm@${ATTESA2:-40}"
    "vai:110,40@1" "mon:mouse_button 1;mouse_button 0@3"
    "vai:400,76@1" "mon:mouse_button 1;mouse_button 0@3"
    "file:///disk/seconda.html@n4" "key:ret@1" "foto:$T/d-invio.ppm@20"
    "key:alt-f1@3" "echo PS""-INIZIO@1" "ps@5" "echo PS""-FINE@1" "echo FINE-PROCESSI@2")
timeout 900 python3 tools/qemu_drive.py "${A[@]}" > "$OUT/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$OUT/seriale.txt"

PS=$(sed -n '/^PS-INIZIO/,/^PS-FINE/p' "$OUT/seriale.txt")
echo "$PS" | grep -a firefox | sed 's/^/      /'
N=$(echo "$PS" | grep -ac firefox)
[ "$N" -ge 2 ] && si "piu' processi: $N firefox in ps" || no "un solo processo ($N firefox in ps)"
[ -f "$T/d-invio.ppm" ] && gialla "$T/d-invio.ppm" && si "il primo tab carica l'indirizzo scritto" \
    || no "il primo tab non carica l'indirizzo (vedi $OUT/d-invio.png)"
grep -a "\[FAULT\]\|PANIC\|IPDL protocol error\|MOZ_CRASH\|Assertion failure" "$OUT/seriale.txt" | head -5 | sed 's/^/      /'
grep -aq "\[FAULT\]\|PANIC" "$OUT/seriale.txt" && no "FAULT o PANIC" || si "nessun FAULT"
for p in "$T"/*.ppm; do
    python3 tools/ppm2png.py "$p" "$OUT/$(basename "${p%.ppm}").png" > /dev/null 2>&1 && rm -f "$p"
done
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
