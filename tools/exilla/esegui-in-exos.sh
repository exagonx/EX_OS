#!/bin/bash
# =============================================================================
# tools/exilla/esegui-in-exos.sh — un programma EX-OS, eseguito in QEMU
#
#     tools/exilla/esegui-in-exos.sh <binario> [argomenti...]
#
# Mette il binario su un disco ext2 di prova, avvia EX-OS dal CD e lo esegue
# come /disk/<nome>. L'uscita finisce su stdout (letta dalla seriale). Il CD e'
# quello della copia privata (tools/exilla/costruisci-privato.sh) se c'e', se
# no quello condiviso di dist/: in tutt'e due i casi SI LEGGE SOLTANTO.
#
# CD_FORZATO=<iso> sceglie il CD (per confrontare con quello di prima).
# ALTRI="f1 f2" mette altri file accanto al programma; EXOS_RAM la memoria;
# DISCO_MB la misura del disco di prova (32 se non detta).
# AMBIENTE="VAR=valore ..." esporta quelle variabili prima di lanciarlo.
#
# Le cose temporanee stanno in cross_build/<macchina>/costruzione-prova (fuori
# da MEGA). Con /dev/kvm scrivibile la macchina va con -enable-kvm.
# =============================================================================
cd "$(dirname "$0")/../.." || exit 1
BIN="$1"; shift
[ -f "$BIN" ] || { echo "uso: $0 <binario> [argomenti]" >&2; exit 2; }
MACCHINA="${MACCHINA:-$(hostname | tr 'A-Z' 'a-z')}"
D="$PWD/cross_build/$MACCHINA/costruzione-prova"
CD="${CD_FORZATO:-$PWD/cross_build/$MACCHINA/costruzione-sistema/albero/dist/exos.iso}"
[ -f "$CD" ] || CD="$PWD/dist/exos.iso"
IMG="$D/esegui-hd.img"
NOME=$(basename "$BIN")
mkdir -p "$D"

rm -f "$IMG"
qemu-img create -f raw "$IMG" "${DISCO_MB:-32}M" > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, type=83, bootable\n' \
    | "$(command -v sfdisk || echo /sbin/sfdisk)" "$IMG" > /dev/null 2>&1
KVM=""; [ -w /dev/kvm ] && KVM="-enable-kvm"
export EXOS_ISTANZA=exilla EXOS_NO_FLOPPY=1 EXOS_CDROM="$CD"
export EXOS_QEMU_EXTRA="$KVM -drive file=$IMG,format=raw,if=ide"
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@30" > "$D/esegui-0.log" 2>&1
"$(command -v debugfs || echo /sbin/debugfs)" -w -R "write $BIN $NOME" \
    "$IMG?offset=1048576" > /dev/null 2>&1
# ALTRI="file1 file2": altri file da mettere accanto al programma, in /disk.
for f in $ALTRI; do
    "$(command -v debugfs || echo /sbin/debugfs)" -w -R "write $f $(basename "$f")" \
        "$IMG?offset=1048576" > /dev/null 2>&1
done

SER=/tmp/exos/serialexilla.txt
rm -f "$SER"
ESPORTA=()
for v in $AMBIENTE; do ESPORTA+=("export $v@1"); done
timeout "${ATTESA:-300}" python3 tools/qemu_drive.py "mount hd0p1 /disk@5" "${ESPORTA[@]}" \
    "/disk/$NOME $*@${SECONDI:-8}" "echo FINE-ESEGUI@2" > "$D/esegui-1.log" 2>&1
tr -d '\r' < "$SER" 2>/dev/null | sed 's/\x1b\[[0-9;]*m//g' | \
    sed -n "\#/disk/$NOME#,/FINE-ESEGUI/p" | sed '1d;$d' | grep -v "^ex-os:"
