#!/bin/sh
# =============================================================================
# tools/prova_kernel64.sh — il kernel a 64 bit arriva in long mode? (@EXOS-64)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_kernel64.sh
#
# Costruisce (make ARCH=x86_64 kernel64-prova) e avvia due volte lo stesso
# dischetto, guardando la seriale:
#
#   1. su un processore a 64 bit (qemu-system-x86_64) il kernel deve dire di
#      essere in modo a 64 bit e finire con «EXOS64-TAPPA1-OK»;
#   2. su uno a 32 bit (qemu-system-i386, un Pentium) deve DIRE che i 64 bit
#      non ci sono e fermarsi - non riavviarsi in silenzio.
#
# ! LA SECONDA PROVA CONTA QUANTO LA PRIMA: e' il caso di chi mette il
# dischetto sbagliato nella macchina sbagliata.
# =============================================================================
set -e
cd "$(dirname "$0")/.."
make ARCH=x86_64 kernel64-prova > /tmp/exos-kernel64-make.log 2>&1 || {
    echo "la costruzione e' fallita: /tmp/exos-kernel64-make.log"; tail -15 /tmp/exos-kernel64-make.log; exit 1; }

avvia() {   # $1 = emulatore, $2 = cpu, $3 = file della seriale
    rm -f "$3"
    timeout 25 "$1" -cpu "$2" -m 128M -drive file=dist/floppy64.img,format=raw,if=floppy \
        -boot a -display none -serial "file:$3" -no-reboot > /dev/null 2>&1 || true
}

no=0
avvia qemu-system-x86_64 qemu64 /tmp/exos-kernel64-a.txt
echo "=== 1. processore a 64 bit ==="
tr -d '\r' < /tmp/exos-kernel64-a.txt | sed -n '/EX-OS .*x86_64/,$p' | sed 's/^/    /'
if grep -q "EXOS64-TAPPA1-OK" /tmp/exos-kernel64-a.txt && grep -q "giusto" /tmp/exos-kernel64-a.txt; then
    echo "  [OK]  il kernel e' arrivato in modo a 64 bit"
else
    echo "  [NO]  non ci e' arrivato (seriale: /tmp/exos-kernel64-a.txt)"; no=1
fi
if grep -q "EXOS64-TAPPA3A-OK" /tmp/exos-kernel64-a.txt; then
    echo "  [OK]  interrupt: eccezione gestita, orologio, errore di pagina"
else
    echo "  [NO]  gli interrupt non hanno passato le tre prove"; no=1
fi
if grep -q "EXOS64-TAPPA3B-OK" /tmp/exos-kernel64-a.txt; then
    echo "  [OK]  i file comuni lavorano a 64 bit: klog, pagine, heap"
else
    echo "  [NO]  klog, pagine o heap non tornano"; no=1
fi
if grep -q "EXOS64-TAPPA3C-OK" /tmp/exos-kernel64-a.txt; then
    echo "  [OK]  memoria a quattro livelli: mappa, finestra, spazio di un processo"
else
    echo "  [NO]  la paginazione a 64 bit non ha passato le prove"; no=1
fi

# --- 3. il kernel INTERO: gli stessi sorgenti del 32 bit, fino a un programma --
make ARCH=x86_64 kernel64 > /tmp/exos-kernel64-intero-make.log 2>&1 || {
    echo "la costruzione del kernel intero e' fallita: /tmp/exos-kernel64-intero-make.log"
    tail -15 /tmp/exos-kernel64-intero-make.log; exit 1; }
rm -f /tmp/exos-kernel64-c.txt
timeout 40 qemu-system-x86_64 -cpu qemu64 -m 128M \
    -drive file=dist/floppy64-intero.img,format=raw,if=floppy -boot a -display none \
    -serial file:/tmp/exos-kernel64-c.txt -no-reboot > /dev/null 2>&1 || true
echo "=== 3. il kernel intero a 64 bit e il primo programma ==="
tr -d '\r' < /tmp/exos-kernel64-c.txt | grep -a "Long Mode\|PASSO 10\] Sched\|PASSO 11\] Sys\|VFS: root" | sed 's/^/    /'
tr -d '\r' < /tmp/exos-kernel64-c.txt | sed -n '/^primo64/,/EXOS64-TAPPA3D/p' | sed 's/^/    /'
if grep -q "Long Mode a 64 bit" /tmp/exos-kernel64-c.txt && grep -q "VFS: root" /tmp/exos-kernel64-c.txt; then
    echo "  [OK]  il kernel comune si avvia a 64 bit: memoria, scheduler, chiamate, dischetto"
else
    echo "  [NO]  il kernel intero non e' arrivato al file system (seriale: /tmp/exos-kernel64-c.txt)"; no=1
fi
if grep -q "EXOS64-TAPPA3D-OK" /tmp/exos-kernel64-c.txt && ! grep -q "KERNEL PANIC\|PAGE FAULT (KERNEL)" /tmp/exos-kernel64-c.txt; then
    echo "  [OK]  un programma ELF64 gira in ring 3: chiamate di sistema, R8-R15, sleep"
else
    echo "  [NO]  il primo programma a 64 bit non ha finito bene"; no=1
fi

avvia qemu-system-i386 pentium /tmp/exos-kernel64-b.txt
echo "=== 4. processore a 32 bit (Pentium) ==="
tr -d '\r' < /tmp/exos-kernel64-b.txt | grep -a "64 bit\|32 bit" | sed 's/^/    /'
if grep -q "non ha i 64 bit" /tmp/exos-kernel64-b.txt && ! grep -q "TAPPA1-OK" /tmp/exos-kernel64-b.txt; then
    echo "  [OK]  lo dice e si ferma"
else
    echo "  [NO]  doveva dirlo e fermarsi (seriale: /tmp/exos-kernel64-b.txt)"; no=1
fi
exit $no
