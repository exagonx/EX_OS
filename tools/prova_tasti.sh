#!/bin/bash
# =============================================================================
# tools/prova_tasti.sh — pressioni, rilasci e UTF-8 in ExWin (voce 42, punto 2)
#
# kbd.drv 0.007 manda l'evento lungo (rilascio, ripetizione, Unicode, tasto
# fisico, tutti i modificatori), wserver 0.014 lo gira alle finestre che lo
# chiedono (WIN_MSG_TASTI) ed exwin.so 0.021 lo da' con EXM_KEY_DOWN,
# EXM_KEY_UP ed ex_key_info; caselle e aree di testo scrivono in UTF-8. Qui
# ExWin parte dal CD con la tastiera italiana e /exwin/bin/finestreprova -t
# scrive sulla seriale ogni messaggio di tasto. La prova controlla:
#
#   1. la casella: a, e`, o`, Backspace, u`, AltGr+E, Shift+u` danno
#      «aèù€§» in UTF-8 (61 c3a8 c3b9 e282ac c2a7): l'accentata entra (fino
#      alla 0.020 la casella prendeva solo l'ASCII), il Backspace toglie la
#      lettera intera, e l'euro e il § ci sono (fino alla 0.006 di kbd.drv
#      le disposizioni erano in CP437, che non li ha);
#   2. l'area, dopo Tab: e`, freccia a sinistra, a danno «aè» (61 c3a8): la
#      freccia salta i due byte della e`;
#   3. i rilasci: «TASTI su» per la a (fisico 1e) e per lo Shift (2a);
#   4. lo Shift da solo e' «TASTI mod» (EXM_KEY_DOWN, f=2a);
#   5. il Ctrl DESTRO: Ctrl+Q con quello di destra arriva come c=20071
#      (fino a kbd.drv 0.006 il Ctrl destro non c'era);
#   6. la u della e` e' e8 (Unicode), non il byte CP437 8a.
#
#     tools/prova_tasti.sh [directory-di-lavoro]
#     EXOS_ARCH=x86_64 tools/prova_tasti.sh ...       (a 64 bit)
#
# Vuole dist/exos.iso (make iso-exos) o dist/exos64.iso (make iso64).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-tasti}"
mkdir -p "$D"
rm -f "$D"/*.ppm

if [ "${EXOS_ARCH:-i386}" = x86_64 ]; then
    export EXOS_CDROM=dist/exos64.iso EXOS_QEMU=qemu-system-x86_64
else
    export EXOS_CDROM=dist/exos.iso
fi
[ -f "$EXOS_CDROM" ] || { echo "manca $EXOS_CDROM" >&2; exit 1; }
export EXOS_ISTANZA=tasti EXOS_NO_FLOPPY=1 EXOS_RAM="${EXOS_RAM:-512M}"
SER=/tmp/exos/serialtasti.txt

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

# Sulla tastiera italiana: bracket_left = e`, semicolon = o`, backslash = u`.
# ! `keymap it` DOPO il comando: le righe si battono come su una tastiera
# americana (qemu_drive.py), e con l'italiana gia' accesa «-t» e «/» uscivano
# altri segni.
timeout 300 python3 tools/qemu_drive.py \
    "exwin@15" "key:alt-f1@2" "/exwin/bin/finestreprova -t 60 &@2" "keymap it@2" \
    "key:alt-f5@4" \
    "key:a@1" "key:bracket_left@1" "key:semicolon@1" "key:backspace@1" \
    "key:backslash@1" "key:alt_r-e@1" "key:shift-backslash@1" \
    "key:tab@1" "foto:$D/tab.ppm@1" "key:bracket_left@1" "key:left@1" "key:a@1" \
    "key:shift@1" "key:ctrl_r-q@1" \
    "foto:$D/tasti.ppm@1" "key:alt-f1@50" > "$D/qemu.log" 2>&1
tr -d '\r' < "$SER" > "$D/seriale.txt" 2>/dev/null

echo "=== tasti ==="
grep -a "TASTI" "$D/seriale.txt" | sed 's/^/        /'
if grep -aq "TASTI casella 61c3a8c3b9e282acc2a7$" "$D/seriale.txt"; then ok "casella: aèù€§ in UTF-8, il Backspace toglie la lettera intera"
else no "casella: non c'e' 61c3a8c3b9e282acc2a7"; fi
if grep -aq "TASTI area 61c3a8$" "$D/seriale.txt"; then ok "area: la freccia salta la è intera"
else no "area: non c'e' 61c3a8"; fi
if grep -aq "TASTI su c=61 u=61 f=1e" "$D/seriale.txt"; then ok "il rilascio della a"
else no "manca il rilascio della a"; fi
if grep -aq "TASTI mod c=0 u=0 f=2a" "$D/seriale.txt" && grep -aq "TASTI su c=0 u=0 f=2a" "$D/seriale.txt"
then ok "lo Shift da solo: premuto e rilasciato"
else no "lo Shift da solo non arriva"; fi
if grep -aq "TASTI giu c=20071 .*f=10" "$D/seriale.txt"; then ok "Ctrl destro + Q"
else no "Ctrl destro + Q non arriva come c=20071"; fi
if grep -aq "TASTI su c=8a u=e8 f=1a" "$D/seriale.txt"; then ok "la è e' U+00E8 (il codice vecchio resta 8a)"
else no "la è non porta U+00E8"; fi
if [ -s "$D/tasti.ppm" ]; then ok "fotografia: $D/tasti.ppm"
else no "nessuna fotografia"; fi

[ $esito = 0 ] && echo "ESITO: TUTTO A POSTO" || echo "ESITO: NON VA"
exit $esito
