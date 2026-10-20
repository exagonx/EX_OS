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
# CD, guardando la seriale:
#
#   1. su un processore a 64 bit (qemu-system-x86_64) il kernel deve dire di
#      essere in modo a 64 bit e finire con «EXOS64-TAPPA1-OK»;
#   2. su uno a 32 bit (qemu-system-i386, un Pentium) deve DIRE che i 64 bit
#      non ci sono e fermarsi - non riavviarsi in silenzio.
#
# ! LA SECONDA PROVA CONTA QUANTO LA PRIMA: e' il caso di chi mette il
# CD sbagliato nella macchina sbagliata.
# =============================================================================
set -e
cd "$(dirname "$0")/.."
make ARCH=x86_64 kernel64-prova > /tmp/exos-kernel64-make.log 2>&1 || {
    echo "la costruzione e' fallita: /tmp/exos-kernel64-make.log"; tail -15 /tmp/exos-kernel64-make.log; exit 1; }

avvia() {   # $1 = emulatore, $2 = cpu, $3 = file della seriale
    rm -f "$3"
    timeout 25 "$1" -cpu "$2" -m 128M -cdrom dist/exos64-prove.iso \
        -boot d -display none -serial "file:$3" -no-reboot > /dev/null 2>&1 || true
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

# --- 3. il SISTEMA a 64 bit: kernel intero, driver, shell, libc ----------------
make iso64 > /tmp/exos-kernel64-intero-make.log 2>&1 || {
    echo "la costruzione del sistema a 64 bit e' fallita: /tmp/exos-kernel64-intero-make.log"
    tail -15 /tmp/exos-kernel64-intero-make.log; exit 1; }
grep "costruiti\|NON costruiti\|non compilano" /tmp/exos-kernel64-intero-make.log | sed 's/^/    /'
# Si avvia dal CD e si BATTONO i comandi nella shell (tools/prova64.sh): il
# primo programma senza libc, e la prova della libc - segnali, fili, __thread,
# setjmp, virgola mobile, file.
tools/prova64.sh "primo64@~8" "prova64 uno due@~15" "ping 10.0.2.2@~15" > /tmp/exos-kernel64-c.out 2>&1
tr -d '\r' < /tmp/exos/serial64.txt | sed 's/\x1b\[[0-9;]*m//g' > /tmp/exos-kernel64-c.txt
echo "=== 3. il sistema a 64 bit: kernel intero, driver, shell, libc ==="
grep -a "Long Mode\|VFS: root\|servizio 'rete0'\|dhcp: configurato\|ricevuti\|PROVA64-LIBC\|EXOS64-TAPPA3D\|SBAGLIATO" /tmp/exos-kernel64-c.txt | sed 's/^/    /'
si() { grep -a -q "$1" /tmp/exos-kernel64-c.txt; }
if si "Long Mode a 64 bit" && si "VFS: root"; then
    echo "  [OK]  il kernel comune si avvia a 64 bit e monta il CD"
else
    echo "  [NO]  il kernel intero non e' arrivato al file system (seriale: /tmp/exos-kernel64-c.txt)"; no=1
fi
if si "EXOS64-TAPPA3D-OK"; then
    echo "  [OK]  la shell avvia un programma ELF64 senza libc"
else
    echo "  [NO]  il primo programma a 64 bit non ha finito bene"; no=1
fi
if si "PROVA64-LIBC-OK" && ! si "KERNEL PANIC\|PAGE FAULT (KERNEL)"; then
    echo "  [OK]  la libc a 64 bit: argv, malloc, segnali, fili, __thread, file"
else
    echo "  [NO]  la prova della libc a 64 bit non e' passata"; no=1
fi
if si "4 inviati, 4 ricevuti"; then
    echo "  [OK]  driver in ring 3 e rete: e1000, IP, DHCP, ping"
else
    echo "  [NO]  la rete a 64 bit non ha risposto al ping"; no=1
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
