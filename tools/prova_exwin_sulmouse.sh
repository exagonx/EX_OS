#!/bin/bash
# =============================================================================
# tools/prova_exwin_sulmouse.sh — «sul mouse», il riquadro accanto al puntatore
# e gli eventi delle caselle di testo (7 ottobre 2026, exwin.so 0.015, exide
# 0.024)
#
# Chiesto dall'utente in correzioni.txt. Un progetto con due caselle e un
# pulsante: la prima casella ha «sulmouse» (terzo numero della riga «s») e
# l'evento Changed, la seconda l'evento Enter. exide lo salva, il gcc del CD
# degli strumenti lo compila DENTRO EX-OS, e col mouse e la tastiera:
#
#   1. nel codice generato ci sono EXM_MOUSE_OVER, EXM_CHANGED, il tasto Invio,
#      ex_track_mouse_hover ed ex_notify_changes; e in finestra.c exide NON ha
#      aggiunto doppioni degli handler che c'erano gia';
#   2. il puntatore arriva sulla casella: <nome>_MouseOver() scatta UNA volta, e
#      a schermo compare il riquadro giallo sbiadito;
#   3. passare al pulsante toglie il riquadro; tornare sulla casella lo
#      richiama: due arrivi in tutto, coi movimenti dentro la casella che non
#      contano;
#   4. scrivere nella casella chiama _Changed a ogni lettera, e Invio nella
#      seconda chiama _Enter: prima della 0.024 non scattavano mai.
#
#     tools/prova_exwin_sulmouse.sh [directory-di-lavoro]  (default /tmp/exos-sulmouse)
#
# Vuole dist/exos.iso e un CD degli strumenti COI COMPILATORI e con gli header
# di oggi:   EXOS_STRUMENTI_ISO=<percorso>   (default dist/exos-tools.iso)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-sulmouse}"
STRUMENTI="${EXOS_STRUMENTI_ISO:-dist/exos-tools.iso}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
[ -f "$STRUMENTI" ] || { echo "manca il CD degli strumenti ($STRUMENTI): lancia 'make iso'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

export EXOS_ISTANZA=sulmouse
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_RAM="${EXOS_RAM:-256M}"
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide,index=0 -drive file=$STRUMENTI,media=cdrom,if=ide,index=3,readonly=on"
SER=/tmp/exos/serialsulmouse.txt

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

# Il mouse di QEMU e' relativo e si ferma 9 pixel prima: vedi prova_giochi.sh.
vai() {
    local dx=$(( $1 + 9 )) dy=$(( $2 + 9 )) i d rx ry
    for i in 1 2 3 4 5; do echo "mon:mouse_move -600 -600@0"; done
    d=$(( (dx < dy ? dx : dy) / 10 ))
    for ((i = 0; i < d; i++)); do echo "mon:mouse_move 10 10@0.04"; done
    rx=$(( dx - d * 10 )); ry=$(( dy - d * 10 ))
    for ((i = 10; i <= rx; i += 10)); do echo "mon:mouse_move 10 0@0.04"; done
    for ((i = 10; i <= ry; i += 10)); do echo "mon:mouse_move 0 10@0.04"; done
    echo "mon:mouse_move $(( rx % 10 )) $(( ry % 10 ))@2"
}
CLIC="mon:mouse_button 1;mouse_button 0@2"

echo "=== 0. il disco e il progetto ==="
rm -f "$IMG"
qemu-img create -f raw "$IMG" 64M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=126976, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 400 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@50" \
    "mount hd0p1 /disk@6" "mkdir /disk/nuovo@2" "mkdir /disk/nuovo/src@2" \
    "mkdir /disk/nuovo/bin@2" "mkdir /disk/nuovo/obj@2" "mkdir /disk/nuovo/inc@2" \
    "mkdir /disk/casa@2" "mkdir /disk/tmp@2" > "$D/0-disco.log" 2>&1

mkdir -p "$D/prima"
# TextBox1: evento 0 (Changed) e «sulmouse» (il terzo numero della riga «s»).
# TextBox2: evento 1 (Enter).
cat > "$D/prima/finestra.dis" <<'FINEDIS'
# exide 0.002 - lo scrive e lo legge solo exide
F principale 340 220 Sul mouse
c textbox TextBox1 101 20 20 160 22 0
s 1 1 1
c textbox TextBox2 102 20 60 160 22 1
c button Button1 103 20 100 90 26 0 Altro
FINEDIS
cat > "$D/prima/finestra.c" <<'FINEC'
#include "libc.h"
#include "finestra.h"

static int g_sopra, g_cambi;

static void scrivi(const char *nome, const char *r)
{
    int fd = open(nome, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd >= 0) { write(fd, r, strlen(r)); close(fd); }
}

void TextBox1_MouseOver(void)
{
    char r[80];

    sprintf(r, "sopra=%d\n", ++g_sopra);
    scrivi("/disk/nuovo/sopra.txt", r);
    ex_show_popup_pointer("aiuto di prova", "0", "0", "0");
}

void TextBox1_Changed(void)
{
    char r[120];

    sprintf(r, "cambi=%d testo=[%s]\n", ++g_cambi, ex_get_text(TextBox1));
    scrivi("/disk/nuovo/cambi.txt", r);
}

void TextBox2_Enter(void)
{
    char r[120];

    sprintf(r, "invio testo=[%s]\n", ex_get_text(TextBox2));
    scrivi("/disk/nuovo/invio.txt", r);
}

int main(void)
{
    ExMsg m;

    window_create();
    while (ex_get_message(&m)) ex_dispatch(&m);
    return 0;
}
FINEC
R=/cdrom/exos
cat > "$D/prima/compila.sh" <<FINESH
$R/bin/gcc -m32 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie -O2 -Wall -I $R/include -I inc -T $R/programma.ld $R/start.S src/finestra.c src/finestra_gen.c $R/include/exwin_stub.c $R/libc_ponti_asm.o $R/libc_ponti_c.o $R/libc_ponti_avvio.o $R/libc_ponti_exlib.o -o bin/nuovo
FINESH
"$DEBUGFS" -w -R "write $D/prima/finestra.dis nuovo/src/finestra.dis" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/prima/finestra.c nuovo/src/finestra.c" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/prima/compila.sh nuovo/compila.sh" "$OFF" > /dev/null 2>&1

echo "=== exide salva, il gcc di EX-OS compila, il programma gira ==="
# La finestra del programma nasce nell'angolo in alto a sinistra: l'area dei
# controlli comincia a (2, 22). La casella 1 ha il centro a (102, 53), il
# pulsante a (67, 135).
{
    printf '%s\n' "mount hd0p1 /disk@6" "mount cd1 /cdrom@6" \
        "export HOME=/disk/casa@2" "export TMPDIR=/disk/tmp@2" \
        "exwin@20" "key:alt-f1@3" \
        "/exwin/bin/exide /disk/nuovo &@14" "key:alt-f5@4" \
        "key:ctrl-s@8" "key:ctrl-q@4" \
        "key:alt-f1@3" "cd /disk/nuovo@2" \
        "sh compila.sh > /disk/nuovo/obj/compila.log@150" \
        "/disk/nuovo/bin/nuovo &@10" "key:alt-f5@4" "foto:$D/1-partito.ppm@1"
    vai 102 53;  echo "foto:$D/2-sopra.ppm@1"
    echo "mon:mouse_move 6 0@1"; echo "mon:mouse_move -6 2@1"     # dentro la casella
    # ! DA QUI CI SI SPOSTA DI QUANTO SERVE, SENZA RIPARTIRE DALL'ANGOLO: la
    # strada dall'angolo al pulsante passa sopra la casella, e sarebbe un
    # arrivo in piu' — giusto per il toolkit, sbagliato per il conto.
    for i in 1 2 3 4 5 6 7 8; do echo "mon:mouse_move 0 10@0.05"; done   # giu', sul pulsante
    echo "mon:mouse_move 0 0@2"; echo "foto:$D/3-altrove.ppm@1"
    for i in 1 2 3 4 5 6 7 8; do echo "mon:mouse_move 0 -10@0.05"; done  # e su, di nuovo
    echo "mon:mouse_move 0 0@2"; echo "foto:$D/4-di-nuovo.ppm@1"
    echo "$CLIC"; echo "xy@n2"; echo "foto:$D/5-scritto.ppm@1"
    echo "key:tab@1"; echo "k@n1"; echo "key:ret@3"
    echo "key:alt-f1@3"
} > "$D/args.txt"
mapfile -t A < "$D/args.txt"
timeout 1200 python3 tools/qemu_drive.py "${A[@]}" > "$D/2-giro.log" 2>&1

G="$D/dopo"
mkdir -p "$G"
for f in src/finestra.c src/finestra_gen.c obj/compila.log bin/nuovo sopra.txt cambi.txt invio.txt; do
    : > "$G/$(basename $f)"
    "$DEBUGFS" -R "dump nuovo/$f $G/$(basename $f)" "$OFF" > /dev/null 2>&1
done

gialli() {      # quanti pixel del colore predefinito del riquadro (255,248,220)
    python3 - "$1" <<'FINEPY'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)[3]
print(sum(1 for i in range(0, len(d) - 2, 3) if d[i:i+3] == bytes((255, 248, 220))))
FINEPY
}

echo "=== 1. il codice generato ==="
for cosa in "EXM_MOUSE_OVER" "TextBox1_MouseOver();" "EXM_CHANGED" "TextBox1_Changed();" \
            "ex_get_focus(f)" "TextBox2_Enter();" "ex_track_mouse_hover(g_form, 1);" \
            "ex_notify_changes(g_form, 1);"; do
    grep -q -F "$cosa" "$G/finestra_gen.c" && ok "finestra_gen.c ha  $cosa" \
                                           || no "in finestra_gen.c manca  $cosa"
done
[ "$(grep -c "^void TextBox1_MouseOver" "$G/finestra.c")" = 1 ] \
    && [ "$(grep -c "^void TextBox1_Changed" "$G/finestra.c")" = 1 ] \
    && ok "exide non ha raddoppiato gli handler che c'erano gia'" \
    || no "handler raddoppiati o spariti in finestra.c"
grep -q "^void Button1_Click" "$G/finestra.c" && ok "ha aggiunto quello che mancava (Button1_Click)" \
                                              || no "Button1_Click non e' stato aggiunto"
if [ -s "$G/nuovo" ] && head -c 4 "$G/nuovo" | grep -q "ELF"; then
    ok "il gcc dentro EX-OS ha compilato il programma"
else
    no "bin/nuovo non c'e':"; head -6 "$G/compila.log" | sed 's/^/        /'
fi

echo "=== 2. il puntatore arriva sulla casella ==="
g1=$(gialli "$D/1-partito.ppm"); g2=$(gialli "$D/2-sopra.ppm")
[ "${g1:-1}" = 0 ] && [ "${g2:-0}" -gt 400 ] \
    && ok "il riquadro accanto al puntatore compare ($g2 pixel del suo giallo, $g1 prima)" \
    || no "il riquadro non si vede (pixel gialli: $g1 prima, $g2 col puntatore sopra)"

echo "=== 3. via dal controllo, e di nuovo sopra ==="
g3=$(gialli "$D/3-altrove.ppm"); g4=$(gialli "$D/4-di-nuovo.ppm")
[ "${g3:-1}" = 0 ] && ok "passando al pulsante il riquadro se ne va da solo" \
                   || no "il riquadro e' rimasto ($g3 pixel) col puntatore sul pulsante"
[ "${g4:-0}" -gt 400 ] && [ "$(cat "$G/sopra.txt")" = "sopra=2" ] \
    && ok "tornando sulla casella scatta di nuovo, e sono due in tutto: una per arrivo, non una per movimento" \
    || no "al ritorno: $(cat "$G/sopra.txt") (atteso sopra=2), pixel gialli $g4"
g5=$(gialli "$D/5-scritto.ppm")
[ "${g5:-1}" = 0 ] && ok "un clic e un tasto lo tolgono" || no "dopo clic e tasti il riquadro c'e' ancora ($g5 pixel)"

echo "=== 4. gli eventi delle caselle ==="
echo "        $(cat "$G/cambi.txt")   $(cat "$G/invio.txt")"
[ "$(cat "$G/cambi.txt")" = "cambi=2 testo=[xy]" ] \
    && ok "TextBox1_Changed scatta a ogni lettera (due, testo «xy»)" \
    || no "Changed: atteso «cambi=2 testo=[xy]»"
[ "$(cat "$G/invio.txt")" = "invio testo=[k]" ] \
    && ok "TextBox2_Enter scatta a Invio nella casella" || no "Enter: atteso «invio testo=[k]»"
grep -a "\[FAULT\]" "$SER" > /dev/null 2>&1 && no "un [FAULT] sulla seriale (vedi $SER)" || ok "nessun [FAULT]"

echo
[ $esito = 0 ] && echo "  prova_exwin_sulmouse: TUTTO BENE" \
               || echo "  prova_exwin_sulmouse: qualcosa non va (file in $D)"
exit $esito
