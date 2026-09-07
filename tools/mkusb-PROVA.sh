#!/bin/sh
# =============================================================================
# tools/mkusb.sh
# EX-OS — Extensible Operating System
#
# Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Prepara una CHIAVETTA USB avviabile con sopra l'ambiente intero: il sistema,
# i programmi, i driver e gli strumenti di sviluppo (gcc, as, ld, make, nasm,
# le librerie).
#
#     make usb DISPOSITIVO=/dev/sdX [MB=1024]
#     tools/mkusb.sh /dev/sdX [MB]
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

FLOPPY=dist/floppy.img
CD_STRUMENTI=dist/exos-tools.iso

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

[ -e "$DISPOSITIVO" ] || rifiuta \
    "$DISPOSITIVO non e' un dispositivo a blocchi." \
    "Serve il DISCO, per esempio /dev/sdb."

NOME=$(basename "$DISPOSITIVO")

# Una partizione ha il file 'partition' dentro /tmp/claude-1000/-home-graziano-MEGA-Sviluppo-EXA-OS-exa-os-2026-07-16-225-exa-os/74e8873e-733c-4b71-be94-01553608a94f/scratchpad/finto/sys/class/block/<nome>.
[ ! -e "/tmp/claude-1000/-home-graziano-MEGA-Sviluppo-EXA-OS-exa-os-2026-07-16-225-exa-os/74e8873e-733c-4b71-be94-01553608a94f/scratchpad/finto/sys/class/block/$NOME/partition" ] || rifiuta \
    "$DISPOSITIVO e' una PARTIZIONE, non un disco." \
    "La tabella delle partizioni e il settore di avvio si scrivono sul disco:" \
    "usa /dev/${NOME%%[0-9]*} invece di /dev/$NOME."

[ -d "/tmp/claude-1000/-home-graziano-MEGA-Sviluppo-EXA-OS-exa-os-2026-07-16-225-exa-os/74e8873e-733c-4b71-be94-01553608a94f/scratchpad/finto/sys/block/$NOME" ] || rifiuta \
    "non trovo /tmp/claude-1000/-home-graziano-MEGA-Sviluppo-EXA-OS-exa-os-2026-07-16-225-exa-os/74e8873e-733c-4b71-be94-01553608a94f/scratchpad/finto/sys/block/$NOME." \
    "$DISPOSITIVO non sembra un disco intero."

RIMOVIBILE=$(cat "/tmp/claude-1000/-home-graziano-MEGA-Sviluppo-EXA-OS-exa-os-2026-07-16-225-exa-os/74e8873e-733c-4b71-be94-01553608a94f/scratchpad/finto/sys/block/$NOME/removable" 2>/dev/null || echo 0)
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

# Montata la chiavetta, o una sua partizione? Si guarda /tmp/claude-1000/-home-graziano-MEGA-Sviluppo-EXA-OS-exa-os-2026-07-16-225-exa-os/74e8873e-733c-4b71-be94-01553608a94f/scratchpad/finto/proc/mounts e non
# `mount`, che su alcune macchine e' un wrapper che non elenca tutto.
MONTATE=$(awk -v d="$DISPOSITIVO" '$1 == d || index($1, d) == 1 {print $1" su "$2}' \
          /tmp/claude-1000/-home-graziano-MEGA-Sviluppo-EXA-OS-exa-os-2026-07-16-225-exa-os/74e8873e-733c-4b71-be94-01553608a94f/scratchpad/finto/proc/mounts 2>/dev/null || true)
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

MODELLO=$(cat "/tmp/claude-1000/-home-graziano-MEGA-Sviluppo-EXA-OS-exa-os-2026-07-16-225-exa-os/74e8873e-733c-4b71-be94-01553608a94f/scratchpad/finto/sys/block/$NOME/device/model" 2>/dev/null || echo "?")
BYTE=$(( $(cat "/tmp/claude-1000/-home-graziano-MEGA-Sviluppo-EXA-OS-exa-os-2026-07-16-225-exa-os/74e8873e-733c-4b71-be94-01553608a94f/scratchpad/finto/sys/block/$NOME/size") * 512 ))
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

[ -f "$FLOPPY" ] || rifiuta "manca $FLOPPY." "Lancia prima 'make'."
[ -f "$CD_STRUMENTI" ] || rifiuta \
    "manca $CD_STRUMENTI." \
    "Lancia prima 'make iso' (e' il CD degli strumenti: gcc, as, ld, make)."

SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
[ -x "$SFDISK" ] || rifiuta "sfdisk non trovato (pacchetto util-linux)."

mkdir -p dischi
echo "=== 1/4  immagine vuota e tabella delle partizioni (${MB} MB) ==="
rm -f "$IMG"
qemu-img create -f raw "$IMG" "${MB}M" > /dev/null

# Una sola partizione primaria, tipo 83, attiva; inizio a 2048 come ogni
# strumento moderno (allinea a 1 MB, cioe' a qualunque dimensione di blocco
# fisico). Due partizioni servirebbero solo se un BIOS si rifiutasse di
# avviare da qui, e quello si scopre provando.
SETTORI=$(( MB * 1024 * 1024 / 512 - 2048 ))
printf 'label: dos\nunit: sectors\n\nstart=2048, size=%s, type=83, bootable\n' \
    "$SETTORI" | "$SFDISK" "$IMG" > /dev/null 2>&1
echo "[OK] hd0p1, tipo 83, attiva"

# --- il sistema -------------------------------------------------------------
#
# ! DUE GIRI DI QEMU E NON UNO, ed e' voluto. `install` guarda cosa trova sul
# supporto per proporre i componenti opzionali: con il CD degli strumenti gia'
# attaccato si troverebbe davanti un albero che non e' suo. Il sistema si
# installa dal floppy, gli strumenti dal loro CD, e ognuno vede solo il proprio.
# ! ALL'ULTIMA DOMANDA SI RISPONDE «no», E NON E' FRETTA. L'installatore
# chiede se lanciare `hwconfig`, che guarda l'hardware e riscrive kernel.cfg
# con i driver di QUESTA macchina. Su un disco fisso e' la cosa giusta; su una
# CHIAVETTA e' il contrario esatto, perche' la macchina che la costruisce e'
# QEMU e quella che la usera' e' un'altra. Scriverci dentro l'elenco dei driver
# di QEMU vorrebbe dire consegnare una chiavetta configurata per un computer
# che non esiste.
#
# Chi la usa lancia `hwconfig` al primo avvio sulla macchina vera: e' una riga,
# ed e' l'unico posto da cui si vede l'hardware giusto.
#
# ! E SI MANDA DUE VOLTE, e non e' superstizione. Fra l'ultima
# password e la domanda «lancio hwconfig?» ci sta la COPIA DI TUTTO IL SISTEMA,
# che dura quanto dura: qui i tempi sono sleep fissi (vedi qemu_drive.py), non
# attese di una scritta. La prima volta questa prova e' fallita proprio cosi' —
# il «si» battuto mentre la copia era ancora in corso, la domanda comparsa
# dopo, e la macchina rimasta ferma su di essa fino allo scadere del tempo, con
# un errore che accusava l'installatore.
#
# Due «no» a distanza coprono tutt'e due i casi: se il primo viene raccolto
# (type-ahead), il secondo finisce al prompt della shell e diventa un innocuo
# «comando non trovato»; se il primo si perde, il secondo arriva a domanda
# gia' fatta. E' brutto, ed e' meno brutto di un numero indovinato — che qui
# e' gia' stato sbagliato due volte, in due modi diversi.
#
# ! IL RIMEDIO VERO NON E' QUESTO: e' che `install` prenda le risposte da un
# file. Finche' non lo fa, ogni domanda nuova sfasa questa sequenza di uno —
# e' gia' successo tre volte a tools/mkhd.sh, che porta le cicatrici scritte.
UTENTE="${EXOS_UTENTE:-exos}"
PW_ROOT="${EXOS_PW_ROOT:-root}"
PW_UTENTE="${EXOS_PW_UTENTE:-exos}"
LINGUA="${EXOS_LINGUA:-1}"

echo ""
echo "=== 2/4  formattazione e sistema, DENTRO EX-OS (qualche minuto) ==="
EXOS_ISTANZA=usb1 \
EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide" \
    python3 tools/qemu_drive.py \
        "mkfs -t ext2 -L exos hd0p1@4" \
        "si@240" \
        "mount hd0p1 /disk@10" \
        "install -t /disk@10" \
        "$LINGUA@120" \
        "$PW_ROOT@2" "$PW_ROOT@3" \
        "$UTENTE@2" "$PW_UTENTE@2" "$PW_UTENTE@3" \
        "no@240" "no@60" \
    > /tmp/exos-mkusb-1.log 2>&1

grep -q "Installazione completata" /tmp/exos-mkusb-1.log || {
    echo "[ERRORE] il sistema non e' stato installato." >&2
    echo "         registro: /tmp/exos-mkusb-1.log" >&2
    tail -25 /tmp/exos-mkusb-1.log >&2
    exit 1
}
grep -E "kernel: .* intervall|stage2: LBA" /tmp/exos-mkusb-1.log | sed 's/^ */  /' || true
echo "[OK] sistema installato e avviabile"

# --- gli strumenti ----------------------------------------------------------
echo ""
echo "=== 3/4  strumenti di sviluppo dal CD (parecchi minuti) ==="
EXOS_ISTANZA=usb2 EXOS_CDROM="$CD_STRUMENTI" EXOS_RAM=256M \
EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide,index=0,media=disk" \
    python3 tools/qemu_drive.py \
        "mount hd0p1 /disco@12" \
        "toolinst -y /disco@1800" \
    > /tmp/exos-mkusb-2.log 2>&1

# ! LA RIGA DEL SUCCESSO E' «Fatto. Al prossimo avvio...», e la si e' letta
# nel sorgente di toolinst invece di indovinarla: un controllo che cerca una
# parola che il programma non stampa mai fallisce SEMPRE, e allora si finisce
# per toglierlo — cioe' per non controllare piu' niente.
# ! E SI GUARDA ANCHE SE CI SONO STATI ERRORI: toolinst arriva in fondo lo
# stesso e li conta, ma un compilatore installato a meta' non e' installato.
if grep -q "Finito con .* errori" /tmp/exos-mkusb-2.log; then
    echo "[ERRORE] toolinst e' arrivato in fondo ma con errori:" >&2
    grep -E "errori" /tmp/exos-mkusb-2.log | sed 's/^/         /' >&2
    echo "         registro: /tmp/exos-mkusb-2.log" >&2
    exit 1
fi
grep -q "Fatto\. Al prossimo avvio" /tmp/exos-mkusb-2.log || {
    echo "[ERRORE] gli strumenti non sono stati installati." >&2
    echo "         registro: /tmp/exos-mkusb-2.log" >&2
    tail -25 /tmp/exos-mkusb-2.log >&2
    exit 1
}
echo "[OK] strumenti installati in /exos"

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
echo "     Per entrare:  root / $PW_ROOT   oppure   $UTENTE / $PW_UTENTE"
echo "     Il compilatore e' in /exos/bin/gcc, ed e' gia' nel PATH."
echo ""
echo "! LA CHIAVETTA E' GRANDE $GB GB MA LA PARTIZIONE E' ${MB} MB: il resto"
echo "  non e' usato. Per prenderselo tutto serve `MB=` piu' grande, o far"
echo "  crescere la partizione a mano — che EX-OS oggi non sa fare."
