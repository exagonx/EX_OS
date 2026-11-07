#!/bin/bash
# =============================================================================
# tools/exilla/prova-webgl.sh - WebGL in Firefox inside EX-OS (voce 88 d)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/prova-webgl.sh                      32 bit
#     EXOS_ARCH=x86_64 tools/exilla/prova-webgl.sh     64 bit
#
# Firefox draws a page with a WebGL canvas: a blue background and a red
# triangle, then reads back with readPixels the centre (red) and a corner
# (blue). The page writes everything to the serial line with dump():
#     WEBGL <round> contesto webgl2|webgl   which context Firefox gave
#     WEBGL <round> renderer ...            GL_RENDERER, GL_VERSION, GLSL
#     WEBGL <round> centro r,g,b angolo r,g,b
#     WEBGL <round> ESITO OK | ESITO NO <why>
# Two rounds in the same boot:
#   senza     firefox --headless --screenshot (as prova-gecko.sh), the picture
#             stays in costruzione-prova<64>/webgl-senza.png. Told, NOT
#             judged: headless Gecko has no canvas manager here (see below);
#   finestra  Firefox in an ExWin window, the screen photographed: it must
#             have the triangle's red and the background's blue, many pixels
#             of each (webgl-finestra-N.png). This is the path a user takes,
#             and it decides.
# Mesa (tools/mesa-exos/costruisci.sh) comes from the sysroot with every
# library it needs, in /disk/lib, found through LD_LIBRARY_PATH: Firefox opens
# libEGL.so and libGLESv2.so with dlopen (gfx/gl/GLLibraryEGL.cpp), as on a
# machine where netupdate -install:mesa put them in /exos/lib.
#     FOTO_OGNI=s FOTO_N=n   photographs of the window, one every s seconds
# The package is made here when the build is newer than it.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$PWD/cross_build/$MACCHINA"
O="$PWD/cross_build/exilla-obj/gecko"; D="$B/costruzione-prova"; SUFF=""
T=i386-exos; X="$B/exos-cross"; BITS=32
if [ "$EXOS_ARCH" = x86_64 ]; then
    O="$PWD/cross_build/exilla-obj/gecko64"; D="$B/costruzione-prova64"; SUFF=64
    T=x86_64-exos; X="$B/exos-cross64"; BITS=64
fi
P="$O/dist/firefox"
mkdir -p "$D"
# the package is made again when anything in dist/bin is newer (as
# prova-gecko.sh: a relink to the libc that Mesa needs leaves the old package
# behind, and the test would run the old binary without saying so)
if [ ! -x "$P/firefox" ] || [ -n "$(find -L "$O/dist/bin" -newer "$P/firefox" -type f -print -quit 2>/dev/null)" ]; then
    EXOS_ARCH=$EXOS_ARCH tools/exilla/gecko-costruisci.sh package > "$D/webgl-package.log" 2>&1 || {
        echo "mach package fallito: $D/webgl-package.log" >&2; exit 1; }
fi
[ -x "$P/firefox" ] || { echo "manca $P/firefox: prima tools/exilla/gecko-costruisci.sh build" >&2; exit 1; }
[ -f "$X/$T/lib/libEGL.so.1" ] || { echo "manca Mesa nel sysroot: prima tools/mesa-exos/costruisci.sh $BITS" >&2; exit 1; }
mkdir -p "$D" /tmp/exos
S="$D/webgl-disco"
rm -rf "${S:?}"; mkdir -p "$S/profilo" "$S/profilo2" "$S/casa" "$S/font" "$S/lib"
cp -a "$P" "$S/firefox"; cp exwin/font/*.ttf "$S/font/"

# --- Mesa and what it needs, by DT_NEEDED (no list written by hand) ------------
. tools/mesa-exos/librerie-prova.sh
for l in libEGL.so.1 libGLESv2.so.2; do
    cp "$(readlink -f "$X/$T/lib/$l")" "$S/lib/$l"
    librerie_prova "$S/lib/$l" $T "$X" $BITS "$S/lib" > /dev/null || exit 1
done
# the names Firefox asks for first (libEGL.so, then libEGL.so.1): EX-OS has no
# symbolic links, so the plain name is a copy
cp "$S/lib/libEGL.so.1" "$S/lib/libEGL.so"; cp "$S/lib/libGLESv2.so.2" "$S/lib/libGLESv2.so"
echo "  librerie per Firefox: $(ls "$S/lib" | tr '\n' ' ')"

for p in profilo profilo2; do
    echo 'user_pref("browser.dom.window.dump.enabled", true);' > "$S/$p/user.js"
done

cat > "$S/webgl.html" <<'EOF'
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>WebGL</title>
<style>body { font-family: sans-serif; background: #f4f1e8; margin: 16px; }</style></head>
<body>
  <h1 id="esito">WebGL non e' partito</h1>
  <canvas id="c" width="480" height="360"></canvas>
  <p id="info"></p>
<script>
var giro = location.hash.substring(1) || "?";
function riga(s) { dump("WEBGL " + giro + " " + s + "\n"); }
function fine(ok, perche) {
  riga(ok ? "ESITO OK" : "ESITO NO " + perche);
  document.getElementById("esito").textContent = ok ? "WebGL: OK" : "WebGL: NO - " + perche;
  document.title = ok ? "WebGL OK" : "WebGL NO";
}
var c = document.getElementById("c"), gl = null, quale = "";
c.addEventListener("webglcontextcreationerror", function (e) {
  riga("errore di creazione: " + (e.statusMessage || "(nessun messaggio)"));
});
try {
  gl = c.getContext("webgl2", { preserveDrawingBuffer: true }); quale = "webgl2";
  if (!gl) { gl = c.getContext("webgl", { preserveDrawingBuffer: true }); quale = "webgl"; }
} catch (e) { riga("eccezione " + e); }
if (!gl) {
  fine(false, "nessun contesto");
} else {
  riga("contesto " + quale);
  var ri = gl.getExtension("WEBGL_debug_renderer_info");
  riga("renderer " + gl.getParameter(gl.RENDERER) + " | " +
       (ri ? gl.getParameter(ri.UNMASKED_RENDERER_WEBGL) + " | " : "") +
       gl.getParameter(gl.VERSION) + " | " + gl.getParameter(gl.SHADING_LANGUAGE_VERSION));
  function shader(tipo, testo) {
    var s = gl.createShader(tipo); gl.shaderSource(s, testo); gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) riga("shader: " + gl.getShaderInfoLog(s));
    return s;
  }
  var pr = gl.createProgram();
  gl.attachShader(pr, shader(gl.VERTEX_SHADER,
    "attribute vec2 p; void main() { gl_Position = vec4(p, 0.0, 1.0); }"));
  gl.attachShader(pr, shader(gl.FRAGMENT_SHADER,
    "precision mediump float; void main() { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); }"));
  gl.linkProgram(pr);
  if (!gl.getProgramParameter(pr, gl.LINK_STATUS)) riga("link: " + gl.getProgramInfoLog(pr));
  gl.useProgram(pr);
  var b = gl.createBuffer();
  gl.bindBuffer(gl.ARRAY_BUFFER, b);
  gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([-0.8, -0.8, 0.8, -0.8, 0.0, 0.8]), gl.STATIC_DRAW);
  var a = gl.getAttribLocation(pr, "p");
  gl.enableVertexAttribArray(a); gl.vertexAttribPointer(a, 2, gl.FLOAT, false, 0, 0);
  gl.viewport(0, 0, c.width, c.height);
  gl.clearColor(0.0, 0.0, 1.0, 1.0); gl.clear(gl.COLOR_BUFFER_BIT);
  gl.drawArrays(gl.TRIANGLES, 0, 3);
  var px = new Uint8Array(4), qx = new Uint8Array(4);
  gl.readPixels(c.width / 2, c.height / 2, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, px);
  gl.readPixels(4, 4, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, qx);
  var err = gl.getError();
  riga("centro " + px[0] + "," + px[1] + "," + px[2] + " angolo " + qx[0] + "," + qx[1] + "," + qx[2] +
       " glGetError " + err);
  document.getElementById("info").textContent = quale + ": " + gl.getParameter(gl.VERSION);
  var rosso = px[0] > 240 && px[1] < 16 && px[2] < 16, blu = qx[2] > 240 && qx[0] < 16 && qx[1] < 16;
  if (rosso && blu && err == 0) fine(true, "");
  else fine(false, (rosso ? "" : "centro non rosso ") + (blu ? "" : "angolo non blu ") + (err ? "errore GL " + err : ""));
}
</script>
</body></html>
EOF

# --- the disk -------------------------------------------------------------------
MB=$(( $(du -sm "$S" | cut -f1) * 13 / 10 + 128 ))
IMG="$D/webgl-hd.img"; PART="$D/webgl-part.img"
rm -f "$IMG" "$PART"
"$(command -v mke2fs || echo /sbin/mke2fs)" -q -t ext2 -b 4096 -L webgl -d "$S" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' \
    | "$(command -v sfdisk || echo /sbin/sfdisk)" "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"

# --- EX-OS ------------------------------------------------------------------------
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
CD="$PWD/dist/exos.iso"; CPU=""
if [ "$EXOS_ARCH" = x86_64 ]; then CD="$PWD/dist/exos64.iso"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64; fi
export EXOS_ISTANZA=webgl$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialwebgl$SUFF.txt; rm -f "$SER"
# ! THE PHOTOGRAPHS GO THROUGH /tmp/exos: the project's path has the user's
# «@», which qemu_drive.py would take for the wait («cmd@seconds»).
F=/tmp/exos/webgl$SUFF-foto; rm -rf "$F"; mkdir -p "$F"
FOTO_OGNI="${FOTO_OGNI:-120}"; FOTO_N="${FOTO_N:-4}"
A=("mount hd0p1 /disk@5" "echo attendo la fine dei lavori di avvio@30"
   "export MOZ_FORCE_DISABLE_E10S=1@1" "export HOME=/disk/casa@1" "export LANG=it_IT.UTF-8@1"
   "export EXILLA_FONTS=/disk/font@1" "export LD_LIBRARY_PATH=/disk/lib@1")
for v in $AMBIENTE; do A+=("export $v@1"); done
A+=("/disk/firefox/firefox --headless -no-remote -profile /disk/profilo --window-size 800,600 --screenshot /disk/webgl-senza.png file:///disk/webgl.html#senza@~${SECONDI:-900}"
    "exwin@14" "key:alt-f1@2"
    "/disk/firefox/firefox -no-remote -profile /disk/profilo2 file:///disk/webgl.html#finestra &@3"
    "key:alt-f5@3")
for i in $(seq 1 "$FOTO_N"); do A+=("foto:$F/$i.ppm@$FOTO_OGNI"); done
A+=("key:alt-f1@3" "echo FINE-WEBGL@2")
timeout "${ATTESA:-$(( ${SECONDI:-900} + FOTO_OGNI * FOTO_N + 600 ))}" python3 tools/qemu_drive.py "${A[@]}" > "$D/webgl-1.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/webgl-seriale.txt"
grep -a -E "^WEBGL|FAULT|PF:|MOZ_CRASH|Assertion|cannot open|undefined|EGL" "$D/webgl-seriale.txt" | grep -av ENTROPIA | head -40

esito=0
# ! ONLY THE WINDOW DECIDES (7 November 2026). Headless, Firefox makes no
# canvas manager ("errore di creazione: !CanvasManagerChild::Get()") and
# gives no context: a limit of headless Gecko on EX-OS, not of Mesa, and not
# the path of anyone using Firefox. That round is told, not judged.
if grep -aq "^WEBGL finestra ESITO OK" "$D/webgl-seriale.txt"; then echo "  [OK] finestra: la pagina ha disegnato e riletto il triangolo"
else echo "  [NO] finestra: $(grep -a "^WEBGL finestra ESITO" "$D/webgl-seriale.txt" | tail -1)"; esito=1; fi
echo "  [--] senza finestre (non giudicato): $(grep -a "^WEBGL senza ESITO" "$D/webgl-seriale.txt" | tail -1)"

rm -f "$D/webgl-senza.png"
"$(command -v debugfs || echo /sbin/debugfs)" -R "dump /webgl-senza.png $D/webgl-senza.png" "$IMG?offset=1048576" > /dev/null 2>&1
[ -s "$D/webgl-senza.png" ] && echo "      $D/webgl-senza.png"

ultima=$(ls "$F"/*.ppm 2>/dev/null | sort -V | tail -1)
if [ -n "$ultima" ]; then
    python3 -I - "$ultima" <<'PY'
import sys
d = open(sys.argv[1], "rb").read()
import re
# P6, width height, 255, ONE blank, then RGB. ! Not d.split(maxsplit=4):
# the last piece loses its leading blanks too, and pixels whose bytes
# are 0x0a or 0x20 at the top left shortened the data (IndexError).
m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+\d+\s", d)
w, h = int(m.group(1)), int(m.group(2)); px = d[m.end():]
rossi = blu = 0
for i in range(0, w * h * 3, 3):
    r, g, b = px[i], px[i + 1], px[i + 2]
    if r > 240 and g < 20 and b < 20: rossi += 1
    elif b > 240 and r < 20 and g < 20: blu += 1
print("      schermo %dx%d: %d pixel rossi, %d blu" % (w, h, rossi, blu))
sys.exit(0 if rossi > 20000 and blu > 40000 else 1)
PY
    if [ $? = 0 ]; then echo "  [OK] la foto della finestra ha il triangolo rosso sul blu"
    else echo "  [NO] la foto della finestra non ha il rosso e il blu del canvas"; esito=1; fi
    for p in "$F"/*.ppm; do
        python3 tools/ppm2png.py "$p" "$D/webgl-finestra-$(basename "${p%.ppm}").png" > /dev/null 2>&1 && rm -f "$p"
    done
    ls "$D"/webgl-finestra-*.png
else
    echo "  [NO] nessuna foto della finestra"; esito=1
fi
[ $esito = 0 ] && echo "=== [OK] ===" || echo "=== [NO] ==="
exit $esito
