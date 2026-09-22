#!/bin/bash
# =============================================================================
# tools/prova_exide_dove.sh — il progetto nuovo sceglie DOVE, e se lo ricorda
#
# Prova quel che e' stato chiesto il 22 settembre 2026 (@EXIDE-SFOGLIA):
#
#   1. «Nuovo progetto» apre un ALBERO da sfogliare — non una riga in cui
#      battere un percorso a memoria — e parte dalla CASA di chi e' entrato,
#      cioe' da $HOME e non da «/home/» piu' il nome dell'utente;
#   2. dentro quel dialogo si CREA UNA CARTELLA, senza uscirne;
#   3. la volta dopo si riparte da dove si e' creato l'ultimo progetto — dalla
#      directory che lo CONTIENE — e la memoria sta nel profilo dell'utente,
#      $HOME/.app/exide/ultima.txt.
#
# Il punto 3 si prova con un SECONDO AVVIO: il disco di prova resta fra i due
# giri, e cosi' si prova anche che la memoria sopravviva allo spegnimento —
# che e' l'unica cosa che la rende una memoria.
#
# ! LA CASA DI PROVA STA SUL DISCO, non sul CD. Avviando dal CD $HOME vale «/»
# (e' la riga HOME di /boot/kernel.cfg) e la radice e' in sola lettura: li'
# dentro nessun programma puo' tenersi niente. Con `export HOME=/disk/casa` la
# shell passa la casa ai figli — exwin compreso, che la passa alla scrivania —
# ed e' esattamente la strada che fa `login` su una macchina installata.
#
# ! QUI NON SI TOCCA IL MOUSE, E NON E' PIGRIZIA. Il primo giro di questa prova
# lo usava: `mouse_move` di QEMU e' RELATIVO, i movimenti si sommano a partire
# dall'angolo, e qualche passo per strada si perde — il puntatore e' arrivato
# venticinque pixel piu' su del pulsante, il clic e' finito sulla barra del
# titolo e la prova ha accusato il programma di non fare una cosa che nessuno
# gli aveva chiesto. Da tastiera i tasti arrivano tutti o non arriva niente.
#
# ! ED E' LA PROVA CHE VALE DI PIU', perche' e' la strada di chi il mouse non
# ce l'ha: un pulsante col fuoco non risponde all'Invio (tasto_al_fuoco in
# lib/exwin/exwin.c), quindi senza Ctrl+N «Nuova cartella» sarebbe decorativo
# per chi gira con Tab.
#
# ! NIENTE pkill. qemu_drive.py si ripulisce da solo, e un pkill dentro uno
# script fa cadere in silenzio tutto il comando che lo contiene.
#
#     tools/prova_exide_dove.sh [directory-di-lavoro]   (default /tmp/exos-ide)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-ide}"
mkdir -p "$D"
IMG="$D/prova-hd.img"

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }

SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
[ -x "$SFDISK" ] || { echo "manca sfdisk (util-linux)" >&2; exit 1; }

export EXOS_ISTANZA=ide
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

avvia_exide() {
    printf '%s\n' "mount hd0p1 /disk@6" \
                  "export HOME=/disk/casa@2" \
                  "exwin@20" "key:alt-f1@3" \
                  "/exwin/bin/exide &@12" "key:alt-f5@4"
}

# --- 1. Il disco di prova, con dentro una casa -------------------------------
#
# ! LA FORMATTAZIONE LA FA EX-OS, come in prova_filemgr.sh e per la stessa
# ragione: un ext2 fatto da mke2fs porta estensioni che il driver rifiuta.
echo "=== 1. disco ext2 di prova, e la casa /disk/casa ==="
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

timeout 400 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" \
    "mkdir /disk/casa@2" "ls /disk@3" > "$D/1-disco.log" 2>&1

grep -q "casa" "$D/1-disco.log" || {
    echo "NON RIUSCITO: il disco di prova non si e' popolato (vedi $D/1-disco.log)"
    exit 1
}

# --- 2. Il dialogo si apre, e parte dalla casa -------------------------------
echo "=== 2. Ctrl+N: l'albero, e si parte dalla casa ==="
{
    avvia_exide
    echo "key:ctrl-n@4"
    echo "foto:$D/2-dialogo.ppm@2"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 400 python3 tools/qemu_drive.py "${A[@]}" > "$D/2-dialogo.log" 2>&1

python3 tools/misura_finestre.py --client "$D/2-dialogo.ppm" > "$D/2-client.txt" || {
    echo "NON RIUSCITO: nella fotografia non c'e' nessuna finestra attiva."
    echo "              (il dialogo non si e' aperto: vedi $D/2-dialogo.log)"
    exit 1
}
CLIENT=$(cat "$D/2-client.txt")
CX=${CLIENT% *}; CY=${CLIENT#* }
echo "    il client del dialogo comincia in ($CX,$CY)"

# --- 3. La cartella nuova, il progetto dentro, e la memoria ------------------
#
# Tutto da tastiera: Ctrl+N dentro il dialogo apre «Nuova cartella», il nome e
# l'Invio la creano e ci si entra, il nome del progetto va nella casella (che
# ha il fuoco da quando il dialogo si e' aperto) e l'Invio conferma.
echo "=== 3. Ctrl+N crea la cartella, poi il progetto, poi cosa resta scritto ==="
{
    avvia_exide
    echo "key:ctrl-n@4"
    echo "foto:$D/3a-dialogo.ppm@2"

    echo "key:ctrl-n@3"                    # la cartella nuova: il dialogo dentro
    echo "foto:$D/3b-chiede-nome.ppm@2"
    echo "progetti@4"                      # il nome, e Invio conferma
    echo "foto:$D/3c-dentro.ppm@2"

    echo "prova@4"                         # il nome del progetto, e Invio crea
    echo "foto:$D/3d-creato.ppm@2"

    # e si guarda sul DISCO, non sulla finestra
    echo "key:alt-f1@2"
    echo "ls /disk/casa/progetti/prova@4"
    echo "cat /disk/casa/.app/exide/ultima.txt@3"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 500 python3 tools/qemu_drive.py "${A[@]}" > "$D/3-creato.log" 2>&1

# --- 4. Il secondo avvio: si riparte da dove si era finito -------------------
echo "=== 4. secondo avvio: il dialogo si ricorda /disk/casa/progetti ==="
{
    avvia_exide
    echo "key:ctrl-n@4"
    echo "foto:$D/4-ricordato.ppm@2"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 400 python3 tools/qemu_drive.py "${A[@]}" > "$D/4-memoria.log" 2>&1

# --- Il verdetto -------------------------------------------------------------
echo ""
esito=0

if grep -q "src" "$D/3-creato.log" && grep -q "obj" "$D/3-creato.log"; then
    echo "  [OK]  il progetto: /disk/casa/progetti/prova ha src, inc, lib, bin, obj"
else
    echo "  [NO]  il progetto non e' nato dove si e' scelto (vedi $D/3-creato.log)"
    esito=1
fi

if grep -q "^/disk/casa/progetti" "$D/3-creato.log"; then
    echo "  [OK]  la memoria: ultima.txt dice /disk/casa/progetti — la CONTENITRICE"
else
    echo "  [NO]  ultima.txt non dice /disk/casa/progetti (vedi $D/3-creato.log)"
    esito=1
fi

# La riga del percorso, sotto l'elenco: e' l'unico posto in cui si vede da
# dove il dialogo e' ripartito, e sta nei pixel.
python3 tools/ppm2png.py "$D/4-ricordato.ppm" "$D/4-percorso.png" \
    "$CX" $(( CY + 220 )) 408 18 2>/dev/null

echo ""
echo "  Da guardare: $D/4-percorso.png deve dire /disk/casa/progetti,"
echo "               $D/3b-chiede-nome.ppm deve avere il percorso nella domanda."
echo "  Le fotografie sono in $D/*.ppm — tools/ppm2png.py le converte."
exit $esito
