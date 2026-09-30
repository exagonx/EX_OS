#!/bin/bash
# =============================================================================
# tools/prova_pennello_veloce.sh — Pennello col mouse mosso in fretta
# (28 settembre 2026, segnalato da chi lo usa: «tende a bloccare tutto se il
# mouse si muove troppo velocemente»)
#
# Col pulsante premuto e l'ellisse (lo strumento piu' pesante: a ogni
# movimento rifa' l'anteprima elastica) arrivano 300 movimenti a 10 ms l'uno —
# una mano che trascina in fretta — poi si lascia il pulsante e si preme
# di fotografare. Sei fotografie, un secondo l'una: si guarda in quale il
# PUNTATORE e' arrivato dove la raffica l'ha mandato, (400, 300): dentro la
# tela bianca. ! NON (500, 500), che e' sulla tavolozza: i quadratini scuri
# sembravano il puntatore, e la prova passava su un sistema fermo. Il
# puntatore lo muove il server grafico: se resta indietro, il server e'
# occupato a ricevere i disegni di Pennello — ed e' il «blocca tutto».
#
# ! E LA CAUSA NON ERA IL MOUSE: era la memoria (vedi pool_prendi in
# pennello.c). Coi 16 MB fissi dell'annulla il sistema restava con 204 KB.
#
# ! LA PRIMA STESURA CONTAVA I PIXEL DELL'ELLISSE, e sbagliava due volte: la
# raffica andava avanti e indietro (un punto, non un'ellisse), e poi l'ellisse
# mancava proprio perche' Pennello era indietro. Il puntatore misura la cosa
# che si vede: il sistema che non risponde. Sulla scrivania vuota la stessa
# raffica lo porta a destinazione subito (provato).
#
#     tools/prova_pennello_veloce.sh [directory-di-lavoro]
#
# Vuole: dist/exos.iso aggiornato (make iso-exos).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-pennveloce}"
mkdir -p "$D"
rm -f "$D"/*.ppm
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
export EXOS_ISTANZA=pennveloce EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso

raffica="mouse_button 1"
# ! LA RAFFICA DEVE ALLARGARE L'ELLISSE: il primo giro andava avanti e
# indietro, finiva vicino a dove era partito e disegnava un punto — la prova
# passava senza aver provato niente. Adesso 300 passi da (1, 1) con un
# tremolio, e la fotografia «0» controlla che l'ellisse ci sia.
for i in $(seq 1 100); do raffica="$raffica;mouse_move 2 1;mouse_move 1 2;mouse_move -2 -2"; done
raffica="$raffica;mouse_button 0"

{
    echo "exwin@14"; echo "key:alt-f1@2"
    echo "/exwin/bin/pennello &@8"; echo "key:alt-f5@3"
    echo "key:o@1"                                   # l'ellisse
    for i in $(seq 1 5); do echo "mon:mouse_move -600 -600@0"; done
    for i in $(seq 1 30); do echo "mon:mouse_move 10 0@0"; done
    for i in $(seq 1 20); do echo "mon:mouse_move 0 10@0"; done
    echo "monr:$raffica@0"
    for k in 0 1 2 3 4 5 6; do echo "foto:$D/$k.ppm@0"; done
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/1.log" 2>&1

# Il puntatore: pixel quasi neri nel quadrato attorno a (400, 300).
# ! DA 386 E DA 286, NON DA 396: il puntatore di QEMU arriva 10 px prima, e
# la sua punta sta a (390, 290). Col quadrato da 396 la prova diceva «il
# server e' intasato» su un sistema fermo, con l'ellisse gia' disegnata (30
# settembre 2026). L'ellisse (centro 340,250, raggio 50) a y 286 arriva a x
# 375: nel quadrato non entra.
arrivato() {
    python3 - "$1" <<'PY'
import sys
d = open(sys.argv[1], "rb").read(); p = d.split(b"\n", 3)
w, h = map(int, p[1].split()); px = p[3]
n = sum(1 for y in range(286, 316) for x in range(386, 416)
        if px[(y*w+x)*3] < 30 and px[(y*w+x)*3+1] < 30 and px[(y*w+x)*3+2] < 30)
print(1 if n >= 5 else 0)
PY
}
quando=""
for k in 0 1 2 3 4 5 6; do
    [ -s "$D/$k.ppm" ] || continue
    a=$(arrivato "$D/$k.ppm"); echo "        foto $k (circa ${k}s dopo la raffica): puntatore arrivato = $a"
    if [ "$a" = 1 ] && [ -z "$quando" ]; then quando=$k; fi
done
# ! LA CAUSA VERA ERA LA MEMORIA: Pennello prendeva 16 MB per l'annulla e,
# sui 32 MB di QEMU, al primo trascinamento il kernel scriveva «PMM: OUT OF
# MEMORY» e si fermava tutto. Qui si controlla anche che non lo scriva.
if tr -d '\r' < /tmp/exos/serialpennveloce.txt | grep -q "OUT OF MEMORY"; then
    echo "  [NO]  la memoria e' finita (PMM: OUT OF MEMORY sulla seriale)"; exit 1
fi
echo "  [OK]  la memoria non finisce"
if [ -n "$quando" ] && [ "$quando" -le 1 ]; then
    echo "  [OK]  il puntatore arriva entro ${quando}s: Pennello non intasa il server"; exit 0
fi
echo "  [NO]  il puntatore resta indietro (arrivato alla foto: ${quando:-mai}): il server e' intasato"
exit 1
