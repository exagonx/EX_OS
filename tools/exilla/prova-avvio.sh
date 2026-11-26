#!/bin/bash
# the build's trees are in <home of the user>/ex_os (tools/fuori.sh)
[ -n "${EXOS_FUORI:-}" ] || { EXOS_FUORI=$(getent passwd "${SUDO_USER:-$(id -un)}" | cut -d: -f6); EXOS_FUORI="${EXOS_FUORI:-$HOME}/ex_os"; }; export EXOS_FUORI
# =============================================================================
# tools/exilla/prova-avvio.sh - how long Firefox takes to start, cold (voce 120)
#
#     tools/exilla/prova-avvio.sh [GIRI] [NOME]       64 bit (dist/exos64.iso), 2 giri
#     EXOS_ARCH=i386 tools/exilla/prova-avvio.sh      32 bit
#     CD=/path/altro.iso tools/exilla/prova-avvio.sh  another CD (a baseline)
#
# The user, 25/11/2026: on the .253 Exilla starts in 90 s and more, the same
# Firefox under Linux on the same machine in 14, cold. Each round boots
# EX-OS afresh (nothing in the caches), starts ExWin and Firefox with
# several processes, as Exilla does, on a yellow page that writes PRONTA
# with dump() once drawn (two animation frames after load). A reader on the
# host takes the time from "AVVIO-FF" (echoed right before the start) to
# "PRONTA" on the serial line; then dmesg says what the file cache and the
# disk did. Results in cross_build/<macchina>/costruzione-prova[64]/avvio-NOME/.
#
# ! EVERY ROUND STARTS FROM THE SAME DISK (26/11): a pristine copy of the
# master image, never booted. The first boot makes Firefox's profile (a first
# start), the second boots the same copy again (cold caches, the profile
# there: the user's case on the .253). Reusing one disk across runs compared
# a first start with a second one - 272 s against 131 s, meaning nothing.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
R="$PWD"
GIRI="${1:-2}"
NOME="${2:-ultima}"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
if [ "${EXOS_ARCH:-x86_64}" = x86_64 ]; then
    SUFF=64; CD="${CD:-$R/dist/exos64.iso}"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
else
    SUFF="";  CD="${CD:-$R/dist/exos.iso}";   CPU=""; unset EXOS_QEMU
fi
O="${EXOS_FUORI}/cross_build/exilla-obj/gecko$SUFF"; P="$O/dist/firefox"
D="${EXOS_FUORI}/cross_build/$MACCHINA/costruzione-prova$SUFF"; OUT="$D/avvio-$NOME"
G="$D/avvio-disco"; MASTER="$D/avvio-campione.img"; IMG="$D/avvio-hd.img"; PART="$D/avvio-part.img"
rm -rf "$OUT"; mkdir -p "$D" /tmp/exos "$OUT"
[ -x "$P/firefox" ] || { echo "manca $P/firefox: prima tools/exilla/gecko-costruisci.sh package" >&2; exit 1; }

# the disk, made again only when the package is newer
if [ ! -f "$MASTER" ] || [ "$P/firefox" -nt "$MASTER" ] || [ "$0" -nt "$MASTER" ]; then
    rm -rf "$G"; mkdir -p "$G/font" "$G/p" "$G/c"
    cp -a "$P" "$G/firefox"; cp exwin/font/*.ttf "$G/font/"
    cp tools/exilla/exos-media.js "$G/firefox/defaults/pref/" 2>/dev/null
    {
        echo 'pref("datareporting.policy.firstRunURL", "");'
        echo 'pref("browser.dom.window.dump.enabled", true);'
        echo 'pref("browser.shell.checkDefaultBrowser", false);'
        echo 'pref("browser.startup.homepage_override.mstone", "ignore");'
    } > "$G/firefox/defaults/pref/avvio.js"
    cat > "$G/gialla.html" <<'EOF'
<!DOCTYPE html><html><head><meta charset="utf-8"><title>Avvio</title></head>
<body style="background:#fff3c4" onload="requestAnimationFrame(function(){requestAnimationFrame(function(){dump('PRONTA\n');});});">
<h1>AVVIO</h1></body></html>
EOF
    MB=$(( $(du -sm "$G" | cut -f1) * 13 / 10 + 256 ))
    rm -f "$MASTER" "$PART"
    fakeroot /sbin/mke2fs -q -t ext2 -b 4096 -L avvio -d "$G" "$PART" "${MB}M" || exit 1
    qemu-img create -f raw "$MASTER" "$((MB + 2))M" > /dev/null
    printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' | /sbin/sfdisk "$MASTER" > /dev/null 2>&1
    dd if="$PART" of="$MASTER" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"; rm -rf "$G"
    sync "$MASTER"
fi
cp "$MASTER" "$IMG"; sync "$IMG"
echo "CD: $CD" > "$OUT/cd.txt"; ls -la --time-style=+%F_%T "$CD" >> "$OUT/cd.txt"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=avvio$SUFF EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -smp 2 -nic none -drive file=$IMG,format=raw,if=ide,cache=none"
SER=/tmp/exos/serialavvio$SUFF.txt

for giro in $(seq 1 "$GIRI"); do
    rm -f "$SER"
    # the reader: host time of AVVIO-FF and of PRONTA on the serial line
    python3 -I - "$SER" > "$OUT/tempo$giro.txt" <<'PY' &
import sys, time, os
f = sys.argv[1]; t0 = None; fine = time.time() + 1500
while time.time() < fine:
    try:
        s = open(f, 'rb').read()
    except OSError:
        s = b''
    if t0 is None and b'AVVIO-FF' in s:
        t0 = time.time()
    if t0 is not None and b'PRONTA' in s[s.find(b'AVVIO-FF'):]:
        print("%.1f" % (time.time() - t0)); sys.exit(0)
    time.sleep(0.1)
print("oltre")
PY
    LETTORE=$!
    A=("mount hd0p1 /disk@5" "echo attendo la fine dei lavori di avvio@30")
    for v in HOME=/disk/c LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font; do A+=("export $v@1"); done
    A+=("exwin@14" "key:alt-f1@2"
        "echo AVVIO-FF; /disk/firefox/firefox -no-remote -profile /disk/p file:///disk/gialla.html &@${ATTESA:-240}"
        "key:alt-f5@2" "foto:$OUT/schermo$giro.ppm@1"
        "mona:$OUT/cpu$giro.txt=info registers -a@1" "mona:$OUT/cpu$giro.txt=info registers -a@1"
        "mona:$OUT/cpu$giro.txt=info registers -a@1" "key:alt-f1@2"
        "ps@3" "dmesg | grep -a 'FC:\|BKL\|SO:\|PF:\|FAULT'@4" "echo FINE-AVVIO@2")
    timeout 900 python3 tools/qemu_drive.py "${A[@]}" > "$OUT/qemu$giro.log" 2>&1
    # PRONTA never came: the reader stops with QEMU and says "oltre"
    kill $LETTORE 2>/dev/null; wait $LETTORE 2>/dev/null
    [ -s "$OUT/tempo$giro.txt" ] || echo "oltre" > "$OUT/tempo$giro.txt"
    # ! A FROZEN SYSTEM SAYS NOTHING ON THE SERIAL: no FINE-AVVIO, and then
    # cpu$giro.txt (three samples of every processor, from QEMU) says where
    # (ps answering counts too: its lines can split the FINE-AVVIO echo)
    grep -aq 'FINE-AVVIO\|blocc\.\|dorme ' "$SER" || echo "    ! FERMO: nessun FINE-AVVIO, registri in $OUT/cpu$giro.txt"
    tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$OUT/seriale$giro.txt"
    python3 tools/ppm2png.py "$OUT/schermo$giro.ppm" "$OUT/schermo$giro.png" > /dev/null 2>&1 && rm -f "$OUT/schermo$giro.ppm"
    echo "giro $giro: $(cat "$OUT/tempo$giro.txt") s fino a PRONTA"
    grep -a 'FC: pagine' "$OUT/seriale$giro.txt" | tail -1 | sed 's/^/    /'
    grep -a 'VFS: lucchetto' "$OUT/seriale$giro.txt" |
        sed 's/.*tenuto \([0-9]*\) ms dal PID [0-9]* (\([a-z_]*\)[^)]*).*/\2 \1/' |
        awk '{n[$1]++; t[$1]+=$2} END {for (k in n) printf "    VFS tenuto da %s: %d volte, %d ms\n", k, n[k], t[k]}'
done
