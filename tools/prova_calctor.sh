#!/bin/bash
# =============================================================================
# tools/prova_calctor.sh — la calcolatrice di ExWin (@CALCTOR)
#
# Il verdetto lo da' il NASTRO, non lo schermo: si fanno i calcoli dalla
# tastiera nelle tre modalita', si salva la cronologia con Ctrl+S su un disco
# di prova, e il file si legge qui fuori con debugfs. Ogni riga deve dire il
# risultato giusto:
#
#   Normale        2+3*4 = 14 (le precedenze), 200+10% = 220 (la percentuale
#                  da ufficio), 7/0 = errore (e la riga NON finisce nel nastro)
#   Scientifica    2^10 = 1024, (1+2)*3 = 9 — ci si arriva col MENU, Opzioni >
#                  Modalita > Scientifica, cioe' passando dalla tendina laterale
#   Programmatore  12&10 = 8, 1<4 = 16 (shift), 7%3 = 1 (mod)
#
# Le fotografie ($D/*.ppm) sono per chi guarda: la disposizione nelle tre
# modalita', e la tendina laterale aperta.
#
#     tools/prova_calctor.sh [directory-di-lavoro]   (default /tmp/exos-calctor)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), sfdisk e debugfs.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-calctor}"
mkdir -p "$D"
rm -f "$D"/*.ppm
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

export EXOS_ISTANZA=calctor EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1

# F10 apre File; destra = Opzioni; Invio sulla prima voce (Modalita) apre la
# tendina laterale; giu' scende fra Normale, Scientifica, Programmatore.
{
    echo "mount hd0p1 /disk@6"; echo "exwin@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/calctor &@8"; echo "key:alt-f5@3"
    echo "foto:$D/1-normale.ppm@1"
    echo "2+3*4@2"; echo "200+10%@2"; echo "7/0@2"; echo "key:esc@1"
    echo "key:f10@1"; echo "key:right@1"; echo "key:ret@1"; echo "key:down@1"
    echo "foto:$D/2-tendina.ppm@1"
    echo "key:ret@3"
    echo "foto:$D/3-scientifica.ppm@1"
    echo "2^10@2"; echo "(1+2)*3@2"
    echo "key:f10@1"; echo "key:right@1"; echo "key:ret@1"; echo "key:down@1"; echo "key:down@1"; echo "key:ret@3"
    echo "12&10@2"; echo "1<4@2"; echo "7%3@2"
    echo "foto:$D/4-programmatore.ppm@1"
    echo "key:ctrl-s@3"
    echo "foto:$D/5-salva.ppm@1"
    for i in $(seq 1 40); do echo "key:backspace@0"; done
    echo "/disk/nastro.txt@4"
    echo "foto:$D/6-salvato.ppm@1"
    echo "key:ret@2"
    echo "key:alt-f1@2"; echo "ls /disk@3"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

rm -f "$D/nastro.txt"
"$DEBUGFS" -R "dump /nastro.txt $D/nastro.txt" "$OFF" > /dev/null 2>&1
echo "=== il nastro salvato ==="
sed 's/^/    /' "$D/nastro.txt" 2>/dev/null

esito=0
controlla() {   # riga-attesa descrizione
    if grep -qxF "$1" "$D/nastro.txt" 2>/dev/null; then echo "  [OK]  $2"
    else echo "  [NO]  $2: manca «$1»"; esito=1; fi
}
controlla "2 + 3 * 4 = 14"                 "le precedenze"
controlla "200 + 20 = 220"                 "la percentuale da ufficio (il 10% di 200)"
controlla "2 ^ 10 = 1024"                  "x^y, in scientifica (arrivata dal menu laterale)"
controlla "(1 + 2) * 3 = 9"                "le parentesi"
controlla "12 AND 10 = 8  (DEC)"           "AND, in programmatore"
controlla "1 << 4 = 16  (DEC)"             "lo shift"
controlla "7 mod 3 = 1  (DEC)"             "il resto"
if grep -q "7 / 0" "$D/nastro.txt" 2>/dev/null; then
    echo "  [NO]  la divisione per zero e' finita nel nastro"; esito=1
else
    echo "  [OK]  la divisione per zero non entra nel nastro"
fi

echo ""
echo "  Le fotografie sono in $D/*.ppm"
exit $esito
