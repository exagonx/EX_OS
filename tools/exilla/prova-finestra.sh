#!/bin/bash
# =============================================================================
# tools/exilla/prova-finestra.sh — Firefox in una finestra di ExWin (tappa 7)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/exilla/prova-finestra.sh        (dopo tools/exilla/prova-gecko.sh)
#
# Usa il disco che prova-gecko.sh ha preparato (Firefox, i caratteri, la
# pagina di prova), avvia EX-OS, accende ExWin e lancia Firefox SENZA
# --headless: la finestra la da' il server a finestre. Fotografa lo schermo
# ogni FOTO_OGNI secondi (Firefox impiega minuti a partire in QEMU), poi
# muove il mouse sulla pagina e clicca, e fotografa ancora.
#
# Le fotografie finiscono in cross_build/<macchina>/costruzione-prova/finestra/
# come PNG. La prova e' riuscita se nell'ultima si vede la pagina: guardarle e'
# il giudizio, la seriale dice solo se qualcosa e' caduto.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
B="$PWD/cross_build/$MACCHINA"
D="$B/costruzione-prova"
F="$D/finestra"
IMG="$D/gecko-hd.img"
# ! IL DISCO SI RIFA SE FIREFOX E' PIU' NUOVO: prova-gecko.sh impacchetta e
# prepara il disco (e intanto prova la modalita' senza finestre). Senza questo
# si provava il binario di un giro prima, senza saperlo.
# ! SI CONFRONTA COL PACCHETTO, NON COL DISCO: QEMU scrive sul disco a ogni
# prova, e il disco risulterebbe sempre piu' nuovo di Firefox.
O="$PWD/cross_build/exilla-obj/gecko"
if [ ! -f "$IMG" ] || [ "$O/dist/bin/firefox" -nt "$O/dist/firefox/firefox" ]; then
    tools/exilla/prova-gecko.sh > "$D/prova-gecko-da-finestra.log" 2>&1
    echo "prova-gecko (pacchetto e disco): $(tail -1 "$D/prova-gecko-da-finestra.log")"
fi
[ -f "$IMG" ] || { echo "manca $IMG" >&2; exit 1; }
# ! LE FOTO PASSANO DA /tmp/exos: il percorso del progetto contiene la «@»
# dell'utente, e qemu_drive.py la prenderebbe per l'attesa («cmd@secondi»).
T=/tmp/exos/finestra-foto
rm -rf "$F" "$T"; mkdir -p "$F" "$T"

KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
CD="$B/costruzione-sistema/albero/dist/exos.iso"; [ -f "$CD" ] || CD="$PWD/dist/exos.iso"
export EXOS_ISTANZA=finestra EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD" EXOS_RAM="${EXOS_RAM:-2048M}"
export EXOS_QEMU_EXTRA="$KVM -drive file=$IMG,format=raw,if=ide"
rm -f /tmp/exos/serialfinestra.txt

FOTO_OGNI="${FOTO_OGNI:-120}"
FOTO_N="${FOTO_N:-5}"
{
    echo "mount hd0p1 /disk@5"
    for v in MOZ_FORCE_DISABLE_E10S=1 HOME=/disk/casa LANG=it_IT.UTF-8 \
             EXILLA_FONTS=/disk/font EXILLA_DIAG=1 $AMBIENTE; do echo "export $v@1"; done
    echo "exwin@14"
    echo "key:alt-f1@2"
    echo "/disk/firefox/firefox -no-remote -profile /disk/profilo file:///disk/prova.html &@3"
    echo "key:alt-f5@3"
    for i in $(seq 1 "$FOTO_N"); do
        echo "foto:$T/$i.ppm@$FOTO_OGNI"
    done
    # CAMPIONI=n: n volte i registri della CPU, per vedere chi gira (EIP).
    for i in $(seq 1 "${CAMPIONI:-0}"); do echo "regs:$T/regs.txt@0"; done
    # Il mouse di QEMU e' relativo: prima nell'angolo, poi sulla pagina.
    for i in $(seq 1 8); do echo "mon:mouse_move -600 -600@0"; done
    for i in $(seq 1 30); do echo "mon:mouse_move 10 10@0"; done
    echo "foto:$T/mouse.ppm@5"
    echo "mon:mouse_button 1;mouse_button 0@5"
    echo "foto:$T/clic.ppm@10"
    # La barra degli indirizzi: dall'angolo a (400,76) dello schermo, un clic,
    # poi l'indirizzo scritto con la tastiera e Invio (la tastiera va alla
    # finestra col fuoco: Firefox).
    for i in $(seq 1 8); do echo "mon:mouse_move -600 -600@0"; done
    for i in $(seq 1 40); do echo "mon:mouse_move 10 0@0"; done
    for i in $(seq 1 7); do echo "mon:mouse_move 0 11@0"; done
    echo "mon:mouse_button 1;mouse_button 0@4"
    echo "foto:$T/barra.ppm@2"
    echo "file:///disk/seconda.html@1"
    for i in $(seq 1 "${CAMPIONI_INVIO:-0}"); do echo "regs:$T/regs-invio.txt@0"; done
    echo "foto:$T/dopo-invio.ppm@20"
    echo "foto:$T/seconda.ppm@${ATTESA_PAGINA:-40}"
    echo "foto:$T/seconda-dopo.ppm@2"
    for i in $(seq 1 "${CAMPIONI_FINE:-0}"); do echo "regs:$T/regs-fine.txt@0"; done
    echo "key:alt-f1@3"
    echo "stack@4"
    echo "mem@3"
    echo "echo FINE-FINESTRA@2"
} > "$F/args.txt"
mapfile -t A < "$F/args.txt"
timeout $(( FOTO_OGNI * FOTO_N + 900 )) python3 tools/qemu_drive.py "${A[@]}" > "$F/qemu.log" 2>&1

[ -f "$T/regs.txt" ] && cp "$T/regs.txt" "$F/regs.txt"
[ -f "$T/regs-fine.txt" ] && cp "$T/regs-fine.txt" "$F/regs-fine.txt"
[ -f "$T/regs-invio.txt" ] && cp "$T/regs-invio.txt" "$F/regs-invio.txt"
for p in "$T"/*.ppm; do
    [ -f "$p" ] || continue
    q="$F/$(basename "${p%.ppm}").png"
    python3 tools/ppm2png.py "$p" "$q" > /dev/null 2>&1 && rm -f "$p"
done
tr -d '\r' < /tmp/exos/serialfinestra.txt | sed 's/\x1b\[[0-9;]*m//g' | grep -av ENTROPIA \
    | grep -a "FAULT\|PF:\|Exilla\|exilla:\|firefox\|wserver\|MOZ_CRASH\|Assertion" | tail -40
tr -d '\r' < /tmp/exos/serialfinestra.txt | sed 's/\x1b\[[0-9;]*m//g' | sed -n '/> stack/,/FINE-FINESTRA/p' | head -60
echo "=== fotografie in $F ==="
ls "$F"/*.png 2>/dev/null
