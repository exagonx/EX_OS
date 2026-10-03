#!/bin/bash
# =============================================================================
# tools/prova_exide_vecchio.sh — un progetto di exide scritto prima
# dell'API inglese si apre, si salva e compila ancora (3 ottobre 2026)
#
# Il progetto si scrive a mano sul disco (i sorgenti in <progetto>/src), come lo lasciava l'exide di prima:
# classi italiane in finestra.dis («pulsante», «testo»), un handler
# Pulsante1_Clic in finestra.c che usa h_CasellaTesto1 ed ex_testo_metti, e
# un main con finestra_crea() ed ex_prendi_msg(). exide lo apre e Ctrl+S lo
# salva. Poi si guarda sull'host:
#
#   1. finestra.c: l'handler si chiama Pulsante1_Click, il corpo scritto
#      dall'utente c'e' ancora, e nessun gemello vuoto Pulsante1_Click in piu';
#   2. finestra.c.prima-inglese: la copia di prima;
#   3. finestra.h: gli alias h_CasellaTesto1 e finestra_crea;
#   4. finestra.c e finestra_gen.c compilano con gli header nuovi di lib/exwin.
#
# Solo tastiera: il mouse di QEMU e' relativo e sbaglia mira (vedi
# prova_exide_dove.sh).
#
#     tools/prova_exide_vecchio.sh [directory-di-lavoro]  (default /tmp/exos-ide-vecchio)
# =============================================================================
cd "$(dirname "$0")/.." || exit 1
D="${1:-/tmp/exos-ide-vecchio}"
mkdir -p "$D"
IMG="$D/prova-hd.img"
OFF="$IMG?offset=1048576"

[ -f dist/exos.iso ] || { echo "manca dist/exos.iso: lancia 'make iso-exos'" >&2; exit 1; }
SFDISK=$(command -v sfdisk || echo /usr/sbin/sfdisk)
DEBUGFS=$(command -v debugfs || echo /usr/sbin/debugfs)
[ -x "$SFDISK" ]  || { echo "manca sfdisk (util-linux)" >&2; exit 1; }
[ -x "$DEBUGFS" ] || { echo "manca debugfs (e2fsprogs)" >&2; exit 1; }

export EXOS_ISTANZA=idevecchio
export EXOS_NO_FLOPPY=1
export EXOS_CDROM=dist/exos.iso
export EXOS_QEMU_EXTRA="-drive file=$IMG,format=raw,if=ide"

esito=0
ok() { echo "  [OK]  $1"; }
no() { echo "  [NO]  $1"; esito=1; }

echo "=== 1. disco ext2, e il progetto scritto come lo lasciava l'exide di prima ==="
rm -f "$IMG"
qemu-img create -f raw "$IMG" 32M > /dev/null
printf 'label: dos\nunit: sectors\n\nstart=2048, size=63488, type=83, bootable\n' \
    | "$SFDISK" "$IMG" > /dev/null 2>&1
timeout 400 python3 tools/qemu_drive.py \
    "mkfs -t ext2 -L prova hd0p1@4" "si@40" \
    "mount hd0p1 /disk@6" "mkdir /disk/vecchio@2" "mkdir /disk/vecchio/src@2" "mkdir /disk/casa@2" \
    > "$D/1-disco.log" 2>&1

mkdir -p "$D/prima"
cat > "$D/prima/finestra.dis" <<'EOF'
# exide
F principale 320 200 Vecchio
c pulsante Pulsante1 101 20 20 90 26 0 Premi
c testo CasellaTesto1 102 20 60 140 22 0 ciao
EOF
cat > "$D/prima/finestra.c" <<'EOF'
/* Questo file e' TUO: exide ci aggiunge in fondo gli handler che
 * mancano e non riscrive mai quel che c'e' gia'.
 *
 * Gli id, i puntatori ai controlli e i prototipi degli handler
 * stanno in finestra.h, che invece si rigenera a ogni salvataggio.
 */

#include "libc.h"
#include "finestra.h"

int main(void)
{
    ExMsg m;

    finestra_crea();
    while (ex_prendi_msg(&m)) ex_smista(&m);
    return 0;
}

/* Pulsante1: Clic */
void Pulsante1_Clic(void)
{
    ex_testo_metti(h_CasellaTesto1, "premuto");
}
EOF
for f in finestra.dis finestra.c; do
    "$DEBUGFS" -w -R "write $D/prima/$f vecchio/src/$f" "$OFF" > /dev/null 2>&1
done
"$DEBUGFS" -R "ls -l vecchio/src" "$OFF" 2>/dev/null | grep -q finestra.dis \
    || { echo "NON RIUSCITO: il progetto non e' sul disco"; exit 1; }

echo "=== 2. exide lo apre e Ctrl+S lo salva ==="
timeout 400 python3 tools/qemu_drive.py \
    "mount hd0p1 /disk@6" "export HOME=/disk/casa@2" \
    "exwin@20" "key:alt-f1@3" \
    "/exwin/bin/exide /disk/vecchio &@14" "key:alt-f5@4" \
    "foto:$D/2-aperto.ppm@2" "key:ctrl-s@6" "foto:$D/2-salvato.ppm@2" \
    > "$D/2-exide.log" 2>&1

rm -rf "$D/dopo"; mkdir -p "$D/dopo"
for f in finestra.c finestra.c.prima-inglese finestra.h finestra_gen.c finestra.dis; do
    "$DEBUGFS" -R "dump vecchio/src/$f $D/dopo/$f" "$OFF" > /dev/null 2>&1
done

echo "=== 3. i file dopo il salvataggio ==="
C="$D/dopo/finestra.c"
if [ -s "$C" ]; then
    grep -q "void Pulsante1_Click(void)" "$C" && ok "l'handler si chiama Pulsante1_Click" \
                                               || no "manca void Pulsante1_Click(void) in finestra.c"
    grep -q "Pulsante1_Clic\b" "$C" && no "il vecchio Pulsante1_Clic e' ancora in finestra.c" \
                                    || ok "il vecchio nome non c'e' piu'"
    [ "$(grep -c 'void Pulsante1_Click(' "$C")" = 1 ] && ok "un handler solo, nessun gemello vuoto" \
                                                      || no "Pulsante1_Click compare piu' di una volta"
    grep -q '"premuto"' "$C" && ok "il corpo scritto dall'utente c'e' ancora" \
                             || no "il corpo dell'handler e' sparito"
else
    no "finestra.c non si legge dal disco"
fi
[ -s "$D/dopo/finestra.c.prima-inglese" ] && ok "c'e' la copia finestra.c.prima-inglese" \
                                          || no "manca finestra.c.prima-inglese"
H="$D/dopo/finestra.h"
grep -q "h_CasellaTesto1" "$H" 2>/dev/null && ok "finestra.h tiene l'alias h_CasellaTesto1" \
                                           || no "finestra.h senza l'alias h_CasellaTesto1"
grep -q "finestra_crea" "$H" 2>/dev/null && ok "finestra.h tiene l'alias finestra_crea" \
                                         || no "finestra.h senza l'alias finestra_crea"

echo "=== 4. compila con gli header nuovi ==="
for f in finestra.c finestra_gen.c; do
    if [ -s "$D/dopo/$f" ] && gcc -m32 -ffreestanding -fno-builtin -std=c11 -Wall -nostdlib \
            -I "$D/dopo" -I lib/include -I lib/exwin -I drivers/wserver -I drivers/kbd \
            -fsyntax-only "$D/dopo/$f" > "$D/4-$f.log" 2>&1; then
        if [ -s "$D/4-$f.log" ]; then no "$f compila ma con avvisi (vedi $D/4-$f.log)"
        else ok "$f compila senza avvisi"; fi
    else
        no "$f non compila (vedi $D/4-$f.log)"
    fi
done

echo
[ $esito = 0 ] && echo "  prova_exide_vecchio: TUTTO BENE" \
               || echo "  prova_exide_vecchio: qualcosa non va (fotografie e file in $D)"
exit $esito
