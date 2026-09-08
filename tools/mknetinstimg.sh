#!/bin/bash
# =============================================================================
# tools/mknetinstimg.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
#
# SPDX-License-Identifier: GPL-2.0-or-later
# This file is part of EX-OS, distributed under the GNU GPL v2.
# See the LICENSE file in the project root for the full license text.
# =============================================================================
#
# Costruisce dist/netinst.img: il floppy della RETE.
#
#     tools/mknetinstimg.sh          (oppure: make netinst-img)
#
# ! NON E' AVVIABILE, ED E' VOLUTO. Il floppy di avvio resta quello minimale,
# che e' collaudato e pieno: su 1406 KB utili ne restano ventisei liberi, e i
# driver di rete piu' netupdate ne vogliono trecentosessanta. Farceli stare
# vorrebbe dire togliere qualcosa da li' — cioe' peggiorare il supporto che
# serve a installare il sistema, per aggiungere roba che serve DOPO averlo
# installato.
#
# LA STRADA E' QUESTA:
#
#   1. si installa il sistema di base dal floppy minimale (o dal CD);
#   2. si avvia il sistema installato;
#   3. si monta questo floppy:      mount fd0 /mnt
#   4. si lancia lo script che porta tutto a destinazione e accende la rete:
#                                   /mnt/netinst.sh
#   5. e da li' netupdate lavora:   netupdate -repo:<indirizzo>
#                                   netupdate -check
#
# ! I FILE VANNO COPIATI, NON USATI DOVE SONO, e la ragione e' in lib/rete.c:
# la tabella che associa una scheda PCI al suo driver contiene percorsi
# ASSOLUTI — «/dev/e1000.drv» — perche' quella tabella la legge netdetect per
# fare uno spawn. Un driver lasciato su /mnt non lo troverebbe nessuno. Percio'
# l'immagine ha la stessa forma della destinazione (/dev, /bin, /exwin/lib) e
# netinst.sh non fa che ricopiarla al suo posto: una copia che si legge in
# quattro righe e non ha casi particolari.
#
# ! E exhttp.so DEVE ESSERCI. netupdate parla HTTP tramite la libreria
# condivisa /exwin/lib/exhttp.so, che il sistema minimale non ha (exwin non e'
# nel minimale). Senza, netupdate muore dicendo «non trovo la libreria
# condivisa della rete» — un errore chiaro, ma che si evita mettendocela.
# =============================================================================

set -e

RADICE=$(cd "$(dirname "$0")/.." && pwd)
cd "$RADICE"

IMG="dist/netinst.img"
FLOPPY_SIZE=1474560

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
log_info() { echo -e "${BLUE}[INFO]${NC}  $1"; }
log_ok()   { echo -e "${GREEN}[OK]${NC}    $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC}  $1"; }
log_err()  { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

command -v mformat >/dev/null || log_err "manca mformat: sudo apt install mtools"
command -v mcopy   >/dev/null || log_err "manca mcopy: sudo apt install mtools"

# --- Che cosa ci va dentro ---------------------------------------------------
#
# I driver: le tre schede che EX-OS riconosce, lo stack IP, e il bus PCI.
#
# ! pci.drv C'E', E LA PRIMA VERSIONE DI QUESTO SCRIPT NON CE L'AVEVA. Il
# ragionamento era «sta gia' nel sistema minimale (dev/pci.drv in
# minimale.txt), una seconda copia un giorno diverge». Sbagliato, e lo dice la
# prova dell'8 settembre 2026 su un sistema appena installato:
#
#     /mnt/netinst.sh
#     ...
#     /dev/pci.drv &
#     exec: comando non trovato: /dev/pci.drv
#
# minimale.txt e' l'elenco di cio' che sta sul FLOPPY, non di cio' che
# `install` copia sul disco. Senza il bus PCI netdetect non trova la scheda, e
# la catena si ferma al primo anello — con un messaggio giusto che pero' manda
# a cercare il guasto nel driver di rete.
DRIVER="e1000.drv ne2k.drv pcnet.drv ip.drv pci.drv"

# I programmi: quelli che servono ad accendere la rete, a guardarla quando non
# va, e ad aggiornarsi.
#   netdetect  sceglie il driver giusto per la scheda che c'e'
#   dhcp       un indirizzo, se c'e' un server
#   ipcfg      cosa ho, e i contatori quando qualcosa non torna
#   ping/host  la rete c'e'? e il nome si risolve?
#   nettest    i contatori della SCHEDA: persi in coda, persi dalla scheda
#   scarica    un URL e un file: prova l'HTTP senza tirare in ballo netupdate
#   netupdate  quello per cui esiste questo floppy
PROGRAMMI="netdetect dhcp ipcfg ping host nettest scarica netupdate"

# La libreria condivisa della rete, che il minimale non ha.
LIBRERIE="exwin/lib/exhttp.so"

log_info "Il floppy della rete: $IMG"

# --- I file ci sono tutti? ---------------------------------------------------
manca=0
# ! I DRIVER STANNO IN DUE POSTI, e va saputo: quelli che esistono solo sul CD
# in build/drivers-cd, quelli che stanno anche sul floppy in build/drivers.
# pci.drv e' del secondo gruppo, e cercarlo solo nel primo lo faceva sembrare
# non compilato.
drv_dove() {
    if [ -f "build/drivers-cd/$1" ]; then echo "build/drivers-cd/$1"
    elif [ -f "build/drivers/$1" ];  then echo "build/drivers/$1"
    else echo ""; fi
}
for d in $DRIVER; do
    [ -n "$(drv_dove "$d")" ] || { log_warn "manca il driver $d"; manca=1; }
done
for p in $PROGRAMMI; do
    [ -f "build/bin-cd/$p" ] || [ -f "build/bin/$p" ] || {
        log_warn "manca $p (ne' in build/bin-cd ne' in build/bin)"; manca=1; }
done
[ -f "build/exwin/lib/exhttp.so" ] || { log_warn "manca build/exwin/lib/exhttp.so"; manca=1; }
[ $manca -eq 1 ] && log_err "compila prima:  make -j2 iso-exos"

# --- Il conto, PRIMA di formattare ------------------------------------------
#
# ! LO SPAZIO SI CONTA PRIMA. mcopy che finisce il posto a meta' lascia
# un'immagine a meta' e un codice d'uscita che si perde in mezzo agli altri:
# meglio saperlo adesso e dire quanto manca.
UTILE=1457664          # 1440 KB meno le strutture FAT12 e la root directory
TOTALE=0
somma() { TOTALE=$((TOTALE + $(stat -c%s "$1"))); }
for d in $DRIVER;    do somma "$(drv_dove "$d")"; done
for p in $PROGRAMMI; do
    if [ -f "build/bin-cd/$p" ]; then somma "build/bin-cd/$p"; else somma "build/bin/$p"; fi
done
somma "build/exwin/lib/exhttp.so"

log_info "contenuto: $TOTALE byte ($((TOTALE / 1024)) KB) su $((UTILE / 1024)) KB utili"
[ $TOTALE -gt $UTILE ] && log_err "non ci sta: togli qualcosa da PROGRAMMI"

# --- L'immagine --------------------------------------------------------------
mkdir -p dist
rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=512 count=$((FLOPPY_SIZE / 512)) status=none

# ! NIENTE SETTORE DI AVVIO: questo floppy non parte, e non deve sembrare che
# possa. Un'immagine con un boot sector che poi non avvia e' peggio di una che
# il BIOS scarta subito, perche' manda a cercare il guasto nel caricatore.
mformat -f 1440 -v NETINST -i "$IMG" ::

mmd -i "$IMG" ::/dev ::/bin ::/exwin ::/exwin/lib

for d in $DRIVER;    do mcopy -i "$IMG" "$(drv_dove "$d")" "::/dev/$d"; done
for p in $PROGRAMMI; do
    if [ -f "build/bin-cd/$p" ]; then mcopy -i "$IMG" "build/bin-cd/$p" "::/bin/$p"
    else                              mcopy -i "$IMG" "build/bin/$p"    "::/bin/$p"; fi
done
mcopy -i "$IMG" "build/exwin/lib/exhttp.so" "::/exwin/lib/exhttp.so"

# --- Lo script che fa il lavoro ----------------------------------------------
TMP=$(mktemp); trap 'rm -f "$TMP" "$TMP.txt"' EXIT

cat > "$TMP" <<'EOF'
# netinst.sh - porta la rete su un sistema gia' installato.
#
# ! LE COPIE SONO CON -y, E NON E' PIGRIZIA. Senza, alla seconda esecuzione cp
# chiede «esiste gia', sovrascrivere?» e RESTA LI' ad aspettare una risposta:
# uno script che si puo' lanciare una volta sola e' uno script che non si puo'
# rilanciare quando qualcosa e' andato storto — cioe' proprio quando serve.
# Provato l'8 settembre 2026: la seconda prova si e' fermata sulla prima copia.
#
# Si lancia dopo aver montato questo floppy:
#     mount fd0 /mnt
#     /mnt/netinst.sh
#
# Copia i driver in /dev, i programmi in /bin, exhttp.so in /exwin/lib, e
# accende la rete. I percorsi di destinazione non sono una scelta: la tabella
# che associa una scheda al suo driver (lib/rete.c) li tiene ASSOLUTI, e la
# libreria della rete si cerca in /exwin/lib.
echo Copio i driver di rete...
cp -y /mnt/dev/e1000.drv /dev/e1000.drv
cp -y /mnt/dev/ne2k.drv /dev/ne2k.drv
cp -y /mnt/dev/pcnet.drv /dev/pcnet.drv
cp -y /mnt/dev/ip.drv /dev/ip.drv
# Anche il bus PCI: su un sistema appena installato non c'e', e senza di lui
# netdetect non trova nessuna scheda.
cp -y /mnt/dev/pci.drv /dev/pci.drv

echo Copio i programmi...
cp -y /mnt/bin/netdetect /bin/netdetect
cp -y /mnt/bin/dhcp /bin/dhcp
cp -y /mnt/bin/ipcfg /bin/ipcfg
cp -y /mnt/bin/ping /bin/ping
cp -y /mnt/bin/host /bin/host
cp -y /mnt/bin/nettest /bin/nettest
cp -y /mnt/bin/scarica /bin/scarica
cp -y /mnt/bin/netupdate /bin/netupdate

echo Copio la libreria della rete...
mkdir /exwin
mkdir /exwin/lib
cp -y /mnt/exwin/lib/exhttp.so /exwin/lib/exhttp.so

echo Accendo la rete...
/dev/pci.drv &
netdetect -c
/dev/ip.drv &
dhcp

echo
# ! NIENTE APOSTROFI FUORI DALLE VIRGOLETTE: la shell di EX-OS legge l'apice
# come inizio di stringa e risponde «manca la apice di chiusura». Provato l'8
# settembre 2026 con «La rete e' pronta»: la riga non usciva.
echo "La rete e' pronta. Adesso:"
echo "  netupdate -repo:<indirizzo>   aggiunge il repository"
echo "  netupdate -check              guarda cosa e' cambiato"
EOF
mcopy -i "$IMG" "$TMP" ::/netinst.sh

cat > "$TMP.txt" <<'EOF'
IL FLOPPY DELLA RETE DI EX-OS
=============================

Questo floppy NON SI AVVIA: non ha kernel ne' caricatore. Serve dopo, su un
sistema gia' installato, per portargli la rete e netupdate.

COME SI USA

  1. installa il sistema di base dal floppy minimale (o dal CD)
  2. avvia il sistema installato
  3. mount fd0 /mnt
  4. /mnt/netinst.sh          copia tutto al suo posto e accende la rete
  5. netupdate -repo:<indirizzo>
     netupdate -check

CHE COSA C'E' DENTRO

  /dev/e1000.drv   Intel 82540EM / 82545EM / 82574L
  /dev/ne2k.drv    NE2000 e compatibili
  /dev/pcnet.drv   AMD PCnet
  /dev/ip.drv      lo stack IP: ARP, ICMP, UDP, TCP
  /dev/pci.drv     il bus PCI, senza il quale non si trova nessuna scheda
  /bin/netdetect   guarda che scheda c'e' e avvia il driver giusto
  /bin/dhcp        un indirizzo dal server, se c'e'
  /bin/ipcfg       la configurazione e i contatori dello stack
  /bin/ping        la rete risponde?
  /bin/host        il nome si risolve?
  /bin/nettest     i contatori della scheda: dove si perdono i pacchetti
  /bin/scarica     un URL e un file, per provare l'HTTP da solo
  /bin/netupdate   gli aggiornamenti dalla rete
  /exwin/lib/exhttp.so   la libreria HTTP e TLS che netupdate carica

PERCHE' I FILE SI COPIANO INVECE DI GIRARE DA QUI

La tabella che associa una scheda PCI al suo driver tiene percorsi assoluti
(/dev/e1000.drv): un driver lasciato su /mnt non lo troverebbe nessuno. E la
libreria della rete si cerca in /exwin/lib. La forma di questo floppy e' la
stessa della destinazione apposta: netinst.sh non fa che ricopiarla al suo
posto.

PERCHE' NON E' AVVIABILE

Il floppy di avvio e' quello minimale, ed e' pieno: su 1406 KB utili ne restano
ventisei liberi. Questa roba ne vuole trecentosessanta. Metterla li' vorrebbe
dire togliere qualcosa dal supporto che serve a INSTALLARE il sistema, per
aggiungere roba che serve DOPO averlo installato.
EOF
mcopy -i "$IMG" "$TMP.txt" ::/LEGGIMI.TXT

# --- Cosa c'e' dentro, letto DALL'IMMAGINE ----------------------------------
# ! SI RILEGGE L'IMMAGINE, non si ristampa la lista di sopra: quello che conta
# e' cosa c'e' finito davvero.
echo
mdir -i "$IMG" -b -/ :: 2>/dev/null | grep -v '/$' | sed 's|^::/|  |'
echo
LIBERI=$(mdir -i "$IMG" :: 2>/dev/null | tail -1 | tr -d ' ' | sed 's/bytesfree//' | tr -d '\240')
log_ok "$IMG pronto ($(mdir -i "$IMG" -b -/ :: 2>/dev/null | grep -cv '/$') file)"
mdir -i "$IMG" :: 2>/dev/null | tail -2
echo
echo "Su EX-OS, dopo aver installato e avviato il sistema:"
echo "    mount fd0 /mnt"
echo "    /mnt/netinst.sh"
