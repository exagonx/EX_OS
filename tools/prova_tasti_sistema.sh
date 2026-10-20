#!/bin/bash
# =============================================================================
# tools/prova_tasti_sistema.sh — Ctrl+Alt+Canc (@TASTI-SISTEMA)
#
#   1. in grafica: la prima pressione arriva al server, che la gira alla
#      barra; la fotografia deve mostrare la domanda «Ctrl+Alt+Canc»;
#   2. in grafica: la seconda entro 5 secondi riavvia DAL SERVER;
#   3. in console di testo: la prima scrive l'avviso, la seconda riavvia —
#      e lo fa il driver della tastiera, che risponde anche quando tutto il
#      resto e' bloccato.
#
# ! IL VERDETTO LO DA' LA SERIALE: QEMU gira con -no-reboot, quindi un
# riavvio chiude la macchina, e l'ultima riga scritta e' quella del kernel.
#
# ! FRA LE DUE PRESSIONI NIENTE FOTOGRAFIE: una fotografia costa piu' di un
# secondo (1,4 MB), e due di troppo portano la seconda pressione fuori dai
# cinque secondi — e' successo, e sembrava un difetto del codice.
#
#     tools/prova_tasti_sistema.sh [directory-di-lavoro]
#
# Vuole: dist/exos.iso aggiornato (make iso-exos).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-tasti}"
mkdir -p "$D"
rm -f "$D"/*.ppm
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
export EXOS_ISTANZA=tasti EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
SER=/tmp/exos/serialtasti.txt
esito=0

seriale() { tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g'; }

echo "=== 1. in grafica, la prima pressione: la domanda ==="
rm -f "$SER"
timeout 300 python3 tools/qemu_drive.py "exwin@12" "key:alt-f5@3" \
    "key:ctrl-alt-delete@3" "foto:$D/domanda.ppm@1" > "$D/1.log" 2>&1
if python3 - "$D/domanda.ppm" <<'EOF'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)
w, h = map(int, d[1].split()); px = d[3]
# La barra del titolo attiva, in mezzo allo schermo: e' il dialogo.
n = sum(1 for y in range(h // 3, 2 * h // 3) for x in range(w // 4, 3 * w // 4)
        if px[(y * w + x) * 3:(y * w + x) * 3 + 3] == bytes((30, 77, 125)))
sys.exit(0 if n > 1000 else 1)
EOF
then echo "  [OK]  la barra chiede che cosa fare"
else echo "  [NO]  nessuna domanda sullo schermo (vedi $D/domanda.ppm)"; esito=1; fi

echo "=== 2. in grafica, la seconda entro 5 secondi: riavvio dal server ==="
rm -f "$SER"
timeout 300 python3 tools/qemu_drive.py "exwin@12" "key:alt-f5@3" \
    "key:ctrl-alt-delete@2" "key:ctrl-alt-delete@4" > "$D/2.log" 2>&1
if seriale | grep -q "wserver: Ctrl+Alt+Canc due volte: riavvio" &&
   seriale | grep -q "Riavvio del sistema in corso"; then
    echo "  [OK]  il server riavvia, il kernel sincronizza"
else
    echo "  [NO]  nessun riavvio (vedi $SER)"; esito=1
fi

echo "=== 3. in console di testo: avviso, poi riavvio dal driver ==="
rm -f "$SER"
timeout 300 python3 tools/qemu_drive.py "echo PRONTO@2" \
    "key:ctrl-alt-delete@2" "key:ctrl-alt-delete@4" > "$D/3.log" 2>&1
if seriale | grep -q "premilo di nuovo entro 5 secondi" &&
   seriale | grep -q "kbd: Ctrl+Alt+Canc due volte: riavvio" &&
   seriale | grep -q "Riavvio del sistema in corso"; then
    echo "  [OK]  prima l'avviso, poi il riavvio"
else
    echo "  [NO]  la console di testo non ha fatto le due cose (vedi $SER)"; esito=1
fi

exit $esito
