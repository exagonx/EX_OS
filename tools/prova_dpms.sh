#!/bin/bash
# =============================================================================
# tools/prova_dpms.sh - kernel 0.312 and /dev/dpms.drv in QEMU (voce 78 b)
#
#     tools/prova_dpms.sh            32 and 64 bit
#     tools/prova_dpms.sh 64         only one
#
# QEMU has a standard VGA, so this proves the "vga" method (SR01) and the
# service: the NVIDIA one (the laptop's SOR) is proved on the laptop.
# The screen is looked at with the monitor's screendump, which is not a key:
#   1. `dpms.drv` without the service says it is off;
#   2. `-dopo:1` starts it, the method is vga;
#   3. `-spegni`: three seconds later the screen is black;
#   4. a key: the screen is back, with the text;
#   5. a minute and a bit without keys: black by itself;
#   6. the mouse moved (PS/2, from the monitor, no key): back; `dpms.drv`
#      says 2 switch-offs;
#   7. `-ferma`, an unknown option refused, no [FAULT].
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D=/tmp/exos-prove/dpms; rm -rf "$D"; mkdir -p "$D"
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

# Mean brightness of a PPM (P6), 0..255.
luce() {
    python3 -c '
import sys
d = open(sys.argv[1], "rb").read()
i = 0; campi = []
while len(campi) < 4:
    while d[i:i+1].isspace(): i += 1
    if d[i:i+1] == b"#":
        while d[i:i+1] != b"\n": i += 1
        continue
    j = i
    while not d[j:j+1].isspace(): j += 1
    campi.append(d[i:j]); i = j
i += 1
px = d[i:]
print(sum(px[::97]) * 97 // max(1, len(px)))' "$1" 2>/dev/null || echo -1
}

giro() {    # $1 = 32|64
    local bit=$1 iso q log L f
    if [ "$bit" = 64 ]; then iso=dist/exos64.iso; q=qemu-system-x86_64
    else                     iso=dist/exos.iso;   q=qemu-system-i386; fi
    log="$D/$bit.log"
    echo "=== $bit bit ==="
    EXOS_CDROM=$iso EXOS_QEMU=$q EXOS_ISTANZA=dp$bit EXOS_NO_FLOPPY=1 EXOS_RAM=512M \
        timeout 600 python3 tools/qemu_drive.py "root@4" "root@6" \
        "uname -a@2" \
        "dpms.drv -version@2" \
        "dpms.drv@3" \
        "dpms.drv -dopo:1@8" \
        "dpms.drv@3" \
        "foto:$D/${bit}a.ppm@1" \
        "dpms.drv -spegni@4" \
        "foto:$D/${bit}b.ppm@1" \
        "dpms.drv@3" \
        "foto:$D/${bit}c.ppm@1" \
        "echo aspetto@75" \
        "foto:$D/${bit}d.ppm@1" \
        "mon:mouse_move 40 30@2" \
        "foto:$D/${bit}e.ppm@1" \
        "dpms.drv@3" \
        "dpms.drv -xyz@2" \
        "dpms.drv -ferma@3" \
        "dpms.drv@3" > "$log" 2>&1
    L=$(tr -d '\r' < "$log")
    echo "$L" | grep -aq "EX-OS .* 0.312" && si "$bit: kernel 0.312" || no "$bit: il kernel non e' 0.312 (vedi $log)"
    echo "$L" | grep -aq "dpms.drv 0.001" && si "$bit: dpms.drv 0.001" || no "$bit: -version"
    echo "$L" | grep -aq "servizio:   spento" && si "$bit: senza servizio lo dice" || no "$bit: lo stato senza servizio"
    echo "$L" | grep -aq "servizio acceso, metodo vga" && si "$bit: -dopo:1 accende il servizio, metodo vga" \
        || no "$bit: il servizio non e' partito col metodo vga"
    for f in a b c d e; do eval "l_$f=$(luce "$D/$bit$f.ppm")"; done
    echo "  (luce: prima $l_a, -spegni $l_b, tasto $l_c, un minuto $l_d, mouse $l_e)"
    [ "$l_a" -gt 0 ] && si "$bit: lo schermo all'inizio si vede" || no "$bit: lo schermo all'inizio e' nero"
    [ "$l_b" -eq 0 ] && si "$bit: -spegni lo fa nero" || no "$bit: dopo -spegni non e' nero"
    [ "$l_c" -gt 0 ] && si "$bit: un tasto lo riaccende" || no "$bit: il tasto non lo riaccende"
    [ "$l_d" -eq 0 ] && si "$bit: dopo un minuto senza tasti e' nero da solo" || no "$bit: dopo un minuto non si e' spento"
    [ "$l_e" -gt 0 ] && si "$bit: il mouse lo riaccende" || no "$bit: il mouse non lo riaccende"
    echo "$L" | grep -aq "spegnimenti dall'avvio del servizio: 2" && si "$bit: conta 2 spegnimenti" \
        || no "$bit: non conta 2 spegnimenti"
    echo "$L" | grep -aq "opzione sconosciuta '-xyz'" && si "$bit: opzione sconosciuta rifiutata" || no "$bit: -xyz"
    [ "$(echo "$L" | grep -ac 'servizio:   spento')" -ge 2 ] && si "$bit: -ferma lo spegne" || no "$bit: -ferma"
    echo "$L" | grep -aq "FAULT\|non implementata" && no "$bit: FAULT o syscall mancante nel log" || si "$bit: niente FAULT"
}

if [ $# -ge 1 ]; then giro "$1"; else giro 32; giro 64; fi
echo "=== $ok riusciti, $no no ==="
[ "$no" = 0 ]
