#!/bin/sh
# =============================================================================
# tools/mkiso64.sh — il CD avviabile di EX-OS a 64 bit (@EXOS-64)
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/mkiso64.sh            dist/exos64.iso        il kernel INTERO
#     tools/mkiso64.sh prove      dist/exos64-prove.iso  il kernel di prova
#
# ! LA VERSIONE A 64 BIT NON HA DISCHETTI (l'utente, 10 ottobre 2026: «crea
# solo iso e usbkey, sui nuovi pc il floppy non esiste piu'»). I suoi supporti
# sono il CD e la chiavetta. Questo fa il CD; la chiavetta arriva quando a
# 64 bit ci sara' `install` (vedi @EXOS-64 in in_lavorazione.txt).
#
# ! DENTRO IL CD C'E' ANCORA UN'IMMAGINE DI 1,44 MB, E NON E' UN DISCHETTO: e'
# l'immagine di avvio El Torito, la stessa strada del CD a 32 bit. Il BIOS la
# presenta al caricatore come unita' A:, Stage 1 e Stage 2 (gli stessi del
# 32 bit, byte per byte) ci leggono LOADER.BIN e KERNEL.BIN, e li' finisce:
# il kernel non trova nessun lettore di dischetti e monta il CD come radice.
# Nessuno la vede e nessuno la scrive su un supporto: sta in build-64/avvio/.
# =============================================================================
set -e
cd "$(dirname "$0")/.."

QUALE="${1:-intero}"
case "$QUALE" in
    intero) KERNEL=build-64/kernel-intero.bin; ISO=dist/exos64.iso;       ETICHETTA="EXOS64" ;;
    prove)  KERNEL=build-64/kernel.bin;        ISO=dist/exos64-prove.iso; ETICHETTA="EXOS64 PROVE" ;;
    *) echo "uso: $0 [intero|prove]" >&2; exit 2 ;;
esac
for f in build/stage1.bin build/stage2.bin "$KERNEL"; do
    [ -f "$f" ] || { echo "mkiso64: manca $f" >&2; exit 1; }
done

# --- l'immagine di avvio (interna) ---------------------------------------------
mkdir -p build-64/avvio dist
AVVIO=build-64/avvio/avvio-$QUALE.img
dd if=/dev/zero of="$AVVIO" bs=512 count=2880 status=none
mformat -f 1440 -v "EXOS64  " -i "$AVVIO" ::
dd if=build/stage1.bin of="$AVVIO" bs=512 count=1 conv=notrunc status=none
mcopy -i "$AVVIO" build/stage2.bin ::/LOADER.BIN
mcopy -i "$AVVIO" "$KERNEL" ::/KERNEL.BIN

# --- il contenuto del CD: la radice che il kernel monta -------------------------
# ! PER IL SISTEMA L'ALBERO E' build-64/iso-exos, lo stesso nome di quello a
# 32 bit (build/iso-exos): e' da li' che tools/mknetinst.sh prende cio' che
# pubblica, e cosi' `make netinst64` non ha un elenco suo da tenere uguale.
if [ "$QUALE" = "intero" ]; then RADICE=build-64/iso-exos; else RADICE=build-64/iso-prove; fi
rm -rf "$RADICE"
mkdir -p "$RADICE/doc"
printf 'EX-OS a 64 bit - %s\r\nNon e'"'"' ancora un sistema: vedi @EXOS-64 in in_lavorazione.txt\r\n' \
    "$ETICHETTA" > "$RADICE/doc/leggimi.txt"
if [ "$QUALE" = "intero" ]; then
    # come sul CD a 32 bit: il kernel e il caricatore anche come file, perche'
    # `install` e `netupdate` li copiano da qui, non dall'immagine di avvio
    cp "$KERNEL" "$RADICE/KERNEL.BIN"
    cp build/stage2.bin "$RADICE/LOADER.BIN"
    if [ -f build-64/prove/primo ]; then
        # il kernel avvia /bin/sh sulle console: per ora e' il programma di
        # prova, l'unico a 64 bit che c'e'
        mkdir -p "$RADICE/bin"
        cp build-64/prove/primo "$RADICE/bin/sh"
    fi
    # ! FINCHE' /bin/sh E' IL PROGRAMMA DI PROVA QUESTO NON E' UN SISTEMA, e lo
    # si scrive in un file che exagonx/repo-update.sh guarda prima di
    # pubblicare: un repository a 64 bit con dentro un kernel e nient'altro
    # non deve finire sul server per distrazione. Si toglie questa riga il
    # giorno che la shell vera prende il posto di primo.c.
    echo "il sistema a 64 bit non e' ancora installabile: /bin/sh e' tools/prove64/primo.c" \
        > build-64/NON-ANCORA-UN-SISTEMA.txt
fi

python3 tools/mkiso.py "$ISO" --da "$RADICE" --avvio "$AVVIO" --etichetta "$ETICHETTA" > /dev/null
echo "mkiso64: $ISO ($(stat -c%s "$ISO") byte)"
