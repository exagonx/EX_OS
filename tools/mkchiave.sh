#!/bin/sh
# =============================================================================
# tools/mkchiave.sh — la chiavetta USB di EX-OS a 64 bit: un DISCHETTO in RAM
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
#
#     make chiave64 [MB=64]                       fa dist/exos64-chiave.img
#     sudo make chiave64 DISPOSITIVO=/dev/sdX     e la scrive sulla chiavetta
#
# ! LA FORMA E' QUELLA DI dist/installa.img, che sul PC dell'utente si avvia
# (e' l'utente ad averla chiesta, il 10 ottobre 2026: «ritorniamo all'immagine
# floppy che carica interamente sulla RAM e da li' monta l'unita' USB»):
#
#   settori 0-2879     UN DISCHETTO da 1,44 MB, FAT12: settore di avvio del
#                      floppy, Stage 2 con RAMDISCO=1, il kernel a 64 bit, la
#                      shell, la tastiera, il PCI, i tre driver USB, mount e
#                      pochi comandi. Il BIOS avvia la chiavetta come un
#                      floppy; Stage 2 lo copia INTERO in RAM leggendolo una
#                      traccia per volta; il kernel ci monta la radice.
#   dal settore 2880   IL SISTEMA: un volume FAT16 con tutto l'albero del CD a
#                      64 bit. Non sta nel dischetto e non serve all'avvio.
#                      Lo monta in /sistema, una volta partiti i driver USB,
#                      /dev/fetta.drv - che lo trova dall'etichetta EXOS64.
#
# ! NIENTE TABELLA DELLE PARTIZIONI, ed e' voluto: con una tabella nel settore
# 0 un BIOS puo' prendere la chiavetta per un disco e non avviarla piu' come
# dischetto. Per questo il secondo volume non lo annuncia nessuno, e serve
# fetta.drv a dire che c'e'.
#
# ! LE DUE CHIAVETTE DI PRIMA NON HANNO FUNZIONATO SU QUEL PC: quella
# installata (make usb64: il kernel non legge l'USB) e quella con la radice in
# una partizione caricata con INT 13h/42h (tools/mkchiave-disco.sh: la lettura
# fallisce). Quest'ultima resta nel repository ma nessuna voce di make la usa.
#
# ! NON TOCCA NESSUN DISPOSITIVO. Fa un file; a scriverlo e' `make chiave64
# DISPOSITIVO=...`, con i controlli e la conferma di tools/mkusb.sh.
# =============================================================================
set -e
cd "$(dirname "$0")/.."

# EXOS_ARCH=i386 fa la stessa chiavetta per la versione a 32 bit (make usb):
# vedi in fondo al passo 1.
ARCH="${EXOS_ARCH:-x86_64}"
IMG=dist/exos64-usb.img
[ "$ARCH" = i386 ] && IMG=dist/exos-usb.img
STAGE1=build-64/avvio/stage1-288.bin
STAGE2="${EXOS_STAGE2_CHIAVE:-build-64/avvio/stage2-rd1-288.bin}"
KERNEL=build-64/kernel-intero.bin
ALBERO=build-64/iso-exos
B=build-64/bin
D=build-64/drivers

# Che cosa sta nel dischetto. Poco, perche' la libc a 64 bit e' dentro ogni
# programma e il kernel da solo ne prende 300 KB: cio' che serve ad avere una
# shell e a raggiungere la chiavetta. Tutto il resto e' in /sistema.
# ! UN DISCHETTO DA 2,88 MB (l'utente, 10 ottobre 2026: «dato che il dispositivo
# USB e' capiente fallo di 2,88 secondo lo standard BIOS dei formati
# conosciuti»), con dentro: l'installazione minimale, gli strumenti di rete, e
# i driver e il supporto USB - il driver di rete che manca si prendera' da una
# chiavetta DOPO l'installazione.
# ! DEI 2,88 SE NE USANO 2 MB: i caricatori leggono cluster di un settore, e
# una FAT12 non passa i 4084 cluster. Vedi bootloader/stage1/boot.asm.
COMANDI="sh ls cp rm mount mkdir shutdown keymap disk fdisk mkfs install automount blkscan dmesg mem uname netdetect ipcfg ping dhcp netupdate telnetd login hwconfig"
DRIVER="kbd pci ehci ohci uhci nforce ip e1000 rtl8169"

for f in "$STAGE1" "$STAGE2"; do
    [ -f "$f" ] || { echo "mkchiave: manca $f (lo assembla make usb / make usb64)" >&2; exit 1; }
done
if [ "$ARCH" = i386 ]; then
    [ -f dist/installa.img ] || { echo "mkchiave: manca dist/installa.img: prima 'make installa'" >&2; exit 1; }
else
    [ -f "$KERNEL" ]     || { echo "mkchiave: manca $KERNEL: prima 'make iso64'" >&2; exit 1; }
    [ -d "$ALBERO/bin" ] || { echo "mkchiave: manca $ALBERO: prima 'make iso64'" >&2; exit 1; }
    for c in $COMANDI; do [ -f "$B/$c" ]     || { echo "mkchiave: manca $B/$c" >&2; exit 1; }; done
    for d in $DRIVER;  do [ -f "$D/$d.drv" ] || { echo "mkchiave: manca $D/$d.drv" >&2; exit 1; }; done
fi

T=$(mktemp -d /tmp/exos-chiave.XXXXXX)
trap 'rm -rf "$T"' EXIT

# --- 1. il dischetto ------------------------------------------------------------
F="$T/floppy.img"
# 80 cilindri, 2 testine, 36 settori: 5760 settori. Il FAT12 dentro ne dichiara
# 4123 - una FAT di 12 settori, cluster di un settore, 14 settori di radice:
# gli stessi numeri scritti nel settore di avvio (boot.asm, DISCHETTO=288).
dd if=/dev/zero of="$F" bs=512 count=5760 status=none
mformat -i "$F" -v "EXOS64  " -T 4123 -h 2 -s 36 -c 1 -r 14 -L 12 -m 0xf0 ::
dd if="$STAGE1" of="$F" bs=512 count=1 conv=notrunc status=none
if [ "$ARCH" = i386 ]; then
    # ! A 32 BIT IL CONTENUTO E' QUELLO DI dist/installa.img, preso tale e quale
    # (kernel, shell, attrezzi del disco, rete, USB, netupdate, libc.so): e'
    # il dischetto che sul PC vero funziona, e un secondo elenco qui un giorno
    # ne divergerebbe. Si cambia il caricatore - quello da 2,88 MB - e nei 600
    # KB in piu' entrano telnetd e login, se il sistema li ha costruiti.
    mkdir -p "$T/inst"
    mcopy -s -n -Q -i dist/installa.img ::/ "$T/inst/" 2>/dev/null || mcopy -s -n -Q -i dist/installa.img '::*' "$T/inst/"
    rm -f "$T/inst/LOADER.BIN" "$T/inst/loader.bin"
    for v in "$T/inst"/*; do mcopy -i "$F" -s -Q "$v" ::; done
    mcopy -i "$F" -o "$STAGE2" ::/LOADER.BIN
    for c in telnetd login; do
        [ -f "build/iso-exos/bin/$c" ] && mcopy -i "$F" "build/iso-exos/bin/$c" "::/bin/$c" || true
    done
    # avvio silenzioso e l'indirizzo in fondo, come a 64 bit
    mcopy -n -i "$F" ::/boot/kernel.cfg "$T/k.cfg" && sed -i 's/^verboseboot *= *1/verboseboot = 0/' "$T/k.cfg" &&
        mcopy -o -i "$F" "$T/k.cfg" ::/boot/kernel.cfg
    mcopy -n -i "$F" ::/boot/autoexec.sh "$T/a.sh"
    { echo '!silenced'; cat "$T/a.sh"
      echo 'echo PER ENTRARE DA UN ALTRO COMPUTER (in chiaro, sulla rete locale):'
      echo 'echo   telnetd &                     chiede utente e password'
      echo 'echo   telnetd -s &                  da la shell SENZA chiedere niente'
      echo 'echo'
      echo 'echo ====== L INDIRIZZO DI QUESTA MACCHINA ======'
      echo 'ipcfg'
      echo 'echo @echo Sistema gia avviato: rete e driver sono accesi. > /boot/autoexec.sh'; } > "$T/a2.sh"
    mcopy -o -i "$F" "$T/a2.sh" ::/boot/autoexec.sh
    printf 'avvio   = no\nporta   = 23\nshell   = /bin/login\n' > "$T/telnetd.cfg"
    mcopy -o -i "$F" "$T/telnetd.cfg" ::/boot/telnetd.cfg
else
mcopy -i "$F" "$STAGE2" ::/LOADER.BIN
mcopy -i "$F" "$KERNEL" ::/KERNEL.BIN
# (/sistema NON si crea qui: mount vuole un punto che non esiste ancora)
mmd -i "$F" ::/boot ::/bin ::/dev ::/USB
for c in $COMANDI; do mcopy -i "$F" "$B/$c" "::/bin/$c"; done
for d in $DRIVER;  do mcopy -i "$F" "$D/$d.drv" "::/dev/$d.drv"; done

cat > "$T/kernel.cfg" <<'CFG'
# kernel.cfg della chiavetta a 64 bit (tools/mkchiave.sh)
[kernel]
loglevel    = 3
timer_hz    = 100
verboseboot = 0
keymap      = it

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
cat > "$T/autoexec.sh" <<'AUTO'
# autoexec.sh della chiavetta a 64 bit (tools/mkchiave.sh)
# ! QUESTO SCRIPT DEVE GIRARE UNA VOLTA SOLA. La shell lo esegue a ogni
# accesso: dopo un `login` (alla tastiera o da telnet) ripartirebbero pci.drv,
# il driver della scheda e ip.drv SOPRA quelli gia' accesi, e un secondo
# driver rimette a zero la scheda sotto il primo - la rete si ferma. Visto
# dall'utente sul PC vero il 10 ottobre 2026. L'ultima riga lo sostituisce
# con una riga sola; la radice e' in RAM, al prossimo avvio torna questo.
# (le righe non si ripetono a schermo: si vede solo cio' che dicono)
!silenced
echo ===============================================================
echo  EX-OS a 64 bit - dalla chiavetta (questo sistema e tutto in RAM)
echo ===============================================================
/dev/pci.drv &
/dev/ehci.drv -avvio &
/dev/ohci.drv -avvio &
/dev/uhci.drv -avvio &
automount &
echo
echo Accendo la rete: scheda, stack IP, indirizzo
netdetect -c
/dev/ip.drv &
dhcp
echo
ipcfg
echo
echo PER INSTALLARE SU UN DISCO, in ordine:
echo   disk                          quale disco c e: hd0, hd1...
echo   fdisk hd0                     n = nuova partizione, tipo 83, poi w
echo   mkfs -t ext2 -L exos hd0p1    formatta (chiede si)
echo   mount hd0p1 /disk
echo   install -t /disk              copia questo sistema e lo rende avviabile
echo   shutdown                      poi si riavvia dal disco
echo DAL DISCO, per avere tutto il resto:
echo   netupdate                     la prima volta chiede il server
echo UNA CHIAVETTA USB compare da sola in /USB. Un driver che manca:
echo   cp /USB/DRIVE0/xxx.drv /dev
echo PER ENTRARE DA UN ALTRO COMPUTER (in chiaro, sulla rete locale):
echo   telnetd &                     chiede utente e password
echo   telnetd -s &                  da la shell SENZA chiedere niente
echo
echo ====== L INDIRIZZO DI QUESTA MACCHINA ======
ipcfg
echo @echo Sistema gia avviato: rete e driver sono accesi. > /boot/autoexec.sh
AUTO
mcopy -i "$F" "$T/kernel.cfg" ::/boot/kernel.cfg
mcopy -i "$F" "$T/autoexec.sh" ::/boot/autoexec.sh
# telnetd NON parte da solo (avvio = no): lo accende chi sta alla tastiera, e
# sceglie lui se con l'accesso o senza. Vedi l'ultima schermata di autoexec.sh.
printf 'avvio   = no\nporta   = 23\nshell   = /bin/login\n' > "$T/telnetd.cfg"
mcopy -i "$F" "$T/telnetd.cfg" ::/boot/telnetd.cfg
fi

# --- 2. l'immagine: il dischetto, e basta ------------------------------------------
# Va bene su un dischetto vero e in testa a una chiavetta: e' la stessa cosa.
mkdir -p dist
cp "$F" "$IMG"

echo "mkchiave: $IMG (2,88 MB)"
echo "          dischetto: $(mdir -i "$F" :: | tail -1 | sed 's/^ *//')"
