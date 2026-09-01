#!/bin/sh
# =============================================================================
# tools/prove/audio_prova.sh — il banco di prova del suono, da fuori
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
#   sh tools/prove/audio_prova.sh [sb16|es1370|ac97|hda]
#
# Avvia EX-OS dal CD con una scheda audio emulata, gli fa caricare il driver,
# gli fa eseguire il collaudo e gli fa suonare i due file di /suono. Poi
# GUARDA COSA E' USCITO: l'audiodev `wav` di QEMU scrive su file gli stessi
# campioni che sarebbero andati all'altoparlante, e tools/prove/audio_wav.py
# ci cerca dentro il tono.
#
# ! LA DIFFERENZA FRA QUESTA PROVA E `audio -c` E' TUTTO IL PUNTO. `audio -c`
# chiede all'hardware se si e' mosso: interrupt arrivati, contatore DMA che
# avanza. Sono domande giuste e insufficienti — il DMA avanza allo stesso modo
# se i campioni sono sbagliati, se le due meta' del buffer sono scambiate, se
# il formato dichiarato non e' quello scritto. Quelle cose si SENTONO, e
# l'unico modo di vederle da uno script e' guardare l'uscita.
# =============================================================================
set -e

SCHEDA="${1:-sb16}"
[ "$SCHEDA" = "tutte" ] && {
    for x in sb16 es1370 ac97 hda; do sh "$0" "$x" || exit 1; done
    exit 0
}
FUORI="${EXOS_PROVA_DIR:-/tmp/exos-audio}"
mkdir -p "$FUORI"
WAV="$FUORI/uscita-$SCHEDA.wav"
rm -f "$WAV"

case "$SCHEDA" in
    sb16)   DEV="-device sb16,audiodev=snd0 -device adlib,audiodev=snd0" ;;
    es1370) DEV="-device ES1370,audiodev=snd0" ;;
    ac97)   DEV="-device AC97,audiodev=snd0" ;;
    hda)    DEV="-device intel-hda -device hda-output,audiodev=snd0" ;;
    *)      echo "schede: sb16, es1370, ac97, hda, tutte"; exit 2 ;;
esac

echo "=== EX-OS con una $SCHEDA, uscita in $WAV ==="

# ! LA RETE SI SPEGNE PER LE SCHEDE PCI, e non e' per comodita': su QEMU la
# scheda di rete predefinita e la scheda audio finiscono sulla stessa linea
# IRQ, e questa prova deve misurare il DRIVER AUDIO. Che le due linee
# condivise convivano e' una prova a se', e si fa cosi':
#
#     python3 tools/qemu_drive.py "audio -i@50" "ping 10.0.2.2@20"
#
# — con la rete ACCESA, guardando che il ping risponda dopo il collaudo.
case "$SCHEDA" in sb16) RETE="" ;; *) RETE="-nic none" ;; esac

EXOS_CDROM=dist/exos.iso \
EXOS_NO_FLOPPY=1 \
EXOS_QEMU_EXTRA="$RETE -audiodev wav,id=snd0,path=$WAV $DEV" \
python3 tools/qemu_drive.py \
    "/dev/pci.drv &@6" \
    "audio -i@50" \
    "audio /suono/prova.wav@10" \
    "audio -m /suono/prova.mid@10" \
    > "$FUORI/seriale.txt" 2>&1 || true

echo
echo "=== cosa ha detto la macchina ==="
sed -n '/=== seriale dal prompt in poi ===/,$p' "$FUORI/seriale.txt" \
    | grep -v '^\[WARN\]' | grep -v '^$'

echo
echo "=== cosa e' uscito dall'altoparlante ==="
python3 tools/prove/audio_wav.py "$WAV"
