#!/bin/bash
# =============================================================================
# tools/prova_runbas.sh — runbas, e F5 dentro gfedit (@RUNBAS)
#
#   1. runbas esegue un programma: stringhe, un FOR, e le funzioni che
#      passano da lib/exmat/exmat.c (EXP, SIN, ^, INT). Il verdetto e'
#      l'uscita sulla seriale, confrontata con i numeri giusti;
#   2. un errore si dice, col nome del file e il perche';
#   3. gfedit apre lo stesso file, F5 lo esegue, Invio torna all'editor.
#
#     tools/prova_runbas.sh [directory-di-lavoro]
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), debugfs (e2fsprogs).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-runbas}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"
[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)

export EXOS_ISTANZA=runbas EXOS_NO_FLOPPY=1 EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"
SER=/tmp/exos/serialrunbas.txt

rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 300 python3 tools/qemu_drive.py "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    > "$D/0.log" 2>&1

cat > "$D/p.bas" <<'EOF'
DIM s AS STRING
DIM i AS INTEGER
DIM t AS INTEGER
s = "Ciao da runbas"
PRINT s
t = 0
FOR i = 1 TO 10
    t = t + i
NEXT i
PRINT "somma"; t
PRINT "exp"; INT(EXP(1) * 1000)
PRINT "sin"; INT(SIN(1) * 1000)
PRINT "pot"; 2 ^ 10
PRINT "int"; INT(-2.5)
PRINT "FINE-PROGRAMMA"
EOF
# ! L'ERRORE E' UN GOTO SENZA ETICHETTA, e non una riga storta: gfbasic e'
# permissivo — una stringa non chiusa, una funzione che non c'e' o «x = = 3»
# passano senza dire niente (provato sull'host, 27 settembre 2026; scritto in
# @RUNBAS). Un'etichetta mancante e' quel che di sicuro rileva.
printf 'PRINT "prima"\nGOTO nessuno\n' > "$D/rotto.bas"
"$DEBUGFS" -w -R "write $D/p.bas p.bas" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/rotto.bas rotto.bas" "$OFF" > /dev/null 2>&1
cat > "$D/chiede.bas" <<'EOF'
INPUT "Nome"; n$
INPUT "Eta", e
INPUT a, b
LINE INPUT "Frase: "; f$
GET "Premi un tasto: "; k$
PRINT "N="; n$; " E="; e; " A+B="; a + b
PRINT "F="; f$
PRINT "K="; k$
EOF
"$DEBUGFS" -w -R "write $D/chiede.bas chiede.bas" "$OFF" > /dev/null 2>&1

esito=0
seriale() { tr -d '\r' < "$SER" | sed 's/\x1b\[[0-9;]*m//g'; }

echo "=== 1 e 2. runbas dalla shell ==="
rm -f "$SER"
timeout 300 python3 tools/qemu_drive.py "mount hd0p1 /disk@6" \
    "runbas /disk/p.bas@5" "runbas /disk/rotto.bas@4" > "$D/1.log" 2>&1
# ! NIENTE SPAZIO DAVANTI AI NUMERI: il QBASIC ne mette uno ai positivi,
# gfbasic no («somma55»). E' dell'interprete, non di runbas: scritto in
# @RUNBAS.
atteso="$(printf 'Ciao da runbas\nsomma55\nexp2718\nsin841\npot1024\nint-3\nFINE-PROGRAMMA')"
uscita="$(seriale | sed -n '/runbas \/disk\/p.bas/,/FINE-PROGRAMMA/p' | sed 1d | sed 's/ *$//')"
if [ "$uscita" = "$atteso" ]; then
    echo "  [OK]  l'uscita e' quella giusta, numeri compresi"
else
    echo "  [NO]  l'uscita e':"; echo "$uscita" | sed 's/^/        /'; esito=1
fi
# ! IL CODICE D'USCITA NON SI GUARDA: la shell di EX-OS non conosce $? (lo
# stampa com'e'). Si guarda il messaggio, che dice il file e il perche'.
if seriale | grep -q "runbas: /disk/rotto.bas: label 'nessuno' non trovata"; then
    echo "  [OK]  l'errore si dice, col file e il perche'"
else
    echo "  [NO]  l'errore non si vede"; esito=1
fi

echo "=== 3. F5 dentro gfedit ==="
rm -f "$SER"
timeout 300 python3 tools/qemu_drive.py "mount hd0p1 /disk@6" \
    "gfedit /disk/p.bas@4" "key:f5@6" "key:ret@3" "key:alt-x@3" \
    "echo DOPO-GFEDIT@2" > "$D/3.log" 2>&1
if seriale | grep -q "FINE-PROGRAMMA" && seriale | grep -q "Invio per tornare" &&
   seriale | grep -q "DOPO-GFEDIT"; then
    echo "  [OK]  F5 esegue, Invio torna all'editor, e l'editor si chiude"
else
    echo "  [NO]  F5 non ha fatto il giro (vedi $SER)"; esito=1
fi

# --- 4. le domande: INPUT, LINE INPUT, GET (27 settembre 2026) --------------
#
# ! IL PROMPT DEVE USCIRE PRIMA DELLA RISPOSTA, ed e' quello che si guarda:
# sulla seriale la domanda e la risposta battuta stanno sulla STESSA riga
# («Nome? Mario»). Prima il prompt restava nel buffer di stdout finche' non
# arrivava un a capo, cioe' dopo la risposta. GET prende un tasto senza Invio.
echo "=== 4. INPUT, LINE INPUT e GET mostrano la domanda ==="
rm -f "$SER"
timeout 300 python3 tools/qemu_drive.py "mount hd0p1 /disk@6" \
    "runbas /disk/chiede.bas@4" "Mario@2" "42@2" "3, 4@2" "ciao, mondo@2" \
    "key:x@3" "echo DOPO-CHIEDE@2" > "$D/4.log" 2>&1
tutto=1
for r in "Nome? Mario" "Eta42" "? 3, 4" "Frase: ciao, mondo" "N=Mario E=42 A+B=7" "F=ciao, mondo" "K=x"; do
    seriale | sed 's/ *$//' | grep -qxF "$r" || { echo "  [NO]  manca la riga «$r»"; tutto=0; esito=1; }
done
seriale | grep -q "^Premi un tasto: x" || { echo "  [NO]  GET non ha preso il tasto"; tutto=0; esito=1; }
[ $tutto = 1 ] && echo "  [OK]  le domande prima delle risposte; GET senza Invio"

# --- 5. INPUT dentro F5 di gfedit (28 settembre 2026) -----------------------
#
# ! LA PARTE 3 LANCIAVA DA gfedit UN PROGRAMMA SENZA INPUT, e la 4 provava gli
# INPUT da runbas lanciato dalla shell: la combinazione non l'aveva provata
# nessuno. Il difetto, segnalato da chi lo usa: da F5 il prompt non
# aspettava la risposta, perche' il primo piano della console restava a
# gfedit e l'INPUT del programma leggeva subito la fine dell'input.
echo "=== 5. F5 in gfedit: l'INPUT aspetta la risposta ==="
cat > "$D/f5input.bas" <<'BAS'
DIM n AS INTEGER
INPUT "Numero"; n
PRINT "RISPOSTA "; n * 2
BAS
"$DEBUGFS" -w -R "write $D/f5input.bas f5input.bas" "$OFF" > /dev/null 2>&1
rm -f "$SER"
timeout 300 python3 tools/qemu_drive.py "mount hd0p1 /disk@6" \
    "gfedit /disk/f5input.bas@4" "key:f5@6" "21@3" "key:ret@3" "key:alt-x@3" \
    "echo DOPO-F5@2" > "$D/5.log" 2>&1
if seriale | grep -q "RISPOSTA 42"; then
    echo "  [OK]  l'INPUT ha aspettato la risposta: 21 -> RISPOSTA 42"
else
    echo "  [NO]  da F5 l'INPUT non ha ricevuto la risposta (vedi $SER)"; esito=1
fi
seriale | grep -qx "DOPO-F5" && echo "  [OK]  gfedit si e' chiuso e la shell e' tornata" \
    || { echo "  [NO]  dopo F5 la shell non e' tornata"; esito=1; }

exit $esito
