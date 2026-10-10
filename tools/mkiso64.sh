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
# Stage 2 del CD: quello assemblato apposta da `make iso64` (senza volume in
# RAM), non quello di build/, che dipende da cio' che si e' costruito per ultimo.
STAGE2=build-64/avvio/stage2-cd.bin
[ -f "$STAGE2" ] || STAGE2=build/stage2.bin
for f in build/stage1.bin "$STAGE2" "$KERNEL"; do
    [ -f "$f" ] || { echo "mkiso64: manca $f" >&2; exit 1; }
done

# --- l'immagine di avvio (interna) ---------------------------------------------
mkdir -p build-64/avvio dist
AVVIO=build-64/avvio/avvio-$QUALE.img
dd if=/dev/zero of="$AVVIO" bs=512 count=2880 status=none
mformat -f 1440 -v "EXOS64  " -i "$AVVIO" ::
dd if=build/stage1.bin of="$AVVIO" bs=512 count=1 conv=notrunc status=none
mcopy -i "$AVVIO" "$STAGE2" ::/LOADER.BIN
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
    cp "$STAGE2" "$RADICE/LOADER.BIN"
    # ! I DATI SONO GLI STESSI DEL CD A 32 BIT, e si prendono da li'
    # (build/iso-exos, fatto da `make iso-exos`): kernel.cfg, gli script di
    # avvio, i manuali, i caratteri, le icone, i certificati. Si copia tutto
    # cio' che NON e' un programma - un programma si riconosce dai primi
    # quattro byte, 0x7F 'E' 'L' 'F' - cosi' non c'e' un secondo elenco da
    # tenere uguale: un file di dati nuovo sul CD a 32 bit arriva qui da solo.
    # I programmi a 32 bit restano fuori: qui girerebbero solo per dire che
    # non sono di questa macchina.
    if [ -d build/iso-exos ]; then
        (cd build/iso-exos && find . -type f ! -name KERNEL.BIN ! -name LOADER.BIN) | while read -r f; do
            if [ "$(head -c 4 "build/iso-exos/$f" | od -An -tx1 | tr -d ' \n')" != "7f454c46" ]; then
                mkdir -p "$RADICE/$(dirname "$f")"
                cp "build/iso-exos/$f" "$RADICE/$f"
            fi
        done
    else
        echo "mkiso64: manca build/iso-exos (make iso-exos): il CD avra' solo i programmi" >&2
    fi
    mkdir -p "$RADICE/bin" "$RADICE/dev" "$RADICE/drivers"
    # i comandi a 64 bit (tools/costruisci-utente64.sh), shell compresa
    if [ -d build-64/bin ] && [ -f build-64/bin/sh ]; then
        cp build-64/bin/* "$RADICE/bin/"
    fi
    # la scrivania: i programmi di ExWin e il server grafico (che si costruisce
    # come un driver ma sta con loro, come sul CD a 32 bit)
    if [ -d build-64/exwin/bin ]; then
        mkdir -p "$RADICE/exwin/bin"
        cp build-64/exwin/bin/* "$RADICE/exwin/bin/" 2>/dev/null || true
        [ -f build-64/drivers/wserver.drv ] && cp build-64/drivers/wserver.drv "$RADICE/exwin/bin/wserver"
    fi
    # i driver: in /dev (da dove kernel.cfg e i comandi li caricano) e in
    # /drivers (da dove `install` li copia), come sul CD a 32 bit
    if [ -d build-64/drivers ]; then
        cp build-64/drivers/*.drv "$RADICE/dev/" 2>/dev/null || true
        cp build-64/drivers/*.drv "$RADICE/drivers/" 2>/dev/null || true
    fi
    # il primo programma a 64 bit resta, col suo nome: e' una prova che non
    # dipende dalla libc. Se la shell non c'e' ancora, fa lui da /bin/sh.
    if [ -f build-64/prove/primo ]; then
        cp build-64/prove/primo "$RADICE/bin/primo64"
        [ -f "$RADICE/bin/sh" ] || cp build-64/prove/primo "$RADICE/bin/sh"
    fi
    # (Fino al 10 ottobre 2026 qui si lasciava build-64/NON-ANCORA-UN-SISTEMA.txt,
    # che fermava exagonx/repo-update.sh -64 prima di pubblicare. Tolto quando
    # il sistema a 64 bit si e' avviato sul PC vero e l'utente ha chiesto il
    # repository: senza, netupdate non ha da dove prendere il resto.)
    rm -f build-64/NON-ANCORA-UN-SISTEMA.txt
fi

python3 tools/mkiso.py "$ISO" --da "$RADICE" --avvio "$AVVIO" --etichetta "$ETICHETTA" > /dev/null
echo "mkiso64: $ISO ($(stat -c%s "$ISO") byte)"
