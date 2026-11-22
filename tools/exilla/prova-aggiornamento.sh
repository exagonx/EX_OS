#!/bin/bash
# =============================================================================
# tools/exilla/prova-aggiornamento.sh - Exilla updated over a USED profile
#
#     tools/exilla/prova-aggiornamento.sh            32 bit (gecko, dist/exos.iso)
#     EXOS_ARCH=x86_64 tools/exilla/prova-aggiornamento.sh   64 bit
#
# Voce 73, 17/11/2026: the Tabbrowser.sys.mjs fix was installed on the test
# PC and the first tab stayed dead, because the profile's startupCache keeps
# the old compiled scripts and Firefox drops it only when the build number
# inside the firefox program changes (an omni.ja-only update keeps it).
# exilla 0.007 remembers the installed Exilla in ~/.exilla/installata and
# starts Firefox once with MOZ_PURGE_CACHES=1 when it changed.
#
# One disk, four starts, the profile always the same (HOME=/disk/c2):
#   1  exilla, OLD omni.ja (the Tabbrowser of before the fix): makes the
#      profile and its script cache;
#   2  omni.ja replaced with the NEW one, firefox started DIRECTLY, without
#      the launcher: the old cache stays, the first tab must be dead. If it
#      is not, the test proves nothing and says so;
#   3  exilla: it sees the change, purges, the first tab must load;
#   4  exilla again: nothing changed, no purge, the first tab still loads.
# In each start file:///disk/seconda.html is typed in the first tab (yellow
# page = loaded). OLD_OMNI= the old omni.ja (default: the one made by
# ~/cross_build/prove/firefox-17-11-tab, the current omni.ja with the
# original Tabbrowser.sys.mjs).
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
R="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "$EXOS_ARCH" = x86_64 ]; then
    SUFF=64; CD="$R/dist/exos64.iso"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
    LANCIA="$R/build-64/exwin/bin/exilla"
else
    SUFF="";  CD="$R/dist/exos.iso";   CPU=""; LANCIA="$R/build/exilla/exilla"
fi
O="$R/cross_build/exilla-obj/gecko$SUFF"; P="$O/dist/firefox"
VECCHIO="${OLD_OMNI:-$HOME/cross_build/prove/firefox-17-11-tab/omni-vecchio$SUFF/omni.ja}"
D="$R/cross_build/$MACCHINA/costruzione-prova$SUFF"; OUT="$D/aggiornamento"
G="$D/aggiornamento-disco"; IMG="$D/aggiornamento-hd.img"; PART="$D/aggiornamento-part.img"
for f in "$P/firefox" "$P/omni.ja" "$VECCHIO" "$LANCIA" "$CD"; do
    [ -f "$f" ] || { echo "manca $f" >&2; exit 1; }
done
mkdir -p "$D" /tmp/exos; rm -rf "$OUT"; mkdir -p "$OUT"
# ! A COPY OF THE CD: another build may rewrite dist/ while the four starts run
cp "$CD" "$OUT/cd.iso"

rm -rf "$G"; mkdir -p "$G/font"
cp -a "$P" "$G/firefox"; cp exwin/font/*.ttf "$G/font/"
cp "$LANCIA" "$G/firefox/exilla"
cp "$P/omni.ja" "$G/omni-nuovo.ja"
cp "$VECCHIO" "$G/firefox/omni.ja"
# no Privacy Notice tab (as prova-primotab.sh: it takes the focus), and no
# "Make Nightly your default browser?" box: from the third start it covers
# the window and the address typed went into it (17/11, first run of this test)
# ! THE BROWSER'S OWN PREFS COME AFTER defaults/pref: firefox.js (in
# browser/omni.ja) set checkDefaultBrowser back to true. The browser's dir on
# disk is read after it, as Linux distributions do with vendor.js.
echo 'pref("datareporting.policy.firstRunURL", "");' > "$G/firefox/defaults/pref/primotab.js"
mkdir -p "$G/firefox/browser/defaults/preferences"
echo 'pref("browser.shell.checkDefaultBrowser", false);' \
    > "$G/firefox/browser/defaults/preferences/primotab.js"
printf '<!DOCTYPE html><html><head><meta charset="utf-8"><title>Seconda</title></head><body style="background:#fff3c4"><h1>SECONDA PAGINA</h1></body></html>' > "$G/seconda.html"
MB=$(( $(du -sm "$G" | cut -f1) * 13 / 10 + 128 ))
rm -f "$IMG" "$PART"
# ! fakeroot: root owns the files, as on an installed system (Firefox run by
# root with $HOME owned by uid 1000 exits 1 since stat is true, kernel 0.339).
fakeroot /sbin/mke2fs -q -t ext2 -b 4096 -L gecko -d "$G" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=aggiorna$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$OUT/cd.iso" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -smp 2 -nic none -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialaggiorna$SUFF.txt
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
AVVISO="Exilla e' cambiata dall'ultimo avvio"
LOG=/disk/c2/.exilla/firefox.log

for giro in 1 2 3 4; do
    T=/tmp/exos/aggiorna$SUFF-foto; rm -rf "$T"; mkdir -p "$T"
    rm -f "$SER"
    A=("mount hd0p1 /disk@5" "echo attendo la fine dei lavori di avvio@30")
    for v in HOME=/disk/c2 LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font EXILLA_DIR=/disk/firefox EXWIN_RAM_DETTO=1; do
        A+=("export $v@1"); done
    [ $giro = 1 ] && A+=("mkdir /disk/c2@3")
    [ $giro = 2 ] && A+=("cp -y /disk/omni-nuovo.ja /disk/firefox/omni.ja@~120")
    A+=("ls -l /disk/c2/.exilla@4" "cat /disk/c2/.exilla/installata@3" "exwin@14" "key:alt-f1@2")
    if [ $giro = 2 ]; then
        A+=("export MOZ_FORCE_DISABLE_E10S=1@1" "export EXILLA_DIAG=1@1"
            "/disk/firefox/firefox -no-remote -profile /disk/c2/.exilla/profilo &@3")
    else
        A+=("/disk/firefox/exilla &@3")
    fi
    A+=("key:alt-f5@3"
        "foto:$T/a1.ppm@${ATTESA1:-100}" "foto:$T/a2.ppm@${ATTESA2:-30}"
        "key:esc@3" "foto:$T/a3.ppm@1"
        "vai:110,40@1" "mon:mouse_button 1;mouse_button 0@3"
        "vai:400,76@1" "mon:mouse_button 1;mouse_button 0@3"
        "file:///disk/seconda.html@n4" "key:ret@15" "foto:$T/d-invio.ppm@1"
        "key:ctrl-q@5" "key:ret@30" "key:alt-f1@3"
        "ls -l /disk/c2/.exilla/profilo/startupCache@4")
    [ $giro != 2 ] && A+=("cat $LOG@8")
    A+=("echo FINE-AGGIORNA@2")
    timeout 1200 python3 tools/qemu_drive.py "${A[@]}" > "$OUT/qemu$giro.log" 2>&1
    S="$OUT/seriale$giro.txt"
    tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$S"
    for p in "$T"/*.ppm; do
        python3 tools/ppm2png.py "$p" "$OUT/$(basename "${p%.ppm}")$giro.png" > /dev/null 2>&1
    done
    carica=1; [ -f "$T/d-invio.ppm" ] && gialla "$T/d-invio.ppm" || carica=0
    avviso=0; grep -aq "$AVVISO" "$S" && avviso=1
    # ! THE FIRST TAB IS REMOTE when "Failed to launch tab subprocess" comes
    # BEFORE the first click (evento 1): later, a click in the bar may try one
    # too, harmlessly. "remoteTab is null" on Enter does not always come.
    remoto=$(awk '/evento 1 su/ { exit } /Failed to launch tab subprocess/ { r = 1 } END { print r + 0 }' "$S")
    case $giro in
    1)  [ $carica = 1 ] && si "1 Exilla vecchia, profilo nuovo: il primo tab carica" \
            || no "1 Exilla vecchia, profilo nuovo: il primo tab non carica (d-invio1.png)"
        [ $avviso = 1 ] && si "1 nessuna impronta: exilla butta la cache" \
            || no "1 nessuna impronta, ma exilla non lo dice"
        grep -aq "startupCache\|scriptCache" "$S" && grep -aq "scriptCache" "$S" \
            && si "1 il profilo ha la cache degli script" \
            || no "1 il profilo non ha la cache degli script: la prova non vale" ;;
    2)  [ $carica = 0 ] && [ $remoto = 1 ] \
            && si "2 controllo: omni.ja nuovo senza exilla, il primo tab e' morto (il guasto)" \
            || no "2 controllo: il guasto NON si riproduce senza exilla, la prova non vale" ;;
    3)  [ $avviso = 1 ] && si "3 exilla vede l'aggiornamento e butta la cache" \
            || no "3 exilla non vede l'aggiornamento"
        [ $carica = 1 ] && si "3 dopo l'aggiornamento il primo tab carica" \
            || no "3 dopo l'aggiornamento il primo tab non carica (d-invio3.png)"
        [ $remoto = 0 ] && si "3 nessun tab remoto" || no "3 il primo tab nasce remoto (tab subprocess prima del clic)" ;;
    4)  [ $avviso = 0 ] && si "4 niente di cambiato: la cache resta" \
            || no "4 exilla butta la cache anche senza aggiornamento"
        [ $carica = 1 ] && si "4 il primo tab carica" || no "4 il primo tab non carica (d-invio4.png)"
        [ $remoto = 0 ] && si "4 nessun tab remoto" || no "4 il primo tab nasce remoto" ;;
    esac
    grep -aq "\[FAULT\]\|PANIC" "$S" && no "$giro FAULT o PANIC" || si "$giro nessun FAULT"
done
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
