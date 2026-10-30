#!/bin/bash
# =============================================================================
# tools/exilla/prova-chiusura.sh — how long Firefox takes to close (voce 38)
#
#     EXOS_ARCH=x86_64 tools/exilla/prova-chiusura.sh [dir]
#
# On a COPY of the disk from prova-gecko.sh: network on, Firefox up for ATTESA
# seconds (default 300, as the laptop: Remote Settings is working), then Ctrl+Q
# and a `ps` every 30 s for GIRI rounds (default 24). The question is whether
# the process goes away, and how soon: the log of the laptop stopped on the
# shutdown barrier, with Places closing SQLite (PlacesShutdownProgress 5).
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
D="${1:-/tmp/exos/chiusura}"
SUFF=""; [ "$EXOS_ARCH" = x86_64 ] && SUFF=64
S="$PWD/cross_build/$MACCHINA/costruzione-prova$SUFF/gecko-hd.img"
[ -f "$S" ] || { echo "manca $S: tools/exilla/prova-gecko.sh" >&2; exit 1; }
rm -rf "$D"; mkdir -p "$D"
cp --sparse=always "$S" "$D/hd.img"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
CD="$PWD/dist/exos.iso"; CPU=""
if [ "$EXOS_ARCH" = x86_64 ]; then
    CD="$PWD/dist/exos64.iso"; CPU="-cpu qemu64"; export EXOS_QEMU=qemu-system-x86_64
fi
export EXOS_ISTANZA=chiusura EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM $CPU -drive file=$D/hd.img,format=raw,if=ide -netdev user,id=u0 -device e1000,netdev=u0"

A=("mount hd0p1 /disk@5")
for v in MOZ_FORCE_DISABLE_E10S=1 HOME=/disk/casa LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font \
         EXWIN_LOG=/disk/firefox.log; do A+=("export $v@1"); done
A+=("host example.com@12" "exwin@14" "key:alt-f1@2"
    "/disk/firefox/firefox -no-remote -profile /disk/profilo file:///disk/prova.html &@3"
    "key:alt-f5@${ATTESA:-300}" "foto:$D/prima.ppm@2"
    "key:ctrl-q@5" "foto:$D/dopo-ctrl-q.ppm@1" "key:alt-f1@2")
for i in $(seq 1 "${GIRI:-24}"); do A+=("ps@30"); done
A+=("ls -a -l /disk/profilo@4" "echo FINE-CHIUSURA@2")
timeout $(( ${ATTESA:-300} + 30 * ${GIRI:-24} + 300 )) python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
for p in "$D"/*.ppm; do python3 tools/ppm2png.py "$p" "${p%.ppm}.png" > /dev/null 2>&1; done
grep -a -c 'firefox' "$D/qemu.log"
grep -a -n '/disk/firefox/firefox\|FINE-CHIUSURA' "$D/qemu.log" | tail -30 | cut -c1-80
