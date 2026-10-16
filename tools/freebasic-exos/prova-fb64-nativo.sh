#!/bin/bash
# =============================================================================
# tools/freebasic-exos/prova-fb64-nativo.sh — native fbc inside 64-bit EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/freebasic-exos/prova-fb64-nativo.sh
#
# Needs: make tools64 (the tree build-64/iso/exos: gcc, as, ld, libc...) and
# tools/freebasic-exos/prepara-fb64.sh (fbc, libfb). An ext2 disk is made on
# the host (mke2fs -d) holding that tree plus FreeBASIC laid out as the tools
# CD will hold it:
#     bin/fbc   include/freebasic/   lib/freebasic/exos-x86_64/
# In QEMU the 64-bit CD boots and the disk is mounted on /disk - not on
# /exos: EX-OS mounts only on a name that does not exist yet, and the CD has
# /exos. So the prefix is /disk, and the test also shows that gcc and fbc
# find their files from where they run. Then
#     gcc /disk/prova-gcc.c   fbc /disk/prova64.bas -x /disk/p64   /disk/p64
# The output is the serial log, printed between the two markers.
# =============================================================================
set -e
cd "$(dirname "$0")/../.."
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-fb64"
FB="$RADICE/cross_build/fb64/fb-build-exos64"
ALBERO="$RADICE/cross_build/fb64/albero"
T="$D/albero"

[ -d build-64/iso/exos/bin ] || { echo "manca build-64/iso/exos: make tools64" >&2; exit 1; }
[ -x "$FB/fbc" ] || { echo "manca $FB/fbc: tools/freebasic-exos/prepara-fb64.sh" >&2; exit 1; }

rm -rf "$T"; mkdir -p "$T"
cp -a build-64/iso/exos/. "$T/"
mkdir -p "$T/include/freebasic" "$T/lib/freebasic/exos-x86_64"
cp "$FB/fbc" "$T/bin/fbc"
cp -a "$ALBERO/inc/." "$T/include/freebasic/"
cp "$FB/libfb.a" "$FB/fbrt0.o" "$T/lib/freebasic/exos-x86_64/"
cp "$FB/fbextra.x" "$T/lib/freebasic/exos-x86_64/" 2>/dev/null || true
cp tools/freebasic-exos/prova64.bas "$T/prova64.bas"
cp build-64/iso/prova-gcc.c "$T/prova-gcc.c"

IMG="$D/fb64-hd.img"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 400M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' \
    | "$(command -v sfdisk || echo /sbin/sfdisk)" "$IMG" > /dev/null 2>&1
"$(command -v mke2fs || echo /sbin/mke2fs)" -q -t ext2 -b 1024 -L prova -d "$T" \
    -E offset=1048576 "$IMG" 398M

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_QEMU=qemu-system-x86_64 EXOS_ISTANZA=fb64 EXOS_NO_FLOPPY=1
export EXOS_CDROM="$RADICE/dist/exos64.iso"
export EXOS_QEMU_EXTRA="$KVM -cpu qemu64 -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialfb64.txt
rm -f "$SER"
timeout 600 python3 tools/qemu_drive.py "mount hd0p1 /disk@5" "export PATH=/disk/bin:/bin@1" \
    "echo INIZIO-FB64@1" "gcc /disk/prova-gcc.c -o /disk/pg@60" "/disk/pg@5" "fbc -version@8" \
    "fbc /disk/prova64.bas -x /disk/p64@150" "/disk/p64@10" \
    "echo FINE-FB64@2" > "$D/qemu.log" 2>&1 || true
tr -d '\r' < "$SER" 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | \
    sed -n '/INIZIO-FB64$/,/FINE-FB64$/p' | grep -v "^ex-os:" | grep -v "^\[INFO\]"
