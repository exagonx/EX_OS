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
# What is [OK]: the card played at least as many seconds as the audio files
# last, VLC wrote no error for the audio, and every screenshot of a video has
# a picture in it (at least 5% of the screen differs from the empty desktop).
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
AUDIO="sound.ogg small-shot.flac small-shot.mp3"
VIDEO="gizmo.mp4 vp9cake.webm av1.mp4"
for f in $AUDIO $VIDEO; do cp "firefox-main/dom/media/test/$f" "$S/m/"; done
# how long the audio lasts, as FFmpeg on this machine reads it
# (tools/prova_codec.sh builds one in cross_build/costruzione-ffmpeg/host)
FFMPEG=$(command -v ffmpeg || echo "$TC/costruzione-ffmpeg/host/ffmpeg")
DURATA=0
if [ -x "$FFMPEG" ]; then
    for f in $AUDIO; do
        d=$("$FFMPEG" -hide_banner -i "$S/m/$f" 2>&1 | sed -n 's/.*Duration: \([0-9:.]*\),.*/\1/p' \
            | awk -F: '{ print $1 * 3600 + $2 * 60 + $3 }')
        DURATA=$(python3 -c "print($DURATA + ${d:-0})")
    done
fi
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
    for f in $AUDIO; do
        echo "echo VLC-AUDIO $f@1"
        echo "/disk/vlc -I dummy --aout exos --no-video --play-and-exit /disk/m/$f@${PAUSA_AUDIO:-20}"
    done
    echo "echo VLC-AUDIO-FINE@1"
    echo "exwin@14"
    echo "key:alt-f5@4"
    echo "foto:$T/fondo.ppm@2"
    echo "key:alt-f1@2"
    for f in $VIDEO; do
        echo "echo VLC-VIDEO $f@1"
        echo "/disk/vlc -I dummy --aout exos --vout exos --play-and-exit /disk/m/$f &@1"
        echo "key:alt-f5@4"
        echo "foto:$T/${f%%.*}.ppm@${PAUSA_VIDEO:-15}"
        echo "key:alt-f1@2"
    done
    echo "echo FINE-VLC@2"
} > "$D/passi.txt"
mapfile -t A < "$D/passi.txt"
timeout "${ATTESA:-1200}" python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"
esito=0

# --- audio --------------------------------------------------------------------
sed -n '/VLC-AUDIO /,/VLC-AUDIO-FINE/p' "$D/seriale.txt" | grep -a -i -E "error|errore" \
    | grep -a -v -E "no suitable interface module|globalhotkeys" | head -10 > "$D/errori-audio.txt"
SUONATI=$(python3 - "$WAV" <<'E'
import sys, struct
try:
    b = open(sys.argv[1], 'rb').read()
except OSError:
    print(0); sys.exit()
ch, rate = struct.unpack('<HI', b[22:28])
d = b[44:]
n = len(d) // (2 * ch)
# seconds with sound in them (a block of 10 ms is "sound" if not all near 0)
blocco = max(1, rate // 100) * 2 * ch
pieni = sum(1 for i in range(0, len(d) - blocco, blocco)
            if max(abs(v) for v in struct.unpack('<%dh' % (blocco // 2), d[i:i + blocco])) > 64)
print('%.2f %.2f %d' % (n / rate, pieni / 100.0, rate))
E
)
read -r TOT PIENI RATE <<< "$SUONATI"
echo "  scheda: ${TOT:-0} s scritti, ${PIENI:-0} s con suono, a ${RATE:-?} Hz; file audio: ${DURATA} s"
if [ -s "$D/errori-audio.txt" ]; then
    echo "  [NO] VLC ha scritto errori sull'audio:"; sed 's/^/        /' "$D/errori-audio.txt"; esito=1
elif python3 -c "import sys; sys.exit(0 if float('${PIENI:-0}') >= 0.9 * float('$DURATA') > 0 else 1)"; then
    echo "  [OK] audio: Vorbis, FLAC e MP3 suonati dalla scheda"
else
    echo "  [NO] audio: la scheda ha suonato meno dei file"; esito=1
fi

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
    if [ "${cambiati:-0}" -ge 5 ]; then
        echo "  [OK] $f: finestra con l'immagine, $cambiati% dello schermo cambiato, $q"
    else
        echo "  [NO] $f: ${cambiati:-0}% dello schermo cambiato, $q"; esito=1
    fi
done
grep -a -i -E "vlc|exos|FAULT|PF:" "$D/seriale.txt" | grep -a -v "^/disk/vlc\|VLC-" | head -20
exit $esito
