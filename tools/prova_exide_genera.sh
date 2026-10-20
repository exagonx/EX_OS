#!/bin/bash
# =============================================================================
# tools/prova_exide_genera.sh — quel che exide GENERA compila ed e' coerente
# (4 ottobre 2026, segnalato dall'utente: «header incoerenti, tipi mancanti,
# variabili sbagliate»)
#
# Un progetto con TUTTI i quindici controlli, una seconda maschera e un'icona
# si scrive sul disco come finestra.dis soltanto; exide lo apre e Ctrl+S
# genera finestra.h, finestra_gen.c e finestra.c. Poi, sull'host:
#
#   1. i tre file ci sono;
#   2. compilano senza un avviso con gli header di lib/exwin di OGGI, e anche
#      con EXWIN_SENZA_NOMI_ITALIANI (cioe' usano solo l'API inglese);
#   3. ogni variabile dichiarata in finestra.h e' definita in finestra_gen.c,
#      e ogni handler dichiarato e' definito in finestra.c: collegati insieme,
#      restano da risolvere solo le funzioni della libreria;
#   4. gli header che finiscono sul CD degli strumenti sono quelli di oggi: e'
#      con quelli che il gcc DENTRO EX-OS compila il progetto, e il 3 ottobre
#      2026 il CD era rimasto con l'exwin.h di prima della traduzione.
#
#     tools/prova_exide_genera.sh [directory-di-lavoro]  (default /tmp/exos-ide-genera)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-ide-genera}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
[ -x "$SFDISK" ]  || { echo "manca sfdisk (util-linux)" >&2; exit 1; }
[ -x "$DEBUGFS" ] || { echo "manca debugfs (e2fsprogs)" >&2; exit 1; }

export EXOS_ISTANZA=idegenera
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

echo "=== 1. il progetto: quindici controlli, due maschere, un'icona ==="
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 400 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" "mkdir /disk/nuovo@2" "mkdir /disk/nuovo/src@2" "mkdir /disk/casa@2" \
    > "$D/1-disco.log" 2>&1

mkdir -p "$D/prima"
cat > "$D/prima/finestra.dis" <<'EOF'
# exide 0.002 - lo scrive e lo legge solo exide
F principale 520 420 Prova di tutto
c button Button1 101 10 10 90 26 0 Premi
i /exwin/icon/baseapp/calctor_64.ico
c label Label1 102 110 14 90 16 1 Etichetta
c textbox TextBox1 103 10 44 140 22 1 testo
c checkbox CheckBox1 104 10 72 140 20 0 Una spunta
c radio Radio1 105 10 96 140 20 0 Un radio
c frame Frame1 106 160 44 160 90 0 Riquadro
c separator Line1 107 10 140 300 2 0
c header Heading1 108 10 148 200 22 0 Intestazione
c list List1 109 10 176 160 110 1 riga
c textarea TextArea1 110 180 176 200 120 0
c codearea CodeArea1 111 10 292 240 100 0
c combo ComboBox1 112 330 10 140 22 0
c tab Tabs1 113 330 44 180 24 0 Uno
c scrollbar ScrollBar1 114 490 76 16 120 0
c image Image1 115 400 300 64 64 0
F opzioni 300 200 Le opzioni
c button Chiudi 201 20 20 90 26 0 Chiudi
c textbox Nome 202 20 60 140 22 0
EOF
"$DEBUGFS" -w -R "write $D/prima/finestra.dis nuovo/src/finestra.dis" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -R "ls -l nuovo/src" "$OFF" 2>/dev/null | grep -q finestra.dis \
    || { echo "NON RIUSCITO: il progetto non e' sul disco"; exit 1; }

echo "=== 2. exide lo apre e Ctrl+S genera i sorgenti ==="
timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "export HOME=/disk/casa@2" \
    "exwin@20" "key:alt-f1@3" \
    "/exwin/bin/exide /disk/nuovo &@14" "key:alt-f5@4" \
    "foto:$D/2-aperto.ppm@2" "key:ctrl-s@8" "foto:$D/2-salvato.ppm@2" \
    > "$D/2-exide.log" 2>&1

G="$D/dopo"
mkdir -p "$G"
for f in finestra.c finestra.h finestra_gen.c; do
    : > "$G/$f"
    "$DEBUGFS" -R "dump nuovo/src/$f $G/$f" "$OFF" > /dev/null 2>&1
    [ -s "$G/$f" ] && ok "$f generato" || no "$f manca"
done

echo "=== 3. compila con gli header di oggi, senza un avviso ==="
INC="-I $G -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd"
CC="gcc -m32 -ffreestanding -fno-builtin -fno-pic -fno-pie -std=c11 -Wall -Wextra -nostdlib"
for variante in "" "-DEXWIN_SENZA_NOMI_ITALIANI"; do
    come=${variante:+ senza gli alias italiani}
    for f in finestra.c finestra_gen.c; do
        if [ -s "$G/$f" ] && $CC $variante $INC -c "$G/$f" -o "$G/${f%.c}.o" > "$G/$f.log" 2>&1 \
           && [ ! -s "$G/$f.log" ]; then
            ok "$f compila$come"
        else
            no "$f non compila pulito$come (vedi $G/$f.log)"; head -5 "$G/$f.log" | sed 's/^/        /'
        fi
    done
done

echo "=== 4. dichiarazioni e definizioni si corrispondono ==="
if [ -s "$G/finestra.o" ] && [ -s "$G/finestra_gen.o" ]; then
    ld -m elf_i386 -r "$G/finestra.o" "$G/finestra_gen.o" -o "$G/tutto.o" 2> "$G/ld.log"
    fuori=$(nm -u "$G/tutto.o" | awk '{print $2}' | grep -v '^ex_' | tr '\n' ' ')
    [ -z "$fuori" ] && ok "restano da risolvere solo le funzioni ex_* della libreria" \
                    || no "simboli senza definizione: $fuori"
    for v in g_form g_form_opzioni Button1 Label1 TextBox1 CheckBox1 Radio1 Frame1 Line1 Heading1 \
             List1 TextArea1 CodeArea1 ComboBox1 Tabs1 ScrollBar1 Image1 Chiudi Nome \
             window_create window_proc opzioni_create opzioni_proc main \
             Button1_Click Label1_Click TextBox1_Enter CheckBox1_Changed Radio1_Changed \
             List1_Opened TextArea1_Changed CodeArea1_Changed ComboBox1_Selected Tabs1_Selected \
             ScrollBar1_Scrolled Chiudi_Click Nome_Changed; do
        nm "$G/tutto.o" | grep -q " [TDBC] $v\$" || { no "manca la definizione di $v"; mancano=1; }
    done
    [ -z "$mancano" ] && ok "ogni variabile, maschera e handler e' definito"
else
    no "niente da collegare"
fi

echo "=== 5. gli header del CD degli strumenti sono quelli di oggi ==="
# Il gcc dentro EX-OS compila con <strumenti>/include/exwin.h: se li' c'e' ancora
# l'header di prima, il codice generato qui sopra non compila.
for iso in dist/exos-tools.iso; do
    if [ ! -f "$iso" ]; then echo "        ($iso non c'e': salto)"; continue; fi
    if grep -a -q 'typedef.*ExWindow;' "$iso" && grep -a -q 'ex_peek_message' "$iso"; then
        ok "$iso ha l'exwin.h con l'API inglese"
    else
        no "$iso ha ancora l'exwin.h di prima: rifarlo con 'make iso'"
    fi
done

# Quel che `make netinst` pubblica e quel che `make iso` ha composto: gli stessi
# file dei sorgenti, byte per byte. E' da qui che `netupdate` e `toolinst`
# prendono gli header.
for albero in build/iso/exos dist/netinst/file/exos; do
    [ -d "$albero/include" ] || { echo "        ($albero non c'e': salto)"; continue; }
    diversi=""
    for f in lib/exwin/exwin.h lib/exwin/exwin_stub.c lib/include/libc.h lib/exdlg/exdlg.h; do
        cmp -s "$f" "$albero/include/$(basename "$f")" || diversi="$diversi $(basename "$f")"
    done
    [ -z "$diversi" ] && ok "$albero ha gli header e gli stub di oggi" \
                      || no "$albero e' rimasto indietro:$diversi (make netinst li rinfresca)"
done

echo
[ $esito = 0 ] && echo "  prova_exide_genera: TUTTO BENE" \
               || echo "  prova_exide_genera: qualcosa non va (file in $G)"
exit $esito
