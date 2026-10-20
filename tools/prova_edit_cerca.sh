#!/bin/bash
# =============================================================================
# tools/prova_edit_cerca.sh — cerca e sostituisci nell'editor di ExWin
#
# Prova @EDIT-CERCA sul file salvato, non sullo schermo:
#   1. «Tutte»: in «uno due uno / tre uno» ogni «uno» diventa «UNO»;
#   2. «Una», poi F3: in «a x a x a» la prima «a» cambia subito, la seconda
#      con F3 — che dopo «Una» sostituisce — e la terza resta com'e'.
#
# ! I PULSANTI SI SCELGONO CON Shift+Tab: nel dialogo delle scelte l'Invio
# prende l'ultimo, «Annulla», per contratto (lib/exdlg, ex_dlg_scegli).
#
#     tools/prova_edit_cerca.sh [directory-di-lavoro]
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), debugfs (e2fsprogs).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-edit-cerca}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

export EXOS_ISTANZA=editcerca EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1
printf 'uno due uno\ntre uno\n' > "$D/a.txt"
printf 'a x a x a\n' > "$D/b.txt"
"$DEBUGFS" -w -R "write $D/a.txt a.txt" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/b.txt b.txt" "$OFF" > /dev/null 2>&1

esito=0

echo "=== 1. Sostituisci, Tutte ==="
timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "exwin@12" "key:alt-f1@2" \
    "/exwin/bin/exeditor /disk/a.txt &@6" "key:alt-f5@3" \
    "key:ctrl-h@2" "uno@2" "UNO@2" "key:shift-tab@1" "key:shift-tab@1" "key:ret@3" \
    "key:ctrl-s@3" "key:alt-f1@2" "sync@2" > "$D/1.log" 2>&1
"$DEBUGFS" -R "dump /a.txt $D/a-dopo.txt" "$OFF" > /dev/null 2>&1
if [ "$(cat "$D/a-dopo.txt" 2>/dev/null)" = "$(printf 'UNO due UNO\ntre UNO')" ]; then
    echo "  [OK]  tutte e tre sostituite, su due righe"
else
    echo "  [NO]  il file e' diventato:"; cat "$D/a-dopo.txt" 2>/dev/null | sed 's/^/        /'; esito=1
fi

echo "=== 2. Sostituisci, Una, poi F3 ==="
timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "exwin@12" "key:alt-f1@2" \
    "/exwin/bin/exeditor /disk/b.txt &@6" "key:alt-f5@3" \
    "key:ctrl-h@2" "a@2" "b@2" "key:shift-tab@1" "key:ret@3" \
    "key:f3@2" "key:ctrl-s@3" "key:alt-f1@2" "sync@2" > "$D/2.log" 2>&1
"$DEBUGFS" -R "dump /b.txt $D/b-dopo.txt" "$OFF" > /dev/null 2>&1
if [ "$(cat "$D/b-dopo.txt" 2>/dev/null)" = "b x b x a" ]; then
    echo "  [OK]  la prima subito, la seconda con F3, la terza intatta"
else
    echo "  [NO]  il file e' diventato:"; cat "$D/b-dopo.txt" 2>/dev/null | sed 's/^/        /'; esito=1
fi

exit $esito
