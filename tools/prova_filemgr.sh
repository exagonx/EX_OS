#!/bin/bash
# =============================================================================
# tools/prova_filemgr.sh — i segni, la copia e la cancellazione del file manager
#
# Prova quel che e' stato chiesto il 21 settembre 2026 (@FILEMGR-OPS):
#
#   1. la BARRA SPAZIATRICE segna una riga, la riga mostra un «*» nella
#      colonna 0 e la riga di stato dice quante ne sono segnate;
#   2. Ctrl+C copia le righe segnate in una directory che esiste, e i file
#      arrivano davvero — lo dice `ls`, non la finestra;
#   3. il tasto Canc chiede prima, dicendo quanti file e quanti byte, e
#      cancella solo se si risponde di si'.
#
# ! LA PROVA VUOLE UN FILESYSTEM SCRIVIBILE, E NON PUO' ESSERE IL CD. Il CD e'
# in sola lettura per costruzione e il FAT12 del dischetto, montato sotto un
# punto che non e' la radice, rifiuta di creare directory annidate (lo dice
# lui: «supportate solo directory nella root»). Quindi la prova si fa su un
# ext2: un disco di 32 MB formattato DENTRO EX-OS, con mkfs.
#
# ! IL PUNTO DI MONTAGGIO NON DEVE ESISTERE. `mount hd0p1 /USB` rende -17
# (EEXIST) perche' /USB sul CD c'e' gia': si monta su /disk, che non c'e'.
#
# ! LA GRAFICA STA SULLA CONSOLE 5, non sulla 2 — lo dice exwin all'avvio
# («grafica accesa sulla console 5»). E dopo `exwin` la console attiva E' GIA'
# la 5: un comando battuto subito dopo finisce nella grafica invece che nella
# shell. Percio' si torna alla 1 con Alt+F1, si lancia il file manager, e solo
# allora si passa alla 5.
#
# ! NEL DIALOGO DI SALVATAGGIO SI BATTE UN PERCORSO ASSOLUTO. La casella
# «Salva come» tiene il NOME, e il dialogo lo attacca alla directory che mostra;
# da oggi un nome che comincia con «/» e' un percorso e la directory non conta
# (lib/exdlg/exdlg.c, componi()). Questa prova batte «/disk/dest», quindi prova
# anche quello.
#
# ! NIENTE pkill. qemu_drive.py si ripulisce da solo, e un pkill dentro uno
# script fa cadere in silenzio tutto il comando che lo contiene.
#
#     tools/prova_filemgr.sh [directory-di-lavoro]     (default /tmp/exos-fm)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-fm}"
mkdir -p "$D"
IMG="$D/prova-hd.img"

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }

SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
[ -x "$SFDISK" ] || { echo "manca sfdisk (util-linux)" >&2; exit 1; }

export EXOS_ISTANZA=fm
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

# --- 1. Il disco di prova, formattato da EX-OS -------------------------------
#
# ! LA FORMATTAZIONE LA FA EX-OS, come in mkhd.sh e per la stessa ragione: un
# ext2 fatto da mke2fs porta estensioni che il driver di EX-OS rifiuta, e ogni
# versione di e2fsprogs ne accende di nuove.
echo "=== 1. disco ext2 di prova ($IMG) ==="
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

timeout 400 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" \
    "mkdir /disk/prova@2" "mkdir /disk/prova/sub@2" "mkdir /disk/dest@2" \
    "cp /boot/help.txt /disk/prova/alfa.txt@3" \
    "cp /boot/help.txt /disk/prova/beta.txt@3" \
    "cp /boot/help.txt /disk/prova/gamma.txt@3" \
    "cp /boot/help.txt /disk/prova/sub/delta.txt@3" \
    "ls /disk/prova@3" > "$D/1-disco.log" 2>&1

grep -q "gamma.txt" "$D/1-disco.log" || {
    echo "NON RIUSCITO: il disco di prova non si e' popolato (vedi $D/1-disco.log)"
    exit 1
}
echo "    /disk/prova: alfa.txt beta.txt gamma.txt sub/delta.txt, e /disk/dest vuota"

# --- 2. I segni e la copia ---------------------------------------------------
#
# Tab porta il fuoco dall'albero all'elenco; una freccia giu' salta la riga
# «sub» (le cartelle restano in cima) e si segnano alfa.txt e beta.txt: la
# barra segna E scende, quindi due barre di fila segnano due righe.
echo "=== 2. due righe segnate, e Ctrl+C le copia in /disk/dest ==="
ARGS=("mount hd0p1 /disk@6" "exwin@18" "key:alt-f1@3"
      "/exwin/bin/filemgr /disk/prova &@8" "key:alt-f5@3"
      "key:tab@1" "key:down@1" "key:spc@1" "key:spc@1"
      "foto:$D/2-segnate.ppm@2"
      "key:ctrl-c@3")
# La casella propone il nome della directory di partenza: si svuota a
# backspace, come nella barra del navigatore.
for i in $(seq 1 40); do ARGS+=("key:backspace@0"); done
ARGS+=("/disk/dest@6" "foto:$D/2-copiati.ppm@2"
       "key:alt-f1@2" "ls /disk/dest@5")

timeout 400 python3 tools/qemu_drive.py "${ARGS[@]}" > "$D/2-copia.log" 2>&1

# --- 3. La cancellazione, che chiede prima -----------------------------------
echo "=== 3. Canc chiede, e cancella solo se si risponde di si' ==="
ARGS=("mount hd0p1 /disk@6" "exwin@18" "key:alt-f1@3"
      "/exwin/bin/filemgr /disk/dest &@8" "key:alt-f5@3"
      "key:tab@1" "key:spc@1" "key:spc@1"
      "key:delete@3" "foto:$D/3-domanda.ppm@2"
      # ! IL FUOCO E' SUL PULSANTE CHE NON PERDE NIENTE (vedi ex_dlg_conferma):
      # per cancellare davvero ci si sposta sull'altro.
      "key:tab@1" "key:ret@4" "foto:$D/3-fatto.ppm@2"
      "key:alt-f1@2" "ls /disk/dest@5")

timeout 400 python3 tools/qemu_drive.py "${ARGS[@]}" > "$D/3-cancella.log" 2>&1

# --- Il verdetto -------------------------------------------------------------
echo ""
esito=0

if grep -q "alfa.txt" "$D/2-copia.log" && grep -q "beta.txt" "$D/2-copia.log"; then
    echo "  [OK]  la copia: alfa.txt e beta.txt sono in /disk/dest"
else
    echo "  [NO]  la copia: in /disk/dest non ci sono (vedi $D/2-copia.log)"
    esito=1
fi

# ! `ls` DI UNA DIRECTORY VUOTA NON DICE «0 file»: dice «(vuota, o solo nomi
# nascosti)». Cercare il conto che non c'e' faceva fallire questa prova mentre
# la cancellazione era riuscita — un verdetto che accusa il programma provato.
if grep -q "vuota" "$D/3-cancella.log"; then
    echo "  [OK]  la cancellazione: /disk/dest e' vuota"
else
    echo "  [NO]  la cancellazione: in /disk/dest c'e' ancora roba"
    echo "        (guarda $D/3-domanda.ppm: che pulsante ha il fuoco?)"
    esito=1
fi

echo ""
echo "Le fotografie sono in $D/*.ppm — tools/ppm2png.py le converte."
exit $esito
