#!/bin/sh
# =============================================================================
# tools/mkusb.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# SCRIVE SU UNA CHIAVETTA USB un'immagine di avvio GIA' FATTA, con i controlli
# sul dispositivo e la conferma battuta a mano.
#
#     sudo make usb   DISPOSITIVO=/dev/sdX      (dist/exos-usb.img)
#     sudo make usb64 DISPOSITIVO=/dev/sdX      (dist/exos64-usb.img)
#     EXOS_USB_IMMAGINE=<img> tools/mkusb.sh /dev/sdX
#
# ! LA REGOLA DELL'UTENTE (14 ottobre 2026): OGNI chiavetta e' un dischetto
# caricato TUTTO IN RAM da Stage 2 (RAMDISCO=1), e dopo l'avvio dall'USB non
# si legge piu' niente. E' l'unica forma che parte sul PC vero. Prima di
# scrivere, tools/verifica-chiave.sh apre l'immagine e lo controlla: misura
# da dischetto, settore di avvio, Stage 2 con RAMDISCO=1. Altrimenti no.
#
# ! LA VECCHIA CHIAVETTA - un disco ext2 installato in QEMU e letto dall'USB
# dopo l'avvio - QUI DENTRO NON C'E' PIU' (tolta il 14 ottobre 2026): sul PC
# vero non partiva, e restando nello script la si poteva rifare per sbaglio.
# Le note qui sotto sul perche' formattare da EX-OS valgono ancora per
# tools/mkhd.sh, che fa i dischi di prova.
#
# ! LA CHIAVETTA VIENE FORMATTATA. Quel che c'e' sopra si perde: partizione,
#   filesystem, file. Non esiste un modo gentile di farlo — serve una tabella
#   delle partizioni nostra, un settore di avvio nostro e un ext2 nostro —
#   quindi la sola cosa che si puo' fare e' non farlo mai per sbaglio, ed e'
#   quello che fa la meta' di questo script.
#
# -----------------------------------------------------------------------------
# PERCHE' ESISTE
#
# Gli strumenti stanno sul CD, che e' in SOLA LETTURA: per compilare dentro
# EX-OS bisogna comunque procurarsi altrove un ext2 dove scrivere, e la ricetta
# per farlo e' un compito a se' (in_lavorazione.txt, @PROVE-STRUMENTI, e costa
# mezz'ora ogni volta che la si ritrova). Su una chiavetta il compilatore e la
# directory di lavoro stanno sullo stesso volume, e la ricetta sparisce. E'
# anche l'unico modo di far girare EX-OS su una macchina vera senza toccarne il
# disco.
#
# -----------------------------------------------------------------------------
# ! SI COSTRUISCE UN'IMMAGINE, E SOLO ALLA FINE SI SCRIVE SUL DISPOSITIVO
#
# QEMU non tocca mai la chiavetta. Formattare e installare vogliono decine di
# comandi dentro EX-OS, e ognuno e' un'occasione di sbagliare bersaglio: farli
# su un file dentro dischi/ vuol dire che l'unico momento pericoloso e' UNO, e'
# alla fine, ed e' una riga sola (`dd`). Se qualcosa va storto prima, la
# chiavetta non e' stata nemmeno aperta.
#
# ! E LA FORMATTAZIONE LA FA EX-OS, NON LINUX — la ragione e' scritta per
# esteso in tools/mkhd.sh e vale identica qui: un ext2 fatto da `mke2fs` porta
# di serie estensioni che il driver di EX-OS rifiuta, e ogni versione di
# e2fsprogs ne accende di nuove. E `install` non copia solo file: scrive l'MBR,
# il settore di avvio della partizione e la MAPPA DEI SETTORI del kernel, che
# su ext2 non e' contiguo. Rifare quel calcolo da Linux vorrebbe dire due
# implementazioni dello stesso formato, e la seconda sbaglierebbe in silenzio.
# =============================================================================

set -e

RADICE=$(cd "$(dirname "$0")/.." && pwd)
cd "$RADICE"

DISPOSITIVO="$1"
MB="${2:-1024}"
IMG="dischi/usb-${MB}.img"

# ! LA VERSIONE A 64 BIT (make usb64, cioe' EXOS_ARCH=x86_64): stessa strada,
# stesse protezioni. Cambiano il CD da cui si installa (dist/exos64.iso),
# l'emulatore con cui lo si fa, e il terzo passo: gli strumenti di sviluppo a
# 64 bit non esistono ancora (make tools64), quindi sulla chiavetta va il
# sistema e basta. Niente dischetto: a 64 bit non c'e'.
ARCH64=0
[ "${EXOS_ARCH:-i386}" = "x86_64" ] && ARCH64=1

# ! UN'IMMAGINE GIA' FATTA (EXOS_USB_IMMAGINE=<file>): lo script non costruisce
# niente e fa solo la sua seconda meta' - i controlli sul dispositivo, la
# conferma battuta a mano, la scrittura. La usa `make chiave64`, che l'immagine
# la costruisce dall'host con tools/mkchiave.sh: le protezioni restano queste,
# scritte una volta sola.
PRONTA="${EXOS_USB_IMMAGINE:-}"
if [ -z "$PRONTA" ]; then
    echo "mkusb: serve un'immagine gia' fatta (EXOS_USB_IMMAGINE=...)." >&2
    echo "       Si fanno con  make usb  (32 bit) o  make usb64  (64 bit): un dischetto" >&2
    echo "       da 2,88 MB caricato tutto in RAM. Per scriverla:" >&2
    echo "         sudo make usb64 DISPOSITIVO=/dev/sdX" >&2
    exit 1
fi
[ -f "$PRONTA" ] || { echo "mkusb: manca l'immagine $PRONTA" >&2; exit 1; }
"$RADICE/tools/verifica-chiave.sh" "$PRONTA" || exit 1
MB=$(( ($(stat -c%s "$PRONTA") + 1048575) / 1048576 ))

CD_SISTEMA=dist/exos.iso
CD_STRUMENTI=dist/exos-tools.iso
FLOPPY=dist/floppy.img          # serve al secondo giro, per gli strumenti
if [ $ARCH64 -eq 1 ]; then
    IMG="dischi/usb64-${MB}.img"
    CD_SISTEMA=dist/exos64.iso
    export EXOS_QEMU=qemu-system-x86_64
    export EXOS_RAM="${EXOS_RAM:-256M}"
    CPU64="-cpu qemu64 "
else
    CPU64=""
fi

rifiuta() {
    echo "" >&2
    echo "mkusb: $1" >&2
    shift
    for r in "$@"; do echo "       $r" >&2; done
    echo "" >&2
    exit 1
}

# =============================================================================
# 1. I RIFIUTI, PRIMA DI QUALUNQUE ALTRA COSA
#
# ! SI CONTROLLA PRIMA DI COSTRUIRE, non prima di scrivere. Un'ora di
# costruzione che finisce con «questo dispositivo non va bene» e' un'ora
# buttata, e la volta dopo si e' tentati di saltare il controllo.
# =============================================================================

[ -n "$DISPOSITIVO" ] || rifiuta \
    "manca il dispositivo." \
    "uso:  make usb DISPOSITIVO=/dev/sdX [MB=1024]" \
    "" \
    "! IL DISPOSITIVO NON SI CERCA DA SOLO, e non e' pigrizia: un programma" \
    "  che indovina su quale disco scrivere e' un programma che prima o poi" \
    "  indovina male, e quella volta non c'e' modo di rimediare." \
    "" \
    "  Per vedere cosa c'e' attaccato:   lsblk -d -o NAME,SIZE,RM,MODEL"

[ -b "$DISPOSITIVO" ] || rifiuta \
    "$DISPOSITIVO non e' un dispositivo a blocchi." \
    "Serve il DISCO, per esempio /dev/sdb."

NOME=$(basename "$DISPOSITIVO")

# Una partizione ha il file 'partition' dentro /sys/class/block/<nome>.
[ ! -e "/sys/class/block/$NOME/partition" ] || rifiuta \
    "$DISPOSITIVO e' una PARTIZIONE, non un disco." \
    "La tabella delle partizioni e il settore di avvio si scrivono sul disco:" \
    "usa /dev/${NOME%%[0-9]*} invece di /dev/$NOME."

[ -d "/sys/block/$NOME" ] || rifiuta \
    "non trovo /sys/block/$NOME." \
    "$DISPOSITIVO non sembra un disco intero."

RIMOVIBILE=$(cat "/sys/block/$NOME/removable" 2>/dev/null || echo 0)
if [ "$RIMOVIBILE" != "1" ]; then
    if [ "${EXOS_USB_FORZA:-0}" = "1" ]; then
        echo "mkusb: $DISPOSITIVO NON e' rimovibile, e si procede lo stesso"
        echo "       perche' EXOS_USB_FORZA=1. Spero tu sappia cos'e'."
    else
        rifiuta \
            "$DISPOSITIVO non e' un dispositivo rimovibile." \
            "Il caso normale deve essere impossibile: un disco fisso e' quasi" \
            "sempre il disco di qualcuno." \
            "" \
            "Se sai davvero cosa stai facendo:  EXOS_USB_FORZA=1 make usb ..."
    fi
fi

# Montata la chiavetta, o una sua partizione? Si guarda /proc/mounts e non
# `mount`, che su alcune macchine e' un wrapper che non elenca tutto.
MONTATE=$(awk -v d="$DISPOSITIVO" '$1 == d || index($1, d) == 1 {print $1" su "$2}' \
          /proc/mounts 2>/dev/null || true)
[ -z "$MONTATE" ] || rifiuta \
    "$DISPOSITIVO e' MONTATO in questo momento:" \
    "$MONTATE" \
    "" \
    "Smontalo prima:  umount ${DISPOSITIVO}*"

# ! E CI SI DEVE POTER SCRIVERE, e va chiesto ADESSO. Scrivere su un disco
# vuole i permessi di root: senza questo controllo, il `dd` in fondo fallirebbe
# dopo mezz'ora di costruzione con un «Permission denied», e la mezz'ora
# sarebbe buttata. E' la stessa ragione per cui tutti i rifiuti stanno qui in
# cima e non piu' avanti.
[ -w "$DISPOSITIVO" ] || rifiuta \
    "non ho il permesso di scrivere su $DISPOSITIVO." \
    "Scrivere su un disco vuole root:" \
    "" \
    "    sudo make usb DISPOSITIVO=$DISPOSITIVO" \
    "" \
    "(la costruzione non ne avrebbe bisogno, ma il controllo si fa adesso:" \
    " accorgersene dopo vorrebbe dire buttare via tutto il lavoro fatto.)"

# E, per scrupolo, non dev'essere il disco su cui sta questo repository.
MIO=$(df --output=source . 2>/dev/null | tail -1)
case "$MIO" in
    "$DISPOSITIVO"*) rifiuta \
        "$DISPOSITIVO e' il disco su cui sta questo repository ($MIO)." \
        "No." ;;
esac

# =============================================================================
# 2. COSA SI STA PER CANCELLARE, DETTO PRIMA
# =============================================================================

MODELLO=$(cat "/sys/block/$NOME/device/model" 2>/dev/null || echo "?")
BYTE=$(( $(cat "/sys/block/$NOME/size") * 512 ))
GB=$(( BYTE / 1000000000 ))

echo ""
echo "============================================================"
echo " QUESTO CANCELLA TUTTO QUEL CHE C'E' SU $DISPOSITIVO"
echo "============================================================"
echo ""
printf "  dispositivo   %s\n" "$DISPOSITIVO"
printf "  modello       %s\n" "$MODELLO"
printf "  dimensione    %s GB (%s byte)\n" "$GB" "$BYTE"
echo ""
echo "  adesso ci sta sopra:"
# ! ANCHE QUANDO NON C'E' NIENTE VA DETTO. Una sezione vuota si legge come «il
# comando non ha funzionato», e chi sta per battere «confirm» merita una frase
# invece di uno spazio bianco.
SOPRA=$(lsblk -o NAME,SIZE,FSTYPE,LABEL,MOUNTPOINT "$DISPOSITIVO" 2>/dev/null || true)
if [ -n "$SOPRA" ]; then
    echo "$SOPRA" | sed 's/^/    /'
else
    echo "    (nessuna partizione riconosciuta — la chiavetta e' vuota o e'"
    echo "     formattata in un modo che lsblk non legge: si cancella lo stesso)"
fi
echo ""
echo "  tutto questo sara' PERSO: partizioni, filesystem, file."
echo ""

if [ "$BYTE" -lt $(( MB * 1024 * 1024 )) ]; then
    rifiuta "la chiavetta e' piu' piccola dell'immagine da scrivere (${MB} MB)." \
            "Usa MB= piu' piccolo, o una chiavetta piu' grande."
fi

# =============================================================================
# 3. LA CONFERMA SI SCRIVE, NON SI PREME
#
# ! UN [s/n] SI RISPONDE COL DITO PRIMA CHE CON LA TESTA. Chi ha appena battuto
# tre comandi di fila batte anche il quarto, e qui il costo di sbagliare non e'
# una costruzione da rifare: e' il disco di qualcuno. Una parola da COPIARE
# obbliga a leggere la riga in cui e' scritta, che e' proprio la riga dove c'e'
# il nome del dispositivo.
#
# ! E SI LEGGE DA UN TERMINALE. Una conferma che si puo' mandare da una pipe
# non e' una conferma: e' un parametro, e domani finisce dentro uno script che
# la scrive da sola. Se stdin non e' un terminale, non si procede.
# =============================================================================

[ -t 0 ] || rifiuta \
    "la conferma va battuta a mano, e stdin non e' un terminale." \
    "Questo comando non si mette dentro uno script: e' l'unico punto in cui" \
    "qualcuno guarda il nome del dispositivo prima che sia troppo tardi."

printf "Per procedere scrivi  confirm  e batti Invio: "
read -r RISPOSTA || RISPOSTA=""

if [ "$RISPOSTA" != "confirm" ]; then
    echo ""
    echo "mkusb: non hai scritto «confirm». Non e' stato toccato niente."
    exit 1
fi

echo ""
echo "Va bene. La chiavetta verra' scritta ALLA FINE, dopo la costruzione."
echo ""

# =============================================================================
# 4. L'IMMAGINE
# =============================================================================

IMG="$PRONTA"
echo "=== immagine: $IMG (${MB} MB) ==="

# =============================================================================
# 5. E ADESSO, IL MOMENTO PERICOLOSO: UNA RIGA SOLA
# =============================================================================

echo ""
echo "=== 4/4  scrittura su $DISPOSITIVO ==="
echo "     (l'unico momento in cui la chiavetta viene toccata)"

dd if="$IMG" of="$DISPOSITIVO" bs=4M conv=fsync status=progress
sync

echo ""
echo "[OK] $DISPOSITIVO e' pronto."
echo ""
echo "     All'avvio il dischetto si copia tutto in RAM: dopo, la chiavetta si puo'"
echo "     anche togliere. Lo spazio oltre i primi 2,88 MB non si usa."
