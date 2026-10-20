#!/bin/bash
# =============================================================================
# tools/prova_tasto_destro.sh — il tasto destro, il trascinamento e le
# associazioni (29 settembre 2026: @DESK-MENU, @FM-MENU, @FM-TRASCINA,
# @ASSOCIAZIONI)
#
#   1. la shell: un file che non e' un programma si apre col suo programma;
#      /disk/x.png lo dice (le immagini sono di ExWin), /disk/nota.txt apre
#      gfedit;
#   2. la scrivania: tasto destro sul vuoto -> Nuova cartella; tasto destro
#      su nota.txt -> Copia, sul vuoto -> Incolla («copia di nota.txt»);
#      su nota.txt -> Rinomina, poi -> Cancella;
#   3. il file manager: tasto destro -> Nuovo file; beta.txt trascinato
#      sulla cartella «dest» dell'albero -> Copia; alfa.txt -> Sposta.
#
# Ogni verdetto lo da' `ls`, non la finestra.
#
# ! IL MENU SI GUIDA DA TASTIERA: la prima voce e' gia' scelta, i separatori
# non sono righe, e Invio sceglie. Il mouse di QEMU e' relativo e si pilota a
# passi di dieci (vedi prova_desktop.sh): usarlo anche nel menu moltiplica
# le occasioni di sbagliare riga.
#
# ! IL TASTO DESTRO E' `mouse_button 2` nel monitor di QEMU (1 sinistro,
# 2 destro, 4 centrale).
#
#     tools/prova_tasto_destro.sh [directory-di-lavoro]   (default /tmp/exos-destro)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos) in grafica 800x600, sfdisk.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-destro}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)

export EXOS_ISTANZA=destro EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

passi_a() {
    local dx=$1 dy=$2 i d rx ry
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do echo "mon:mouse_move 10 10@0"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do echo "mon:mouse_move 10 0@0"; done
    for ((i = 10; i <= ry; i += 10)); do echo "mon:mouse_move 0 10@0"; done
    echo "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@0"
}
casa() { for i in 1 2 3 4 5; do echo "mon:mouse_move -600 -600@0"; done; }
va() { casa; passi_a "$1" "$2"; }                   # x y assoluti
destro() { va "$1" "$2"; echo "mon:mouse_button 2;mouse_button 0@3"; }
giu() { local i; for ((i = 0; i < $1; i++)); do echo "key:down@0.3"; done; }
cancella_riga() { local i; for i in $(seq 1 30); do echo "key:backspace@0"; done; }

corri() {   # nome, poi i passi da un file .args
    mapfile -t A < "$D/$1.args"
    timeout 600 python3 tools/qemu_drive.py "${A[@]}" > "$D/$1.log" 2>&1
}

# --- il disco: formattato da EX-OS, come in prova_filemgr.sh -----------------
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

# ! nota.txt E' LA PRIMA VOCE DELLA SCRIVANIA, in alto a sinistra: ext2 rende
# i nomi nell'ordine in cui sono nati, e la prova clicca per posizione.
{
    echo "mkfs -t ext2 -L prova hd0p1@4"; echo "si@40"; echo "mount hd0p1 /disk@6"
    for c in "mkdir /disk/casa" "mkdir /disk/casa/desktop" \
             "cp /boot/help.txt /disk/casa/desktop/nota.txt" \
             "mkdir /disk/prova" "mkdir /disk/dest" \
             "cp /boot/help.txt /disk/prova/alfa.txt" \
             "cp /boot/help.txt /disk/prova/beta.txt" \
             "cp /boot/help.txt /disk/x.png" "cp /boot/help.txt /disk/nota.txt"; do
        echo "$c@3"
    done
    echo "/disk/x.png@3"
    echo "/disk/nota.txt@5"; echo "foto:$D/1-gfedit.ppm@1"
} > "$D/1.args"
corri 1

# --- 2. la scrivania -----------------------------------------------------------
{
    echo "mount hd0p1 /disk@6"; echo "export HOME=/disk/casa@1"
    echo "exwin@14"; echo "key:alt-f5@3"
    destro 400 300; echo "foto:$D/2-menu.ppm@1"
    echo "key:ret@3"; echo "cartella1@4"                 # Nuova cartella
    destro 46 40; giu 2; echo "key:ret@2"                # nota.txt: Copia
    destro 400 300; giu 2; echo "key:ret@5"              # Incolla
    echo "foto:$D/2-incollato.ppm@1"
    destro 46 40; giu 1; echo "key:ret@3"                # Rinomina
    cancella_riga; echo "nota2.txt@4"
    echo "key:alt-f1@2"; echo "ls /disk/casa/desktop@4"; echo "key:alt-f5@3"
    # ! DOPO LA RINOMINA IN ALTO C'E' cartella1: rename() mette il nome nuovo
    # in fondo alla directory, e nota2.txt scende al terzo posto. Si cancella
    # quindi la cartella, che e' anche il caso che chiede di piu'.
    destro 46 40; giu 4; echo "key:ret@3"                # Cancella
    echo "foto:$D/2-conferma.ppm@1"
    # ! IL FUOCO E' SUL PULSANTE CHE NON PERDE NIENTE (ex_dlg_conferma)
    echo "key:tab@1"; echo "key:ret@4"
    echo "key:alt-f1@2"; echo "echo DOPO@1"; echo "ls /disk/casa/desktop@4"
} > "$D/2.args"
corri 2

# --- 3. il file manager ------------------------------------------------------
# Dalla fotografia di /disk/prova: alfa.txt a (270,74), beta.txt a (270,90),
# la cartella «dest» nell'albero a (75,155).
#
# ! IL PUNTATORE ARRIVA DIECI PIXEL PRIMA, in x e in y (visto nella foto del
# primo giro: mirato a 75,155, stava a 63,145, sopra «prova»). Sulle icone
# della scrivania, 76 pixel, non si nota; su righe di 16 si': i bersagli qui
# sotto hanno gia' i dieci in piu'.
trascina() {    # x0 y0 x1 y1
    va "$1" "$2"; echo "mon:mouse_button 1@1"
    casa; passi_a "$3" "$4"; echo "mon:mouse_button 0@3"
}
{
    echo "mount hd0p1 /disk@6"; echo "exwin@14"; echo "key:alt-f1@3"
    echo "/exwin/bin/filemgr /disk/prova &@8"; echo "key:alt-f5@3"
    destro 400 300; giu 2; echo "key:ret@3"              # Nuovo file
    echo "nuovo.txt@4"
    trascina 280 100 85 165; echo "foto:$D/3-domanda.ppm@1"
    # ! IL FUOCO E' SU ANNULLA, l'ultimo: Shift+Tab torna indietro
    echo "key:shift-tab@1"; echo "key:shift-tab@1"; echo "key:ret@5"  # Copia
    trascina 280 84 85 165; echo "key:shift-tab@1"; echo "key:ret@5" # Sposta
    echo "foto:$D/3-fatto.ppm@1"
    echo "key:alt-f1@2"; echo "echo DEST@1"; echo "ls /disk/dest@4"
    echo "echo PROVA@1"; echo "ls /disk/prova@4"
} > "$D/3.args"
corri 3

# --- il verdetto -------------------------------------------------------------
esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

echo "=== 1. la shell ==="
if grep -aq "x.png: si apre con /exwin/bin/immagini" "$D/1.log"; then
    ok "x.png: la shell dice che si apre con immagini, dentro ExWin"
else no "x.png: nessun rimando a immagini (vedi $D/1.log)"; fi
# gfedit scrive il nome del file nella sua riga in alto
if python3 tools/ppm2png.py "$D/1-gfedit.ppm" "$D/1-gfedit.png" > /dev/null 2>&1 &&
   ! grep -aq "comando non trovato\|not found" "$D/1.log"; then
    ok "nota.txt: la shell lo apre con gfedit (vedi $D/1-gfedit.png)"
else no "nota.txt: non si e' aperto (vedi $D/1.log)"; fi

echo "=== 2. la scrivania ==="
PRIMA=$(sed -n '/ls \/disk\/casa\/desktop/,/DOPO/p' "$D/2.log")
DOPO=$(sed -n '/^DOPO/,$p' "$D/2.log")
echo "$PRIMA" | grep -q "<DIR>.*cartella1" && ok "Nuova cartella: cartella1" \
    || no "Nuova cartella: cartella1 non c'e'"
echo "$PRIMA" | grep -q "copia di nota.txt" && ok "Copia e Incolla: copia di nota.txt" \
    || no "Copia e Incolla: nessuna copia"
echo "$PRIMA" | grep -q " nota2.txt" && ok "Rinomina: nota.txt ora e' nota2.txt" \
    || no "Rinomina: nota2.txt non c'e'"
! echo "$DOPO" | grep -q "cartella1" && echo "$DOPO" | grep -q " nota2.txt" \
    && ok "Cancella: cartella1 non c'e' piu', il resto si'" \
    || no "Cancella: vedi $D/2.log e $D/2-conferma.ppm"

echo "=== 3. il file manager ==="
DEST=$(sed -n '/^DEST/,/^PROVA/p' "$D/3.log")
PROVA=$(sed -n '/^PROVA/,$p' "$D/3.log")
echo "$PROVA" | grep -q "nuovo.txt" && ok "Nuovo file: nuovo.txt" \
    || no "Nuovo file: nuovo.txt non c'e'"
echo "$DEST" | grep -q "beta.txt" && echo "$PROVA" | grep -q "beta.txt" \
    && ok "trascinato e Copia: beta.txt e' in dest e anche in prova" \
    || no "Copia trascinando: vedi $D/3.log e $D/3-domanda.ppm"
echo "$DEST" | grep -q "alfa.txt" && ! echo "$PROVA" | grep -q "alfa.txt" \
    && ok "trascinato e Sposta: alfa.txt e' in dest e non piu' in prova" \
    || no "Sposta trascinando: vedi $D/3.log e $D/3-fatto.ppm"

echo ""
echo "  Le fotografie e i registri sono in $D"
echo "  esito: $esito (0 = tutto a posto)"
exit $esito
