#!/bin/bash
# =============================================================================
# tools/mkfloppy.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# This file is part of EX-OS, distributed under the GNU GPL v2.
# See the LICENSE file in the project root for the full license text.
# =============================================================================

set -e  # Esci immediatamente su qualsiasi errore

# --- Configurazione -----------------------------------------------------------
IMG="dist/floppy.img"           # Percorso immagine output
BOOT_SECTOR="build/stage1.bin"  # Boot sector 512 byte (Stage 1)
LOADER="build/stage2.bin"       # Stage 2 loader
KERNEL="build/kernel.bin"       # Kernel binario
KERNEL_CFG="boot/kernel.cfg"    # File di configurazione kernel

# Dimensione floppy 1.44MB in byte (512 * 2880 settori)
FLOPPY_SIZE=1474560
FLOPPY_SECTORS=2880

# Colori per output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# --- Funzioni helper ----------------------------------------------------------

log_info()  { echo -e "${BLUE}[INFO]${NC}  $1"; }
log_ok()    { echo -e "${GREEN}[OK]${NC}    $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_err()   { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

check_tool() {
    command -v "$1" >/dev/null 2>&1 || log_err "Strumento non trovato: '$1'. Installa con: sudo apt install $2"
}

check_file() {
    [ -f "$1" ] || log_err "File non trovato: '$1' — compila prima con 'make'"
}

# --- Verifica prerequisiti ----------------------------------------------------

log_info "Verifica prerequisiti..."

check_tool dd      "coreutils"
check_tool mformat "mtools"
check_tool mcopy   "mtools"
check_tool mmd     "mtools"
check_tool mattrib "mtools"

log_ok "Tutti gli strumenti trovati"

# --- Verifica file necessari --------------------------------------------------

log_info "Verifica file compilati..."

# Il boot sector è obbligatorio
check_file "$BOOT_SECTOR"

# Lo stage 2 è obbligatorio
check_file "$LOADER"

# Il kernel è obbligatorio
check_file "$KERNEL"

# Verifica dimensione boot sector: DEVE essere esattamente 512 byte
STAGE1_SIZE=$(stat -c%s "$BOOT_SECTOR")
if [ "$STAGE1_SIZE" -ne 512 ]; then
    log_err "Stage 1 ($BOOT_SECTOR) deve essere esattamente 512 byte, trovato: $STAGE1_SIZE byte"
fi

log_ok "File compilati trovati e validi"

# --- Controllo dimensioni (warning se vicino al limite) -----------------------

log_info "Controllo dimensioni file..."

KERNEL_SIZE=$(stat -c%s "$KERNEL")
LOADER_SIZE=$(stat -c%s "$LOADER")

log_info "  stage1.bin : $STAGE1_SIZE byte (512 byte esatti)"
log_info "  stage2.bin : $LOADER_SIZE byte"
log_info "  kernel.bin : $KERNEL_SIZE byte"

# Spazio disponibile su FAT12 1.44MB (circa 1.38MB usabili dopo FAT+root dir)
USABLE=1406976
USED=$((LOADER_SIZE + KERNEL_SIZE))

if [ "$USED" -gt "$USABLE" ]; then
    log_err "File troppo grandi per il floppy! Usato: $USED byte, Disponibile: $USABLE byte"
elif [ "$USED" -gt $((USABLE * 80 / 100)) ]; then
    log_warn "Floppy quasi pieno: $USED / $USABLE byte usati (>80%)"
else
    log_ok "Spazio sufficiente: $USED / $USABLE byte usati"
fi

# --- Crea immagine vuota ------------------------------------------------------

log_info "Creazione immagine floppy vuota (1.44MB)..."

mkdir -p dist

# Crea immagine di zeri
dd if=/dev/zero of="$IMG" bs=512 count=$FLOPPY_SECTORS status=none

log_ok "Immagine creata: $IMG ($FLOPPY_SECTORS settori x 512 byte)"

# --- Formattazione FAT12 ------------------------------------------------------
#
# Parametri mformat per floppy 1.44MB FAT12:
#   -f 1440     : formato 1440KB (1.44MB)
#   -t 80       : 80 tracce
#   -h 2        : 2 testine
#   -s 18       : 18 settori per traccia
#   -v "EXOS"   : volume label
#   -i          : specifica file immagine
#   ::          : device virtuale mtools (root del floppy)
#
# NOTA: NON usiamo il parametro -B (boot sector) qui perché
#       installeremo il boot sector manualmente con dd dopo.
#       mformat scrive solo le strutture FAT12 (FAT1, FAT2, root directory).

log_info "Formattazione FAT12..."

mformat -f 1440 \
        -v "EXOS    " \
        -i "$IMG" \
        ::

log_ok "FAT12 formattato (volume label: EXOS)"

# --- Installa boot sector (Stage 1) -------------------------------------------
#
# ATTENZIONE: operazione critica.
# Il boot sector FAT12 ha questa struttura:
#
#   Byte 0-2:   JMP short + NOP  (codice)
#   Byte 3-10:  OEM Name
#   Byte 11-61: BPB (BIOS Parameter Block) — scritto da mformat
#   Byte 62+:   Codice Stage 1
#   Byte 510:   0x55
#   Byte 511:   0xAA
#
# mformat ha già scritto il BPB nei byte 11-61.
# Il nostro stage1.bin contiene il codice con un BPB identico.
# Dobbiamo preservare il BPB scritto da mformat (potrebbe differire
# leggermente) oppure usare il BPB del nostro stage1.
#
# Strategia: scriviamo l'intero stage1.bin nel settore 0.
# Il nostro stage1.bin DEVE avere un BPB corretto identico a quello
# che mformat userebbe per 1.44MB FAT12.

log_info "Installazione boot sector (Stage 1)..."

dd if="$BOOT_SECTOR" of="$IMG" bs=512 count=1 conv=notrunc status=none

log_ok "Boot sector installato (settore 0)"

# --- Verifica firma boot sector -----------------------------------------------

# Leggi gli ultimi 2 byte del boot sector e verifica 0x55AA
SIG=$(dd if="$IMG" bs=2 skip=255 count=1 status=none | od -A n -t x2 | tr -d " \n")

if [ "$SIG" = "aa55" ]; then
    log_ok "Firma boot sector verificata: 0x55AA trovata"
else
    log_err "Firma boot sector MANCANTE o errata! (trovato: $SIG, atteso aa55)"
fi

# --- Crea struttura directory sul floppy --------------------------------------

log_info "Creazione struttura directory..."

mmd -i "$IMG" ::/boot  2>/dev/null || log_warn "Directory /boot già esistente"
mmd -i "$IMG" ::/bin   2>/dev/null || log_warn "Directory /bin già esistente"
mmd -i "$IMG" ::/lib   2>/dev/null || log_warn "Directory /lib già esistente"
mmd -i "$IMG" ::/dev   2>/dev/null || log_warn "Directory /dev già esistente"

log_ok "Directory create: /boot /bin /lib /dev"

# --- Copia file nella root ----------------------------------------------------

log_info "Copia Stage 2 (LOADER.BIN) nella root..."
mcopy -i "$IMG" "$LOADER" ::/LOADER.BIN
log_ok "LOADER.BIN copiato"

log_info "Copia Kernel (KERNEL.BIN) nella root..."
mcopy -i "$IMG" "$KERNEL" ::/KERNEL.BIN
log_ok "KERNEL.BIN copiato"

# --- Copia file di configurazione ---------------------------------------------

if [ -f "$KERNEL_CFG" ]; then
    log_info "Copia kernel.cfg in /boot/..."
    mcopy -i "$IMG" "$KERNEL_CFG" ::/boot/KERNEL.CFG
    log_ok "KERNEL.CFG copiato in /boot/"
else
    log_warn "kernel.cfg non trovato ($KERNEL_CFG) — sarà aggiunto in seguito"
fi

# --- Copia eventuali driver già compilati -------------------------------------

if ls build/drivers/*.drv >/dev/null 2>&1; then
    log_info "Copia driver ELF in /dev/..."
    for drv in build/drivers/*.drv; do
        fname=$(basename "$drv")
        mcopy -i "$IMG" "$drv" "::/dev/$fname"
        log_ok "  Driver copiato: $fname"
    done
else
    log_warn "Nessun driver trovato in build/drivers/ — /dev/ sarà vuota"
fi

# --- Copia eventuali programmi già compilati ----------------------------------

if ls build/bin/* >/dev/null 2>&1; then
    log_info "Copia programmi in /bin/..."
    for prog in build/bin/*; do
        fname=$(basename "$prog")

        # Copiare tutto ciò che si trova qui dentro è sbagliato: file
        # intermedi rimasti da build precedenti (.o, .d) finivano sul
        # floppy come se fossero programmi — 82 KB su 1.44 MB, cioè il 6%
        # dello spazio, occupati da roba non eseguibile. Si copia solo ciò
        # che è un eseguibile.
        case "$fname" in
            *.o|*.d|*.a|*.so) log_info "  Saltato (non eseguibile): $fname"; continue ;;
        esac
        [ -x "$prog" ] || { log_info "  Saltato (non eseguibile): $fname"; continue; }

        mcopy -i "$IMG" "$prog" "::/bin/$fname"
        log_ok "  Programma copiato: $fname"
    done
else
    log_warn "Nessun programma trovato in build/bin/ — /bin/ sarà vuota"
fi

# /bin/umount è lo stesso binario di /bin/mount: il programma guarda argv[0]
# e smonta se è stato invocato come 'umount'. Copiarlo due volte costa ~12 KB
# di floppy; compilarne uno secondo costerebbe lo stesso spazio più il
# rischio che le due copie divergano.
if [ -f build/bin/mount ]; then
    mcopy -i "$IMG" build/bin/mount ::/bin/umount
    log_ok "  Programma copiato: umount (stesso binario di mount)"
fi

# --- Copia eventuali shared libraries -----------------------------------------

if ls build/lib/*.so >/dev/null 2>&1; then
    log_info "Copia librerie in /lib/..."
    for lib in build/lib/*.so; do
        fname=$(basename "$lib")
        mcopy -i "$IMG" "$lib" "::/lib/$fname"
        log_ok "  Libreria copiata: $fname"
    done
else
    log_warn "Nessuna libreria trovata in build/lib/ — /lib/ sarà vuota"
fi

# --- Verifica finale contenuto floppy -----------------------------------------

log_info "Contenuto floppy finale:"
echo "-------------------------------------------"
mdir -i "$IMG" -/ :: 2>/dev/null || mdir -i "$IMG" ::
echo "-------------------------------------------"

# --- Statistiche finali -------------------------------------------------------

FINAL_SIZE=$(stat -c%s "$IMG")
log_ok "Immagine completata: $IMG"
log_info "  Dimensione: $FINAL_SIZE byte ($FLOPPY_SECTORS settori)"
log_info "  Formato:    FAT12, 1.44MB, 80 tracce, 2 testine, 18 settori/traccia"
log_info "  Volume:     EXOS"
log_info ""
log_info "Per testare con QEMU:"
log_info "  qemu-system-i386 -fda $IMG -m 32M -boot a"
log_info ""
log_info "Per testare con debug GDB:"
log_info "  qemu-system-i386 -fda $IMG -m 32M -boot a -s -S"
log_info "  (poi in altro terminale: gdb → target remote :1234)"

echo ""
log_ok "=============================="
log_ok " Floppy image pronta!         "
log_ok "=============================="
