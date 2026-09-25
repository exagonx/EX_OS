#!/bin/bash
# =============================================================================
# tools/prova_tar.sh — tar, gzip e gunzip, nei due versi
#
# Prova @TAR-GZ: il programma bin/tar/tar.c, che risponde a tre nomi (/bin/tar,
# /bin/gzip, /bin/gunzip).
#
#   1. EX-OS crea un .tar e un .tar.gz, li elenca, li riestrae, e `cmp` dice
#      che i file tornati sono IDENTICI agli originali;
#   2. quegli archivi li aprono lettori ESTRANEI — GNU tar e gzip -t dell'host,
#      e il modulo tarfile di Python — e i byte si confrontano qui fuori;
#   3. un .tar.gz fatto da GNU tar (nome piu' lungo di 100, voce 'L') e un .tar
#      in formato pax (voce 'x' con «path=») li estrae EX-OS;
#   4. gzip e gunzip da soli, in tutti e due i versi con l'host;
#   5. le due difese: un nome che esce dalla destinazione («../../boot/...») va
#      rifiutato, e un .gz con un byte guastato dev'essere riconosciuto dal CRC.
#
# ! I FILE ENTRANO ED ESCONO DALL'IMMAGINE CON debugfs, come in prova_zip.sh:
# niente sudo, niente mount.
#
# ! IL PUNTO 2 E' QUELLO CHE CONTA DAVVERO: un archiviatore che rilegge i
# propri archivi prova soltanto di essere coerente con se' stesso.
#
#     tools/prova_tar.sh [directory-di-lavoro]     (default /tmp/exos-tar)
#
# Vuole: dist/exos.iso aggiornato (make iso-exos), debugfs (e2fsprogs), tar e
# gzip dell'host, e build/bin-cd/tar (make tar_prog).
#
# ! I TRE PROGRAMMI SI PROVANO DAL DISCO, NON DAL CD: la prova copia
# build/bin-cd/{tar,gzip,gunzip} in /bin del disco di prova e li lancia da
# /disk/bin. Cosi' si prova quel che si e' appena compilato, anche con un'ISO
# vecchia — e anche su una macchina il cui GCC non fa piu' stare il floppy.
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-tar}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }

SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
[ -x "$SFDISK" ]  || { echo "manca sfdisk (util-linux)" >&2; exit 1; }
[ -x "$DEBUGFS" ] || { echo "manca debugfs (e2fsprogs)" >&2; exit 1; }
[ -f build/bin-cd/tar ] || { echo "manca build/bin-cd/tar: lancia 'make tar_prog'" >&2; exit 1; }
TAR=/disk/bin/tar; GZIP=/disk/bin/gzip; GUNZIP=/disk/bin/gunzip

export EXOS_ISTANZA=tar
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

esito=0
ok()  { echo "  [OK]  $1"; }
no()  { echo "  [NO]  $1"; esito=1; }
esci() { "$DEBUGFS" -R "dump $1 $2" "$OFF" > /dev/null 2>&1; }
entra() { "$DEBUGFS" -w -R "write $1 $2" "$OFF" > /dev/null 2>&1; }

# --- 1. Il disco, e un albero da archiviare ----------------------------------
echo "=== 1. disco ext2 di prova, e un albero dentro ==="
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1

timeout 400 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" "mkdir /disk/bin@2" "mkdir /disk/albero@2" "mkdir /disk/albero/sub@2" \
    "mkdir /disk/albero/vuota@2" \
    "cp /boot/help.txt /disk/albero/alfa.txt@3" \
    "cp /bin/ls /disk/albero/sub/binario@3" \
    "ls /disk/albero/sub@3" > "$D/1-disco.log" 2>&1

for p in tar gzip gunzip; do entra "build/bin-cd/$p" "bin/$p"; done

grep -aq "binario" "$D/1-disco.log" || {
    echo "NON RIUSCITO: il disco non si e' popolato (vedi $D/1-disco.log)"
    exit 1
}
rm -rf "$D/orig" && mkdir -p "$D/orig"
esci /albero/alfa.txt "$D/orig/alfa.txt"
esci /albero/sub/binario "$D/orig/binario"

# --- 2. Il giro completo dentro EX-OS ----------------------------------------
# ! IL VERDETTO LO DA' `cmp`, NON IL PROGRAMMA PROVATO.
echo "=== 2. crea, elenca, estrae (tar e tar.gz) — e cmp guarda i byte ==="
timeout 500 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "cd /disk@1" \
    "$TAR cf /disk/a.tar albero@8" \
    "$TAR czf /disk/a.tgz albero@10" \
    "$TAR tf /disk/a.tgz@6" \
    "mkdir /disk/f1@1" "$TAR xf /disk/a.tar -C /disk/f1@8" \
    "mkdir /disk/f2@1" "$TAR xf /disk/a.tgz -C /disk/f2@10" \
    "cmp /disk/albero/alfa.txt /disk/f1/albero/alfa.txt@4" \
    "cmp /disk/albero/sub/binario /disk/f1/albero/sub/binario@4" \
    "cmp /disk/albero/alfa.txt /disk/f2/albero/alfa.txt@4" \
    "cmp /disk/albero/sub/binario /disk/f2/albero/sub/binario@4" \
    "ls /disk/f2/albero@3" \
    "echo CONFRONTO-FINITO@3" > "$D/2-giro.log" 2>&1

if grep -aq "CONFRONTO-FINITO" "$D/2-giro.log" && grep -aq "vuota" "$D/2-giro.log" && \
   ! grep -aqi "differ\|non riesco\|cannot\|bad header\|corrupt" "$D/2-giro.log"; then
    ok "il giro completo: tar e tar.gz tornano identici, cartella vuota compresa"
else
    no "il giro completo (vedi $D/2-giro.log)"
fi

# --- 3. Lettori estranei aprono i nostri archivi -----------------------------
echo "=== 3. gli archivi di EX-OS, aperti da chi non li ha scritti ==="
esci /a.tar "$D/a.tar"
esci /a.tgz "$D/a.tgz"

if [ -s "$D/a.tgz" ] && gzip -t "$D/a.tgz" 2>> "$D/3-estranei.log"; then
    ok "gzip -t dell'host: CRC e lunghezza del .tgz tornano"
else
    no "gzip -t rifiuta il nostro .tgz (vedi $D/3-estranei.log)"
fi

for a in a.tar a.tgz; do
    rm -rf "$D/host-$a" && mkdir -p "$D/host-$a"
    if tar xf "$D/$a" -C "$D/host-$a" 2>> "$D/3-estranei.log" && \
       cmp -s "$D/orig/alfa.txt" "$D/host-$a/albero/alfa.txt" && \
       cmp -s "$D/orig/binario" "$D/host-$a/albero/sub/binario" && \
       [ -d "$D/host-$a/albero/vuota" ]; then
        ok "GNU tar estrae $a, e i byte sono quelli di EX-OS"
    else
        no "GNU tar non estrae bene $a (vedi $D/3-estranei.log)"
    fi
done

if python3 -c "
import sys, tarfile
t = tarfile.open('$D/a.tgz')
nomi = set(t.getnames())
voluti = {'albero', 'albero/sub', 'albero/vuota', 'albero/alfa.txt', 'albero/sub/binario'}
sys.exit(0 if voluti <= nomi else 1)
" 2>> "$D/3-estranei.log"; then
    ok "python tarfile legge il .tgz e trova tutte le voci"
else
    no "python tarfile non trova le voci attese"
fi

# --- 4. Archivi fatti fuori, aperti da EX-OS ---------------------------------
# ! I NOMI LUNGHI SONO IL PUNTO: sotto i cento caratteri ogni tar e' ustar
# puro, e le voci 'L' (GNU) e 'x' (pax) non si vedrebbero mai.
echo "=== 4. un .tar.gz di GNU tar e un .tar pax, estratti da EX-OS ==="
LUNGO="cartella_con_un_nome_decisamente_lungo/e_un_file_che_supera_i_cento_caratteri_del_campo_nome_di_ustar.txt"
rm -rf "$D/src" && mkdir -p "$D/src/$(dirname "$LUNGO")" "$D/src/sub"
python3 -c "
open('$D/src/testo.txt','w').write('EX-OS e un sistema operativo scritto da zero. ' * 700)
open('$D/src/sub/dentro.bin','wb').write(bytes((i*7+3) % 256 for i in range(20000)))
open('$D/src/$LUNGO','w').write('nome lungo\n' * 50)
"
(cd "$D/src" && tar --format=gnu -czf ../gnu.tgz . && tar --format=pax -cf ../pax.tar .)
entra "$D/gnu.tgz" gnu.tgz
entra "$D/pax.tar" pax.tar

timeout 500 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" \
    "mkdir /disk/gnu@1" "$TAR xf /disk/gnu.tgz -C /disk/gnu@12" \
    "mkdir /disk/pax@1" "$TAR xf /disk/pax.tar -C /disk/pax@12" \
    > "$D/4-estranei.log" 2>&1

tutto=1
for dir in gnu pax; do
    rm -rf "$D/tornati-$dir" && mkdir -p "$D/tornati-$dir"
    esci "/$dir/testo.txt" "$D/tornati-$dir/testo.txt"
    esci "/$dir/sub/dentro.bin" "$D/tornati-$dir/dentro.bin"
    esci "/$dir/$LUNGO" "$D/tornati-$dir/lungo.txt"
    if cmp -s "$D/src/testo.txt" "$D/tornati-$dir/testo.txt" && \
       cmp -s "$D/src/sub/dentro.bin" "$D/tornati-$dir/dentro.bin" && \
       cmp -s "$D/src/$LUNGO" "$D/tornati-$dir/lungo.txt"; then
        ok "formato $dir: sottocartella e nome lungo, i byte tornano tutti"
    else
        no "formato $dir non si estrae bene (vedi $D/4-estranei.log)"
    fi
done

# --- 5. gzip e gunzip da soli ------------------------------------------------
echo "=== 5. gzip e gunzip, con l'host dall'altra parte ==="
gzip -9 -c "$D/src/testo.txt" > "$D/fuori.txt.gz"
entra "$D/fuori.txt.gz" fuori.txt.gz

timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" \
    "cp /disk/albero/alfa.txt /disk/g.txt@3" "$GZIP /disk/g.txt@8" \
    "$GUNZIP /disk/fuori.txt.gz@8" \
    > "$D/5-gzip.log" 2>&1

esci /g.txt.gz "$D/g.txt.gz"
esci /fuori.txt "$D/fuori.txt"
if [ -s "$D/g.txt.gz" ] && gzip -dc "$D/g.txt.gz" 2>> "$D/5-gzip.log" | cmp -s - "$D/orig/alfa.txt"; then
    ok "gzip di EX-OS: l'host lo apre e i byte tornano ($(stat -c %s "$D/orig/alfa.txt") -> $(stat -c %s "$D/g.txt.gz"))"
else
    no "il .gz di EX-OS non torna sull'host (vedi $D/5-gzip.log)"
fi
if cmp -s "$D/src/testo.txt" "$D/fuori.txt"; then
    ok "gunzip di EX-OS apre un gzip -9 dell'host"
else
    no "gunzip non apre il gzip dell'host (vedi $D/5-gzip.log)"
fi

# --- 6. Le due difese --------------------------------------------------------
echo "=== 6. il nome che scappa, e il CRC ==="
python3 -c "
import tarfile, io
t = tarfile.open('$D/cattivo.tar', 'w', format=tarfile.USTAR_FORMAT)
for nome, dati in (('../../boot/rubato.txt', b'questo non deve uscire\n'),
                   ('buono.txt', b'questo invece si\n')):
    i = tarfile.TarInfo(nome); i.size = len(dati)
    t.addfile(i, io.BytesIO(dati))
t.close()
d = bytearray(open('$D/fuori.txt.gz','rb').read())
d[len(d) - 6] ^= 0xFF          # dentro il CRC32 della coda
open('$D/rotto.gz','wb').write(bytes(d))
"
entra "$D/cattivo.tar" cattivo.tar
entra "$D/rotto.gz" rotto.gz

timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" \
    "mkdir /disk/difesa@1" "$TAR xf /disk/cattivo.tar -C /disk/difesa@8" \
    "ls /disk/difesa@3" \
    "$GUNZIP /disk/rotto.gz@8" \
    > "$D/6-difese.log" 2>&1

if grep -aq "skipped unsafe name" "$D/6-difese.log" && grep -aq "buono.txt" "$D/6-difese.log"; then
    ok "il nome che scappa e' rifiutato, il resto si estrae lo stesso"
else
    no "il nome fuori dalla destinazione non e' stato fermato"
fi
if grep -aq "CRC32 mismatch" "$D/6-difese.log"; then
    ok "il byte guastato e' riconosciuto invece che estratto"
else
    no "un .gz rovinato e' passato senza un lamento"
fi

echo ""
echo "  I registri sono in $D/*.log"
exit $esito
