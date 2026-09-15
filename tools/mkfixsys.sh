#!/bin/bash
# =============================================================================
# tools/mkfixsys.sh — costruisce dist/fixsys.img, il disco di SOCCORSO
#
#     tools/mkfixsys.sh
#
# Un floppy avviabile con dentro il minimo per rimettere in sesto un sistema
# installato: la libc, la shell, `install`, e i due comandi per montare il
# disco. Si avvia da qui, si ripara il disco, si toglie il dischetto.
#
# -----------------------------------------------------------------------------
# ! A CHE COSA SERVE, E PERCHE' NON BASTA IL FLOPPY NORMALE
#
# I programmi di EX-OS chiamano la libc condivisa PER NOME, e i nomi si
# risolvono all'avvio del programma. Da qui un'asimmetria che decide tutto:
#
#     binario VECCHIO + libc NUOVA   ->  funziona: i nomi vecchi ci sono ancora
#     binario NUOVO   + libc VECCHIA ->  non parte NIENTE: ne manca uno
#
# Un aggiornamento che si interrompe dopo i programmi e prima della libc lascia
# la macchina nel secondo caso: si accende, ha la rete a posto, e non ha un solo
# comando con cui rimediare — `scarica`, `dhcp`, `ipcfg`, `netupdate` rispondono
# tutti «la libreria condivisa non ha la funzione ...». E' successo il 15
# settembre 2026 su una macchina vera, e questo dischetto e' la risposta.
#
# Il floppy normale saprebbe fare lo stesso lavoro, ma porta quaranta programmi
# e resta con undicimila byte liberi: e' il supporto di INSTALLAZIONE, pieno per
# mestiere. Questo porta sei file e si legge in un colpo d'occhio — su un disco
# di soccorso, meno cose ci sono, meno ce ne sono da sbagliare.
#
# ! E NON C'E' NESSUN PROGRAMMA NUOVO QUI DENTRO. `install -a` fa gia' la cosa
# giusta: confronta il supporto col disco, ELENCA che cosa cambierebbe, e copia
# solo quello. Il dischetto non aggiunge codice, aggiunge un posto da cui farlo
# partire quando sul disco non parte piu' niente.
# =============================================================================
set -e

IMG="dist/fixsys.img"
BOOT_SECTOR="build/stage1.bin"
LOADER="build/stage2.bin"
KERNEL="build/kernel.bin"
FLOPPY_SECTORS=2880          # 1.44 MB

# ! QUESTO E' L'ELENCO, ED E' CORTO APPOSTA.
#
#   sh       senza una shell non si batte niente
#   install  il riparatore vero: -a confronta, elenca e copia
#   mount    (e umount, che e' lo stesso binario: guarda argv[0])
#   disk     per vedere come si chiama la partizione, se non e' hd0p1
#   ls       per guardare che cosa c'e' prima e dopo
#
# Tutto il resto starebbe pure — restano piu' di ottocento kilobyte liberi — ma
# ogni file in piu' e' un file che `install -a` copierebbe sul disco, e qui si
# vuole toccare il minimo indispensabile: la libc e i comandi di base.
PROGRAMMI="sh install mount disk ls cp"

# ! I DRIVER NON SONO UN DI PIU': SENZA kbd.drv NON SI BATTE NIENTE.
#
# Il kernel carica come processi i moduli elencati in /boot/kernel.cfg, e la
# tastiera e' uno di quelli: la disciplina di linea — quella che raccoglie i
# caratteri e consegna la RIGA quando si batte invio — vive li' dentro. Senza
# quel file il kernel dice «driver kbd non caricato» e la shell accetta i
# caratteri senza eseguire mai niente. Visto su una macchina vera il 15
# settembre 2026, ed e' il genere di guasto che non somiglia alla sua causa.
#
# pci e uhci servono alle macchine senza 8042, dove la tastiera e' USB: senza
# di loro non ci sarebbe modo di battere il comando che le farebbe funzionare.
DRIVER="kbd pci uhci"

VERDE="\033[0;32m"; GIALLO="\033[1;33m"; ROSSO="\033[0;31m"; BLU="\033[0;34m"; N="\033[0m"
info() { echo -e "${BLU}[INFO]${N}  $1"; }
ok()   { echo -e "${VERDE}[OK]${N}    $1"; }
avv()  { echo -e "${GIALLO}[WARN]${N}  $1"; }
err()  { echo -e "${ROSSO}[ERRORE]${N} $1"; exit 1; }

cd "$(dirname "$0")/.."

for f in "$BOOT_SECTOR" "$LOADER" "$KERNEL" build/lib/libc.so; do
    [ -f "$f" ] || err "manca $f: prima `make`"
done
for p in $PROGRAMMI; do
    [ -x "build/bin/$p" ] || err "manca build/bin/$p"
done

mkdir -p dist

info "Creazione immagine vuota (1.44 MB)..."
dd if=/dev/zero of="$IMG" bs=512 count=$FLOPPY_SECTORS status=none

info "Formattazione FAT12..."
mformat -f 1440 -v FIXSYS -i "$IMG" ::

# ! IL SETTORE 0 SI RISCRIVE PER INTERO, BPB COMPRESO. Il nostro stage1 ha
# dentro un BPB identico a quello che mformat scrive per un 1.44 MB: e' la
# stessa strada di mkfloppy.sh, e la firma si verifica subito dopo perche' un
# dischetto che non si avvia e' peggio di nessun dischetto.
info "Installazione del settore di avvio..."
dd if="$BOOT_SECTOR" of="$IMG" bs=512 count=1 conv=notrunc status=none

FIRMA=$(dd if="$IMG" bs=2 skip=255 count=1 status=none | od -A n -t x2 | tr -d " \n")
[ "$FIRMA" = "aa55" ] || err "firma del settore di avvio mancante (trovato $FIRMA)"
ok "Settore di avvio installato e verificato"

mmd -i "$IMG" ::/boot ::/bin ::/lib ::/dev

mcopy -i "$IMG" "$LOADER" ::/LOADER.BIN
mcopy -i "$IMG" "$KERNEL" ::/KERNEL.BIN
ok "Avvio: LOADER.BIN + KERNEL.BIN"

mcopy -i "$IMG" build/lib/libc.so ::/lib/libc.so
ok "La libc: $(stat -c %s build/lib/libc.so) byte"

for p in $PROGRAMMI; do
    mcopy -i "$IMG" "build/bin/$p" "::/bin/$p"
    ok "  $p"
done
# umount e' lo stesso binario di mount: il programma guarda argv[0].
mcopy -i "$IMG" build/bin/mount ::/bin/umount
ok "  umount (stesso binario di mount)"

for d in $DRIVER; do
    if [ -f "build/drivers/$d.drv" ]; then
        mcopy -i "$IMG" "build/drivers/$d.drv" "::/dev/$d.drv"
        ok "  $d.drv"
    elif [ -f "build/drivers-cd/$d.drv" ]; then
        mcopy -i "$IMG" "build/drivers-cd/$d.drv" "::/dev/$d.drv"
        ok "  $d.drv (da drivers-cd)"
    else
        err "manca $d.drv: senza, dal dischetto non si batte niente"
    fi
done

# ! LA CONFIGURAZIONE E' QUELLA DEL SOCCORSO, non quella di sistema: nomina
# solo i moduli che stanno davvero qui dentro, e manda diritti alla shell senza
# chiedere un accesso che sta su un disco rotto. Vedi tools/fixsys-kernel.cfg.
mcopy -i "$IMG" tools/fixsys-kernel.cfg ::/boot/KERNEL.CFG
ok "  kernel.cfg del soccorso"

[ -f boot/fixsys.sh ] && mcopy -i "$IMG" boot/fixsys.sh ::/boot/FIXSYS.SH

# ! L'AUTOEXEC DI QUESTO DISCHETTO NON E' QUELLO DEL SISTEMA. Chi avvia un
# disco di soccorso ha gia' un guaio fra le mani: la prima cosa che deve
# vedere e' che cosa battere, non «Sistema pronto».
mcopy -i "$IMG" tools/fixsys-autoexec.sh ::/boot/AUTOEXEC.SH
ok "  autoexec del soccorso"

echo
info "Contenuto:"
mdir -i "$IMG" ::/bin ::/lib ::/boot 2>/dev/null | grep -vE "^$|Volume|Serial" | sed 's/^/    /'
echo
LIBERI=$(mdir -i "$IMG" :: 2>/dev/null | grep -i "bytes free" | tr -dc '0-9')
ok "dist/fixsys.img pronto — ${LIBERI} byte liberi"
echo
echo "    Si avvia da questo dischetto e si batte:"
echo "        sh -f /boot/fixsys.sh"
echo
