#!/bin/bash
# =============================================================================
# tools/prova_grep.sh — /bin/grep dentro EX-OS, contro il grep dell'host
#
# Stessi modelli sullo stesso file (boot/kernel.txt, che sul CD e' /boot/kernel.txt):
# i conteggi devono uguagliare quelli dell'host, anche attraverso una pipe.
#
#     tools/prova_grep.sh [directory-di-lavoro]
#     EXOS_ARCH=x86_64 tools/prova_grep.sh ...       (a 64 bit)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos/grep}"
mkdir -p "$D"
if [ "${EXOS_ARCH:-i386}" = x86_64 ]; then
    export EXOS_CDROM=dist/exos64.iso EXOS_QEMU=qemu-system-x86_64
else
    export EXOS_CDROM=dist/exos.iso
fi
[ -f "$EXOS_CDROM" ] || { echo "manca $EXOS_CDROM" >&2; exit 1; }
export EXOS_ISTANZA=grep EXOS_NO_FLOPPY=1 EXOS_RAM="${EXOS_RAM:-256M}"
F=boot/kernel.txt
T=/boot/kernel.txt
timeout 200 python3 tools/qemu_drive.py \
    "grep -c kernel $T@3" "grep -c -i KERNEL $T@3" "grep -c -v kernel $T@3" \
    "grep -c -E '^(#|=)' $T@3" "grep -c 'a\\{2,3\\}' $T@3" \
    "cat $T | grep -c kernel@3" "grep -n -m zzz $T@3" "grep nonesiste $T@3" \
    "grep -version@2" > "$D/qemu.log" 2>&1
tr -d '\r' < /tmp/exos/serialgrep.txt > "$D/seriale.txt" 2>/dev/null
esito=0
atteso() {   # il comando come battuto, il numero dell'host
    # ! senza "> " davanti: il primo comando si batte prima che esca il prompt
    r=$(grep -a -F -A8 -- "$1" "$D/seriale.txt" | grep -a -m1 -E '^[0-9]+$')
    if [ "$r" = "$2" ]; then echo "  [OK]  $1 = $2"; else echo "  [NO]  $1: $r invece di $2"; esito=1; fi
}
atteso "grep -c kernel $T" "$(LC_ALL=C grep -c kernel $F)"
atteso "grep -c -i KERNEL $T" "$(LC_ALL=C grep -c -i KERNEL $F)"
atteso "grep -c -v kernel $T" "$(LC_ALL=C grep -c -v kernel $F)"
atteso "grep -c -E '^(#|=)' $T" "$(LC_ALL=C grep -c -E '^(#|=)' $F)"
atteso "grep -c 'a\\{2,3\\}' $T" "$(LC_ALL=C grep -c 'a\{2,3\}' $F)"
atteso "cat $T | grep -c kernel" "$(LC_ALL=C grep -c kernel $F)"
[ $esito -eq 0 ] && echo "TUTTO A POSTO" || echo "QUALCOSA NON VA (vedi $D)"
exit $esito
