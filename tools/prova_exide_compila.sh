#!/bin/bash
# =============================================================================
# tools/prova_exide_compila.sh — il progetto di exide compila DENTRO EX-OS e
# il programma parte (4 ottobre 2026)
#
# prova_exide_genera.sh compila sull'host con gli header dei sorgenti; qui si
# fa il giro vero, quello di chi usa il sistema: exide genera, il gcc del CD
# degli strumenti compila con la riga di compila.sh, e il binario si avvia in
# una finestra. E' la prova che il 3 ottobre 2026 mancava: l'exide nuovo con
# gli strumenti di prima non compilava niente.
#
#     tools/prova_exide_compila.sh [directory-di-lavoro]  (default /tmp/exos-ide-compila)
#
# Vuole dist/exos.iso e un CD degli strumenti COI COMPILATORI:
#     EXOS_STRUMENTI_ISO=<percorso>   (default dist/exos-tools.iso)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-ide-compila}"
STRUMENTI="${EXOS_STRUMENTI_ISO:-dist/exos-tools.iso}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
[ -f "$STRUMENTI" ] || { echo "manca il CD degli strumenti ($STRUMENTI): lancia 'make iso'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

export EXOS_ISTANZA=idecompila
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_RAM="${EXOS_RAM:-256M}"
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide,index=0 -drive file=$STRUMENTI,media=cdrom,if=ide,index=3,readonly=on"
SER=/tmp/exos/serialidecompila.txt

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

echo "=== 1. il disco e il progetto ==="
rm -f "$IMG"
qemu-img create -f raw "$IMG" 64M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=126976, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 400 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@50" \
    "mount hd0p1 /disk@6" "mkdir /disk/nuovo@2" "mkdir /disk/nuovo/src@2" \
    "mkdir /disk/nuovo/bin@2" "mkdir /disk/nuovo/obj@2" "mkdir /disk/nuovo/inc@2" \
    "mkdir /disk/casa@2" "mkdir /disk/tmp@2" > "$D/1-disco.log" 2>&1

mkdir -p "$D/prima"
cat > "$D/prima/finestra.dis" <<'EOF'
# exide 0.002 - lo scrive e lo legge solo exide
F principale 320 200 Compilato qui
c button Button1 101 20 20 110 26 0 Premi
c label Label1 102 20 60 200 16 1 Fatto da exide
c textbox TextBox1 103 20 90 160 22 0 ciao
c checkbox CheckBox1 104 20 120 160 20 0 Una spunta
c list List1 105 200 20 100 110 0 riga
EOF
# La riga di exide (riga_compila in exide.c), con la radice sul CD.
R=/cdrom/exos
cat > "$D/prima/compila.sh" <<EOF
$R/bin/gcc -m32 -ffreestanding -fno-builtin -nostdlib -fno-pic -fno-pie -O2 -Wall -I $R/include -I inc -T $R/programma.ld $R/start.S src/finestra.c src/finestra_gen.c $R/include/exwin_stub.c $R/libc_ponti_asm.o $R/libc_ponti_c.o $R/libc_ponti_avvio.o $R/libc_ponti_exlib.o -o bin/nuovo
EOF
"$DEBUGFS" -w -R "write $D/prima/finestra.dis nuovo/src/finestra.dis" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/prima/compila.sh nuovo/compila.sh" "$OFF" > /dev/null 2>&1

echo "=== 2. exide genera, il gcc di EX-OS compila, il programma parte ==="
timeout 900 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "mount cd1 /cdrom@6" "ls /cdrom/exos/bin/gcc@3" \
    "export HOME=/disk/casa@2" "export TMPDIR=/disk/tmp@2" \
    "exwin@20" "key:alt-f1@3" \
    "/exwin/bin/exide /disk/nuovo &@14" "key:alt-f5@4" "key:ctrl-s@8" "key:ctrl-q@4" \
    "key:alt-f1@3" "cd /disk/nuovo@2" \
    "sh compila.sh > /disk/nuovo/obj/compila.log@150" \
    "ls -l /disk/nuovo/bin@3" \
    "/disk/nuovo/bin/nuovo &@10" "key:alt-f5@4" "foto:$D/3-programma.ppm@2" \
    > "$D/2-giro.log" 2>&1

G="$D/dopo"
mkdir -p "$G"
for f in src/finestra.c src/finestra.h src/finestra_gen.c obj/compila.log bin/nuovo; do
    : > "$G/$(basename $f)"
    "$DEBUGFS" -R "dump nuovo/$f $G/$(basename $f)" "$OFF" > /dev/null 2>&1
done

[ -s "$G/finestra.h" ] && [ -s "$G/finestra_gen.c" ] && [ -s "$G/finestra.c" ] \
    && ok "exide ha generato i tre sorgenti" || no "mancano dei sorgenti generati"

if [ -s "$G/nuovo" ] && head -c 4 "$G/nuovo" | grep -q "ELF"; then
    ok "il gcc dentro EX-OS ha prodotto bin/nuovo ($(stat -c %s "$G/nuovo") byte)"
else
    no "bin/nuovo non c'e': la compilazione dentro EX-OS e' fallita"
    grep -a "error\|errore\|unknown\|undeclared" "$D/2-giro.log" "$G/compila.log" 2>/dev/null | head -8 | sed 's/^/        /'
fi
if grep -a -q "warning\|error" "$G/compila.log" 2>/dev/null || grep -a -q "error:" "$D/2-giro.log"; then
    no "il compilatore ha detto qualcosa:"; grep -a "warning\|error" "$G/compila.log" "$D/2-giro.log" | head -8 | sed 's/^/        /'
else
    ok "nessun errore e nessun avviso dal compilatore"
fi

echo "=== 3. il programma compilato apre la sua finestra ==="
if [ -f "$D/3-programma.ppm" ]; then
    # La finestra del programma: grigio dei controlli la' dove prima c'era il
    # blu della scrivania.
    n=$(python3 - "$D/3-programma.ppm" <<'FINEPY'
import sys
d = open(sys.argv[1], "rb").read().split(b"\n", 3)
w = int(d[1].split()[0]); px = d[3]
print(sum(1 for y in range(30, 230) for x in range(10, 330)
          if px[(y * w + x) * 3:(y * w + x) * 3 + 3] == bytes((192, 192, 192))))
FINEPY
)
    [ "${n:-0}" -gt 20000 ] && ok "la finestra del programma c'e' ($n pixel grigi)" \
                           || no "la finestra del programma non si vede ($n pixel grigi)"
else
    no "manca la fotografia"
fi
grep -a "\[FAULT\]" "$SER" > /dev/null 2>&1 && no "un [FAULT] sulla seriale (vedi $SER)" || ok "nessun [FAULT]"

echo
[ $esito = 0 ] && echo "  prova_exide_compila: TUTTO BENE" \
               || echo "  prova_exide_compila: qualcosa non va (file in $D)"
exit $esito
