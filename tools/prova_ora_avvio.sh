#!/bin/bash
# =============================================================================
# tools/prova_ora_avvio.sh — the clock from the network at boot (date 0.003)
#
#     tools/prova_ora_avvio.sh [dir]                     32 bit
#     EXOS_ARCH=x86_64 tools/prova_ora_avvio.sh [dir]    64 bit
#
# The RTC starts from 2007-09-01, as on the laptop with the dead CMOS battery:
#   with the network    the year is the real one, with no line on the screen
#   without a network   the boot reaches the prompt and the clock stays put
# pool.ntp.org for real, through QEMU's user network.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
ARCH="${EXOS_ARCH:-i386}"
D="${1:-/tmp/exos-prove/ora-$ARCH}"
rm -rf "$D"; mkdir -p "$D"
if [ "$ARCH" = x86_64 ]; then
    ISO=dist/exos64.iso; export EXOS_QEMU=qemu-system-x86_64 EXOS_RAM=256M; EXTRA="-cpu qemu64"
else
    ISO=dist/exos.iso; export EXOS_RAM=64M; EXTRA=""
fi
[ -f "$ISO" ] || { echo "manca $ISO" >&2; exit 1; }
export EXOS_ISTANZA=ora$ARCH EXOS_NO_FLOPPY=1 EXOS_CDROM="$ISO"

esito=0
ANNO=$(date +%Y)

echo "=== 1. RTC al 2007 e la rete ==="
EXOS_QEMU_EXTRA="$EXTRA -rtc base=2007-09-01T12:00:00 -nic user,model=e1000" \
    timeout 200 python3 tools/qemu_drive.py "date -d@25" "date -d@3" > "$D/1-rete.log" 2>&1
if grep -a -q "^$ANNO-" "$D/1-rete.log"; then
    echo "  [OK]  l'orologio e' passato al $ANNO"
else
    echo "  [NO]  orologio non rimesso: vedi $D/1-rete.log"; esito=1
fi
if grep -a -q -E "^ntp:|orologio rimesso|orologio era" "$D/1-rete.log"; then
    echo "  [NO]  ha stampato qualcosa all'avvio"; esito=1
else
    echo "  [OK]  in silenzio"
fi

echo "=== 2. RTC al 2007 senza rete ==="
EXOS_QEMU_EXTRA="$EXTRA -rtc base=2007-09-01T12:00:00 -nic none" \
    timeout 200 python3 tools/qemu_drive.py "date -d@8" > "$D/2-senza.log" 2>&1
if grep -a -q "^2007-" "$D/2-senza.log"; then
    echo "  [OK]  si arriva al prompt, l'orologio resta dov'era"
else
    echo "  [NO]  vedi $D/2-senza.log"; esito=1
fi

[ "$esito" = 0 ] && echo "TUTTO A POSTO" || echo "QUALCOSA NON VA"
exit $esito
