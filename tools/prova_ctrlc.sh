#!/bin/bash
# =============================================================================
# tools/prova_ctrlc.sh — Ctrl+C sulla console di testo (@TASTI-SISTEMA)
#
#   1. al prompt, a meta' riga: Ctrl+C svuota la riga e la shell resta viva
#      (il comando battuto prima NON parte, quello dopo si');
#   2. con /bin/textline in primo piano: Ctrl+C CHIEDE, nominando il
#      programma; «n» lo lascia vivo (risponde ancora ai suoi comandi);
#   3. di nuovo Ctrl+C e «s»: textline si ferma e torna il prompt.
#
# ! IL VERDETTO LO DA' LA SERIALE, dove finisce tutto cio' che la console
# scrive: le righe con i marcatori NON-DEVE-PARTIRE, VIVO-1, VIVO-2.
#
#     tools/prova_ctrlc.sh [directory-di-lavoro]    (default /tmp/exos-ctrlc)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-ctrlc}"
mkdir -p "$D"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
export EXOS_ISTANZA=ctrlc EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
SER=/tmp/exos/serialctrlc.txt
rm -f "$SER"

# «@n» dopo il ritardo: il testo parte SENZA Invio (vedi qemu_drive.py).
timeout 400 python3 tools/qemu_drive.py \
    "echo NON-DEVE-PARTIRE@n1" "key:ctrl-c@2" \
    "echo VIVO-1@2" \
    "textline /prova.txt@3" \
    "key:ctrl-c@2" "n@n2" \
    "h@2" \
    "key:ctrl-c@2" "s@n3" \
    "echo VIVO-2@2" \
    "foto:$D/console.ppm@1" > "$D/1.log" 2>&1

S="$D/seriale.txt"
tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$S"
esito=0
ok() { echo "  [OK]  $*"; }
no() { echo "  [NO]  $*"; esito=1; }

# 1. La riga cancellata non e' stata eseguita: il marcatore compare solo
#    nell'eco di cio' che si e' battuto, mai da solo su una riga.
if grep -qx "NON-DEVE-PARTIRE" "$S"; then no "la riga cancellata con Ctrl+C e' partita lo stesso"
else ok "al prompt Ctrl+C cancella la riga senza eseguirla"; fi
grep -qx "VIVO-1" "$S" && ok "la shell e' viva dopo Ctrl+C al prompt" || no "dopo Ctrl+C al prompt la shell non risponde"

# 2. La domanda, due volte, col nome del programma.
q=$(grep -c '\[Ctrl+C\] Fermo "textline"' "$S")
[ "$q" -ge 2 ] && ok "Ctrl+C chiede, e nomina textline ($q domande)" || no "la domanda con il nome di textline c'e' $q volte, non 2"

# «n» lo lascia vivo: il suo «h» (l'aiuto) risponde. L'aiuto di textline
# elenca i comandi; basta che dopo la prima domanda compaia qualcosa di suo.
if awk '/\[Ctrl\+C\] Fermo/{c++} c==1 && /COMANDI/{f=1} END{exit !f}' "$S"; then
    ok "con «n» textline resta vivo e risponde"
else
    no "dopo «n» textline non ha risposto al suo «h»"
fi

# 3. «s» lo ferma e la shell torna.
grep -qx "VIVO-2" "$S" && ok "con «s» textline si ferma e la shell torna" || no "dopo «s» la shell non e' tornata"
grep -q "kbd: Ctrl+C, programma fermato" "$S" && ok "il driver annota la fermata sulla seriale" || echo "  [--]  la riga del driver non c'e' sulla seriale"

echo ""
echo "  La seriale e' in $S, la fotografia in $D/console.ppm"
exit $esito
