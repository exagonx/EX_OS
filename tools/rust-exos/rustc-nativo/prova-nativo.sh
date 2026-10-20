#!/bin/bash
# =============================================================================
# tools/rust-exos/rustc-nativo/prova-nativo.sh — rustc running INSIDE EX-OS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/rust-exos/rustc-nativo/prova-nativo.sh
#
# @RUST-64 R64-2 step e. Needs costruisci-exos.sh (the rustc for EX-OS and
# the std for x86_64-unknown-exos) and make tools64 (gcc, as, ld, libc for
# the native link). An ext2 disk holds the tools tree with rustc in bin/ and
# the std in lib/rustlib/x86_64-unknown-exos/lib, so rustc finds its sysroot
# from where it runs (dladdr on itself: lib/libc.c). In QEMU (64-bit CD,
# 2 GB) the disk is mounted on /disk; rustc -vV, then rustc compiles
# ciao.rs and links it with the native gcc, and the program runs. Then the
# procedural macros: macro.rs becomes a .so (the native gcc -shared, with
# crtbeginS/crtendS), usa.rs is compiled with it (a function-like and a
# derive macro) and runs, and rotto.rs - whose macro panics inside the .so -
# must end in "proc macro panicked", not in a crash.
# =============================================================================
set -e
cd "$(dirname "$0")/../../.."
RADICE="$PWD"
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$RADICE/cross_build/$MACCHINA/rustc-build"
RUSTC="$B/x86_64-unknown-exos/stage2/bin/rustc"
STAMP="$B/x86_64-unknown-linux-gnu/stage1-std/x86_64-unknown-exos/dist/.libstd-stamp"
D="$RADICE/cross_build/$MACCHINA/costruzione-prova-rustc-nativo"
T="$D/albero"

[ -x "$RUSTC" ] || { echo "manca $RUSTC: costruisci-exos.sh" >&2; exit 1; }
[ -f "$STAMP" ] || { echo "manca la std per EX-OS ($STAMP)" >&2; exit 1; }
[ -d build-64/iso/exos/bin ] || { echo "manca build-64/iso/exos: make tools64" >&2; exit 1; }

rm -rf "$T"; mkdir -p "$T"
cp -a build-64/iso/exos/. "$T/"
cp "$RUSTC" "$T/bin/rustc"
"$RADICE/cross_build/exos-cross64/bin/x86_64-exos-strip" "$T/bin/rustc"
L="$T/lib/rustlib/x86_64-unknown-exos/lib"
mkdir -p "$L"
# the stamp: NUL-separated paths, each with a one-letter kind in front
tr '\0' '\n' < "$STAMP" | sed -n 's/^t//p' | grep -E '\.(rlib|rmeta)$' | while read -r f; do cp "$f" "$L/"; done
for f in ciao.rs macro.rs usa.rs rotto.rs; do cp "tools/rust-exos/rustc-nativo/$f" "$T/$f"; done
echo "albero: $(du -sh "$T" | cut -f1), std: $(ls "$L" | wc -l) file"

IMG="$D/disco.img"
rm -f "$IMG"
qemu-img create -f raw "$IMG" 900M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' \
    | "$(command -v sfdisk || echo /sbin/sfdisk)" "$IMG" > /dev/null 2>&1
"$(command -v mke2fs || echo /sbin/mke2fs)" -q -t ext2 -b 4096 -L rust -d "$T" \
    -E offset=1048576 "$IMG" 898M

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_QEMU=qemu-system-x86_64 EXOS_ISTANZA=rustn EXOS_NO_FLOPPY=1 EXOS_RAM=2G
export EXOS_CDROM="$RADICE/dist/exos64.iso"
export EXOS_QEMU_EXTRA="$KVM -cpu qemu64 -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialrustn.txt
rm -f "$SER"
timeout ${LIMITE:-3600} python3 tools/qemu_drive.py "mount hd0p1 /disk@8" "export PATH=/disk/bin:/bin@1" \
    "echo INIZIO-RUSTN@1" "cd /disk@1" "rustc -vV@40" "rustc --print sysroot@30" \
    "rustc -O /disk/ciao.rs -o /disk/ciao@${ATTESA:-600}" "/disk/ciao@10" \
    "rustc --edition 2021 --crate-type proc-macro --crate-name quarantadue /disk/macro.rs -o /disk/libquarantadue.so@${ATTESA:-600}" \
    "rustc --edition 2021 /disk/usa.rs --extern quarantadue=/disk/libquarantadue.so -o /disk/usa@${ATTESA:-600}" \
    "/disk/usa@10" \
    "rustc --edition 2021 /disk/rotto.rs --extern quarantadue=/disk/libquarantadue.so -o /disk/rotto@${ATTESA_ROTTO:-300}" \
    "echo FINE-RUSTN@2" > "$D/qemu.log" 2>&1 || true
tr -d '\r' < "$SER" 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | \
    sed -n '/INIZIO-RUSTN$/,/FINE-RUSTN$/p' | grep -av "^ex-os:" | grep -av "^\[INFO\]"
