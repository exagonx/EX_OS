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

# Il nome si puo' dare da fuori: `tools/mksonda.sh dist/test_aa3k.img` fa la
# stessa immagine con un altro nome. Serve a tenere separati i dischetti di
# prova di una macchina dalla sonda di sempre, senza due script che divergono.
IMG="${1:-dist/sonda.img}"
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
#   blkscan   rilegge le partizioni di una chiavetta appena comparsa
#   automount la monta da sola in /USB/DRIVE0
#   mount     montare a mano quando l'automatismo non ce la fa: e' lui a
#             stampare il NUMERO dell'errore, che e' la sola cosa che
#             distingue sei cause diverse
#   disk      dice quali dispositivi a blocchi esistono e quanto sono grandi
#   fdisk mkfs install  ! GLI ATTREZZI DEL DISCO, dal 14 settembre 2026.
#             Adesso che il dischetto si fa guidare dalla rete, una
#             installazione si puo' fare da un altra stanza: disk per vedere
#             cosa c e, fdisk per partizionare, mkfs per formattare, install
#             per scriverci il sistema di base. Costano centodiecimila byte
#             su duecentoventimila liberi.
#             ! install COPIA IL SISTEMA CHE STA GIRANDO, cioe questo
#             dischetto: quel che ne esce e un sistema MINIMO che si avvia.
#             Riempirlo e il passo dopo, e lo fa la rete con netupdate.
#   mkdir     ! SERVE A install, E LA SUA ASSENZA E COSTATA UN VIAGGIO. Un
#             sistema appena installato vuole /dev, e senza mkdir non c e
#             modo di crearla a mano quando qualcosa va storto.
PROGRAMMI="sh ls cp keymap shutdown hwinfo mount disk fdisk mkfs install mkdir"
#   dhcp      ! SENZA QUESTO LA RETE NON PARTE DA SOLA. netdetect trova la
#             scheda e avvia il driver, ip.drv monta lo stack, ma un
#             indirizzo non se lo da' nessuno: il DHCP sta SOPRA UDP come un
#             qualunque client, e il perche' e' in cima a drivers/net/ip_proto.h
#             — uno stack che parla DHCP da solo e' uno stack che fa due
#             mestieri. Costava diciottomila byte e la sua assenza e' costata
#             un viaggio all'Acer.
#   telnetd   ! CI STA SEMPRE, LO AVVIA SOLO REMOTO=1. Copiarlo costa
#             ventitremila byte su un dischetto che ne ha trecentomila
#             liberi; deciderlo qui in base a una variabile vorrebbe dire
#             un elenco che cambia da un'invocazione all'altra, e
#             verifica-dipendenze-sonda nel Makefile legge PROPRIO QUESTA
#             RIGA per sapere cosa deve essere ricostruito.
#   scarica   ! E L UNICO MODO DI CORREGGERE QUALCOSA SENZA RISCRIVERE IL
#             DISCHETTO. Questa macchina sta in un altra stanza e ogni
#             riscrittura del floppy costa un viaggio e un riavvio: con
#             scarica un programma aggiornato arriva dalla rete e si mette
#             in /bin, che tanto e in RAM. Tredicimila byte per non
#             rifare la strada.
PROGRAMMI_CD="blkscan automount netdetect ipcfg ping dhcp host audio telnetd scarica"

# I driver. kbd serve per battere qualcosa, svga per cambiare modalita' fra
# un referto e l'altro, sonda e' il motivo per cui questo dischetto esiste.
DRIVER="kbd.drv svga.drv pci.drv uhci.drv"

# I driver che stanno in build/drivers-cd. Il PCI e' il fornitore di tutti; i
# tre controller USB coprono qualunque macchina di quell'epoca.
#
# ! CI STANNO PERCHE' QUESTO DISCHETTO E' QUASI VUOTO: novecentomila byte
# liberi contro i diciottomila del floppy normale. E servono: su una macchina
# il cui lettore sta sull'USB, la chiavetta e' l'unico posto dove il referto
# puo' restare dopo lo spegnimento.
# ! sis900.drv STA QUI PERCHE' QUESTO DISCHETTO VA SU QUELLA MACCHINA. QEMU
# non emula la SiS 900 — `qemu-system-i386 -device help` non la nomina — quindi
# l'unico posto dove quel driver si puo' provare e' l'Acer, e l'unico modo di
# portarcelo e' questo dischetto.
# ! E CI SONO ANCHE LE ALTRE TRE SCHEDE DI RETE, dal 14 settembre 2026, per
# due ragioni che vanno insieme. La prima e' che un dischetto di prova che
# riconosce una sola scheda serve a una macchina sola: ne2k, pcnet ed e1000
# costano settantottomila byte su trecentomila liberi e coprono quasi tutto
# quel che si trova in giro.
#
# ! LA SECONDA E' CHE COSI' IL DISCHETTO SI PROVA IN QEMU PRIMA DI PORTARLO.
# La SiS 900 QEMU non la emula, ma ne2k, pcnet ed e1000 si': con una di
# quelle a bordo si puo' vedere QUI che netdetect trova la scheda, che il
# DHCP prende l indirizzo e che la shell remota risponde davvero. Senza, ogni
# modifica al dischetto si verifica solo andando alla macchina — ed e' come
# si e' scoperto, dopo un viaggio, che netdetect non partiva affatto.
DRIVER_CD="ehci.drv ohci.drv sonda.drv sis.drv mappa.drv sis900.drv ne2k.drv pcnet.drv e1000.drv ip.drv ac97.drv cardbus.drv"

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
mmd -i "$IMG" ::/USB      # dove automount monta le chiavette

# --- Stage 2, con la modalita' video rimessa a TESTO -------------------------
#
# ! IL MODO NON STA IN kernel.cfg, STA DENTRO STAGE 2. Lo imposta INT 10h, cioe'
# il BIOS, cioe' qualcuno che gira prima del modo protetto: kernel.cfg e' la
# configurazione che si legge, il byte dentro LOADER.BIN e' il recapito che
# viene ubbidito (il perche' per esteso sta in testa a drivers/svga/svga.c).
#
# build/stage2.bin porta il byte dell'ultima prova fatta qui, e qui lo si mette
# a quello che serve a QUESTA immagine.
#
# ! E DA QUANDO LA RADICE STA IN RAM, QUESTO E' L'UNICO POSTO DA CUI SI PUO'
# CAMBIARE. `svga.drv` scrive dentro /LOADER.BIN della radice, che li' e' una
# copia in memoria: al riavvio non e' cambiato niente. Il 10 settembre 2026 due
# referti presi «in due modalita' diverse» sono usciti identici per questo.
#
#   MODO_VIDEO=0  testo 80x25 (predefinito)
#   MODO_VIDEO=1  640x480     2  800x600     3  1024x768
# =============================================================================
# REMOTO=1 — il dischetto si fa guidare dalla rete
#
# ! SPENTO DI SUO, E NON PER PRUDENZA FORMALE. Con REMOTO=1 il dischetto apre
# una shell telnet SENZA ACCESSO: chi arriva sulla porta 23 si trova un /bin/sh
# da amministratore, e la password non c'e' perche' non c'e' niente da
# digitare. Su una rete di casa, per provare i driver di una macchina che sta
# in un'altra stanza, e' esattamente quel che serve; su qualunque altra rete
# e' una porta aperta.
#
# ! E TELNET E' IN CHIARO comunque, password o no: e' nato prima che qualcuno
# ascoltasse. Il commento in cima a bin/telnetd/telnetd.c lo dice meglio —
# «su una rete di cui non ci si fida non si accende».
#
# Il dischetto normale della sonda non cambia: telnetd ce l'ha a bordo ma
# nessuno lo avvia, e si puo' lanciare a mano quando serve.
REMOTO="${REMOTO:-0}"

if [ "$REMOTO" = "1" ]; then
    RIGHE_REMOTO="echo
echo QUESTO DISCHETTO SI FA GUIDARE DALLA RETE.
echo L indirizzo e quello stampato qui sopra da ipcfg.
echo Da un altra macchina:  telnet QUELL INDIRIZZO
echo Ti trovi una shell da amministratore, senza password.
telnetd -s &
echo"
else
    RIGHE_REMOTO="echo   telnetd -s ^&      apre una shell telnet sulla porta 23,
echo                     senza password: si fa guidare dalla rete"
fi

MODO_VIDEO="${MODO_VIDEO:-0}"
case "$MODO_VIDEO" in
    0) MODO_NOME="testo 80x25" ;;
    1) MODO_NOME="640x480" ;;
    2) MODO_NOME="800x600" ;;
    3) MODO_NOME="1024x768" ;;
    *) log_err "MODO_VIDEO=$MODO_VIDEO: sono 0, 1, 2 o 3" ;;
esac

TMPS2=$(mktemp)
cp "$LOADER" "$TMPS2"
python3 - "$TMPS2" "$MODO_VIDEO" <<'PY'
import sys
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
k = d.find(b'SVGAMODE')
if k < 0 or k + 8 >= len(d):
    sys.exit("firma SVGAMODE non trovata in stage2.bin")
d[k + 8] = int(sys.argv[2])
open(p, 'wb').write(d)
PY
mcopy -i "$IMG" "$TMPS2" ::/LOADER.BIN
rm -f "$TMPS2"
log_ok "stage2 copiato, modalita' video $MODO_VIDEO ($MODO_NOME)"

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
echo
echo Accendo l USB: se infili una chiavetta la monto in /USB/DRIVE0
/dev/pci.drv &
/dev/ehci.drv -avvio &
/dev/ohci.drv -avvio &
/dev/uhci.drv -avvio &
automount &
echo
echo Accendo la rete: scheda, stack IP, indirizzo
#
# ! LE TRE RIGHE VANNO IN QUEST ORDINE E NON E UNA FORMALITA. netdetect -c
# trova la scheda e AVVIA IL DRIVER (netdetect da solo elenca e basta, ed e
# l errore che viene naturale fare); ip.drv monta lo stack sopra quel driver;
# dhcp sta SOPRA UDP come un client qualunque e chiede l indirizzo. Saltarne
# una lascia la catena a meta, e ognuna dice quale manca.
netdetect -c
/dev/ip.drv &
#
# ! dhcp SENZA & E SENZA -r, ED E' VOLUTO. In primo piano BLOCCA finche'
# l indirizzo non c e, e quel che viene dopo ha bisogno che ci sia: telnetd
# chiede allo stack di mettersi in ascolto, e uno stack senza indirizzo non
# sa su cosa. Con dhcp -r ^& la riga dopo parte subito e vince la corsa una
# volta su due, cioe un dischetto che a volte si fa guidare e a volte no.
# La concessione dura ore: per una sessione di prove non serve rinnovarla,
# e se scade si rilancia dhcp -r ^& a mano.
dhcp
echo
ipcfg
echo
AUT

# ! L HEREDOC SOPRA E QUOTATO — <<'AUT' — quindi NON espande le variabili, ed
# e' giusto cosi': dentro ci sono ^& e altri caratteri che una espansione
# rovinerebbe. Le righe che cambiano fra un dischetto e l altro si aggiungono
# qui in mezzo, fra due heredoc, invece di togliere le virgolette e dover poi
# proteggere tutto il resto.
printf '%s\n' "$RIGHE_REMOTO" >> "$TMPAUT"

cat >> "$TMPAUT" <<'AUT'
echo Adesso:
echo   ls /              vedere che il referto ci sia
echo   ls /USB/DRIVE0    la chiavetta, quando l hai infilata
echo   cp /SONDA1.TXT /USB/DRIVE0/    portarsi via il referto
echo   ipcfg             che indirizzo ha preso
echo   ping 8.8.8.8      se la rete esce davvero
echo   sis900.drv -debug   TUTTO in /SIS900.TXT: registri, le 32 righe del
echo                       PHY, i 16 descrittori, i contatori. Con la rete
echo                       accesa lo scrive il driver stesso, senza toccarla.
echo                       ! la radice e in RAM: cp /SIS900.TXT /USB/DRIVE0/
echo   sis900.drv -d     la rete: MAC, filtro, contatori, e cosa vogliono dire
echo   sis900.drv -phy   chi risponde sul filo di gestione del PHY
echo   sis900.drv -mac 00:11:22:33:44:55 ^& poi /dev/ip.drv ^& poi dhcp -r ^&
echo                     se il MAC esce a zero: lo trovi con ipconfig /all
echo   sis.drv -prova /PROVA.TXT   le quattro varianti del video
echo   cardbus.drv       lo slot PCMCIA, senza toccarlo
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

# umount e' lo stesso binario di mount: guarda argv[0]. Copiarlo due volte
# costa dodici KB su un dischetto che ne ha settecentomila liberi.
if [ -f build/bin/mount ]; then
    mcopy -i "$IMG" build/bin/mount ::/bin/umount
    log_ok "  /bin/umount"
fi

for p in $PROGRAMMI_CD; do
    if [ -x "build/bin-cd/$p" ]; then
        mcopy -i "$IMG" "build/bin-cd/$p" "::/bin/$p"
        log_ok "  /bin/$p"
    else
        log_warn "  build/bin-cd/$p non c'e': saltato"
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

for d in $DRIVER_CD; do
    if [ -f "build/drivers-cd/$d" ]; then
        mcopy -i "$IMG" "build/drivers-cd/$d" "::/dev/$d"
        log_ok "  /dev/$d"
    else
        log_warn "  build/drivers-cd/$d non c'e': saltato"
    fi
done

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
