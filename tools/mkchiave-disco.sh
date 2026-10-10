#!/bin/sh
# =============================================================================
# tools/mkchiave.sh — la chiavetta USB di EX-OS che parte su QUALUNQUE BIOS
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     tools/mkchiave.sh [MB]         fa dist/exos64-chiave.img  (di serie 64 MB)
#     make chiave64 [MB=64]          lo stesso, e assembla prima il caricatore
#     sudo make chiave64 DISPOSITIVO=/dev/sdX     e la scrive sulla chiavetta
#
# ! PERCHE' NON BASTA `make usb64`. Quella chiavetta e' un disco installato:
# il BIOS la avvia, carica il kernel, e il kernel va a cercare la radice sui
# dischi ATA - dove una chiavetta non sta. Provato dall'utente il 10 ottobre
# 2026: «si avvia ma la shell non carica». I driver USB sono programmi, stanno
# sul disco che non si riesce a leggere.
#
# QUI LA RADICE STA IN RAM (l'idea e' dell'utente: «un ramdrive che carica
# un'immagine, e da li' monta i driver USB per accedere alla chiavetta»):
#
#   settore 0          l'MBR di sempre, con la tabella delle partizioni
#   settore 8          Stage 2, assemblato con RAMDISCO=2
#   settore 64         il kernel
#   partizione 1       ATTIVA, FAT16, 16 MB: IL SISTEMA. Nel suo primo settore
#                      c'e' il settore di avvio da disco (boothd) con la mappa
#                      di Stage 2 e del kernel. Stage 2, finche' il BIOS c'e',
#                      la copia INTERA in RAM; il kernel ci monta la radice.
#   partizione 2       FAT, il resto: I DATI. La radice in RAM sparisce allo
#                      spegnimento; questa no. La montano i driver USB e
#                      `automount`, che partono dalla radice in RAM: /USB/...
#
# ! SI COSTRUISCE TUTTA DALL'HOST, senza avviare EX-OS: Stage 2 e kernel
# stanno in settori contigui fuori dalle partizioni, quindi la mappa dei
# settori sono tre numeri scritti qui e non il risultato di un'installazione.
#
# ! LA PARTIZIONE DEL SISTEMA NON PASSA I 24 MB: Stage 2 ne carica al piu'
# tanti (bootloader/stage2/loader.asm, .disco), perche' il volume in RAM sta
# nella fascia dei primi 64 MB del kernel. Il sistema a riga di comando ne
# occupa meno di dieci.
#
# ! NON TOCCA NESSUN DISPOSITIVO. Fa un file. A scriverlo sulla chiavetta e'
# `make chiave64 DISPOSITIVO=...`, che passa da tools/mkusb.sh e dalle sue
# protezioni (il dispositivo nominato, «confirm» battuto a mano).
# =============================================================================
set -e
cd "$(dirname "$0")/.."

MB="${1:-64}"
IMG=dist/exos64-chiave.img
STAGE2="${EXOS_STAGE2_CHIAVE:-build-64/avvio/stage2-chiave.bin}"
KERNEL=build-64/kernel-intero.bin
ALBERO=build-64/iso-exos
SIS_MB=16                           # la partizione del sistema
P1=2048                             # dove comincia (1 MiB: lo spazio prima e' di Stage 2 e del kernel)
LBA_S2=8
LBA_K=64

for f in build/boot/mbr.bin build/boot/boothd.bin "$STAGE2" "$KERNEL"; do
    [ -f "$f" ] || { echo "mkchiave: manca $f (make iso64, poi make chiave64)" >&2; exit 1; }
done
[ -d "$ALBERO/bin" ] || { echo "mkchiave: manca $ALBERO: prima 'make iso64'" >&2; exit 1; }
[ "$MB" -ge $((SIS_MB + 8)) ] || { echo "mkchiave: servono almeno $((SIS_MB + 8)) MB" >&2; exit 1; }

SET_S2=$(( ($(stat -c%s "$STAGE2") + 511) / 512 ))
BYTE_K=$(stat -c%s "$KERNEL")
SET_K=$(( (BYTE_K + 511) / 512 ))
[ $((LBA_S2 + SET_S2)) -le $LBA_K ] || { echo "mkchiave: Stage 2 non sta prima del kernel" >&2; exit 1; }
[ $((LBA_K + SET_K)) -le $P1 ]      || { echo "mkchiave: il kernel non sta prima della partizione" >&2; exit 1; }

SET_P1=$(( SIS_MB * 2048 ))
P2=$(( P1 + SET_P1 ))
SET_P2=$(( MB * 2048 - P2 ))
T=$(mktemp -d /tmp/exos-chiave.XXXXXX)
trap 'rm -rf "$T"' EXIT

# --- la partizione del sistema: un FAT16 con dentro l'albero del CD -----------
dd if=/dev/zero of="$T/p1.img" bs=512 count=$SET_P1 status=none
mformat -i "$T/p1.img" -v EXOS64 -c 4 -h 255 -s 63 -H $P1 -T $SET_P1 ::
# Tutto cio' che c'e' sul CD a 64 bit, meno la scrivania (exwin: caratteri e
# icone di un ExWin che a 64 bit non c'e' ancora, 8 MB).
for d in "$ALBERO"/*; do
    case "$(basename "$d")" in exwin) continue ;; esac
    mcopy -i "$T/p1.img" -s -Q "$d" ::
done
# ! SU QUESTA CHIAVETTA PARTE ANCHE IL DRIVER UHCI (USB 1.1 delle macchine
# Intel). Sul CD e sul disco lo script di avvio accende solo EHCI e OHCI, perche'
# li' una chiavetta e' un di piu'; qui la chiavetta e' il supporto da cui si e'
# partiti, e su una macchina col solo UHCI nessuno la monterebbe.
if mcopy -i "$T/p1.img" ::/boot/avvio.sh "$T/avvio.sh" 2>/dev/null &&
   grep -q "ehci.drv -avvio" "$T/avvio.sh" && ! grep -q "^/dev/uhci.drv -avvio" "$T/avvio.sh"; then
    sed -i 's|^/dev/ohci.drv -avvio \&|&\n/dev/uhci.drv -avvio \&|' "$T/avvio.sh"
    mcopy -i "$T/p1.img" -o "$T/avvio.sh" ::/boot/avvio.sh
fi

# Il suo primo settore: il settore di avvio da disco, col BPB che mformat ha
# appena scritto (byte 3..89) e la mappa dei settori a 0x1A0.
python3 - "$T/p1.img" build/boot/boothd.bin $LBA_S2 $SET_S2 $BYTE_K $LBA_K $SET_K <<'PY'
import struct, sys
img, boothd, lba_s2, set_s2, byte_k, lba_k, set_k = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:])
b = bytearray(open(boothd, 'rb').read())
assert len(b) == 512 and struct.unpack_from('<I', b, 0x1A0)[0] == 0x44485845, "boothd.bin non e' quello atteso"
with open(img, 'r+b') as f:
    fat = f.read(512)
    b[3:90] = fat[3:90]                                     # il BPB del volume, intatto
    struct.pack_into('<IH', b, 0x1A0 + 4, lba_s2, set_s2)   # Stage 2: LBA assoluto, settori
    struct.pack_into('<IH', b, 0x1A0 + 10, byte_k, 1)       # kernel: byte esatti, un intervallo
    struct.pack_into('<IH', b, 0x1A0 + 16, lba_k, set_k)    #   l'intervallo
    f.seek(0); f.write(b)
    # ! LA SOMMA DI CONTROLLO DEL VOLUME, a 0x1F0 del suo primo settore: 'EXRD'
    # e la somma di tutte le parole di 32 bit (col campo a zero). Il kernel la
    # ricalcola su cio' che Stage 2 ha messo in RAM e dice a schermo se
    # coincide: su una macchina vera e' l'unico modo di sapere se il BIOS ha
    # letto giusto, prima di dare la colpa a tutto il resto.
    f.seek(0); tutto = bytearray(f.read())
    tutto[0x1F0:0x1F8] = b'EXRD' + bytes(4)
    somma = sum(struct.unpack('<%dI' % (len(tutto) // 4), tutto)) & 0xFFFFFFFF
    f.seek(0x1F0); f.write(b'EXRD' + struct.pack('<I', somma))
PY

# --- la partizione dei dati ----------------------------------------------------
dd if=/dev/zero of="$T/p2.img" bs=512 count=$SET_P2 status=none
mformat -i "$T/p2.img" -v DATI -h 255 -s 63 -H $P2 -T $SET_P2 ::
printf 'Questa partizione resta: la radice di EX-OS su questa chiavetta sta in RAM\r\ne sparisce allo spegnimento. Da EX-OS e'"'"' in /USB.\r\n' > "$T/leggimi.txt"
mcopy -i "$T/p2.img" "$T/leggimi.txt" ::/LEGGIMI.TXT

# --- l'immagine -------------------------------------------------------------------
mkdir -p dist
dd if=/dev/zero of="$IMG" bs=1M count="$MB" status=none
dd if="$STAGE2"   of="$IMG" bs=512 seek=$LBA_S2 conv=notrunc status=none
dd if="$KERNEL"   of="$IMG" bs=512 seek=$LBA_K  conv=notrunc status=none
dd if="$T/p1.img" of="$IMG" bs=512 seek=$P1     conv=notrunc status=none
dd if="$T/p2.img" of="$IMG" bs=512 seek=$P2     conv=notrunc status=none
python3 - "$IMG" build/boot/mbr.bin $P1 $SET_P1 $P2 $SET_P2 <<'PY'
import struct, sys
img, mbr, p1, n1, p2, n2 = sys.argv[1], sys.argv[2], *map(int, sys.argv[3:])
m = bytearray(open(mbr, 'rb').read())
assert len(m) == 512
def voce(attiva, tipo, primo, quanti):      # CHS «oltre il limite»: conta l'LBA
    return struct.pack('<B3sB3sII', 0x80 if attiva else 0, b'\xfe\xff\xff', tipo, b'\xfe\xff\xff', primo, quanti)
m[446:462] = voce(1, 0x06, p1, n1)          # FAT16, attiva: il sistema
m[462:478] = voce(0, 0x0C if n2 > 1048576 else 0x06, p2, n2)   # FAT: i dati
m[478:510] = bytes(32)
m[510:512] = b'\x55\xaa'
with open(img, 'r+b') as f: f.write(m)
PY

echo "mkchiave: $IMG (${MB} MB)"
echo "          sistema: partizione 1, FAT16 ${SIS_MB} MB, $(mdir -i "$T/p1.img" -s :: 2>/dev/null | tail -1 | sed 's/^ *//')"
echo "          Stage 2 al settore $LBA_S2 ($SET_S2), kernel al settore $LBA_K ($SET_K)"
