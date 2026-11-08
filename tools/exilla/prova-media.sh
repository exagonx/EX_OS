#!/bin/bash
# =============================================================================
# tools/exilla/prova-media.sh - Firefox plays audio and video inside EX-OS
#
#     tools/exilla/prova-media.sh                      32 bit
#     EXOS_ARCH=x86_64 tools/exilla/prova-media.sh     64 bit
#
# QEMU gets an AC'97 card (`audio -i` finds it), Firefox a page that plays,
# one after the other (the driver serves one client at a time), Firefox's own
# test files: Ogg Vorbis, FLAC, MP3, AAC, and video in VP8, VP9, AV1 and
# H.264. For each one the page writes to the serial line, with dump(), where
# the playback got to, the file's length, the decoded frames and any error.
# A line is [OK] when time went forward with no error, to at least one second
# or to 90% of a shorter file, and, for a video, frames were decoded.
#     FILE="av1.mp4" ATTESA_FILE=30 ...   only some files, seconds for each
#     MOZ_LOG=FFmpegVideo:4,MediaDecoder:4 ...   Firefox's logs, on the serial line
# Run after tools/exilla/prova-gecko.sh, which makes the package.
# QEMU_PIU="-smp 2" adds QEMU options: 2 processors, as on the laptop (the
# threads of Firefox run together from kernel 0.321).
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$PWD/cross_build/$MACCHINA"
O="$PWD/cross_build/exilla-obj/gecko"; D="$B/costruzione-prova"; SUFF=""
if [ "$EXOS_ARCH" = x86_64 ]; then O="$PWD/cross_build/exilla-obj/gecko64"; D="$B/costruzione-prova64"; SUFF=64; fi
P="$O/dist/firefox"
[ -x "$P/firefox" ] || { echo "manca $P/firefox: prima tools/exilla/prova-gecko.sh" >&2; exit 1; }
S="$D/media-disco"
rm -rf "${S:?}"; mkdir -p "$S/profilo" "$S/casa" "$S/font" "$S/m"
cp -a "$P" "$S/firefox"; cp exwin/font/*.ttf "$S/font/"
FILE="${FILE:-sound.ogg small-shot.flac small-shot.mp3 small-shot.m4a bipbop_short_vp8.webm vp9cake.webm av1.mp4 gizmo.mp4}"
for f in $FILE; do cp "firefox-main/dom/media/test/$f" "$S/m/"; done
# the system's FFmpeg (tools/ffmpeg-exos, shared since 3 November 2026):
# Firefox opens libavcodec.so.63 with dlopen for H.264 and AAC. Only the
# files named by their soname (no symbolic links on EX-OS).
BITS=32; [ "$EXOS_ARCH" = x86_64 ] && BITS=64
mkdir -p "$S/lib"
for l in ffmpeg libvpx dav1d opus vorbis ogg zlib; do
    cp cross_build/$l-exos$BITS/exos/lib/lib*.so.[0-9]* "$S/lib/" 2>/dev/null
done
echo "  librerie per Firefox: $(ls "$S/lib" | tr '\n' ' ')"
cat > "$S/profilo/user.js" <<'EOF'
user_pref("browser.dom.window.dump.enabled", true);
user_pref("media.autoplay.default", 0);
user_pref("media.autoplay.blocking_policy", 0);
EOF
{
cat <<'EOF'
<!DOCTYPE html><html><head><meta charset="utf-8"><title>media</title></head><body><script>
var ATTESA_MS = __ATTESA_MS__;
var file = [
EOF
for f in $FILE; do echo "  \"$f\","; done
cat <<'EOF'
];
function prova(i) {
  if (i >= file.length) { dump("MEDIA-FINE\n"); return; }
  var f = file[i], v = /\.(webm|mp4)$/.test(f) && f != "small-shot.m4a";
  var e = document.createElement(v ? "video" : "audio");
  var t0 = 0, err = "";
  e.onerror = function () { err = e.error ? ("errore " + e.error.code + " " + e.error.message) : "errore"; };
  document.body.appendChild(e);
  e.src = "m/" + f;
  e.play().catch(function (x) { err = err || ("play: " + x.name); });
  setTimeout(function () {
    var q = v && e.getVideoPlaybackQuality ? e.getVideoPlaybackQuality().totalVideoFrames : -1;
    dump("MEDIA " + f + " tempo=" + e.currentTime.toFixed(2) + " durata=" + (e.duration || 0).toFixed(2) +
         " finito=" + (e.ended ? 1 : 0) + " pronto=" + e.readyState +
         " fotogrammi=" + q + " " + (err || "ok") + "\n");
    e.pause(); e.removeAttribute("src"); e.load(); e.remove();
    setTimeout(function () { prova(i + 1); }, 1500);
  }, ATTESA_MS);
}
dump("MEDIA-INIZIO\n");
prova(0);
</script></body></html>
EOF
} | sed "s/__ATTESA_MS__/${ATTESA_FILE:-8}000/" > "$S/media.html"
MB=$(( $(du -sm "$S" | cut -f1) * 13 / 10 + 128 ))
IMG="$D/media-hd.img"; PART="$D/media-part.img"
rm -f "$IMG" "$PART"
/sbin/mke2fs -q -t ext2 -b 4096 -L media -d "$S" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
CD="$PWD/dist/exos.iso"; CPU=""
if [ "$EXOS_ARCH" = x86_64 ]; then CD="$PWD/dist/exos64.iso"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64; fi
export EXOS_ISTANZA=media$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU${QEMU_PIU:+ $QEMU_PIU} -drive file=$IMG,format=raw,if=ide -audiodev wav,id=a0,path=$D/media-uscita.wav -device AC97,audiodev=a0"
SER=/tmp/exos/serialmedia$SUFF.txt; rm -f "$SER" "$D/media-uscita.wav"
timeout "${ATTESA:-1500}" python3 tools/qemu_drive.py "mount hd0p1 /disk@5" "echo attendo@25" "audio -i@40" "audio@5" \
    "export MOZ_FORCE_DISABLE_E10S=1@1" "export MOZ_HEADLESS=1@1" \
    "export HOME=/disk/casa@1" "export LD_LIBRARY_PATH=/disk/lib@1" "export MOZ_LOG=${MOZ_LOG:-}@1" "export LANG=it_IT.UTF-8@1" "export EXILLA_FONTS=/disk/font@1" \
    "/disk/firefox/firefox --headless -no-remote -profile /disk/profilo --window-size 800,600 file:///disk/media.html &@${SECONDI:-600}" \
    "echo FINE-MEDIA@2" > "$D/media-1.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/media-seriale.txt"
grep -a -E "^audio|scheda|AC'97|ac97" "$D/media-seriale.txt" | head -6
esito=0
for f in $FILE; do
    r=$(grep -a "^MEDIA $f " "$D/media-seriale.txt" | tail -1)
    t=$(echo "$r" | sed -n 's/.*tempo=\([0-9.]*\).*/\1/p'); q=$(echo "$r" | sed -n 's/.*fotogrammi=\(-\?[0-9]*\).*/\1/p')
    d=$(echo "$r" | sed -n 's/.*durata=\([0-9.]*\).*/\1/p')
    # time went forward: to the end of the file, or at least one second
    avanti=$(python3 -c "import sys; t, d = float('${t:-0}'), float('${d:-0}'); print(1 if t > 0 and (t >= 1 or t >= 0.9 * d) else 0)")
    if [ -n "$r" ] && echo "$r" | grep -q " ok$" && [ "$avanti" = 1 ] && [ "${q:-0}" != 0 ]; then
        echo "  [OK] $r"
    else
        echo "  [NO] ${r:-MEDIA $f: nessuna riga}"; esito=1
    fi
done
[ -s "$D/media-uscita.wav" ] && echo "  uscita della scheda: $(du -h "$D/media-uscita.wav" | cut -f1) in $D/media-uscita.wav"
exit $esito
