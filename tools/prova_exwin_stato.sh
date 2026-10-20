#!/bin/bash
# =============================================================================
# tools/prova_exwin_stato.sh — visibile e attivo: le due proprieta' di exide e
# le funzioni ex_set_visible / ex_set_enabled (7 ottobre 2026)
#
# Chiesto dall'utente in correzioni.txt. Un progetto con una spunta accesa,
# una casella SPENTA (riga «s 1 0» nel disegno), un'etichetta NASCOSTA («s 0
# 1»), una spunta e una lista spente. exide lo apre e lo salva; il gcc del CD
# degli strumenti lo compila DENTRO EX-OS; il programma gira, e tutto si fa
# con la tastiera:
#
#   1. nel codice generato escono ex_set_visible(Label1, 0) ed
#      ex_set_enabled(...), e il disegno salvato ha ancora le righe «s»;
#   2. Tab non arriva sui controlli spenti: dopo due Tab la barra spaziatrice
#      accende ancora la spunta accesa, non quella spenta;
#   3. la spunta chiama un handler che scrive su disco che cosa rispondono
#      ex_is_visible ed ex_is_enabled, e poi li rovescia;
#   4. dopo, la casella prima spenta prende il fuoco e i tasti, e l'etichetta
#      compare: lo dicono il secondo file e la differenza fra le fotografie.
#
#     tools/prova_exwin_stato.sh [directory-di-lavoro]  (default /tmp/exos-stato)
#
# Vuole dist/exos.iso e un CD degli strumenti COI COMPILATORI e con gli header
# di oggi (exwin.so 0.014):
#     EXOS_STRUMENTI_ISO=<percorso>   (default dist/exos-tools.iso)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-stato}"
STRUMENTI="${EXOS_STRUMENTI_ISO:-dist/exos-tools.iso}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
[ -f "$STRUMENTI" ] || { echo "manca il CD degli strumenti ($STRUMENTI): lancia 'make iso'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

export EXOS_ISTANZA=stato
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_RAM="${EXOS_RAM:-256M}"
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide,index=0 -drive file=$STRUMENTI,media=cdrom,if=ide,index=3,readonly=on"
SER=/tmp/exos/serialstato.txt

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

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
# Le righe «s» dicono visibile e attivo del controllo che le precede.
cat > "$D/prima/finestra.dis" <<'FINEDIS'
# exide 0.002 - lo scrive e lo legge solo exide
F principale 340 220 Visibile e attivo
c checkbox CheckBox2 101 20 20 160 20 0 Rovescia
c textbox TextBox1 102 20 54 160 22 0 ciao
s 1 0
c label Label1 103 20 88 200 16 1 Adesso mi vedi
s 0 1
c checkbox CheckBox1 104 20 114 160 20 0 Una spunta
s 1 0
c list List1 105 200 20 120 110 0 riga
s 1 0
FINEDIS
cat > "$D/prima/finestra.c" <<'FINEC'
#include "libc.h"
#include "finestra.h"

static int g_giro = 1;

void CheckBox2_Changed(void)
{
    char r[240], nome[40];
    int  fd;

    sprintf(nome, "/disk/nuovo/stato%d.txt", g_giro++);
    sprintf(r, "etichetta visibile=%d casella attiva=%d spunta attiva=%d "
               "lista attiva=%d testo1=[%s] spunta1=%d spunta2=%d\n",
            ex_is_visible(Label1), ex_is_enabled(TextBox1),
            ex_is_enabled(CheckBox1), ex_is_enabled(List1),
            ex_get_text(TextBox1), ex_is_checked(CheckBox1),
            ex_is_checked(CheckBox2));
    fd = open(nome, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) { write(fd, r, strlen(r)); close(fd); }

    ex_set_visible(Label1, !ex_is_visible(Label1));
    ex_set_enabled(TextBox1, !ex_is_enabled(TextBox1));
}

int main(void)
{
    ExMsg m;

    window_create();
    while (ex_get_message(&m)) ex_dispatch(&m);
    return 0;
}
FINEC
# La riga di exide (riga_compila in exide.c), con la radice sul CD.
R=/cdrom/exos
cat > "$D/prima/compila.sh" <<FINESH
$R/bin/gcc -m32 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie -O2 -Wall -I $R/include -I inc -T $R/programma.ld $R/start.S src/finestra.c src/finestra_gen.c $R/include/exwin_stub.c $R/libc_ponti_asm.o $R/libc_ponti_c.o $R/libc_ponti_avvio.o $R/libc_ponti_exlib.o -o bin/nuovo
FINESH
"$DEBUGFS" -w -R "write $D/prima/finestra.dis nuovo/src/finestra.dis" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/prima/finestra.c nuovo/src/finestra.c" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/prima/compila.sh nuovo/compila.sh" "$OFF" > /dev/null 2>&1

echo "=== exide salva, il gcc di EX-OS compila, il programma gira ==="
timeout 900 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "mount cd1 /cdrom@6" \
    "export HOME=/disk/casa@2" "export TMPDIR=/disk/tmp@2" \
    "exwin@20" "key:alt-f1@3" \
    "/exwin/bin/exide /disk/nuovo &@14" "key:alt-f5@4" "foto:$D/2-exide.ppm@1" \
    "key:ctrl-s@8" "key:ctrl-q@4" \
    "key:alt-f1@3" "cd /disk/nuovo@2" \
    "sh compila.sh > /disk/nuovo/obj/compila.log@150" \
    "/disk/nuovo/bin/nuovo &@10" "key:alt-f5@4" "foto:$D/3-prima.ppm@2" \
    "key:tab@1" "key:tab@1" "key:spc@4" "foto:$D/4-dopo.ppm@2" \
    "key:tab@1" "Z@n1" "key:tab@1" "key:spc@4" "foto:$D/5-fine.ppm@2" \
    "key:alt-f1@3" "cat /disk/nuovo/stato1.txt@2" "cat /disk/nuovo/stato2.txt@2" \
    > "$D/2-giro.log" 2>&1

G="$D/dopo"
mkdir -p "$G"
for f in src/finestra.dis src/finestra_gen.c obj/compila.log bin/nuovo stato1.txt stato2.txt; do
    : > "$G/$(basename $f)"
    "$DEBUGFS" -R "dump nuovo/$f $G/$(basename $f)" "$OFF" > /dev/null 2>&1
done

echo "=== 1. il codice generato e il disegno salvato ==="
grep -q "ex_set_visible(Label1, 0);" "$G/finestra_gen.c" \
    && ok "l'etichetta nasce con ex_set_visible(Label1, 0)" \
    || no "manca ex_set_visible(Label1, 0) in finestra_gen.c"
[ "$(grep -c "ex_set_enabled(.*, 0);" "$G/finestra_gen.c")" = 3 ] \
    && ok "tre controlli nascono con ex_set_enabled(..., 0)" \
    || no "ex_set_enabled in finestra_gen.c: $(grep -c 'ex_set_enabled' "$G/finestra_gen.c") righe, attese 3"
[ "$(grep -c "^s 1 0" "$G/finestra.dis")" = 3 ] && [ "$(grep -c "^s 0 1" "$G/finestra.dis")" = 1 ] \
    && ok "il disegno salvato da exide ha ancora le righe «s»" \
    || no "le righe «s» non sono tornate uguali in finestra.dis"
if [ -s "$G/nuovo" ] && head -c 4 "$G/nuovo" | grep -q "ELF"; then
    ok "il gcc dentro EX-OS ha compilato il programma"
else
    no "bin/nuovo non c'e':"; head -6 "$G/compila.log" | sed 's/^/        /'
fi

echo "=== 2. spento e nascosto, alla partenza ==="
S1=$(cat "$G/stato1.txt" 2>/dev/null); S2=$(cat "$G/stato2.txt" 2>/dev/null)
echo "        $S1"
case "$S1" in
    *"etichetta visibile=0 casella attiva=0 spunta attiva=0 lista attiva=0"*)
        ok "ex_is_visible ed ex_is_enabled dicono 0 per chi nasce nascosto o spento" ;;
    *)  no "lo stato iniziale non e' quello del disegno" ;;
esac
case "$S1" in
    *"testo1=[ciao] spunta1=0 spunta2=1"*)
        ok "Tab salta i controlli spenti: la barra ha acceso la spunta accesa, non l'altra" ;;
    *)  no "i tasti non sono finiti dove dovevano (atteso testo1=[ciao] spunta1=0 spunta2=1)" ;;
esac

echo "=== 3. riaccesi dal programma ==="
echo "        $S2"
case "$S2" in
    *"etichetta visibile=1 casella attiva=1"*)
        ok "ex_set_visible ed ex_set_enabled li hanno riaccesi" ;;
    *)  no "dopo la prima spunta lo stato non e' cambiato" ;;
esac
case "$S2" in
    # Arrivandoci con Tab il testo e' tutto scelto, e la lettera lo sostituisce:
    # «Z» da sola e' la risposta giusta, come in ogni casella di testo.
    *"testo1=[Z]"*|*"testo1=[ciaoZ]"*|*"testo1=[Zciao]"*)
        ok "la casella riaccesa prende il fuoco e i tasti" ;;
    *)  no "la casella riaccesa non ha preso la Z" ;;
esac
if [ -f "$D/3-prima.ppm" ] && [ -f "$D/4-dopo.ppm" ]; then
    d=$(python3 - "$D/3-prima.ppm" "$D/4-dopo.ppm" <<'FINEPY'
import sys
a, b = (open(p, "rb").read().split(b"\n", 3)[3] for p in sys.argv[1:3])
print(sum(1 for i in range(0, min(len(a), len(b)), 3) if a[i:i+3] != b[i:i+3]))
FINEPY
)
    [ "${d:-0}" -gt 300 ] && ok "a schermo si vede: $d pixel cambiati (l'etichetta, il velo tolto)" \
                          || no "le due fotografie sono quasi uguali ($d pixel)"
else
    no "mancano le fotografie"
fi
grep -a "\[FAULT\]" "$SER" > /dev/null 2>&1 && no "un [FAULT] sulla seriale (vedi $SER)" || ok "nessun [FAULT]"

echo
[ $esito = 0 ] && echo "  prova_exwin_stato: TUTTO BENE" \
               || echo "  prova_exwin_stato: qualcosa non va (file in $D)"
exit $esito
