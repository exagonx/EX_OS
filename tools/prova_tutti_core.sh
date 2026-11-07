#!/bin/bash
# =============================================================================
# tools/prova_tutti_core.sh - every processor works by default (kernel 0.319,
# voce 92), and there is no ceiling of 16
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_tutti_core.sh                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_tutti_core.sh   64 bit
#     PROVA_CORE_GIRI="2 4" ...                    only these rounds
#     PROVA_CORE_MODO=kvm|tcg ...                  every round that way
#
# The CD as it is, no kernel.cfg line touched (the default is smp = 2). For
# 1, 2 and 4 processors under KVM (real processors running together; without
# /dev/kvm under emulation) and 20 under emulation (more than the 16 of up to
# kernel 0.318):
#   1. hwinfo: "trovati N";
#   2. hwinfo: N-1 in state LAVORA, none IN ATTESA, none MUTO;
#   3. smpprova: sysconf says N at work and N in all (what Firefox, FFmpeg and
#      Mesa size their threads on: up to 0.318 always 1);
#   4. smpprova N: under KVM with no more processors than the host has, at
#      least 0.7 x N; emulated, more than one; with more virtual processors
#      than the host has real ones the figure is printed, not judged;
#   5. /proc/cpuinfo has N "processor" lines;
#   6. the shell still answers, no [FAULT] or panic on the serial line.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/tutti-core-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    export EXOS_QEMU=qemu-system-x86_64 EXOS_CDROM=dist/exos64.iso; CPU="-cpu qemu64"
else
    export EXOS_QEMU=qemu-system-i386 EXOS_CDROM=dist/exos.iso; CPU=""
fi
[ -f "$EXOS_CDROM" ] || { echo "manca $EXOS_CDROM" >&2; exit 1; }
export EXOS_ISTANZA=core$ARCH EXOS_NO_FLOPPY=1 EXOS_RAM=512M
SER=/tmp/exos/serialcore$ARCH.txt
HOST=$(nproc)

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

giro() {        # giro <processors> <kvm|tcg>
    local n=$1 modo=$2 L="$D/cpu$1-$2.txt" acc="-accel tcg,thread=multi" t w sc tot
    # ! UNDER EMULATION QEMU RUNS EVERY PROCESSOR IN ONE HOST THREAD unless
    # told otherwise: 20 processors did the work of 1.07 (measured 7/11)
    if [ "$modo" = kvm ]; then
        [ -w /dev/kvm ] || { modo=tcg; L="$D/cpu$n-tcg.txt"; }
        [ "$modo" = kvm ] && acc="-enable-kvm"
    fi
    echo "=== $n processor$([ "$n" = 1 ] && echo e || echo i), $modo ($ARCH) ==="
    export EXOS_QEMU_EXTRA="$CPU -smp $n $acc"
    t=$(( n > 8 ? 240 : 30 ))
    timeout 900 python3 tools/qemu_drive.py "hwinfo@15" "smpprova $n 3@$t" \
        "cat /proc/cpuinfo@3" "echo FINE-CORE@2" > "$D/cpu$n-$modo-qemu.log" 2>&1
    tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$L"

    w=$(sed -n 's/^ *trovati  *\([0-9][0-9]*\).*/\1/p' "$L" | head -1)
    [ "$w" = "$n" ] && si "hwinfo: trovati $n" || no "hwinfo: trovati ${w:-niente}, QEMU ne ha $n ($L)"
    w=$(grep -ac "LAVORA   - esegue processi" "$L")
    if [ "$w" = $((n - 1)) ] && ! grep -aq "IN ATTESA - acceso\|MUTO  *- svegliato" "$L"; then
        si "$((n - 1)) in piu' al lavoro, nessuno in attesa o muto"
    else
        no "al lavoro $w su $((n - 1)), in attesa $(grep -ac 'IN ATTESA - acceso' "$L"), muti $(grep -ac 'MUTO  *- svegliato' "$L")"
    fi
    sc=$(grep -a "sysconf dice" "$L" | head -1)
    echo "$sc" | grep -q "dice $n processori al lavoro, $n in tutto" \
        && si "sysconf: $n e $n" || no "sysconf: ${sc:-niente} (attesi $n)"
    if [ "$n" -ge 2 ]; then
        tot=$(sed -n 's/^ *lavoro totale  *\([0-9]*\)\.\([0-9][0-9]\) volte.*/\1\2/p' "$L" | head -1)
        tot=${tot:-0}; tot=$((10#$tot))
        if [ "$modo" = kvm ] && [ "$n" -le "$HOST" ]; then
            [ "$tot" -ge $((n * 70)) ] \
                && si "smpprova: $n insieme fanno $((tot / 100)).$(printf %02d $((tot % 100))) volte uno" \
                || no "smpprova: $n insieme fanno $((tot / 100)).$(printf %02d $((tot % 100))) volte uno (attese almeno $((n * 7 / 10)))"
        elif [ "$n" -le "$HOST" ]; then
            grep -aq "^smpprova: PIU' PROCESSORI LAVORANO" "$L" \
                && si "smpprova: piu' processori lavorano ($((tot / 100)).$(printf %02d $((tot % 100))) volte uno, emulati)" \
                || no "smpprova: lavora un processore solo ($L)"
        else
            # ! MORE VIRTUAL PROCESSORS THAN REAL ONES: the host suspends the
            # one holding the kernel's single lock and the others wait for it
            # (measured 7/11: 20 on a 4-core host, 1.79 under KVM, 0.96
            # emulated; 4 of 4: 3.5 and 3.37). Not a measure of a machine
            # with that many processors: said, not judged.
            echo "  [--] smpprova: $((tot / 100)).$(printf %02d $((tot % 100))) volte uno, con $n processori virtuali su $HOST veri (non giudicato)"
        fi
    fi
    w=$(grep -ac "^processor[[:space:]]*:" "$L")
    [ "$w" = "$n" ] && si "/proc/cpuinfo: $n processori" || no "/proc/cpuinfo: $w processori, attesi $n"
    if grep -aq "^FINE-CORE" "$L" && ! grep -aq "\[FAULT\]\|PANIC\|panic\|ECCEZIONE" "$L"; then
        si "la shell risponde, nessun [FAULT]"
    else
        no "shell ferma o [FAULT] ($L)"
    fi
}

for g in ${PROVA_CORE_GIRI:-1 2 4 20}; do
    if [ -n "${PROVA_CORE_MODO:-}" ]; then giro "$g" "$PROVA_CORE_MODO"
    elif [ "$g" -gt 16 ]; then giro "$g" tcg; else giro "$g" kvm; fi
done
echo "=== $ok riusciti, $no no ==="
[ $no = 0 ]
