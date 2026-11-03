#!/bin/bash
# =============================================================================
# tools/prova_codec.sh - FFmpeg decodes inside EX-OS what it decodes on Linux
#
#     tools/prova_codec.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_codec.sh [dir]    64 bit
#
# The samples are Firefox's own test files: H.264 + AAC, VP8, VP9, AV1, Opus,
# Vorbis, FLAC, MP3, AAC. Every decoded frame gets an MD5 (framemd5) inside
# EX-OS, and the same FFmpeg archive built for Linux, without assembly, must
# give the same numbers for the VIDEO. Audio is compared by packet count only:
# float decoders round differently with assembly and x87 (measured: a few
# packets at 64 bit, up to a quarter at 32), and MD5 cannot tell how much. AV1 has no decoder of its own on Linux here (dav1d
# is EX-OS's): for that one, at least one frame and no error.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/codec-$ARCH}"
rm -rf "$D"; mkdir -p "$D/albero/m" "$D/albero/r" "$D/linux"
SBIN=/usr/sbin
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; BIT=64; export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=1024M; EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; BIT=32; export EXOS_RAM=512M; EXTRA=""
fi
FF=cross_build/ffmpeg-exos$BIT/exos/bin/ffmpeg
[ -x "$FF" ] || { echo "manca $FF: tools/ffmpeg-exos/costruisci.sh" >&2; exit 1; }
CAMPIONI="gizmo.mp4 gizmo.webm vp9cake.webm bipbop_short_vp8.webm av1.mp4 small-shot.flac small-shot.mp3 small-shot.m4a sound.ogg"

# the same archive, for Linux
H=$PWD/cross_build/costruzione-ffmpeg/host
if [ ! -x "$H/ffmpeg" ]; then
    mkdir -p "$H"
    tar -xf cross_build/terze-parti/ffmpeg/ffmpeg-9.0.2.tar.xz -C "$H"
    ( cd "$H" && ffmpeg-9.0.2/configure --disable-asm --disable-doc --disable-autodetect \
        --disable-network --disable-ffplay --disable-ffprobe && make -j8 ) > "$H/costruisci.log" 2>&1 \
        || { tail -5 "$H/costruisci.log"; exit 1; }
fi

{
    for c in $CAMPIONI; do
        cp "firefox-main/dom/media/test/$c" "$D/albero/m/"
        echo "/a/ffmpeg -nostdin -hide_banner -loglevel error -threads 1 -bitexact -i /a/m/$c -map 0 -flags +bitexact -c:a pcm_s16le -f framemd5 /a/r/$c.md5"
        "$H/ffmpeg" -nostdin -hide_banner -loglevel error -threads 1 -bitexact \
            -i "$D/albero/m/$c" -map 0 -flags +bitexact -c:a pcm_s16le -f framemd5 "$D/linux/$c.md5" 2> "$D/linux/$c.err"
    done
    echo "echo CODEC-FINITO"
} > "$D/albero/fa.sh"
cp "$FF" "$D/albero/ffmpeg"; chmod 755 "$D/albero/ffmpeg"
IMG="$D/hd.img"; P="$D/p.img"
$SBIN/mke2fs -q -F -t ext2 -b 4096 -m 0 -L codec -d "$D/albero" "$P" 200M || exit 1
qemu-img create -f raw "$IMG" 202M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | $SBIN/sfdisk "$IMG" > /dev/null 2>&1
dd if="$P" of="$IMG" bs=1M seek=1 conv=notrunc status=none
EXOS_ISTANZA=codec$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    EXOS_QEMU_EXTRA="$EXTRA -enable-kvm -drive file=$IMG,format=raw,if=ide" \
    timeout 1200 python3 tools/qemu_drive.py "mount hd0p1 /a@5" "echo attendo@25" \
    "/bin/sh /a/fa.sh@~${TEMPO:-900}" "umount /a@5" > "$D/qemu.log" 2>&1
dd if="$IMG" of="$P" bs=1M skip=1 status=none
esito=0
for c in $CAMPIONI; do
    $SBIN/debugfs -R "dump /r/$c.md5 $D/$c.md5" "$P" > /dev/null 2>&1
    n=$(grep -vc '^#' "$D/$c.md5" 2>/dev/null); n=${n:-0}
    if [ "$c" = av1.mp4 ]; then
        [ "$n" -gt 0 ] && echo "  [OK] $c: $n fotogrammi decodificati (AV1, senza riferimento)" \
            || { echo "  [NO] $c: niente fotogrammi"; esito=1; }
    else
        # stream 0 or 1, whichever is video by the header of the Linux file
        v=$(sed -n 's/^#media_type \([01]\): video/\1/p' "$D/linux/$c.md5")
        m=$(grep -vc '^#' "$D/linux/$c.md5")
        if [ "$n" != "$m" ] || [ "$n" = 0 ]; then
            echo "  [NO] $c: $n pacchetti contro $m su Linux"; esito=1
        elif [ -n "$v" ] && ! cmp -s <(grep "^$v," "$D/$c.md5") <(grep "^$v," "$D/linux/$c.md5"); then
            echo "  [NO] $c: video diverso da Linux"; esito=1
        else
            echo "  [OK] $c: $n pacchetti${v:+, video identico a Linux}"
        fi
    fi
done
[ $esito = 0 ] && echo "=== $ARCH: A POSTO ===" || echo "=== $ARCH: NON A POSTO ==="
exit $esito
