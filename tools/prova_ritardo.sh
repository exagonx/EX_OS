#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/prova_ritardo.sh - how late everybody else gets while one program
# reads a lot from the disk (voce 117: the mouse stutters while Firefox loads)
#
#     tools/prova_ritardo.sh [CPU] [MB/s]             32 bit
#     EXOS_ARCH=x86_64 tools/prova_ritardo.sh [CPU] [MB/s]
#
# CPU = processors given to QEMU (default 2), MB/s = the disk's read speed
# (default 12: the .253 in PIO, voce 27; 0 = as fast as the host). The disk
# is also held to 150 reads a second, a seek like a real platter's.
# Two rounds, tools/prove/ritardo.c measuring every 5 ms a sleep, a stat("/")
# and a byte through a pipe:
#   A  a child reads the whole Firefox package (the first second is quiet:
#      the baseline);
#   B  ExWin up, Firefox started cold while ritardo measures.
# The lines "RIT ..." per second go to $OUT/riassunto.txt. Nothing is judged:
# it is a measure, to compare before and after a change.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
R="$PWD"
NCPU="${1:-2}"; MBS="${2:-12}"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "$EXOS_ARCH" = x86_64 ]; then
    SUFF=64; CD="$R/dist/exos64.iso"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
    GCC="${EXOS_FUORI}/cross_build/$MACCHINA/exos-cross64/bin/x86_64-exos-gcc"
else
    SUFF="";  CD="$R/dist/exos.iso";   CPU=""
    GCC="${EXOS_FUORI}/cross_build/$MACCHINA/exos-cross/bin/i386-exos-gcc"
fi
O="${EXOS_FUORI}/cross_build/exilla-obj/gecko$SUFF"; P="$O/dist/firefox"
D="/tmp/exos-prove/ritardo$SUFF"; OUT="$D/esito-${NCPU}cpu-${MBS}mbs"
G="$D/disco"; IMG="$D/hd.img"; PART="$D/part.img"
[ -x "$P/firefox" ] || { echo "manca $P/firefox: prima tools/exilla/prova-processi.sh" >&2; exit 1; }
mkdir -p "$D" /tmp/exos; rm -rf "$OUT"; mkdir -p "$OUT"

rm -rf "$G"; mkdir -p "$G/font"
"$GCC" -O2 -Wall -o "$G/ritardo" tools/prove/ritardo.c -lpthread || { echo "ritardo non compila" >&2; exit 1; }
cp -a "$P" "$G/firefox"; cp exwin/font/*.ttf "$G/font/"
echo 'pref("datareporting.policy.firstRunURL", "");' > "$G/firefox/defaults/pref/ritardo.js"
MB=$(( $(du -sm "$G" | cut -f1) * 13 / 10 + 128 ))
rm -f "$IMG" "$PART"
fakeroot /sbin/mke2fs -q -t ext2 -b 4096 -L ritardo -d "$G" "$PART" "${MB}M" || exit 1
qemu-img create -f raw "$IMG" "$((MB + 2))M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"
sync "$IMG"

LIM=""
[ "$MBS" != 0 ] && LIM=",throttling.bps-read=$((MBS * 1000000)),throttling.iops-read=150"
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=ritardo$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -smp $NCPU -nic none -drive file=$IMG,format=raw,if=ide$LIM"
SER=/tmp/exos/serialritardo$SUFF.txt; rm -f "$SER"
# ! EVERY FILE OF THE PACKAGE, NOT libxul ALONE: the cold start reads them all
LISTA=$(cd "$G" && find firefox -type f -size +1M | sed 's#^#/disk/#' | tr '\n' ' ')
# ! ONE BOOT PER ROUND: after A the package sits in the page cache, and B
# would measure a warm start.
corsa() {    # name, then the steps after the mount
    local n="$1"; shift
    rm -f "$SER"
    timeout 900 python3 tools/qemu_drive.py "mount hd0p1 /disk@5" \
        "echo attendo la fine dei lavori di avvio@30" "$@" > "$OUT/qemu-$n.log" 2>&1
    tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$OUT/seriale-$n.txt"
}
corsa A "echo RIT-A-INIZIO@1" "/disk/ritardo 40 $LISTA@60" "echo RIT-A-FINE@2"
B=()
for v in HOME=/disk/c2 LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font; do B+=("export $v@1"); done
B+=("mkdir /disk/p2@2" "mkdir /disk/c2@2" "exwin@14" "key:alt-f1@2"
    "/disk/ritardo 90 > /disk/b.txt &@2"
    "/disk/firefox/firefox -no-remote -profile /disk/p2 &@3" "key:alt-f5@3"
    "foto:$OUT/b1.ppm@40" "foto:$OUT/b2.ppm@50" "key:alt-f1@3"
    "echo RIT-B-INIZIO@1" "cat /disk/b.txt@3" "echo RIT-B-FINE@2")
corsa B "${B[@]}"
{
    echo "== $SUFF bit, $NCPU processori, disco ${MBS} MB/s =="
    echo "-- A: un figlio legge il pacchetto di Firefox (il secondo 0 e' quieto)"
    sed -n '/^RIT-A-INIZIO/,/^RIT-A-FINE/p' "$OUT/seriale-A.txt" | grep -a "^RIT [0-9]\|^RIT fine\|^RIT letti"
    echo "-- B: Firefox parte a freddo con la scrivania accesa"
    sed -n '/^RIT-B-INIZIO/,/^RIT-B-FINE/p' "$OUT/seriale-B.txt" | grep -a "^RIT [0-9]\|^RIT fine"
} > "$OUT/riassunto.txt"
for p in "$OUT"/*.ppm; do
    [ -f "$p" ] && python3 tools/ppm2png.py "$p" "${p%.ppm}.png" > /dev/null 2>&1 && rm -f "$p"
done
cat "$OUT/riassunto.txt"
