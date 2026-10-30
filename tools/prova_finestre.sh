#!/bin/bash
# =============================================================================
# tools/prova_finestre.sh — molte finestre insieme (voce 42, punto 1)
#
# wserver 0.013 ed exwin.so 0.020 non hanno piu' un numero fisso di finestre:
# fino alla 0.012 la diciassettesima veniva rifiutata e l'elenco della barra
# ne portava sedici al massimo. Qui ExWin parte dal CD, e dalla console 1 si
# lancia /exwin/bin/finestreprova con N finestre (40 se non si dice), ognuna
# con un titolo piu' lungo dei 48 byte di una volta. La prova controlla:
#
#   1. sulla seriale «FINESTRE aperte N di N» (il server le ha date tutte);
#   2. «FINESTRE fine 0»: l'elenco, arrivato a pezzi, le ha tutte e N;
#   3. «FINESTRE chiuse N»: chiuderle tutte di fila non ferma il server (fino
#      alla 0.012 l'elenco a chi lo segue si mandava aspettando la sua casella
#      di 4 posti, piena perche' il programma stava chiudendo: il server
#      restava fermo dieci secondi a pezzo e lo schermo con lui);
#   4. «CONTROLLI fine 0» da «finestreprova -c»: combo, menu, aree di testo,
#      casella e lista riempiti oltre i tetti di una volta (exwin.so 0.020);
#   5. una fotografia con le finestre aperte e una dopo la chiusura, da
#      guardare (titoli tagliati prima dei pulsanti; dopo, desktop pulito e
#      barra delle applicazioni vuota: la barra segue ancora l'elenco anche
#      se finestreprova l'ha chiesto per se').
#
#     tools/prova_finestre.sh [directory-di-lavoro] [N]
#     EXOS_ARCH=x86_64 tools/prova_finestre.sh ...       (a 64 bit)
#
# Vuole dist/exos.iso (make iso-exos) o dist/exos64.iso (make iso64).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-finestre}"
N="${2:-40}"
mkdir -p "$D"
rm -f "$D"/*.ppm

if [ "${EXOS_ARCH:-i386}" = x86_64 ]; then
    export EXOS_CDROM=dist/exos64.iso EXOS_QEMU=qemu-system-x86_64
else
    export EXOS_CDROM=dist/exos.iso
fi
[ -f "$EXOS_CDROM" ] || { echo "manca $EXOS_CDROM" >&2; exit 1; }
export EXOS_ISTANZA=finestre EXOS_NO_FLOPPY=1 EXOS_RAM="${EXOS_RAM:-512M}"
SER=/tmp/exos/serialfinestre.txt

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

timeout 600 python3 tools/qemu_drive.py \
    "exwin@15" "key:alt-f1@2" "/exwin/bin/finestreprova $N 30 &@1" \
    "key:alt-f5@40" "foto:$D/aperte.ppm@1" "key:alt-f1@40" \
    "key:alt-f5@2" "foto:$D/chiuse.ppm@6" \
    "key:alt-f1@2" "/exwin/bin/finestreprova -c 60 &@1" "key:alt-f5@30" \
    "foto:$D/controlli.ppm@1" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" > "$D/seriale.txt" 2>/dev/null

echo "=== molte finestre ($N) ==="
grep -a "FINESTRE" "$D/seriale.txt" | sed 's/^/        /'
if grep -aq "FINESTRE aperte $N di $N" "$D/seriale.txt"; then ok "il server le ha date tutte e $N"
else no "il server non le ha date tutte (vedi $D/seriale.txt)"; fi
if grep -aq "FINESTRE fine 0" "$D/seriale.txt"; then ok "l'elenco a pezzi le ha tutte e $N"
else no "l'elenco non le ha tutte"; fi
if grep -aq "FINESTRE chiuse $N" "$D/seriale.txt"; then ok "chiuse tutte e $N di fila"
else no "la chiusura si e' fermata"; fi
grep -a "CONTROLLI" "$D/seriale.txt" | sed 's/^/        /'
if grep -aq "CONTROLLI fine 0" "$D/seriale.txt"; then ok "i controlli tengono tutto"
else no "un controllo si e' fermato (vedi le righe CONTROLLI)"; fi
if grep -aq "coda eventi\|niente memoria" "$D/seriale.txt"; then no "il server dice di aver perso qualcosa"; fi
if [ -s "$D/aperte.ppm" ]; then ok "fotografia: $D/aperte.ppm"
else no "nessuna fotografia"; fi

[ $esito = 0 ] && echo "ESITO: TUTTO A POSTO" || echo "ESITO: NON VA"
exit $esito
