#!/bin/bash
# =============================================================================
# tools/prova_affinita.sh - processes sent to given processors, and a
# processor reserved to one process (kernel 0.319, voce 92)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_affinita.sh                    32 bit
#     EXOS_ARCH=x86_64 tools/prova_affinita.sh   64 bit
#
# The CD in QEMU with 4 processors under KVM (emulated without /dev/kvm):
#   1. `cpu` lists 4 processors at work, 0 the boot one;
#   2. `cpu -x 1 smpprova 2 8`: both workers inherit {1} and share it, the
#      work of about one (at most 1.15). ! 8 SECONDS, NOT 3: the second
#      worker starts ~0.8 s after the first (loading it from the CD in
#      QEMU), and for that time each works alone: with 3 s the pair did
#      1.26, each exactly 0.63, which is that and not a leak (measured 7/11);
#   3. `cpu -x 1-2 smpprova 2 8`: two processors, about two (at least 1.6);
#   4. `cpu -R 3 smpprova -figlio 25000 &`: processor 3 reserved to it.
#      `cpu` says RISERVATO, the reserved process's Cpus_allowed_list is 3,
#      a new process's (cat /proc/self/status) is 0-2;
#   5. meanwhile `smpprova 3 8`: three workers on the three left, about
#      three (at least 2.4: the reserved one does not slow them);
#   6. once the reserved process ends, `cpu` says processor 3 is "di tutti"
#      again (the reservation goes with its owner);
#   7. `cpu -r 1 0` is refused (processor 0 cannot be reserved);
#   8. no [FAULT] or panic, the shell answers.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/affinita-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    export EXOS_QEMU=qemu-system-x86_64 EXOS_CDROM=dist/exos64.iso; CPU="-cpu qemu64"
else
    export EXOS_QEMU=qemu-system-i386 EXOS_CDROM=dist/exos.iso; CPU=""
fi
[ -f "$EXOS_CDROM" ] || { echo "manca $EXOS_CDROM" >&2; exit 1; }
ACC="-accel tcg,thread=multi"; [ -w /dev/kvm ] && ACC="-enable-kvm"
export EXOS_ISTANZA=aff$ARCH EXOS_NO_FLOPPY=1 EXOS_RAM=512M EXOS_QEMU_EXTRA="$CPU -smp 4 $ACC"
SER=/tmp/exos/serialaff$ARCH.txt
L="$D/seriale.txt"

ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

timeout 900 python3 tools/qemu_drive.py \
    "echo PASSO-1@1" "cpu@3" \
    "echo PASSO-2@1" "cpu -x 1 smpprova 2 8@24" \
    "echo PASSO-3@1" "cpu -x 1-2 smpprova 2 8@24" \
    "echo PASSO-4@1" "cpu -R 3 smpprova -figlio 25000 &@3" "cpu@3" "ps@3" \
    "cat /proc/self/status@2" \
    "echo PASSO-5@1" "smpprova 3 8@24" \
    "echo PASSO-6@1" "cpu@10" "cpu@2" \
    "echo PASSO-7@1" "cpu -r 1 0@2" \
    "echo FINE-AFF@2" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$L"
passo() { sed -n "/^PASSO-$1\$/,/^PASSO-$(( $1 + 1 ))\$/p" "$L"; }
passo7() { sed -n '/^PASSO-7$/,/^FINE-AFF$/p' "$L"; }
volte() { sed -n 's/^ *lavoro totale  *\([0-9]*\)\.\([0-9][0-9]\) volte.*/\1\2/p' | head -1; }

# 1
passo 1 > "$D/1.txt"
grep -q "processori al lavoro: 4" "$D/1.txt" && grep -q "cpu 0 .*d'avvio" "$D/1.txt" \
    && si "1. cpu: 4 processori al lavoro, 0 quello d'avvio" || no "1. cpu: $(head -3 "$D/1.txt" | tr '\n' ' ')"
# 2
v=$(passo 2 | volte); v=$((10#${v:-0}))
[ "$v" -gt 0 ] && [ "$v" -le 115 ] && si "2. due programmi fissati su 1: $((v / 100)).$(printf %02d $((v % 100))) volte uno" \
    || no "2. due programmi fissati su 1 fanno $((v / 100)).$(printf %02d $((v % 100))) volte uno (attese al piu' 1.15)"
# 3
v=$(passo 3 | volte); v=$((10#${v:-0}))
[ "$v" -ge 160 ] && si "3. due programmi su 1-2: $((v / 100)).$(printf %02d $((v % 100))) volte uno" \
    || no "3. due programmi su 1-2 fanno $((v / 100)).$(printf %02d $((v % 100))) volte uno (attese almeno 1.60)"
# 4
passo 4 > "$D/4.txt"
pid=$(sed -n 's/.*cpu 3 .*RISERVATO a PID \([0-9]*\).*/\1/p' "$D/4.txt" | head -1)
if [ -n "$pid" ] && grep -q "RISERVATO a PID $pid (.*smpprova" "$D/4.txt"; then
    si "4. cpu: processore 3 RISERVATO a PID $pid (smpprova)"
else
    no "4. cpu non dice il processore 3 riservato ($D/4.txt)"
fi
grep -q "^Cpus_allowed_list:[[:space:]]*0-2$" "$D/4.txt" \
    && si "4. un processo nuovo puo' girare su 0-2, non sul 3 riservato" \
    || no "4. Cpus_allowed_list di cat: $(grep Cpus_allowed "$D/4.txt")"
# 5
v=$(passo 5 | volte); v=$((10#${v:-0}))
[ "$v" -ge 240 ] && si "5. tre programmi sui tre rimasti: $((v / 100)).$(printf %02d $((v % 100))) volte uno" \
    || no "5. tre programmi fanno $((v / 100)).$(printf %02d $((v % 100))) volte uno (attese almeno 2.40)"
# 6
passo 6 > "$D/6.txt"
grep -q "cpu 3 .*di tutti" "$D/6.txt" && ! grep -q "RISERVATO" "$D/6.txt" \
    && si "6. finito il processo, il processore 3 e' di nuovo di tutti" \
    || no "6. dopo la fine il processore 3: $(grep 'cpu 3 ' "$D/6.txt" | tail -1)"
# 7
passo7 | grep -q "cpu: .*non valido" \
    && si "7. il processore 0 non si riserva" || no "7. cpu -r 1 0: $(passo7 | sed -n 2p)"
# 8
if grep -aq "^FINE-AFF" "$L" && ! grep -aq "\[FAULT\]\|PANIC\|panic\|ECCEZIONE" "$L"; then
    si "8. la shell risponde, nessun [FAULT]"
else
    no "8. shell ferma o [FAULT] ($L)"
fi
echo "=== $ok riusciti, $no no ($ARCH) ==="
[ $no = 0 ]
