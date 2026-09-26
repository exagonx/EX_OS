#!/bin/bash
# =============================================================================
# tools/prova_zip.sh — gli archivi ZIP, nei due versi
#
# Prova @ZIP: la libreria lib/exzip e il programma /bin/zip.
#
#   1. EX-OS crea un archivio, lo elenca, lo riestrae, e `cmp` dice che i file
#      tornati sono IDENTICI agli originali;
#   2. quell'archivio lo apre un lettore ESTRANEO — `unzip -t` di Info-ZIP, o
#      il modulo zipfile di Python — perche' un formato lo prova chi non l'ha
#      scritto;
#   3. un archivio DEFLATE fatto da Info-ZIP, con dentro una sottodirectory, lo
#      estrae EX-OS, e i byte si confrontano qui fuori;
#   4. le due difese: un nome che esce dalla destinazione («../../boot/...») va
#      rifiutato, e un archivio con un byte guastato dev'essere riconosciuto
#      dal CRC invece che estratto a caso.
#
# ! I FILE ENTRANO ED ESCONO DALL'IMMAGINE CON debugfs, che non chiede di
# essere root e non monta niente. Montare l'ext2 vorrebbe dire sudo, e una
# prova che chiede i privilegi e' una prova che non si lancia.
#
# ! IL PUNTO 2 E' QUELLO CHE CONTA DAVVERO. Un archiviatore che rilegge i
# propri archivi prova soltanto di essere coerente con se' stesso: puo'
# scrivere un formato inventato e non accorgersene mai.
#
#     tools/prova_zip.sh [directory-di-lavoro]     (default /tmp/exos-zip)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), debugfs (e2fsprogs).
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-zip}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }

SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
[ -x "$SFDISK" ]  || { echo "manca sfdisk (util-linux)" >&2; exit 1; }
[ -x "$DEBUGFS" ] || { echo "manca debugfs (e2fsprogs)" >&2; exit 1; }

export EXOS_ISTANZA=zip
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

esito=0

# --- 1. Il disco, e due file da archiviare -----------------------------------
echo "=== 1. disco ext2 di prova, e due file dentro ==="
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

timeout 400 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" "mkdir /disk/prova@2" \
    "cp /boot/help.txt /disk/prova/alfa.txt@3" \
    "cp /bin/ls /disk/prova/binario@3" \
    "ls /disk/prova@3" > "$D/1-disco.log" 2>&1

grep -q "binario" "$D/1-disco.log" || {
    echo "NON RIUSCITO: il disco non si e' popolato (vedi $D/1-disco.log)"
    exit 1
}

# --- 2. Il giro completo, e il confronto byte per byte -----------------------
#
# ! IL VERDETTO LO DA' `cmp`, NON IL PROGRAMMA PROVATO. Un archiviatore che
# dichiara «2 estratti» ha detto solo cio' che ha creduto di fare.
echo "=== 2. crea, elenca, estrae — e cmp guarda i byte ==="
timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" \
    "zip /disk/a.zip /disk/prova/alfa.txt /disk/prova/binario@8" \
    "zip -l /disk/a.zip@5" \
    "mkdir /disk/fuori@2" \
    "zip -x /disk/a.zip /disk/fuori@8" \
    "cmp /disk/prova/alfa.txt /disk/fuori/alfa.txt@4" \
    "cmp /disk/prova/binario /disk/fuori/binario@4" \
    "echo CONFRONTO-FINITO@3" > "$D/2-giro.log" 2>&1

# `cmp` tace quando i file sono uguali: quel che NON deve esserci e' «differ».
if grep -q "CONFRONTO-FINITO" "$D/2-giro.log" && \
   ! grep -qi "differ\|non riesco" "$D/2-giro.log"; then
    echo "  [OK]  il giro completo: i file estratti sono identici agli originali"
else
    echo "  [NO]  il giro completo (vedi $D/2-giro.log)"
    esito=1
fi

# --- 3. Un lettore estraneo apre il nostro archivio --------------------------
echo "=== 3. l'archivio di EX-OS, aperto da chi non l'ha scritto ==="
"$DEBUGFS" -R "dump /a.zip $D/a.zip" "$OFF" > /dev/null 2>&1

if [ -s "$D/a.zip" ] && python3 -c "
import sys, zipfile
z = zipfile.ZipFile('$D/a.zip')
sys.exit(0 if z.testzip() is None and len(z.infolist()) == 2 else 1)
" 2>> "$D/3-estraneo.log"; then
    echo "  [OK]  python zipfile lo apre e lo verifica"
else
    echo "  [NO]  un lettore estraneo non apre il nostro archivio"
    esito=1
fi

# ! E DAL 23 SETTEMBRE 2026 IL TESTO DEVE ESSERE COMPRESSO DAVVERO: la
# libreria scrive deflate quando conviene. Senza questo controllo un archivio
# tornato a «store» per sbaglio passerebbe tutte le prove qui sopra, perche'
# anche uno store si apre benissimo.
if python3 -c "
import sys, zipfile
i = zipfile.ZipFile('$D/a.zip').getinfo('alfa.txt')
print('        alfa.txt: %d byte -> %d nell\'archivio, metodo %d' % (i.file_size, i.compress_size, i.compress_type))
sys.exit(0 if i.compress_type == 8 and i.compress_size < i.file_size else 1)
"; then
    echo "  [OK]  il testo e' compresso in deflate"
else
    echo "  [NO]  il testo e' entrato senza compressione"
    esito=1
fi

if command -v unzip > /dev/null; then
    if unzip -t "$D/a.zip" >> "$D/3-estraneo.log" 2>&1; then
        echo "  [OK]  unzip -t di Info-ZIP: nessun errore"
    else
        echo "  [NO]  unzip -t lo rifiuta (vedi $D/3-estraneo.log)"
        esito=1
    fi
fi

# --- 4. Un archivio DEFLATE vero, aperto da EX-OS ----------------------------
echo "=== 4. un deflate fatto da Info-ZIP, estratto da EX-OS ==="
rm -rf "$D/src" "$D/tornati" "$D/vero.zip"
mkdir -p "$D/src/sub" "$D/tornati"
python3 -c "
open('$D/src/testo.txt','w').write('EX-OS e un sistema operativo scritto da zero. ' * 700)
open('$D/src/sub/dentro.bin','wb').write(bytes((i*7+3) % 256 for i in range(20000)))
"
if command -v zip > /dev/null; then
    (cd "$D/src" && zip -r -9 ../vero.zip . > /dev/null)
else
    python3 -c "
import zipfile, os
z = zipfile.ZipFile('$D/vero.zip','w',zipfile.ZIP_DEFLATED)
z.write('$D/src/testo.txt','testo.txt')
z.writestr('sub/','')
z.write('$D/src/sub/dentro.bin','sub/dentro.bin')
z.close()"
fi

"$DEBUGFS" -w -R "write $D/vero.zip vero.zip" "$OFF" > /dev/null 2>&1

timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "zip -l /disk/vero.zip@5" \
    "mkdir /disk/vero@2" "zip -x /disk/vero.zip /disk/vero@10" \
    "ls /disk/vero/sub@3" > "$D/4-deflate.log" 2>&1

"$DEBUGFS" -R "dump /vero/testo.txt $D/tornati/testo.txt" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -R "dump /vero/sub/dentro.bin $D/tornati/dentro.bin" "$OFF" > /dev/null 2>&1

if cmp -s "$D/src/testo.txt" "$D/tornati/testo.txt" && \
   cmp -s "$D/src/sub/dentro.bin" "$D/tornati/dentro.bin"; then
    echo "  [OK]  deflate e sottodirectory: i byte tornano tutti"
else
    echo "  [NO]  il deflate non si apre bene (vedi $D/4-deflate.log)"
    esito=1
fi

# --- 5. Le due difese --------------------------------------------------------
echo "=== 5. il nome che scappa, e il CRC ==="
python3 -c "
import zipfile
z = zipfile.ZipFile('$D/cattivo.zip','w',zipfile.ZIP_DEFLATED)
z.writestr('../../boot/rubato.txt','questo non deve uscire\n')
z.writestr('buono.txt','questo invece si\n')
z.close()
d = bytearray(open('$D/vero.zip','rb').read())
# ! IL BYTE SI GUASTA A META' DEI DATI, trovato dalla struttura dell archivio,
# non a un punto fisso. Era d[80]: il 26 settembre 2026 lo zip dell host ha
# scritto le voci in un altro ordine, il byte 80 e' caduto nella misura di
# un intestazione locale - che il nostro estrattore giustamente non legge,
# prende le misure dal catalogo - e un archivio sano passava per rotto.
import struct
i = max(zipfile.ZipFile('$D/vero.zip').infolist(), key=lambda v: v.compress_size)
n, x = struct.unpack('<HH', bytes(d[i.header_offset + 26:i.header_offset + 30]))
d[i.header_offset + 30 + n + x + i.compress_size // 2] ^= 0xFF
open('$D/rotto.zip','wb').write(bytes(d))
"
"$DEBUGFS" -w -R "write $D/cattivo.zip cattivo.zip" "$OFF" > /dev/null 2>&1
"$DEBUGFS" -w -R "write $D/rotto.zip rotto.zip" "$OFF" > /dev/null 2>&1

timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" \
    "mkdir /disk/difesa@2" "zip -x /disk/cattivo.zip /disk/difesa@8" \
    "ls /disk/difesa@3" \
    "mkdir /disk/rotto@2" "zip -x /disk/rotto.zip /disk/rotto@10" \
    > "$D/5-difese.log" 2>&1

if grep -q "nome che esce dalla destinazione" "$D/5-difese.log" && \
   grep -q "buono.txt" "$D/5-difese.log"; then
    echo "  [OK]  il nome che scappa e' rifiutato, il resto si estrae lo stesso"
else
    echo "  [NO]  il nome fuori dalla destinazione non e' stato fermato"
    esito=1
fi

if grep -qE "CRC non torna|non si aprono" "$D/5-difese.log"; then
    echo "  [OK]  il byte guastato e' riconosciuto invece che estratto"
else
    echo "  [NO]  un archivio rovinato e' passato senza un lamento"
    esito=1
fi

# --- 6. Una cartella intera (23 settembre 2026) --------------------------------
# ! LA CARTELLA VUOTA E' IL PUNTO: un albero con i soli file ci arriverebbe lo
# stesso, e una cartella vuota no — se non ha la sua voce «nome/».
echo "=== 6. una cartella intera, con una sottocartella e una vuota ==="
timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" \
    "mkdir /disk/albero@1" "mkdir /disk/albero/sub@1" "mkdir /disk/albero/vuota@1" \
    "cp /boot/help.txt /disk/albero/uno.txt@3" \
    "cp /bin/ls /disk/albero/sub/due@3" \
    "zip /disk/b.zip /disk/albero@10" \
    "mkdir /disk/fuori2@1" "zip -x /disk/b.zip /disk/fuori2@10" \
    "ls /disk/fuori2/albero@3" "cmp /disk/albero/sub/due /disk/fuori2/albero/sub/due@4" \
    "echo CARTELLA-FINITA@2" > "$D/6-cartella.log" 2>&1

"$DEBUGFS" -R "dump /b.zip $D/b.zip" "$OFF" > /dev/null 2>&1
if [ -s "$D/b.zip" ] && python3 -c "
import sys, zipfile
z = zipfile.ZipFile('$D/b.zip')
nomi = sorted(z.namelist())
print('        dentro:', ' '.join(nomi))
voluti = {'albero/', 'albero/sub/', 'albero/vuota/', 'albero/uno.txt', 'albero/sub/due'}
sys.exit(0 if z.testzip() is None and voluti <= set(nomi) else 1)
"; then
    echo "  [OK]  l'albero e' nell'archivio, cartelle vuote comprese, e si verifica"
else
    echo "  [NO]  l'albero non e' nell'archivio com'era (vedi $D/6-cartella.log)"
    esito=1
fi
if grep -aq "CARTELLA-FINITA" "$D/6-cartella.log" && grep -aq "vuota" "$D/6-cartella.log" && \
   ! grep -aqi "differ" "$D/6-cartella.log"; then
    echo "  [OK]  estratto da EX-OS: c'e' anche la cartella vuota, e i byte tornano"
else
    echo "  [NO]  l'estrazione dell'albero non torna (vedi $D/6-cartella.log)"
    esito=1
fi

echo ""
echo "  I registri sono in $D/*.log"
exit $esito
