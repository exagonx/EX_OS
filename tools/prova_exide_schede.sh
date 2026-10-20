#!/bin/bash
# =============================================================================
# tools/prova_exide_schede.sh — i sorgenti del progetto in schede, in exide
# (@EXIDE-SCHEDE, 29 settembre 2026)
#
# Un progetto minimo fatto sull'host (src/finestra.dis di una riga, uno.c,
# due.c). exide parte sul progetto (`exide /disk/pr`); Strumenti > Files da
# tastiera, Invio apre il primo sorgente; nella finestra dei file Ctrl+O apre
# due.c in una seconda scheda; si scrive una Z, Ctrl+Tab cambia scheda — e
# cambiando scheda exide SALVA, come fanno le linguette di «Sorgente» — poi
# Ctrl+W chiude la scheda. Il verdetto e' due.c sul disco.
#
#     tools/prova_exide_schede.sh [directory-di-lavoro]   (default /tmp/exos-ide-schede)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk, debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-ide-schede}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

export EXOS_ISTANZA=ideschede EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" > "$D/0.log" 2>&1
printf 'f 300 200 Prova\n' > "$D/finestra.dis"
printf 'int uno;\n' > "$D/uno.c"
printf 'int due;\n' > "$D/due.c"
for c in "mkdir pr" "mkdir pr/src" "write $D/finestra.dis pr/src/finestra.dis" \
         "write $D/uno.c pr/src/uno.c" "write $D/due.c pr/src/due.c"; do
    "$DEBUGFS" -w -R "$c" "$OFF" > /dev/null 2>&1
done

ARGS=("mount hd0p1 /disk@6" "exwin@14" "key:alt-f1@2" "/exwin/bin/exide /disk/pr &@12"
      "key:alt-f5@4" "key:f10@2" "key:right@1" "key:right@1"
      "key:down@1" "key:down@1" "key:down@1" "key:down@1" "key:ret@4"
      "foto:$D/1-files.ppm@1" "key:ret@4" "foto:$D/2-primo.ppm@1"
      "key:ctrl-o@4")
for i in $(seq 1 40); do ARGS+=("key:backspace@0"); done
ARGS+=("/disk/pr/src/due.c@5" "foto:$D/3-due-schede.ppm@1"
       "Z@n1" "key:ctrl-tab@3" "foto:$D/4-tornato.ppm@1"
       "key:ctrl-w@3" "foto:$D/5-chiusa.ppm@1"
       "key:alt-f1@2" "sync@3")
timeout 500 python3 tools/qemu_drive.py "${ARGS[@]}" > "$D/1.log" 2>&1

esito=0
"$DEBUGFS" -R "dump /pr/src/due.c $D/due-dopo.c" "$OFF" > /dev/null 2>&1
if [ "$(head -1 "$D/due-dopo.c" 2>/dev/null)" = "Zint due;" ]; then
    echo "  [OK]  due.c aperto in una scheda nuova, e salvato cambiando scheda"
else
    echo "  [NO]  due.c e' diventato: $(cat "$D/due-dopo.c" 2>/dev/null) (vedi $D/*.ppm)"; esito=1
fi
echo "  Le fotografie sono in $D"
exit $esito
