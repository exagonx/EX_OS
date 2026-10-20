#!/bin/bash
# =============================================================================
# tools/prova_wiki.sh — una voce vera di Wikipedia, e quanto ci mette
# (@NAV-WIKI, 30 settembre 2026)
#
#     bash tools/prova_wiki.sh [voce] [directory-di-lavoro]
#
# Avvia EX-OS in QEMU con la rete dell'host, apre la voce (Venezia se non se
# ne dice un'altra) in EXBrowser, aspetta, fotografa, e riporta:
#   - la riga «tempi:» che EXBrowser scrive sulla seriale (rete, script, CSS,
#     impaginazione), che e' il numero da confrontare fra un giorno e l'altro;
#   - le righe dei fogli di stile e ogni errore di script.
#
# ! SERVE INTERNET SULL'HOST: la rete di QEMU e' quella «user», che esce dalla
# macchina che la fa girare. Senza, la riga dei tempi non arriva e lo si dice.
#
# Il 29 settembre 2026 Venezia dava: rete 45770 ms in 71 richieste, script
# 620 ms, css 300 ms (1077 regole), impaginazione 1190 ms.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
VOCE="${1:-Venezia}"
D="${2:-/tmp/exos-wiki}"
mkdir -p "$D"
rm -f "$D"/*.ppm
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SER=/tmp/exos/serialwiki.txt
rm -f "$SER"

export EXOS_ISTANZA=wiki EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso EXOS_RAM=64M
timeout 900 python3 tools/qemu_drive.py "exwin -conta@14" "key:alt-f1@2" \
    "/exwin/bin/exbrowser https://it.wikipedia.org/wiki/$VOCE &@240" \
    "key:alt-f5@4" "foto:$D/voce.ppm@1" > "$D/1.log" 2>&1

tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g' > "$D/seriale.txt"
echo "=== $VOCE"
grep -a "exbrowser: foglio" "$D/seriale.txt" | sed 's/foglio .*load.php[^:]*:/foglio (load.php):/' | head -5
grep -a -i "exbrowser:.*\(errore\|error\|troncat\|fuori\)" "$D/seriale.txt" | head -10
if grep -a -q "exbrowser: tempi:" "$D/seriale.txt"; then
    grep -a "exbrowser: tempi:" "$D/seriale.txt" | tail -1
    echo "  La fotografia e' $D/voce.ppm"
    exit 0
fi
echo "  [NO]  la riga dei tempi non e' arrivata (rete? vedi $D/seriale.txt)"
exit 1
