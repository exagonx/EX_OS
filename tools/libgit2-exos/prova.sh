#!/bin/bash
# =============================================================================
# tools/libgit2-exos/prova.sh — libgit2 inside 64-bit EX-OS (QEMU)
#
# Builds prova-git.c against the libgit2.a of costruisci.sh, puts it and the
# git2 command-line tool on an ext2 disk (mke2fs -d) and runs them inside
# dist/exos64.iso: prova-git's steps (init, add, commit, status, log, branch,
# hash-object, local clone, diff), then git2 hash-object / cat-file / clone
# on the repository prova-git made.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
R=$PWD
D=/tmp/claude-1004/git64
T=$D/albero
rm -rf "$D"; mkdir -p "$T/bin" "$T/w"
export PATH="$R/cross_build/exos-cross64/bin:$PATH"
O=$R/cross_build/openssl-build-exos64
x86_64-exos-gcc -static -O2 -Wall -o "$T/bin/prova-git" tools/libgit2-exos/prova-git.c \
    -lgit2 "$O/libssl.a" "$O/libcrypto.a" || exit 1
cp -p cross_build/libgit2-exos64/bin/git2 "$T/bin/git2"
x86_64-exos-strip "$T/bin/prova-git" "$T/bin/git2"
printf 'ciao\n' > "$T/ciao.txt"

IMG=$D/git-hd.img
qemu-img create -f raw "$IMG" 64M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' \
    | "$(command -v sfdisk || echo /sbin/sfdisk)" "$IMG" > /dev/null 2>&1
"$(command -v mke2fs || echo /sbin/mke2fs)" -q -t ext2 -b 1024 -L prova -d "$T" \
    -E offset=1048576 "$IMG" 62M

export EXOS_QEMU=qemu-system-x86_64 EXOS_ISTANZA=git64 EXOS_NO_FLOPPY=1
export EXOS_CDROM="$R/dist/exos64.iso" EXOS_RAM=512M
export EXOS_QEMU_EXTRA="-cpu qemu64 -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialgit64.txt
rm -f "$SER"
timeout 900 python3 tools/qemu_drive.py "mount hd0p1 /disk@5" "export PATH=/disk/bin:/bin@1" \
    "export HOME=/disk@1" "echo INIZIO-GIT@1" "prova-git /disk/w@60" \
    "git2 hash-object /disk/ciao.txt@15" "cd /disk/w/repo@1" "git2 cat-file -p HEAD@15" \
    "cd /disk@1" "git2 clone /disk/w/repo /disk/c2@40" "cat /disk/c2/sotto/dati.txt@3" \
    "echo FINE-GIT@2" > "$D/qemu.log" 2>&1 || true
tr -d '\r' < "$SER" 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | \
    sed -n '/INIZIO-GIT$/,/FINE-GIT$/p' | grep -v "^\[INFO\]"
