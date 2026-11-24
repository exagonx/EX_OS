#!/bin/bash
# =============================================================================
# tools/prova_console_utente.sh - an ordinary user at the console keeps the
# machine's devices (kernel 0.359, voce 111 step 4b)
#
#     tools/prova_console_utente.sh                 32 bit
#     EXOS_ARCH=x86_64 tools/prova_console_utente.sh
#
# Uses the disk that tools/prova_permessi.sh installed (root/root,
# mario/mario): run that first. mario logs in at console 0 and starts ExWin:
#   1  the kernel knows mario sits at console 0 ("CONSOLE 0: entra l'uid 1000");
#   2  the desktop is drawn (keyboard, mouse, accelerator, framebuffer and
#      wserver all take mario: the photo is not the black text console);
#   3  no "non puo' scrivere" and no "non siede alla console" in dmesg;
#   4  after exit (the shell gone) console 0 is empty again ("esce l'uid").
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
PD="/tmp/exos-prove/permessi-$ARCH"
D="/tmp/exos-prove/console-$ARCH"
if [ "$ARCH" = x86_64 ]; then Q=qemu-system-x86_64; EXTRA="-cpu qemu64"; else Q=qemu-system-i386; EXTRA=""; fi
[ -f "$PD/hd.img" ] || { echo "manca $PD/hd.img: prima tools/prova_permessi.sh" >&2; exit 1; }
rm -rf "$D"; mkdir -p "$D"; cp "$PD/hd.img" "$D/hd.img"
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
ok=0; no=0
si() { echo "  [OK] $1"; ok=$((ok+1)); }
no() { echo "  [NO] $1"; no=$((no+1)); }

A=("mario@2" "mario@6" "keymap us@3"
   "echo CHI; dmesg | grep -a 'CONSOLE [0-9]:'; echo FINE-CHI@4"
   "exwin@25" "foto:$D/scrivania.ppm@3"
   "key:alt-f1@3"
   "echo RIFIUTI; dmesg | grep -a \"non puo' scrivere\|non siede alla console\"; echo FINE-RIFIUTI@5"
   "exit@4" "root@2" "root@6" "keymap us@3"
   "echo DOPO; dmesg | grep -a 'CONSOLE [0-9]:'; echo FINE-DOPO@4")
EXOS_QEMU=$Q EXOS_ISTANZA=cu$ARCH EXOS_NO_FLOPPY=1 EXOS_RAM=512M EXOS_MARCA="utente:" \
EXOS_QEMU_EXTRA="$KVM $EXTRA -smp 2 -drive file=$D/hd.img,format=raw,if=ide -nic none" \
    timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/qemu.log" 2>&1
L=$(tr -d '\r' < "$D/qemu.log")

echo "$L" | sed -n '/^CHI/,/^FINE-CHI/p' | grep -aq "CONSOLE 0: entra l'uid 1000" \
    && si "il kernel sa che mario siede alla console 0" || no "nessun «CONSOLE 0: entra l'uid 1000»"
if [ -f "$D/scrivania.ppm" ]; then
    # ! NOT A COUNT OF COLOURS: a plain desktop has few of them (8 on the
    # first run, drawn fine). The text console is black almost everywhere,
    # the desktop is not: under 50% black pixels is a drawn desktop.
    NERO=$(python3 - "$D/scrivania.ppm" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read(); p = d.split(b'\n', 3); px = p[3]
n = len(px) // 3; nero = sum(1 for i in range(0, n * 3, 3 * 7) if px[i] < 16 and px[i+1] < 16 and px[i+2] < 16)
print(nero * 100 // max(1, len(range(0, n * 3, 3 * 7))))
PY
)
    [ "${NERO:-100}" -lt 50 ] && si "la scrivania di mario e' disegnata (nero al $NERO%)" \
        || no "la scrivania non c'e' (nero al $NERO%, vedi $D/scrivania.png)"
    python3 tools/ppm2png.py "$D/scrivania.ppm" "$D/scrivania.png" > /dev/null 2>&1
else no "nessuna fotografia della scrivania"; fi
R=$(echo "$L" | sed -n '/^RIFIUTI/,/^FINE-RIFIUTI/p' | grep -a "uid 1000")
[ -z "$R" ] && si "nessun dispositivo rifiuta mario" || { echo "$R" | head -5 | sed 's/^/      /'; no "mario rifiutato"; }
echo "$L" | sed -n '/^DOPO/,/^FINE-DOPO/p' | grep -aq "CONSOLE 0: esce l'uid 1000" \
    && si "all'uscita la console 0 si svuota" || no "nessun «CONSOLE 0: esce l'uid 1000»"
echo "=== $ok riusciti, $no no ($ARCH) ==="
[ $no -eq 0 ]
