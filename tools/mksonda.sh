#!/bin/bash
# =============================================================================
# tools/mksonda.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# This file is part of EX-OS, distributed under the GNU GPL v2.
# See the LICENSE file in the project root for the full license text.
# =============================================================================
#
# IL DISCHETTO DELLA SONDA — si avvia, guarda la macchina, scrive, si ferma.
#
# Non e' il floppy di EX-OS con un programma in piu': e' un dischetto che fa
# UNA cosa sola. Ci sta dentro il minimo per arrivare a un prompt — kernel,
# shell, tastiera — piu' sonda.drv, e un autoexec che la lancia da solo.
#
# ! PERCHE' UN'IMMAGINE A PARTE, E NON UN FILE IN PIU' SU floppy.img
#
# Sul floppy normale restano ventiseimila byte liberi: il referto di una
# macchina vera ne occupa il doppio, e non ci starebbe. Qui dentro c'e' meno
# di mezzo megabyte di sistema, e tutto il resto e' spazio per i referti —
# uno per ogni modalita' video che si vuole confrontare.
#
# ! E IL DISCHETTO VA IN SCRITTURA. Se la linguetta e' aperta, la sonda
# scrive il file e la scrittura fallisce a meta': il messaggio lo dice, ma
# lo dice a chi sta guardando lo schermo in quel momento.
# =============================================================================

set -e

IMG="dist/sonda.img"
BOOT_SECTOR="build/stage1.bin"
LOADER="build/stage2.bin"
KERNEL="build/kernel.bin"

FLOPPY_SECTORS=2880

# I programmi. Meno di cosi' non si arriva a un prompt; piu' di cosi' e'
# spazio tolto ai referti.
#
#   sh        la shell che esegue l'autoexec
#   ls        per vedere se il referto c'e' davvero
#   keymap    la macchina di prova ha la tastiera italiana e il dischetto
#             nasce americano
#   shutdown  per fermarla come si deve invece di staccare la corrente
#   hwinfo    dice a schermo quel che la sonda scrive su file: serve quando
#             si vuole guardare al volo senza spegnere
PROGRAMMI="sh ls keymap shutdown hwinfo"

# I driver. kbd serve per battere qualcosa, svga per cambiare modalita' fra
# un referto e l'altro, sonda e' il motivo per cui questo dischetto esiste.
DRIVER="kbd.drv svga.drv"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
log_info() { echo -e "${BLUE}[INFO]${NC}  $1"; }
log_ok()   { echo -e "${GREEN}[OK]${NC}    $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_err()  { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

for t in dd mformat mcopy mmd mdir; do
    command -v "$t" >/dev/null 2>&1 || log_err "manca lo strumento '$t' (sudo apt install mtools)"
done

# ! sonda.drv sta in build/drivers-cd, non in build/drivers, e non e' un
# capriccio: mkfloppy.sh copia sul floppy normale tutto quel che trova nella
# seconda, e li' non ci sta. Vedi il commento accanto a SONDA_OUT nel Makefile.
SONDA_DRV="build/drivers-cd/sonda.drv"

for f in "$BOOT_SECTOR" "$LOADER" "$KERNEL" "$SONDA_DRV" build/bin/sh; do
    [ -f "$f" ] || log_err "manca '$f' — compila prima con 'make'"
done

[ "$(stat -c%s "$BOOT_SECTOR")" -eq 512 ] || log_err "stage1.bin non e' di 512 byte"

mkdir -p dist

log_info "Immagine vuota da 1.44 MB..."
dd if=/dev/zero of="$IMG" bs=512 count=$FLOPPY_SECTORS status=none
mformat -f 1440 -v "EXOSSONDA" -i "$IMG" ::
dd if="$BOOT_SECTOR" of="$IMG" bs=512 count=1 conv=notrunc status=none

SIG=$(dd if="$IMG" bs=2 skip=255 count=1 status=none | od -A n -t x2 | tr -d " \n")
[ "$SIG" = "aa55" ] || log_err "firma di avvio mancante (trovato: $SIG)"
log_ok "avviabile: stage1 nel settore 0"

mmd -i "$IMG" ::/boot
mmd -i "$IMG" ::/bin
mmd -i "$IMG" ::/dev
mmd -i "$IMG" ::/lib

# --- Stage 2, con la modalita' video rimessa a TESTO -------------------------
#
# ! IL MODO NON STA IN kernel.cfg, STA DENTRO STAGE 2. Lo imposta INT 10h, cioe'
# il BIOS, cioe' qualcuno che gira prima del modo protetto: kernel.cfg e' la
# configurazione che si legge, il byte dentro LOADER.BIN e' il recapito che
# viene ubbidito (il perche' per esteso sta in testa a drivers/svga/svga.c).
#
# build/stage2.bin porta il byte dell'ultima prova fatta qui. Senza azzerarlo,
# il primo referto uscirebbe nella modalita' di ieri invece che in testo — e i
# due referti da sottrarre sarebbero due volte lo stesso.
TMPS2=$(mktemp)
cp "$LOADER" "$TMPS2"
python3 - "$TMPS2" <<'PY'
import sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
k = d.find(b'SVGAMODE')
if k < 0 or k + 8 >= len(d):
    sys.exit("firma SVGAMODE non trovata in stage2.bin")
d[k + 8] = 0            # 0 = console di testo 80x25
open(p, 'wb').write(d)
PY
mcopy -i "$IMG" "$TMPS2" ::/LOADER.BIN
rm -f "$TMPS2"
log_ok "stage2 copiato con la modalita' video azzerata (testo 80x25)"

mcopy -i "$IMG" "$KERNEL" ::/KERNEL.BIN

# --- La configurazione, scritta qui e non copiata da boot/kernel.cfg ---------
#
# Quella del floppy normale monta il CD, avvia login e lascia la tastiera
# americana: tre cose che su un dischetto di diagnosi sono solo tre modi di
# fermarsi prima di arrivare al punto.
TMPCFG=$(mktemp)
cat > "$TMPCFG" <<'CFG'
# =============================================================================
# kernel.cfg del dischetto della sonda
#
# Fa una cosa sola: arrivare al prompt e lanciare /boot/autoexec.sh, che
# scrive il referto. Niente CD da montare, niente accesso da fare.
# =============================================================================

[kernel]
loglevel    = 3
timer_hz    = 100

# ! ACCESO. Se qualcosa si ferma prima del prompt, su questa macchina non
# c'e' una seriale a dirlo: l'unica traccia e' quel che resta a schermo.
verboseboot = 1

# La macchina di prova ha la tastiera italiana. Si cambia da qui, o con
# `keymap us` una volta al prompt.
keymap      = it

# La modalita' video. Commentata = testo 80x25, che e' il PRIMO dei due
# referti. Per il secondo non si tocca questo file: si batte
# `svga.drv 800x600` e si riavvia — e' lui a scrivere dentro LOADER.BIN.
# svga      = 800x600

[env]
PATH        = /bin:/dev
HOME        = /
TERM        = vga

[boot]
shell       = /bin/sh
modules     = kbd

[modules]
kbd         = /dev/kbd.drv
CFG
mcopy -i "$IMG" "$TMPCFG" ::/boot/KERNEL.CFG
rm -f "$TMPCFG"
log_ok "kernel.cfg scritto (tastiera italiana, testo 80x25)"

# --- L'autoexec -------------------------------------------------------------
#
# ! LA SONDA SI LANCIA DA SOLA, e non e' una comodita'. Chi prova questo
# dischetto sta guardando uno schermo che potrebbe non accendersi affatto: se
# il referto dipendesse da un comando battuto a mano, una macchina che arriva
# al prompt senza mostrarlo non produrrebbe niente. Cosi' invece il file
# c'e' comunque, e lo si legge dopo, da un'altra parte.
TMPAUT=$(mktemp)
cat > "$TMPAUT" <<'AUT'
# autoexec.sh - il dischetto della sonda
#
# Una riga = un comando. `autoexec=0` in /boot/kernel.cfg salta questo file,
# e Alt+F2 da' comunque una shell pulita se qualcosa qui si blocca.

echo ===============================================================
echo  SONDA - scrivo il referto di questa macchina sul dischetto
echo ===============================================================
sonda.drv -auto
echo
echo Adesso:
echo   ls /              vedere che il referto ci sia
echo   svga.drv 800x600  cambiare modo, riavviare, e farne un secondo
echo   shutdown          fermare la macchina
AUT
mcopy -i "$IMG" "$TMPAUT" ::/boot/AUTOEXEC.SH
rm -f "$TMPAUT"
log_ok "autoexec.sh scritto (lancia la sonda da solo)"

for p in $PROGRAMMI; do
    if [ -x "build/bin/$p" ]; then
        mcopy -i "$IMG" "build/bin/$p" "::/bin/$p"
        log_ok "  /bin/$p"
    else
        log_warn "  build/bin/$p non c'e': saltato"
    fi
done

# ! SENZA /lib/libc.so I PROGRAMMI NON PARTONO. La libc e' condivisa: `ls`
# rispondeva «non trovo la libreria condivisa /lib/libc.so» e usciva con 1.
# La sonda no — nei .drv la libc e' dentro — ma un dischetto su cui non si
# riesce a fare `ls` e' un dischetto su cui non si vede il referto.
if [ -f build/lib/libc.so ]; then
    mcopy -i "$IMG" build/lib/libc.so ::/lib/libc.so
    log_ok "  /lib/libc.so"
else
    log_err "manca build/lib/libc.so — senza, /bin non funziona"
fi

mcopy -i "$IMG" "$SONDA_DRV" ::/dev/sonda.drv
log_ok "  /dev/sonda.drv"

for d in $DRIVER; do
    if [ -f "build/drivers/$d" ]; then
        mcopy -i "$IMG" "build/drivers/$d" "::/dev/$d"
        log_ok "  /dev/$d"
    else
        log_warn "  build/drivers/$d non c'e': saltato"
    fi
done

echo "-------------------------------------------"
mdir -i "$IMG" -/ :: 2>/dev/null || mdir -i "$IMG" ::
echo "-------------------------------------------"

LIBERI=$(mdir -i "$IMG" :: 2>/dev/null | grep -i 'free' | tail -1 | tr -dc '0-9')
log_ok "immagine pronta: $IMG"
log_info "  spazio per i referti: ${LIBERI:-?} byte"
log_info ""
log_info "Su una macchina vera:"
log_info "  dd if=$IMG of=/dev/fd0 bs=512   (linguetta di protezione CHIUSA)"
log_info ""
log_info "In QEMU, per vedere che si avvii e scriva:"
log_info "  qemu-system-i386 -fda $IMG -m 32M -boot a"
