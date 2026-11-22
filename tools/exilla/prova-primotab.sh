#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/exilla/prova-primotab.sh - the address bar of the FIRST tab (voce 73)
#
#     tools/exilla/prova-primotab.sh            32 bit (gecko, dist/exos.iso)
#     EXOS_ARCH=x86_64 tools/exilla/prova-primotab.sh   64 bit
#
# Until the Tabbrowser.sys.mjs patch of 17/11/2026, the SECOND start of
# Firefox without an address (profile already there, SessionStore restoring)
# made the first <browser> remote: EX-OS launches no child processes, so
# what was typed in that tab's bar never loaded ("aBrowser.frameLoader.
# remoteTab is null"). A new tab worked.
#
# Two starts on the same disk: the first makes the profile, the second is
# the one that counts. In each, file:///disk/seconda.html is typed in the
# first tab, then in a new tab; the page is yellow (#fff3c4), so the photo
# after Enter says whether it loaded. Photos and serial in
# cross_build/<macchina>/costruzione-prova[64]/primotab/.
# From ~/cross_build/prove/firefox-16-11/primotab.sh (the analysis).
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
D="${EXOS_FUORI}/cross_build/$MACCHINA/costruzione-prova$SUFF"; OUT="$D/primotab"
G="$D/primotab-disco"; IMG="$D/primotab-hd.img"; PART="$D/primotab-part.img"
mkdir -p "$D" /tmp/exos; rm -rf "$OUT"; mkdir -p "$OUT"

# the package must hold what dist/bin holds (see prova-gecko.sh)
if [ ! -x "$P/firefox" ] || [ -n "$(find -L "$O/dist/bin" -newer "$P/firefox" -type f -print -quit 2>/dev/null)" ]; then
    tools/exilla/gecko-costruisci.sh package > "$D/gecko-package.log" 2>&1 || {
        echo "mach package fallito: $D/gecko-package.log" >&2; exit 1; }
fi

rm -rf "$G"; mkdir -p "$G/font"
cp -a "$P" "$G/firefox"; cp exwin/font/*.ttf "$G/font/"
# ! NO PRIVACY NOTICE TAB (test copy only): on a first start Firefox opens
# it, and when its page (or its error page) arrives it takes the focus from
# the tab under test - seen at 32 and 64 bit, with and without network.
echo 'pref("datareporting.policy.firstRunURL", "");' > "$G/firefox/defaults/pref/primotab.js"
printf '<!DOCTYPE html><html><head><meta charset="utf-8"><title>Seconda</title></head><body style="background:#fff3c4"><h1>SECONDA PAGINA</h1></body></html>' > "$G/seconda.html"
MB=$(( $(du -sm "$G" | cut -f1) * 13 / 10 + 128 ))
rm -f "$IMG" "$PART"
# ! fakeroot: root owns the files, as on an installed system (Firefox run by
# root with $HOME owned by uid 1000 exits 1 since stat is true, kernel 0.339).
fakeroot /sbin/mke2fs -q -t ext2 -b 4096 -L gecko -d "$G" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"

# ! THE IMAGE GOES TO THE HOST DISK BEFORE QEMU STARTS (17/11, voce 70): the
# guest's FLUSH CACHE is QEMU's fdatasync of this file, and a fresh 1.1 GB
# image took the host 60 s to write. The flush at mount ran out of its 30 s,
# the ext4 journal did not open, the volume went read-only: "Profile Missing".
sync "$IMG"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=primotab$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
# ! NO NETWORK: on a first start Firefox opens the Privacy Notice tab, and
# when its page arrived from the net it took the focus from the tab under
# test (17/11, both at 32 and 64 bit). The fix needs no network.
export EXOS_QEMU_EXTRA="$KVM $CPU -smp 2 -nic none -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialprimotab$SUFF.txt
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }
# the photo's middle is the page's yellow?
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

for giro in 1 2; do
    T=/tmp/exos/primotab$SUFF-foto; rm -rf "$T"; mkdir -p "$T"
    rm -f "$SER"
    # ! THE BOOT JOBS FIRST: a command typed during the ATA BSY stall of the
    # first seconds may do nothing (17/11: mkdir /disk/p2 made nothing and said
    # nothing - why, still to find; and
    # Firefox stopped at "Profile Missing"). Same wait as prova-gecko.sh.
    A=("mount hd0p1 /disk@5" "echo attendo la fine dei lavori di avvio@30")
    for v in MOZ_FORCE_DISABLE_E10S=1 HOME=/disk/c2 LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font EXILLA_DIAG=1; do A+=("export $v@1"); done
    [ $giro = 1 ] && A+=("mkdir /disk/p2@3" "mkdir /disk/c2@3")
    A+=("ls -l /disk@4")
    A+=("exwin@14" "key:alt-f1@2"
        "/disk/firefox/firefox -no-remote -profile /disk/p2 &@3" "key:alt-f5@3"
        "foto:$T/a1.ppm@${ATTESA1:-70}" "foto:$T/a2.ppm@${ATTESA2:-30}"
        # the first tab chosen by hand: on a first start the Privacy Notice
        # tab may take the focus when its page arrives (17/11)
        "vai:110,40@1" "mon:mouse_button 1;mouse_button 0@3"
        "vai:400,76@1" "mon:mouse_button 1;mouse_button 0@3"
        "file:///disk/seconda.html@n4" "key:ret@1" "foto:$T/d-invio.ppm@12"
        # the new tab with Ctrl+T: the + button moves with the tabs
        "key:ctrl-t@5"
        "file:///disk/seconda.html@n4" "key:ret@1" "foto:$T/g-invio.ppm@12"
        "key:alt-f1@3" "echo FINE-PRIMOTAB@2")
    timeout 900 python3 tools/qemu_drive.py "${A[@]}" > "$OUT/qemu$giro.log" 2>&1
    tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$OUT/seriale$giro.txt"
    [ "$(grep -ac '^d.* [pc]2$' "$OUT/seriale$giro.txt")" -ge 2 ] \
        || echo "  (avvio $giro: ls -l /disk non mostra p2 e c2: la prova non vale)"
    [ -f "$T/d-invio.ppm" ] && gialla "$T/d-invio.ppm" && si "avvio $giro: il primo tab carica l'indirizzo scritto" \
        || no "avvio $giro: il primo tab non carica l'indirizzo (vedi $OUT/d-invio$giro.png)"
    [ -f "$T/g-invio.ppm" ] && gialla "$T/g-invio.ppm" && si "avvio $giro: un tab nuovo carica l'indirizzo scritto" \
        || no "avvio $giro: un tab nuovo non carica l'indirizzo (vedi $OUT/g-invio$giro.png)"
    grep -aq "remoteTab is null" "$OUT/seriale$giro.txt" && no "avvio $giro: remoteTab is null" \
        || si "avvio $giro: nessun remoteTab is null"
    grep -aq "\[FAULT\]\|PANIC" "$OUT/seriale$giro.txt" && no "avvio $giro: FAULT o PANIC" || si "avvio $giro: nessun FAULT"
    for p in "$T"/*.ppm; do
        python3 tools/ppm2png.py "$p" "$OUT/$(basename "${p%.ppm}")$giro.png" > /dev/null 2>&1 && rm -f "$p"
    done
done
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
