#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/exilla/prova-schermo-intero.sh - Firefox goes full screen (voce 121)
#
#     tools/exilla/prova-schermo-intero.sh            64 bit (dist/exos64.iso)
#     EXOS_ARCH=i386 tools/exilla/prova-schermo-intero.sh   32 bit
#
# The user, 25/11/2026: YouTube's videos do not go full screen. Firefox in
# ExWin on a yellow page; a click asks requestFullscreen (as YouTube's
# button does), Esc leaves it, F11 is the browser's own full screen. Each
# step a photograph, and the colour is read where it must be:
#   1. normal: the title bar on top, the taskbar at the bottom
#   2. after the click: yellow in all four corners (no frame, no taskbar),
#      and the page says "FS 1 WxH" with the size of the screen
#   3. after Esc: the title bar and the taskbar back, "FS 0"
#   4. F11: no title bar, no taskbar
#   5. F11 again: both back
# Results in cross_build/<macchina>/costruzione-prova[64]/schermo-intero/.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
R="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "${EXOS_ARCH:-x86_64}" = x86_64 ]; then
    SUFF=64; CD="${CD:-$R/dist/exos64.iso}"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
else
    SUFF="";  CD="${CD:-$R/dist/exos.iso}";   CPU=""; unset EXOS_QEMU
fi
P="${EXOS_FUORI}/cross_build/exilla-obj/gecko$SUFF/dist/firefox"
D="${EXOS_FUORI}/cross_build/$MACCHINA/costruzione-prova$SUFF"; OUT="$D/schermo-intero"
G="$OUT/disco"; IMG="$OUT/hd.img"; PART="$OUT/part.img"
[ -x "$P/firefox" ] || { echo "manca $P/firefox: prima tools/exilla/gecko-costruisci.sh package" >&2; exit 1; }
rm -rf "$OUT"; mkdir -p "$G/font" "$G/p" "$G/c" /tmp/exos
cp -a "$P" "$G/firefox"; cp exwin/font/*.ttf "$G/font/"
cp tools/exilla/exos-media.js "$G/firefox/defaults/pref/" 2>/dev/null
{
    echo 'pref("datareporting.policy.firstRunURL", "");'
    echo 'pref("browser.dom.window.dump.enabled", true);'
    echo 'pref("browser.shell.checkDefaultBrowser", false);'
    echo 'pref("browser.startup.homepage_override.mstone", "ignore");'
    echo 'pref("full-screen-api.transition-duration.enter", "0 0");'
    echo 'pref("full-screen-api.transition-duration.leave", "0 0");'
    echo 'pref("full-screen-api.warning.timeout", 0);'
} > "$G/firefox/defaults/pref/schermo.js"
cat > "$G/gialla.html" <<'EOF'
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Schermo</title>
<style>html,body{margin:0;height:100%;overflow:hidden;background:#fff3c4}h1{margin:0}</style></head>
<body onload="requestAnimationFrame(function(){requestAnimationFrame(function(){dump('PRONTA\n');});});">
<h1>CLIC = SCHERMO INTERO</h1>
<script>
document.addEventListener("click", function () {
  document.documentElement.requestFullscreen().catch(function (e) { dump("FS-ERRORE " + e + "\n"); });
});
document.addEventListener("fullscreenchange", function () {
  setTimeout(function () {
    dump("FS " + (document.fullscreenElement ? 1 : 0) + " " + innerWidth + "x" + innerHeight +
         " schermo " + screen.width + "x" + screen.height + "\n");
  }, 500);
});
document.addEventListener("fullscreenerror", function () { dump("FS-ERRORE fullscreenerror\n"); });
</script></body></html>
EOF
MB=$(( $(du -sm "$G" | cut -f1) * 13 / 10 + 256 ))
fakeroot /sbin/mke2fs -q -t ext2 -b 4096 -L schermo -d "$G" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"; rm -rf "$G"; sync "$IMG"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=schermo$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -smp 2 -nic none -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialschermo$SUFF.txt; rm -f "$SER"
A=("mount hd0p1 /disk@5" "echo attendo la fine dei lavori di avvio@30")
for v in HOME=/disk/c LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font; do A+=("export $v@1"); done
A+=("exwin@14" "key:alt-f1@2"
    "/disk/firefox/firefox -no-remote -profile /disk/p file:///disk/gialla.html &@${ATTESA:-300}"
    "key:alt-f5@3" "foto:$OUT/passo1.ppm@2"
    "vai:400,400@1" "mon:mouse_button 1;mouse_button 0@8" "foto:$OUT/passo2.ppm@2"
    "key:esc@8" "foto:$OUT/passo3.ppm@2"
    "key:f11@10" "foto:$OUT/passo4.ppm@2"
    "key:f11@10" "foto:$OUT/passo5.ppm@2"
    "key:alt-f1@2" "echo FINE-SCHERMO@2")
timeout 1200 python3 tools/qemu_drive.py "${A[@]}" > "$OUT/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$OUT/seriale.txt"

echo "=== Firefox a schermo intero ($([ -n "$SUFF" ] && echo 64 || echo 32) bit) ==="
grep -a "^FS\|PRONTA" "$OUT/seriale.txt" | sed 's/^/        /'
python3 -I - "$OUT" "$OUT/seriale.txt" <<'PY'
import sys
d, ser = sys.argv[1], sys.argv[2]
esito = 0
def ok(t): print("  [OK]  " + t)
def no(t):
    global esito
    print("  [NO]  " + t); esito = 1

def ppm(p):
    b = open(p, "rb").read()
    campi, i = [], 0
    while len(campi) < 4:
        while b[i:i+1].isspace(): i += 1
        j = i
        while not b[j:j+1].isspace(): j += 1
        campi.append(b[i:j]); i = j
    return int(campi[1]), int(campi[2]), b[i+1:]

GIALLO, BARRA, TASK = (0xff, 0xf3, 0xc4), (0x31, 0x4D, 0x8B), (0xd4, 0xd0, 0xc8)
NOMI = {GIALLO: "la pagina", BARRA: "la barra del titolo", TASK: "la barra delle applicazioni"}
def attesi(n, w, h):
    if n in (1, 3, 5):
        return [(w // 2, 8, BARRA, True), (w // 2, h - 3, TASK, True), (w // 2, h // 2 + 60, GIALLO, True)]
    if n == 2:
        return [(x, y, GIALLO, True) for x, y in ((2, 2), (w - 3, 2), (2, h - 3), (w - 30, h - 3))]
    return [(w // 2, 8, BARRA, False), (w // 2, h - 3, TASK, False), (w // 2, h // 2 + 60, GIALLO, True)]

dims = None
for n in range(1, 6):
    try:
        w, h, px = ppm("%s/passo%d.ppm" % (d, n))
    except Exception as e:
        no("passo %d: manca la fotografia (%s)" % (n, e)); continue
    dims = (w, h)
    for x, y, c, deve in attesi(n, w, h):
        k = (y * w + x) * 3
        v = tuple(px[k:k + 3])
        vicino = all(abs(a - b) <= 8 for a, b in zip(v, c))
        t = "passo %d (%d,%d): %s%s (%02x%02x%02x)" % (n, x, y, "" if deve else "non ", NOMI[c], *v)
        ok(t) if vicino == deve else no(t)

s = open(ser, "rb").read().decode("latin-1")
ok("la pagina e' pronta") if "PRONTA" in s else no("manca PRONTA: Firefox non e' partito in tempo")
if dims:
    pieno = "FS 1 %dx%d" % dims
    ok("requestFullscreen: " + pieno) if pieno in s else no("manca '%s' sulla seriale" % pieno)
ok("Esc: FS 0") if "FS 0 " in s else no("manca 'FS 0': Esc non esce")
ok("nessun FS-ERRORE") if "FS-ERRORE" not in s else no("FS-ERRORE sulla seriale")
ok("nessun [FAULT]") if "[FAULT]" not in s else no("un [FAULT] sulla seriale")
sys.exit(esito)
PY
E=$?
for p in "$OUT"/*.ppm; do
    [ -f "$p" ] && python3 tools/ppm2png.py "$p" "${p%.ppm}.png" > /dev/null 2>&1 && rm -f "$p"
done
rm -f "$IMG"
echo "  fotografie: $OUT"
[ $E = 0 ] && echo "=== TUTTO A POSTO ===" || echo "=== QUALCOSA NON VA ==="
exit $E
