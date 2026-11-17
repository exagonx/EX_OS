#!/bin/bash
# =============================================================================
# tools/prova_cachedisco.sh - cachedisco in kernel.cfg counts (kernel 0.344,
# voce 27)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_cachedisco.sh            32 bit
#     tools/prova_cachedisco.sh 64         64 bit
#
# Up to 0.343 the page cache was made at PASSO 13a, kernel.cfg is read at
# 13b: "cachedisco = 1024" on the PC di prova still gave 383 MB (17/11).
# Four starts from the CD, the CD's kernel.cfg changed by xorriso. The disk is
# first read at mount, after kernel.cfg: cache_init makes the page cache (on
# the PC di prova the first read comes before, blk_cache_pagine_avvia does).
#   1. as it is (512 MB of RAM): 1/8 of the RAM, 63 MB, after 13b;
#   2. cachedisco = 48: 48 MB;
#   3. cachedisco = 0: the cache off;
#   4. (64 bit only) the PC di prova: 4 GB of RAM (3071 MB under 4 GB, so 383
#      MB of default) and cachedisco = 1024: 1024 MB, an 8 MB table in the
#      kernel band.
# Each one mounts a disk made on Linux (the page cache is for ATA disks, not
# for the CD) and reads a 4 MB file on it twice: the second read comes from
# the cache (/proc/disco, field 12), except with the cache off.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
BIT=${1:-32}
D=/tmp/exos-prove/cachedisco$BIT; rm -rf "$D"; mkdir -p "$D"
if [ "$BIT" = 64 ]; then ISO=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64; CFG=build-64/iso-exos/boot/kernel.cfg
else                     ISO=dist/exos.iso;   CFG=build/iso-exos/boot/kernel.cfg; fi
[ -f "$ISO" ] && [ -f "$CFG" ] || { echo "manca $ISO o $CFG" >&2; exit 1; }
command -v xorriso > /dev/null || { echo "manca xorriso" >&2; exit 1; }
# the disk: one ext2 partition with a 4 MB file
IMG="$D/hd.img"; PART="$D/part.img"
mkdir -p "$D/dentro"; head -c 4194304 /dev/urandom > "$D/dentro/grande"
/sbin/mke2fs -q -t ext2 -b 4096 -d "$D/dentro" "$PART" 16M || exit 1
qemu-img create -f raw "$IMG" 18M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83\n' | /sbin/sfdisk "$IMG" > /dev/null 2>&1
dd if="$PART" of="$IMG" bs=1M seek=1 conv=notrunc status=none; rm -f "$PART"
HASH=$(sha256sum "$D/dentro/grande" | cut -d' ' -f1)
KV=$(sed -n 's/^#define EXOS_VERSION *"\(.*\)"/\1/p' kernel/include/version.h)
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

# cd_con N: the CD with "cachedisco = N" under [kernel]
cd_con() {
    awk -v n="$1" '{ print } /^\[kernel\]/ { print "cachedisco = " n }' "$CFG" > "$D/kernel$1.cfg"
    grep -q "^cachedisco = $1$" "$D/kernel$1.cfg" || { no "kernel.cfg senza [kernel]"; return 1; }
    xorriso -indev "$ISO" -outdev "$D/cd$1.iso" -boot_image any replay \
        -map "$D/kernel$1.cfg" /boot/kernel.cfg -commit > "$D/xorriso$1.log" 2>&1
}
# giro NOME CD RAM: boot, the cache lines of dmesg, two reads of help.txt
giro() {
    EXOS_ISTANZA=cd$BIT$1 EXOS_NO_FLOPPY=1 EXOS_CDROM="$2" EXOS_RAM="$3" \
    EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide" \
        timeout 400 python3 tools/qemu_drive.py "uname -a@2" "mount hd0p1 /a@4" \
        "cat /proc/disco@2" "sha256sum /a/grande@6" "sha256sum /a/grande@6" "cat /proc/disco@2" "dmesg@4" \
        > "$D/$1.log" 2>&1
    tr -d '\r' < "$D/$1.log" | sed 's/\x1b\[[0-9;]*m//g' > "$D/$1.txt"
    grep -aq "EX-OS .* $KV" "$D/$1.txt" || no "$1: il kernel non e' $KV (vedi $D/$1.txt)"
    grep -aq "\[FAULT\]\|PANIC" "$D/$1.txt" && no "$1: FAULT o PANIC"
    [ "$(grep -ac "^$HASH" "$D/$1.txt")" -ge 2 ] || no "$1: il file letto non e' quello scritto"
}
# the page cache line comes after "Configurazione caricata"
dopo_13b() { awk '/PASSO 13b\] Configurazione caricata/ { c = 1 } /BLK: cache delle pagine/ && c { t = 1 } END { exit !t }' "$D/$1.txt"; }
# sectors from the cache between the two /proc/disco lines (field 12)
dalla_cache() { grep -a "^hd0 " "$D/$1.txt" | awk 'NR == 1 { a = $13 } { b = $13 } END { print b - a }'; }

echo "=== 1. il CD com'e' (512 MB) ==="
giro uno "$ISO" 512M
grep -aq "BLK: cache delle pagine fino a 6[34] MB" "$D/uno.txt" && si "1/8 della RAM: $(grep -ao 'fino a [0-9]* MB' "$D/uno.txt")" || no "manca 'fino a 63 MB' ($D/uno.txt)"
dopo_13b uno && si "la cache si fa dopo kernel.cfg (PASSO 13b)" || no "la cache si fa prima di kernel.cfg"
[ "$(dalla_cache uno)" -gt 0 ] && si "la seconda lettura viene dalla cache ($(dalla_cache uno) settori)" || no "niente dalla cache ($D/uno.txt)"

echo "=== 2. cachedisco = 48 ==="
if cd_con 48; then
    giro due "$D/cd48.iso" 512M
    grep -aq "BLK: cache delle pagine fino a 48 MB" "$D/due.txt" && si "cachedisco = 48: 48 MB" || no "cachedisco = 48 non vale ($D/due.txt)"
fi

echo "=== 3. cachedisco = 0 ==="
if cd_con 0; then
    giro tre "$D/cd0.iso" 512M
    grep -aq "BLK: cache delle pagine spenta" "$D/tre.txt" && si "cachedisco = 0: spenta" || no "cachedisco = 0 non la spegne ($D/tre.txt)"
    [ "$(dalla_cache tre)" = 0 ] && si "spenta: niente dalla cache delle pagine" || echo "  (spenta: $(dalla_cache tre) settori dalla cache dei settori)"
fi

if [ "$BIT" = 64 ]; then
    echo "=== 4. 4 GB e cachedisco = 1024 (il PC di prova) ==="
    if cd_con 1024; then
        giro quattro "$D/cd1024.iso" 4096M
        grep -aq "BLK: cache delle pagine fino a 1024 MB" "$D/quattro.txt" && si "4 GB: 1024 MB" \
            || no "4 GB, cachedisco = 1024: $(grep -ao 'cache delle pagine.*' "$D/quattro.txt" | head -1) ($D/quattro.txt)"
    fi
fi
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
