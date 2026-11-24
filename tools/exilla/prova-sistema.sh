#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/exilla/prova-sistema.sh - Firefox and the programs of the system
# (voce 118): the service Firefox asks on Linux (nsIGIOService) answered by
# EX-OS, and the file dialog of ExWin as Firefox's file picker
#
#     tools/exilla/prova-sistema.sh                    32 bit
#     EXOS_ARCH=x86_64 tools/exilla/prova-sistema.sh
#
#   1  a startup script with chrome privileges (autoconfig, test copy only)
#      asks the service: the program of image/png is the one of tipi.txt
#      ("Immagini"), mp4 is video/mp4;
#   2  nsIFile.reveal() of /disk/prova.txt opens the file manager on /disk
#      (filemgr in ps);
#   3  a page that is one big <input type=file>: a click opens the ExWin
#      dialog (scegli in ps), the path typed comes back to the page
#      ("FILE-SCELTO prova.txt" from dump);
#   4  no FAULT.
# Output: ~/ex_os/cross_build/<machine>/costruzione-prova[64]/sistema/.
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
D="${EXOS_FUORI}/cross_build/$MACCHINA/costruzione-prova$SUFF"; OUT="$D/sistema"
G="$D/sistema-disco"; IMG="$D/sistema-hd.img"; PART="$D/sistema-part.img"
mkdir -p "$D" /tmp/exos; rm -rf "$OUT"; mkdir -p "$OUT"

if [ ! -x "$P/firefox" ] || [ -n "$(find -L "$O/dist/bin" -newer "$P/firefox" -type f -print -quit 2>/dev/null)" ]; then
    tools/exilla/gecko-costruisci.sh package > "$D/gecko-package.log" 2>&1 || {
        echo "mach package fallito: $D/gecko-package.log" >&2; exit 1; }
fi

rm -rf "$G"; mkdir -p "$G/font"
cp -a "$P" "$G/firefox"; cp exwin/font/*.ttf "$G/font/"
cat > "$G/firefox/defaults/pref/sistema.js" <<'EOF'
pref("datareporting.policy.firstRunURL", "");
pref("browser.dom.window.dump.enabled", true);
pref("general.config.filename", "sistema.cfg");
pref("general.config.obscure_value", 0);
pref("general.config.sandbox_enabled", false);
EOF
# ! THE FIRST LINE OF AN AUTOCONFIG FILE IS SKIPPED: a comment
cat > "$G/firefox/sistema.cfg" <<'EOF'
// prova-sistema.sh (voce 118)
try {
  let gio = Components.classes["@mozilla.org/gio-service;1"].getService(Components.interfaces.nsIGIOService);
  let app = gio.getAppForMimeType("image/png");
  dump("GIO-PNG " + (app ? app.name + " " + app.QueryInterface(Components.interfaces.nsIGIOMimeApp).command : "nessuno") + "\n");
  dump("GIO-MP4 " + gio.getMimeTypeFromExtension("mp4") + "\n");
  let f = Components.classes["@mozilla.org/file/local;1"].createInstance(Components.interfaces.nsIFile);
  f.initWithPath("/disk/prova.txt");
  f.reveal();
  dump("GIO-REVEAL fatto\n");
} catch (e) {
  dump("GIO-ERRORE " + e + "\n");
}
EOF
echo "una riga" > "$G/prova.txt"
cat > "$G/sistema.html" <<'EOF'
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Sistema</title>
<style>html,body{margin:0;height:100%}input{position:fixed;left:0;top:0;width:100%;height:100%;font-size:40px;background:#c4e3ff}</style></head>
<body><input type="file" id="f" onchange="dump('FILE-SCELTO ' + (this.files.length ? this.files[0].name : '-') + '\n')"></body></html>
EOF
MB=$(( $(du -sm "$G" | cut -f1) * 13 / 10 + 128 ))
rm -f "$IMG" "$PART"
fakeroot /sbin/mke2fs -q -t ext2 -b 4096 -L gecko -d "$G" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"
sync "$IMG"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=sistema$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -smp 2 -nic none -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialsistema$SUFF.txt
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

T=/tmp/exos/sistema$SUFF-foto; rm -rf "$T"; mkdir -p "$T"
rm -f "$SER"
A=("mount hd0p1 /disk@5" "echo attendo la fine dei lavori di avvio@30")
for v in HOME=/disk/c2 LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font EXILLA_DIAG=1; do A+=("export $v@1"); done
A+=("mkdir /disk/p2@3" "mkdir /disk/c2@3")
A+=("exwin@14" "key:alt-f1@2"
    "/disk/firefox/firefox -no-remote -profile /disk/p2 file:///disk/sistema.html &@3" "key:alt-f5@3"
    "foto:$T/a1.ppm@${ATTESA1:-100}"
    "key:alt-f1@2" "echo PS1""-INIZIO@1" "ps@5" "echo PS1""-FINE@1" "key:alt-f5@3"
    "vai:400,400@1" "mon:mouse_button 1;mouse_button 0@8" "foto:$T/b-dialogo.ppm@3"
    "/disk/prova.txt@n2" "key:ret@6" "foto:$T/c-dopo.ppm@3"
    "key:alt-f1@3" "echo PS2""-INIZIO@1" "ps@5" "echo PS2""-FINE@1" "echo FINE-SISTEMA@2")
timeout 900 python3 tools/qemu_drive.py "${A[@]}" > "$OUT/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$OUT/seriale.txt"

grep -a "GIO-\|FILE-SCELTO\|exwin.so\|non parte" "$OUT/seriale.txt" | head -8 | sed 's/^/      /'
grep -aq "GIO-PNG Immagini /exwin/bin/immagini" "$OUT/seriale.txt" \
    && si "il programma di image/png e' quello di tipi.txt (Immagini)" || no "image/png: nessun programma, o non quello di tipi.txt"
grep -aq "GIO-MP4 video/mp4" "$OUT/seriale.txt" && si "mp4 e' video/mp4" || no "mp4 non riconosciuto"
PS1=$(sed -n '/^PS1-INIZIO/,/^PS1-FINE/p' "$OUT/seriale.txt")
echo "$PS1" | grep -aq filemgr && grep -aq "GIO-REVEAL fatto" "$OUT/seriale.txt" \
    && si "reveal() apre il file manager" || no "reveal() non apre il file manager"
grep -aq "FILE-SCELTO prova.txt" "$OUT/seriale.txt" \
    && si "il dialogo di ExWin sceglie il file per la pagina" || no "nessun FILE-SCELTO prova.txt (vedi $OUT/b-dialogo.png)"
grep -a "\[FAULT\]\|PANIC" "$OUT/seriale.txt" | head -3 | sed 's/^/      /'
grep -aq "\[FAULT\]\|PANIC" "$OUT/seriale.txt" && no "FAULT o PANIC" || si "nessun FAULT"
for p in "$T"/*.ppm; do
    python3 tools/ppm2png.py "$p" "$OUT/$(basename "${p%.ppm}").png" > /dev/null 2>&1 && rm -f "$p"
done
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
