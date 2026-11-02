#!/bin/bash
# =============================================================================
# tools/exilla/prova-riapertura.sh - Firefox: first start, close, second start
#
#     tools/exilla/prova-riapertura.sh [dir]      64 bit
#
# The laptop's report of 1 November 2026: the profile is created, Firefox is
# closed, and at the second start it no longer works. A fresh ext4 disk with
# 4 KB blocks like the laptop's, Firefox from prova-gecko.sh, an empty profile.
# Screens go to the dir, the log ends with /proc/disco of each stage and the
# commit counts printed at unmount. ATTESA1/ATTESA2 are the seconds of each run.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
D="${1:-$HOME/cross_build/prove/riapertura}"
A1="${ATTESA1:-420}"; A2="${ATTESA2:-300}"
T="$PWD/cross_build/nasi5deb/costruzione-prova64/gecko-disco"
[ -d "$T/firefox" ] || { echo "manca $T" >&2; exit 1; }
rm -rf "$D"; mkdir -p "$D"
P="$D/p1.img"; IMG="$D/hd.img"
# ! NOT YET A VALID TEST: on 2 November 2026 Firefox stopped here on "Profile
# Missing" on this ext4 disk, with the tree owned by root too. Cause not found.
fakeroot sh -c "cp -a '$T' '$D/albero' && chown -R 0:0 '$D/albero' && \
    /usr/sbin/mke2fs -q -F -t ext4 -b 4096 -m 0 -L sistema -d '$D/albero' '$P' 2600M" || exit 1
rm -rf "$D/albero"
qemu-img create -f raw "$IMG" 2700M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%d, type=83\n' $((2600 * 2048)) \
    | /usr/sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$P" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$P"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_QEMU=qemu-system-x86_64 EXOS_ISTANZA=riap EXOS_NO_FLOPPY=1
export EXOS_CDROM="$PWD/dist/exos64.iso" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM -cpu qemu64 -drive file=$IMG,format=raw,if=ide -netdev user,id=u0 -device e1000,netdev=u0"

A=("mount hd0p1 /disk@6" "netdetect -c@12" "dhcp@25")
for v in MOZ_FORCE_DISABLE_E10S=1 HOME=/disk/casa LANG=it_IT.UTF-8 EXILLA_FONTS=/disk/font \
         EXWIN_LOG=/disk/firefox.log; do A+=("export $v@1"); done
FF="/disk/firefox/firefox -no-remote -profile /disk/profilo file:///disk/prova.html"
A+=("exwin@14" "key:alt-f1@2" "cat /proc/disco@2" "$FF &@3"
    "key:alt-f5@$A1" "foto:$D/1-aperto.ppm@2"
    "key:ctrl-q@5" "foto:$D/1-ctrl-q.ppm@1" "key:ret@60" "foto:$D/1-chiuso.ppm@1" "key:alt-f1@2"
    "ps@4" "cat /proc/disco@2" "ls -a -l /disk/profilo@4"
    "$FF &@3" "key:alt-f5@$A2" "foto:$D/2-aperto.ppm@2" "key:alt-f1@2"
    "ps@4" "cat /proc/disco@2" "dmesg | grep ext@4" "poweroff@60")
timeout $((A1 + A2 + 900)) python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
for p in "$D"/*.ppm; do python3 tools/ppm2png.py "$p" "${p%.ppm}.png" > /dev/null 2>&1; done
cp /tmp/exos/serialriap.txt "$D/seriale.txt" 2>/dev/null
grep -a 'commit\|^hd0' "$D/seriale.txt" "$D/qemu.log" | tail -8
