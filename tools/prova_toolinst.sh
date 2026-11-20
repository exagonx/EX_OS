#!/bin/bash
# =============================================================================
# tools/prova_toolinst.sh -- toolinst 0.007 reads the whole tools catalogue
# (20 November 2026)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_toolinst.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_toolinst.sh [dir]    64 bit
#
# The system CD boots, the tools CD is the second one: `toolinst -n -y` says
# what it would install, copying nothing. Up to 0.006 it kept 12 groups, 10
# `solo`, 4 `file`, 8 `nota` and 4 `vuole`: the catalogue has 38 groups,
# the last ones (vlc, qt5, mesa...) could not be chosen.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/toolinst-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; STRUMENTI=dist/exos64-tools.iso
    export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=512M; EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; STRUMENTI=dist/exos-tools.iso
    export EXOS_RAM=256M; EXTRA=""
fi
for f in "$ISO" "$STRUMENTI"; do [ -f "$f" ] || { echo "manca $f" >&2; exit 1; }; done
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

# the groups of the catalogue on that CD, and the last one
CAT=$(mktemp)
xorriso -osirrox on -indev "$STRUMENTI" -extract /exos/strumenti.txt "$CAT" > /dev/null 2>&1 \
    || { echo "non leggo exos/strumenti.txt da $STRUMENTI" >&2; exit 1; }
N=$(grep -c '^\[' "$CAT")
ULTIMO=$(grep '^\[' "$CAT" | tail -1 | tr -d '[]')
rm -f "$CAT"

export EXOS_QEMU_EXTRA="$EXTRA -drive file=$STRUMENTI,media=cdrom,if=ide,index=3,readonly=on"
EXOS_ISTANZA=ti$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO" \
    timeout 900 python3 tools/qemu_drive.py "mount cd1 /cdrom@6" \
    "toolinst -version@3" "toolinst -n -y -s /cdrom@~300" "echo FINE-TI@2" > "$D/ti.log" 2>&1
S="$D/seriale.txt"
tr -d '\r' < "/tmp/exos/serialti$ARCH.txt" | sed 's/\x1b\[[0-9;]*[mKD]//g' > "$S" 2>/dev/null

grep -aq "toolinst 0.007" "$S" && si "toolinst 0.007" || no "non e' toolinst 0.007 ($S)"
grep -aq "piu' di [0-9]* gruppi\|piu' di [0-9]* 'solo'\|piu' di [0-9]* 'file'" "$S" \
    && no "dice ancora 'piu' di N': un tetto ($S)" || si "nessun 'piu' di N ... ignoro'"
for g in vlc qt5 harfbuzz "$ULTIMO"; do
    grep -aiq "\b$g\b" "$S" && si "vede [$g] (il catalogo ha $N gruppi)" || no "[$g] non c'e' nell'elenco ($S)"
done
grep -aq "terminato per fault\|KERNEL PANIC" "$S" && no "un fault sulla seriale"
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
