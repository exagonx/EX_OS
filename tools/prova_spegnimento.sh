#!/bin/bash
# =============================================================================
# tools/prova_spegnimento.sh - shutdown, reboot and halt with several
# processors at work, asked from every processor (kernel 0.320)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_spegnimento.sh                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_spegnimento.sh   64 bit
#
# ! WHY: with kernel 0.319 (smp = 2 the default) the laptop stopped at
# "Spegnimento fra 3..." when the program asking - ExWin's program manager,
# or the shell - was on processor 1: the countdown waited for g_ticks holding
# the kernel's lock, and g_ticks rises on processor 0, which waited for the
# lock. Every case is one boot of the CD in QEMU under KVM (emulated without
# /dev/kvm), the command started on a given processor with `cpu -x`:
#   1. shutdown from processor 1 (2 processors): the countdown ends,
#      "Spegnimento hardware", and the machine is off (the shell's next echo
#      never comes);
#   2. shutdown from processor 0, the same;
#   3. shutdown -r from processor 1: the machine resets (QEMU, run with
#      -no-reboot, ends);
#   4. shutdown -h from processor 1: "Sistema arrestato";
#   5. shutdown from processor 1 while a program computes on processor 0
#      (smpprova -figlio): off all the same, the program ended by the kernel;
#   6. 4 processors, shutdown from processor 3: off.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/spegnimento-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    export EXOS_QEMU=qemu-system-x86_64 EXOS_CDROM=dist/exos64.iso; CPU="-cpu qemu64"
else
    export EXOS_QEMU=qemu-system-i386 EXOS_CDROM=dist/exos.iso; CPU=""
fi
[ -f "$EXOS_CDROM" ] || { echo "manca $EXOS_CDROM" >&2; exit 1; }
ACC="-accel tcg,thread=multi"; [ -w /dev/kvm ] && ACC="-enable-kvm"
export EXOS_NO_FLOPPY=1 EXOS_RAM=512M

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

# caso <name> <processors> <seconds after the command> <commands...>
# leaves the clean serial log in $D/<name>.txt
caso() {
    local nome=$1 n=$2; shift 2
    export EXOS_ISTANZA=spe$nome$ARCH EXOS_QEMU_EXTRA="$CPU -smp $n $ACC"
    timeout 300 python3 tools/qemu_drive.py "$@" "echo ANCORA-ACCESA@3" > "$D/$nome-qemu.log" 2>&1
    tr -d '\r' < "/tmp/exos/serialspe$nome$ARCH.txt" | sed 's/\x1b\[[0-9;]*m//g' > "$D/$nome.txt"
}
spenta() {      # spenta <name> <what>: counted down, powered off, nothing after
    local L="$D/$1.txt"
    if [ "$(grep -ac 'Spegnimento fra' "$L")" = 3 ] && grep -aq "Spegnimento hardware" "$L" &&
       ! grep -aq "^ANCORA-ACCESA" "$L" && ! grep -aq "Sistema arrestato" "$L"; then
        si "$2: conto finito, macchina spenta"
    else
        no "$2: $(grep -a 'Spegnimento\|arrestato\|ANCORA' "$L" | tail -2 | tr '\n' ' ') ($L)"
    fi
}

caso uno 2 "cpu -x 1 shutdown@40"
spenta uno "1. shutdown dal processore 1"
caso zero 2 "cpu -x 0 shutdown@40"
spenta zero "2. shutdown dal processore 0"

caso riavvio 2 "cpu -x 1 shutdown -r@60"
# ! qemu_drive.py runs QEMU with -no-reboot: the reset ENDS QEMU instead of
# booting again, so "reset" is: the kernel said it resets, and the shell's
# next echo never came.
grep -aq "Riavvio del sistema in corso" "$D/riavvio.txt" && ! grep -aq "^ANCORA-ACCESA" "$D/riavvio.txt" \
    && si "3. shutdown -r dal processore 1: $(grep -a 'Riavvio:' "$D/riavvio.txt" | tail -1 | sed 's/^ *//'), la macchina si resetta" \
    || no "3. shutdown -r dal processore 1 ($D/riavvio.txt)"

caso arresto 2 "cpu -x 1 shutdown -h@30"
grep -aq "Sistema arrestato" "$D/arresto.txt" && ! grep -aq "^ANCORA-ACCESA" "$D/arresto.txt" \
    && si "4. shutdown -h dal processore 1: sistema arrestato" \
    || no "4. shutdown -h dal processore 1 ($D/arresto.txt)"

caso carico 2 "cpu -x 0 smpprova -figlio 60000 &@3" "cpu -x 1 shutdown@60"
spenta carico "5. shutdown dal processore 1 con un programma che calcola sullo 0"

caso quattro 4 "cpu -x 3 shutdown@40"
spenta quattro "6. 4 processori, shutdown dal 3"

echo "=== $ok riusciti, $no no ($ARCH) ==="
[ $no = 0 ]
