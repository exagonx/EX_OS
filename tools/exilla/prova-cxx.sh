#!/bin/bash
# =============================================================================
# tools/exilla/prova-cxx.sh — tools/exilla/prova-cxx.cc, eseguito dentro EX-OS
#
# Tappa 0 di Exilla (28 settembre 2026): compila la prova col g++ i386-exos di
# QUESTA macchina (tools/exilla/toolchain-questa-macchina.sh), la mette su un
# disco di prova e la esegue in QEMU dal CD. Il verdetto e' l'uscita della
# prova sulla seriale: ogni controllo OK, e «0 NO».
#
# ! NON LANCIA make: il CD e' quello che c'e' in dist/ (lo costruisce chi fa
# make, sull'altro PC — ISTRUZIONI-SECONDO-PROFILO.md, 3.1). Qui si compila
# solo il programma di prova, e le cose temporanee stanno in
# cross_build/<macchina>/costruzione-prova (escluse da MEGA).
#
#     tools/exilla/prova-cxx.sh
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
TCB="$PWD/cross_build/$MACCHINA/exos-cross/bin"
D="$PWD/cross_build/$MACCHINA/costruzione-prova"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
mkdir -p "$D"

[ -x "$TCB/i386-exos-g++" ] || { echo "manca $TCB/i386-exos-g++: tools/exilla/toolchain-questa-macchina.sh" >&2; exit 1; }
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

# Le opzioni con cui Mozilla compila Gecko: niente eccezioni, niente RTTI.
"$TCB/i386-exos-g++" -O2 -fno-exceptions -fno-rtti \
    tools/exilla/prova-cxx.cc -o "$D/prova-cxx" || exit 1
echo "compilata: $(stat -c %s "$D/prova-cxx") byte"

export EXOS_ISTANZA=exilla EXOS_NO_FLOPPY=1 EXOS_CDROM="${EXOS_CDROM:-dist/exos.iso}"
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_QEMU_EXTRA="$KVM -drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialexilla.txt

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1
"$DEBUGFS" -w -R "write $D/prova-cxx prova-cxx" "$OFF" > /dev/null 2>&1

rm -f "$SER"
timeout 300 python3 tools/qemu_drive.py "mount hd0p1 /disk@6" \
    "/disk/prova-cxx@6" "echo FINE-PROVA@2" > "$D/1.log" 2>&1

uscita=$(tr -d '\r' < "$SER" 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | \
         sed -n '/prova-cxx: il C++/,/FINE-PROVA/p')
echo "$uscita" | grep -v "FINE-PROVA\|^ex-os"
if echo "$uscita" | grep -q "prova-cxx: 0 NO" && ! echo "$uscita" | grep -q "\[NO\]"; then
    echo "=== [OK] il C++ di Exilla gira dentro EX-OS ==="
    exit 0
fi
echo "=== [NO] vedi $D/1.log e $SER ==="
exit 1
