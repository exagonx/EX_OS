#!/bin/bash
# =============================================================================
# tools/prova_lpt.sh - kernel 0.324 and /dev/lpt.drv in QEMU (voce 98)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/prova_lpt.sh             32 and 64 bit
#     tools/prova_lpt.sh 64          only one
#     QEMU_PIU="-smp 2" ...          two processors (the wave reserves one)
#
# QEMU's parallel port at 0x378, IRQ 7, written to a file:
#   1. the kernel is 0.324 or later, lpt.drv 0.002 answers -version;
#   2. without the service lpt.drv says so; -avvia finds 0x378 (SPP);
#   3. -stampa of /boot/help.txt: the file QEMU writes is the same bytes;
#   4. -id: no IEEE 1284 answer (QEMU's port has nobody behind it), no hang;
#   5. -dati:0x5A is read back by -pin;
#   6. -prova-fronti:300: QEMU's port raises IRQ 7 at every write of the
#      control register with the interrupt enabled, and every one of the 300
#      edges comes back with its instant, none lost;
#   7. -prova-onda:2000:200: 2000 half-waves on D1, and how late they were,
#      with in/out straight from the driver (0.324, ioport_diretto);
#   8. -ferma, and no [FAULT].
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D=/tmp/exos-prove/lpt; mkdir -p "$D"
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

giro() {    # $1 = 32|64
    local bit=$1 iso q log L out
    if [ "$bit" = 64 ]; then iso=dist/exos64.iso; q=qemu-system-x86_64
    else                     iso=dist/exos.iso;   q=qemu-system-i386; fi
    log="$D/$bit.log"; out="$D/stampa$bit.out"
    rm -f "$log" "$out"
    echo "=== $bit bit ${QEMU_PIU:+($QEMU_PIU)} ==="
    EXOS_CDROM=$iso EXOS_QEMU=$q EXOS_ISTANZA=lp$bit EXOS_NO_FLOPPY=1 EXOS_RAM=512M \
    EXOS_QEMU_EXTRA="-parallel file:$out${QEMU_PIU:+ $QEMU_PIU}" \
        timeout 900 python3 tools/qemu_drive.py "root@4" "root@6" \
        "uname -a@2" \
        "lpt.drv -version@2" \
        "lpt.drv@3" \
        "lpt.drv -avvia@6" \
        "lpt.drv@3" \
        "lpt.drv -stampa /boot/help.txt@~120" \
        "lpt.drv -id@4" \
        "lpt.drv -dati:0x5A@3" \
        "lpt.drv -pin@3" \
        "lpt.drv -prova-fronti:300@~60" \
        "lpt.drv -prova-onda:2000:200@~30" \
        "lpt.drv -xyz@2" \
        "lpt.drv -ferma@3" \
        "lpt.drv@3" > "$log" 2>&1
    L=$(tr -d '\r' < "$log")
    echo "$L" | grep -a "fronti:\|onda:\|^lpt0:" | sed 's/^/      /'
    echo "$L" | grep -aq "EX-OS .* 0.32[4-9]" && si "$bit: kernel 0.324 o dopo" || no "$bit: il kernel e' prima della 0.324 (vedi $log)"
    echo "$L" | grep -aq "lpt.drv 0.002" && si "$bit: lpt.drv 0.002" || no "$bit: -version"
    echo "$L" | grep -aq "nessun servizio acceso" && si "$bit: senza servizio lo dice" || no "$bit: lo stato senza servizio"
    echo "$L" | grep -aq "lpt0: 0x378 irq 7, SPP" && si "$bit: -avvia trova 0x378, irq 7" || no "$bit: -avvia non trova la porta"
    if [ -f "$out" ] && cmp -s "$out" boot/help.txt; then si "$bit: la stampa e' identica a /boot/help.txt ($(stat -c %s "$out") byte)"
    else no "$bit: la stampa non e' /boot/help.txt ($(stat -c %s "$out" 2>/dev/null || echo 0) byte di $(stat -c %s boot/help.txt))"; fi
    echo "$L" | grep -aq "nessuna risposta IEEE 1284" && si "$bit: -id senza periferica risponde e non si blocca" || no "$bit: -id"
    echo "$L" | grep -aq "dati      0x5a" && si "$bit: -dati:0x5A riletto da -pin" || no "$bit: -dati / -pin"
    echo "$L" | grep -aq "fronti: 300, .* persi 0" && si "$bit: 300 fronti su 300 col loro istante, nessuno perso" \
        || no "$bit: i fronti non sono 300 (vedi $log)"
    echo "$L" | grep -aq "onda: 2000 mezze onde di 200 us" && si "$bit: l'onda di 2000 mezze onde suonata" || no "$bit: -prova-onda"
    echo "$L" | grep -aq "onda: .*in/out diretti" && si "$bit: l'onda con in/out diretti (bitmap del TSS)" || no "$bit: l'onda non usa in/out diretti"
    echo "$L" | grep -aq "opzione sconosciuta '-xyz'" && si "$bit: opzione sconosciuta rifiutata" || no "$bit: -xyz"
    echo "$L" | grep -aq "lpt0: fermato" && si "$bit: -ferma" || no "$bit: -ferma"
    grep -aq "FAULT\|KERNEL PANIC" /tmp/exos/seriallp$bit.txt 2>/dev/null && no "$bit: [FAULT] sulla seriale" || si "$bit: niente FAULT"
}

if [ $# -ge 1 ]; then giro "$1"; else giro 32; giro 64; fi
echo "=== $ok riusciti, $no no ==="
[ "$no" = 0 ]
