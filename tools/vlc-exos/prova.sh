#!/bin/bash
# =============================================================================
# tools/vlc-exos/prova.sh - VLC plays audio and video inside EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/vlc-exos/prova.sh [32|64]      64 by default
#
# QEMU gets an AC'97 card whose output goes to a WAV file on this machine.
# First, on the console, VLC plays Firefox's test files in Vorbis, FLAC and
# MP3 through the "audio" service (aout_exos); then ExWin starts and VLC
# plays three videos, H.264 with AAC, VP9 and AV1, in a window (vout_exos),
# with a screenshot during each one. Run after tools/vlc-exos/costruisci.sh.
# (voce 105, step 4, 20 November 2026) VLC as on Linux: libvlc.so.5 and
# libvlccore.so.9 in /disk/lib, the plugins in /disk/plugins
# (VLC_PLUGIN_PATH), opened with dlopen; first of all vlc-cache-gen writes
# their plugins.dat, and [OK] wants it written.
# Between the two, the text interface (vlc -I ncurses, 18 November 2026): its
# screen read back while it plays a 1 kHz tone, z twice (volume down), read
# again, then q: the volume reads 100% and then 90%, and the card's output
# goes from a peak of 10000 to 7290 (0.9 cubed: VLC's software volume in
# aout_exos, 19 November 2026; before it the screen read nothing and the Qt
# slider sat at 0%). Not sound.ogg: Firefox's test file is silence (-91 dB),
# good for the Vorbis decoder and useless for the ear. At the end the Qt interface
# (vlc -I qt, same day): its window on the screen, VLC's own log (-vv, on
# the disk, read back with debugfs) says the "qt" interface module is in
# use. (voce 105 step 5, 21 November 2026) The video plays INSIDE it: the log
# says vout_exos made a child window of the video widget and no window of
# its own, and two screenshots 2 s apart differ in the middle of VLC's
# window (the video moves there). When the video is over, the same window
# shows the cone in its middle again: the child window went with it.
# What is [OK]: each audio file, recorded alone from the card (QEMU's
# wavcapture around it), is what FFmpeg on this machine decodes from it: as
# long, as many seconds of sound, the same peak (19 November 2026: one WAV of
# the whole run passed even with a file that never reached the card), VLC
# wrote no error for the audio, and every screenshot of a video has
# a picture in it (at least 2% of the screen differs from the empty desktop: the AV1 file is 160x90).
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
BIT=${1:-64}
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
TC="$PWD/cross_build"
V="$TC/vlc-exos$BIT/exos/bin/vlc"
[ -x "$V" ] || { echo "manca $V: prima tools/vlc-exos/costruisci.sh $BIT" >&2; exit 1; }
D="$TC/$MACCHINA/prova-vlc$BIT"
S="$D/disco"
rm -rf "${D:?}"; mkdir -p "$S/m" "$S/casa" "$D/foto"
cp "$V" "$S/vlc"
VL="$TC/vlc-exos$BIT/exos/lib"
mkdir -p "$S/lib"
cp "$VL"/libvlc.so.[0-9]* "$VL"/libvlccore.so.[0-9]* "$S/lib/"
cp -r "$VL/vlc/plugins" "$S/plugins"
cp "$VL/vlc/vlc-cache-gen" "$S/vlc-cache-gen"
# VLC is linked to the shared libraries (3 November 2026): the ones it names
# go to /disk/lib, found through LD_LIBRARY_PATH (on a system, /exos/lib)
for l in ffmpeg libvpx dav1d opus vorbis ogg flac zlib; do
    cp "$TC"/$l-exos$BIT/exos/lib/lib*.so.[0-9]* "$S/lib/" 2>/dev/null
done
# Qt for the Qt interface (tools/qt5-exos), and every library the program and
# Qt's plugins need that is not there yet, from the sysroot
Q="$TC/qt5-exos$BIT/exos/qt5"
case $BIT in 64) TRIPLA=x86_64-exos; CROSS=$TC/$MACCHINA/exos-cross64 ;; *) TRIPLA=i386-exos; CROSS=$TC/$MACCHINA/exos-cross ;; esac
[ -f "$Q/lib/libQt5Widgets.so.5" ] || { echo "manca $Q: prima tools/qt5-exos/costruisci.sh $BIT" >&2; exit 1; }
cp -a "$Q" "$S/qt5"
rm -rf "$S/qt5/include" "$S/qt5/mkspecs" "$S/qt5/bin" "$S/qt5/lib/cmake" "$S/qt5/lib/pkgconfig" "$S"/qt5/lib/*.a "$S"/qt5/lib/*.la "$S"/qt5/lib/*.prl
python3 - "$CROSS/bin/$TRIPLA-readelf" "$S" "$CROSS/$TRIPLA/lib" <<'PY' || exit 1
import glob, os, re, shutil, subprocess, sys
readelf, disco, sysroot = sys.argv[1:4]
qui = {os.path.basename(f): f for f in glob.glob(disco + "/lib/*") + glob.glob(disco + "/qt5/lib/*.so.*")}
da_fare = [disco + "/vlc", disco + "/vlc-cache-gen"] + glob.glob(disco + "/qt5/plugins/*/*.so") \
    + glob.glob(disco + "/plugins/*/*.so")
visti, mancano = set(), set()
while da_fare:
    d = subprocess.run([readelf, "-d", da_fare.pop()], capture_output=True, text=True).stdout
    for n in re.findall(r"\(NEEDED\).*\[(.*)\]", d):
        if n in visti: continue
        visti.add(n)
        if n in qui: da_fare.append(qui[n]); continue
        p = os.path.join(sysroot, n)
        if os.path.exists(p):
            shutil.copy(os.path.realpath(p), os.path.join(disco, "lib", n)); da_fare.append(p)
        else:
            mancano.add(n)
if mancano:
    print("  [NO] librerie non trovate: %s" % " ".join(sorted(mancano))); sys.exit(1)
PY
# ! NOT small-shot.flac: 0.2 s, and VLC 3.0.24 decodes nothing of it, on
# Linux too (cvlc --aout afile writes an empty WAV); FFmpeg does. A second of
# 441 Hz instead
AUDIO="sound.ogg sin-441-1s-44100.flac small-shot.mp3"
VIDEO="gizmo.mp4 vp9cake.webm av1.mp4"
# the tone of the volume check: 2 s at 1 kHz, peak 10000, 48 kHz stereo
python3 - "$S/m/tono.wav" <<'PY'
import math, struct, sys
r = 48000
d = b"".join(struct.pack("<hh", v, v) for v in
             (int(10000 * math.sin(2 * math.pi * 1000 * i / r)) for i in range(2 * r)))
open(sys.argv[1], "wb").write(b"RIFF" + struct.pack("<I", 36 + len(d)) + b"WAVEfmt "
    + struct.pack("<IHHIIHH", 16, 1, 2, r, 4 * r, 4, 16) + b"data" + struct.pack("<I", len(d)) + d)
PY
for f in $AUDIO $VIDEO; do cp "firefox-main/dom/media/test/$f" "$S/m/"; done
# the reference for each audio file: FFmpeg on this machine
# (tools/prova_codec.sh builds one in cross_build/costruzione-ffmpeg/host)
FFMPEG=$(command -v ffmpeg || echo "$TC/costruzione-ffmpeg/host/ffmpeg")
MB=$(( $(du -sm "$S" | cut -f1) * 13 / 10 + 32 ))
IMG="$D/hd.img"; PART="$D/part.img"
/sbin/mke2fs -q -t ext2 -b 4096 -L vlc -d "$S" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
CD="$PWD/dist/exos.iso"; CPU=""
if [ "$BIT" = 64 ]; then CD="$PWD/dist/exos64.iso"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64; fi
WAV="$D/uscita.wav"
export EXOS_ISTANZA=vlc$BIT EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$IMG,format=raw,if=ide -audiodev wav,id=a0,path=$WAV -device AC97,audiodev=a0"
SER=/tmp/exos/serialvlc$BIT.txt; rm -f "$SER"
T=/tmp/exos/foto-vlc$BIT; rm -rf "$T"; mkdir -p "$T"
{
    echo "mount hd0p1 /disk@5"
    echo "echo attendo la fine dei lavori di avvio@25"
    echo "audio -i@40"
    echo "export HOME=/disk/casa@1"
    echo "export LD_LIBRARY_PATH=/disk/lib:/disk/qt5/lib@1"
    echo "export QT_PLUGIN_PATH=/disk/qt5/plugins@1"
    echo "export VLC_PLUGIN_PATH=/disk/plugins@1"
    # the plugins' cache, as a Linux package does after installing
    echo "/disk/vlc-cache-gen /disk/plugins > /disk/casa/cache-gen.log 2>&1@${PAUSA_CACHE:-60}"
    echo "export QT_QPA_FONTDIR=/disk/qt5/lib/fonts@1"
    echo "export QT_EXWIN_DEBUG=${QT_EXWIN_DEBUG:-0}@1"   # 1: what the exwin plugin does, in qt.log
    # Qt's own logging categories, e.g. QT_LOGGING_RULES=qt.widgets.painting=true
    [ -n "${QT_LOGGING_RULES:-}" ] && echo "export QT_LOGGING_RULES=$QT_LOGGING_RULES@1"
    for f in $AUDIO; do
        echo "echo VLC-AUDIO $f@1"
        # what the card plays for this file alone (capture 0: one at a time)
        echo "mon:wavcapture $D/ascolto-$f.wav a0@1"
        echo "/disk/vlc -I dummy --aout exos --no-video --play-and-exit /disk/m/$f@${PAUSA_AUDIO:-20}"
        echo "mon:stopcapture 0@1"
    done
    echo "echo VLC-AUDIO-FINE@1"
    # --loop: still playing at the screenshot (after the end the screen says
    # "no current item" and no volume)
    echo "/disk/vlc -I ncurses --aout exos --no-video --loop /disk/m/tono.wav@6"
    echo "foto:$T/ncurses.ppm@1"
    echo "z@n1"
    echo "z@n4"
    echo "foto:$T/ncurses-z.ppm@1"
    echo "q@n4"
    echo "echo VLC-NCURSES-FINE@1"
    echo "exwin@14"
    echo "key:alt-f5@4"
    echo "foto:$T/fondo.ppm@2"
    echo "key:alt-f1@2"
    for f in $VIDEO; do
        echo "echo VLC-VIDEO $f@1"
        r=4; [ "$f" = av1.mp4 ] && r=16      # one second long
        echo "/disk/vlc -I dummy --aout exos --vout exos --input-repeat=$r --play-and-exit /disk/m/$f &@1"
        # the files last a few seconds: the picture while it plays
        echo "key:alt-f5@3"
        echo "foto:$T/${f%%.*}.ppm@${PAUSA_VIDEO:-20}"
        echo "key:alt-f1@2"
    done
    # the Qt interface, with a video it plays inside its window
    echo "echo VLC-QT@1"
    # (--no-qt-privacy-ask: the first start's dialog covered the window).
    # gizmo.mp4 lasts 5.6 s: nine times is about 50 s of video, then the
    # window shows the cone again
    echo "/disk/vlc -I qt -vv --no-qt-privacy-ask --aout exos --vout exos --input-repeat=8 /disk/m/gizmo.mp4 > /disk/casa/qt.log 2>&1 &@1"
    echo "key:alt-f5@${PAUSA_QT:-30}"
    echo "foto:$T/qt.ppm@2"
    echo "foto:$T/qt2.ppm@${PAUSA_CONO:-60}"
    echo "foto:$T/cono.ppm@2"
    echo "key:alt-f1@2"
    echo "sync@3"
    echo "echo FINE-VLC@2"
} > "$D/passi.txt"
mapfile -t A < "$D/passi.txt"
timeout "${ATTESA:-1200}" python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"
esito=0

# --- audio --------------------------------------------------------------------
sed -n '/VLC-AUDIO /,/VLC-AUDIO-FINE/p' "$D/seriale.txt" | grep -a -i -E "error|errore" \
    | grep -a -v -E "no suitable interface module|globalhotkeys|XML reader not found" | head -10 > "$D/errori-audio.txt"
if [ -s "$D/errori-audio.txt" ]; then
    echo "  [NO] VLC ha scritto errori sull'audio:"; sed 's/^/        /' "$D/errori-audio.txt"; esito=1
fi
[ -x "$FFMPEG" ] || { echo "  [NO] audio: manca FFmpeg su questa macchina, niente da confrontare"; esito=1; }
for f in $AUDIO; do
    [ -x "$FFMPEG" ] || break
    "$FFMPEG" -v error -i "$S/m/$f" -f s16le -ac 1 -ar 48000 - > "$D/atteso-$f.raw" 2>/dev/null
    riga=$(python3 - "$D/atteso-$f.raw" "$D/ascolto-$f.wav" <<'E'
import struct, sys

def misura(d, rate, ch):
    """seconds, seconds with sound (10 ms blocks not all near 0), peak"""
    if not d:
        return 0.0, 0.0, 0
    v = struct.unpack('<%dh' % (len(d) // 2), d[:len(d) // 2 * 2])
    b = max(1, rate // 100) * ch
    pieni = sum(1 for i in range(0, len(v), b) if max(abs(x) for x in v[i:i + b]) > 64)
    return len(v) / ch / rate, pieni / 100.0, max(abs(x) for x in v)

a = misura(open(sys.argv[1], 'rb').read(), 48000, 1)
try:
    w = open(sys.argv[2], 'rb').read()
    ch, rate = struct.unpack('<HI', w[22:28])
    c = misura(w[44:], rate, ch)
except (OSError, struct.error):
    c = (0.0, 0.0, 0)
ok = c[0] >= 0.9 * a[0] > 0 and c[1] >= 0.9 * a[1] and (
    abs(c[2] - a[2]) <= 0.15 * a[2] if a[2] > 64 else c[2] <= 64)
print('%s %.2f %.2f %d %.2f %.2f %d' % ('OK' if ok else 'NO', c[0], c[1], c[2], a[0], a[1], a[2]))
E
)
    read -r va cs cp cm as ap am <<< "$riga"
    m="scheda ${cs:-0} s, ${cp:-0} s di suono, picco ${cm:-0}; il file ${as:-?} s, ${ap:-?} s, picco ${am:-?}"
    [ "${am:-0}" -le 64 ] 2>/dev/null && m="$m (il file e' muto)"
    if [ "$va" = OK ]; then
        echo "  [OK] audio $f: $m"
    else
        echo "  [NO] audio $f: $m ($D/ascolto-$f.wav)"; esito=1
    fi
done

# --- the text interface -----------------------------------------------------
python3 tools/schermo_testo.py -i "$T/ncurses.ppm" > "$D/ncurses.txt" 2>&1
if grep -q "VLC media player" "$D/ncurses.txt" && grep -q "tono.wav" "$D/ncurses.txt"; then
    echo "  [OK] vlc -I ncurses: l'intestazione e il file sullo schermo"
else
    echo "  [NO] vlc -I ncurses: lo schermo non lo mostra ($D/ncurses.txt)"; esito=1
fi
python3 tools/schermo_testo.py -i "$T/ncurses-z.ppm" > "$D/ncurses-z.txt" 2>&1
v1=$(sed -n 's/.*Volume *: *\([0-9]*\)%.*/\1/p' "$D/ncurses.txt" | head -1)
v2=$(sed -n 's/.*Volume *: *\([0-9]*\)%.*/\1/p' "$D/ncurses-z.txt" | head -1)
if [ "$v1" = 100 ] && [ "$v2" = 90 ]; then
    echo "  [OK] volume: 100%, e 90% dopo due z"
else
    echo "  [NO] volume: ${v1:-niente}% e poi ${v2:-niente}% invece di 100 e 90 ($D/ncurses.txt, $D/ncurses-z.txt)"; esito=1
fi
# what the card played: a second and more of the tone at 10000, then at 7290
SENTITO=$(python3 - "$WAV" <<'E'
import struct, sys
try:
    b = open(sys.argv[1], 'rb').read()
except OSError:
    print("0 0"); sys.exit()
ch, rate = struct.unpack('<HI', b[22:28])
d, s = b[44:], rate // 4 * 2 * ch
picchi = [max(abs(x) for x in struct.unpack('<%dh' % (s // 2), d[i:i + s]))
          for i in range(0, len(d) - s, s)]
def corsa(lo, hi):          # the longest run of quarter seconds in [lo, hi]
    m = n = 0
    for p in picchi:
        n = n + 1 if lo <= p <= hi else 0
        m = max(m, n)
    return m
print(corsa(9800, 10100), corsa(7150, 7450))
E
)
read -r PIENO BASSO <<< "$SENTITO"
if [ "${PIENO:-0}" -ge 4 ] && [ "${BASSO:-0}" -ge 4 ]; then
    echo "  [OK] volume: la scheda ha suonato il tono a 10000 e poi a 7290"
else
    echo "  [NO] volume: nel WAV ${PIENO:-0} quarti di secondo a 10000 e ${BASSO:-0} a 7290 (almeno 4 e 4)"; esito=1
fi
grep -aq "VLC-NCURSES-FINE" "$D/seriale.txt" || { echo "  [NO] vlc -I ncurses: q non l'ha chiuso"; esito=1; }

# --- video --------------------------------------------------------------------
for f in $VIDEO; do
    p="$T/${f%%.*}.ppm"; q="$D/foto/${f%%.*}.png"
    if [ ! -f "$p" ]; then echo "  [NO] $f: nessuna fotografia"; esito=1; continue; fi
    python3 tools/ppm2png.py "$p" "$q" > /dev/null 2>&1
    cambiati=$(python3 - "$T/fondo.ppm" "$p" <<'E'
import sys
def pixel(n):
    return open(n, 'rb').read().split(maxsplit=4)[4]  # P6, w, h, max, pixels
try:
    a, b = pixel(sys.argv[1]), pixel(sys.argv[2])
except (OSError, IndexError):
    print(0); sys.exit()
n = min(len(a), len(b)) // 3
diversi = sum(1 for i in range(0, n, 5) if a[3 * i:3 * i + 3] != b[3 * i:3 * i + 3])
print(diversi * 5 * 100 // max(n, 1))
E
)
    if [ "${cambiati:-0}" -ge 2 ]; then
        echo "  [OK] $f: finestra con l'immagine, $cambiati% dello schermo cambiato, $q"
    else
        echo "  [NO] $f: ${cambiati:-0}% dello schermo cambiato, $q"; esito=1
    fi
done
# --- the Qt interface -------------------------------------------------------
P1=$(/sbin/sfdisk -d "$IMG" 2>/dev/null | sed -n 's/.*start= *\([0-9]*\).*/\1/p' | head -1)
dd if="$IMG" of="$D/p1.img" bs=512 skip="${P1:-2048}" status=none
/sbin/debugfs -R "dump /casa/qt.log $D/qt.log" "$D/p1.img" > /dev/null 2>&1
/sbin/debugfs -R "dump /casa/cache-gen.log $D/cache-gen.log" "$D/p1.img" > /dev/null 2>&1
# --- the plugins' cache (voce 105, step 4) ---------------------------------
PD=$(/sbin/debugfs -R "stat /plugins/plugins.dat" "$D/p1.img" 2>/dev/null | sed -n 's/.*Size: *\([0-9]*\).*/\1/p' | head -1)
rm -f "$D/p1.img"
if [ "${PD:-0}" -gt 10000 ]; then
    echo "  [OK] vlc-cache-gen: plugins.dat di $PD byte"
else
    echo "  [NO] vlc-cache-gen: plugins.dat ${PD:-assente} ($D/cache-gen.log)"; esito=1
fi
if grep -aq 'using interface module "qt"' "$D/qt.log" 2>/dev/null; then
    c=$(python3 - "$T/fondo.ppm" "$T/qt.ppm" <<'E'
import sys
def pixel(n):
    return open(n, 'rb').read().split(maxsplit=4)[4]
try:
    a, b = pixel(sys.argv[1]), pixel(sys.argv[2])
except (OSError, IndexError):
    print(0); sys.exit()
n = min(len(a), len(b)) // 3
print(sum(1 for i in range(0, n, 5) if a[3 * i:3 * i + 3] != b[3 * i:3 * i + 3]) * 5 * 100 // max(n, 1))
E
)
    python3 tools/ppm2png.py "$T/qt.ppm" "$D/foto/qt.png" > /dev/null 2>&1
    if [ "${c:-0}" -ge 10 ]; then
        echo "  [OK] vlc -I qt: l'interfaccia Qt e' in uso, $c% dello schermo cambiato, $D/foto/qt.png"
    else
        echo "  [NO] vlc -I qt: in uso ma ${c:-0}% dello schermo cambiato, $D/foto/qt.png"; esito=1
    fi
    # ! THE VIDEO INSIDE THE INTERFACE (voce 105 step 5): vout_exos says
    # "child window N of M" when it is a child of Qt's video widget, and
    # "window N, WxH" when it made a window of its own
    if grep -aq 'child window [0-9]* of [0-9]' "$D/qt.log"; then
        echo "  [OK] vlc -I qt: il video in una finestra figlia dell'interfaccia ($(grep -a -o 'child window [0-9]* of [0-9]*, [0-9x]*' "$D/qt.log" | head -1))"
    else
        echo "  [NO] vlc -I qt: il registro non dice 'child window' ($D/qt.log)"; esito=1
        grep -a -i "exwin\|embedded\|vout window\|window" "$D/qt.log" | head -8 | sed 's/^/        /'
    fi
    if grep -aq 'debug: window [0-9]*, [0-9]*x[0-9]*, picture' "$D/qt.log"; then
        echo "  [NO] vlc -I qt: il video ha aperto anche una finestra sua"; esito=1
    fi
    # the video moves in the middle of VLC's window: two pictures 2 s apart
    m=$(python3 - "$T/qt.ppm" "$T/qt2.ppm" <<'E'
import re, sys
def leggi(n):
    t = open(n, 'rb').read()
    return int(t.split()[1]), t[re.match(rb'P6\s+\d+\s+\d+\s+\d+\s', t).end():]
try:
    w, a = leggi(sys.argv[1]); _, b = leggi(sys.argv[2])
except (OSError, AttributeError, IndexError, ValueError):
    print(0); sys.exit()
print(sum(1 for y in range(200, 360, 2) for x in range(300, 500, 2)
          if abs(a[3 * (y * w + x)] - b[3 * (y * w + x)]) > 24))
E
)
    python3 tools/ppm2png.py "$T/qt2.ppm" "$D/foto/qt2.png" > /dev/null 2>&1
    if [ "${m:-0}" -ge 200 ]; then
        echo "  [OK] vlc -I qt: il video si muove al centro della finestra di VLC ($m punti cambiati in 2 s)"
    else
        echo "  [NO] vlc -I qt: al centro della finestra niente si muove (${m:-0} punti), $D/foto/qt.png e qt2.png"; esito=1
    fi
    # the cone in the middle of VLC's window, the video over: orange
    # pixels. A child widget repainting alone (the cone fades in) never
    # reached the screen while the exwin plugin's winId() was 0 (19 November 2026)
    python3 tools/ppm2png.py "$T/cono.ppm" "$D/foto/cono.png" > /dev/null 2>&1
    n=$(python3 - "$T/cono.ppm" <<'E'
import re, sys
# ! THE HEADER BY ITS GRAMMAR: split() also ate the first bytes of the
# picture when they look like blanks, and the colours came out shifted
try:
    t = open(sys.argv[1], 'rb').read()
    d = t[re.match(rb'P6\s+\d+\s+\d+\s+\d+\s', t).end():]
except (OSError, AttributeError):
    print(0); sys.exit()
w = int(t.split()[1])
# only the middle of VLC's window (the video behind it may be orange too)
print(sum(1 for y in range(200, 360) for x in range(300, 500)
          if d[3 * (y * w + x)] > 200 and 90 < d[3 * (y * w + x) + 1] < 180 and d[3 * (y * w + x) + 2] < 80))
E
)
    if [ "${n:-0}" -ge 1000 ]; then
        echo "  [OK] vlc -I qt: il cono al centro della finestra ($n pixel arancioni)"
    else
        echo "  [NO] vlc -I qt: il cono non si vede (${n:-0} pixel arancioni), $D/foto/cono.png"; esito=1
    fi
else
    echo "  [NO] vlc -I qt: il registro non dice 'using interface module \"qt\"' ($D/qt.log)"; esito=1
    grep -a -i "qt\|interface" "$D/qt.log" 2>/dev/null | head -8 | sed 's/^/        /'
fi
grep -a -i -E "vlc|exos|FAULT|PF:" "$D/seriale.txt" | grep -a -v "^/disk/vlc\|VLC-" | head -20
exit $esito
