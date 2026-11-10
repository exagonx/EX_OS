#!/bin/bash
# =============================================================================
# tools/exilla/prova-lampi.sh - white flashes while Firefox repaints (voce 95)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/prova-lampi.sh                     32 bit
#     EXOS_ARCH=x86_64 tools/exilla/prova-lampi.sh    64 bit
#     PACCHETTO=<dir with firefox> ...                another build of Firefox
#     QEMU_PIU="-smp 2" ...                           two processors
#     FOTO=150 PASSO=0.2 ...                          how many pictures, how often
#     TIPO=video ...                                  a video instead of the canvas
#     LENTO=1 ...                                     no KVM: a slow processor, as
#                                                     on old hardware (frames late)
#     ETICHETTA=vecchio ...                           its own directories and QEMU,
#                                                     to run two of them together
#
# The user saw white flashes over videos in Firefox, with one processor and
# more with two. Here Firefox, in an ExWin window, shows a page with NO white
# at all: a black page and a canvas that repaints itself 60 times a second in
# dark colours. Then FOTO screendumps close together: a picture with white
# pixels in the middle of the screen caught Firefox's window half drawn.
#
# The prova is [OK] when no picture has white there AND the animation is
# visible in them (the pictures differ: otherwise nothing was being drawn and
# the absence of flashes would prove nothing).
#
# TIPO=video: the canvas gave 0 flashes with the Firefox of before too (9
# November 2026), so the flashes belong to video. The page is MAGENTA and a
# VP9 video (vp9cake.webm, in a loop) covers it all: magenta in the middle of
# the screen is the page showing through where a frame of the video was
# missing; a middle mostly pure white is an area cleared and not yet painted.
#
# Run after tools/exilla/prova-gecko.sh, which makes the package.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$PWD/cross_build/$MACCHINA"
O="$PWD/cross_build/exilla-obj/gecko"; D="$B/costruzione-prova"; SUFF=""
if [ "$EXOS_ARCH" = x86_64 ]; then O="$PWD/cross_build/exilla-obj/gecko64"; D="$B/costruzione-prova64"; SUFF=64; fi
P="${PACCHETTO:-$O/dist/firefox}"
[ -x "$P/firefox" ] || { echo "manca $P/firefox: prima tools/exilla/prova-gecko.sh" >&2; exit 1; }
E="${ETICHETTA:+-$ETICHETTA}"
F="$D/lampi$E"
# ! THE PICTURES GO THROUGH /tmp/exos: the project path has the user's "@",
# which qemu_drive.py would take for the wait ("cmd@seconds").
T=/tmp/exos/lampi$SUFF$E
rm -rf "$F" "$T"; mkdir -p "$F" "$T"

S="$D/lampi$E-disco"
rm -rf "${S:?}"; mkdir -p "$S/profilo" "$S/casa" "$S/font"
cp -a "$P" "$S/firefox"; cp exwin/font/*.ttf "$S/font/"
PAGINA=lampi.html
if [ "${TIPO:-canvas}" = video ]; then
    PAGINA=video.html
    cp firefox-main/dom/media/test/vp9cake.webm "$S/"
    BITS=32; [ "$EXOS_ARCH" = x86_64 ] && BITS=64
    mkdir -p "$S/lib"
    for l in ffmpeg libvpx dav1d opus vorbis ogg zlib; do
        cp cross_build/$l-exos$BITS/exos/lib/lib*.so.[0-9]* "$S/lib/" 2>/dev/null
    done
    printf '%s\n' 'user_pref("media.autoplay.default", 0);' \
        'user_pref("media.autoplay.blocking_policy", 0);' > "$S/profilo/user.js"
    printf '%s\n' '<!DOCTYPE html>' \
        '<html><head><meta charset="utf-8"><title>Lampi video</title>' \
        '<style>html, body { margin: 0; background: #FF00FF; overflow: hidden; }' \
        'video { position: fixed; left: 0; top: 0; width: 100vw; height: 100vh; object-fit: fill; }</style></head>' \
        '<body><video src="vp9cake.webm" autoplay loop muted></video></body></html>' > "$S/video.html"
fi
cat > "$S/lampi.html" <<'EOF'
<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Lampi</title>
<style>html, body { margin: 0; background: #000; overflow: hidden; }
canvas { position: fixed; left: 0; top: 0; }</style></head>
<body><canvas id="c"></canvas>
<script>
  // Dark colours only (no channel above 0x60): any white pixel on the
  // screen inside this page is not the page's.
  var c = document.getElementById("c"), g = c.getContext("2d"), t = 0;
  c.width = innerWidth; c.height = innerHeight;
  function giro() {
    t++;
    g.fillStyle = "rgb(" + (t * 3 % 0x60) + "," + (t * 5 % 0x60) + "," + (t * 7 % 0x60) + ")";
    g.fillRect(0, 0, c.width, c.height);
    g.fillStyle = "#402010";
    g.fillRect((t * 8) % c.width, 40, 120, c.height - 80);
    requestAnimationFrame(giro);
  }
  requestAnimationFrame(giro);
</script></body></html>
EOF

MB=$(( $(du -sm "$S" | cut -f1) * 13 / 10 + 128 ))
IMG="$D/lampi$E-hd.img"; PART="$D/lampi$E-part.img"
rm -f "$IMG" "$PART"
"$(command -v mke2fs || echo /sbin/mke2fs)" -q -t ext2 -b 4096 -L lampi -d "$S" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' \
    | "$(command -v sfdisk || echo /sbin/sfdisk)" "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none
rm -f "$PART"

KVM=""; [ -w /dev/kvm ] && [ -z "${LENTO:-}" ] && KVM="-enable-kvm"
CD="$PWD/dist/exos.iso"; CPU=""
if [ "$EXOS_ARCH" = x86_64 ]; then
    CD="$PWD/dist/exos64.iso"; CPU="-cpu qemu64"
    export EXOS_QEMU=qemu-system-x86_64
fi
export EXOS_ISTANZA=lampi$SUFF${ETICHETTA:-} EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU${QEMU_PIU:+ $QEMU_PIU} -drive file=$IMG,format=raw,if=ide"
FOTO="${FOTO:-150}"; PASSO="${PASSO:-0.2}"
{
    echo "mount hd0p1 /disk@5"
    # ! THE JOBS OF THE BOOT FIRST (ehci, ohci, sis.drv -2dservizio): exwin
    # typed while they still ran came back to the prompt silently, without
    # wserver, four times out of four on 9 November 2026 (voce 97). As in
    # prova-gecko.sh.
    echo "echo attendo la fine dei lavori di avvio@30"
    for v in MOZ_FORCE_DISABLE_E10S=1 HOME=/disk/casa LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font \
             LD_LIBRARY_PATH=/disk/lib; do
        echo "export $v@1"; done
    echo "exwin@14"
    echo "key:alt-f1@2"
    echo "/disk/firefox/firefox -no-remote -profile /disk/profilo file:///disk/$PAGINA &@3"
    echo "key:alt-f5@3"
    # the pointer in the corner, out of the middle of the screen
    for i in $(seq 1 8); do echo "mon:mouse_move -600 -600@0"; done
    echo "foto:$T/avvio.ppm@${ATTESA_AVVIO:-300}"
    echo "raffica:$T/f|$FOTO|$PASSO@2"
    echo "key:alt-f1@3"
    echo "echo FINE-LAMPI@2"
} > "$F/args.txt"
mapfile -t A < "$F/args.txt"
timeout $(( ${ATTESA_AVVIO:-300} + 900 )) python3 tools/qemu_drive.py "${A[@]}" > "$F/qemu.log" 2>&1

python3 -I - "$T" "$F" "${TIPO:-canvas}" <<'PY'
import glob, os, shutil, sys, zlib
T, F = sys.argv[1], sys.argv[2]
def leggi(p):
    d = open(p, "rb").read()
    campi, pos = [], 0
    while len(campi) < 4:
        while d[pos:pos+1].isspace(): pos += 1
        if d[pos:pos+1] == b"#":
            pos = d.index(b"\n", pos)
            continue
        fine = pos
        while not d[fine:fine+1].isspace(): fine += 1
        campi.append(d[pos:fine]); pos = fine
    return int(campi[1]), int(campi[2]), d[pos+1:]
TIPO = sys.argv[3]
foto = sorted(glob.glob(os.path.join(T, "f[0-9][0-9][0-9].ppm")))
lampi, diverse, valide, prima, bianchi_tutti = [], 0, 0, None, []
for p in foto:
    w, h, px = leggi(p)
    # the middle of the screen: a quarter of each side in
    bianchi = magenta = buio = 0
    for y in range(h // 4, 3 * h // 4, 2):
        riga = px[(y * w + w // 4) * 3:(y * w + 3 * w // 4) * 3]
        for x in range(0, len(riga), 6):
            r, g, b = riga[x], riga[x+1], riga[x+2]
            if r > 0xE0 and g > 0xE0 and b > 0xE0: bianchi += 1
            elif r > 0xE0 and g < 0x30 and b > 0xE0: magenta += 1
            elif r <= 0x60 and g <= 0x60 and b <= 0x60: buio += 1
    tot = (h // 4) * (w // 4)
    # valid: the page (canvas) or the video is there, not the console
    if (TIPO == "video" and magenta < tot // 2) or buio > tot // 2: valide += 1
    bianchi_tutti.append(bianchi)
    impronta = zlib.crc32(px)
    if prima is not None and impronta != prima: diverse += 1
    prima = impronta
    if magenta > 200 or (TIPO != "video" and bianchi > 200) or (TIPO == "video" and bianchi > tot // 4):
        lampi.append((os.path.basename(p), bianchi, magenta))
print("lampi (%s): %d foto, %d valide, %d diverse dalla precedente" % (TIPO, len(foto), valide, diverse))
if bianchi_tutti:
    s = sorted(bianchi_tutti)
    print("  punti bianchi in mezzo: min %d, mediana %d, max %d" % (s[0], s[len(s) // 2], s[-1]))
# the first and the last picture stay too: they show what was being drawn
for q in (foto[:1] + foto[-1:]):
    if os.path.exists(q): shutil.copy(q, os.path.join(F, os.path.basename(q)))
for nome, n, m in lampi:
    print("  LAMPO %s: %d punti bianchi, %d magenta" % (nome, n, m))
    shutil.move(os.path.join(T, nome), os.path.join(F, nome))
ok = foto and valide >= len(foto) * 9 // 10 and diverse >= len(foto) // 2 and not lampi
print("=== [%s] %d foto su %d con un lampo ===" % ("OK" if ok else "NO", len(lampi), len(foto)))
sys.exit(0 if ok else 1)
PY
rc=$?
[ -f "$T/avvio.ppm" ] && python3 tools/ppm2png.py "$T/avvio.ppm" "$F/avvio.png" > /dev/null 2>&1
for p in "$F"/f*.ppm; do [ -f "$p" ] && python3 tools/ppm2png.py "$p" "${p%.ppm}.png" > /dev/null 2>&1 && rm -f "$p"; done
rm -rf "$T"
exit $rc
